#include "Physics/Physics.hpp"

#include <bit>

#include "Game/Component.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <thread>
#include <numeric>
#include <unordered_set>

namespace molten {

namespace {
const float kPenetrationSlop = 0.005f;      // overlap we tolerate, to keep resting contacts stable
const float kCorrectionFraction = 0.8f;     // how much of the remaining overlap is removed per step
const float kBounceThreshold = 1.0f;        // approach speeds below this do not bounce
// Sleeping: slower than this, for this long, and a body is left out of the steps until woken.
const float kSleepSpeed = 0.15f;            // meters a second (a stack's resting jitter is below it)
const float kSleepSpin = 0.25f;             // radians a second
const float kSleepAfter = 0.6f;             // seconds
const size_t kPairsPerThread = 192;         // fewer pairs than this for each thread: not worth starting one
const float kWalkableNormalY = 0.65f;       // surfaces flatter than about 50 degrees count as ground

/// One over how hard a body of this shape is to turn about each of its own axes.
Vec3 inverseInertiaOf(const Collider& collider, float mass) {
    Vec3 inertia;
    switch (collider.shape) {
        case ColliderShape::Sphere: inertia = Vec3{1, 1, 1} * (0.4f * collider.radius * collider.radius); break;
        case ColliderShape::Capsule: {
            float r = collider.radius, h = collider.halfHeight * 2 + r;      // as a cylinder a little longer than its middle
            float across = r * r / 4 + h * h / 12;
            inertia = Vec3{across, r * r / 2, across};
            break;
        }
        case ColliderShape::Mesh:
            if (collider.isConvex && collider.convex) {
                // As the box around it: near enough for how it turns.
                Vec3 e = (collider.convex->bounds.max - collider.convex->bounds.min) * 0.5f;
                inertia = Vec3{e.y * e.y + e.z * e.z, e.x * e.x + e.z * e.z, e.x * e.x + e.y * e.y} / 3.0f;
                break;
            }
            [[fallthrough]];
        default: {
            Vec3 e = collider.halfExtents;
            inertia = Vec3{e.y * e.y + e.z * e.z, e.x * e.x + e.z * e.z, e.x * e.x + e.y * e.y} / 3.0f;
        }
    }
    inertia = simd::max(inertia, Vec3{1e-5f, 1e-5f, 1e-5f}) * std::max(mass, 1e-4f);
    return Vec3{1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z};
}

/// How fast a twist (an impulse of turning, world axes) sets a body turning.
template <typename ProxyType>
Vec3 turnFrom(const ProxyType& proxy, Vec3 twist) {
    if (!proxy.body || simd::length_squared(proxy.inverseInertia) <= 0) return Vec3{0, 0, 0};
    Quat q = proxy.body->rotation;
    return rotate(q, rotate(simd::conjugate(q), twist) * proxy.inverseInertia);
}

}  // namespace

/// Where a collider without a body of its own stands: in the world, under whatever it is under.
void worldPlace(const Transform& transform, Vec3& position, Quat& rotation) {
    if (!transform.parent.isValid()) {
        position = transform.position;
        rotation = transform.rotation;
        return;
    }
    position = transform.worldPosition();
    Vec3 x = xyz(transform.world.columns[0]), y = xyz(transform.world.columns[1]), z = xyz(transform.world.columns[2]);
    float sx = simd::length(x), sy = simd::length(y), sz = simd::length(z);
    if (sx < 1e-8f || sy < 1e-8f || sz < 1e-8f) {
        rotation = transform.rotation;
        return;
    }
    rotation = simd::normalize(Quat(Mat3(x / sx, y / sy, z / sz)));
}

ShapeInstance Physics::makeShape(const Collider& collider, Vec3 position, Quat rotation) {
    ShapeInstance shape;
    shape.shape = collider.shape;
    // A mesh collider is the model's own points, where the model is: no offset.
    shape.center = position + rotate(rotation, collider.shape == ColliderShape::Mesh ? Vec3{0, 0, 0} : collider.offset);
    shape.rotation = rotation;
    shape.halfExtents = collider.halfExtents;
    shape.radius = collider.radius;
    shape.halfHeight = collider.halfHeight;
    shape.mesh = collider.mesh.get();
    if (collider.shape == ColliderShape::Mesh && collider.isConvex) {
        shape.shape = ColliderShape::Convex;
        shape.convex = collider.convex.get();
        shape.mesh = nullptr;
    }
    return shape;
}

bool Physics::isAwake(const Proxy& proxy) {
    return proxy.body && proxy.body->type != BodyType::Static && !proxy.body->isSleeping;
}

/// The exact test of every pair the broad phase kept. Each pair reads only its two shapes, so
/// with many pairs they are shared among threads (each its own stretch of the list); with a few,
/// starting threads would cost more than it saves.
void Physics::narrowPhase() {
    auto test = [this](size_t from, size_t to) {
        for (size_t k = from; k < to; k++) {
            Candidate& candidate = candidates_[k];
            candidate.info = ContactInfo{};
            candidate.touches = collide(proxies_[order_[candidate.first]].shape, proxies_[order_[candidate.second]].shape, candidate.info);
        }
    };
    const size_t count = candidates_.size();
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const size_t threads = std::min<size_t>(std::min(cores, 8u), count / kPairsPerThread);
    if (threads <= 1) {
        test(0, count);
        return;
    }
    std::vector<std::thread> helpers;
    const size_t share = (count + threads - 1) / threads;
    for (size_t t = 1; t < threads; t++) helpers.emplace_back(test, t * share, std::min(count, (t + 1) * share));
    test(0, std::min(count, share));
    for (std::thread& helper : helpers) helper.join();
}

uint64_t Physics::pairKey(Entity a, Entity b) {
    uint32_t low = std::min(a.index, b.index), high = std::max(a.index, b.index);
    return (uint64_t(high) << 32) | uint64_t(low);
}

void Physics::step(Scene& scene, float dt) {
    rayShapesReady_ = false;                    // bodies are about to move
    // 0. What lay on something that is gone (deleted, switched off, its collider taken away) wakes and falls.
    for (const auto& [key, pair] : touching_) {
        for (int side = 0; side < 2; side++) {
            Entity gone = side ? pair.second : pair.first, other = side ? pair.first : pair.second;
            const Transform* place = scene.get<Transform>(gone);
            if (scene.isAlive(gone) && scene.has<Collider>(gone) && place && place->isActiveInHierarchy) continue;
            if (RigidBody* body = scene.get<RigidBody>(other)) body->wake();
        }
    }
    // 1. Integrate: velocity from forces, then position from velocity (semi-implicit Euler).
    scene.each<RigidBody, Transform>([&](Entity, RigidBody& body, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!body.isInitialized) {
            body.position = transform.position;
            body.writtenPosition = transform.position;
            body.rotation = body.writtenRotation = transform.rotation;
            body.isInitialized = true;
        } else if (simd::length_squared(transform.position - body.writtenPosition) > 1e-10f) {
            // Moved by hand: an editor's tools, the inspector, the game's own code. The body goes where it was put.
            body.position = transform.position;
            body.writtenPosition = transform.position;
            body.wake();
        }
        // And turned by hand; a body that does not turn by itself always faces as its Transform says.
        if (!body.rotates || std::abs(simd::dot(transform.rotation.vector, body.writtenRotation.vector)) < 0.9999995f) {
            if (body.rotates) body.wake();
            body.rotation = body.writtenRotation = transform.rotation;
        }
        body.previousPosition = body.position;
        body.previousRotation = body.rotation;
        // Asleep with a velocity: the game gave it one (a sleeping body has none). It wakes.
        if (body.isSleeping && (simd::length_squared(body.velocity) > 0 || simd::length_squared(body.angularVelocity) > 0)) body.wake();
        if (body.isSleeping || body.type != BodyType::Dynamic) body.isSleeping = body.isSleeping && body.type == BodyType::Dynamic;
        if (body.isSleeping) return;                                // where it lies, as it lies: standing on what it stood on
        body.isGrounded = false;
        if (body.type == BodyType::Dynamic) {
            body.velocity += gravity * (body.gravityScale * dt);
            body.velocity *= 1.0f / (1.0f + body.linearDamping * dt);
            body.angularVelocity *= 1.0f / (1.0f + body.angularDamping * dt);
        }
        if (body.type != BodyType::Static) {
            body.position += body.velocity * dt;
            if (body.rotates) {
                Vec3 turn = body.angularVelocity * dt;
                float angle = simd::length(turn);
                if (angle > 1e-7f) body.rotation = simd::normalize(Quat(angle, turn / angle) * body.rotation);
            }
        }
    });

    // 2. Place every collider in the world.
    proxies_.clear();
    std::unordered_map<uint64_t, Vec3> placesNow;
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        Proxy proxy;
        proxy.entity = entity;
        proxy.collider = &collider;
        proxy.body = scene.get<RigidBody>(entity);
        Vec3 position;
        Quat rotation;
        if (proxy.body) {
            position = proxy.body->position, rotation = proxy.body->rotation;
        } else {
            worldPlace(transform, position, rotation);
            // Without a body it does not move by physics; if it is not where it was, something else
            // moved it. (Seen for the first time, it is taken to have been there all along.)
            uint64_t key = (uint64_t(entity.index) << 32) | entity.generation;
            auto last = lastPlaces_.find(key);
            proxy.movedItself = last != lastPlaces_.end() && simd::distance_squared(last->second, position) > 1e-10f;
            placesNow[key] = position;
        }
        proxy.shape = makeShape(collider, position, rotation);
        proxy.bounds = proxy.shape.bounds();
        if (proxy.body && proxy.body->type == BodyType::Dynamic) {
            proxy.inverseMass = 1.0f / std::max(proxy.body->mass, 1e-4f);
            if (proxy.body->rotates) proxy.inverseInertia = inverseInertiaOf(collider, proxy.body->mass);
        }
        proxies_.push_back(proxy);
    });

    // 3. Broad phase, "sort and sweep": order the boxes along x, then each box only
    //    needs to be compared with the ones that start before it ends.
    order_.resize(proxies_.size());
    std::iota(order_.begin(), order_.end(), 0u);
    std::sort(order_.begin(), order_.end(), [&](uint32_t l, uint32_t r) {
        return proxies_[l].bounds.min.x < proxies_[r].bounds.min.x;
    });

    lastPlaces_ = std::move(placesNow);

    contacts_.clear();
    contactProxies_.clear();
    std::unordered_map<uint64_t, std::pair<Entity, Entity>> touchingNow, triggeringNow;
    // What moves in this step, by any means: a pair of which neither does is not looked at.
    std::unordered_map<uint64_t, bool> movesNow;
    for (const Proxy& proxy : proxies_) {
        movesNow[(uint64_t(proxy.entity.index) << 32) | proxy.entity.generation] = isAwake(proxy) || proxy.movedItself;
    }

    // The pairs worth an exact look: boxes that overlap, layers that meet, at least one of them moving.
    candidates_.clear();
    for (size_t i = 0; i < order_.size(); i++) {
        Proxy& a = proxies_[order_[i]];
        for (size_t j = i + 1; j < order_.size(); j++) {
            Proxy& b = proxies_[order_[j]];
            if (b.bounds.min.x > a.bounds.max.x) break;             // nothing further along can overlap a
            if (!a.bounds.intersects(b.bounds)) continue;
            if (!(a.collider->layer & b.collider->mask) || !(b.collider->layer & a.collider->mask)) continue;
            // The project's own table of which layers meet (Project Settings > Physics).
            if (!(layerMeets[a.collider->layer ? size_t(std::countr_zero(a.collider->layer)) : 0] & b.collider->layer)) continue;
            bool aMoves = isAwake(a) || a.movedItself;
            bool bMoves = isAwake(b) || b.movedItself;
            if (!aMoves && !bMoves) continue;                       // two things that stay put (or sleep) never need testing
            candidates_.push_back(Candidate{uint32_t(i), uint32_t(j)});
        }
    }

    // 4. Narrow phase: the exact shapes. Each pair is on its own, so with many of them they are
    //    shared among the processor's cores; what they found is then gone through in order.
    narrowPhase();

    for (const Candidate& candidate : candidates_) {
        if (!candidate.touches) continue;
        const size_t i = candidate.first, j = candidate.second;
        Proxy& a = proxies_[order_[i]];
        Proxy& b = proxies_[order_[j]];
        const ContactInfo& info = candidate.info;
        if (a.collider->isTrigger || b.collider->isTrigger) {
            bool aIsTrigger = a.collider->isTrigger;
            triggeringNow[pairKey(a.entity, b.entity)] = aIsTrigger ? std::make_pair(a.entity, b.entity) : std::make_pair(b.entity, a.entity);
            continue;
        }
        if (a.inverseMass + b.inverseMass <= 0) continue;           // kinematic against static: nothing to solve
        // A sleeper touched by something that moves wakes up, and is solved as awake from now.
        if (a.body && a.body->isSleeping) a.body->wake();
        if (b.body && b.body->isSleeping) b.body->wake();

        Vec3 velocityA = a.body ? a.body->velocity : Vec3{0, 0, 0};
        Vec3 velocityB = b.body ? b.body->velocity : Vec3{0, 0, 0};
        Contact contact;
        contact.a = a.entity;
        contact.b = b.entity;
        contact.point = info.point;
        contact.normal = info.normal;
        contact.depth = info.depth;
        contact.impactSpeed = std::max(0.0f, -simd::dot(velocityB - velocityA, info.normal));
        if (info.pointCount > 1) {
            // One contact for each point where they touch: together they hold a box flat.
            for (int point = 0; point < info.pointCount; point++) {
                contact.point = info.points[point];
                contact.depth = info.depths[point];
                contact.share = 1.0f / float(info.pointCount);
                contacts_.push_back(contact);
                contactProxies_.push_back({order_[i], order_[j]});
            }
        } else {
            contacts_.push_back(contact);
            contactProxies_.push_back({order_[i], order_[j]});
        }
        touchingNow[pairKey(a.entity, b.entity)] = {a.entity, b.entity};
    }
    // Two that touched and now both stay put (asleep, or never moving) still touch: not looked at,
    // they must not seem to part and meet again when one of them wakes.
    for (const auto& [key, pair] : touching_) {
        if (touchingNow.count(key)) continue;
        auto first = movesNow.find((uint64_t(pair.first.index) << 32) | pair.first.generation);
        auto second = movesNow.find((uint64_t(pair.second.index) << 32) | pair.second.generation);
        if (first != movesNow.end() && second != movesNow.end() && !first->second && !second->second) touchingNow[key] = pair;
    }

    // 5. Resolve velocities with impulses, at the point of contact: a push there moves a
    //    body and, off-center, turns it. Several passes let contacts that share a body,
    //    like a ball in a corner, settle against each other; joints are solved among them.
    for (int iteration = 0; iteration < solverIterations; iteration++) {
        for (size_t c = 0; c < contacts_.size(); c++) {
            const Contact& contact = contacts_[c];
            Proxy& a = proxies_[contactProxies_[c].first];
            Proxy& b = proxies_[contactProxies_[c].second];
            Vec3 armA = a.body ? contact.point - a.body->position : Vec3{0, 0, 0};
            Vec3 armB = b.body ? contact.point - b.body->position : Vec3{0, 0, 0};
            auto velocityAt = [](const Proxy& proxy, Vec3 arm) {
                return proxy.body ? proxy.body->velocity + simd::cross(proxy.body->angularVelocity, arm) : Vec3{0, 0, 0};
            };
            /// How much the two give way to a push along a direction at the contact: the more, the lighter and the easier to turn.
            auto give = [&](Vec3 direction) {
                return a.inverseMass + b.inverseMass + simd::dot(direction, simd::cross(turnFrom(a, simd::cross(armA, direction)), armA)) +
                       simd::dot(direction, simd::cross(turnFrom(b, simd::cross(armB, direction)), armB));
            };
            auto push = [&](Vec3 impulse) {
                if (a.inverseMass > 0) {
                    a.body->velocity -= impulse * a.inverseMass;
                    a.body->angularVelocity -= turnFrom(a, simd::cross(armA, impulse));
                }
                if (b.inverseMass > 0) {
                    b.body->velocity += impulse * b.inverseMass;
                    b.body->angularVelocity += turnFrom(b, simd::cross(armB, impulse));
                }
            };
            Vec3 relative = velocityAt(b, armB) - velocityAt(a, armA);
            float normalSpeed = simd::dot(relative, contact.normal);
            if (normalSpeed >= 0) continue;                         // already separating

            float restitutionA = a.body ? a.body->restitution : 0.0f;
            float restitutionB = b.body ? b.body->restitution : 0.0f;
            float bounce = -normalSpeed > kBounceThreshold ? std::max(restitutionA, restitutionB) : 0.0f;
            float normalImpulse = -(1.0f + bounce) * normalSpeed / give(contact.normal);
            push(contact.normal * normalImpulse);

            // Friction acts along the surface, against the sliding direction, and can
            // never be stronger than the friction coefficient times the normal impulse.
            relative = velocityAt(b, armB) - velocityAt(a, armA);
            Vec3 tangent = relative - contact.normal * simd::dot(relative, contact.normal);
            float slideSpeed = simd::length(tangent);
            if (slideSpeed > 1e-5f) {
                tangent /= slideSpeed;
                float frictionA = a.body ? a.body->friction : 0.6f;
                float frictionB = b.body ? b.body->friction : 0.6f;
                float coefficient = std::sqrt(frictionA * frictionB);
                float frictionImpulse = std::min(slideSpeed / give(tangent), coefficient * normalImpulse);
                push(tangent * -frictionImpulse);
            }
        }
        solveJoints(scene, dt);
    }

    // 6. Remove what is left of the overlap by moving the bodies apart, and note who
    //    is standing on what.
    for (size_t c = 0; c < contacts_.size(); c++) {
        const Contact& contact = contacts_[c];
        Proxy& a = proxies_[contactProxies_[c].first];
        Proxy& b = proxies_[contactProxies_[c].second];
        float inverseMassSum = a.inverseMass + b.inverseMass;
        float amount = std::max(contact.depth - kPenetrationSlop, 0.0f) * kCorrectionFraction * contact.share / inverseMassSum;
        if (a.inverseMass > 0) a.body->position -= contact.normal * (amount * a.inverseMass);
        if (b.inverseMass > 0) b.body->position += contact.normal * (amount * b.inverseMass);

        if (b.inverseMass > 0 && contact.normal.y > kWalkableNormalY) {
            b.body->isGrounded = true;
            b.body->groundNormal = contact.normal;
        }
        if (a.inverseMass > 0 && contact.normal.y < -kWalkableNormalY) {
            a.body->isGrounded = true;
            a.body->groundNormal = -contact.normal;
        }
    }

    // 6b. Who falls asleep, and who was woken by what happened in this step (pushed by a contact
    //     or a joint, moved apart from what it sank into).
    scene.each<RigidBody, Transform>([&](Entity entity, RigidBody& body, Transform& transform) {
        if (!transform.isActiveInHierarchy || body.type != BodyType::Dynamic || !body.isInitialized) return;
        const float speed = simd::length_squared(body.velocity), spin = simd::length_squared(body.angularVelocity);
        if (body.isSleeping) {
            if (speed > 0 || spin > 0 || simd::distance_squared(body.position, body.previousPosition) > 1e-12f) body.wake();
            return;
        }
        // A body on a joint stays awake: what it hangs from or holds would not wake it.
        if (!body.canSleep || scene.has<Joint>(entity)) {
            body.stillFor = 0;
            return;
        }
        body.stillFor = speed < kSleepSpeed * kSleepSpeed && spin < kSleepSpin * kSleepSpin ? body.stillFor + dt : 0.0f;
    });
    // Those that touch fall asleep together or not at all: a crate under one still sliding must
    // not sleep (it would only be woken at once). One ready beside one that is not waits, and so
    // in turn do those beside it (a few times over, for a stack).
    std::unordered_set<uint64_t> waiting;
    auto keyOf = [](Entity entity) { return (uint64_t(entity.index) << 32) | entity.generation; };
    auto isReady = [&](Entity entity) {
        const RigidBody* body = scene.get<RigidBody>(entity);
        if (!body || body->type != BodyType::Dynamic || body->isSleeping) return true;
        return body->stillFor >= kSleepAfter && !waiting.count(keyOf(entity));
    };
    for (int pass = 0; pass < 4; pass++) {
        bool changed = false;
        for (const auto& [key, pair] : touchingNow) {
            const bool first = isReady(pair.first), second = isReady(pair.second);
            if (first == second) continue;
            const Entity ready = first ? pair.first : pair.second;
            const RigidBody* body = scene.get<RigidBody>(ready);
            if (body && body->type == BodyType::Dynamic && !body->isSleeping) changed |= waiting.insert(keyOf(ready)).second;
        }
        if (!changed) break;
    }
    scene.each<RigidBody, Transform>([&](Entity entity, RigidBody& body, Transform& transform) {
        if (!transform.isActiveInHierarchy || body.type != BodyType::Dynamic || body.isSleeping || !isReady(entity)) return;
        body.isSleeping = true;
        body.velocity = Vec3{0, 0, 0};
        body.angularVelocity = Vec3{0, 0, 0};
        body.previousPosition = body.position;
        body.previousRotation = body.rotation;
    });

    // 7. Events. Collected first and sent last, so a callback can change the scene freely.
    std::vector<Contact> began;
    std::unordered_map<uint64_t, bool> noted;
    for (const Contact& contact : contacts_) {
        uint64_t key = pairKey(contact.a, contact.b);
        if (!touching_.count(key) && !noted[key]) began.push_back(contact);       // once for a pair, however many points it touches at
        noted[key] = true;
    }
    std::vector<std::pair<Entity, Entity>> entered, exited;
    // In a trigger and standing still: it has not left. (Such a pair was not looked at above.)
    for (const auto& [key, pair] : triggering_) {
        if (triggeringNow.count(key)) continue;
        auto first = movesNow.find((uint64_t(pair.first.index) << 32) | pair.first.generation);
        auto second = movesNow.find((uint64_t(pair.second.index) << 32) | pair.second.generation);
        if (first != movesNow.end() && second != movesNow.end() && !first->second && !second->second) triggeringNow[key] = pair;
    }
    for (const auto& [key, pair] : triggeringNow) {
        if (!triggering_.count(key)) entered.push_back(pair);
    }
    for (const auto& [key, pair] : triggering_) {
        if (!triggeringNow.count(key)) exited.push_back(pair);
    }
    touching_ = std::move(touchingNow);
    triggering_ = std::move(triggeringNow);

    begunContacts_.insert(begunContacts_.end(), began.begin(), began.end());
    enteredTriggers_.insert(enteredTriggers_.end(), entered.begin(), entered.end());
    if (onContactBegin) for (const Contact& contact : began) onContactBegin(contact);
    if (onTriggerEnter) for (const auto& pair : entered) onTriggerEnter(pair.first, pair.second);
    if (onTriggerExit) for (const auto& pair : exited) onTriggerExit(pair.first, pair.second);
}

void Physics::writeTransforms(Scene& scene, float alpha) {
    scene.each<RigidBody, Transform>([&](Entity, RigidBody& body, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!body.isInitialized) return;
        transform.position = lerp(body.previousPosition, body.position, alpha);
        body.writtenPosition = transform.position;
        if (body.rotates) {
            transform.rotation = simd::slerp(body.previousRotation, body.rotation, alpha);
            body.writtenRotation = transform.rotation;
        }
    });
}

void Physics::teleport(Scene& scene, Entity entity, Vec3 position) {
    if (Transform* transform = scene.get<Transform>(entity)) transform->position = position;
    if (RigidBody* body = scene.get<RigidBody>(entity)) {
        body->position = position;
        body->previousPosition = position;                         // no blending across the jump
        body->writtenPosition = position;
        body->isInitialized = true;
    }
}

bool Physics::isUnder(Scene& scene, Entity entity, Entity ancestor) {
    const Transform* up = scene.get<Transform>(entity);
    for (int depth = 0; up && up->parent.isValid() && depth < 64; depth++) {
        if (up->parent == ancestor) return true;
        up = scene.get<Transform>(up->parent);
    }
    return false;
}

void Physics::prepareRayShapes(Scene& scene) {
    size_t colliders = scene.count<Collider>();
    if (rayShapesReady_ && rayShapesStamp_ == scene.transformStamp() && rayShapesColliders_ == colliders) return;
    rayShapes_.clear();
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (!transform.isActiveInHierarchy || collider.isTrigger) return;
        const RigidBody* body = scene.get<RigidBody>(entity);
        Vec3 position;
        Quat rotation;
        if (body && body->isInitialized) position = body->position, rotation = body->rotation;
        else worldPlace(transform, position, rotation);
        RayShape cached;
        cached.entity = entity;
        cached.shape = makeShape(collider, position, rotation);
        AABB box = cached.shape.bounds();
        cached.low = box.min - Vec3{0.01f, 0.01f, 0.01f};
        cached.high = box.max + Vec3{0.01f, 0.01f, 0.01f};
        cached.layer = collider.layer;
        rayShapes_.push_back(cached);
    });
    rayShapesReady_ = true;
    rayShapesStamp_ = scene.transformStamp();
    rayShapesColliders_ = colliders;
}

std::optional<RaycastHit> Physics::raycast(Scene& scene, const Ray& ray, float maxDistance, uint32_t mask, Entity ignore) {
    prepareRayShapes(scene);
    std::optional<RaycastHit> closest;
    float reach = maxDistance;
    for (const RayShape& cached : rayShapes_) {
        if (!(cached.layer & mask) || cached.entity == ignore) continue;
        // The box first: where along the ray it enters and leaves each pair of its sides.
        float enter = 0, leave = reach;
        bool misses = false;
        for (int axis = 0; axis < 3 && !misses; axis++) {
            float origin = ray.origin[axis], direction = ray.direction[axis];
            if (std::abs(direction) < 1e-9f) {
                misses = origin < cached.low[axis] || origin > cached.high[axis];
                continue;
            }
            float a = (cached.low[axis] - origin) / direction, b = (cached.high[axis] - origin) / direction;
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a);
            leave = std::min(leave, b);
            misses = enter > leave;
        }
        if (misses || !scene.isAlive(cached.entity)) continue;
        // What is under the one ignored is ignored too: a car's own body, under the car.
        if (ignore.isValid() && isUnder(scene, cached.entity, ignore)) continue;
        auto hit = raycastShape(ray, cached.shape);
        if (!hit || hit->distance > reach) continue;
        reach = hit->distance;
        closest = RaycastHit{cached.entity, ray.at(hit->distance), hit->normal, hit->distance};
    }
    return closest;
}

std::vector<Entity> Physics::overlapSphere(Scene& scene, Vec3 center, float radius, uint32_t mask) {
    std::vector<Entity> found;
    ShapeInstance probe;
    probe.shape = ColliderShape::Sphere;
    probe.center = center;
    probe.radius = radius;
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!(collider.layer & mask)) return;
        const RigidBody* body = scene.get<RigidBody>(entity);
        Vec3 position;
        Quat rotation;
        if (body && body->isInitialized) position = body->position, rotation = body->rotation;
        else worldPlace(transform, position, rotation);
        ContactInfo info;
        if (collide(probe, makeShape(collider, position, rotation), info)) found.push_back(entity);
    });
    return found;
}

void RigidBody::applyImpulse(Vec3 impulse, Vec3 worldPoint) {
    if (type != BodyType::Dynamic) return;
    wake();
    velocity += impulse / std::max(mass, 1e-4f);
    // Without its collider the body's shape is not known here: it turns as a ball of half a meter would.
    if (rotates) angularVelocity += simd::cross(worldPoint - position, impulse) / (0.4f * 0.25f * std::max(mass, 1e-4f));
}

// ---------------------------------------------------------------- joints

namespace {

/// One end of a joint: a body, or the world, which nothing moves.
struct End {
    RigidBody* body = nullptr;
    float inverseMass = 0;
    Vec3 inverseInertia{0, 0, 0};
    Vec3 position{0, 0, 0};
    Quat rotation{};

    Vec3 turnFrom(Vec3 twist) const {
        if (!body || simd::length_squared(inverseInertia) <= 0) return Vec3{0, 0, 0};
        return rotate(rotation, rotate(simd::conjugate(rotation), twist) * inverseInertia);
    }
    Vec3 velocityAt(Vec3 arm) const { return body ? body->velocity + simd::cross(body->angularVelocity, arm) : Vec3{0, 0, 0}; }
    Vec3 angularVelocity() const { return body ? body->angularVelocity : Vec3{0, 0, 0}; }
    void push(Vec3 impulse, Vec3 arm) const {
        if (inverseMass <= 0) return;
        body->velocity += impulse * inverseMass;
        body->angularVelocity += turnFrom(simd::cross(arm, impulse));
    }
    void twist(Vec3 angularImpulse) const {
        if (inverseMass > 0) body->angularVelocity += turnFrom(angularImpulse);
    }
};

End endOf(Scene& scene, Entity entity) {
    End end;
    const Transform* transform = scene.get<Transform>(entity);
    if (!transform) return end;
    end.position = transform->worldPosition();
    end.rotation = transform->rotation;
    RigidBody* body = scene.get<RigidBody>(entity);
    if (!body || !body->isInitialized) return end;
    end.position = body->position;
    end.rotation = body->rotation;
    if (body->type != BodyType::Static) end.body = body;
    if (body->type == BodyType::Dynamic) {
        end.inverseMass = 1.0f / std::max(body->mass, 1e-4f);
        const Collider* collider = scene.get<Collider>(entity);
        if (body->rotates) end.inverseInertia = inverseInertiaOf(collider ? *collider : Collider{}, body->mass);
    }
    return end;
}

}  // namespace

void Physics::solveJoints(Scene& scene, float dt) {
    const float pull = 0.2f / std::max(dt, 1e-5f);              // how much of what is out of place is put right each step
    scene.each<Joint, Transform>([&](Entity entity, Joint& joint, Transform& transform) {
        if (joint.isBroken || !transform.isActiveInHierarchy) return;
        End a = endOf(scene, entity);
        if (!a.body) return;
        if (!joint.isSet) {
            joint.other = Entity{};
            if (!joint.connectedTo.empty()) {
                scene.each<Name, Transform>([&](Entity candidate, Name& name, Transform&) {
                    if (!joint.other.isValid() && candidate != entity && name.value == joint.connectedTo) joint.other = candidate;
                });
            }
            End b = joint.other.isValid() ? endOf(scene, joint.other) : End{};
            Vec3 point = a.position + rotate(a.rotation, joint.anchor);
            bool isDistance = joint.kind == "distance";
            // A rope or a rod runs to the middle of the other body; the rest meet at the anchor.
            joint.otherAnchor = isDistance && joint.other.isValid() ? Vec3{0, 0, 0} : rotate(simd::conjugate(b.rotation), point - b.position);
            if (isDistance && joint.length <= 0) joint.length = simd::length(b.position + rotate(b.rotation, joint.otherAnchor) - point);
            joint.restRelative = simd::normalize(simd::conjugate(b.rotation) * a.rotation);
            joint.otherAxis = rotate(simd::conjugate(b.rotation), rotate(a.rotation, safeNormalize(joint.axis, Vec3{0, 1, 0})));
            joint.isSet = true;
        }
        End b = joint.other.isValid() ? endOf(scene, joint.other) : End{};
        Vec3 armA = rotate(a.rotation, joint.anchor), armB = rotate(b.rotation, joint.otherAnchor);
        Vec3 apart = b.position + armB - (a.position + armA);
        auto give = [&](Vec3 direction) {
            return a.inverseMass + b.inverseMass + simd::dot(direction, simd::cross(a.turnFrom(simd::cross(armA, direction)), armA)) +
                   simd::dot(direction, simd::cross(b.turnFrom(simd::cross(armB, direction)), armB));
        };
        float strongest = 0;
        // Along a direction: the two ends are given the speed toward each other that closes what is asked.
        auto along = [&](Vec3 direction, float error, bool onlyPulls) {
            float k = give(direction);
            if (k <= 1e-9f) return;
            float speed = simd::dot(b.velocityAt(armB) - a.velocityAt(armA), direction);
            float impulse = (-pull * error - speed) / k;
            if (onlyPulls) impulse = std::min(impulse, 0.0f);
            b.push(direction * impulse, armB);
            a.push(direction * -impulse, armA);
            strongest = std::max(strongest, std::abs(impulse));
        };
        if (joint.kind == "distance") {
            float distance = simd::length(apart);
            if (distance > 1e-5f && !(joint.isRope && distance < joint.length)) along(apart / distance, distance - joint.length, joint.isRope);
        } else {
            along(Vec3{1, 0, 0}, apart.x, false);
            along(Vec3{0, 1, 0}, apart.y, false);
            along(Vec3{0, 0, 1}, apart.z, false);
        }
        // How the two are turned to each other.
        Vec3 wanted{0, 0, 0};
        bool turns = false;
        Vec3 relative = b.angularVelocity() - a.angularVelocity();
        if (joint.kind == "hinge") {
            // Only about the axis: any other turning between them is taken away, and the axes are brought back together.
            Vec3 axisA = rotate(a.rotation, safeNormalize(joint.axis, Vec3{0, 1, 0})), axisB = rotate(b.rotation, joint.otherAxis);
            Vec3 across = relative - axisA * simd::dot(relative, axisA);
            wanted = simd::cross(axisA, axisB) * -pull - across;
            turns = true;
        } else if (joint.kind == "ball" && joint.swingLimit > 0) {
            // Free within the limit; past it, brought back and kept from going further.
            Quat error = simd::normalize(b.rotation * joint.restRelative * simd::conjugate(a.rotation));
            Vec3 axis = error.vector.xyz * (error.vector.w < 0 ? -1.0f : 1.0f);
            float half = simd::length(axis);
            float angle = 2 * std::asin(std::min(half, 1.0f));
            float limit = radians(joint.swingLimit);
            if (half > 1e-6f && angle > limit) {
                axis /= half;
                float going = simd::dot(relative, axis);
                wanted = axis * ((angle - limit) * -pull - std::max(going, 0.0f));
                turns = true;
            }
        } else if (joint.kind == "fixed") {
            Quat error = simd::normalize(b.rotation * joint.restRelative * simd::conjugate(a.rotation));
            Vec3 turn = error.vector.xyz * (error.vector.w < 0 ? -2.0f : 2.0f);
            wanted = turn * -pull - relative;
            turns = true;
        }
        float amount = simd::length(wanted);
        if (turns && amount > 1e-6f) {
            Vec3 direction = wanted / amount;
            float k = simd::dot(direction, a.turnFrom(direction) + b.turnFrom(direction));
            if (k > 1e-9f) {
                b.twist(direction * (amount / k));
                a.twist(direction * (-amount / k));
            }
        }
        // Pulled harder than it can bear, it gives way.
        if (joint.breakForce > 0 && strongest / std::max(dt, 1e-5f) > joint.breakForce) joint.isBroken = true;
    });
}

MOLTEN_COMPONENT(Joint, MOLTEN_FIELD(connectedTo).picks("entity").help("The name of the other entity. Empty ties it to where it is in the world"),
                 MOLTEN_FIELD(kind).choices({"ball", "hinge", "fixed", "distance"}), MOLTEN_FIELD(anchor).help("Where on this body, in its own axes"),
                 MOLTEN_FIELD(axis).help("Hinge: the axis it turns about"), MOLTEN_FIELD(length).help("Distance: 0 keeps the length there is at the start"),
                 MOLTEN_FIELD(isRope).help("Distance: only pulls, as a rope. Off: a rod, which also pushes"),
                 MOLTEN_FIELD(breakForce).help("Above zero: gives way when pulled harder than this, in newtons"),
                 MOLTEN_FIELD(swingLimit).range(0, 180).help("Ball: the most it bends from how it started, in degrees. 0: as far as it likes"))

}  // namespace molten
