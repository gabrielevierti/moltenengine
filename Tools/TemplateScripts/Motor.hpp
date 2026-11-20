// Motor.hpp
// The car's engine and gearbox, as this game's own script: see Motor.cpp for what it does.
// (It is in a header so that DriveControls can shift it into reverse and change engines.)
#pragma once

#include "MoltenMetal.hpp"

#include <string>

/// What an engine is like: where its torque is, how far it revs, and its gearbox.
struct EngineKind {
    const char* name;
    float idle, redline;                // revolutions per minute
    float peakTorque, peakRpm;          // newton meters, and where
    float lowShare, highShare;          // torque at idle and at the redline, as shares of the peak
    float gears[7];
    int gearCount;
    float finalDrive, reverse;
    bool isElectric;
    float layers[4];                    // the speeds its sound was recorded at: engine_<name>_<rpm>.wav
};

/// The engines there are. Add a row to add one (and four recordings of it to Audio).
const EngineKind& engineKind(const std::string& name);

struct Motor {
    std::string engine = "i4";          // "i4", "i5", "i6", "v8", "v12", "diesel" or "electric"
    bool tractionControl = true;        // eases off a wheel that spins: without it, a strong engine needs a careful foot
    std::string soundFolder = "Audio";

    void fixedUpdate(molten::World& world, molten::Entity self, float dt);
    void update(molten::World& world, molten::Entity self, float dt);

    int gear = 1;                       // -1 reverse, 1 and up forward
    float rpm = 900;
    float shiftPause = 0;               // seconds until the gearbox takes torque again
    float throttle = 0;
    uint32_t voices[4] = {0, 0, 0, 0};
    std::string soundingEngine;
};
