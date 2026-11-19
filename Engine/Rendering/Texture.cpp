#include "Rendering/Texture.hpp"

#include "Core/Log.hpp"

#include <stb_image.h>

namespace molten {

std::shared_ptr<Texture> Texture::fromPixels(GraphicsDevice& device, const uint8_t* pixels, int width, int height,
                                             TextureFormat format, bool mipmaps) {
    MTL::PixelFormat pixelFormat = format == TextureFormat::RGBA8_sRGB ? MTL::PixelFormatRGBA8Unorm_sRGB
                                 : format == TextureFormat::RGBA8 ? MTL::PixelFormatRGBA8Unorm
                                 : MTL::PixelFormatR8Unorm;
    int bytesPerPixel = format == TextureFormat::R8 ? 1 : 4;

    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::texture2DDescriptor(
        pixelFormat, NS::UInteger(width), NS::UInteger(height), mipmaps && (width > 1 || height > 1));
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    auto texture = NS::TransferPtr(device.device()->newTexture(descriptor));
    texture->replaceRegion(MTL::Region(0, 0, NS::UInteger(width), NS::UInteger(height)), 0, pixels,
                           NS::UInteger(width * bytesPerPixel));

    if (texture->mipmapLevelCount() > 1) {
        // The GPU builds the smaller levels: a blit pass, submitted right away.
        MTL::CommandBuffer* commandBuffer = device.queue()->commandBuffer();
        MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
        blit->generateMipmaps(texture.get());
        blit->endEncoding();
        commandBuffer->commit();
    }
    return std::make_shared<Texture>(texture);
}

std::shared_ptr<Texture> Texture::solidColor(GraphicsDevice& device, Vec4 color, TextureFormat format) {
    Vec4 scaled = simd::clamp(color, Vec4{0, 0, 0, 0}, Vec4{1, 1, 1, 1}) * 255.0f + 0.5f;
    const uint8_t pixel[4] = {uint8_t(scaled.x), uint8_t(scaled.y), uint8_t(scaled.z), uint8_t(scaled.w)};
    return fromPixels(device, pixel, 1, 1, format, false);
}

std::shared_ptr<Texture> Texture::load(GraphicsDevice& device, const std::string& path, TextureFormat format, bool mipmaps) {
    int width = 0, height = 0, channels = 0;
    int wanted = format == TextureFormat::R8 ? 1 : 4;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, wanted);
    if (!pixels) {
        logError("cannot load texture %s: %s", path.c_str(), stbi_failure_reason());
        return nullptr;
    }
    std::shared_ptr<Texture> texture = fromPixels(device, pixels, width, height, format, mipmaps);
    stbi_image_free(pixels);
    return texture;
}

}  // namespace molten
