// Hunter.cpp
// A machine that hovers, looks for the player, flies at them and hurts them.
// This game's own script: change it as the game needs.
#include "Shooting.hpp"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- Hunter

void Hunter::fixedUpdate(World& world, Entity self, float dt) {
    Scene& scene = world.scene;
    Transform* place = scene.get<Transform>(self);
    if (!place || dt <= 0) return;
    Vec3 at = place->worldPosition();
    if (!hasHome) {
        home = at;
        hasHome = true;
    }
    time += dt;
    untilNext = std::max(untilNext - dt, 0.0f);

    // Does it see its prey: near enough, and nothing solid on the way to the middle of them.
    Entity hunted = world.find(prey);
    const Transform* them = scene.get<Transform>(hunted);
    Vec3 aim = them ? them->worldPosition() + Vec3{0, 1.1f, 0} : at;
    float away = simd::length(aim - at);
    bool sees = false;
    if (them && away <= sightDistance) {
        auto between = world.physics.raycast(scene, Ray{at, (aim - at) / std::max(away, 1e-4f)}, away, 0xFFFFFFFFu, self);
        sees = !between || between->entity == hunted || Physics::isUnder(scene, between->entity, hunted);
    }
    if (sees) {
        isHunting = true;
        unseenFor = 0;
    } else if (isHunting) {
        unseenFor += dt;
        if (unseenFor > forgetsAfter || !them) isHunting = false;
    }

    // Where it wants to be: at its prey, or home; and how high: over the ground, and over whatever is in its way.
    Vec3 goal = isHunting && them ? aim : home;
    Vec3 flat{goal.x - at.x, 0, goal.z - at.z};
    float far = simd::length(flat);
    Vec3 heading = far > 1e-3f ? flat / far : Vec3{0, 0, 0};
    float stopAt = isHunting ? reach * 0.7f : 0.3f;
    float pace = far > stopAt ? (isHunting ? speed : speed * 0.5f) : 0.0f;
    float wantedHeight = goal.y;
    if (!isHunting) wantedHeight = home.y;
    if (auto ground = world.physics.raycast(scene, Ray{at, Vec3{0, -1, 0}}, 30.0f, 0xFFFFFFFFu, self)) {
        if (!hunted.isValid() || (ground->entity != hunted && !Physics::isUnder(scene, ground->entity, hunted))) {
            wantedHeight = std::max(wantedHeight, ground->point.y + (isHunting ? std::min(hoverHeight, 1.2f) : hoverHeight));
        }
    }
    bool blocked = false;
    if (pace > 0) {
        if (auto wall = world.physics.raycast(scene, Ray{at, heading}, 1.6f, 0xFFFFFFFFu, self)) {
            blocked = wall->entity != hunted && !(hunted.isValid() && Physics::isUnder(scene, wall->entity, hunted));
        }
    }
    Vec3 moved = heading * (blocked ? 0.0f : pace * dt);
    // Something in the way: up, until it is not.
    float climb = blocked ? 2.6f * dt : std::clamp(wantedHeight + 0.12f * std::sin(time * 2.1f) - at.y, -1.8f * dt, 1.8f * dt);
    moved.y = climb;
    place->position += moved;                           // it has nothing above it in the scene: its place is the world's
    // It looks where it goes, or at its prey.
    Vec3 look = isHunting && them ? Vec3{aim.x - at.x, 0, aim.z - at.z} : heading;
    if (simd::length_squared(look) > 1e-4f) {
        place->rotation = simd::normalize(simd::slerp(place->rotation, lookRotation(simd::normalize(look)), std::min(6.0f * dt, 1.0f)));
    } else if (!isHunting) {
        place->rotation = simd::normalize(place->rotation * Quat(0.6f * dt, Vec3{0, 1, 0}));        // at home, it turns and looks about
    }

    // Within reach, it hurts.
    if (isHunting && them && away <= reach && untilNext <= 0) {
        untilNext = every;
        std::string sound = attackSound;
        float dealt = damage;
        if (kit::Health* health = scene.get<kit::Health>(hunted)) health->damage(world, hunted, dealt);
        if (!sound.empty()) world.playSound(sound);
    }
}

MOLTEN_COMPONENT(Hunter, MOLTEN_FIELD(prey).picks("entity"), MOLTEN_FIELD(sightDistance).range(1, 200), MOLTEN_FIELD(speed).range(0, 30), MOLTEN_FIELD(hoverHeight).range(0.2f, 20),
                 MOLTEN_FIELD(reach).range(0.3f, 10).help("This near, it hurts"), MOLTEN_FIELD(damage).range(0, 1000), MOLTEN_FIELD(every).range(0.1f, 10).help("Seconds between two"),
                 MOLTEN_FIELD(forgetsAfter).range(0, 120).help("Seconds without seeing its prey before it goes home"), MOLTEN_FIELD(attackSound).picks("sound"))
