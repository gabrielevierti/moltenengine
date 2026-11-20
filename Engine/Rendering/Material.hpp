// Material.hpp
// How a surface looks: the inputs of the physically based shader.
// The layout follows glTF's "metallic-roughness" model, the one most tools export.
#pragma once

#include "Rendering/Mesh.hpp"
#include "Rendering/Texture.hpp"

#include <memory>
#include <string>
#include <vector>

namespace molten {

enum class AlphaMode : uint8_t {
    Opaque,     // alpha is ignored
    Mask,       // pixels with alpha below alphaCutoff are cut out
    Blend,      // drawn after everything opaque, sorted back to front, blended
};

/// Each factor multiplies the matching texture. A missing texture counts as white
/// (or as a flat surface, for the normal map), so factors alone describe a plain material.
struct Material {
    Vec4 baseColor{1, 1, 1, 1};         // linear RGB and opacity
    float metallic = 0.0f;              // 0 plastic, wood, stone; 1 metal
    float roughness = 0.6f;             // 0 mirror; 1 chalk
    Vec3 emissive{0, 0, 0};             // light the surface gives off; above 1 it blooms
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    Vec2 uvScale{1, 1};

    AlphaMode alphaMode = AlphaMode::Opaque;
    bool isDoubleSided = false;         // do not cull back faces
    bool isUnlit = false;               // base color and emissive only, no lighting
    bool isWater = false;               // a surface of water: moving waves, a mirror at a glancing angle, clear from above
    float waveScale = 1.0f;             // water: larger numbers make smaller waves
    float waveHeight = 1.0f;            // water: how steep they are

    /// A shader written by hand: the name of a `.shader` file in Assets (see
    /// Rendering/SurfaceShader.hpp), or empty for the engine's own. It is given the surface
    /// as everything above made it, and returns what is lit.
    std::string shader;
    /// What the material says for that shader's properties, by name. A property it does
    /// not mention has the value the shader file gives it.
    struct ShaderValue {
        std::string name;
        Vec4 value{0, 0, 0, 0};
    };
    std::vector<ShaderValue> shaderValues;
    const Vec4* shaderValue(const std::string& name) const {
        for (const ShaderValue& item : shaderValues) {
            if (item.name == name) return &item.value;
        }
        return nullptr;
    }
    void setShaderValue(const std::string& name, Vec4 value) {
        for (ShaderValue& item : shaderValues) {
            if (item.name == name) {
                item.value = value;
                return;
            }
        }
        shaderValues.push_back(ShaderValue{name, value});
    }

    /// The material's file in Assets ("Materials/Brick.material") when it is an asset that
    /// scenes share; empty for one that belongs to a single scene or was made in code.
    std::string assetName;

    std::shared_ptr<Texture> baseColorTexture;          // sRGB
    std::shared_ptr<Texture> normalTexture;             // tangent space, green up
    std::shared_ptr<Texture> metallicRoughnessTexture;  // roughness in green, metalness in blue
    std::shared_ptr<Texture> occlusionTexture;          // red
    std::shared_ptr<Texture> emissiveTexture;           // sRGB
};

/// Component: draws a mesh with a material at the entity's Transform.
struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    Vec4 tint{1, 1, 1, 1};              // multiplies the material's base color for this entity only
    bool castsShadows = true;
    bool isVisible = true;
};

}  // namespace molten
