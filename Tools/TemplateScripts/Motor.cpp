// Motor.cpp
// An engine behind an automatic gearbox. Every fixed step it works out how fast the driven
// wheels are turning it, how much torque it gives at that speed with the pedal where it
// is, and passes that torque through the gear to the wheels. The wheels themselves, their
// grip and the car's body are the engine's parts (Wheel, Suspension, VehicleBody).
#include "Motor.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

namespace {

const EngineKind kEngines[] = {
    // A small four: has to be revved, and likes it.
    {"i4", 850, 6800, 190, 4300, 0.62f, 0.74f, {3.58f, 2.05f, 1.42f, 1.08f, 0.87f, 0.72f}, 6, 4.1f, 3.4f, false, {1000, 2600, 4400, 6200}},
    // A big eight: torque everywhere, long gears.
    {"v8", 700, 6500, 560, 4400, 0.72f, 0.76f, {2.97f, 2.07f, 1.43f, 1.0f, 0.84f, 0.56f}, 6, 3.42f, 3.0f, false, {800, 2000, 3700, 5600}},
    // A turbo diesel: all of it early, nothing at the top.
    {"diesel", 800, 4800, 420, 2000, 0.5f, 0.48f, {3.8f, 2.1f, 1.3f, 0.95f, 0.75f, 0.62f}, 6, 3.2f, 3.5f, false, {900, 1700, 2800, 4200}},
    // A five: an odd number of cylinders and a warble nobody mistakes.
    {"i5", 800, 7000, 320, 4200, 0.66f, 0.74f, {3.6f, 2.1f, 1.45f, 1.1f, 0.88f, 0.72f}, 6, 3.9f, 3.4f, false, {900, 2500, 4300, 6200}},
    // A straight six: smooth all the way up.
    {"i6", 750, 7200, 400, 4500, 0.68f, 0.78f, {3.5f, 2.05f, 1.4f, 1.05f, 0.85f, 0.7f}, 6, 3.6f, 3.3f, false, {800, 2400, 4400, 6500}},
    // A twelve: revs forever, and makes its power at the top.
    {"v12", 900, 8500, 690, 6000, 0.6f, 0.82f, {3.1f, 2.1f, 1.55f, 1.2f, 0.97f, 0.8f, 0.67f}, 7, 3.7f, 2.9f, false, {900, 2800, 5200, 8000}},
    // An electric motor: full torque from nothing, one gear, no idle.
    {"electric", 0, 15000, 430, 5200, 1.0f, 0.35f, {1.0f}, 1, 9.0f, 1.0f, true, {1500, 4500, 9000, 14000}},
};

/// The torque with the pedal down, at a speed.
float torqueAt(const EngineKind& kind, float rpm) {
    if (rpm > kind.redline) return 0;                               // the limiter
    if (kind.isElectric) return rpm <= kind.peakRpm ? kind.peakTorque : kind.peakTorque * kind.peakRpm / rpm;   // then constant power
    if (rpm <= kind.peakRpm) {
        float t = saturate((rpm - kind.idle) / std::max(kind.peakRpm - kind.idle, 1.0f));
        t = t * t * (3 - 2 * t);
        return kind.peakTorque * (kind.lowShare + (1 - kind.lowShare) * t);
    }
    float t = (rpm - kind.peakRpm) / std::max(kind.redline - kind.peakRpm, 1.0f);
    return kind.peakTorque * (1 - (1 - kind.highShare) * t * t);
}

}  // namespace

const EngineKind& engineKind(const std::string& name) {
    for (const EngineKind& kind : kEngines) {
        if (name == kind.name) return kind;
    }
    return kEngines[0];
}

void Motor::fixedUpdate(World& world, Entity self, float dt) {
    Scene& scene = world.scene;
    Entity car = kit::vehicleOf(scene, self);
    kit::VehicleBody* body = scene.get<kit::VehicleBody>(car);
    if (!body) return;
    const EngineKind& kind = engineKind(engine);
    throttle = body->controls.throttle;
    gear = std::clamp(gear, -1, kind.gearCount);
    if (gear == 0) gear = 1;

    // The wheels it turns, and how fast they are turning.
    int driven = 0;
    float spin = 0, radius = 0.33f;
    scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
        if (kit::vehicleOf(scene, entity) != car || !wheel.isDriven) return;
        driven++;
        spin += wheel.state.spin;
        radius = wheel.radius;
    });
    if (driven == 0) return;
    spin /= float(driven);

    float ratio = (gear == -1 ? -kind.reverse : kind.gears[gear - 1]) * kind.finalDrive;
    float wheelRpm = std::abs(spin * ratio) * 60.0f / (2.0f * kPi);
    // Below idle the clutch slips: the engine keeps turning while the car stands.
    float engineRpm = std::max(wheelRpm, kind.idle + throttle * (kind.isElectric ? 0.0f : 1100.0f));
    rpm += (engineRpm - rpm) * std::min(1.0f, dt * 14.0f);
    shiftPause = std::max(0.0f, shiftPause - dt);
    float torque = shiftPause > 0 ? 0.0f : torqueAt(kind, engineRpm) * throttle;
    // With the pedal up the engine holds the car back, more the faster it turns.
    if (!kind.isElectric) torque -= (1 - throttle) * kind.peakTorque * 0.12f * (engineRpm / kind.redline) * (wheelRpm > kind.idle ? 1.0f : 0.0f);
    else torque -= (1 - throttle) * 40.0f * saturate(wheelRpm / 800.0f);       // a little regeneration
    float perWheel = torque * ratio * 0.9f / float(driven);
    scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
        if (kit::vehicleOf(scene, entity) != car) return;
        wheel.driveTorque = wheel.isDriven ? perWheel : 0.0f;
        wheel.easesWhenSpinning = tractionControl;
    });

    // The gearbox: up near the top, down when the engine labors. It goes by the speed of
    // the road, not of the wheels: spinning them is no reason to change up.
    float roadRpm = std::abs(body->speed()) / std::max(radius, 0.05f) * std::abs(ratio) * 60.0f / (2.0f * kPi);
    if (gear >= 1 && shiftPause <= 0 && !kind.isElectric) {
        if (roadRpm > kind.redline * 0.9f && gear < kind.gearCount) {
            gear++;
            shiftPause = 0.22f;
        } else if (gear > 1 && roadRpm < kind.redline * (0.3f + 0.12f * throttle)) {
            float lower = kind.gears[gear - 2] / kind.gears[gear - 1] * roadRpm;
            if (lower < kind.redline * 0.82f) {
                gear--;
                shiftPause = 0.18f;
            }
        }
    }
}

void Motor::update(World& world, Entity, float) {
    // The sound: four recordings of each engine at different speeds, blended by how fast it
    // turns. One recording played faster or slower sounds like a toy.
    const EngineKind& kind = engineKind(engine);
    if (soundingEngine != engine) {
        for (uint32_t& voice : voices) {
            world.stopSound(voice);
            voice = 0;
        }
        if (!soundFolder.empty()) {
            for (int layer = 0; layer < 4; layer++) {
                voices[layer] = world.startSound(soundFolder + "/engine_" + engine + "_" + std::to_string(int(kind.layers[layer])) + ".wav", 0.0f, 1.0f, true);
            }
        }
        soundingEngine = engine;
    }
    float loudness = 0.32f + 0.6f * throttle;
    for (int layer = 0; layer < 4; layer++) {
        float own = kind.layers[layer];
        float below = layer > 0 ? kind.layers[layer - 1] : own, above = layer < 3 ? kind.layers[layer + 1] : own;
        float weight = 1.0f;
        if (rpm < own && layer > 0) weight = saturate((rpm - below) / (own - below));
        else if (rpm > own && layer < 3) weight = saturate((above - rpm) / (above - own));
        world.changeSound(voices[layer], loudness * std::sqrt(weight), std::clamp(rpm / own, 0.5f, 2.0f));
    }
}

MOLTEN_COMPONENT(Motor, MOLTEN_FIELD(engine).choices({"i4", "i5", "i6", "v8", "v12", "diesel", "electric"}), MOLTEN_FIELD(tractionControl), MOLTEN_FIELD(soundFolder))
