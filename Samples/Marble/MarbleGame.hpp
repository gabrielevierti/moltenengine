// MarbleGame.hpp
// Marble: roll through the course, collect every gem, reach the goal.
//
// The whole game is this one class. Read it as the answer to "how do I use the engine?":
// start() builds the scene, fixedUpdate() pushes the marble, update() does everything
// that follows the frame rate, draw() paints the interface.
#pragma once

#include "Autopilot.hpp"
#include "Level.hpp"
#include "Sounds.hpp"

class MarbleGame : public molten::Game {
public:
    void start(molten::Engine& engine) override;
    void fixedUpdate(molten::Engine& engine, float dt) override;
    void update(molten::Engine& engine, float dt) override;
    void draw(molten::Engine& engine, molten::Canvas& canvas) override;

    /// Shows the whole course from above instead of following the marble. On by default
    /// when there is no window, so that a captured image shows the entire level.
    bool showsOverview = false;

    /// Lets the autopilot play (`--demo`).
    bool isDemo = false;
    bool hasWon() const { return hasWon_; }
    int collectedCount() const { return collected_; }

private:
    void restart(molten::Engine& engine);
    void collect(molten::Engine& engine, molten::Entity gem);
    void respawn(molten::Engine& engine);
    void updateCamera(molten::Engine& engine, float dt);
    void spawnBurst(molten::Engine& engine, molten::Vec3 position, molten::Vec3 color);

    Level level_;
    Sounds sounds_;
    Autopilot autopilot_;
    molten::SoundHandle rollSound_ = 0;

    int collected_ = 0;
    bool hasWon_ = false;
    bool hasMoved_ = false;             // the clock starts with the first input
    float playTime_ = 0;
    float collectPulse_ = 0;            // makes the counter jump when a gem is collected
    float jumpBuffer_ = 0;              // a jump pressed slightly too early still counts
    molten::Vec2 wish_{0, 0};               // the stick, remembered for fixedUpdate
    molten::Vec3 lastSafePosition_{0, 1, 0};

    float cameraYaw_ = 0;               // radians around the marble; 0 looks toward -z
    float cameraPitch_ = 0.45f;
    float cameraDistance_ = 9.0f;
    molten::Vec3 cameraTarget_{0, 0, 0};
};
