// Material.hpp
// How a surface looks: the inputs of the physically based shader.
// The layout follows glTF's "metallic-roughness" model, the one most tools export.
#pragma once

#include "Rendering/Mesh.hpp"
#include "Rendering/Texture.hpp"

#include <memory>

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
