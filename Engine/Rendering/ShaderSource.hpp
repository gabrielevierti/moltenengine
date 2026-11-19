// ShaderSource.hpp
// Reads the .metal files of a directory and joins them into one piece of source text
// that Metal can compile at run time.
//
// Metal's run-time compiler takes a string, not a folder, so it cannot follow
// #include "..." on its own. This resolves those includes (each file is included at
// most once) and adds #line directives, so that a compiler error still names the file
// and line where the mistake really is.
#pragma once

#include <string>
#include <vector>

namespace molten {

struct ShaderSource {
    std::string text;                   // everything, ready for the compiler
    std::vector<std::string> files;     // each file that was read, for change detection
    std::string error;                  // empty unless a file or an include was missing

    static ShaderSource load(const std::string& directory);
};

}  // namespace molten
