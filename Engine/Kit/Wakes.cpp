#include "Kit/Wakes.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"
#include "Water/Waves.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

namespace {

constexpr int kTrailPoints = 15;                // laid down, besides where it is now: the water reads sixteen
constexpr double kTrailEvery = 0.45;            // seconds between them: about seven seconds of wake

// Half its width and half its length along the water: the hull it was given, or its collider.
Vec2 sizeOf(const Buoyancy& buoyancy, const Collider* collider, const Transform& transform) {
    if (buoyancy.hull.x > 0 && buoyancy.hull.z > 0) return Vec2{buoyancy.hull.x, buoyancy.hull.z};
    Vec3 scale{simd::length(xyz(transform.world.columns[0])), simd::length(xyz(transform.world.columns[1])), simd::length(xyz(transform.world.columns[2]))};
    if (collider) {
        switch (collider->shape) {
            case ColliderShape::Sphere: return Vec2{collider->radius, collider->radius} * std::max(scale.x, scale.z);
            case ColliderShape::Capsule: return Vec2{collider->radius * scale.x, (collider->radius + collider->halfHeight) * scale.z};
            case ColliderShape::Box: return Vec2{collider->halfExtents.x * scale.x, collider->halfExtents.z * scale.z};
            default: break;
        }
    }
    return Vec2{0.5f * scale.x, 0.5f * scale.z};
}

}  // namespace

void updateWakes(World& world, float dt) {
    Scene& scene = world.scene;
    const double time = world.effectsTime;
    for (World::Wake& wake : world.wakes) wake.isSeen = false;
    scene.each<Buoyancy, Transform>([&](Entity entity, Buoyancy& buoyancy, Transform& transform) {
        if (!transform.isActiveInHierarchy || !buoyancy.isInWater) return;
        auto found = std::find_if(world.wakes.begin(), world.wakes.end(), [&](const World::Wake& w) { return w.body == entity; });
        if (found == world.wakes.end()) {
            world.wakes.push_back(World::Wake{});
            found = world.wakes.end() - 1;
            found->body = entity;
        }
        World::Wake& wake = *found;
        wake.isSeen = true;
        Vec3 at = transform.worldPosition();
        float surface = buoyancy.waterLevel;
        waterHeightAt(world, at, surface);
        Vec3 middle{at.x, surface, at.z};
        // Which way it heads: its -z, along the water.
        Vec3 forward = -xyz(transform.world.columns[2]);
        Vec2 heading = safeNormalize(Vec2{forward.x, forward.z}, wake.heading);
        Vec2 size = sizeOf(buoyancy, scene.get<Collider>(entity), transform);
        // How fast it goes along the water: its body's, or how far it went since the last frame.
        float speed = 0;
        if (const RigidBody* body = scene.get<RigidBody>(entity)) {
            speed = std::hypot(body->velocity.x, body->velocity.z);
        } else if (dt > 0 && !wake.trail.empty()) {
            speed = std::hypot(middle.x - wake.middle.x, middle.z - wake.middle.z) / dt;
        }
        wake.middle = middle;
        wake.heading = heading;
        wake.halfWidth = std::max(size.x, 0.1f);
        wake.halfLength = std::max(size.y, 0.1f);
        wake.speed += (speed - wake.speed) * std::min(dt * 4.0f, 1.0f);
        // Its stern, laid down every so often.
        Vec3 stern = middle - Vec3{heading.x, 0, heading.y} * wake.halfLength;
        if (wake.trail.empty() || time - double(wake.trail.front().w) >= kTrailEvery || time < double(wake.trail.front().w)) {
            wake.trail.insert(wake.trail.begin(), Vec4{stern.x, stern.y, stern.z, float(time)});
            wake.trailSpeed.insert(wake.trailSpeed.begin(), wake.speed);
            if (int(wake.trail.size()) > kTrailPoints) wake.trail.pop_back(), wake.trailSpeed.pop_back();
        }
    });
    // What sank, left the water or is gone leaves no more wake.
    world.wakes.erase(std::remove_if(world.wakes.begin(), world.wakes.end(), [](const World::Wake& w) { return !w.isSeen; }), world.wakes.end());
}

}  // namespace molten::kit
