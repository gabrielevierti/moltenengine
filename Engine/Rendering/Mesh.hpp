// Mesh.hpp
// A mesh on the GPU: a vertex buffer, an index buffer, and the box that encloses it.
#pragma once

#include "Assets/MeshData.hpp"
#include "Rendering/GraphicsDevice.hpp"

#include <memory>
#include <string>

namespace molten {

class Mesh {
public:
    Mesh(GraphicsDevice& device, const MeshData& data);

    MTL::Buffer* vertexBuffer() const { return vertexBuffer_.get(); }
    MTL::Buffer* indexBuffer() const { return indexBuffer_.get(); }
    uint32_t indexCount() const { return indexCount_; }
    /// The mesh's triangles arranged for ray tracing (a "bottom-level acceleration
    /// structure"), or nullptr on a GPU that cannot trace rays. Built once, with the mesh.
    MTL::AccelerationStructure* accelerationStructure() const { return accelerationStructure_.get(); }
    /// In the mesh's own space. The renderer transforms it to cull against the view.
    const AABB& bounds() const { return bounds_; }

    /// Where the mesh came from: a file name in the Assets folder, or "cube", "sphere",
    /// "plane", "cylinder". Empty for meshes made in code. Scene files store this name.
    std::string name;

private:
    void buildAccelerationStructure(GraphicsDevice& device);

    NS::SharedPtr<MTL::Buffer> vertexBuffer_, indexBuffer_;
    NS::SharedPtr<MTL::AccelerationStructure> accelerationStructure_;
    uint32_t indexCount_ = 0;
    AABB bounds_;
};

}  // namespace molten
