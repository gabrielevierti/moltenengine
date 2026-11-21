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
    uniforms.water = material.isWater ? std::max(material.waveScale, 0.01f) : 0.0f;
    uniforms.waveHeight = material.waveHeight;
    uniforms.sway = std::max(material.sway, 0.0f);
    // Water reads the sea's waves where other materials read their normal map.
    bool readsOcean = material.isWater && oceanTexture_ && oceanPatch_ > 0;
    if (material.isWater) waterSeen_ = true;
    if (readsOcean) {
        uniforms.oceanScale = 1.0f / oceanPatch_;
        uniforms.oceanOffset = -oceanOrigin_;
    }
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
    encoder->setFragmentTexture(readsOcean ? oceanTexture_ : textureOr(material.normalTexture, flatNormal_), gpu::MMTextureNormal);
    encoder->setFragmentTexture(textureOr(material.metallicRoughnessTexture, white_), gpu::MMTextureMetallicRoughness);
    encoder->setFragmentTexture(textureOr(material.occlusionTexture, white_), gpu::MMTextureOcclusion);
    encoder->setFragmentTexture(textureOr(material.emissiveTexture, white_), gpu::MMTextureEmissive);
    // Pixel art is drawn in sharp pixels: its picture says so (its import settings).
    bool isPixelated = material.baseColorTexture && material.baseColorTexture->isPixelated;
    encoder->setFragmentSamplerState(isPixelated ? device_.pixelSampler() : device_.materialSampler(), gpu::MMSamplerMaterial);
}

void Renderer::drawBatches(MTL::RenderCommandEncoder* encoder, const std::vector<Batch>& batches, MeshPass pass,
                           MTL::PixelFormat colorFormat, int sampleCount) {
    const Material* boundMaterial = nullptr;
    for (const Batch& batch : batches) {
        const Material& material = *batch.material;
        bool isMasked = material.alphaMode == AlphaMode::Mask;

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
                description.vertexFunction = "vertex_mesh";
                if (material.isUnlit) description.fragmentFunction = "fragment_unlit";
                else if (tracesRaysThisFrame_) description.fragmentFunction = isMasked ? "fragment_pbr_masked_traced" : "fragment_pbr_traced";
                else description.fragmentFunction = isMasked ? "fragment_pbr_masked" : "fragment_pbr";
                if (material.alphaMode == AlphaMode::Blend) description.blend = BlendMode::AlphaBlend;
                break;
        }
        MTL::RenderPipelineState* pipeline = shaders_.pipeline(description);
        if (!pipeline) continue;

        encoder->setRenderPipelineState(pipeline);
        encoder->setCullMode(material.isDoubleSided ? MTL::CullModeNone : MTL::CullModeBack);
        // vertex_mesh reads the material too (how the wind sways it): every pass that uses it binds it.
        if (batch.material != boundMaterial && (pass == MeshPass::Forward || pass == MeshPass::Prepass || isMasked)) {
            bindMaterial(encoder, material);
            boundMaterial = batch.material;
        }
        encoder->setVertexBuffer(batch.mesh->vertexBuffer(), 0, gpu::MMBufferVertices);
        encoder->setVertexBuffer(batch.instances.buffer, batch.instances.offset, gpu::MMBufferInstances);
        encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(batch.mesh->indexCount()), MTL::IndexTypeUInt32,
                                       batch.mesh->indexBuffer(), NS::UInteger(0), NS::UInteger(batch.count));

        stats.drawCalls++;
        if (pass == MeshPass::Forward) {
            stats.instances += int(batch.count);
            stats.triangles += int(batch.mesh->indexCount() / 3 * batch.count);
        }
    }
}

// ---------------------------------------------------------------- 1. shadow maps

void Renderer::renderShadows(MTL::CommandBuffer* commandBuffer, const std::vector<DrawItem>& items, const Cascades& cascades) {
    for (int cascade = 0; cascade < cascades.count; cascade++) {
        // Whatever touches this cascade's box, or stands between it and the sun.
        Frustum frustum(cascades.viewProjection[cascade]);
        std::vector<const DrawItem*> casters;
        for (const DrawItem& item : items) {
            if (item.castsShadows && item.material->alphaMode != AlphaMode::Blend && frustum.intersects(item.bounds, false)) {
                casters.push_back(&item);
            }
        }
        std::vector<Batch> batches = makeBatches(casters, true);

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

        MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
        encoder->setLabel(label("Shadow cascade"));
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

    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(label("Depth and normals"));
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

    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(label("Scene"));
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);

    // What every draw of this pass shares is bound once.
    encoder->setVertexBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(passUniforms.buffer, passUniforms.offset, gpu::MMBufferPass);
    encoder->setFragmentBuffer(frameUniforms.buffer, frameUniforms.offset, gpu::MMBufferFrame);
    encoder->setFragmentBuffer(lights.buffer, lights.offset, gpu::MMBufferLights);
    encoder->setFragmentTexture(shadowMap_.get(), gpu::MMTextureShadowMap);
    encoder->setFragmentTexture(environment_.irradianceMap(), gpu::MMTextureIrradiance);
    encoder->setFragmentTexture(environment_.prefilteredMap(), gpu::MMTexturePrefiltered);
    encoder->setFragmentTexture(environment_.brdfLUT(), gpu::MMTextureBRDF);
    encoder->setFragmentTexture(settings.ambientOcclusionEnabled ? occlusion_.get() : white_->handle(),
                                gpu::MMTextureAmbientOcclusion);
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
        stats.drawCalls++;
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
    scene.each<ParticleEmitter, Transform>([&](Entity, ParticleEmitter& emitter, Transform& transform) {
        if (emitter.particles.empty() || !transform.isActiveInHierarchy) return;
        instances.clear();
        for (const Particle& particle : emitter.particles) {
            float t = saturate(particle.age / particle.lifetime);
            float size = particleSize(emitter, t);
            Vec4 color = particleColor(emitter, t);
            instances.push_back(gpu::ParticleInstance{vec4(particle.position, size), color, vec4(particle.velocity, emitter.stretch)});
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
        // One instance per particle; the four vertices of the square come from the vertex index.
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4), NS::UInteger(instances.size()));
        stats.drawCalls++;
    });
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
    stats.drawCalls++;
}

// ---------------------------------------------------------------- 5. bloom

void Renderer::renderBloom(MTL::CommandBuffer* commandBuffer) {
    if (!settings.bloomEnabled || bloom_.empty()) return;

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
    bool hasBloom = settings.bloomEnabled && !bloom_.empty();
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

    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(label("Tone mapping and canvas"));
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
        encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
        stats.drawCalls++;
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
    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(label("Tone mapping, before MetalFX"));
    encoder->setRenderPipelineState(pipeline);
    encoder->setFragmentBytes(postUniforms, uniformsSize, gpu::MMBufferPass);
    encoder->setFragmentTexture(sceneColor_.get(), 0);
    encoder->setFragmentTexture(hasBloom ? bloom_[0].get() : white_->handle(), 1);
    encoder->setFragmentTexture(upscaleLUT_ ? upscaleLUT_ : white_->handle(), 2);
    encoder->setFragmentTexture(upscaleAdapted_ ? upscaleAdapted_ : white_->handle(), 3);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
    stats.drawCalls++;
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
    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(label("Interface"));
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
        stats.drawCalls++;
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

    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass.get());
    encoder->setLabel(label(fragmentFunction));
    encoder->setRenderPipelineState(pipeline);
    if (uniforms) encoder->setFragmentBytes(uniforms, NS::UInteger(uniformsSize), gpu::MMBufferPass);
    if (source0) encoder->setFragmentTexture(source0, 0);
    if (source1) encoder->setFragmentTexture(source1, 1);
    if (source2) encoder->setFragmentTexture(source2, 2);
    if (sampler0) encoder->setFragmentSamplerState(sampler0, 0);
    encoder->setFragmentSamplerState(device_.linearClampSampler(), gpu::MMSamplerLinearClamp);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    encoder->endEncoding();
    stats.drawCalls++;
}

}  // namespace molten
