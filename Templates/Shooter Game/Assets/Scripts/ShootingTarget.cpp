// ShootingTarget.cpp
// A plate that falls when it is shot, and stands up again if asked to.
// This game's own script: change it as the game needs.
#include "Shooting.hpp"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- ShootingTarget

void ShootingTarget::onMessage(World& world, Entity, const Message& message) {
    if (message.name != "shot" || isDown) return;
    isDown = true;
    downFor = 0;
    if (!counter.empty()) world.addValue(counter, points);
    if (!sound.empty()) world.playSound(sound);
}

void ShootingTarget::update(World& world, Entity self, float dt) {
    Transform* place = world.scene.get<Transform>(self);
    if (!place) return;
    if (!hasUpright) {
        upright = place->rotation;
        hasUpright = true;
    }
    if (isDown) {
        downFor += dt;
        if (standsAfter > 0 && downFor >= standsAfter) isDown = false;
    }
    // Down fast, as a thing knocked over; up again slowly, as a thing wound back.
    float wanted = isDown ? 1.0f : 0.0f;
    fallen += std::clamp(wanted - fallen, -dt * 1.2f, dt * 5.0f);
    float eased = fallen * fallen * (3 - 2 * fallen);
    place->rotation = upright * Quat(radians(fallAngle * eased), Vec3{1, 0, 0});
}

MOLTEN_COMPONENT(ShootingTarget, MOLTEN_FIELD(counter).picks("value"), MOLTEN_FIELD(points), MOLTEN_FIELD(standsAfter).range(0, 120).help("Seconds before it stands again; 0: it stays down"),
                 MOLTEN_FIELD(fallAngle).range(0, 120), MOLTEN_FIELD(sound).picks("sound"))
