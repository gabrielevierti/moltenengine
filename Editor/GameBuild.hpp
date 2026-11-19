// GameBuild.hpp
// Turning a project into something that runs: building its C++ code into a library the
// editor (and the exported game) loads, and putting together an application.
#pragma once

#include "Project/Project.hpp"

#include <string>
#include <vector>

namespace molten::editor {

struct BuildResult {
    bool succeeded = false;
    bool hadSources = false;            // false: the project has no code, which is not an error
    std::string libraryPath;            // the library that was built
    std::string log;                    // what the compiler said
    double seconds = 0;
};

/// The .cpp files in the project's Source folder, in every subfolder.
std::vector<std::string> gameSourceFiles(const Project& project);

/// Compiles the project's Source folder into Library/Game/Game-<number>.dylib (a new
/// file each time: a library that is loaded cannot be replaced on disk) and copies it
/// to Library/Game/Game.dylib, the one an exported game takes.
/// `hostExecutable` is the program the library will be loaded into: the editor.
BuildResult buildGameCode(const Project& project, const std::string& hostExecutable);

/// Puts together `destination`/<Name>.app from the player, the engine's shaders and
/// assets, the project and its built code. Returns "" or what went wrong.
std::string exportMacApplication(const Project& project, const std::string& destination, const std::string& playerExecutable,
                                 const std::string& engineRoot, std::string* applicationPath = nullptr);

/// The path of the program that is running.
std::string currentExecutablePath();

}  // namespace molten::editor
