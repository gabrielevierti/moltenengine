// Follow.cpp
// A camera that follows something: the player's car, plane or boat. This game's own script:
// change it as the game needs.
#include "MoltenMetal.hpp"

#include <cmath>

using namespace molten;

struct Follow {
    std::string target = "Player";      // the name of the entity to follow
    Vec3 offset{0, 7, 9};               // where the camera sits, from the target
    float smoothness = 6.0f;            // higher follows more tightly; 0 jumps there at once
    bool looksAtTarget = true;
    bool turnsWithTarget = false;       // the offset turns as the target does: a camera that stays behind a car

    Vec3 heading{0, 0, 0};              // which way the camera is behind the target, eased

    /// After everything has moved, so that it follows where the target is drawn this frame.
    void lateUpdate(World& world, Entity self, float dt) {
        Transform* transform = world.scene.get<Transform>(self);
        const Transform* followed = world.scene.get<Transform>(world.find(target));
        if (!transform || !followed) return;
        Vec3 center = followed->worldPosition();
        // Each frame covers a fixed share of what is left: quick when far, gentle when near,
        // and the same at any frame rate.
        float share = smoothness > 0 ? 1.0f - std::exp(-smoothness * dt) : 1.0f;
        if (turnsWithTarget) {
            // Behind something fast the camera is held at its distance exactly, and what is eased
            // is the direction: easing the position would let it fall back by a length that
            // changes with every frame, and the whole picture would shake. Around the vertical
            // only: a car on its roof keeps the camera above the road.
            Vec3 ahead = rotate(followed->rotation, Vec3{0, 0, -1});
            ahead.y = 0;
            if (simd::length_squared(ahead) > 1e-4f) {
                ahead = simd::normalize(ahead);
                heading = simd::length_squared(heading) < 0.5f ? ahead : safeNormalize(heading + (ahead - heading) * share, ahead);
            }
            if (simd::length_squared(heading) < 0.5f) heading = Vec3{0, 0, -1};
            transform->position = center + rotate(lookRotation(heading), offset);
            if (looksAtTarget) {
                // A little above the target and level with the horizon, not swinging with its every move.
                Vec3 toTarget = center + Vec3{0, offset.y * 0.35f, 0} - transform->position;
                if (simd::length_squared(toTarget) > 1e-4f) transform->rotation = lookRotation(toTarget);
            }
            return;
        }
        transform->position += (center + offset - transform->position) * share;
        if (looksAtTarget) {
            Vec3 toTarget = center - transform->position;
            if (simd::length_squared(toTarget) > 1e-4f) transform->rotation = lookRotation(toTarget);
        }
    }
};

MOLTEN_COMPONENT(Follow, MOLTEN_FIELD(target).picks("entity").help("The name of the entity to follow"), MOLTEN_FIELD(offset).help("Where the camera sits, from the target"),
                 MOLTEN_FIELD(smoothness).range(0, 20).help("Higher follows more tightly; 0 jumps there at once"), MOLTEN_FIELD(looksAtTarget),
                 MOLTEN_FIELD(turnsWithTarget).help("The offset turns as the target does: a camera that stays behind"))
