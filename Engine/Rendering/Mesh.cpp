#include "Rendering/Mesh.hpp"
#include <cmath>
#include <cstring>

namespace molten {

Mesh::Mesh(GraphicsDevice& device, const MeshData& data) {
    std::vector<float> vertices = data.interleaved();
    vertexBuffer_ = NS::TransferPtr(device.device()->newBuffer(vertices.data(), vertices.size() * sizeof(float),
                                                               MTL::ResourceStorageModeShared));
    indexBuffer_ = NS::TransferPtr(device.device()->newBuffer(data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                                              MTL::ResourceStorageModeShared));
    indexCount_ = uint32_t(data.indices.size());
    positions_ = data.positions;
    indices_ = data.indices;
    bounds_ = data.bounds();
    if (device.supportsRayTracing() && indexCount_ >= 3) buildAccelerationStructure(device);
}

/// Tracing a ray against every triangle would be hopeless; the GPU instead sorts the
/// triangles into a tree of boxes once, and a ray then only visits the boxes it crosses.
/// The structure reads the positions straight from the vertex buffer.
void Mesh::buildAccelerationStructure(GraphicsDevice& device) {
    auto geometry = NS::TransferPtr(MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init());
    geometry->setVertexBuffer(vertexBuffer_.get());
    geometry->setVertexFormat(MTL::AttributeFormatFloat3);                     // the position starts each vertex
    geometry->setVertexStride(MeshData::kFloatsPerVertex * sizeof(float));
    geometry->setIndexBuffer(indexBuffer_.get());
    geometry->setIndexType(MTL::IndexTypeUInt32);
    geometry->setTriangleCount(indexCount_ / 3);
    geometry->setOpaque(true);

    auto descriptor = NS::TransferPtr(MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init());
    descriptor->setGeometryDescriptors(NS::Array::array(geometry.get()));

    MTL::AccelerationStructureSizes sizes = device.device()->accelerationStructureSizes(descriptor.get());
    accelerationStructure_ = NS::TransferPtr(device.device()->newAccelerationStructure(sizes.accelerationStructureSize));
    auto scratch = NS::TransferPtr(device.device()->newBuffer(sizes.buildScratchBufferSize, MTL::ResourceStorageModePrivate));
    if (!accelerationStructure_ || !scratch) {
        accelerationStructure_.reset();
        return;
    }
    // Built on the engine's own command queue, so it is ready before any frame that
    // was submitted after it.
    MTL::CommandBuffer* commandBuffer = device.queue()->commandBuffer();
    MTL::AccelerationStructureCommandEncoder* encoder = commandBuffer->accelerationStructureCommandEncoder();
    encoder->buildAccelerationStructure(accelerationStructure_.get(), descriptor.get(), scratch.get(), 0);
    encoder->endEncoding();
    commandBuffer->commit();
}

bool Mesh::update(const MeshData& data) {
    if (data.positions.size() != positions_.size() || data.indices.size() != indices_.size() || !vertexBuffer_) return false;
    std::vector<float> vertices = data.interleaved();
    std::memcpy(vertexBuffer_->contents(), vertices.data(), vertices.size() * sizeof(float));
    positions_ = data.positions;
    bounds_ = data.bounds();
    return true;
}

std::optional<float> Mesh::raycast(const Ray& ray) const {
    if (!intersectAABB(ray, bounds_)) return std::nullopt;
    std::optional<float> nearest;
    for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
        // Moller and Trumbore's test of a ray against a triangle, either side of it.
        Vec3 a = positions_[indices_[i]], edge1 = positions_[indices_[i + 1]] - a, edge2 = positions_[indices_[i + 2]] - a;
        Vec3 p = simd::cross(ray.direction, edge2);
        float det = simd::dot(edge1, p);
        if (std::abs(det) < 1e-12f) continue;
        float inverse = 1.0f / det;
        Vec3 fromA = ray.origin - a;
        float u = simd::dot(fromA, p) * inverse;
        if (u < 0 || u > 1) continue;
        Vec3 q = simd::cross(fromA, edge1);
        float v = simd::dot(ray.direction, q) * inverse;
        if (v < 0 || u + v > 1) continue;
        float t = simd::dot(edge2, q) * inverse;
        if (t > 1e-5f && (!nearest || t < *nearest)) nearest = t;
    }
    return nearest;
}

}  // namespace molten
