// Track.hpp
// The race: gates to fly through in order, laps, and the clock. Pure logic, like the
// flight model, so the rules can be tested without flying.
#pragma once

#include "Math/Math.hpp"

#include <string>
#include <vector>

namespace fpv {

/// A rectangular opening to fly through, in one direction.
struct Gate {
    molten::Vec3 center{0, 0, 0};
    float yaw = 0;                  // radians: the direction to fly through it. 0 is toward -Z
    float width = 3.2f;
    float height = 2.4f;

    static Gate at(molten::Vec3 center, float yawDegrees, float width = 3.2f, float height = 2.4f) {
        return Gate{center, molten::radians(yawDegrees), width, height};
    }
    molten::Quat rotation() const { return molten::axisAngle(molten::Vec3{0, 1, 0}, yaw); }
    /// The direction of flight through the gate.
    molten::Vec3 forward() const { return molten::rotate(rotation(), molten::Vec3{0, 0, -1}); }
    molten::Vec3 right() const { return molten::rotate(rotation(), molten::Vec3{1, 0, 0}); }

    /// Did something that moved from `start` to `end` go through the opening, the right way?
    ///
    /// The gate is a rectangle in a plane. The path crosses the plane where its distance
    /// to it changes sign; what is left is to see whether that point is inside the rectangle.
    bool isPassed(molten::Vec3 start, molten::Vec3 end) const;
};

/// Where a race stands.
class Race {
public:
    enum class Phase {
        Countdown,      // 3, 2, 1: the drone cannot take off yet
        Running,
        Finished,
    };

    Race(int gateCount, int laps) : gateCount(gateCount), laps(laps) {}

    int gateCount;
    int laps;
    Phase phase = Phase::Countdown;
    float countdown = 3;
    int nextGate = 0;
    int lap = 0;                    // laps completed
    float time = 0;
    std::vector<float> lapTimes;

    void update(float dt);
    /// Tells the race that gate number `index` was flown through. Only the next gate in
    /// order counts; returns true if it did.
    bool pass(int index);
    /// The last gate that was passed.
    int lastGate() const { return (nextGate + gateCount - 1) % gateCount; }
};

/// The course: a loop of gates around a field, with climbs, a dive and a hairpin.
const std::vector<Gate>& courseGates();

/// How far a point on the ground is from the racing line: the straight paths that join
/// each gate to the next, seen from above.
float distanceToCourse(molten::Vec2 point);

/// 83.5 seconds -> "1:23.50"
std::string formatTime(float seconds);

}  // namespace fpv
