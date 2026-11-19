// Application.hpp
// One way to start a program on every platform.
//
// On a Mac a program may simply make an Engine and call run(). On iOS the system owns
// the loop: the program hands over control at once and is called back for every
// frame. runApplication() hides the difference:
//
//     int main(int argc, char** argv) {
//         return molten::runApplication(argc, argv, molten::EngineConfig(), [](molten::Engine&) {
//             return std::make_unique<MyGame>();
//         });
//     }
#pragma once

#include "Core/Engine.hpp"

#include <functional>
#include <memory>

namespace molten {

/// Makes the game once the engine exists.
using GameFactory = std::function<std::unique_ptr<Game>(Engine&)>;

/// Creates the engine with `defaults` (and the command line, where there is one), makes
/// the game, and runs it to the end. Returns the exit code for main().
int runApplication(int argc, char** argv, EngineConfig defaults, GameFactory makeGame);

}  // namespace molten
