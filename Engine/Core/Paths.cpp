#include "Core/Paths.hpp"

#include <cstdlib>
#include <filesystem>

#ifndef MOLTEN_SOURCE_ROOT
#define MOLTEN_SOURCE_ROOT "."
#endif

namespace molten {

namespace fs = std::filesystem;

std::string findRootDirectory(const std::string& executablePath) {
    if (const char* fromEnvironment = std::getenv("MOLTEN_ROOT")) {
        return fromEnvironment;
    }
    if (!executablePath.empty()) {
        std::error_code code;
        // Through any link: the editor's application in the build folder links to the program.
        fs::path program = fs::weakly_canonical(fs::absolute(fs::path(executablePath), code), code);
        if (code) program = fs::absolute(fs::path(executablePath));
        fs::path beside = program.parent_path();
        // Both folders, and spelled exactly: the Mac does not tell "shaders" from "Shaders", and the
        // build folder has a "shaders" of its own (the shaders' compiling) that is not the engine's.
        auto isRoot = [](const fs::path& folder) {
            std::error_code ignored;
            if (!fs::is_directory(folder / "Shaders", ignored) || !fs::is_directory(folder / "Assets", ignored)) return false;
            for (const auto& entry : fs::directory_iterator(folder, ignored)) {
                if (entry.path().filename() == "Shaders") return true;
            }
            return false;
        };
        if (isRoot(beside)) return beside.string();
        // Inside a Mac application: MyGame.app/Contents/MacOS/MyGame, with everything in Contents/Resources.
        fs::path resources = beside.parent_path() / "Resources";
        if (isRoot(resources)) return resources.string();
    }
    return MOLTEN_SOURCE_ROOT;
}

std::string userDataDirectory(const std::string& name) {
    const char* home = std::getenv("HOME");
    std::string safe;
    for (char c : name) safe += (c == '/' || c == ':') ? '-' : c;
    std::filesystem::path folder = std::filesystem::path(home && *home ? home : "/tmp") / "Library" / "Application Support" / (safe.empty() ? "Molten Game" : safe);
    std::error_code code;
    std::filesystem::create_directories(folder, code);
    return folder.string();
}

}  // namespace molten
