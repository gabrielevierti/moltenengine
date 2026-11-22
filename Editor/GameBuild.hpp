// GameBuild.hpp
// Turning a project into something that runs: building its C++ code into a library the
// editor (and the exported game) loads, and putting together an application.
#pragma once

#include "Project/Project.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace molten::editor {

struct BuildResult {
    bool succeeded = false;
    bool hadSources = false;            // false: the project has no code, which is not an error
    std::string libraryPath;            // the library that was built
    std::string log;                    // what the compiler said
    double seconds = 0;
    bool reused = false;                // nothing had changed since the last build: that library, not compiled again
};

/// The .cpp files in the project's Source folder, in every subfolder.
std::vector<std::string> gameSourceFiles(const Project& project);

/// A number that changes whenever a file of the Source folder is added, removed or saved.
uint64_t gameSourceSignature(const Project& project);

/// Makes Source/<name>.cpp with a component called `name` in it, ready to be filled in.
/// The name must be usable in C++: letters, digits and _, not starting with a digit.
/// Returns the new file's path, or "" with the reason in `error`.
std::string createScriptFile(const Project& project, const std::string& name, std::string* error = nullptr);

/// Compiles the project's Source folder into Library/Game/Game-<number>.dylib (a new
/// file each time: a library that is loaded cannot be replaced on disk) and copies it
/// to Library/Game/Game.dylib, the one an exported game takes.
/// `hostExecutable` is the program the library will be loaded into: the editor.
///
/// When neither the code nor the editor itself has changed since the last build that
/// succeeded, nothing is compiled: the result names that library and says `reused`.
/// Opening a project then costs no compiler at all. `mayReuse` false compiles regardless.
/// It reads and writes files and runs the compiler, nothing else: safe on another thread.
BuildResult buildGameCode(const Project& project, const std::string& hostExecutable, bool mayReuse = true);

/// A copy of a built library under the next number, to load a second time what is loaded
/// already. Returns its path, or "" if it could not be copied.
std::string copyOfBuild(const std::string& libraryPath);

/// Puts together `destination`/<Name>.app from the player, the engine's shaders and
/// assets, the project and its built code. Returns "" or what went wrong.
std::string exportMacApplication(const Project& project, const std::string& destination, const std::string& playerExecutable,
                                 const std::string& engineRoot, std::string* applicationPath = nullptr, bool isDevelopment = false);

/// The path of the program that is running.
std::string currentExecutablePath();

}  // namespace molten::editor
