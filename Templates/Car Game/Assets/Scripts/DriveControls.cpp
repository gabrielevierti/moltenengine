// DriveControls.cpp
// The driver: reads the keys and tells the car what is wanted. This game's own script: change
// it as the game needs. The car itself is made of the engine's parts (a Vehicle Body, its
// Wheels with Suspension and Deformable Tires, a Motor), which do what `controls` say.
//
//   W S       accelerate and brake (the action moveZ); S at a standstill is reverse
//   A D       steer (moveX)
//   Space     the handbrake (jump)
//   1 to 7    another engine: i4, i5, i6, v8, v12, diesel, electric
//
// It writes the shared values "speed" (km/h), "rpm" and "gear", for the dials, and plays the
// tires' squeal and the wind.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

namespace {

/// From `value` toward `target`, by at most `step`.
float approach(float value, float target, float step) {
    if (value < target) return std::min(value + step, target);
    return std::max(value - step, target);
}

}  // namespace

struct DriveControls {
    bool switchesEngines = true;        // keys 1 to 7
    std::string skidSound = "Audio/skid.wav";
    std::string windSound = "Audio/wind.wav";

    uint32_t skidVoice = 0, windVoice = 0;
    bool hasSounds = false;

    /// The car's own Motor: on the body, or on a part under it.
    kit::Motor* motorOf(World& world, Entity self) {
        kit::Motor* found = nullptr;
        world.scene.each<kit::Motor, Transform>([&](Entity entity, kit::Motor& motor, Transform&) {
            if (!found && kit::vehicleOf(world.scene, entity) == self) found = &motor;
        });
        return found;
    }

    void fixedUpdate(World& world, Entity self, float dt) {
        kit::VehicleBody* vehicle = world.scene.get<kit::VehicleBody>(self);
        if (!vehicle) return;
        const Input& input = world.input;
        kit::Controls& controls = vehicle->controls;
        kit::Motor* motor = motorOf(world, self);
        float ahead = -input.axis("moveZ");                 // W is +1
        float along = vehicle->speed();
        int gear = motor ? motor->gear : 1;
        // Pulling back at a standstill is reverse; pushing forward again, first gear.
        if (gear >= 1 && along < 0.6f && ahead < -0.1f) gear = -1;
        else if (gear == -1 && along > -0.6f && ahead > 0.1f) gear = 1;
        float wantThrottle = gear >= 1 ? std::max(ahead, 0.0f) : std::max(-ahead, 0.0f);
        float wantBrake = gear >= 1 ? std::max(-ahead, 0.0f) : std::max(ahead, 0.0f);
        // Pedals and wheel move as feet and hands do, not at the speed a key goes down.
        controls.throttle = approach(controls.throttle, wantThrottle, dt * 6);
        controls.brake = approach(controls.brake, wantBrake, dt * 8);
        float turn = input.axis("moveX");
        controls.steer = approach(controls.steer, turn, dt * (std::abs(turn) > 0.01f ? 3.2f : 5.0f));
        controls.handbrake = input.isDown("jump") ? 1.0f : 0.0f;
        if (motor) motor->gear = gear;
    }

    void update(World& world, Entity self, float) {
        kit::VehicleBody* vehicle = world.scene.get<kit::VehicleBody>(self);
        if (!vehicle) return;
        kit::Motor* motor = motorOf(world, self);
        // Another engine at the touch of a key: the quickest way to feel what an engine is.
        if (motor && switchesEngines) {
            const char* const choices[] = {"i4", "i5", "i6", "v8", "v12", "diesel", "electric"};
            for (int i = 0; i < 7; i++) {
                if (world.input.wasPressed(Key(int(Key::Num1) + i))) motor->engine = choices[i];
            }
        }
        // The dials.
        world.setValue("speed", std::round(std::abs(vehicle->speed()) * 3.6f));
        if (motor) {
            world.setValue("rpm", std::round(motor->rpm / 10.0f) * 10.0f);
            world.setValue("gear", float(motor->gear));
        }
        // The tires squeal as they slide, and the wind grows with the speed.
        if (!hasSounds) {
            if (!skidSound.empty()) skidVoice = world.startSound(skidSound, 0.0f, 1.0f, true);
            if (!windSound.empty()) windVoice = world.startSound(windSound, 0.0f, 1.0f, true);
            hasSounds = true;
        }
        float slide = 0, fastest = 0;
        world.scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
            if (kit::vehicleOf(world.scene, entity) != self) return;
            fastest = std::max(fastest, std::abs(wheel.state.spin) * wheel.radius);
            if (!wheel.state.isOnGround) return;
            // A tire pulls hardest turning a tenth faster than it rolls, and corners hardest
            // pointed 8 degrees off where it goes: past that, it slides.
            float excess = std::max(std::abs(wheel.state.slipRatio) / 0.1f, std::abs(wheel.state.slipAngle) / 0.14f) - 1.1f;
            slide = std::max(slide, std::clamp(excess * 0.6f, 0.0f, 1.0f) * std::clamp(wheel.state.load / 2500.0f, 0.0f, 1.0f));
        });
        float air = vehicle->airspeed();
        float moving = std::clamp((air + fastest) / 6.0f, 0.0f, 1.0f);
        world.changeSound(skidVoice, slide * moving * 0.7f, 0.85f + 0.3f * moving);
        world.changeSound(windVoice, std::clamp(air * air / 2500.0f, 0.0f, 1.0f) * 0.6f, 0.7f + air / 80.0f);
    }
};

MOLTEN_COMPONENT(DriveControls, MOLTEN_FIELD(switchesEngines).help("Keys 1 to 7 put another engine in"), MOLTEN_FIELD(skidSound).picks("sound"),
                 MOLTEN_FIELD(windSound).picks("sound"))
