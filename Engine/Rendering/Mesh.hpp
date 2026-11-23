// Mesh.hpp
// A mesh on the GPU: a vertex buffer, an index buffer, and the box that encloses it.
#pragma once

#include "Assets/MeshData.hpp"
#include "Rendering/GraphicsDevice.hpp"

#include <memory>
#include <string>

#include <optional>
#include <vector>

namespace molten {

struct SkinSource;

class Mesh {
public:
    /// `moves`: its vertices will change (an animated model, cloth, a terrain being shaped), so its
    /// ray-tracing structure is made to be refitted to them (refit()) instead of built once for good.
    Mesh(GraphicsDevice& device, const MeshData& data, bool moves = false);

    MTL::Buffer* vertexBuffer() const { return vertexBuffer_.get(); }
    MTL::Buffer* indexBuffer() const { return indexBuffer_.get(); }
    uint32_t indexCount() const { return indexCount_; }
    /// The mesh's triangles arranged for ray tracing (a "bottom-level acceleration
    /// structure"), or nullptr on a GPU that cannot trace rays. Built once, with the mesh.
    MTL::AccelerationStructure* accelerationStructure() const { return accelerationStructure_.get(); }
    /// In the mesh's own space. The renderer transforms it to cull against the view.
    const AABB& bounds() const { return bounds_; }
    /// Moves the vertices of a mesh to those of `data`, which must have as many and the
    /// same triangles: what an animated model does every frame. False if it does not fit.
    bool update(const MeshData& data);
    bool isDynamic = false;             // made for one entity and changed as it moves
    /// Its ray-tracing structure made to fit the vertices as they are now (they moved this frame):
    /// encoded by the renderer before the frame's rays are traced. Nothing for a mesh made not to move.
    void refit(MTL::AccelerationStructureCommandEncoder* encoder);
    /// For a mesh whose vertices the GPU moves (skinning): the box they are in now.
    void setBounds(const AABB& bounds) { bounds_ = bounds; }
    /// Posed on the GPU from this (Skinning.hpp): the model's vertices and bones, shared by every
    /// entity showing the same model. Null for any other mesh.
    std::shared_ptr<const SkinSource> skin;
    /// Takes the shape of `fresher` (the same file, read again): everything that holds this
    /// mesh draws the new one. Its name stays. The GPU must not be using the old one.
    void adopt(Mesh&& fresher) {
        vertexBuffer_ = std::move(fresher.vertexBuffer_);
        indexBuffer_ = std::move(fresher.indexBuffer_);
        accelerationStructure_ = std::move(fresher.accelerationStructure_);
        structureDescriptor_ = std::move(fresher.structureDescriptor_);
        refitScratch_ = std::move(fresher.refitScratch_);
        indexCount_ = fresher.indexCount_;
        bounds_ = fresher.bounds_;
        positions_ = std::move(fresher.positions_);
        indices_ = std::move(fresher.indices_);
    }
    /// Where a ray in the mesh's own space first meets one of its triangles, as a multiple
    /// of the ray's direction. For tools that need to know what is under the pointer.
    std::optional<float> raycast(const Ray& ray) const;

    /// Its corners and which three make each triangle, as they were given: kept for tools.
    const std::vector<Vec3>& positions() const { return positions_; }
    const std::vector<uint32_t>& indices() const { return indices_; }

    /// Where the mesh came from: a file name in the Assets folder, or "cube", "sphere",
    /// "plane", "cylinder". Empty for meshes made in code. Scene files store this name.
    std::string name;

private:
    void buildAccelerationStructure(GraphicsDevice& device, bool moves);

    NS::SharedPtr<MTL::Buffer> vertexBuffer_, indexBuffer_;
    NS::SharedPtr<MTL::AccelerationStructure> accelerationStructure_;
    // Kept for refit(), when it moves: what the structure was built from, and room to refit it in.
    NS::SharedPtr<MTL::PrimitiveAccelerationStructureDescriptor> structureDescriptor_;
    NS::SharedPtr<MTL::Buffer> refitScratch_;
    uint32_t indexCount_ = 0;
    AABB bounds_;
    std::vector<Vec3> positions_;       // kept for raycast
    std::vector<uint32_t> indices_;
};

}  // namespace molten
