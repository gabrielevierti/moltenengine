#include "Rendering/Mesh.hpp"

namespace molten {

Mesh::Mesh(GraphicsDevice& device, const MeshData& data) {
    std::vector<float> vertices = data.interleaved();
    vertexBuffer_ = NS::TransferPtr(device.device()->newBuffer(vertices.data(), vertices.size() * sizeof(float),
                                                               MTL::ResourceStorageModeShared));
    indexBuffer_ = NS::TransferPtr(device.device()->newBuffer(data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                                              MTL::ResourceStorageModeShared));
    indexCount_ = uint32_t(data.indices.size());
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

}  // namespace molten
