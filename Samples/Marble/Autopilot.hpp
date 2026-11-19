// Autopilot.hpp
// Plays the course by itself: `marble --demo`.
//
// It exists for two reasons. Watching the game play is a nice way to see the engine
// work; and "the autopilot reaches the goal" is a test of everything at once (physics,
// triggers, the level itself) that can run unattended on a build machine.
//
// The autopilot is a list of waypoints and a steering rule. It reads the same things a
// player sees (where the marble is, where the ferry is) and produces the same thing a
// player produces: a stick direction.
#pragma once

#include "Level.hpp"

class Autopilot {
public:
    struct Command {
        molten::Vec2 push{0, 0};        // on the ground plane: x is east, y is north (toward -z). Length at most 1
        bool jump = false;
    };

    void reset() {
        step_ = 0;
        hasJumped_ = false;
    }

    /// What the autopilot wants to do during this physics step.
    Command steer(molten::Engine& engine, const Level& level);

private:
    int step_ = 0;
    bool hasJumped_ = false;
};
