// Game.hpp
// What a game provides: a handful of functions the engine calls at the right moments.
// Override the ones you need.
//
//     struct MyGame : molten::Game {
//         void start(molten::Engine& engine) override { /* build the scene */ }
//         void update(molten::Engine& engine, float dt) override { /* every frame */ }
//     };
//
//     int main(int argc, char** argv) {
//         molten::Engine engine(molten::EngineConfig::fromArguments(argc, argv));
//         MyGame game;
//         return engine.run(game);
//     }
#pragma once

namespace molten {

class Engine;
class Canvas;

class Game {
public:
    virtual ~Game() = default;

    /// Once, when everything is ready: load assets and create entities here.
    virtual void start(Engine& engine) {}

    /// At a fixed rate (60 times per second), just before each physics step. Apply forces
    /// and change velocities here. A frame may run this zero, one or several times, so do
    /// not look for key presses here: read input in update() and remember what you need.
    virtual void fixedUpdate(Engine& engine, float dt) {}

    /// Once per frame, before drawing. `dt` is the duration of the frame in seconds.
    virtual void update(Engine& engine, float dt) {}

    /// Once per frame, after update(): draw the interface, in points from the top left.
    virtual void draw(Engine& engine, Canvas& canvas) {}

    /// Once, when the game is closing.
    virtual void shutdown(Engine& engine) {}
};

}  // namespace molten
