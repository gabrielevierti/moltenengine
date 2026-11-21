// CameraShots.hpp
// Shots, and a camera that goes from one to the next smoothly: what film tools call virtual
// cameras. A Camera Shot is a place to film from (move it with Follow, Orbit Camera, a
// timeline, a script); the Camera Director, on the real Camera, takes the shot of the highest
// priority among those switched on, and blends to it over the shot's blend time. Switch a shot
// on (or raise its priority) and the camera glides there; switch it off and it glides back.
#pragma once

#include "Core/World.hpp"

namespace molten::kit {

struct CameraShot {
    int priority = 10;                  // the highest of the shots switched on is filmed
    float blendSeconds = 1.0f;          // how long the camera takes to come to it; 0 cuts
    float fieldOfView = 60.0f;          // degrees, top to bottom

    /// The shot the director goes to: the highest priority switched on (the first, if equal).
    static Entity best(World& world);
};

struct CameraDirector {
    // State.
    Entity shot{};                      // the one filmed
    float blended = 1.0f;               // how far into the blend, 0 to 1
    float blendSeconds = 0;
    Vec3 fromPosition{0, 0, 0};
    Quat fromRotation{};
    float fromFieldOfView = 60.0f;

    void lateUpdate(World& world, Entity self, float dt);
    bool isBlending() const { return blended < 1.0f; }
};

void linkCameraShots();

}  // namespace molten::kit
