#include "Rendering/GraphicsDevice.hpp"

#include "Core/Log.hpp"

#include <cstdlib>

namespace molten {

namespace {

NS::SharedPtr<MTL::SamplerState> makeSampler(MTL::Device* device, MTL::SamplerMinMagFilter filter, MTL::SamplerMipFilter mipFilter,
                                             MTL::SamplerAddressMode addressMode, int anisotropy, bool compares) {
    auto descriptor = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());
    descriptor->setMinFilter(filter);
    descriptor->setMagFilter(filter);
    descriptor->setMipFilter(mipFilter);
    descriptor->setSAddressMode(addressMode);
    descriptor->setTAddressMode(addressMode);
    descriptor->setRAddressMode(addressMode);
    descriptor->setMaxAnisotropy(NS::UInteger(anisotropy));
    if (compares) descriptor->setCompareFunction(MTL::CompareFunctionLessEqual);
    return NS::TransferPtr(device->newSamplerState(descriptor.get()));
}

NS::SharedPtr<MTL::DepthStencilState> makeDepthState(MTL::Device* device, MTL::CompareFunction compare, bool write) {
    auto descriptor = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    descriptor->setDepthCompareFunction(compare);
    descriptor->setDepthWriteEnabled(write);
    return NS::TransferPtr(device->newDepthStencilState(descriptor.get()));
}

}  // namespace

GraphicsDevice::GraphicsDevice() {
    AutoreleaseScope pool;
    device_ = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!device_) {
        logError("Metal is not available on this machine");
        std::abort();
    }
    queue_ = NS::TransferPtr(device_->newCommandQueue());
    supportsMemoryless_ = device_->supportsFamily(MTL::GPUFamilyApple2);
    supportsRayTracing_ = device_->supportsRaytracing() && device_->supportsRaytracingFromRender();
    supportsLayeredRendering_ = device_->supportsFamily(MTL::GPUFamilyApple5) || device_->supportsFamily(MTL::GPUFamilyMac2);
    supportsFramebufferRead_ = device_->supportsFamily(MTL::GPUFamilyApple1);
    logInfo("GPU: %s", device_->name()->utf8String());

    MTL::Device* device = device_.get();
    materialSampler_ = makeSampler(device, MTL::SamplerMinMagFilterLinear, MTL::SamplerMipFilterLinear, MTL::SamplerAddressModeRepeat, 8, false);
    pixelSampler_ = makeSampler(device, MTL::SamplerMinMagFilterNearest, MTL::SamplerMipFilterLinear, MTL::SamplerAddressModeRepeat, 1, false);
    noiseSampler_ = makeSampler(device, MTL::SamplerMinMagFilterLinear, MTL::SamplerMipFilterLinear, MTL::SamplerAddressModeRepeat, 1, false);
    linearClampSampler_ = makeSampler(device, MTL::SamplerMinMagFilterLinear, MTL::SamplerMipFilterLinear, MTL::SamplerAddressModeClampToEdge, 1, false);
    pointClampSampler_ = makeSampler(device, MTL::SamplerMinMagFilterNearest, MTL::SamplerMipFilterNotMipmapped, MTL::SamplerAddressModeClampToEdge, 1, false);
    shadowSampler_ = makeSampler(device, MTL::SamplerMinMagFilterLinear, MTL::SamplerMipFilterNotMipmapped, MTL::SamplerAddressModeClampToEdge, 1, true);

    depthWrite_ = makeDepthState(device, MTL::CompareFunctionLess, true);
    depthReadOnly_ = makeDepthState(device, MTL::CompareFunctionLessEqual, false);
    depthOff_ = makeDepthState(device, MTL::CompareFunctionAlways, false);
}

NS::SharedPtr<MTL::Texture> GraphicsDevice::makeRenderTarget(MTL::PixelFormat format, int width, int height, int sampleCount,
                                                             bool memoryless, int mipLevels) const {
    auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(sampleCount > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
    descriptor->setPixelFormat(format);
    descriptor->setWidth(NS::UInteger(width > 0 ? width : 1));
    descriptor->setHeight(NS::UInteger(height > 0 ? height : 1));
    descriptor->setSampleCount(NS::UInteger(sampleCount));
    descriptor->setMipmapLevelCount(NS::UInteger(mipLevels));
    if (memoryless && supportsMemoryless_) {
        descriptor->setUsage(MTL::TextureUsageRenderTarget);
        descriptor->setStorageMode(MTL::StorageModeMemoryless);
    } else {
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        descriptor->setStorageMode(MTL::StorageModePrivate);
    }
    return NS::TransferPtr(device_->newTexture(descriptor.get()));
}

}  // namespace molten
