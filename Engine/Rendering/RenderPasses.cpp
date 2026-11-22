// RenderPasses.cpp
// The passes of a frame, in the order they run. Renderer.cpp decides what is drawn;
// this file is where the draw calls are.
#include "Rendering/Particles.hpp"
#include "Rendering/Renderer.hpp"

#include <algorithm>

namespace molten {

namespace {

NS::String* label(const char* text) {
    return NS::String::string(text, NS::UTF8StringEncoding);
}

Vec2 texelSize(MTL::Texture* texture) {
    return Vec2{1.0f / float(texture->width()), 1.0f / float(texture->height())};
}

}  // namespace

// ---------------------------------------------------------------- meshes

void Renderer::bindMaterial(MTL::RenderCommandEncoder* encoder, const Material& material) {
    gpu::MaterialUniforms uniforms{};
    uniforms.baseColor = material.baseColor;
    uniforms.emissive = material.emissive;
    uniforms.metallic = material.metallic;
    uniforms.roughness = material.roughness;
    uniforms.normalScale = material.normalScale;
    uniforms.occlusionStrength = material.occlusionStrength;
    uniforms.alphaCutoff = material.alphaCutoff;
    uniforms.uvScale = material.uvScale;
    // Water's waves are the wind's (the pass's uniforms carry it); the material says how long and how high it makes them.
    uniforms.water = material.isWater ? 1.0f / std::clamp(material.waveSize, 0.01f, 100.0f) : 0.0f;
    uniforms.waveHeight = std::max(material.waveHeight, 0.0f);
    uniforms.foam = std::max(material.foam, 0.0f);
    uniforms.isSea = material.isSea ? 1.0f : 0.0f;
    uniforms.sway = std::max(material.sway, 0.0f);
    // Water, and what a shader of the game's own draws seen through (its own water): they read how far what is behind them is.
    if (material.isWater || (!material.shader.empty() && material.isSeenThrough())) waterSeen_ = true;
    if (!material.shader.empty()) {
        // Its own shader: which one, and its properties in the order the shader declares them.
        int place = surfaceShaders_.use(material.shader);
        const SurfaceShader* shader = surfaceShaders_.shader(place);
        if (shader && place <= compiledSurfaceShaders_ && shaders_.hasSurfaceCode()) {
            uniforms.customShader = float(place);
            for (size_t i = 0; i < shader->properties.size() && i < 8; i++) {
                const Vec4* said = material.shaderValue(shader->properties[i].name);
                uniforms.custom[i] = said ? *said : shader->properties[i].value;
            }
        }
    }
    // A few dozen bytes: Metal copies them into the command buffer, no buffer of our own needed.
    encoder->setFragmentBytes(&uniforms, sizeof(uniforms), gpu::MMBufferMaterial);
    encoder->setVertexBytes(&uniforms, sizeof(uniforms), gpu::MMBufferMaterial);      // the wind sways it in the vertex stage

    // A material without a map gets a one-pixel stand-in, so the shader needs no branches.
    auto textureOr = [](const std::shared_ptr<Texture>& texture, const std::shared_ptr<Texture>& fallback) {
        return (texture ? texture : fallback)->handle();
    };
    encoder->setFragmentTexture(textureOr(material.baseColorTexture, white_), gpu::MMTextureBaseColor);
    encoder->setFragmentTexture(textureOr(material.normalTexture, flatNormal_), gpu::MMTextureNormal);
    encoder->setFragmentTexture(textureOr(material.metallicRoughnessTexture, white_), gpu::MMTextureMetallicRoughness);
    encoder->setFragmentTexture(textureOr(material.occlusionTexture, white_), gpu::MMTextureOcclusion);
    encoder->setFragmentTexture(textureOr(material.emissiveTexture, white_), gpu::MMTextureEmissive);
    // Pixel art is drawn in sharp pixels: its picture says so (its import settings).
    bool isPixelated = material.baseColorTexture && material.baseColorTexture->isPixelated;
    encoder->setFragmentSamplerState(isPixelated ? device_.pixelSampler() : device_.materialSampler(), gpu::MMSamplerMaterial);
}

MTL::RenderCommandEncoder* Renderer::encoderFor(MTL::CommandBuffer* commandBuffer, MTL::RenderPassDescriptor* pass, const char* name) {
    if (gpuTimer_) gpuTimer_->time(pass, name);
    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass);
    encoder->setLabel(label(name));
    return encoder;
}

void Renderer::drawBatches(MTL::RenderCommandEncoder* encoder, const std::vector<Batch>& batches, MeshPass pass,
                           MTL::PixelFormat colorFormat, int sampleCount, bool bothSides) {
    const Material* boundMaterial = nullptr;
    for (const Batch& batch : batches) {
        const Material& material = *batch.material;
        bool isMasked = material.alphaMode == AlphaMode::Mask;
        if (batch.mesh == waterSheet_.get() && pass != MeshPass::Forward) continue;        // water's sheet is only what is seen: it has no shape of its own to shade or to hide with

        PipelineDescription description;
        description.usesMeshVertices = true;
        description.colorFormat = colorFormat;
        description.depthFormat = kDepthFormat;
        description.sampleCount = sampleCount;
        switch (pass) {
            case MeshPass::Shadow:
                // Depth only. Cut-out materials still need a fragment function, to make their holes.
                description.vertexFunction = isMasked ? "vertex_mesh" : "vertex_mesh_depth";
                description.fragmentFunction = isMasked ? "fragment_depth_masked" : "";
                break;
            case MeshPass::Prepass:
                description.vertexFunction = "vertex_mesh";
                description.fragmentFunction = isMasked ? "fragment_prepass_masked" : "fragment_prepass";
                break;
            case MeshPass::Forward:
                // A sheet of water is the grid laid around the eye (collectItems put it in the mesh's place): its points are put on the sheet and moved by the waves.
                description.vertexFunction = batch.mesh == waterSheet_.get() ? "vertex_water" : "vertex_mesh";
                if (material.isWater && readsBehindWater()) {
                    // Water one sees into (Water.metal): its own light, plus what is drawn under it
                    // times the colors that get through it. The blending does it, once a pixel.
                    description.fragmentFunction = tracesRaysThisFrame_ ? "fragment_water_traced" : "fragment_water";
                    description.blend = BlendMode::Through;
                    break;
                }
                if (material.isUnlit) description.fragmentFunction = "fragment_unlit";
                else if (tracesRaysThisFrame_) description.fragmentFunction = isMasked ? "fragment_pbr_masked_traced" : "fragment_pbr_traced";
                else description.fragmentFunction = isMasked ? "fragment_pbr_masked" : "fragment_pbr";
                if (material.isSeenThrough()) description.blend = BlendMode::AlphaBlend;
                break;
        }
        MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
        if (!pipeline) continue;

        encoder->setRenderPipelineState(pipeline);
        encoder->setCullMode(material.isDoubleSided || bothSides ? MTL::CullModeNone : MTL::CullModeBack);
        // vertex_mesh reads the material too (how the wind sways it): every pass that uses it binds it.
        if (batch.material != boundMaterial && (pass == MeshPass::Forward || pass == MeshPass::Prepass || isMasked)) {
            bindMaterial(encoder, material);
            boundMaterial = batch.material;
        }
        encoder->setVertexBuffer(batch.mesh->vertexBuffer(), 0, gpu::MMBufferVertices);
        encoder->setVertexBuffer(batch.instances.buffer, batch.instances.offset, gpu::MMBufferInstances);
        encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(batch.mesh->indexCount()), MTL::IndexTypeUInt32,
                                       batch.mesh->indexBuffer(), NS::UInteger(0), NS::UInteger(batch.count));

        if (pass == MeshPass::Forward) {
            stats.drawCalls++;
            stats.instances += int(batch.count);
            stats.triangles += int(batch.mesh->indexCount() / 3 * batch.count);
        } else if (pass == MeshPass::Prepass) {
            stats.depthDraws++;
        } else {
            stats.shadowDraws++;
        }
    }
}

// ---------------------------------------------------------------- 1. shadow maps

std::vector<Renderer::ShadowCaster> Renderer::findShadowCasters(const std::vector<DrawItem>& items, const Cascades& cascades) {
    std::vector<ShadowCaster> casters;
    for (const DrawItem& item : items) {
        // Glass and other blended materials let the light through.
        if (!item.castsShadows || item.material->isSeenThrough()) continue;
        uint32_t reached = 0;
        for (int cascade = 0; cascade < cascades.count; cascade++) {
            if (cascades.reaches(cascade, item.bounds)) reached |= 1u << cascade;
        }
        if (reached) casters.push_back(ShadowCaster{&item, reached});
    }
    stats.shadowCasters += int(casters.size());
    return casters;
}

void Renderer::renderShadows(MTL::CommandBuffer* commandBuffer, std::vector<ShadowCaster>& casters, const Cascades& cascades,
                             const FrameAllocator::Allocation& passUniforms, const FrameAllocator::Allocation& frameUniforms) {
    // Every cascade in one pass: the shadow map is an array of textures, and each copy of a
    // mesh says which slice it goes into. One draw for each mesh with each material.
    PipelineDescription plain;
    plain.usesMeshVertices = true;
    plain.isLayered = true;
    plain.depthFormat = kDepthFormat;
    plain.vertexFunction = "vertex_shadow";
    PipelineDescription swaying = plain;        // the wind moves it: its shadow moves with it
    swaying.vertexFunction = "vertex_shadow_detailed";
    PipelineDescription masked = swaying;       // holes cut in it: they are holes in its shadow too
    masked.fragmentFunction = "fragment_shadow_masked";
    if (!device_.supportsLayeredRendering() || !shaders_.hasExtras() || !shaders_.pipeline(plain) || !shaders_.pipeline(swaying) || !shaders_.pipeline(masked)) {
        renderShadowsEachCascade(commandBuffer, casters, cascades);
        return;
    }

    // Things that share a material and a mesh next to each other: each run is one draw.
    std::sort(casters.begin(), casters.end(), [](const ShadowCaster& a, const ShadowCaster& b) {
        if (a.item->material != b.item->material) return std::less<const Material*>()(a.item->material, b.item->material);
        return std::less<const Mesh*>()(a.item->mesh, b.item->mesh);
    });

    // No color attachment: a shadow map is only depth. Every slice is cleared, and drawn into.
    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(shadowMap_.get());
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1.0);
    depth->setStoreAction(MTL::StoreActionStore);
    pass->setRenderTargetArrayLength(NS::UInteger(shadowMapSlices_));

    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Shadow cascades");
    encoder->setDepthStencilState(device_.depthWrite());
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    // Casters in front of a cascade's box are flattened onto its near plane instead of being clipped away.
    encoder->setDepthClipMode(MTL::DepthClipModeClamp);
    // Push depths back in proportion to the surface's slope as the light sees it.
    encoder->setDepthBias(0.0f, 1.5f, 0.0f);
    encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);      // the wind and the time, for what sways
    encoder->setVertexBuffer(frameUniforms.buffer, frameUniforms.offset, gpu::MMBufferFrame);   // each cascade's view of the scene
    encoder->setFragmentSamplerState(device_.materialSampler(), gpu::MMSamplerMaterial);

    std::vector<gpu::InstanceData> instances;
    std::vector<gpu::ShadowCopy> copies;
    size_t first = 0;
    while (first < casters.size()) {
        const DrawItem& lead = *casters[first].item;
        instances.clear();
        copies.clear();
        size_t next = first;
        while (next < casters.size() && casters[next].item->mesh == lead.mesh && casters[next].item->material == lead.material) {
            const DrawItem& item = *casters[next].item;
            for (int cascade = 0; cascade < cascades.count; cascade++) {
                if (casters[next].cascades & (1u << cascade)) copies.push_back(gpu::ShadowCopy{uint32_t(instances.size()), uint32_t(cascade)});
            }
            instances.push_back(gpu::InstanceData{item.world, normalMatrix(item.world), item.tint});
            next++;
        }
        first = next;

        const Material& material = *lead.material;
        const bool isMasked = material.alphaMode == AlphaMode::Mask, sways = material.sway > 0;
        MTL::RenderPipelineState* pipeline = shaders_.pipeline(isMasked ? masked : sways ? swaying : plain);
        if (!pipeline || copies.empty()) continue;
        encoder->setRenderPipelineState(pipeline);
        encoder->setCullMode(material.isDoubleSided ? MTL::CullModeNone : MTL::CullModeBack);
        if (isMasked || sways) bindMaterial(encoder, material);
        FrameAllocator::Allocation instanceBuffer = allocator().copyArray(instances.data(), instances.size());
        FrameAllocator::Allocation copyBuffer = allocator().copyArray(copies.data(), copies.size());
        encoder->setVertexBuffer(lead.mesh->vertexBuffer(), 0, gpu::MMBufferVertices);
        encoder->setVertexBuffer(instanceBuffer.buffer, instanceBuffer.offset, gpu::MMBufferInstances);
        encoder->setVertexBuffer(copyBuffer.buffer, copyBuffer.offset, gpu::MMBufferShadowCopies);
        encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(lead.mesh->indexCount()), MTL::IndexTypeUInt32,
                                       lead.mesh->indexBuffer(), NS::UInteger(0), NS::UInteger(copies.size()));
        stats.shadowDraws++;
    }
    encoder->endEncoding();
}

void Renderer::renderShadowsEachCascade(MTL::CommandBuffer* commandBuffer, const std::vector<ShadowCaster>& casters, const Cascades& cascades) {
    for (int cascade = 0; cascade < cascades.count; cascade++) {
        std::vector<const DrawItem*> inThisOne;
        for (const ShadowCaster& caster : casters) {
            if (caster.cascades & (1u << cascade)) inThisOne.push_back(caster.item);
        }
        std::vector<Batch> batches = makeBatches(inThisOne, true);

        gpu::PassUniforms uniforms{};
        uniforms.viewProjection = cascades.viewProjection[cascade];
        uniforms.view = identity();
        uniforms.projection = cascades.viewProjection[cascade];
        FrameAllocator::Allocation passUniforms = allocator().copy(uniforms);

        // No color attachment: a shadow map is only a depth texture, one array slice per cascade.
        auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
        MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
        depth->setTexture(shadowMap_.get());
        depth->setSlice(NS::UInteger(cascade));
        depth->setLoadAction(MTL::LoadActionClear);
        depth->setClearDepth(1.0);
        depth->setStoreAction(MTL::StoreActionStore);

        MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Shadow cascade");
        encoder->setDepthStencilState(device_.depthWrite());
        encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
        // Casters in front of the box are flattened onto its near plane instead of being clipped away.
        encoder->setDepthClipMode(MTL::DepthClipModeClamp);
        // Push depths back in proportion to the surface's slope as the light sees it.
        encoder->setDepthBias(0.0f, 1.5f, 0.0f);
        encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
        encoder->setFragmentSamplerState(device_.materialSampler(), gpu::MMSamplerMaterial);
        drawBatches(encoder, batches, MeshPass::Shadow, MTL::PixelFormatInvalid, 1);
        encoder->endEncoding();
    }
}

bool Renderer::renderRainMap(MTL::CommandBuffer* commandBuffer, const std::vector<DrawItem>& items, const Mat4& viewProjection) {
    if (!rainMap_) rainMap_ = device_.makeRenderTarget(kDepthFormat, kRainMapSize, kRainMapSize);
    if (!rainMap_) return false;
    // What the rain falls on first: everything solid in the square (water is what it falls into,
    // and glass and other things seen through are left out: their wet look is their own).
    Frustum square(viewProjection);
    std::vector<const DrawItem*> roofs;
    for (const DrawItem& item : items) {
        if (item.material->isWater || item.material->isSeenThrough() || !square.intersects(item.bounds)) continue;
        roofs.push_back(&item);
    }
    std::vector<Batch> batches = makeBatches(roofs, true);

    gpu::PassUniforms uniforms{};
    uniforms.viewProjection = viewProjection;
    uniforms.view = identity();
    uniforms.projection = viewProjection;
    FrameAllocator::Allocation passUniforms = allocator().copy(uniforms);

    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(rainMap_.get());
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1.0);
    depth->setStoreAction(MTL::StoreActionStore);
    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Rain from above");
    encoder->setDepthStencilState(device_.depthWrite());
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    encoder->setDepthClipMode(MTL::DepthClipModeClamp);
    encoder->setDepthBias(0.0f, 1.5f, 0.0f);
    encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentSamplerState(device_.materialSampler(), gpu::MMSamplerMaterial);
    // Both sides of every surface: a roof seen from above may be a plane facing down.
    drawBatches(encoder, batches, MeshPass::Shadow, MTL::PixelFormatInvalid, 1, true);
    encoder->endEncoding();
    return true;
}

// ---------------------------------------------------------------- 2 and 3. ambient occlusion

void Renderer::renderPrepass(MTL::CommandBuffer* commandBuffer, const std::vector<Batch>& opaque, const FrameAllocator::Allocation& passUniforms) {
    // Depth and view-space normals of everything opaque, without multisampling.
    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(prepassNormal_.get());
    color->setLoadAction(MTL::LoadActionClear);
    color->setClearColor(MTL::ClearColor(0.5, 0.5, 1.0, 1.0));       // a normal facing the camera
    color->setStoreAction(MTL::StoreActionStore);
    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(prepassDepth_.get());
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1.0);
    depth->setStoreAction(MTL::StoreActionStore);

    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Depth and normals");
    encoder->setDepthStencilState(device_.depthWrite());
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentSamplerState(device_.materialSampler(), gpu::MMSamplerMaterial);
    drawBatches(encoder, opaque, MeshPass::Prepass, MTL::PixelFormatRGBA8Unorm, 1);
    encoder->endEncoding();
}

void Renderer::renderOcclusion(MTL::CommandBuffer* commandBuffer, const std::vector<Batch>& opaque, const View& view,
                               const FrameAllocator::Allocation& passUniforms) {
    (void)opaque;
    (void)passUniforms;
    gpu::AmbientOcclusionUniforms uniforms{};
    uniforms.projection = view.projection;
    uniforms.inverseProjection = simd::inverse(view.projection);
    uniforms.texelSize = texelSize(occlusionRaw_.get());
    uniforms.radius = settings.ambientOcclusionRadius;
    uniforms.bias = 0.03f;
    uniforms.intensity = settings.ambientOcclusionIntensity;
    uniforms.power = settings.ambientOcclusionPower;
    drawFullscreen(commandBuffer, occlusionRaw_.get(), "fragment_ssao", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                   prepassDepth_.get(), prepassNormal_.get(), device_.pointClampSampler());
    drawFullscreen(commandBuffer, occlusion_.get(), "fragment_ssao_blur", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                   occlusionRaw_.get(), nullptr, nullptr);
}

// ---------------------------------------------------------------- the light around things, traced

bool Renderer::canTraceAmbientFirst() {
    if (!canTraceRays() || !shaders_.hasExtras()) return false;
    PipelineDescription trace;
    trace.vertexFunction = "vertex_fullscreen";
    trace.fragmentFunction = "fragment_traced_ambient";
    trace.colorFormat = kSceneFormat;
    trace.secondColorFormat = kSceneFormat;
    if (!shaders_.pipeline(trace)) return false;
    PipelineDescription gather = trace;
    gather.fragmentFunction = "fragment_traced_accumulate";
    gather.secondColorFormat = MTL::PixelFormatRG16Float;
    if (!shaders_.pipeline(gather)) return false;
    for (const char* function : {"fragment_traced_ambient_smooth", "fragment_traced_ambient_smooth_wide", "fragment_traced_ambient_smooth_widest"}) {
        PipelineDescription smooth;
        smooth.vertexFunction = "vertex_fullscreen";
        smooth.fragmentFunction = function;
        smooth.colorFormat = kSceneFormat;
        if (!shaders_.pipeline(smooth)) return false;
    }
    return true;
}

void Renderer::bindTracedScene(MTL::RenderCommandEncoder* encoder, const FrameAllocator::Allocation& passUniforms,
                               const FrameAllocator::Allocation& frameUniforms) {
    encoder->setFragmentBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(frameUniforms.buffer, frameUniforms.offset, gpu::MMBufferFrame);
    encoder->setFragmentAccelerationStructure(sceneAccelerationStructure_.get(), gpu::MMBufferAccelerationStructure);
    if (tracedSurfaces_.buffer) encoder->setFragmentBuffer(tracedSurfaces_.buffer, tracedSurfaces_.offset, gpu::MMBufferTracedSurfaces);
    // Metal must be told that the pass also reads the structures the scene's one refers to.
    for (const MTL::AccelerationStructure* meshStructure : tracedMeshes_) {
        encoder->useResource(meshStructure, MTL::ResourceUsageRead, MTL::RenderStageFragment);
    }
}

MTL::Texture* Renderer::renderTracedAmbient(MTL::CommandBuffer* commandBuffer, const View& view, int width, int height, double time,
                                            const FrameAllocator::Allocation& frameUniforms, bool withBounce) {
    // Half the scene's size each way: a quarter of the pixels, and of the rays.
    int halfWidth = std::max(4, (width + 1) / 2), halfHeight = std::max(4, (height + 1) / 2);
    // Last frame's gathered pictures become "before"; this frame's are drawn into the others.
    std::swap(tracedLightGathered_, tracedLightGatheredBefore_);
    std::swap(tracedFrames_, tracedFramesBefore_);
    std::swap(tracedSurface_, tracedSurfaceBefore_);
    auto ensure = [&](NS::SharedPtr<MTL::Texture>& target, MTL::PixelFormat format) {
        if (target && int(target->width()) == halfWidth && int(target->height()) == halfHeight) return;
        target = device_.makeRenderTarget(format, halfWidth, halfHeight);
        tracedHasHistory_ = false;                  // a new size: nothing gathered before fits it
    };
    ensure(tracedLight_[0], kSceneFormat);
    ensure(tracedLight_[1], kSceneFormat);
    ensure(tracedSurface_, kSceneFormat);
    ensure(tracedSurfaceBefore_, kSceneFormat);
    ensure(tracedLightGathered_, kSceneFormat);
    ensure(tracedLightGatheredBefore_, kSceneFormat);
    ensure(tracedFrames_, MTL::PixelFormatRG16Float);
    ensure(tracedFramesBefore_, MTL::PixelFormatRG16Float);
    PipelineDescription description;
    description.vertexFunction = "vertex_fullscreen";
    description.fragmentFunction = "fragment_traced_ambient";
    description.colorFormat = kSceneFormat;
    description.secondColorFormat = kSceneFormat;
    MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
    PipelineDescription gatherDescription = description;
    gatherDescription.fragmentFunction = "fragment_traced_accumulate";
    gatherDescription.secondColorFormat = MTL::PixelFormatRG16Float;
    MTL::RenderPipelineState* gatherPipeline = shaders_.pipeline(gatherDescription);
    if (!pipeline || !gatherPipeline || !tracedLight_[0] || !tracedLight_[1] || !tracedSurface_ ||
        !tracedLightGathered_ || !tracedLightGatheredBefore_ || !tracedFrames_ || !tracedFramesBefore_ || !tracedSurfaceBefore_ ||
        !sceneAccelerationStructure_) {
        tracedHasHistory_ = false;
        return nullptr;
    }

    // The camera of the scene's own picture: the pass finds where each of its pixels is in
    // the world from the depth drawn for that picture.
    gpu::PassUniforms uniforms = makePassUniforms(view.view, view.projection, view.position, width, height, view.nearPlane, view.farPlane, time);
    FrameAllocator::Allocation passUniforms = allocator().copy(uniforms);

    // 1. The surface of each pixel, and the light around it.
    {
        auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
        MTL::RenderPassColorAttachmentDescriptor* light = pass->colorAttachments()->object(0);
        light->setTexture(tracedLight_[0].get());
        light->setLoadAction(MTL::LoadActionDontCare);          // every pixel is written
        light->setStoreAction(MTL::StoreActionStore);
        MTL::RenderPassColorAttachmentDescriptor* surface = pass->colorAttachments()->object(1);
        surface->setTexture(tracedSurface_.get());
        surface->setLoadAction(MTL::LoadActionDontCare);
        surface->setStoreAction(MTL::StoreActionStore);
        MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Traced ambient light");
        encoder->setRenderPipelineState(pipeline);
        bindTracedScene(encoder, passUniforms, frameUniforms);
        encoder->setFragmentTexture(prepassDepth_.get(), 0);
        encoder->setFragmentTexture(prepassNormal_.get(), 1);
        encoder->setFragmentTexture(environment_.irradianceMap(), gpu::MMTextureIrradiance);
        encoder->setFragmentSamplerState(device_.pointClampSampler(), 0);
        encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        encoder->endEncoding();
        stats.passes++;
    }
    // 2. Added to what was gathered at the same places before.
    {
        gpu::TracedHistoryUniforms history{};
        history.previousViewProjection = tracedViewProjectionBefore_;
        history.previousView = tracedViewBefore_;
        history.hasHistory = tracedHasHistory_ ? 1u : 0u;
        history.mostFrames = 20.0f;
        auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
        MTL::RenderPassColorAttachmentDescriptor* light = pass->colorAttachments()->object(0);
        light->setTexture(tracedLightGathered_.get());
        light->setLoadAction(MTL::LoadActionDontCare);
        light->setStoreAction(MTL::StoreActionStore);
        MTL::RenderPassColorAttachmentDescriptor* frames = pass->colorAttachments()->object(1);
        frames->setTexture(tracedFrames_.get());               // over how many frames each pixel was gathered
        frames->setLoadAction(MTL::LoadActionDontCare);
        frames->setStoreAction(MTL::StoreActionStore);
        MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Traced light gathered over frames");
        encoder->setRenderPipelineState(gatherPipeline);
        encoder->setFragmentBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
        encoder->setFragmentBytes(&history, sizeof(history), gpu::MMBufferFrame);
        encoder->setFragmentTexture(tracedLight_[0].get(), 0);
        encoder->setFragmentTexture(tracedSurface_.get(), 2);
        encoder->setFragmentTexture(tracedSurfaceBefore_.get(), 3);
        encoder->setFragmentTexture(tracedLightGatheredBefore_.get(), 4);
        encoder->setFragmentTexture(tracedFramesBefore_.get(), 5);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        encoder->endEncoding();
        stats.passes++;
    }
    tracedViewProjectionBefore_ = view.viewProjection;
    tracedViewBefore_ = view.view;
    tracedHasHistory_ = true;

    // 3. Smoothed across neighbors on the same surface: next to each other, two apart, and for the
    // bounced light, which has the most grain, four apart. What is smoothed is not kept for the
    // next frame: only what was gathered is, or the smoothing would pile up.
    drawFullscreen(commandBuffer, tracedLight_[1].get(), "fragment_traced_ambient_smooth", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                   tracedLightGathered_.get(), tracedSurface_.get(), nullptr);
    drawFullscreen(commandBuffer, tracedLight_[0].get(), "fragment_traced_ambient_smooth_wide", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                   tracedLight_[1].get(), tracedSurface_.get(), nullptr);
    if (!withBounce) return tracedLight_[0].get();
    drawFullscreen(commandBuffer, tracedLight_[1].get(), "fragment_traced_ambient_smooth_widest", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                   tracedLight_[0].get(), tracedSurface_.get(), nullptr);
    return tracedLight_[1].get();
}

// ---------------------------------------------------------------- clouds with depth

bool Renderer::renderClouds(MTL::CommandBuffer* commandBuffer, const View& view, int width, int height, double time,
                            const FrameAllocator::Allocation& frameUniforms) {
    // A picture of their own, a share of the scene's size: clouds are soft, and the sky smooths it up.
    // Even, so that it is two by two of the walked one.
    float share = std::clamp(settings.sky.cloudResolution, 0.2f, 1.0f);
    int cloudWidth = std::max(8, int(std::lround(float(width) * share))), cloudHeight = std::max(8, int(std::lround(float(height) * share)));
    cloudWidth += cloudWidth & 1, cloudHeight += cloudHeight & 1;
    const int freshWidth = cloudWidth / 2, freshHeight = cloudHeight / 2;
    // This frame's picture is drawn over the older of the two: the last frame's is read.
    std::swap(cloudColor_, cloudColorBefore_);
    if (!cloudColor_ || int(cloudColor_->width()) != cloudWidth || int(cloudColor_->height()) != cloudHeight) {
        cloudColor_ = device_.makeRenderTarget(kSceneFormat, cloudWidth, cloudHeight);
        cloudHasHistory_ = false;
    }
    if (!cloudColorBefore_ || int(cloudColorBefore_->width()) != cloudWidth || int(cloudColorBefore_->height()) != cloudHeight) {
        cloudColorBefore_ = device_.makeRenderTarget(kSceneFormat, cloudWidth, cloudHeight);
        cloudHasHistory_ = false;
    }
    if (!cloudFresh_ || int(cloudFresh_->width()) != freshWidth || int(cloudFresh_->height()) != freshHeight) {
        cloudFresh_ = device_.makeRenderTarget(kSceneFormat, freshWidth, freshHeight);
        cloudHasHistory_ = false;
    }
    PipelineDescription description;
    description.vertexFunction = "vertex_sky";
    description.fragmentFunction = "fragment_clouds";
    description.colorFormat = kSceneFormat;
    MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
    if (!pipeline || !cloudColor_ || !cloudColorBefore_ || !cloudFresh_) return false;

    // Which pixel of each two by two is walked this frame: all four in four frames, crosswise.
    static const int kTurns[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
    const int* turn = kTurns[cloudTurn_++ % 4];
    // The walked picture is the same camera with its pixels moved onto those of the larger one:
    // half a pixel of the walked picture, one way or the other.
    Mat4 nudged = identity();
    nudged.columns[3] = Vec4{(float(turn[0]) - 0.5f) / float(freshWidth), -(float(turn[1]) - 0.5f) / float(freshHeight), 0.0f, 1.0f};
    FrameAllocator::Allocation passUniforms = allocator().copy(
        makePassUniforms(view.view, nudged * view.projection, view.position, freshWidth, freshHeight, view.nearPlane, view.farPlane, time));
    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(cloudFresh_.get());
    color->setLoadAction(MTL::LoadActionDontCare);              // every pixel is written
    color->setStoreAction(MTL::StoreActionStore);
    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Clouds");
    encoder->setRenderPipelineState(pipeline);
    encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(frameUniforms.buffer, frameUniforms.offset, gpu::MMBufferFrame);
    encoder->setFragmentTexture(cloudShape_.get(), 0);
    encoder->setFragmentTexture(cloudDetail_.get(), 1);
    // Where something opaque hides the sky no cloud is walked: the depth drawn before the scene says where.
    encoder->setFragmentTexture(wantsDepth() && prepassDepth_ ? prepassDepth_.get() : nullptr, 2);
    encoder->setFragmentSamplerState(device_.noiseSampler(), 0);
    encoder->setFragmentSamplerState(device_.pointClampSampler(), 1);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
    stats.passes++;

    // The whole picture: the walked pixels, and the rest from the last frame's, where each line
    // of sight was seen then. (Clouds are far: only which way the camera looks moves them.)
    Mat4 viewProjection = view.projection * view.view;
    gpu::CloudResolveUniforms resolve{};
    resolve.inverseViewProjection = simd::inverse(viewProjection);
    resolve.previousViewProjection = cloudViewProjectionBefore_;
    resolve.camera = Vec4{view.position.x, view.position.y, view.position.z, 1.0f};
    resolve.offset = Vec2{float(turn[0]), float(turn[1])};
    resolve.hasHistory = cloudHasHistory_ ? 1.0f : 0.0f;
    drawFullscreen(commandBuffer, cloudColor_.get(), "fragment_clouds_resolve", BlendMode::Opaque, false, &resolve, sizeof(resolve),
                   cloudFresh_.get(), cloudColorBefore_.get(), nullptr);
    cloudViewProjectionBefore_ = viewProjection;
    cloudHasHistory_ = true;
    return true;
}

// ---------------------------------------------------------------- 4. the main pass

void Renderer::renderScene(MTL::CommandBuffer* commandBuffer, Scene& scene, const std::vector<Batch>& opaque,
                           const std::vector<Batch>& transparent, const View& view,
                           const FrameAllocator::Allocation& passUniforms, const FrameAllocator::Allocation& frameUniforms,
                           const FrameAllocator::Allocation& lights) {
    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setLoadAction(MTL::LoadActionClear);
    color->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));
    if (targetSamples_ > 1) {
        // Draw into the multisampled texture; at the end of the pass the GPU averages
        // the samples of each pixel into the plain texture ("resolve"). Only that is kept.
        color->setTexture(sceneColorMultisample_.get());
        color->setResolveTexture(sceneColor_.get());
        color->setStoreAction(MTL::StoreActionMultisampleResolve);
    } else {
        color->setTexture(sceneColor_.get());
        color->setStoreAction(MTL::StoreActionStore);
    }
    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(sceneDepth_.get());
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1.0);
    depth->setStoreAction(MTL::StoreActionDontCare);                   // nobody reads it after this pass

    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Scene");
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);

    // What every draw of this pass shares is bound once.
    encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(frameUniforms.buffer, frameUniforms.offset, gpu::MMBufferFrame);
    encoder->setFragmentBuffer(lights.buffer, lights.offset, gpu::MMBufferLights);
    encoder->setFragmentTexture(shadowMap_.get(), gpu::MMTextureShadowMap);
    encoder->setFragmentTexture(rainMapDrawn_ ? rainMap_.get() : nullptr, gpu::MMTextureRainMap);
    // Water reads which way the bottom faces under it (for the sun's lines on it), and what floats on it.
    encoder->setFragmentTexture(wantsDepth() && prepassNormal_ ? prepassNormal_.get() : nullptr, gpu::MMTextureSceneNormal);
    FrameAllocator::Allocation wakes = allocator().copy(waterWakes);
    encoder->setFragmentBuffer(wakes.buffer, wakes.offset, gpu::MMBufferWakes);
    encoder->setFragmentTexture(environment_.irradianceMap(), gpu::MMTextureIrradiance);
    encoder->setFragmentTexture(environment_.prefilteredMap(), gpu::MMTexturePrefiltered);
    encoder->setFragmentTexture(environment_.brdfLUT(), gpu::MMTextureBRDF);
    // The screen's own ambient occlusion, unless the light around things was traced for this picture.
    encoder->setFragmentTexture(settings.ambientOcclusionEnabled && !tracedAmbientDrawn_ && occlusion_ ? occlusion_.get() : white_->handle(),
                                gpu::MMTextureAmbientOcclusion);
    encoder->setFragmentTexture(tracedLightDrawn_, gpu::MMTextureTracedLight);
    encoder->setFragmentTexture(tracedLightDrawn_ ? tracedSurface_.get() : nullptr, gpu::MMTextureTracedSurface);
    // What is opaque, how far: water reads it for how deep it is, and its foam round what stands in it.
    encoder->setFragmentTexture(wantsDepth() && prepassDepth_ ? prepassDepth_.get() : nullptr, gpu::MMTextureSceneDepth);
    // The clouds with depth, if this picture has them: the sky puts them in front of itself.
    encoder->setFragmentTexture(cloudsDrawn_ && cloudColor_ ? cloudColor_.get() : nullptr, gpu::MMTextureClouds);
    encoder->setFragmentSamplerState(device_.materialSampler(), gpu::MMSamplerMaterial);
    encoder->setFragmentSamplerState(device_.shadowSampler(), gpu::MMSamplerShadow);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
    if (tracesRaysThisFrame_) {
        // Only the "_traced" fragment functions read it, and they are only used in frames like this one.
        encoder->setFragmentAccelerationStructure(sceneAccelerationStructure_.get(), gpu::MMBufferAccelerationStructure);
        if (tracedSurfaces_.buffer) encoder->setFragmentBuffer(tracedSurfaces_.buffer, tracedSurfaces_.offset, gpu::MMBufferTracedSurfaces);
        // Metal must be told that the pass also reads the structures the scene's one refers to.
        for (const MTL::AccelerationStructure* meshStructure : tracedMeshes_) {
            encoder->useResource(meshStructure, MTL::ResourceUsageRead, MTL::RenderStageFragment);
        }
    }

    // Opaque objects, writing depth.
    encoder->pushDebugGroup(label("Opaque"));
    encoder->setDepthStencilState(device_.depthWrite());
    drawBatches(encoder, opaque, MeshPass::Forward, kSceneFormat, targetSamples_);
    encoder->popDebugGroup();
    if (hasDecals_ && prepassDepth_) drawDecals(encoder, scene, view);

    // The sky, only where nothing was drawn. From here on nothing writes depth.
    encoder->setDepthStencilState(device_.depthReadOnly());
    PipelineDescription sky;
    sky.vertexFunction = "vertex_sky";
    sky.fragmentFunction = "fragment_sky";
    sky.colorFormat = kSceneFormat;
    sky.depthFormat = kDepthFormat;
    sky.sampleCount = targetSamples_;
    if (MTL::RenderPipelineState* pipeline = shaders_.pipeline(sky)) {
        encoder->setRenderPipelineState(pipeline);
        encoder->setCullMode(MTL::CullModeNone);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        stats.passes++;
    }

    // Transparent objects, already sorted from far to near.
    encoder->pushDebugGroup(label("Transparent"));
    drawBatches(encoder, transparent, MeshPass::Forward, kSceneFormat, targetSamples_);
    encoder->popDebugGroup();

    drawParticles(encoder, scene, view);
    drawDebugLines(encoder);
    encoder->endEncoding();
}

void Renderer::drawParticles(MTL::RenderCommandEncoder* encoder, Scene& scene, const View& view) {
    std::vector<gpu::ParticleInstance> instances;
    // Soft particles read how far the opaque scene is: asked for next frame if some emitter wants it.
    const bool hasDepth = wantsDepth() && prepassDepth_;
    bool wantsSoftness = false;
    scene.each<ParticleEmitter, Transform>([&](Entity, ParticleEmitter& emitter, Transform& transform) {
        if (emitter.particles.empty() || !transform.isActiveInHierarchy) return;
        wantsSoftness |= emitter.softness > 0;
        // Its picture, read once.
        MTL::Texture* picture = nullptr;
        if (!emitter.texture.empty() && loadPicture) {
            auto found = particlePictures_.find(emitter.texture);
            if (found == particlePictures_.end()) found = particlePictures_.emplace(emitter.texture, loadPicture(emitter.texture, TextureFormat::RGBA8_sRGB)).first;
            if (found->second) picture = found->second->handle();
        }
        int columns = std::max(emitter.sheetColumns, 1), rows = std::max(emitter.sheetRows, 1);
        int frames = columns * rows;
        const bool isStretched = emitter.renderMode == ParticleEmitter::RenderMode::Stretched;
        const bool isLocal = emitter.space == ParticleEmitter::Space::Local;
        const float mode = float(int(emitter.renderMode));
        const Vec4 shading{saturate(emitter.lit), hasDepth ? std::max(emitter.softness, 0.0f) : 0.0f, mode, emitter.isRing ? 1.0f : 0.0f};
        instances.clear();
        for (const Particle& particle : emitter.particles) {
            float t = saturate(particle.age / particle.lifetime);
            float size = particleSize(emitter, t) * particle.sizeScale;
            Vec4 color = particleColor(emitter, t);
            color = Vec4{color.x * particle.shade, color.y * particle.shade, color.z * particle.shade, color.w};
            // The frame of its sheet: so many a second, or the whole sheet over its life.
            float frame = emitter.sheetFps > 0 ? std::fmod(std::floor(particle.age * emitter.sheetFps + particle.frame), float(frames))
                                               : std::fmod(std::min(std::floor(t * float(frames)), float(frames - 1)) + particle.frame, float(frames));
            Vec4 look{particle.rotation, frame, picture ? float(columns) : 0.0f, float(rows)};
            Vec3 position = isLocal ? transformPoint(transform.world, particle.position) : particle.position;
            Vec3 velocity = isLocal ? transformDirection(transform.world, particle.velocity) : particle.velocity;
            instances.push_back(gpu::ParticleInstance{vec4(position, size), color, vec4(velocity, isStretched ? std::max(emitter.stretch, 0.001f) : 0.0f), look, shading});
        }
        if (!emitter.isAdditive) {
            // Ordinary blending depends on the order: far particles first.
            std::sort(instances.begin(), instances.end(), [&](const gpu::ParticleInstance& a, const gpu::ParticleInstance& b) {
                return simd::distance_squared(xyz(a.positionAndSize), view.position) >
                       simd::distance_squared(xyz(b.positionAndSize), view.position);
            });
        }

        PipelineDescription description;
        description.vertexFunction = "vertex_particle";
        description.fragmentFunction = "fragment_particle";
        description.colorFormat = kSceneFormat;
        description.depthFormat = kDepthFormat;
        description.sampleCount = targetSamples_;
        description.blend = emitter.isAdditive ? BlendMode::Additive : BlendMode::AlphaBlend;
        MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
        if (!pipeline) return;

        FrameAllocator::Allocation buffer = allocator().copyArray(instances.data(), instances.size());
        encoder->setRenderPipelineState(pipeline);
        encoder->setCullMode(MTL::CullModeNone);
        encoder->setVertexBuffer(buffer.buffer, buffer.offset, gpu::MMBufferInstances);
        encoder->setFragmentTexture(picture ? picture : white_->handle(), 0);
        encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
        // One instance per particle; the four vertices of the square come from the vertex index.
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4), NS::UInteger(instances.size()));
        stats.drawCalls++;
    });
    particlesWantDepth_ = wantsSoftness;
}

void Renderer::drawDebugLines(MTL::RenderCommandEncoder* encoder) {
    const std::vector<gpu::LineVertex>& vertices = debug.vertices();
    if (vertices.empty() || !drawsDebugLines_) return;

    PipelineDescription description;
    description.vertexFunction = "vertex_line";
    description.fragmentFunction = "fragment_line";
    description.colorFormat = kSceneFormat;
    description.depthFormat = kDepthFormat;
    description.sampleCount = targetSamples_;
    description.blend = BlendMode::AlphaBlend;
    MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
    if (!pipeline) return;

    FrameAllocator::Allocation buffer = allocator().copyArray(vertices.data(), vertices.size());
    encoder->setRenderPipelineState(pipeline);
    encoder->setVertexBuffer(buffer.buffer, buffer.offset, gpu::MMBufferVertices);
    encoder->drawPrimitives(MTL::PrimitiveTypeLine, NS::UInteger(0), NS::UInteger(vertices.size()));
    stats.overlayDraws++;
}

// ---------------------------------------------------------------- 5. bloom

void Renderer::renderBloom(MTL::CommandBuffer* commandBuffer) {
    if (!hasBloom()) return;

    gpu::PostUniforms uniforms{};
    uniforms.bloomThreshold = settings.bloomThreshold;
    uniforms.bloomKnee = settings.bloomKnee;
    uniforms.filterRadius = 1.0f;

    // The bright parts of the scene, at half size.
    uniforms.sourceTexelSize = texelSize(sceneColor_.get());
    drawFullscreen(commandBuffer, bloom_[0].get(), "fragment_bloom_prefilter", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                   sceneColor_.get(), nullptr, nullptr);

    // Down the chain: each level is a blurred, half-size copy of the one before.
    for (size_t i = 1; i < bloom_.size(); i++) {
        uniforms.sourceTexelSize = texelSize(bloom_[i - 1].get());
        drawFullscreen(commandBuffer, bloom_[i].get(), "fragment_bloom_downsample", BlendMode::Opaque, false, &uniforms,
                       sizeof(uniforms), bloom_[i - 1].get(), nullptr, nullptr);
    }
    // And back up: each level is added onto the larger one. Level 0 ends up holding the sum.
    for (size_t i = bloom_.size() - 1; i > 0; i--) {
        uniforms.sourceTexelSize = texelSize(bloom_[i].get());
        drawFullscreen(commandBuffer, bloom_[i - 1].get(), "fragment_bloom_upsample", BlendMode::Additive, true, &uniforms,
                       sizeof(uniforms), bloom_[i].get(), nullptr, nullptr);
    }
}

// ---------------------------------------------------------------- decals

void Renderer::drawDecals(MTL::RenderCommandEncoder* encoder, Scene& scene, const View& view) {
    (void)view;
    PipelineDescription description;
    description.vertexFunction = "vertex_decal";
    description.fragmentFunction = "fragment_decal";
    description.colorFormat = kSceneFormat;
    description.depthFormat = kDepthFormat;
    description.sampleCount = targetSamples_;
    description.blend = BlendMode::AlphaBlend;
    PipelineDescription darkening = description;
    darkening.blend = BlendMode::Multiply;
    encoder->pushDebugGroup(label("Decals"));
    // Their boxes' far sides, with no depth test: drawn wherever the box is on screen, even from inside it.
    encoder->setDepthStencilState(device_.depthOff());
    encoder->setCullMode(MTL::CullModeFront);
    encoder->setFragmentTexture(prepassDepth_.get(), 0);
    scene.each<Decal, Transform>([&](Entity, Decal& decal, Transform& transform) {
        if (!transform.isActiveInHierarchy || decal.picture.empty() || decal.opacity <= 0) return;
        if (decal.loadedName != decal.picture) {
            decal.loadedName = decal.picture;
            decal.loaded = loadPicture ? loadPicture(decal.picture, TextureFormat::RGBA8_sRGB) : nullptr;
        }
        MTL::RenderPipelineState* pipeline = shaders_.pipeline(decal.darkens ? darkening : description);
        if (!pipeline) return;
        gpu::DecalUniforms uniforms{};
        uniforms.model = transform.world;
        uniforms.inverseModel = simd::inverse(transform.world);
        uniforms.color = Vec4{decal.color.x, decal.color.y, decal.color.z, std::clamp(decal.opacity, 0.0f, 1.0f)};
        uniforms.darkens = decal.darkens ? 1.0f : 0.0f;
        encoder->setRenderPipelineState(pipeline);
        encoder->setVertexBytes(&uniforms, sizeof(uniforms), gpu::MMBufferMaterial);
        encoder->setFragmentBytes(&uniforms, sizeof(uniforms), gpu::MMBufferMaterial);
        encoder->setFragmentTexture((decal.loaded ? decal.loaded : white_)->handle(), 1);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(36));
        stats.drawCalls++;
    });
    encoder->setCullMode(MTL::CullModeBack);
    encoder->setDepthStencilState(device_.depthWrite());
    encoder->popDebugGroup();
}

// ---------------------------------------------------------------- effects before the bloom

void Renderer::renderEffects(MTL::CommandBuffer* commandBuffer, const View& view, float deltaTime) {
    gpu::EffectUniforms uniforms{};
    uniforms.inverseViewProjection = simd::inverse(view.viewProjection);
    uniforms.previousViewProjection = hasPreviousView_ ? previousViewProjection_ : view.viewProjection;
    uniforms.texelSize = texelSize(sceneColor_.get());
    uniforms.focusDistance = std::max(settings.focusDistance, 0.01f);
    uniforms.focusRange = std::max(settings.focusRange, 0.0f);
    uniforms.blurSize = std::clamp(settings.focusBlur, 0.0f, 32.0f);
    uniforms.motionBlur = std::clamp(settings.motionBlurAmount, 0.0f, 1.0f);
    uniforms.deltaTime = deltaTime;
    uniforms.adaptSpeed = std::max(settings.exposureAdaptSpeed, 0.01f);
    uniforms.darkestExposure = std::max(settings.darkestExposure, 0.001f);
    uniforms.brightestExposure = std::max(settings.brightestExposure, uniforms.darkestExposure);
    uniforms.pictureHeight = float(sceneColor_->height());
    previousViewProjection_ = view.viewProjection;
    bool hadPrevious = hasPreviousView_;
    hasPreviousView_ = true;

    // Each effect reads the scene's picture and writes the other one; then they change places.
    auto apply = [&](const char* function) {
        drawFullscreen(commandBuffer, postColor_.get(), function, BlendMode::Opaque, false, &uniforms, sizeof(uniforms), sceneColor_.get(),
                       prepassDepth_.get(), device_.pointClampSampler());
        std::swap(sceneColor_, postColor_);
    };
    if (prepassDepth_ && postColor_) {
        if (settings.motionBlur && hadPrevious && uniforms.motionBlur > 0) apply("fragment_motion_blur");
        if (settings.depthOfField && uniforms.blurSize > 0) apply("fragment_depth_of_field");
    }

    // How bright the scene is, for the exposure to adapt to: its brightness in a small picture,
    // averaged down to one pixel, then the exposure eased toward what that pixel asks.
    if (settings.autoExposure && luminance_ && adapted_[0] && adapted_[1]) {
        drawFullscreen(commandBuffer, luminance_.get(), "fragment_luminance", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                       sceneColor_.get(), nullptr, nullptr);
        MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
        blit->generateMipmaps(luminance_.get());
        blit->endEncoding();
        int next = 1 - adaptIndex_;
        if (!hadPrevious) uniforms.deltaTime = 1000;                // the first picture: at once
        drawFullscreen(commandBuffer, adapted_[next].get(), "fragment_adapt_exposure", BlendMode::Opaque, false, &uniforms, sizeof(uniforms),
                       luminance_.get(), adapted_[adaptIndex_].get(), nullptr);
        adaptIndex_ = next;
    }
}

// ---------------------------------------------------------------- 6. the final pass

void Renderer::renderFinal(MTL::CommandBuffer* commandBuffer, const Canvas& canvas, const RenderTarget& target, bool withOverlay) {
    bool hasBloom = this->hasBloom();
    gpu::PostUniforms uniforms{};
    uniforms.exposure = settings.exposure;
    uniforms.bloomIntensity = hasBloom ? settings.bloomIntensity : 0.0f;
    uniforms.vignette = settings.vignette;
    uniforms.toneMapper = uint32_t(settings.toneMapper);
    uniforms.lensDistortion = settings.lensDistortion;
    uniforms.chromaticAberration = settings.chromaticAberration;
    uniforms.grain = settings.filmGrain;
    uniforms.interference = settings.interference;
    uniforms.time = float(time_);
    // The color grading table, read again when another is named.
    if (settings.colorGrading != lutName_) {
        lutName_ = settings.colorGrading;
        lut_ = !lutName_.empty() && loadPicture ? loadPicture(lutName_, TextureFormat::RGBA8) : nullptr;
    }
    MTL::Texture* lut = lut_ ? lut_->handle() : nullptr;
    if (lut && lut->height() >= 2 && lut->width() == lut->height() * lut->height()) {
        uniforms.lutSize = float(lut->height());
        uniforms.lutAmount = std::clamp(settings.colorGradingAmount, 0.0f, 1.0f);
    } else {
        lut = nullptr;
    }
    MTL::Texture* adapted = settings.autoExposure && adapted_[adaptIndex_] ? adapted_[adaptIndex_].get() : nullptr;
    uniforms.autoExposure = adapted ? 1u : 0u;
    uniforms.exposureCompensation = settings.exposureCompensation;
    uniforms.contrast = settings.contrast;
    uniforms.saturation = settings.saturation;
    uniforms.temperature = settings.temperature;
    uniforms.tint = settings.tint;
    uniforms.sharpen = settings.sharpen;
    uniforms.lensFlare = settings.lensFlare;
    // The dirt on the glass, read again when another is named.
    if (settings.lensDirt != lensDirtName_) {
        lensDirtName_ = settings.lensDirt;
        lensDirt_ = !lensDirtName_.empty() && loadPicture ? loadPicture(lensDirtName_, TextureFormat::RGBA8_sRGB) : nullptr;
    }
    MTL::Texture* dirt = lensDirt_ ? lensDirt_->handle() : nullptr;
    uniforms.lensDirt = dirt ? std::max(settings.lensDirtAmount, 0.0f) : 0.0f;
    upscaleDirt_ = dirt;
    // Where the sun is on the picture: its direction, as a point infinitely far.
    Vec4 sun = flareViewProjection_ * Vec4{-flareSunDirection_.x, -flareSunDirection_.y, -flareSunDirection_.z, 0.0f};
    uniforms.sunInView = sun.w > 1e-4f && flareSunColor_.x + flareSunColor_.y + flareSunColor_.z > 0.001f ? 1.0f : 0.0f;
    if (uniforms.sunInView > 0) uniforms.sunScreen = Vec2{sun.x / sun.w * 0.5f + 0.5f, 0.5f - sun.y / sun.w * 0.5f};
    uniforms.sunColor = flareSunColor_;

    // Drawn smaller (renderScale): tone mapped at that size, then made the target's size by
    // MetalFX, which keeps edges and detail sharp where plain stretching blurs them.
    Rect area = sceneRect(target.texture);
    MTL::Texture* upscaled = nullptr;
    upscaleLUT_ = lut;
    upscaleAdapted_ = adapted;
    if (settings.metalFXUpscaling && sceneColor_ && int(sceneColor_->width()) < area.width) {
        upscaled = upscaleWithMetalFX(commandBuffer, &uniforms, sizeof(uniforms), hasBloom, area.width, area.height);
    }

    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(target.texture);
    bool coversTarget = area.width == int(target.texture->width()) && area.height == int(target.texture->height());
    // When the scene covers the target the first draw writes every pixel; otherwise what
    // is around it must be cleared.
    color->setLoadAction(coversTarget ? MTL::LoadActionDontCare : MTL::LoadActionClear);
    color->setClearColor(MTL::ClearColor::Make(double(backgroundColor.x), double(backgroundColor.y), double(backgroundColor.z),
                                               double(backgroundColor.w)));
    color->setStoreAction(MTL::StoreActionStore);

    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Tone mapping and canvas");
    MTL::Viewport fullViewport{0.0, 0.0, double(target.texture->width()), double(target.texture->height()), 0.0, 1.0};
    if (!coversTarget) {
        encoder->setViewport(MTL::Viewport{double(area.x), double(area.y), double(area.width), double(area.height), 0.0, 1.0});
        encoder->setScissorRect(MTL::ScissorRect{NS::UInteger(area.x), NS::UInteger(area.y), NS::UInteger(area.width), NS::UInteger(area.height)});
    }

    // From HDR to what a screen can show. The scene texture may be smaller or larger
    // than the target (renderScale); the sampler stretches it.
    PipelineDescription description;
    description.vertexFunction = "vertex_fullscreen";
    description.fragmentFunction = upscaled ? "fragment_copy" : "fragment_tonemap";
    description.colorFormat = target.texture->pixelFormat();
    if (MTL::RenderPipelineState* pipeline = shaders_.pipeline(description)) {
        encoder->setRenderPipelineState(pipeline);
        encoder->setFragmentBytes(&uniforms, sizeof(uniforms), gpu::MMBufferPass);
        encoder->setFragmentTexture(upscaled ? upscaled : sceneColor_.get(), 0);
        encoder->setFragmentTexture(hasBloom ? bloom_[0].get() : white_->handle(), 1);
        encoder->setFragmentTexture(lut ? lut : white_->handle(), 2);
        encoder->setFragmentTexture(adapted ? adapted : white_->handle(), 3);
        encoder->setFragmentTexture(dirt ? dirt : white_->handle(), 4);
        encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        stats.passes++;
    }

    if (!coversTarget) {
        encoder->setViewport(fullViewport);
        encoder->setScissorRect(MTL::ScissorRect{0, 0, target.texture->width(), target.texture->height()});
    }

    // The 2D canvas goes on top, after tone mapping: interface colors must not be tone mapped.
    drawCanvas(encoder, canvas, target.texture);
    if (overlay && withOverlay) overlay(commandBuffer, encoder);
    encoder->endEncoding();
}

MTL::Texture* Renderer::upscaleWithMetalFX(MTL::CommandBuffer* commandBuffer, const void* postUniforms, size_t uniformsSize, bool hasBloom,
                                           int width, int height) {
    static const bool isSupported = MTLFX::SpatialScalerDescriptor::supportsDevice(device_.device());
    if (!isSupported || width < 1 || height < 1) return nullptr;
    Upscaler& up = upscaler_;
    int inputWidth = int(sceneColor_->width()), inputHeight = int(sceneColor_->height());
    // Linear colors in half floats, read and written by MetalFX; the target's sRGB format adds the gamma after.
    const MTL::PixelFormat format = MTL::PixelFormatRGBA16Float;
    if (!up.scaler || up.inputWidth != inputWidth || up.inputHeight != inputHeight || up.outputWidth != width || up.outputHeight != height) {
        auto descriptor = NS::TransferPtr(MTLFX::SpatialScalerDescriptor::alloc()->init());
        descriptor->setInputWidth(NS::UInteger(inputWidth));
        descriptor->setInputHeight(NS::UInteger(inputHeight));
        descriptor->setOutputWidth(NS::UInteger(width));
        descriptor->setOutputHeight(NS::UInteger(height));
        descriptor->setColorTextureFormat(format);
        descriptor->setOutputTextureFormat(format);
        descriptor->setColorProcessingMode(MTLFX::SpatialScalerColorProcessingModeLinear);
        up = Upscaler{};
        up.scaler = NS::TransferPtr(descriptor->newSpatialScaler(device_.device()));
        if (!up.scaler) return nullptr;
        auto makeTexture = [&](int w, int h, MTL::TextureUsage usage) {
            auto texture = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
            texture->setTextureType(MTL::TextureType2D);
            texture->setPixelFormat(format);
            texture->setWidth(NS::UInteger(w));
            texture->setHeight(NS::UInteger(h));
            texture->setUsage(usage | MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
            texture->setStorageMode(MTL::StorageModePrivate);
            return NS::TransferPtr(device_.device()->newTexture(texture.get()));
        };
        up.input = makeTexture(inputWidth, inputHeight, up.scaler->colorTextureUsage());
        up.output = makeTexture(width, height, up.scaler->outputTextureUsage());
        up.inputWidth = inputWidth, up.inputHeight = inputHeight, up.outputWidth = width, up.outputHeight = height;
        if (!up.input || !up.output) {
            up = Upscaler{};
            return nullptr;
        }
    }
    // Tone mapped at the size it was drawn.
    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(up.input.get());
    color->setLoadAction(MTL::LoadActionDontCare);
    color->setStoreAction(MTL::StoreActionStore);
    PipelineDescription description;
    description.vertexFunction = "vertex_fullscreen";
    description.fragmentFunction = "fragment_tonemap";
    description.colorFormat = format;
    MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
    if (!pipeline) return nullptr;
    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Tone mapping, before MetalFX");
    encoder->setRenderPipelineState(pipeline);
    encoder->setFragmentBytes(postUniforms, uniformsSize, gpu::MMBufferPass);
    encoder->setFragmentTexture(sceneColor_.get(), 0);
    encoder->setFragmentTexture(hasBloom ? bloom_[0].get() : white_->handle(), 1);
    encoder->setFragmentTexture(upscaleLUT_ ? upscaleLUT_ : white_->handle(), 2);
    encoder->setFragmentTexture(upscaleAdapted_ ? upscaleAdapted_ : white_->handle(), 3);
    encoder->setFragmentTexture(upscaleDirt_ ? upscaleDirt_ : white_->handle(), 4);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
    stats.passes++;
    // Then made the target's size.
    up.scaler->setColorTexture(up.input.get());
    up.scaler->setOutputTexture(up.output.get());
    up.scaler->setInputContentWidth(NS::UInteger(inputWidth));
    up.scaler->setInputContentHeight(NS::UInteger(inputHeight));
    up.scaler->encodeToCommandBuffer(commandBuffer);
    return up.output.get();
}

void Renderer::renderInterface(MTL::CommandBuffer* commandBuffer, const Canvas& canvas, const RenderTarget& target) {
    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(target.texture);
    color->setLoadAction(MTL::LoadActionClear);
    color->setClearColor(MTL::ClearColor::Make(double(backgroundColor.x), double(backgroundColor.y), double(backgroundColor.z),
                                               double(backgroundColor.w)));
    color->setStoreAction(MTL::StoreActionStore);
    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), "Interface");
    drawCanvas(encoder, canvas, target.texture);
    if (overlay) overlay(commandBuffer, encoder);
    encoder->endEncoding();
}

void Renderer::drawCanvas(MTL::RenderCommandEncoder* encoder, const Canvas& canvas, MTL::Texture* target) {
    const std::vector<gpu::CanvasVertex>& vertices = canvas.vertices();
    Vec2 size = canvas.size();
    if (vertices.empty() || size.x <= 0 || size.y <= 0) return;

    gpu::CanvasUniforms uniforms{};
    uniforms.projection = orthographic(0, size.x, size.y, 0, -1, 1);   // points, y down
    uniforms.distanceRange = canvas.fontDistanceRange();

    FrameAllocator::Allocation buffer = allocator().copyArray(vertices.data(), vertices.size());
    encoder->setVertexBuffer(buffer.buffer, buffer.offset, gpu::MMBufferVertices);
    encoder->setVertexBytes(&uniforms, sizeof(uniforms), gpu::MMBufferPass);
    encoder->setFragmentBytes(&uniforms, sizeof(uniforms), gpu::MMBufferPass);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), 0);

    // Clips are given in points; the scissor rectangle that enforces them is in pixels and
    // must stay inside the target, or Metal stops the program.
    int width = int(target->width()), height = int(target->height());
    Vec2 pixelsPerPoint{float(width) / size.x, float(height) / size.y};

    for (const Canvas::Batch& batch : canvas.batches()) {
        if (batch.vertexCount == 0) continue;
        MTL::ScissorRect scissor{0, 0, NS::UInteger(width), NS::UInteger(height)};
        if (batch.isClipped) {
            int x0 = std::clamp(int(std::lround(batch.clipMin.x * pixelsPerPoint.x)), 0, width);
            int y0 = std::clamp(int(std::lround(batch.clipMin.y * pixelsPerPoint.y)), 0, height);
            int x1 = std::clamp(int(std::lround(batch.clipMax.x * pixelsPerPoint.x)), 0, width);
            int y1 = std::clamp(int(std::lround(batch.clipMax.y * pixelsPerPoint.y)), 0, height);
            if (x1 <= x0 || y1 <= y0) continue;
            scissor = MTL::ScissorRect{NS::UInteger(x0), NS::UInteger(y0), NS::UInteger(x1 - x0), NS::UInteger(y1 - y0)};
        }
        encoder->setScissorRect(scissor);
        PipelineDescription description;
        description.vertexFunction = "vertex_canvas";
        description.fragmentFunction = batch.isText ? "fragment_canvas_text" : "fragment_canvas";
        description.colorFormat = target->pixelFormat();
        description.blend = BlendMode::AlphaBlend;
        MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
        if (!pipeline) continue;
        encoder->setRenderPipelineState(pipeline);
        encoder->setFragmentTexture(batch.texture ? batch.texture->handle() : white_->handle(), 0);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(batch.firstVertex), NS::UInteger(batch.vertexCount));
        stats.overlayDraws++;
    }
}

// ---------------------------------------------------------------- helper

void Renderer::drawFullscreen(MTL::CommandBuffer* commandBuffer, MTL::Texture* target, const char* fragmentFunction,
                              BlendMode blend, bool keepContents, const void* uniforms, size_t uniformsSize,
                              MTL::Texture* source0, MTL::Texture* source1, MTL::SamplerState* sampler0, MTL::Texture* source2) {
    PipelineDescription description;
    description.vertexFunction = "vertex_fullscreen";
    description.fragmentFunction = fragmentFunction;
    description.colorFormat = target->pixelFormat();
    description.blend = blend;
    MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
    if (!pipeline) return;

    auto pass = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    color->setTexture(target);
    color->setLoadAction(keepContents ? MTL::LoadActionLoad : MTL::LoadActionDontCare);
    color->setStoreAction(MTL::StoreActionStore);

    MTL::RenderCommandEncoder* encoder = encoderFor(commandBuffer, pass.get(), fragmentFunction);
    encoder->setRenderPipelineState(pipeline);
    if (uniforms) encoder->setFragmentBytes(uniforms, NS::UInteger(uniformsSize), gpu::MMBufferPass);
    if (source0) encoder->setFragmentTexture(source0, 0);
    if (source1) encoder->setFragmentTexture(source1, 1);
    if (source2) encoder->setFragmentTexture(source2, 2);
    if (sampler0) encoder->setFragmentSamplerState(sampler0, 0);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
    stats.passes++;
}

}  // namespace molten
