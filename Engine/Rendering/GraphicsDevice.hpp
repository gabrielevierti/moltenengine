// GraphicsDevice.hpp
// The Metal device, its command queue, and the small state objects everything shares:
// samplers and depth-stencil states. They are created once, here, because creating
// state objects is not something to do while drawing.
//
// Metal objects are held in NS::SharedPtr, metal-cpp's reference-counting smart
// pointer, so nothing in the engine calls retain() or release() by hand.
#pragma once

#include <Metal/Metal.hpp>

namespace molten {

/// Many Objective-C methods return "autoreleased" objects: nobody owns them, and they
/// are freed when the innermost autorelease pool is drained. Objective-C and Swift
/// programs get pools for free; a C++ program has to make its own, or command buffers
/// and encoders pile up forever. Put one of these on the stack around each frame.
struct AutoreleaseScope {
    AutoreleaseScope() : pool_(NS::AutoreleasePool::alloc()->init()) {}
    ~AutoreleaseScope() { pool_->release(); }
    AutoreleaseScope(const AutoreleaseScope&) = delete;
    AutoreleaseScope& operator=(const AutoreleaseScope&) = delete;

private:
    NS::AutoreleasePool* pool_;
};

class GraphicsDevice {
public:
    /// Uses the system's default GPU.
    GraphicsDevice();

    MTL::Device* device() const { return device_.get(); }
    MTL::CommandQueue* queue() const { return queue_.get(); }

    /// True on Apple GPUs. Their render targets can live in tile memory only, without
    /// any allocation behind them (see the "Tile-Based GPUs" chapter).
    bool supportsMemorylessTargets() const { return supportsMemoryless_; }

    /// True when shaders can trace rays against the scene while drawing: every Apple GPU
    /// from the A13 and the M1 on.
    bool supportsRayTracing() const { return supportsRayTracing_; }

    MTL::SamplerState* materialSampler() const { return materialSampler_.get(); }      // trilinear, anisotropic, repeat
    MTL::SamplerState* pixelSampler() const { return pixelSampler_.get(); }            // nearest, repeat: pixel art
    MTL::SamplerState* linearClampSampler() const { return linearClampSampler_.get(); }
    MTL::SamplerState* pointClampSampler() const { return pointClampSampler_.get(); }
    MTL::SamplerState* shadowSampler() const { return shadowSampler_.get(); }          // compares, then filters

    MTL::DepthStencilState* depthWrite() const { return depthWrite_.get(); }           // test "less", write
    MTL::DepthStencilState* depthReadOnly() const { return depthReadOnly_.get(); }     // test "less or equal", no write
    MTL::DepthStencilState* depthOff() const { return depthOff_.get(); }               // no test, no write

    /// A texture a pass can render into. `sampleCount` above 1 makes it multisampled;
    /// `memoryless` is honored only where supported.
    NS::SharedPtr<MTL::Texture> makeRenderTarget(MTL::PixelFormat format, int width, int height, int sampleCount = 1,
                                                 bool memoryless = false, int mipLevels = 1) const;

private:
    NS::SharedPtr<MTL::Device> device_;
    NS::SharedPtr<MTL::CommandQueue> queue_;
    bool supportsMemoryless_ = false;
    bool supportsRayTracing_ = false;
    NS::SharedPtr<MTL::SamplerState> materialSampler_, pixelSampler_, linearClampSampler_, pointClampSampler_, shadowSampler_;
    NS::SharedPtr<MTL::DepthStencilState> depthWrite_, depthReadOnly_, depthOff_;
};

}  // namespace molten
