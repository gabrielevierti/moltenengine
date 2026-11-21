// ShaderJoin: the engine's .metal files joined into one source, as the engine joins them at run
// time (includes resolved, #line kept), for Tools/compile_shaders.sh to compile ahead of time.
// usage: molten-shader-join <Shaders folder> <joined .metal to write>
#include "Rendering/ShaderSource.hpp"

#include <cstdio>
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: molten-shader-join <Shaders folder> <output .metal>\n");
        return 2;
    }
    molten::ShaderSource source = molten::ShaderSource::load(argv[1]);
    if (!source.error.empty()) {
        std::fprintf(stderr, "shaders: %s\n", source.error.c_str());
        return 1;
    }
    std::ofstream out(argv[2], std::ios::binary);
    out << source.text;
    return out ? 0 : 1;
}
