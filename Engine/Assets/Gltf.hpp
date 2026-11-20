// Gltf.hpp
// Reading glTF 2.0 models (.gltf with its .bin, or a single .glb): meshes, the tree of
// nodes they hang on, materials, skeletons ("skins") and the animations that move them.
//
// A skinned model is drawn by moving every vertex with the bones it is tied to: each
// vertex names up to four joints and how much it follows each. An animation is, for some
// nodes, where they are, how they are turned and how big they are at given times.
#pragma once

#include "Assets/MeshData.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace molten {

struct GltfPrimitive {
    MeshData mesh;
    std::vector<std::array<uint16_t, 4>> joints;    // per vertex, into its skin's list of joints; empty if not skinned
    std::vector<Vec4> weights;
    int material = -1;
};

struct GltfNode {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    Vec3 translation{0, 0, 0};
    Quat rotation{};
    Vec3 scale{1, 1, 1};
    int mesh = -1, skin = -1;
};

struct GltfSkin {
    std::vector<int> joints;                        // nodes
    std::vector<Mat4> inverseBind;                  // from the mesh as it was modeled to each joint's own space
};

struct GltfChannel {
    enum class Path { Translation, Rotation, Scale };
    int node = -1;
    Path path = Path::Translation;
    bool isStep = false;                            // holds each value until the next, instead of blending
    std::vector<float> times;
    std::vector<Vec4> values;                       // xyz, or a rotation's xyzw
};

struct GltfAnimation {
    std::string name;
    float duration = 0;
    std::vector<GltfChannel> channels;
};

struct GltfMaterial {
    std::string name;
    Vec4 baseColor{1, 1, 1, 1};
    float metallic = 1, roughness = 1;
    Vec3 emissive{0, 0, 0};
};

/// Where every node is: what an animation changes.
struct GltfPose {
    std::vector<Vec3> translations;
    std::vector<Quat> rotations;
    std::vector<Vec3> scales;
};

struct GltfModel {
    std::vector<GltfNode> nodes;
    std::vector<std::vector<GltfPrimitive>> meshes;
    std::vector<GltfSkin> skins;
    std::vector<GltfAnimation> animations;
    std::vector<GltfMaterial> materials;

    /// Reads a .gltf or .glb file. On failure `error`, if given, says why.
    static std::optional<GltfModel> load(const std::string& path, std::string* error = nullptr);
    /// From the JSON text; `binary` is the .glb's own data, `directory` where other files are looked for.
    static std::optional<GltfModel> parse(const std::string& json, const std::vector<uint8_t>& binary, const std::string& directory,
                                          std::string* error = nullptr);

    int findAnimation(const std::string& name) const;
    /// The nodes as the file has them, at rest.
    GltfPose restPose() const;
    /// The pose an animation gives at a time; nodes it does not move stay as in `pose`.
    void sample(int animation, float time, GltfPose& pose) const;
    /// Each node's place in the model, from the pose and the tree.
    std::vector<Mat4> globalMatrices(const GltfPose& pose) const;
    /// Every mesh of the model in its place, as one: skinned ones moved by their bones.
    /// With `pose` at rest this is the model as it was made.
    MeshData assembled(const GltfPose& pose) const;
    bool hasSkins() const { return !skins.empty(); }

    /// A model is made of parts: each mesh of each node is one, with one material. A game
    /// that wants them apart (a car's body, glass and wheels, each with its own material)
    /// names a part by its number after the file's name: "Models/car.glb#3".
    struct Part {
        int node = -1, primitive = 0;
        std::string name;               // the node's name
        int material = -1;
    };
    std::vector<Part> parts() const;
    /// One part in its place in the model, as it was made (not moved by bones).
    MeshData part(const Part& which) const;
};

/// True for a file name this reads.
bool isGltfFile(const std::string& path);
/// "Models/car.glb#3" is the file "Models/car.glb" and its part 3; a name without '#' is the whole model (part -1).
std::string gltfFileOf(const std::string& name, int* part = nullptr);

}  // namespace molten
