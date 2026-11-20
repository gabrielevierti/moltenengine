// FlightControls.cpp
// The pilot: reads the keys (or a radio's sticks) and moves the aeroplane's controls. This
// is the game's own script; the aeroplane itself is made of the engine's parts
// (VehicleBody, Wing, Hinge, Propeller, Wheel), which read `VehicleBody::controls`.
//
//   S W       pull and push the stick: climb and dive
//   A D       roll
//   Q E       rudder
//   Shift Control   more and less power (or the throttle stick of a radio)
//   F         flaps: up, half, full
//   B         wheel brakes
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

struct FlightControls {
    /// Keys are all or nothing, and a real stick held hard over throws an aeroplane about.
    /// With this on, the keys ask for a rate of roll and of pitch and the controls are moved
    /// to give it; let go and it levels off; it will not stall, loop or roll over.
    /// Off: the keys are the stick itself.
    bool assist = true;
    float startThrottle = 0;            // 0 to 1: what the lever is set to when the scene starts
    float maxBank = 45;                 // degrees, with the assist
    std::string windSound = "Audio/wind.wav";
    std::string stallSound = "Audio/stall.wav";

    float flapsWanted = 0;
    float trim = 0;                     // what the assist has learned it takes to fly level
    bool hasStarted = false, hasSounds = false;
    uint32_t windVoice = 0, stallVoice = 0;

    static float toward(float value, float target, float step) {
        return value < target ? std::min(value + step, target) : std::max(value - step, target);
    }

    void fixedUpdate(World& world, Entity self, float dt) {
        kit::VehicleBody* plane = world.scene.get<kit::VehicleBody>(self);
        if (!plane) return;
        kit::Controls& controls = plane->controls;
        if (!hasStarted) {
            controls.throttle = saturate(startThrottle);
            hasStarted = true;
        }
        if (!plane->controlsAreGiven) fly(world, *plane, dt);
        controls.flaps = toward(controls.flaps, flapsWanted, dt * 0.5f);    // flaps travel slowly

        world.setValue("airspeed", std::round(plane->airspeed() * 3.6f));
        world.setValue("altitude", std::round(plane->body.position.y));
        world.setValue("throttle", std::round(controls.throttle * 100.0f));
        world.setValue("climb", std::round(plane->body.velocity.y * 10.0f) / 10.0f);
        world.setValue("flaps", std::round(controls.flaps * 100.0f));
        world.setValue("stall", plane->isStalled ? 1.0f : 0.0f);
    }

    void fly(World& world, kit::VehicleBody& plane, float dt) {
        const Input& input = world.input;
        kit::Controls& controls = plane.controls;
        const kit::Body6& body = plane.body;
        float wantPitch = input.axis("moveZ"), wantRoll = input.axis("moveX"), wantYaw = input.axis("yaw");     // S pulls the stick back: nose up
        float speed = plane.airspeed();
        if (assist && speed > 14.0f) {
            // How it is turning now, about its own axes: nose up, rolling right, nose right.
            Vec3 spin = body.toLocal(body.angularVelocity);
            float pitchRate = spin.x, rollRate = -spin.z, yawRate = -spin.y;
            float bank = -std::asin(std::clamp(body.right().y, -1.0f, 1.0f));       // right wing down is positive
            float limit = radians(maxBank);
            // Roll: the keys ask for a rate; with none pressed, for the wings to come level.
            float rollWanted = std::abs(wantRoll) > 0.05f ? wantRoll * 0.9f : std::clamp(-bank * 1.6f, -0.7f, 0.7f);
            if (bank * wantRoll > 0 && std::abs(bank) > limit) rollWanted = std::clamp((limit - std::abs(bank)) * 2.0f, -0.5f, 0.0f) * (bank > 0 ? 1.0f : -1.0f);
            // Pitch: a rate too.
            float pitchWanted = wantPitch * 0.4f;
            if (std::abs(wantPitch) < 0.05f) {
                // Let go, it levels off: the path it flies along is brought back to the horizon,
                // and what it takes to hold it there is learned, as a pilot trims.
                float path = std::asin(std::clamp(body.velocity.y / std::max(speed, 1.0f), -1.0f, 1.0f));
                pitchWanted = std::clamp(-path * 1.1f, -0.22f, 0.22f);
                if (std::abs(path) < radians(5.0f)) trim = std::clamp(trim - path * 0.3f * dt, -0.5f, 0.5f);
            }
            // Not into a loop: the nose is not taken much past thirty degrees up or down.
            float nose = std::asin(std::clamp(body.forward().y, -1.0f, 1.0f));
            if (nose * pitchWanted > 0 && std::abs(nose) > radians(22.0f)) pitchWanted *= saturate((radians(32.0f) - std::abs(nose)) / radians(10.0f));
            // Never past the stall.
            pitchWanted = std::min(pitchWanted, (plane.stallAngle - radians(3.5f) - plane.angleOfAttack) * 5.0f);
            // In a bank the nose would fall: a little back pressure, as a pilot gives without thinking.
            pitchWanted += (1.0f / std::max(std::cos(bank), 0.5f) - 1.0f) * 9.81f / std::max(speed, 20.0f);
            // The faster it flies the less the surfaces need to move for the same effect.
            float effect = std::clamp(900.0f / (speed * speed), 0.25f, 1.6f);
            wantRoll = std::clamp((rollWanted - rollRate) * 1.1f * effect, -1.0f, 1.0f);
            wantPitch = std::clamp((pitchWanted - pitchRate) * 3.2f * effect + trim, -1.0f, 1.0f);
            // The rudder: what the pedals ask for, less whatever the nose is already swinging by, and
            // a push back toward flying straight through the air instead of sideways through it.
            Vec3 through = body.toLocal(body.velocity);
            float slip = std::atan2(through.x, std::max(-through.z, 1.0f));     // the air coming from the right is positive
            wantYaw = std::clamp(wantYaw * 0.5f - yawRate * 2.2f * effect + (std::abs(wantYaw) < 0.05f ? slip * 1.5f : 0.0f), -1.0f, 1.0f);       // the rudder damps the tail's wagging
            controls.pitch = toward(controls.pitch, wantPitch, dt * 9.0f);
            controls.roll = toward(controls.roll, wantRoll, dt * 9.0f);
            controls.yaw = toward(controls.yaw, wantYaw, dt * 9.0f);
        } else {
            // The controls move at the speed hands move them, not at the speed keys change.
            controls.pitch = toward(controls.pitch, wantPitch, dt * 3.5f);
            controls.roll = toward(controls.roll, wantRoll, dt * 4.5f);
            controls.yaw = toward(controls.yaw, wantYaw, dt * 4.0f);
        }
        // A real lever says where it is; keys can only push it up and down.
        if (input.hasJoystick("throttleLever")) controls.throttle = saturate(input.axis("throttleLever") * 0.5f + 0.5f);
        else controls.throttle = saturate(controls.throttle + input.axis("throttle") * dt * 0.6f);
        if (input.wasPressed("flaps")) flapsWanted = flapsWanted > 0.75f ? 0.0f : flapsWanted + 0.5f;
        controls.brake = input.isDown("brake") ? 1.0f : 0.0f;
        controls.steer = controls.yaw;
    }

    void update(World& world, Entity self, float) {
        const kit::VehicleBody* plane = world.scene.get<kit::VehicleBody>(self);
        if (!plane) return;
        if (!hasSounds) {
            if (!windSound.empty()) windVoice = world.startSound(windSound, 0.0f, 1.0f, true);
            if (!stallSound.empty()) stallVoice = world.startSound(stallSound, 0.0f, 1.0f, true);
            hasSounds = true;
        }
        float speed = plane->airspeed();
        world.changeSound(windVoice, saturate(speed * speed / 3600.0f) * 0.6f, 0.7f + speed / 90.0f);
        world.changeSound(stallVoice, plane->isStalled ? 0.5f : 0.0f, 1.0f);
    }
};

MOLTEN_COMPONENT(FlightControls, MOLTEN_FIELD(assist).help("Keys ask for a rate of roll and pitch; let go and it levels off; it will not stall or roll over"),
                 MOLTEN_FIELD(startThrottle).range(0, 1), MOLTEN_FIELD(maxBank).range(10, 80), MOLTEN_FIELD(windSound), MOLTEN_FIELD(stallSound))
