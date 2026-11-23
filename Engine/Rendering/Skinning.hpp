// Skinning.hpp
// Posing a model with bones on the GPU. A skinned model used to be posed on the processor every
// frame: every vertex moved by its bones, its tangents worked out again, the whole mesh copied
// to the GPU. Now the model's vertices are sent once, as they were modeled, with the bones each
// one follows; every frame only the bones' matrices go to the GPU (a few dozen), and a compute
// kernel (skin_vertices, in Mesh.metal) moves the vertices straight into the mesh's own buffer,
// before anything is drawn. Shadows, depth, motion and ray tracing then see the posed mesh as
// they see any other.
//
// The matrices are a "palette": first one for each node of the model (a part that is not skinned
// follows its node, all of it), then those of each skin's joints. Every vertex names four
// entries of the palette and how much it follows each.
#pragma once

#include "Assets/MeshData.hpp"
#include "Math/Math.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace molten {

struct GltfModel;
struct GltfPose;
class GraphicsDevice;

struct SkinSource {
    /// The vertices as they were modeled, in the space of what moves them (MeshData::interleaved's
    /// twelve floats each), in the order GltfModel::assembled puts them.
    std::vector<float> rest;
    std::vector<std::array<uint16_t, 4>> joints;    // per vertex: four entries of the palette
    std::vector<Vec4> weights;                      // and how much it follows each
    uint32_t vertexCount = 0;
    size_t nodeCount = 0;                           // the palette's first entries are the nodes'
    std::vector<size_t> skinStarts;                 // then each skin's joints, from here
    size_t paletteSize = 0;
    /// Per entry of the palette, the box around the vertices it moves, in their own space: posed,
    /// the boxes moved by their matrices enclose the posed mesh (a little generously).
    std::vector<AABB> reach;

    /// What the GPU reads: made by upload(). Kept as opaque pointers so that this header does not
    /// need Metal (the tests build a source without a GPU).
    std::shared_ptr<void> restBuffer, jointBuffer, weightBuffer;

    /// From a model: empty (vertexCount 0) if it has no mesh, or too many joints for 16 bits.
    static std::shared_ptr<SkinSource> build(const GltfModel& model);
    /// Sends the vertices and their bones to the GPU. False if a buffer could not be made.
    bool upload(GraphicsDevice& device);

    /// The palette for a pose: the nodes' places, then each joint's matrix (its place times its
    /// inverse bind matrix), as GltfModel::assembled computes them.
    void palette(const GltfModel& model, const GltfPose& pose, std::vector<Mat4>& out) const;
    /// The box the posed mesh is in.
    AABB bounds(const std::vector<Mat4>& palette) const;
    /// What the kernel does, on the processor: for the tests, and to check the kernel against.
    void poseOnCpu(const std::vector<Mat4>& palette, std::vector<float>& posed) const;
};

}  // namespace molten
