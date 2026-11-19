#include "Rendering/Environment.hpp"

#include <cstring>

namespace molten {

Environment::Environment(GraphicsDevice& device, ShaderLibrary& shaders) : device_(device), shaders_(shaders) {
    sky_ = makeCube(kSkySize, 9);                       // 256 down to 1
    irradiance_ = makeCube(kIrradianceSize, 1);
    prefiltered_ = makeCube(kPrefilteredSize, kPrefilteredMips);
    brdfLUT_ = device_.makeRenderTarget(MTL::PixelFormatRG16Float, kLUTSize, kLUTSize);
}

NS::SharedPtr<MTL::Texture> Environment::makeCube(int size, int mipLevels) const {
    auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(MTL::TextureTypeCube);
    descriptor->setPixelFormat(MTL::PixelFormatRGBA16Float);
    descriptor->setWidth(NS::UInteger(size));
    descriptor->setHeight(NS::UInteger(size));
    descriptor->setMipmapLevelCount(NS::UInteger(mipLevels));
    descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    return NS::TransferPtr(device_.device()->newTexture(descriptor.get()));
}

void Environment::renderFace(MTL::CommandBuffer* commandBuffer, MTL::Texture* target, int face, int mip,
                             const char* fragmentFunction, const gpu::CaptureUniforms& capture,
                             const gpu::FrameUniforms& frame, MTL::Texture* source) {
    PipelineDescription description;
    description.vertexFunction = "vertex_fullscreen";
    description.fragmentFunction = fragmentFunction;
    description.colorFormat = target->pixelFormat();
    MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
    if (!pipeline) return;

    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(target);
    color->setSlice(NS::UInteger(face));
    color->setLevel(NS::UInteger(mip));
    color->setLoadAction(MTL::LoadActionDontCare);      // every pixel is written
    color->setStoreAction(MTL::StoreActionStore);

    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(NS::String::string(fragmentFunction, NS::UTF8StringEncoding));       // shows up in Xcode's GPU debugger
    encoder->setRenderPipelineState(pipeline);
    encoder->setFragmentBytes(&capture, sizeof(capture), gpu::MMBufferPass);
    encoder->setFragmentBytes(&frame, sizeof(frame), gpu::MMBufferFrame);
    if (source) encoder->setFragmentTexture(source, 0);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
}

bool Environment::updateIfNeeded(MTL::CommandBuffer* commandBuffer, const gpu::FrameUniforms& frame) {
    // Only the fields the sky shader reads matter here.
    bool same = hasCapture_ &&
                simd::all(frame.sunDirection == captured_.sunDirection) && simd::all(frame.sunColor == captured_.sunColor) &&
                simd::all(frame.skyZenithColor == captured_.skyZenithColor) &&
                simd::all(frame.skyHorizonColor == captured_.skyHorizonColor) &&
                simd::all(frame.skyGroundColor == captured_.skyGroundColor) && frame.skyIntensity == captured_.skyIntensity &&
                // Clouds drift every frame: only a change in how many there are is worth a new capture.
                frame.cloudCoverage == captured_.cloudCoverage && frame.cloudScale == captured_.cloudScale;
    if (same || !shaders_.isValid()) return false;

    gpu::CaptureUniforms capture{};
    capture.sourceSize = float(kSkySize);

    // 1. The sky itself, then its mip chain: the filters below read blurry levels of it.
    for (int face = 0; face < 6; face++) {
        capture.face = uint32_t(face);
        renderFace(commandBuffer, sky_.get(), face, 0, "fragment_sky_capture", capture, frame, nullptr);
    }
    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    blit->generateMipmaps(sky_.get());
    blit->endEncoding();

    // 2. Diffuse irradiance.
    for (int face = 0; face < 6; face++) {
        capture.face = uint32_t(face);
        renderFace(commandBuffer, irradiance_.get(), face, 0, "fragment_irradiance", capture, frame, sky_.get());
    }

    // 3. Reflections: mip 0 is a mirror, the last mip is fully rough.
    for (int mip = 0; mip < kPrefilteredMips; mip++) {
        capture.roughness = float(mip) / float(kPrefilteredMips - 1);
        for (int face = 0; face < 6; face++) {
            capture.face = uint32_t(face);
            renderFace(commandBuffer, prefiltered_.get(), face, mip, "fragment_prefilter", capture, frame, sky_.get());
        }
    }

    // 4. The lookup table does not depend on the sky: once is enough.
    if (!hasLUT_) {
        renderFace(commandBuffer, brdfLUT_.get(), 0, 0, "fragment_brdf_lut", capture, frame, nullptr);
        hasLUT_ = true;
    }

    captured_ = frame;
    hasCapture_ = true;
    return true;
}

}  // namespace molten
