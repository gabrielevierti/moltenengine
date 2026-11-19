#include "Physics/Physics.hpp"

#include <algorithm>
#include <numeric>

namespace molten {

namespace {
const float kPenetrationSlop = 0.005f;      // overlap we tolerate, to keep resting contacts stable
const float kCorrectionFraction = 0.8f;     // how much of the remaining overlap is removed per step
const float kBounceThreshold = 1.0f;        // approach speeds below this do not bounce
const float kWalkableNormalY = 0.65f;       // surfaces flatter than about 50 degrees count as ground
}

ShapeInstance Physics::makeShape(const Collider& collider, Vec3 position, Quat rotation) {
    ShapeInstance shape;
    shape.shape = collider.shape;
    shape.center = position + rotate(rotation, collider.offset);
    shape.rotation = rotation;
    shape.halfExtents = collider.halfExtents;
    shape.radius = collider.radius;
    shape.halfHeight = collider.halfHeight;
    return shape;
}

uint64_t Physics::pairKey(Entity a, Entity b) {
    uint32_t low = std::min(a.index, b.index), high = std::max(a.index, b.index);
    return (uint64_t(high) << 32) | uint64_t(low);
}

void Physics::step(Scene& scene, float dt) {
    // 1. Integrate: velocity from forces, then position from velocity (semi-implicit Euler).
    scene.each<RigidBody, Transform>([&](Entity, RigidBody& body, Transform& transform) {
        if (!body.isInitialized) {
            body.position = transform.position;
            body.isInitialized = true;
        }
        body.previousPosition = body.position;
        body.isGrounded = false;
        if (body.type == BodyType::Dynamic) {
            body.velocity += gravity * (body.gravityScale * dt);
            body.velocity *= 1.0f / (1.0f + body.linearDamping * dt);
        }
        if (body.type != BodyType::Static) body.position += body.velocity * dt;
    });

    // 2. Place every collider in the world.
    proxies_.clear();
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        Proxy proxy;
        proxy.entity = entity;
        proxy.collider = &collider;
        proxy.body = scene.get<RigidBody>(entity);
        Vec3 position = proxy.body ? proxy.body->position : transform.position;
        proxy.shape = makeShape(collider, position, transform.rotation);
        proxy.bounds = proxy.shape.bounds();
        if (proxy.body && proxy.body->type == BodyType::Dynamic) {
            proxy.inverseMass = 1.0f / std::max(proxy.body->mass, 1e-4f);
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

    contacts_.clear();
    contactProxies_.clear();
    std::unordered_map<uint64_t, std::pair<Entity, Entity>> touchingNow, triggeringNow;

    for (size_t i = 0; i < order_.size(); i++) {
        Proxy& a = proxies_[order_[i]];
        for (size_t j = i + 1; j < order_.size(); j++) {
            Proxy& b = proxies_[order_[j]];
            if (b.bounds.min.x > a.bounds.max.x) break;             // nothing further along can overlap a
            if (!a.bounds.intersects(b.bounds)) continue;
            if (!(a.collider->layer & b.collider->mask) || !(b.collider->layer & a.collider->mask)) continue;
            bool aMoves = a.body && a.body->type != BodyType::Static;
            bool bMoves = b.body && b.body->type != BodyType::Static;
            if (!aMoves && !bMoves) continue;                       // two obstacles never need testing

            // 4. Narrow phase: the exact shapes.
            ContactInfo info;
            if (!collide(a.shape, b.shape, info)) continue;

            if (a.collider->isTrigger || b.collider->isTrigger) {
                bool aIsTrigger = a.collider->isTrigger;
                triggeringNow[pairKey(a.entity, b.entity)] = aIsTrigger ? std::make_pair(a.entity, b.entity)
                                                                        : std::make_pair(b.entity, a.entity);
                continue;
            }
            if (a.inverseMass + b.inverseMass <= 0) continue;       // kinematic against static: nothing to solve

            Vec3 velocityA = a.body ? a.body->velocity : Vec3{0, 0, 0};
            Vec3 velocityB = b.body ? b.body->velocity : Vec3{0, 0, 0};
            Contact contact;
            contact.a = a.entity;
            contact.b = b.entity;
            contact.point = info.point;
            contact.normal = info.normal;
            contact.depth = info.depth;
            contact.impactSpeed = std::max(0.0f, -simd::dot(velocityB - velocityA, info.normal));
            contacts_.push_back(contact);
            contactProxies_.push_back({order_[i], order_[j]});
            touchingNow[pairKey(a.entity, b.entity)] = {a.entity, b.entity};
        }
    }

    // 5. Resolve velocities with impulses. Several passes let contacts that share a
    //    body, like a ball in a corner, settle against each other.
    for (int iteration = 0; iteration < solverIterations; iteration++) {
        for (size_t c = 0; c < contacts_.size(); c++) {
            const Contact& contact = contacts_[c];
            Proxy& a = proxies_[contactProxies_[c].first];
            Proxy& b = proxies_[contactProxies_[c].second];
            Vec3 velocityA = a.body ? a.body->velocity : Vec3{0, 0, 0};
            Vec3 velocityB = b.body ? b.body->velocity : Vec3{0, 0, 0};
            Vec3 relative = velocityB - velocityA;
            float normalSpeed = simd::dot(relative, contact.normal);
            if (normalSpeed >= 0) continue;                         // already separating

            float restitutionA = a.body ? a.body->restitution : 0.0f;
            float restitutionB = b.body ? b.body->restitution : 0.0f;
            float bounce = -normalSpeed > kBounceThreshold ? std::max(restitutionA, restitutionB) : 0.0f;
            float inverseMassSum = a.inverseMass + b.inverseMass;
            float normalImpulse = -(1.0f + bounce) * normalSpeed / inverseMassSum;
            Vec3 impulse = contact.normal * normalImpulse;
            velocityA -= impulse * a.inverseMass;
            velocityB += impulse * b.inverseMass;

            // Friction acts along the surface, against the sliding direction, and can
            // never be stronger than the friction coefficient times the normal impulse.
            relative = velocityB - velocityA;
            Vec3 tangent = relative - contact.normal * simd::dot(relative, contact.normal);
            float slideSpeed = simd::length(tangent);
            if (slideSpeed > 1e-5f) {
                tangent /= slideSpeed;
                float frictionA = a.body ? a.body->friction : 0.6f;
                float frictionB = b.body ? b.body->friction : 0.6f;
                float coefficient = std::sqrt(frictionA * frictionB);
                float frictionImpulse = std::min(slideSpeed / inverseMassSum, coefficient * normalImpulse);
                velocityA += tangent * (frictionImpulse * a.inverseMass);
                velocityB -= tangent * (frictionImpulse * b.inverseMass);
            }
            if (a.inverseMass > 0) a.body->velocity = velocityA;
            if (b.inverseMass > 0) b.body->velocity = velocityB;
        }
    }

    // 6. Remove what is left of the overlap by moving the bodies apart, and note who
    //    is standing on what.
    for (size_t c = 0; c < contacts_.size(); c++) {
        const Contact& contact = contacts_[c];
        Proxy& a = proxies_[contactProxies_[c].first];
        Proxy& b = proxies_[contactProxies_[c].second];
        float inverseMassSum = a.inverseMass + b.inverseMass;
        float amount = std::max(contact.depth - kPenetrationSlop, 0.0f) * kCorrectionFraction / inverseMassSum;
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

    // 7. Events. Collected first and sent last, so a callback can change the scene freely.
    std::vector<Contact> began;
    for (const Contact& contact : contacts_) {
        if (!touching_.count(pairKey(contact.a, contact.b))) began.push_back(contact);
    }
    std::vector<std::pair<Entity, Entity>> entered, exited;
    for (const auto& [key, pair] : triggeringNow) {
        if (!triggering_.count(key)) entered.push_back(pair);
    }
    for (const auto& [key, pair] : triggering_) {
        if (!triggeringNow.count(key)) exited.push_back(pair);
    }
    touching_ = std::move(touchingNow);
    triggering_ = std::move(triggeringNow);

    if (onContactBegin) for (const Contact& contact : began) onContactBegin(contact);
    if (onTriggerEnter) for (const auto& pair : entered) onTriggerEnter(pair.first, pair.second);
    if (onTriggerExit) for (const auto& pair : exited) onTriggerExit(pair.first, pair.second);
}

void Physics::writeTransforms(Scene& scene, float alpha) {
    scene.each<RigidBody, Transform>([&](Entity, RigidBody& body, Transform& transform) {
        if (!body.isInitialized) return;
        transform.position = lerp(body.previousPosition, body.position, alpha);
    });
}

void Physics::teleport(Scene& scene, Entity entity, Vec3 position) {
    if (Transform* transform = scene.get<Transform>(entity)) transform->position = position;
    if (RigidBody* body = scene.get<RigidBody>(entity)) {
        body->position = position;
        body->previousPosition = position;                         // no blending across the jump
        body->isInitialized = true;
    }
}

std::optional<RaycastHit> Physics::raycast(Scene& scene, const Ray& ray, float maxDistance, uint32_t mask, Entity ignore) {
    std::optional<RaycastHit> closest;
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (collider.isTrigger || !(collider.layer & mask) || entity == ignore) return;
        const RigidBody* body = scene.get<RigidBody>(entity);
        Vec3 position = body && body->isInitialized ? body->position : transform.position;
        auto hit = raycastShape(ray, makeShape(collider, position, transform.rotation));
        if (!hit || hit->distance > maxDistance) return;
        if (!closest || hit->distance < closest->distance) {
            closest = RaycastHit{entity, ray.at(hit->distance), hit->normal, hit->distance};
        }
    });
    return closest;
}

std::vector<Entity> Physics::overlapSphere(Scene& scene, Vec3 center, float radius, uint32_t mask) {
    std::vector<Entity> found;
    ShapeInstance probe;
    probe.shape = ColliderShape::Sphere;
    probe.center = center;
    probe.radius = radius;
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (!(collider.layer & mask)) return;
        const RigidBody* body = scene.get<RigidBody>(entity);
        Vec3 position = body && body->isInitialized ? body->position : transform.position;
        ContactInfo info;
        if (collide(probe, makeShape(collider, position, transform.rotation), info)) found.push_back(entity);
    });
    return found;
}

}  // namespace molten
