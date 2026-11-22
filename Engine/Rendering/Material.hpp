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
    /// A surface of water: the waves the scene's wind raises (Environment > Weather > Wind), the sky mirrored
    /// at a glancing angle, the bottom seen through it from above. Its base color is the color
    /// water this deep ends up; its opacity how murky it is (0.3 a clear sea that shows its
    /// bottom ten meters down, 1 a pond). On a flat mesh (a plane) the waves rise and fall, and
    /// things float on them (Water/Waves.hpp).
    bool isWater = false;
    float waveSize = 1.0f;              // water: its waves' lengths, times what the wind makes them
    float waveHeight = 1.0f;            // water: their height, times what the wind raises; 0 a mirror
    float foam = 1.0f;                  // water: how much white shows where waves break and at the shore; 0 none
    bool isSea = false;                 // the scene's sea, which the renderer makes itself (Environment > Water); never in a file
    /// Read from a file of before, when water was a material: the scene's sea takes the place of
    /// what it was on (Water/Water.hpp, retireOldWater). It is lit as any other now.
    bool wasWater = false;
    /// Moved by the scene's wind (Environment > Weather > Wind), the more the higher above the model's base:
    /// grass, reeds, leaves, a flag. 0 still; 1 grass; 0.1 a tree.
    float sway = 0.0f;

    /// True for what the things behind it are seen through: drawn after everything opaque, from
    /// far to near, and casting no shadow. Water always is, whatever its blending says.
    bool isSeenThrough() const { return alphaMode == AlphaMode::Blend || isWater; }

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
    bool isBeyondItsDetail = false;     // set each view by a LOD Group above it: not drawn in this one
    /// The mesh's name where the mesh itself is not there: its file is missing, or the program
    /// has no GPU to hold it (a test, a tool that writes scenes). Written to a scene file as it
    /// was read, so that saving a scene does not forget what it could not load.
    std::string meshName;
};

/// Many copies of an entity's mesh, drawn together in few draw calls: grass, trees, rocks,
/// fence posts. Beside a MeshRenderer: its mesh and material are drawn at each of `places`
/// (in the entity's space) instead of once at the entity. Copies farther than `drawDistance`
/// meters from the camera are not drawn (0: all of them, however far).
/// A decal: a picture projected down onto whatever is under it, inside the entity's box (its
/// scale is its size: x by z across, y how deep it reaches). Dirt, cracks, tire marks, paint, a
/// puddle's darkness, a sign on a wall (turn the entity so that its y points into the wall).
struct Decal {
    std::string picture;                // a picture in Assets, with transparency where nothing is drawn
    Vec3 color{1, 1, 1};
    float opacity = 1.0f;
    bool darkens = true;                // multiplies what is under it, as dirt does; off: painted over it, as paint is

    std::shared_ptr<Texture> loaded;    // the picture, once read
    std::string loadedName;
};

struct MeshInstances {
    std::vector<Mat4> places;
    float drawDistance = 0.0f;
};

/// LOD Group, as in Unity: a model drawn in full near the camera and simpler far from it.
/// Under the entity it is on, the child called `far` (and what is under that) is drawn only
/// beyond `distance` meters from the camera, and everything else only within it. What is
/// not drawn still collides, moves and plays as it did: only what is drawn changes.
struct LodGroup {
    float distance = 30.0f;
    std::string far = "Far";
};

}  // namespace molten
