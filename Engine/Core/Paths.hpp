// Paths.hpp
// Where the engine finds its Shaders and Assets folders.
//
// While developing, both are read straight from the source tree, so a shader can be
// edited and reloaded without rebuilding anything. The location is, in order:
//   1. the directory in the MOLTEN_ROOT environment variable,
//   2. a folder next to the executable that contains "Shaders" (a packaged game),
//      or the Resources folder of the application the executable is in,
//   3. the source tree this binary was built from.
#pragma once

#include <string>

namespace molten {

/// The directory that contains Shaders/ and Assets/.
std::string findRootDirectory(const std::string& executablePath = {});

/// Where a game keeps what it saves for the player (settings, records): Library/Application
/// Support/<name> in the home folder, which on a phone is the application's own. Made if missing.
std::string userDataDirectory(const std::string& name);

}  // namespace molten
