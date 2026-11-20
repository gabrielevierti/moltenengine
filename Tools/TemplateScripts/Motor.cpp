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

// A small four with a turbo: it has to be revved, the boost arrives with a shove at 3000,
// and lifting off lets the pressure out with a hiss.
const EngineKind kEngine = {"i4", 850, 6800, 175, 4000, 0.62f, 0.7f, {3.58f, 2.05f, 1.42f, 1.08f, 0.87f, 0.72f}, 6, 4.1f, 3.4f, {1000, 2600, 4400, 6200}};

/// The torque with the pedal down, at a speed.
float torqueAt(const EngineKind& kind, float rpm) {
    if (rpm > kind.redline) return 0;                               // the limiter
    if (rpm <= kind.peakRpm) {
        float t = saturate((rpm - kind.idle) / std::max(kind.peakRpm - kind.idle, 1.0f));
        t = t * t * (3 - 2 * t);
        return kind.peakTorque * (kind.lowShare + (1 - kind.lowShare) * t);
    }
    float t = (rpm - kind.peakRpm) / std::max(kind.redline - kind.peakRpm, 1.0f);
    return kind.peakTorque * (1 - (1 - kind.highShare) * t * t);
}

}  // namespace

const EngineKind& engineKind() { return kEngine; }

void Motor::fixedUpdate(World& world, Entity self, float dt) {
    Scene& scene = world.scene;
    Entity car = kit::vehicleOf(scene, self);
    kit::VehicleBody* body = scene.get<kit::VehicleBody>(car);
    if (!body) return;
    const EngineKind& kind = engineKind();
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
    float engineRpm = std::max(wheelRpm, kind.idle + throttle * 1100.0f);
    rpm += (engineRpm - rpm) * std::min(1.0f, dt * 14.0f);
    shiftPause = std::max(0.0f, shiftPause - dt);

    // The turbo: the exhaust spins it, so the pressure it gives follows the pedal and the revs,
    // late (the lag); with the pedal up the valve lets it out at once, and blows.
    float wanted = maxBoost * throttle * saturate((rpm - 2200.0f) / 1800.0f);
    if (shiftPause > 0) wanted *= 0.6f;
    boost += (wanted - boost) * std::min(1.0f, dt * (wanted > boost ? spoolRate : 7.0f));
    heldThrottle += (throttle - heldThrottle) * std::min(1.0f, dt * 3.0f);
    if (throttle < 0.25f && heldThrottle > 0.55f && boost > 0.35f * maxBoost) {
        blowsOff = true;
        heldThrottle = throttle;
    }

    float torque = shiftPause > 0 ? 0.0f : torqueAt(kind, engineRpm) * (1.0f + 0.62f * boost) * throttle;
    // With the pedal up the engine holds the car back, more the faster it turns.
    torque -= (1 - throttle) * kind.peakTorque * 0.12f * (engineRpm / kind.redline) * (wheelRpm > kind.idle ? 1.0f : 0.0f);
    float perWheel = torque * ratio * 0.9f / float(driven);
    scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
        if (kit::vehicleOf(scene, entity) != car) return;
        wheel.driveTorque = wheel.isDriven ? perWheel : 0.0f;
        wheel.easesWhenSpinning = tractionControl;
    });

    // The gearbox: up near the top, down when the engine labors. It goes by the speed of
    // the road, not of the wheels: spinning them is no reason to change up.
    float roadRpm = std::abs(body->speed()) / std::max(radius, 0.05f) * std::abs(ratio) * 60.0f / (2.0f * kPi);
    if (gear >= 1 && shiftPause <= 0) {
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
    // The sound: four recordings of the engine at different speeds, blended by how fast it
    // turns (one recording played faster or slower sounds like a toy), the turbo's whistle
    // over it, and the valve's hiss when the pedal comes up.
    const EngineKind& kind = engineKind();
    if (!hasSounds && !soundFolder.empty()) {
        for (int layer = 0; layer < 4; layer++) {
            voices[layer] = world.startSound(soundFolder + "/engine_i4_" + std::to_string(int(kind.layers[layer])) + ".wav", 0.0f, 1.0f, true);
        }
        turboVoice = world.startSound(soundFolder + "/turbo.wav", 0.0f, 1.0f, true);
        hasSounds = true;
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
    float spooled = maxBoost > 0 ? saturate(boost / maxBoost) : 0.0f;
    world.changeSound(turboVoice, 0.22f * spooled * spooled, 0.7f + 0.7f * spooled + rpm / 30000.0f);
    if (blowsOff) {
        if (!soundFolder.empty()) world.playSound(soundFolder + "/blowoff.wav", 0.45f + 0.5f * spooled);
        blowsOff = false;
    }
    world.setValue("boost", boost);
    world.setValue("boostShare", spooled);
}

MOLTEN_COMPONENT(Motor, MOLTEN_FIELD(maxBoost).range(0, 2.5f).help("Bar of pressure the turbo gives with the pedal down"),
                 MOLTEN_FIELD(spoolRate).range(0.2f, 8).help("How quickly the boost builds: lower is more lag"), MOLTEN_FIELD(tractionControl),
                 MOLTEN_FIELD(soundFolder))
