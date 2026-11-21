// ShaderLibrary.hpp
// Compiles the shaders and hands out render pipeline states.
//
// The build compiles the shaders ahead of time (Shaders/Compiled, by Tools/compile_shaders.sh)
// when the Metal compiler is installed; the engine loads those when no source is newer.
// Otherwise it compiles the sources when it starts: a fraction of a second, no build step.
// Either way, save a .metal file while the game runs and the engine recompiles it;
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
    Multiply,       // new * old: darkens what is there (decals of dirt)
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
    /// True if the shaders in use contain the extras (MOLTEN_EXTRAS): water, fog that lies
    /// low and glows toward the sun, traced ambient light. They are tried first and left out
    /// if they do not compile, so that a mistake in them never costs the whole picture.
    bool hasExtras() const { return hasExtras_; }

    /// The pipeline for this description, or nullptr if the shaders failed to compile.
    MTL::RenderPipelineState* pipeline(const PipelineDescription& description);

    /// Recompiles if any shader file changed on disk. Returns true if new shaders are in use.
    bool reloadIfChanged();

    /// The code of the hand-written surface shaders (see SurfaceShader.hpp), compiled with
    /// the engine's own from now on. If it does not compile, the engine's shaders are used
    /// without it, materials that use one are drawn as plain lit surfaces, and
    /// surfaceError() says what the compiler said. Returns true if new shaders are in use.
    bool setSurfaceCode(std::string code);
    bool hasSurfaceCode() const { return hasSurfaceCode_; }
    const std::string& surfaceError() const { return surfaceError_; }

    bool isValid() const { return library_.get() != nullptr; }
    /// The compiler's message from the last failed compilation; empty after a success.
    const std::string& lastError() const { return lastError_; }

private:
    bool compile();
    /// The library the build compiled ahead of time, if it is there and newer than every source file.
    bool loadPrecompiled();
    bool compileVariant(const std::string& source, bool withRayTracing, bool withExtras, bool withSurfaces);
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
    bool hasExtras_ = false;
    std::string surfaceCode_, surfaceError_;
    bool hasSurfaceCode_ = false;
};

}  // namespace molten
