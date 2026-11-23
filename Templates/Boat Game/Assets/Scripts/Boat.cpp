// Boat.cpp
// The boat: pushed ahead by its propeller and turned by its rudder, only while it floats.
// This game's own script: change it as the game needs. The boat itself is made of the
// engine's parts: a Rigid Body, and a Buoyancy that holds it up on the sea.
//
//   W S    throttle ahead and astern (the action moveZ, or the left stick)
//   A D    the rudder (moveX)
//
// With `usesInput` off it is steered by `throttle` and `steer`, set from other code: a boat
// that races the player. It writes the shared values "speed" (km/h) and "knots", for the dial.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

struct Boat {
    float acceleration = 5;             // meters per second, per second, at full throttle
    float topSpeed = 12;                // meters per second
    float turnSpeed = 45;               // degrees per second at full rudder and full speed
    float reverse = 0.4f;               // of the acceleration, going backwards
    bool usesInput = true;
    float throttle = 0;                 // -1 astern to 1 full ahead
    float steer = 0;                    // -1 left to 1 right
    std::string sound;                  // its engine: a sound of Assets that loops, louder and higher with the throttle; empty for silence

    float speed = 0;                    // along the way it points, now
    uint32_t voice = 0;

    void start(World& world, Entity self) {
        // A hull slips through the water lengthwise far more easily than across it.
        if (kit::Buoyancy* floats = world.scene.get<kit::Buoyancy>(self); floats && floats->lengthDrag == 1) floats->lengthDrag = 0.12f;
    }

    void fixedUpdate(World& world, Entity self, float dt) {
        RigidBody* body = world.scene.get<RigidBody>(self);
        const kit::Buoyancy* floats = world.scene.get<kit::Buoyancy>(self);
        if (!body) return;
        if (usesInput) {
            throttle = std::clamp(-world.input.axis("moveZ"), -1.0f, 1.0f);       // W is -1 along moveZ
            steer = std::clamp(world.input.axis("moveX"), -1.0f, 1.0f);
        }
        Vec3 forward = rotate(body->rotation, Vec3{0, 0, -1});
        forward.y = 0;
        forward = safeNormalize(forward, Vec3{0, 0, -1});
        speed = simd::dot(body->velocity, forward);
        if (!floats || !floats->isInWater) return;
        // Pushed by its propeller where it sits in the water, up to its top speed.
        float push = throttle >= 0 ? throttle : throttle * reverse;
        if ((push > 0 && speed < topSpeed) || (push < 0 && speed > -topSpeed * reverse)) body->velocity += forward * (push * acceleration * dt);
        // The rudder turns it the more the faster it goes (and the other way going astern), and
        // the water keeps it going the way it points instead of sliding sideways.
        float bite = std::clamp(speed / std::max(topSpeed * 0.4f, 0.1f), -1.0f, 1.0f);
        body->angularVelocity.y += (-steer * radians(turnSpeed) * bite - body->angularVelocity.y) * std::min(1.0f, 3.0f * dt);
        Vec3 side = simd::cross(forward, Vec3{0, 1, 0});
        float sliding = simd::dot(body->velocity, side);
        body->velocity -= side * sliding * std::min(1.0f, 2.0f * floats->underNow * dt);
        if (!body->rotates) {
            // A body that does not rotate by itself is turned here.
            if (Transform* transform = world.scene.get<Transform>(self)) {
                transform->rotation = simd::normalize(Quat(body->angularVelocity.y * dt, Vec3{0, 1, 0}) * transform->rotation);
            }
        }
    }

    void update(World& world, Entity, float) {
        // For the dial, and for whoever asks: how fast it goes, as a car's speed and as a sailor's.
        world.setValue("speed", std::abs(speed) * 3.6f);
        world.setValue("knots", std::abs(speed) * 1.943844f);
        if (sound.empty()) return;
        if (voice == 0) {
            voice = world.startSound(sound, 0.0f, 1.0f, true);
            if (voice == 0) voice = ~0u;                        // no sound here: do not ask again every frame
        }
        // Idling it mutters; with the lever forward it is loud, and higher the faster it goes.
        float effort = std::abs(throttle), pace = std::clamp(std::abs(speed) / std::max(topSpeed, 0.1f), 0.0f, 1.0f);
        if (voice != ~0u) world.changeSound(voice, 0.22f + 0.5f * effort, 0.6f + 0.45f * effort + 0.35f * pace);
    }
};

MOLTEN_COMPONENT(Boat, MOLTEN_FIELD(acceleration).range(0, 40).help("Meters per second, per second, at full throttle"),
                 MOLTEN_FIELD(topSpeed).range(0, 80).help("Meters per second"), MOLTEN_FIELD(turnSpeed).range(0, 180).help("Degrees per second at full rudder"),
                 MOLTEN_FIELD(reverse).range(0, 1).help("Of the acceleration, going backwards"),
                 MOLTEN_FIELD(usesInput).help("Driven by the move actions. Off: by throttle and steer, from code"),
                 MOLTEN_FIELD(throttle).range(-1, 1), MOLTEN_FIELD(steer).range(-1, 1),
                 MOLTEN_FIELD(sound).picks("sound").help("Its engine: a sound in Assets that loops, louder and higher with the throttle. Empty: silent"))
