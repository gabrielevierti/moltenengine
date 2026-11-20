// LapTimer.cpp
// Times laps of a circuit. The circuit has gates, entities called "Gate 1", "Gate 2" and so
// on, in the order they are passed; "Gate 1" is the start and finish line. The car this
// is on must pass each in turn (cutting across the grass does not count), and passing
// the first again ends the lap. The pit lane passes the line too, at "Pit Gate".
//
// Track limits: the road is the line through "Waypoint 1", "Waypoint 2"... with
// `trackHalfWidth` meters each side of it (the curbs included), and the pit lane the line
// through "Pit Point 1", "Pit Point 2"... When all four wheels are off both, on the grass,
// the lap no longer counts: it is finished, and timed, but it is not a best lap.
//
// What it tells the interface: {lap} the lap being driven, {lapTime:time} how long it has
// taken so far, {lastLap:time} and {bestLap:time}; {lapValid} 1, or 0 once it no longer
// counts; {lastLapValid}.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace molten;

struct LapTimer {
    int gates = 4;                      // how many there are
    float gateRadius = 11.0f;           // how near a gate's middle counts as through it, in meters
    float trackHalfWidth = 6.4f;        // from the middle of the road to the outer edge of the curbs
    float pitHalfWidth = 3.6f;
    std::string lapSound;               // played when a lap ends; empty for none

    int next = 1;                       // the gate to pass next, from 0; the lap starts by leaving gate 0
    int lap = 1;
    float lapTime = 0, lastLap = 0, bestLap = 0;
    bool isRunning = false;             // the clock starts the first time the car crosses the line
    bool isValid = true, lastWasValid = true;
    float offFor = 0;                   // how long all four wheels have been off the track
    std::vector<Vec3> road, pit;        // the two middle lines, read once

    static std::vector<Vec3> lineOf(World& world, const std::string& prefix) {
        std::vector<Vec3> points;
        for (int k = 1; k < 2000; k++) {
            const Transform* point = world.scene.get<Transform>(world.find(prefix + std::to_string(k)));
            if (!point) break;
            points.push_back(point->worldPosition());
        }
        return points;
    }

    /// How far a point is from a line through `points` (closed: the last joins the first), across the ground.
    static float distanceTo(const std::vector<Vec3>& points, bool closed, Vec3 at) {
        float best = 1e9f;
        size_t count = points.size();
        for (size_t i = 0; i + (closed ? 0 : 1) < count; i++) {
            Vec3 a = points[i], b = points[(i + 1) % count];
            float ax = b.x - a.x, az = b.z - a.z, px = at.x - a.x, pz = at.z - a.z;
            float along = std::clamp((px * ax + pz * az) / std::max(ax * ax + az * az, 1e-6f), 0.0f, 1.0f);
            float dx = px - ax * along, dz = pz - az * along;
            best = std::min(best, dx * dx + dz * dz);
        }
        return std::sqrt(best);
    }

    bool isOnTrack(Vec3 at) const {
        if (road.size() >= 3 && distanceTo(road, true, at) <= trackHalfWidth) return true;
        return pit.size() >= 2 && distanceTo(pit, false, at) <= pitHalfWidth;
    }

    void start(World& world, Entity) {
        road = lineOf(world, "Waypoint ");
        pit = lineOf(world, "Pit Point ");
        world.setValue("lap", 1);
        world.setValue("lapTime", 0);
        world.setValue("lastLap", 0);
        world.setValue("bestLap", world.preference("bestLap", 0));
        bestLap = world.preference("bestLap", 0);
        world.setValue("lapValid", 1);
        world.setValue("lastLapValid", 1);
    }

    void update(World& world, Entity self, float dt) {
        const Transform* car = world.scene.get<Transform>(self);
        if (!car || gates < 2) return;
        Vec3 at = car->worldPosition();
        auto near = [&](const std::string& name, float radius) {
            const Transform* place = world.scene.get<Transform>(world.find(name));
            if (!place) return false;
            Vec3 away = place->worldPosition() - at;
            return away.x * away.x + away.z * away.z < radius * radius;
        };
        auto isAt = [&](int gate) {
            if (gate == 0 && near("Pit Gate", pitHalfWidth + 1.5f)) return true;
            return near("Gate " + std::to_string(gate + 1), gateRadius);
        };
        if (!isRunning) {
            isRunning = true;
            lapTime = 0;
        }
        lapTime += dt;

        // Track limits: every wheel of this car off the road and off the pit lane.
        if (dt > 0 && !road.empty()) {
            int wheels = 0, off = 0;
            world.scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel&, Transform& wheel) {
                if (kit::vehicleOf(world.scene, entity) != self) return;
                wheels++;
                if (!isOnTrack(wheel.worldPosition())) off++;
            });
            offFor = wheels > 0 && off == wheels ? offFor + dt : 0.0f;
            // A moment, not a touch: a wheel thrown up by a curb is not a lap lost.
            if (offFor > 0.15f && isValid) {
                isValid = false;
                world.setValue("lapInvalidAt", world.time.elapsed);
            }
        }

        if (isAt(next)) {
            if (next == 0) {
                // Across the line with every gate behind: a lap.
                lastLap = lapTime;
                lastWasValid = isValid;
                if (isValid && (bestLap <= 0 || lapTime < bestLap)) {
                    bestLap = lapTime;
                    world.setPreference("bestLap", bestLap);        // a record is kept from one time to the next
                }
                lapTime = 0;
                lap++;
                isValid = true;
                if (!lapSound.empty()) world.playSound(lapSound);
            }
            next = (next + 1) % gates;
        }
        world.setValue("lap", float(lap));
        world.setValue("lapTime", lapTime);
        world.setValue("lastLap", lastLap);
        world.setValue("bestLap", bestLap);
        world.setValue("lapValid", isValid ? 1.0f : 0.0f);
        world.setValue("lastLapValid", lastWasValid ? 1.0f : 0.0f);
    }
};

MOLTEN_COMPONENT(LapTimer, MOLTEN_FIELD(gates).help("How many gates the circuit has: Gate 1, Gate 2..."), MOLTEN_FIELD(gateRadius).range(2, 40),
                 MOLTEN_FIELD(trackHalfWidth).range(1, 30).help("From the middle of the road (the Waypoints) to the edge of the curbs: beyond it with every wheel, the lap does not count"),
                 MOLTEN_FIELD(pitHalfWidth).range(1, 10), MOLTEN_FIELD(lapSound))
