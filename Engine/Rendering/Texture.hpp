// Texture.hpp
// An image on the GPU.
#pragma once

#include "Math/Math.hpp"
#include "Rendering/GraphicsDevice.hpp"

#include <memory>
#include <string>
#include <vector>

namespace molten {

enum class TextureFormat : uint8_t {
    RGBA8_sRGB,     // colors: base color and emissive maps. The GPU converts to linear when sampling
    RGBA8,          // data: normal, roughness, metalness and occlusion maps
    R8,             // one channel: font atlases, masks
};

class Texture {
public:
    explicit Texture(NS::SharedPtr<MTL::Texture> texture) : texture_(std::move(texture)) {}

    MTL::Texture* handle() const { return texture_.get(); }
    int width() const { return int(texture_->width()); }
    int height() const { return int(texture_->height()); }

    /// The file name in the Assets folder, if the texture was loaded from one. Scene files store it.
    std::string name;
    /// Drawn in sharp pixels instead of smoothed: pixel art. Set by its import settings.
    bool isPixelated = false;

    /// From pixels in memory, row by row from the top. `mipmaps` builds the whole chain.
    static std::shared_ptr<Texture> fromPixels(GraphicsDevice& device, const uint8_t* pixels, int width, int height,
                                               TextureFormat format, bool mipmaps);

    /// A one-pixel texture: the stand-in for a map a material does not have.
    static std::shared_ptr<Texture> solidColor(GraphicsDevice& device, Vec4 color, TextureFormat format = TextureFormat::RGBA8);

    /// The pixels of a PNG or JPEG file, four bytes each, row by row from the top, for code
    /// that wants to look at a picture rather than draw with it. False if it cannot be read.
    static bool readPixels(const std::string& path, std::vector<uint8_t>& pixels, int& width, int& height);

    /// From a PNG or JPEG file. Returns nullptr and logs if the file cannot be read.
    static std::shared_ptr<Texture> load(GraphicsDevice& device, const std::string& path, TextureFormat format, bool mipmaps = true);

private:
    NS::SharedPtr<MTL::Texture> texture_;
};

}  // namespace molten
