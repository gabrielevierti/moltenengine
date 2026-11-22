// Gltf.hpp
// Reading models: glTF 2.0 (.gltf with its .bin, or a single .glb), and as the same thing FBX
// (.fbx), Collada (.dae), Wavefront OBJ with its materials (.obj, .mtl), STL and PLY: meshes,
// the tree of nodes they hang on, materials and their pictures, skeletons ("skins") and the
// animations that move them (glTF and FBX). The other formats are read in ModelFormats.cpp.
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

/// A picture a material uses: a file beside the model, or one carried inside it.
struct GltfPicture {
    std::string file;                   // as the model names it ("Textures/paint.jpg", relative to the model's file; or a path of the machine it was made on)
    int image = -1;                     // or carried in the model file: GltfModel::images
    bool has() const { return !file.empty() || image >= 0; }
};

/// A picture carried inside a model file (a .glb's, an FBX's): its bytes as they are, PNG or JPEG.
struct GltfImage {
    std::string name;
    std::string extension;              // ".png", ".jpg"
    std::vector<uint8_t> bytes;
};

struct GltfMaterial {
    std::string name;
    Vec4 baseColor{1, 1, 1, 1};
    float metallic = 1, roughness = 1;
    Vec3 emissive{0, 0, 0};
    bool isBlended = false;             // seen through (glass): its color's alpha is how much it covers
    bool isMasked = false;              // cut out where its alpha is under alphaCutoff (leaves)
    float alphaCutoff = 0.5f;
    bool isDoubleSided = false;
    /// The picture of its color, where the file names one beside it ("Textures/paint.jpg",
    /// relative to the model's file); empty when there is none, or it is inside the file.
    std::string baseColorTexture;
    /// Its pictures: color (sRGB), normal map, metal in blue and roughness in green, light it gives off, occlusion in red.
    GltfPicture colorMap, normalMap, metalRoughMap, emissiveMap, occlusionMap;
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
    std::vector<GltfImage> images;          // pictures carried inside the file

    /// Reads a model file of any kind isModelFile() names. On failure `error`, if given, says why.
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

/// True for a model file of any kind this reads: .gltf .glb .fbx .dae .obj .stl .ply.
bool isModelFile(const std::string& path);
/// True for a name that is read as a model of parts (GltfModel): every kind isModelFile() names,
/// but an OBJ only for one of its parts ("Models/boat.obj#2"): an OBJ as a whole is read as one
/// mesh (MeshData::parseOBJ), as it always was.
bool isGltfFile(const std::string& path);
/// "Models/car.glb#3" is the file "Models/car.glb" and its part 3; a name without '#' is the whole model (part -1).
std::string gltfFileOf(const std::string& name, int* part = nullptr);

}  // namespace molten
