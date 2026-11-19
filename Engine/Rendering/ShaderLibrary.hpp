// ShaderLibrary.hpp
// Compiles the shaders and hands out render pipeline states.
//
// The shaders are compiled from source when the engine starts. That costs a fraction
// of a second, and buys two things while developing: no build step for shaders, and
// hot reloading. Save a .metal file while the game runs and the engine recompiles it;
// if the new version has an error, the message is logged and the old shaders stay.
//
// A pipeline state fixes the vertex and fragment functions, the formats of the
// textures drawn into, the sample count and the blending. Creating one is slow, so
// each combination is created the first time it is asked for and kept.
#pragma once

#include <Metal/Metal.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

enum class BlendMode : uint8_t {
    Opaque,         // the new color replaces the old one
    AlphaBlend,     // new * alpha + old * (1 - alpha)
    Additive,       // new * alpha + old
};

struct PipelineDescription {
    std::string vertexFunction;
    std::string fragmentFunction;                           // empty for depth-only passes
    MTL::PixelFormat colorFormat = MTL::PixelFormatInvalid; // Invalid: no color attachment
    MTL::PixelFormat depthFormat = MTL::PixelFormatInvalid; // Invalid: no depth attachment
    int sampleCount = 1;
    BlendMode blend = BlendMode::Opaque;
    bool usesMeshVertices = false;                          // reads vertices through the mesh vertex descriptor
};

class ShaderLibrary {
public:
    /// With `allowRayTracing` the shaders are compiled with MOLTEN_RAYTRACING defined, which
    /// adds the code that traces rays. Pass what GraphicsDevice::supportsRayTracing() says.
    ShaderLibrary(MTL::Device* device, std::string directory, bool allowRayTracing = false);

    /// True if the shaders in use contain the ray tracing code. False on a GPU without ray
    /// tracing, and also if that code failed to compile and the engine fell back.
    bool hasRayTracing() const { return hasRayTracing_; }

    /// The pipeline for this description, or nullptr if the shaders failed to compile.
    MTL::RenderPipelineState* pipeline(const PipelineDescription& description);

    /// Recompiles if any shader file changed on disk. Returns true if new shaders are in use.
    bool reloadIfChanged();

    bool isValid() const { return library_.get() != nullptr; }
    /// The compiler's message from the last failed compilation; empty after a success.
    const std::string& lastError() const { return lastError_; }

private:
    bool compile();
    bool compileVariant(const std::string& source, bool withRayTracing);
    std::filesystem::file_time_type newestFileTime() const;

    MTL::Device* device_;
    std::string directory_;
    NS::SharedPtr<MTL::Library> library_;
    NS::SharedPtr<MTL::VertexDescriptor> meshVertexDescriptor_;
    std::unordered_map<std::string, NS::SharedPtr<MTL::RenderPipelineState>> pipelines_;
    std::vector<std::string> files_;
    std::filesystem::file_time_type loadedTime_{};
    std::string lastError_;
    bool allowRayTracing_ = false;
    bool hasRayTracing_ = false;
};

}  // namespace molten
