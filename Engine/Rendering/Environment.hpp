// Environment.hpp
// The sky as a light source.
//
// The procedural sky is rendered into a cubemap, and from that cubemap come the three
// textures of image-based lighting: the irradiance map for diffuse light, the
// prefiltered map for reflections of every roughness, and the BRDF lookup table.
// Everything happens on the GPU with small render passes, one per cube face and mip
// level. It runs when the engine starts and again whenever the sky changes.
#pragma once

#include "Rendering/GPUTypes.hpp"
#include "Rendering/GraphicsDevice.hpp"
#include "Rendering/ShaderLibrary.hpp"

namespace molten {

class Environment {
public:
    Environment(GraphicsDevice& device, ShaderLibrary& shaders);

    /// Re-renders the sky and its filtered maps if the sky described by `frame` differs
    /// from the one captured last. Returns true if it encoded any work.
    bool updateIfNeeded(MTL::CommandBuffer* commandBuffer, const gpu::FrameUniforms& frame);

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
    void renderFace(MTL::CommandBuffer* commandBuffer, MTL::Texture* target, int face, int mip, const char* fragmentFunction,
                    const gpu::CaptureUniforms& capture, const gpu::FrameUniforms& frame, MTL::Texture* source);

    GraphicsDevice& device_;
    ShaderLibrary& shaders_;
    NS::SharedPtr<MTL::Texture> sky_, irradiance_, prefiltered_, brdfLUT_;
    bool hasCapture_ = false;
    bool hasLUT_ = false;
    gpu::FrameUniforms captured_{};
};

}  // namespace molten
