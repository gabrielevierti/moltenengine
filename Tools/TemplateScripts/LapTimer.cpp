// LapTimer.cpp
// Times laps of a circuit. The circuit has gates, entities called "Gate 1", "Gate 2" and so
// on, in the order they are passed; "Gate 1" is the start and finish line. The car this
// is on must pass each in turn (cutting across the grass does not count), and passing
// the first again ends the lap.
//
// What it tells the interface: {lap} the lap being driven, {lapTime:time} how long it has
// taken so far, {lastLap:time} and {bestLap:time}.
#include "MoltenMetal.hpp"

#include <cmath>
#include <string>

using namespace molten;

struct LapTimer {
    int gates = 4;                      // how many there are
    float gateRadius = 11.0f;           // how near a gate's middle counts as through it, in meters
    std::string lapSound;               // played when a lap ends; empty for none

    int next = 1;                       // the gate to pass next, from 0; the lap starts by leaving gate 0
    int lap = 1;
    float lapTime = 0, lastLap = 0, bestLap = 0;
    bool isRunning = false;             // the clock starts the first time the car crosses the line

    void start(World& world, Entity) {
        world.setValue("lap", 1);
        world.setValue("lapTime", 0);
        world.setValue("lastLap", 0);
        world.setValue("bestLap", 0);
    }

    void update(World& world, Entity self, float dt) {
        const Transform* car = world.scene.get<Transform>(self);
        if (!car || gates < 2) return;
        Vec3 at = car->worldPosition();
        auto isAt = [&](int gate) {
            const Transform* place = world.scene.get<Transform>(world.find("Gate " + std::to_string(gate + 1)));
            if (!place) return false;
            Vec3 away = place->worldPosition() - at;
            return away.x * away.x + away.z * away.z < gateRadius * gateRadius;
        };
        if (!isRunning) {
            // Off the line and through the second gate: the first lap is timed from the start.
            isRunning = true;
            lapTime = 0;
        }
        lapTime += dt;
        if (isAt(next)) {
            if (next == 0) {
                // Across the line with every gate behind: a lap.
                lastLap = lapTime;
                if (bestLap <= 0 || lapTime < bestLap) bestLap = lapTime;
                lapTime = 0;
                lap++;
                if (!lapSound.empty()) world.playSound(lapSound);
            }
            next = (next + 1) % gates;
        }
        world.setValue("lap", float(lap));
        world.setValue("lapTime", lapTime);
        world.setValue("lastLap", lastLap);
        world.setValue("bestLap", bestLap);
    }
};

MOLTEN_COMPONENT(LapTimer, MOLTEN_FIELD(gates).help("How many gates the circuit has: Gate 1, Gate 2..."), MOLTEN_FIELD(gateRadius).range(2, 40), MOLTEN_FIELD(lapSound))
