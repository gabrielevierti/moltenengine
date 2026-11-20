#include "Water/Liquid.hpp"

#include "Game/Component.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Particles.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

namespace {

// How much a drop at a distance counts toward how crowded a place is: most when it is
// right there, nothing at the reach `h` and beyond.
float crowding(float distanceSquared, float h) {
    float d = h * h - distanceSquared;
    return d <= 0 ? 0.0f : 315.0f / (64.0f * kPi * std::pow(h, 9.0f)) * d * d * d;
}

// Which way, and how strongly, moving a drop changes that: used to push drops apart.
Vec3 crowdingSlope(Vec3 offset, float h) {
    float distance = simd::length(offset);
    if (distance <= 1e-6f || distance >= h) return Vec3{0, 0, 0};
    float d = h - distance;
    return offset * (-45.0f / (kPi * std::pow(h, 6.0f)) * d * d / distance);
}

}  // namespace

void Liquid::add(Vec3 position, Vec3 velocity) {
    if (int(positions.size()) >= maxDrops) return;
    positions.push_back(position);
    velocities.push_back(velocity);
}

void Liquid::start(World& world, Entity self) {
    hasStarted = true;
    const Transform* transform = world.scene.get<Transform>(self);
    Vec3 origin = transform ? transform->worldPosition() : Vec3{0, 0, 0};
    // The block it starts as: drops a diameter apart, as close as a liquid at rest packs them.
    float spacing = std::max(dropRadius, 0.005f) * 2.0f;
    if (fill.x > 0 && fill.y > 0 && fill.z > 0) {
        int nx = std::max(1, int(fill.x / spacing)), ny = std::max(1, int(fill.y / spacing)), nz = std::max(1, int(fill.z / spacing));
        for (int j = 0; j < ny; j++) {
            for (int k = 0; k < nz; k++) {
                for (int i = 0; i < nx; i++) {
                    // Not quite in rows: drops stacked exactly above one another would fall as a column and never spread.
                    Vec3 nudge = Vec3{float((i * 7 + j * 3 + k * 5) % 11) - 5.0f, 0.0f, float((i * 5 + j * 7 + k * 3) % 13) - 6.0f} * (spacing * 0.012f);
                    add(origin + nudge + Vec3{(float(i) - float(nx - 1) * 0.5f) * spacing, (float(j) - float(ny - 1) * 0.5f) * spacing, (float(k) - float(nz - 1) * 0.5f) * spacing});
                }
            }
        }
    }
}

void Liquid::fixedUpdate(World& world, Entity self, float dt) {
    if (!hasStarted || dt <= 0) return;
    Scene& scene = world.scene;
    const float radius = std::max(dropRadius, 0.005f), spacing = radius * 2.0f;
    const float h = spacing * 2.1f;                             // how far a drop feels its neighbors
    const float closest = spacing * 0.86f;                      // and how near two may come

    // ---- pouring
    if (pourRate > 0) {
        const Transform* transform = scene.get<Transform>(self);
        Vec3 origin = transform ? transform->worldPosition() : Vec3{0, 0, 0};
        Quat rotation = transform ? transform->rotation : Quat{};
        pourAccumulator += pourRate * dt;
        while (pourAccumulator >= 1.0f) {
            pourAccumulator -= 1.0f;
            // Spread over the stream's width in a pattern that never repeats and never clumps.
            float angle = float(pourCount_) * 2.39996323f, share = std::sqrt(float(pourCount_ % 16 + 1) / 16.0f);
            pourCount_++;
            Vec3 across = rotate(rotation, Vec3{std::cos(angle), 0, std::sin(angle)}) * (pourWidth * share);
            add(origin + across, rotate(rotation, Vec3{0, -1, 0}) * pourSpeed);
        }
    }
    const size_t count = positions.size();
    if (count == 0) return;

    // ---- what it can run into: every collider that is not a trigger
    struct Obstacle {
        ShapeInstance shape;
        AABB bounds;
    };
    std::vector<Obstacle> obstacles;
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (entity == self || collider.isTrigger || !transform.isActiveInHierarchy) return;
        const RigidBody* body = scene.get<RigidBody>(entity);
        Obstacle obstacle;
        obstacle.shape = Physics::makeShape(collider, body && body->isInitialized ? body->position : transform.worldPosition(),
                                            body && body->isInitialized ? body->rotation : transform.rotation);
        obstacle.bounds = obstacle.shape.bounds();
        obstacles.push_back(obstacle);
    });

    // What a liquid at rest has around each drop: drops a diameter apart in every direction.
    float rest = 0;
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            for (int k = -2; k <= 2; k++) rest += crowding(float(i * i + j * j + k * k) * spacing * spacing, h);
        }
    }

    // The whole step in two halves: a liquid is stiff, and small steps keep it calm.
    const int substeps = 2;
    const float step = dt / float(substeps);
    predicted_.resize(count);
    moves_.resize(count);
    pressure_.resize(count);
    cellNext_.resize(count);
    const int tableSize = 1 << 14;
    cellStart_.assign(size_t(tableSize), -1);
    auto cellOf = [&](Vec3 p) {
        int x = int(std::floor(p.x / h)), y = int(std::floor(p.y / h)), z = int(std::floor(p.z / h));
        return size_t((uint32_t(x) * 73856093u) ^ (uint32_t(y) * 19349663u) ^ (uint32_t(z) * 83492791u)) & size_t(tableSize - 1);
    };
    /// Calls `visit(j)` for every drop that may be within reach of a place.
    auto nearby = [&](Vec3 p, auto&& visit) {
        int x = int(std::floor(p.x / h)), y = int(std::floor(p.y / h)), z = int(std::floor(p.z / h));
        for (int dz = -1; dz <= 1; dz++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    size_t cell = size_t((uint32_t(x + dx) * 73856093u) ^ (uint32_t(y + dy) * 19349663u) ^ (uint32_t(z + dz) * 83492791u)) & size_t(tableSize - 1);
                    for (int j = cellStart_[cell]; j >= 0; j = cellNext_[size_t(j)]) visit(size_t(j));
                }
            }
        }
    };

    for (int substep = 0; substep < substeps; substep++) {
        for (size_t i = 0; i < count; i++) {
            velocities[i] += world.physics.gravity * step;
            predicted_[i] = positions[i] + velocities[i] * step;
        }
        // Sort the drops into boxes the size of their reach: neighbors are then in the 27 boxes around.
        std::fill(cellStart_.begin(), cellStart_.end(), -1);
        for (size_t i = 0; i < count; i++) {
            size_t cell = cellOf(predicted_[i]);
            cellNext_[i] = cellStart_[cell];
            cellStart_[cell] = int(i);
        }
        for (int pass = 0; pass < 3; pass++) {
            // How overcrowded each drop's place is, as a pressure that will push its neighbors away.
            for (size_t i = 0; i < count; i++) {
                float crowd = 0, slopes = 0;
                Vec3 own{0, 0, 0};
                nearby(predicted_[i], [&](size_t j) {
                    Vec3 offset = predicted_[i] - predicted_[j];
                    float distanceSquared = simd::length_squared(offset);
                    if (distanceSquared >= h * h) return;
                    crowd += crowding(distanceSquared, h);
                    if (j == i) return;
                    Vec3 slope = crowdingSlope(offset, h) / rest;
                    slopes += simd::length_squared(slope);
                    own += slope;
                });
                float excess = std::max(crowd / rest - 1.0f, 0.0f);         // only too many is put right: drops are not pulled together
                pressure_[i] = -excess / (slopes + simd::length_squared(own) + 8.0f);
            }
            for (size_t i = 0; i < count; i++) {
                Vec3 move{0, 0, 0};
                nearby(predicted_[i], [&](size_t j) {
                    if (j == i) return;
                    Vec3 offset = predicted_[i] - predicted_[j];
                    float distanceSquared = simd::length_squared(offset);
                    if (distanceSquared >= h * h) return;
                    move += crowdingSlope(offset, h) * ((pressure_[i] + pressure_[j]) / rest);
                    // And two drops are never in the same place, however few there are around them:
                    // at the surface, and in a splash, crowding alone would not keep them apart.
                    if (distanceSquared < closest * closest) {
                        float distance = std::sqrt(distanceSquared);
                        Vec3 away = distance > 1e-5f ? offset / distance
                                                     : simd::normalize(Vec3{float(i % 7) - 3.0f, 0.5f + float(i > j), float(i % 5) - 2.0f});
                        move += away * ((closest - distance) * 0.5f);
                    }
                });
                moves_[i] = move;
            }
            for (size_t i = 0; i < count; i++) {
                Vec3 p = predicted_[i] + moves_[i];
                // Out of whatever it has sunk into.
                ShapeInstance drop;
                drop.shape = ColliderShape::Sphere;
                drop.radius = radius;
                for (const Obstacle& obstacle : obstacles) {
                    if (p.x < obstacle.bounds.min.x - radius || p.x > obstacle.bounds.max.x + radius || p.y < obstacle.bounds.min.y - radius ||
                        p.y > obstacle.bounds.max.y + radius || p.z < obstacle.bounds.min.z - radius || p.z > obstacle.bounds.max.z + radius) {
                        continue;
                    }
                    drop.center = p;
                    ContactInfo contact;
                    if (collide(drop, obstacle.shape, contact)) p -= contact.normal * contact.depth;
                }
                predicted_[i] = p;
            }
        }
        // The speed each drop turned out to have, and where it now is.
        for (size_t i = 0; i < count; i++) {
            Vec3 velocity = (predicted_[i] - positions[i]) / step;
            // Whatever pushed it out of an obstacle also took some of its sliding along it.
            float speed = simd::length(velocity);
            const float fastest = 0.4f * h / step;              // no drop crosses its neighbors' reach in one step
            if (speed > fastest) velocity *= fastest / speed;
            velocities[i] = velocity;
            moves_[i] = velocity;
        }
        // Drops drag their neighbors along: what makes it flow as one thing, thick or thin.
        float drag = std::clamp(viscosity, 0.0f, 0.5f) + 0.01f;
        for (size_t i = 0; i < count; i++) {
            Vec3 pull{0, 0, 0};
            nearby(predicted_[i], [&](size_t j) {
                if (j == i) return;
                float distanceSquared = simd::length_squared(predicted_[i] - predicted_[j]);
                if (distanceSquared >= h * h) return;
                pull += (moves_[j] - moves_[i]) * (crowding(distanceSquared, h) / rest);
            });
            velocities[i] += pull * drag;
            positions[i] = predicted_[i];
        }
        // And clings a little to what it rests against.
        if (stickiness > 0) {
            float keep = std::max(0.0f, 1.0f - stickiness * step * 6.0f);
            ShapeInstance drop;
            drop.shape = ColliderShape::Sphere;
            drop.radius = radius * 1.3f;
            for (size_t i = 0; i < count; i++) {
                for (const Obstacle& obstacle : obstacles) {
                    Vec3 p = positions[i];
                    if (p.x < obstacle.bounds.min.x - h || p.x > obstacle.bounds.max.x + h || p.y < obstacle.bounds.min.y - h || p.y > obstacle.bounds.max.y + h ||
                        p.z < obstacle.bounds.min.z - h || p.z > obstacle.bounds.max.z + h) {
                        continue;
                    }
                    drop.center = p;
                    ContactInfo contact;
                    if (collide(drop, obstacle.shape, contact)) {
                        Vec3 along = velocities[i] - contact.normal * simd::dot(velocities[i], contact.normal);
                        velocities[i] -= along * (1.0f - keep);
                        break;
                    }
                }
            }
        }
    }
}

void Liquid::update(World& world, Entity self, float) {
    // The drops are drawn by the entity's particle emitter, in its size and color.
    ParticleEmitter* emitter = world.scene.get<ParticleEmitter>(self);
    if (!emitter) return;
    emitter->isEmitting = false;
    emitter->gravity = Vec3{0, 0, 0};
    emitter->windInfluence = 0;
    emitter->maxParticles = std::max(emitter->maxParticles, int(positions.size()));
    emitter->particles.resize(positions.size());
    for (size_t i = 0; i < positions.size(); i++) {
        Particle& particle = emitter->particles[i];
        particle.position = positions[i];
        particle.velocity = Vec3{0, 0, 0};
        particle.age = 0;                                       // a drop does not grow old
        particle.lifetime = 1;
    }
}

MOLTEN_COMPONENT(Liquid, MOLTEN_FIELD(fill).help("A block of liquid this size, around the entity, when the game starts. Zero for none"),
                 MOLTEN_FIELD(pourRate).range(0, 2000).help("Drops per second poured from the entity, along its -y"), MOLTEN_FIELD(pourSpeed).range(0, 20),
                 MOLTEN_FIELD(pourWidth).range(0, 2), MOLTEN_FIELD(maxDrops).range(1, 20000),
                 MOLTEN_FIELD(dropRadius).range(0.01f, 0.5f).help("Smaller is finer and needs more drops for the same amount"),
                 MOLTEN_FIELD(viscosity).range(0, 0.5f).help("0 water to 0.5 honey"), MOLTEN_FIELD(stickiness).range(0, 1))

}  // namespace molten
