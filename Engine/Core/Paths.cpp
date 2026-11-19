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
        fs::path beside = fs::absolute(fs::path(executablePath), code).parent_path();
        if (!code && fs::exists(beside / "Shaders")) return beside.string();
        // Inside a Mac application: MyGame.app/Contents/MacOS/MyGame, with everything in Contents/Resources.
        fs::path resources = beside.parent_path() / "Resources";
        if (!code && fs::exists(resources / "Shaders")) return resources.string();
    }
    return MOLTEN_SOURCE_ROOT;
}

}  // namespace molten
