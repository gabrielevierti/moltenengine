// Motor.hpp
// The car's engine and gearbox, as this game's own script: see Motor.cpp for what it does.
// (It is in a header so that DriveControls can shift it into reverse and read its boost.)
#pragma once

#include "MoltenMetal.hpp"

#include <string>

/// What the engine is like: where its torque is, how far it revs, and its gearbox.
struct EngineKind {
    const char* name;
    float idle, redline;                // revolutions per minute
    float peakTorque, peakRpm;          // newton meters without boost, and where
    float lowShare, highShare;          // torque at idle and at the redline, as shares of the peak
    float gears[7];
    int gearCount;
    float finalDrive, reverse;
    float layers[4];                    // the speeds its sound was recorded at: engine_i4_<rpm>.wav
};

/// The one engine there is: a small turbocharged four.
const EngineKind& engineKind();

struct Motor {
    float maxBoost = 1.1f;              // bar of pressure the turbo gives with the pedal down
    float spoolRate = 1.8f;             // how quickly it builds: lower is more lag
    bool tractionControl = true;        // eases off a wheel that spins: without it, the turbo arriving needs a careful foot
    std::string soundFolder = "Audio";

    void fixedUpdate(molten::World& world, molten::Entity self, float dt);
    void update(molten::World& world, molten::Entity self, float dt);

    int gear = 1;                       // -1 reverse, 1 and up forward
    float rpm = 900;
    float boost = 0;                    // bar, now
    float shiftPause = 0;               // seconds until the gearbox takes torque again
    float throttle = 0;
    float heldThrottle = 0;             // the pedal a moment ago: lifting off it fast blows the valve
    bool blowsOff = false;              // set for one frame when it does
    uint32_t voices[4] = {0, 0, 0, 0};
    uint32_t turboVoice = 0;
    bool hasSounds = false;
};
