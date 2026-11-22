// SurfaceShader.hpp
// Shaders written by hand, as assets: a `.shader` file says what a surface is at each
// pixel (its color, how rough, how much metal, what light it gives off, which way it
// faces), and the engine lights it like any other. A material chooses one beside Lit and
// Unlit, and sets the properties the file declares. Water of a game's own (a pond, a pool, a
// stylized lagoon) is one too: newWaterShaderText() is one that works, to start from.
//
//     properties {
//         float speed = 1 range 0 10
//         color hot = 1 0.35 0.05 1
//         vector flow = 1 0 0 0
//     }
//
//     Surface surface(Surface s, Input in, Props props) {
//         float wave = sin(in.position.x * 4.0 + in.time * props.speed);
//         s.emissive = props.hot.rgb * (0.5 + 0.5 * wave);
//         return s;
//     }
//
// `s` arrives as the material's own settings and textures made it; what the function
// returns is what is lit. The language is Metal's (C++ with float3, sin, mix and the
// rest). Helper functions may be written above `surface`.
//
//     Surface:  color (float3)  alpha  metallic  roughness  occlusion  emissive (float3)  normal (float3, in the world)
//     Input:    uv (float2)  position (float3, in the world)  normal (float3, the mesh's own)
//               view (float3, toward the eye)  time (seconds)  tint (float4, the entity's)
//               wind (float3, meters a second: the scene's, with its weather)
//               behind (meters from this surface to what is behind it along the line of sight:
//               how deep water is there; large where nothing is; for a material seen through)
//
// And the engine's own helpers: noise(float2) is smooth noise between 0 and 1.
//
// This file is the part without a graphics device: reading a `.shader`, and writing the
// Metal code that the shader library compiles with its own.
#pragma once

#include "Math/Math.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace molten {

struct SurfaceProperty {
    enum class Kind { Float, Color, Vector } kind = Kind::Float;
    std::string name;
    Vec4 value{0, 0, 0, 0};             // what a material has until it says otherwise
    float min = 0, max = 1;             // for a float with a range
    bool hasRange = false;
};

struct SurfaceShader {
    static constexpr int kMaxProperties = 8;

    std::string name;                   // the file's name in Assets: "Shaders/Lava.shader"
    std::vector<SurfaceProperty> properties;
    std::string code;                   // the file without its properties block, line for line
    std::string error;                  // what is wrong with the properties block, if anything

    /// Reads the text of a `.shader` file.
    static SurfaceShader parse(const std::string& name, const std::string& text);

    const SurfaceProperty* property(const std::string& propertyName) const;
};

/// The Metal code for a list of shaders: each in a namespace of its own, and one function,
/// `moltenCustomSurface`, that runs the one a material names by its place in the list
/// (from 1). Empty for an empty list.
std::string surfaceShaderCode(const std::vector<SurfaceShader>& shaders);

/// The surface shaders a game uses, in the order they were first asked for: a material's
/// shader is known to the GPU by its place here.
class SurfaceShaderSet {
public:
    /// Where the file of a shader is, from its name in Assets. Without it, names are paths.
    std::function<std::string(const std::string& name)> pathOf;

    /// The place of a shader, from 1: read from its file the first time it is asked for.
    /// A file that cannot be read still has a place, and does nothing.
    int use(const std::string& name);
    const SurfaceShader* shader(int place) const { return place >= 1 && size_t(place) <= shaders_.size() ? &shaders_[size_t(place - 1)] : nullptr; }
    const SurfaceShader* find(const std::string& name) const;
    const std::vector<SurfaceShader>& all() const { return shaders_; }

    /// Reads again the files that changed on disk. True if code() is not what it was the
    /// last time this was asked (a new shader, or a changed one).
    bool takeChanges();
    std::string code() const { return surfaceShaderCode(shaders_); }

private:
    void read(size_t index);

    std::vector<SurfaceShader> shaders_;
    std::vector<std::filesystem::file_time_type> times_;
    bool isChanged_ = false;
};

/// What a new `.shader` file starts as.
std::string newSurfaceShaderText();
/// And a new water shader: water of a game's own (a pond, a pool, a cartoon lagoon), seen
/// through, colored by how deep it is, rippling, with foam where it meets the shore. To change.
std::string newWaterShaderText();

}  // namespace molten
