#include "Autopilot.hpp"

using namespace molten;

namespace {

enum class Action {
    Go,             // roll to the point
    WaitForFerry,   // stay at the point until the ferry is docked nearby
    Board,          // roll onto the ferry
    Ride,           // stay on the ferry until it is close to the point
};

struct Waypoint {
    Action action;
    Vec3 point;
    float speed;            // top speed on the way there, in meters per second
};

// The route: through every gem, across on the ferry, to the goal.
const Waypoint kRoute[] = {
    {Action::Go, {0, 0, -7}, 5},            {Action::Go, {0, 0, -11}, 5},           {Action::Go, {2.2f, 0, -16}, 5},
    {Action::Go, {2.5f, 0, -18}, 4},        {Action::Go, {14.5f, 0, -18}, 5},       {Action::Go, {17, 0, -20}, 4},
    {Action::WaitForFerry, {17, 0, -15.6f}, 3}, {Action::Board, {0, 0, 0}, 3},      {Action::Ride, {17, 0, -6.6f}, 3},
    {Action::Go, {17, 0, -2}, 3},           {Action::Go, {17, 0, 4.5f}, 3},         {Action::Go, {17, 0, 8}, 2.5f},
    {Action::Go, {13, 0, 8}, 2.5f},         {Action::Go, {13, 0, 11.5f}, 2.5f},     {Action::Go, {13, 0, 18.5f}, 3},
};
const int kRouteLength = int(sizeof(kRoute) / sizeof(kRoute[0]));

float flatDistance(Vec3 a, Vec3 b) {
    return simd::length(Vec2{a.x - b.x, a.z - b.z});
}

// One gem hangs above the ferry's path, out of reach unless the marble jumps. The
// autopilot jumps when the ferry carries it past this z coordinate.
const float kJumpAtZ = -9.9f;

}  // namespace

Autopilot::Command Autopilot::steer(Engine& engine, const Level& level) {
    Command command;
    const RigidBody* marble = engine.scene.get<RigidBody>(level.marble);
    const RigidBody* ferry = engine.scene.get<RigidBody>(level.ferry);
    if (!marble || !ferry || step_ >= kRouteLength) return command;

    const Waypoint& waypoint = kRoute[step_];
    Vec3 target = waypoint.point;
    bool isDone = false;
    switch (waypoint.action) {
        case Action::Go:
            isDone = flatDistance(marble->position, target) < 0.45f;
            break;
        case Action::WaitForFerry:
            // Docked means: at the near end of its trip and almost standing still.
            isDone = flatDistance(marble->position, target) < 0.6f && ferry->position.z < -12.2f && simd::length(ferry->velocity) < 0.5f;
            break;
        case Action::Board:
            target = ferry->position;
            isDone = flatDistance(marble->position, target) < 0.5f;
            break;
        case Action::Ride:
            target = ferry->position;
            isDone = ferry->position.z > waypoint.point.z - 0.35f;
            if (!hasJumped_ && marble->position.z > kJumpAtZ && ferry->velocity.z > 0) {
                command.jump = true;
                hasJumped_ = true;
            }
            break;
    }
    if (isDone) {
        step_++;
        return command;
    }

    // Steering: decide the velocity we would like (toward the target, slower when close),
    // then push in proportion to how far the real velocity is from it. On the ferry the
    // velocity we want is the ferry's own, plus whatever keeps us near its center.
    Vec3 toTarget = target - marble->position;
    Vec3 wanted = Vec3{toTarget.x, 0, toTarget.z} * 2.5f;
    float wantedSpeed = simd::length(wanted);
    if (wantedSpeed > waypoint.speed) wanted *= waypoint.speed / wantedSpeed;
    if (waypoint.action == Action::Board || waypoint.action == Action::Ride) wanted += ferry->velocity;
    Vec3 correction = (wanted - marble->velocity) * 0.6f;
    command.push = Vec2{correction.x, -correction.z};  // north is -z
    float length = simd::length(command.push);
    if (length > 1.0f) command.push /= length;
    return command;
}
