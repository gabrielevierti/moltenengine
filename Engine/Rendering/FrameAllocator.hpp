// FrameAllocator.hpp
// Memory for everything that changes every frame: uniforms, instance data, 2D vertices.
//
// It is one large Metal buffer per frame in flight, filled from the start each frame
// (a "bump allocator"): allocating is adding to an offset, and freeing is setting the
// offset back to zero. The renderer keeps three of these and cycles through them,
// because the GPU may still be reading the buffers of the previous two frames.
#pragma once

#include <Metal/Metal.hpp>

#include <cstring>
#include <vector>

namespace molten {

class FrameAllocator {
public:
    struct Allocation {
        MTL::Buffer* buffer = nullptr;
        size_t offset = 0;
        void* pointer = nullptr;
    };

    void initialize(MTL::Device* device, size_t capacity) {
        device_ = device;
        grow(capacity);
    }

    /// Starts a new frame. Whatever was allocated before may be overwritten.
    void reset() {
        offset_ = 0;
        retired_.clear();           // the frames that used the old buffers have finished by now
    }

    /// `alignment` 256 satisfies every GPU's rule for the offset of a bound buffer.
    Allocation allocate(size_t size, size_t alignment = 256) {
        size_t start = (offset_ + alignment - 1) / alignment * alignment;
        if (start + size > capacity_) {
            // Out of room: switch to a larger buffer. Earlier allocations of this frame
            // stay valid, because the old buffer is kept alive until the next reset.
            retired_.push_back(buffer_);
            grow((capacity_ + size) * 2);
            start = 0;
        }
        offset_ = start + size;
        return Allocation{buffer_.get(), start, static_cast<uint8_t*>(buffer_->contents()) + start};
    }

    template <class T> Allocation copy(const T& value) {
        Allocation allocation = allocate(sizeof(T));
        std::memcpy(allocation.pointer, &value, sizeof(T));
        return allocation;
    }

    template <class T> Allocation copyArray(const T* values, size_t count) {
        Allocation allocation = allocate(sizeof(T) * (count > 0 ? count : 1));
        if (count > 0) std::memcpy(allocation.pointer, values, sizeof(T) * count);
        return allocation;
    }

private:
    void grow(size_t capacity) {
        capacity_ = capacity;
        offset_ = 0;
        buffer_ = NS::TransferPtr(device_->newBuffer(NS::UInteger(capacity), MTL::ResourceStorageModeShared));
    }

    MTL::Device* device_ = nullptr;
    NS::SharedPtr<MTL::Buffer> buffer_;
    std::vector<NS::SharedPtr<MTL::Buffer>> retired_;
    size_t capacity_ = 0;
    size_t offset_ = 0;
};

}  // namespace molten
