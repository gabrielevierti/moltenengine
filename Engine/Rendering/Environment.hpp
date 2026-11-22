// Environment.hpp
// The sky as a light source.
//
// The procedural sky is rendered into a cubemap, and from that cubemap come the three
// textures of image-based lighting: the irradiance map for diffuse light, the
// prefiltered map for reflections of every roughness, and the BRDF lookup table.
// Everything happens on the GPU with small render passes, one per cube face and mip
// level. It runs when the engine starts and again whenever the sky changes.
#pragma once

#include <algorithm>
#include <cmath>

#include "Rendering/GPUTypes.hpp"
#include "Rendering/GraphicsDevice.hpp"
#include "Rendering/ShaderLibrary.hpp"

namespace molten {

/// True if the sky of `now` is so near the one captured `then` that capturing it again can
/// wait a few frames: the sun a degree or two farther on, the colors within a tenth, as many
/// clouds. A day going by moves the sky a little every frame, and a capture is some fifty
/// small passes: done every sixth frame it looks the same.
inline bool skyBarelyChanged(const gpu::FrameUniforms& now, const gpu::FrameUniforms& then) {
    // How far apart two directions or colors are, as a share of their size.
    auto apart = [](const auto& a, const auto& b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        float size = std::max(std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z), std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z));
        return std::sqrt(dx * dx + dy * dy + dz * dz) / std::max(size, 0.001f);
    };
    return apart(now.sunDirection, then.sunDirection) < 0.035f && apart(now.skySunDirection, then.skySunDirection) < 0.035f &&
           apart(now.sunColor, then.sunColor) < 0.1f && apart(now.skyZenithColor, then.skyZenithColor) < 0.1f &&
           apart(now.skyHorizonColor, then.skyHorizonColor) < 0.1f && apart(now.skyGroundColor, then.skyGroundColor) < 0.1f &&
           std::abs(now.skyIntensity - then.skyIntensity) <= 0.1f * std::max(std::abs(now.skyIntensity), std::abs(then.skyIntensity)) &&
           std::abs(now.cloudCoverage - then.cloudCoverage) < 0.03f && now.cloudScale == then.cloudScale &&
           std::abs(now.moonBrightness - then.moonBrightness) < 0.1f && now.sunsetColors == then.sunsetColors &&
           now.cloudBase == then.cloudBase && now.cloudTop == then.cloudTop && (now.volumetricClouds != 0) == (then.volumetricClouds != 0);
}

class Environment {
public:
    Environment(GraphicsDevice& device, ShaderLibrary& shaders);

    /// Re-renders the sky and its filtered maps if the sky described by `frame` differs
    /// from the one captured last. Returns true if it encoded any work.
    /// With the two blocks of noise clouds with depth are made of (and `frame` asking for such
    /// clouds), the captured sky has them in it, so that what mirrors the sky mirrors the one
    /// on screen; without, its clouds are the flat layer.
    bool updateIfNeeded(MTL::CommandBuffer* commandBuffer, const gpu::FrameUniforms& frame, MTL::Texture* cloudShape = nullptr,
                        MTL::Texture* cloudDetail = nullptr);

    /// Forces the next updateIfNeeded() to run, for example after a shader reload.
    void invalidate() {
        hasCapture_ = false;
        hasLUT_ = false;
    }

    MTL::Texture* irradianceMap() const { return irradiance_.get(); }
    MTL::Texture* prefilteredMap() const { return prefiltered_.get(); }
    MTL::Texture* brdfLUT() const { return brdfLUT_.get(); }
    float prefilteredMipCount() const { return float(kPrefilteredMips); }

private:
    static constexpr int kSkySize = 256;
    static constexpr int kIrradianceSize = 32;
    static constexpr int kPrefilteredSize = 128;
    static constexpr int kPrefilteredMips = 6;
    static constexpr int kLUTSize = 256;

    NS::SharedPtr<MTL::Texture> makeCube(int size, int mipLevels) const;
    /// Draws the full-screen triangle into one face and mip level of a cubemap (or a 2D texture).
    /// False if the shaders have no such function (one of the extras, and they are without): nothing was drawn.
    bool renderFace(MTL::CommandBuffer* commandBuffer, MTL::Texture* target, int face, int mip, const char* fragmentFunction,
                    const gpu::CaptureUniforms& capture, const gpu::FrameUniforms& frame, MTL::Texture* source, MTL::Texture* cloudShape = nullptr,
                    MTL::Texture* cloudDetail = nullptr);

    GraphicsDevice& device_;
    ShaderLibrary& shaders_;
    NS::SharedPtr<MTL::Texture> sky_, irradiance_, prefiltered_, brdfLUT_;
    bool hasCapture_ = false;
    bool hasLUT_ = false;
    bool capturedClouds_ = false;       // the last capture walked through clouds with depth
    int callsSinceCapture_ = 0;         // a sky that barely changed waits for kCallsBetweenCaptures of them
    static constexpr int kCallsBetweenCaptures = 6;
    gpu::FrameUniforms captured_{};
};

}  // namespace molten
