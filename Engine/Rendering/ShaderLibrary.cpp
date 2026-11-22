#include "Rendering/ShaderLibrary.hpp"

#include "Assets/MeshData.hpp"
#include "Core/Log.hpp"
#include "Rendering/GPUTypes.hpp"
#include "Rendering/ShaderSource.hpp"

#include <TargetConditionals.h>
#include <cstdlib>

namespace molten {

namespace {

NS::String* toNSString(const std::string& text) {
    return NS::String::string(text.c_str(), NS::UTF8StringEncoding);
}

/// Describes MeshData::interleaved(): position, normal, tangent, uv, 12 floats in all.
NS::SharedPtr<MTL::VertexDescriptor> makeMeshVertexDescriptor() {
    auto descriptor = NS::TransferPtr(MTL::VertexDescriptor::alloc()->init());
    const MTL::VertexFormat formats[4] = {MTL::VertexFormatFloat3, MTL::VertexFormatFloat3, MTL::VertexFormatFloat4, MTL::VertexFormatFloat2};
    const NS::UInteger offsets[4] = {0, 12, 24, 40};
    for (NS::UInteger i = 0; i < 4; i++) {
        MTL::VertexAttributeDescriptor* attribute = descriptor->attributes()->object(i);
        attribute->setFormat(formats[i]);
        attribute->setOffset(offsets[i]);
        attribute->setBufferIndex(gpu::MMBufferVertices);
    }
    descriptor->layouts()->object(gpu::MMBufferVertices)->setStride(MeshData::kFloatsPerVertex * sizeof(float));
    return descriptor;
}

/// The engine's source with the surface shaders' code put where Forward.metal marks the
/// place, and the line numbers of what follows set right again.
std::string withSurfaceCode(const std::string& source, const std::string& code) {
    const std::string marker = "// MOLTEN_CUSTOM_SURFACES";
    size_t at = source.find(marker);
    if (at == std::string::npos) return source;
    size_t lineEnd = source.find('\n', at);
    if (lineEnd == std::string::npos) lineEnd = source.size() - 1;
    // Which file and line the marker is on: the last #line before it, and the lines since.
    std::string file = "Forward.metal";
    long line = 1;
    size_t directive = source.rfind("#line ", at);
    if (directive != std::string::npos) {
        size_t directiveEnd = source.find('\n', directive);
        line = std::strtol(source.c_str() + directive + 6, nullptr, 10);
        size_t quote = source.find('"', directive);
        if (quote != std::string::npos && quote < directiveEnd) file = source.substr(quote + 1, source.find('"', quote + 1) - quote - 1);
        for (size_t i = directiveEnd + 1; i < at; i++) {
            if (source[i] == '\n') line++;
        }
    }
    std::string restore = "#line " + std::to_string(line + 1) + " \"" + file + "\"\n";
    return source.substr(0, lineEnd + 1) + code + restore + source.substr(lineEnd + 1);
}

}  // namespace

ShaderLibrary::ShaderLibrary(MTL::Device* device, std::string directory, bool allowRayTracing)
    : device_(device), directory_(std::move(directory)), meshVertexDescriptor_(makeMeshVertexDescriptor()),
      allowRayTracing_(allowRayTracing) {
    compile();
}

bool ShaderLibrary::compile() {
    ShaderSource source = ShaderSource::load(directory_);
    files_ = source.files;
    loadedTime_ = newestFileTime();
    degraded_.clear();
    if (!source.error.empty()) {
        lastError_ = source.error;
        logError("shaders: %s", lastError_.c_str());
        return false;
    }

    // Compiled ahead of time by the build (Tools/compile_shaders.sh), from these same files:
    // used when no file has changed since, and the project has no surface shaders of its own.
    if (surfaceCode_.empty() && loadPrecompiled()) return true;

    // The fullest shaders first, then with less: if only the extras fail (a mistake in
    // them, an older system), or only the ray tracing, the game still runs with the rest.
    struct Variant {
        bool rayTracing, extras;
    };
    const Variant variants[] = {{true, true}, {true, false}, {false, true}, {false, false}};
    // Hand-written surface shaders are somebody else's code: tried with each variant once,
    // and left out for good at the first failure, with the compiler's words kept for them.
    bool triesSurfaces = !surfaceCode_.empty();
    surfaceError_.clear();
    for (const Variant& variant : variants) {
        if (variant.rayTracing && !allowRayTracing_) continue;
        if (triesSurfaces) {
            if (compileVariant(withSurfaceCode(source.text, surfaceCode_), variant.rayTracing, variant.extras, true)) return true;
            triesSurfaces = false;
            surfaceError_ = lastError_;
            logError("the project's surface shaders did not compile, and are left out:\n%s", surfaceError_.c_str());
        }
        if (compileVariant(source.text, variant.rayTracing, variant.extras, false)) return true;
        // The first that fails is the fullest this machine can run: whatever comes after is less.
        if (degraded_.empty()) degraded_ = lastError_;
        logWarning("the shaders did not compile%s%s; trying with less", variant.rayTracing ? " with ray tracing" : "", variant.extras ? " with the extras" : "");
    }
    return false;
}

bool ShaderLibrary::loadPrecompiled() {
#if TARGET_OS_OSX
    const char* platform = "macos";
#else
    const char* platform = nullptr;         // iPhone and iPad: compiled when the game starts, for now
#endif
    if (!platform) return false;
    for (bool withRayTracing : {true, false}) {
        if (withRayTracing && !allowRayTracing_) continue;
        std::filesystem::path path = std::filesystem::path(directory_) / "Compiled" /
                                     (std::string("Molten-") + platform + (withRayTracing ? "-rt" : "-plain") + ".metallib");
        std::error_code code;
        auto made = std::filesystem::last_write_time(path, code);
        if (code || made < loadedTime_) continue;        // missing, or older than the sources: compiled from them instead
        NS::Error* error = nullptr;
        auto loaded = NS::TransferPtr(device_->newLibrary(NS::URL::fileURLWithPath(toNSString(path.string())), &error));
        if (!loaded) {
            logWarning("the precompiled shaders %s could not be loaded (%s): compiling the sources", path.string().c_str(),
                       error ? error->localizedDescription()->utf8String() : "unknown error");
            continue;
        }
        library_ = loaded;
        hasRayTracing_ = withRayTracing;
        hasExtras_ = true;
        hasSurfaceCode_ = false;
        pipelines_.clear();
        lastError_.clear();
        logInfo("shaders loaded precompiled from %s", path.string().c_str());
        return true;
    }
    return false;
}

bool ShaderLibrary::setSurfaceCode(std::string code) {
    if (code == surfaceCode_) return false;
    surfaceCode_ = std::move(code);
    return compile();
}

bool ShaderLibrary::compileVariant(const std::string& source, bool withRayTracing, bool withExtras, bool withSurfaces) {
    // The definitions go in front of the text.
    // (Each file of the source says its own line numbers, so lines in front of it move nothing.)
    std::string text = std::string(withRayTracing ? "#define MOLTEN_RAYTRACING 1\n" : "") + (withExtras ? "#define MOLTEN_EXTRAS 1\n" : "") +
                       (withSurfaces ? "#define MOLTEN_CUSTOM 1\n" : "") + source;
    NS::Error* error = nullptr;
    auto compiled = NS::TransferPtr(device_->newLibrary(toNSString(text), nullptr, &error));
    if (!compiled) {
        lastError_ = error ? error->localizedDescription()->utf8String() : "unknown shader compilation error";
        logError("shaders did not compile:\n%s", lastError_.c_str());
        return false;
    }
    library_ = compiled;
    hasRayTracing_ = withRayTracing;
    hasExtras_ = withExtras;
    hasSurfaceCode_ = withSurfaces;
    pipelines_.clear();             // they were built from the old functions
    lastError_.clear();
    logInfo("compiled %zu shader files from %s%s%s%s", files_.size(), directory_.c_str(), withRayTracing ? ", with ray tracing" : "", withExtras ? ", with the extras" : "",
            withSurfaces ? ", with the project's surface shaders" : "");
    return true;
}

std::filesystem::file_time_type ShaderLibrary::newestFileTime() const {
    std::filesystem::file_time_type newest{};
    for (const std::string& file : files_) {
        std::error_code code;
        auto time = std::filesystem::last_write_time(file, code);
        if (!code && time > newest) newest = time;
    }
    return newest;
}

bool ShaderLibrary::reloadIfChanged() {
    if (newestFileTime() <= loadedTime_) return false;
    logInfo("shader files changed, recompiling");
    return compile();
}

MTL::RenderPipelineState* ShaderLibrary::pipeline(const PipelineDescription& description) {
    if (!library_) return nullptr;
    std::string key = description.vertexFunction + "|" + description.fragmentFunction + "|" +
                      std::to_string(int(description.colorFormat)) + "|" + std::to_string(int(description.depthFormat)) + "|" +
                      std::to_string(description.sampleCount) + "|" + std::to_string(int(description.blend)) + "|" +
                      (description.usesMeshVertices ? "mesh" : "none") + (description.isLayered ? "|layered" : "") +
                      (description.secondColorFormat != MTL::PixelFormatInvalid ? "|+" + std::to_string(int(description.secondColorFormat)) : "");
    // One that could not be made is kept too, as nothing: asked for again every frame, it
    // would fail again every frame, and say so every frame.
    auto found = pipelines_.find(key);
    if (found != pipelines_.end()) return found->second.get();
    pipelines_[key] = NS::SharedPtr<MTL::RenderPipelineState>();

    auto descriptor = NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
    descriptor->setLabel(toNSString(key));

    auto vertexFunction = NS::TransferPtr(library_->newFunction(toNSString(description.vertexFunction)));
    if (!vertexFunction) {
        logError("shader function not found: %s", description.vertexFunction.c_str());
        return nullptr;
    }
    descriptor->setVertexFunction(vertexFunction.get());
    if (!description.fragmentFunction.empty()) {
        auto fragmentFunction = NS::TransferPtr(library_->newFunction(toNSString(description.fragmentFunction)));
        if (!fragmentFunction) {
            logError("shader function not found: %s", description.fragmentFunction.c_str());
            return nullptr;
        }
        descriptor->setFragmentFunction(fragmentFunction.get());
    }
    if (description.usesMeshVertices) descriptor->setVertexDescriptor(meshVertexDescriptor_.get());
    // Metal asks what is drawn (triangles) of a pipeline that chooses the slice it draws into.
    if (description.isLayered) descriptor->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassTriangle);

    if (description.colorFormat != MTL::PixelFormatInvalid) {
        MTL::RenderPipelineColorAttachmentDescriptor* color = descriptor->colorAttachments()->object(0);
        color->setPixelFormat(description.colorFormat);
        if (description.blend != BlendMode::Opaque) {
            color->setBlendingEnabled(true);
            color->setRgbBlendOperation(MTL::BlendOperationAdd);
            color->setAlphaBlendOperation(MTL::BlendOperationAdd);
            color->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
            color->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
            bool additive = description.blend == BlendMode::Additive;
            color->setDestinationRGBBlendFactor(additive ? MTL::BlendFactorOne : MTL::BlendFactorOneMinusSourceAlpha);
            color->setDestinationAlphaBlendFactor(additive ? MTL::BlendFactorOne : MTL::BlendFactorOneMinusSourceAlpha);
            if (description.blend == BlendMode::Multiply) {
                color->setSourceRGBBlendFactor(MTL::BlendFactorDestinationColor);
                color->setDestinationRGBBlendFactor(MTL::BlendFactorZero);
                color->setSourceAlphaBlendFactor(MTL::BlendFactorZero);
                color->setDestinationAlphaBlendFactor(MTL::BlendFactorOne);
            }
            if (description.blend == BlendMode::Through) {
                // What is there, times the second color, plus the first: each color of it kept on its own.
                color->setSourceRGBBlendFactor(MTL::BlendFactorOne);
                color->setDestinationRGBBlendFactor(MTL::BlendFactorSource1Color);
                color->setSourceAlphaBlendFactor(MTL::BlendFactorZero);
                color->setDestinationAlphaBlendFactor(MTL::BlendFactorOne);
            }
        }
    }
    if (description.secondColorFormat != MTL::PixelFormatInvalid) {
        descriptor->colorAttachments()->object(1)->setPixelFormat(description.secondColorFormat);
    }
    descriptor->setDepthAttachmentPixelFormat(description.depthFormat);
    descriptor->setRasterSampleCount(NS::UInteger(description.sampleCount));

    NS::Error* error = nullptr;
    auto state = NS::TransferPtr(device_->newRenderPipelineState(descriptor.get(), &error));
    if (!state) {
        logError("pipeline %s: %s", key.c_str(), error ? error->localizedDescription()->utf8String() : "unknown error");
        return nullptr;
    }
    pipelines_[key] = state;
    return state.get();
}

}  // namespace molten
