// FlightControls.cpp
// The pilot's hands and feet: reads the keys and moves the controls. This game's own script:
// change it as the game needs. The aeroplane itself is made of the engine's parts (a Vehicle
// Body, Wings, Hinges for the ailerons, flaps, elevator and rudder, a Propeller, Wheels).
//
//   S W           pull and push the stick (the action moveZ)
//   A D           roll (moveX)
//   Q E           the rudder (yaw)
//   Shift Ctrl    the throttle up and down (throttle), or a real lever (throttleLever)
//   F             the flaps, a step at a time (flaps)
//   B             the brakes (brake)
//
// Keys are all or nothing, and a real stick held hard over throws an aeroplane about. With
// `assist` on, the keys ask for a rate of roll and of pitch and the controls are moved to give
// it: the wings come level when the keys are let go, the nose is kept off the stall, and in a
// turn it is held up. It writes the shared values "airspeed" (km/h), "altitude", "climb",
// "throttle", "flaps" and "stall", for the dials.
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

float saturated(float value) { return std::clamp(value, 0.0f, 1.0f); }

}  // namespace

struct FlightControls {
    bool assist = true;
    float startThrottle = 0;            // 0 to 1: where the lever is when the scene starts
    std::string windSound = "Audio/wind.wav";
    std::string stallSound = "Audio/stall.wav";

    float flapsWanted = 0;
    float trim = 0;                     // what the assist has learned it takes to fly level
    bool hasStarted = false;
    uint32_t windVoice = 0, stallVoice = 0;
    bool hasSounds = false;

    void fixedUpdate(World& world, Entity self, float dt) {
        kit::VehicleBody* vehicle = world.scene.get<kit::VehicleBody>(self);
        if (!vehicle) return;
        const Input& input = world.input;
        kit::Controls& controls = vehicle->controls;
        const kit::Body6& body = vehicle->body;
        if (!hasStarted) {
            controls.throttle = saturated(startThrottle);
            hasStarted = true;
        }
        float wantPitch = input.axis("moveZ"), wantRoll = input.axis("moveX"), wantYaw = input.axis("yaw");     // S pulls the stick back: nose up
        float speed = vehicle->airspeed();
        if (assist && speed > 14.0f) {
            // How it is turning now, about its own axes: nose up, rolling right, nose right.
            Vec3 spin = body.toLocal(body.angularVelocity);
            float pitchRate = spin.x, rollRate = -spin.z, yawRate = -spin.y;
            float bank = -std::asin(std::clamp(body.right().y, -1.0f, 1.0f));       // right wing down is positive
            // Roll: the keys ask for a rate; with none pressed, for the wings to come level.
            float rollWanted = std::abs(wantRoll) > 0.05f ? wantRoll * 0.9f : std::clamp(-bank * 1.6f, -0.7f, 0.7f);
            // Not past forty-five degrees of bank by keys alone.
            if (bank * wantRoll > 0 && std::abs(bank) > radians(45.0f)) {
                rollWanted = std::clamp((radians(45.0f) - std::abs(bank)) * 2.0f, -0.5f, 0.0f) * (bank > 0 ? 1.0f : -1.0f);
            }
            // Pitch: a rate too. With the keys let go it levels off: the path it flies along, not
            // only its nose, is brought back to the horizon, so that it neither sinks away nor
            // sails up and down as it trades speed for height.
            float pitchWanted = wantPitch * 0.4f;
            if (std::abs(wantPitch) < 0.05f) {
                float path = std::asin(std::clamp(body.velocity.y / std::max(speed, 1.0f), -1.0f, 1.0f));
                pitchWanted = std::clamp(-path * 1.1f, -0.22f, 0.22f);
                // And what it takes to hold it there is learned, as a pilot trims.
                if (std::abs(path) < radians(5.0f)) trim = std::clamp(trim - path * 0.3f * dt, -0.5f, 0.5f);
            }
            // Nor into a loop: the nose is not taken past thirty degrees up or down by keys alone.
            float nose = std::asin(std::clamp(body.forward().y, -1.0f, 1.0f));
            if (nose * pitchWanted > 0 && std::abs(nose) > radians(22.0f)) pitchWanted *= saturated((radians(32.0f) - std::abs(nose)) / radians(10.0f));
            // Never past the stall.
            float margin = vehicle->stallAngle - radians(3.5f) - vehicle->angleOfAttack;
            pitchWanted = std::min(pitchWanted, margin * 5.0f);
            // In a bank the nose would fall: a little back pressure, as a pilot gives without thinking.
            pitchWanted += (1.0f / std::max(std::cos(bank), 0.5f) - 1.0f) * 9.81f / std::max(speed, 20.0f);
            // The faster it flies, the less the surfaces need to move for the same effect.
            float effect = std::clamp(900.0f / (speed * speed), 0.25f, 1.6f);
            wantRoll = std::clamp((rollWanted - rollRate) * 1.1f * effect, -1.0f, 1.0f);
            wantPitch = std::clamp((pitchWanted - pitchRate) * 3.2f * effect + trim, -1.0f, 1.0f);
            // The rudder: what the pedals ask for, less what the nose is already swinging by, and
            // a push back toward flying straight through the air instead of sideways through it.
            Vec3 through = body.toLocal(body.velocity);
            float slip = std::atan2(through.x, std::max(-through.z, 1.0f));     // the air coming from the right is positive
            wantYaw = std::clamp(wantYaw * 0.5f - yawRate * 2.2f * effect + (std::abs(wantYaw) < 0.05f ? slip * 1.5f : 0.0f), -1.0f, 1.0f);
            controls.pitch = approach(controls.pitch, wantPitch, dt * 9.0f);
            controls.roll = approach(controls.roll, wantRoll, dt * 9.0f);
            controls.yaw = approach(controls.yaw, wantYaw, dt * 9.0f);
        } else {
            // The controls move at the speed hands move them, not at the speed keys change.
            controls.pitch = approach(controls.pitch, wantPitch, dt * 3.5f);
            controls.roll = approach(controls.roll, wantRoll, dt * 4.5f);
            controls.yaw = approach(controls.yaw, wantYaw, dt * 4.0f);
        }
        // A real lever says where it is; keys can only push it up and down.
        if (input.hasJoystick("throttleLever")) controls.throttle = saturated(input.axis("throttleLever") * 0.5f + 0.5f);
        else controls.throttle = saturated(controls.throttle + input.axis("throttle") * dt * 0.6f);
        // The flaps go down slowly, as a motor runs them.
        controls.flaps = approach(controls.flaps, flapsWanted, dt * 0.5f);
        controls.brake = input.isDown("brake") ? 1.0f : 0.0f;
        controls.steer = controls.yaw;                          // the nose wheel turns with the rudder
    }

    void update(World& world, Entity self, float) {
        kit::VehicleBody* vehicle = world.scene.get<kit::VehicleBody>(self);
        if (!vehicle) return;
        if (world.input.wasPressed("flaps")) flapsWanted = flapsWanted > 0.75f ? 0.0f : flapsWanted + 0.5f;
        // The dials.
        world.setValue("airspeed", std::round(vehicle->airspeed() * 3.6f));
        world.setValue("altitude", std::round(vehicle->body.position.y));
        world.setValue("throttle", std::round(vehicle->controls.throttle * 100.0f));
        world.setValue("climb", std::round(vehicle->body.velocity.y * 10.0f) / 10.0f);
        world.setValue("flaps", std::round(vehicle->controls.flaps * 100.0f));
        world.setValue("stall", vehicle->isStalled ? 1.0f : 0.0f);
        // The wind grows with the speed; the stall warning sounds while the wings have lost their lift.
        if (!hasSounds) {
            if (!windSound.empty()) windVoice = world.startSound(windSound, 0.0f, 1.0f, true);
            if (!stallSound.empty()) stallVoice = world.startSound(stallSound, 0.0f, 1.0f, true);
            hasSounds = true;
        }
        float speed = vehicle->airspeed();
        world.changeSound(windVoice, saturated(speed * speed / 3600.0f) * 0.6f, 0.7f + speed / 90.0f);
        world.changeSound(stallVoice, vehicle->isStalled ? 0.5f : 0.0f, 1.0f);
    }
};

MOLTEN_COMPONENT(FlightControls, MOLTEN_FIELD(assist).help("Keys ask for a rate of roll and pitch; the wings level themselves; no stall. Off: the keys are the stick"),
                 MOLTEN_FIELD(startThrottle).range(0, 1).help("Where the throttle lever is when the scene starts"), MOLTEN_FIELD(windSound).picks("sound"),
                 MOLTEN_FIELD(stallSound).picks("sound"))
