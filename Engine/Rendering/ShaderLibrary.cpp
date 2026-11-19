#include "Rendering/ShaderLibrary.hpp"

#include "Assets/MeshData.hpp"
#include "Core/Log.hpp"
#include "Rendering/GPUTypes.hpp"
#include "Rendering/ShaderSource.hpp"

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
    if (!source.error.empty()) {
        lastError_ = source.error;
        logError("shaders: %s", lastError_.c_str());
        return false;
    }

    // With ray tracing first. If only that part fails (an older system, a mistake in the
    // ray tracing code), the game still runs: the same shaders without it, and shadow maps.
    if (allowRayTracing_) {
        if (compileVariant(source.text, true)) return true;
        logWarning("the ray tracing shaders did not compile; continuing without ray tracing");
    }
    return compileVariant(source.text, false);
}

bool ShaderLibrary::compileVariant(const std::string& source, bool withRayTracing) {
    // The definition goes in front of the text, on the first line, so that the line
    // numbers of everything after it stay what the #line directives say.
    std::string text = withRayTracing ? "#define MOLTEN_RAYTRACING 1\n" + source : source;
    NS::Error* error = nullptr;
    auto compiled = NS::TransferPtr(device_->newLibrary(toNSString(text), nullptr, &error));
    if (!compiled) {
        lastError_ = error ? error->localizedDescription()->utf8String() : "unknown shader compilation error";
        logError("shaders did not compile:\n%s", lastError_.c_str());
        return false;
    }
    library_ = compiled;
    hasRayTracing_ = withRayTracing;
    pipelines_.clear();             // they were built from the old functions
    lastError_.clear();
    logInfo("compiled %zu shader files from %s%s", files_.size(), directory_.c_str(), withRayTracing ? ", with ray tracing" : "");
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
                      (description.usesMeshVertices ? "mesh" : "none");
    auto found = pipelines_.find(key);
    if (found != pipelines_.end()) return found->second.get();

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
        }
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
