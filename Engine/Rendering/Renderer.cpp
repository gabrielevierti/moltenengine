#include "Rendering/Renderer.hpp"
#include "Rendering/LevelOfDetail.hpp"

#include "Core/Log.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <functional>

namespace molten {

Renderer::Renderer(GraphicsDevice& device, const std::string& shaderDirectory)
    : device_(device), shaders_(device.device(), shaderDirectory, device.supportsRayTracing()), environment_(device, shaders_) {
    frameSemaphore_ = dispatch_semaphore_create(kFramesInFlight);
    for (FrameAllocator& allocator : allocators_) allocator.initialize(device.device(), 1 << 20);
    white_ = Texture::solidColor(device, Vec4{1, 1, 1, 1});
    flatNormal_ = Texture::solidColor(device, Vec4{0.5f, 0.5f, 1.0f, 1.0f});
}

Renderer::~Renderer() {
    // Take every frame slot: once we hold them all, the GPU has finished every frame.
    // Then give them back, because a semaphore must not be destroyed below its initial count.
    for (int i = 0; i < kFramesInFlight; i++) dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
    for (int i = 0; i < kFramesInFlight; i++) dispatch_semaphore_signal(frameSemaphore_);
    dispatch_release(frameSemaphore_);
}

void Renderer::waitUntilIdle() {
    // Command buffers of one queue run in order: when this empty one is done, so is everything before it.
    MTL::CommandBuffer* commandBuffer = device_.queue()->commandBuffer();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();
}

std::vector<uint8_t> Renderer::renderToImage(Scene& scene, const Canvas& canvas, int width, int height, double time) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (!imageTarget_ || int(imageTarget_->width()) != width || int(imageTarget_->height()) != height) {
        imageTarget_ = device_.makeRenderTarget(MTL::PixelFormatBGRA8Unorm_sRGB, width, height);
    }
    RenderTarget target;
    target.texture = imageTarget_.get();
    render(scene, canvas, target, time);

    // The texture lives in GPU memory. A blit pass copies it into a buffer the CPU can
    // read, and then we wait: the copy has only happened once the GPU says so.
    size_t bytesPerRow = size_t(width) * 4;
    auto buffer = NS::TransferPtr(device_.device()->newBuffer(bytesPerRow * size_t(height), MTL::ResourceStorageModeShared));
    MTL::CommandBuffer* commandBuffer = device_.queue()->commandBuffer();
    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    blit->copyFromTexture(imageTarget_.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(NS::UInteger(width), NS::UInteger(height), 1),
                          buffer.get(), 0, bytesPerRow, bytesPerRow * size_t(height));
    blit->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    // The texture stores blue first; image files want red first.
    std::vector<uint8_t> pixels(bytesPerRow * size_t(height));
    const uint8_t* source = static_cast<const uint8_t*>(buffer->contents());
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = source[i + 2];
        pixels[i + 1] = source[i + 1];
        pixels[i + 2] = source[i];
        pixels[i + 3] = 255;
    }
    return pixels;
}

std::vector<uint8_t> Renderer::readView(size_t index, int& width, int& height) {
    width = height = 0;
    MTL::Texture* texture = viewTexture(index);
    if (!texture) return {};
    width = int(texture->width());
    height = int(texture->height());
    size_t bytesPerRow = size_t(width) * 4;
    auto buffer = NS::TransferPtr(device_.device()->newBuffer(bytesPerRow * size_t(height), MTL::ResourceStorageModeShared));
    MTL::CommandBuffer* commandBuffer = device_.queue()->commandBuffer();
    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    blit->copyFromTexture(texture, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(NS::UInteger(width), NS::UInteger(height), 1), buffer.get(), 0, bytesPerRow,
                          bytesPerRow * size_t(height));
    blit->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();
    // The texture stores blue first; image files want red first.
    std::vector<uint8_t> pixels(bytesPerRow * size_t(height));
    const uint8_t* source = static_cast<const uint8_t*>(buffer->contents());
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = source[i + 2];
        pixels[i + 1] = source[i + 1];
        pixels[i + 2] = source[i];
        pixels[i + 3] = 255;
    }
    return pixels;
}

// ---------------------------------------------------------------- render targets

void Renderer::ensureTargets(int width, int height) {
    int samples = std::clamp(settings.sampleCount, 1, 4);
    if (samples == 3) samples = 2;
    if (!device_.device()->supportsTextureSampleCount(NS::UInteger(samples))) samples = 1;
    bool wantOcclusion = settings.ambientOcclusionEnabled;
    if (width == targetWidth_ && height == targetHeight_ && samples == targetSamples_ && wantOcclusion == targetHasOcclusion_) return;
    targetWidth_ = width;
    targetHeight_ = height;
    targetSamples_ = samples;
    targetHasOcclusion_ = wantOcclusion;

    // The scene is drawn in HDR. With multisampling, the multisampled color and the depth
    // are only needed while the pass runs, so on Apple GPUs they take no memory at all.
    sceneColor_ = device_.makeRenderTarget(kSceneFormat, width, height);
    sceneColorMultisample_ = samples > 1 ? device_.makeRenderTarget(kSceneFormat, width, height, samples, true)
                                         : NS::SharedPtr<MTL::Texture>();
    sceneDepth_ = device_.makeRenderTarget(kDepthFormat, width, height, samples, true);

    if (wantOcclusion) {
        prepassDepth_ = device_.makeRenderTarget(kDepthFormat, width, height);
        prepassNormal_ = device_.makeRenderTarget(MTL::PixelFormatRGBA8Unorm, width, height);
        occlusionRaw_ = device_.makeRenderTarget(MTL::PixelFormatR8Unorm, width, height);
        occlusion_ = device_.makeRenderTarget(MTL::PixelFormatR8Unorm, width, height);
    } else {
        prepassDepth_ = prepassNormal_ = occlusionRaw_ = occlusion_ = NS::SharedPtr<MTL::Texture>();
    }

    // The bloom chain: half size, quarter size, and so on.
    bloom_.clear();
    int w = width / 2, h = height / 2;
    while (bloom_.size() < 6 && w >= 8 && h >= 8) {
        bloom_.push_back(device_.makeRenderTarget(kSceneFormat, w, h));
        w /= 2;
        h /= 2;
    }
}

void Renderer::ensureShadowMap() {
    // With shadows off, a single texel keeps the shader's texture argument bound.
    int size = settings.shadowsEnabled ? std::max(settings.cascades.resolution, 16) : 1;
    int slices = settings.shadowsEnabled ? std::clamp(settings.cascades.count, 1, kMaxCascades) : 1;
    if (size == shadowMapSize_ && slices == shadowMapSlices_) return;
    shadowMapSize_ = size;
    shadowMapSlices_ = slices;

    auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(MTL::TextureType2DArray);
    descriptor->setPixelFormat(kDepthFormat);
    descriptor->setWidth(NS::UInteger(size));
    descriptor->setHeight(NS::UInteger(size));
    descriptor->setArrayLength(NS::UInteger(slices));
    descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    shadowMap_ = NS::TransferPtr(device_.device()->newTexture(descriptor.get()));
}

// ---------------------------------------------------------------- extraction

Renderer::View Renderer::findView(Scene& scene, float aspect) const {
    View view;
    view.aspect = aspect;
    bool found = false;
    bool hasOverride = scene.has<Camera>(cameraOverride) && scene.has<Transform>(cameraOverride);
    scene.each<Camera, Transform>([&](Entity entity, Camera& camera, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (found || (hasOverride ? entity != cameraOverride : !camera.isActive)) return;
        found = true;
        // Where the camera is and which way it faces; its scale says nothing about what it sees.
        Vec3 position, scale;
        Quat rotation;
        decompose(transform.world, position, rotation, scale);
        view.world = compose(position, rotation, Vec3{1, 1, 1});
        view.fovY = camera.fovY;
        view.nearPlane = camera.nearPlane;
        view.farPlane = camera.farPlane;
        view.projection = camera.projection(aspect);
    });
    if (!found) {
        // No camera in the scene: look at the origin from a little above.
        view.world = simd::inverse(lookAt(Vec3{0, 2, 6}, Vec3{0, 0, 0}));
        view.projection = perspective(view.fovY, aspect, view.nearPlane, view.farPlane);
    }
    view.view = simd::inverse(view.world);
    view.viewProjection = view.projection * view.view;
    view.position = xyz(view.world.columns[3]);
    return view;
}

void Renderer::collectItems(Scene& scene, const View& view, std::vector<DrawItem>& items) const {
    chooseLevelsOfDetail(scene, view.position);
    scene.each<MeshRenderer, Transform>([&](Entity, MeshRenderer& renderer, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!renderer.isVisible || !renderer.mesh || renderer.isBeyondItsDetail) return;
        DrawItem item;
        item.mesh = renderer.mesh.get();
        item.material = renderer.material ? renderer.material.get() : &defaultMaterial_;
        item.world = transform.world;
        item.tint = renderer.tint;
        item.bounds = renderer.mesh->bounds().transformed(transform.world);
        item.distanceSquared = simd::length_squared(item.bounds.center() - view.position);
        item.castsShadows = renderer.castsShadows;
        items.push_back(item);
    });
}

int Renderer::collectLights(Scene& scene, gpu::LightData* lights, gpu::FrameUniforms& frame, bool& sunCastsShadows) const {
    bool hasSun = false;
    gpu::LightData sun{};
    std::vector<gpu::LightData> others;
    sunCastsShadows = false;

    scene.each<Light, Transform>([&](Entity, Light& light, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!light.isEnabled) return;
        gpu::LightData data{};
        data.position = transform.worldPosition();
        data.range = std::max(light.range, 0.001f);
        data.direction = safeNormalize(-xyz(transform.world.columns[2]), Vec3{0, -1, 0});     // the entity's -Z axis
        data.type = uint32_t(light.type);
        data.color = light.color * light.intensity;
        float cosInner = std::cos(light.innerConeAngle);
        float cosOuter = std::cos(std::max(light.outerConeAngle, light.innerConeAngle + 0.001f));
        data.spotScale = 1.0f / (cosInner - cosOuter);
        data.spotOffset = -cosOuter * data.spotScale;

        // The first directional light is the sun: it lights the sky and may cast shadows.
        if (light.type == LightType::Directional && !hasSun) {
            hasSun = true;
            sun = data;
            sunCastsShadows = light.castsShadows;
        } else {
            others.push_back(data);
        }
    });

    int count = 0;
    if (hasSun) {
        lights[count++] = sun;                  // the shaders expect the sun first
        frame.sunDirection = sun.direction;
        frame.sunColor = sun.color;
    } else {
        frame.sunDirection = Vec3{0, -1, 0};
        frame.sunColor = Vec3{0, 0, 0};
    }
    for (const gpu::LightData& light : others) {
        if (count >= gpu::MMMaxLights) {
            logWarning("more than %d lights in the scene; the rest are ignored", int(gpu::MMMaxLights));
            break;
        }
        lights[count++] = light;
    }
    return count;
}

std::vector<Renderer::Batch> Renderer::makeBatches(std::vector<const DrawItem*>& items, bool sortByState) {
    if (sortByState) {
        // Objects that share a material and a mesh end up next to each other.
        std::sort(items.begin(), items.end(), [](const DrawItem* a, const DrawItem* b) {
            if (a->material != b->material) return std::less<const Material*>()(a->material, b->material);
            return std::less<const Mesh*>()(a->mesh, b->mesh);
        });
    }
    std::vector<Batch> batches;
    std::vector<gpu::InstanceData> instances;
    size_t first = 0;
    while (first < items.size()) {
        // Every run of the same mesh and material becomes one instanced draw.
        instances.clear();
        size_t next = first;
        while (next < items.size() && items[next]->mesh == items[first]->mesh && items[next]->material == items[first]->material) {
            instances.push_back(gpu::InstanceData{items[next]->world, normalMatrix(items[next]->world), items[next]->tint});
            next++;
        }
        Batch batch;
        batch.mesh = items[first]->mesh;
        batch.material = items[first]->material;
        batch.instances = allocator().copyArray(instances.data(), instances.size());
        batch.count = uint32_t(instances.size());
        batches.push_back(batch);
        first = next;
    }
    return batches;
}

gpu::PassUniforms Renderer::makePassUniforms(const Mat4& view, const Mat4& projection, Vec3 position, int width, int height,
                                             float nearPlane, float farPlane, double time) const {
    gpu::PassUniforms uniforms{};
    uniforms.view = view;
    uniforms.projection = projection;
    uniforms.viewProjection = projection * view;
    uniforms.inverseViewProjection = simd::inverse(uniforms.viewProjection);
    uniforms.inverseProjection = simd::inverse(projection);
    uniforms.cameraPosition = position;
    uniforms.time = float(time);
    uniforms.viewportSize = Vec2{float(width), float(height)};
    uniforms.nearPlane = nearPlane;
    uniforms.farPlane = farPlane;
    return uniforms;
}

// ---------------------------------------------------------------- the frame

/// The part of the target the scene is drawn into: sceneViewport kept inside the target,
/// or all of it.
Renderer::Rect Renderer::sceneRect(MTL::Texture* target) const {
    int targetWidth = int(target->width()), targetHeight = int(target->height());
    if (sceneViewport.width <= 0 || sceneViewport.height <= 0) return Rect{0, 0, targetWidth, targetHeight};
    Rect area;
    area.x = std::clamp(sceneViewport.x, 0, targetWidth - 1);
    area.y = std::clamp(sceneViewport.y, 0, targetHeight - 1);
    area.width = std::clamp(sceneViewport.width, 1, targetWidth - area.x);
    area.height = std::clamp(sceneViewport.height, 1, targetHeight - area.y);
    return area;
}

/// Builds, for this frame, the structure rays are traced against: one entry per object
/// that casts a shadow, each pointing at its mesh's own structure with the object's
/// place in the world. Returns false if there is nothing to trace against.
///
/// The meshes' structures never change; only this list of instances is rebuilt, which
/// is cheap: it is a tree over a few hundred boxes, not over millions of triangles.
bool Renderer::buildSceneAccelerationStructure(MTL::CommandBuffer* commandBuffer, const std::vector<DrawItem>& items) {
    tracedMeshes_.clear();
    std::vector<MTL::AccelerationStructureInstanceDescriptor> instances;
    std::vector<gpu::TracedSurface> surfaces;       // what a ray that hits each instance sees
    for (const DrawItem& item : items) {
        const MTL::AccelerationStructure* meshStructure = item.mesh->accelerationStructure();
        // Glass and other blended materials let light through: they cast no shadow here either.
        if (!meshStructure || !item.castsShadows || item.material->alphaMode == AlphaMode::Blend) continue;
        auto found = std::find(tracedMeshes_.begin(), tracedMeshes_.end(), meshStructure);
        uint32_t meshIndex = uint32_t(found - tracedMeshes_.begin());
        if (found == tracedMeshes_.end()) tracedMeshes_.push_back(meshStructure);

        MTL::AccelerationStructureInstanceDescriptor instance{};
        // The top three rows of the world matrix, column by column.
        for (int column = 0; column < 4; column++) {
            Vec4 c = item.world.columns[column];
            instance.transformationMatrix.columns[column] = MTL::PackedFloat3(c.x, c.y, c.z);
        }
        instance.options = MTL::AccelerationStructureInstanceOptionOpaque;
        instance.mask = 0xFF;
        instance.intersectionFunctionTableOffset = 0;
        instance.accelerationStructureIndex = meshIndex;
        instances.push_back(instance);
        gpu::TracedSurface surface{};
        surface.baseColor = item.material->baseColor * item.tint;
        surface.emissive = Vec4{item.material->emissive.x, item.material->emissive.y, item.material->emissive.z, 0};
        surfaces.push_back(surface);
    }
    if (instances.empty()) return false;

    FrameAllocator::Allocation instanceBuffer = allocator().copyArray(instances.data(), instances.size());
    tracedSurfaces_ = allocator().copyArray(surfaces.data(), surfaces.size());
    auto descriptor = NS::TransferPtr(MTL::InstanceAccelerationStructureDescriptor::alloc()->init());
    descriptor->setInstancedAccelerationStructures(
        NS::Array::array(reinterpret_cast<const NS::Object* const*>(tracedMeshes_.data()), tracedMeshes_.size()));
    descriptor->setInstanceCount(instances.size());
    descriptor->setInstanceDescriptorBuffer(instanceBuffer.buffer);
    descriptor->setInstanceDescriptorBufferOffset(instanceBuffer.offset);

    // The structure and the scratch memory are kept between frames and only replaced
    // when the scene has outgrown them. Frames run on the GPU in the order they were
    // submitted, so rebuilding in place never disturbs a frame that is still drawing.
    MTL::AccelerationStructureSizes sizes = device_.device()->accelerationStructureSizes(descriptor.get());
    if (!sceneAccelerationStructure_ || sizes.accelerationStructureSize > sceneAccelerationCapacity_) {
        sceneAccelerationCapacity_ = sizes.accelerationStructureSize + sizes.accelerationStructureSize / 2;
        sceneAccelerationStructure_ = NS::TransferPtr(device_.device()->newAccelerationStructure(sceneAccelerationCapacity_));
    }
    if (!sceneAccelerationScratch_ || sizes.buildScratchBufferSize > sceneAccelerationScratchCapacity_) {
        sceneAccelerationScratchCapacity_ = sizes.buildScratchBufferSize + sizes.buildScratchBufferSize / 2;
        sceneAccelerationScratch_ = NS::TransferPtr(
            device_.device()->newBuffer(sceneAccelerationScratchCapacity_, MTL::ResourceStorageModePrivate));
    }
    if (!sceneAccelerationStructure_ || !sceneAccelerationScratch_) return false;

    MTL::AccelerationStructureCommandEncoder* encoder = commandBuffer->accelerationStructureCommandEncoder();
    encoder->setLabel(NS::String::string("Scene for ray tracing", NS::UTF8StringEncoding));
    encoder->buildAccelerationStructure(sceneAccelerationStructure_.get(), descriptor.get(), sceneAccelerationScratch_.get(), 0);
    encoder->endEncoding();
    return true;
}

void Renderer::swapTargets(size_t slot) {
    if (targetSets_.size() <= slot) targetSets_.resize(slot + 1);
    TargetSet& kept = targetSets_[slot];
    std::swap(kept.width, targetWidth_);
    std::swap(kept.height, targetHeight_);
    std::swap(kept.samples, targetSamples_);
    std::swap(kept.hasOcclusion, targetHasOcclusion_);
    std::swap(kept.colorMultisample, sceneColorMultisample_);
    std::swap(kept.depth, sceneDepth_);
    std::swap(kept.color, sceneColor_);
    std::swap(kept.prepassDepth, prepassDepth_);
    std::swap(kept.prepassNormal, prepassNormal_);
    std::swap(kept.occlusionRaw, occlusionRaw_);
    std::swap(kept.occlusion, occlusion_);
    std::swap(kept.bloom, bloom_);
}

void Renderer::render(Scene& scene, const Canvas& canvas, const RenderTarget& target, double time) {
    if (!target.texture) return;

    // At most three frames may be waiting for the GPU. If it is behind, wait here.
    dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    allocator().reset();
    frameNumber_++;
    stats = RenderStats{};
    hasSceneStructureThisFrame_ = false;
    // What was replaced during the last frame has been drawn with for the last time.
    retiredTextures_.clear();

    if (frameNumber_ % 30 == 0 && shaders_.reloadIfChanged()) environment_.invalidate();
    // Surface shaders written for the game: compiled in when a material first names one,
    // and again when one of their files is saved.
    if ((frameNumber_ % 30 == 0 || surfaceShaders_.all().size() != size_t(compiledSurfaceShaders_)) && surfaceShaders_.takeChanges()) {
        compiledSurfaceShaders_ = int(surfaceShaders_.all().size());
        if (shaders_.setSurfaceCode(surfaceShaders_.code())) environment_.invalidate();
    }

    MTL::CommandBuffer* commandBuffer = device_.queue()->commandBuffer();

    if (views.empty()) {
        // The usual case: one picture, straight into the target.
        swapTargets(0);
        drawsDebugLines_ = true;
        renderView(commandBuffer, scene, canvas, target, time, true);
        swapTargets(0);
    } else {
        // An editor: each view into its own texture, then the interface over the target.
        static const Canvas noCanvas;
        const Entity cameraBefore = cameraOverride;
        const Rect viewportBefore = sceneViewport;
        sceneViewport = Rect{};             // a view fills its texture
        viewColors_.resize(views.size());
        for (size_t i = 0; i < views.size(); i++) {
            const SceneView& wanted = views[i];
            if (wanted.width < 1 || wanted.height < 1) continue;
            NS::SharedPtr<MTL::Texture>& color = viewColors_[i];
            if (!color || int(color->width()) != wanted.width || int(color->height()) != wanted.height) {
                // Whoever was handed the old texture (an interface drawn earlier in this
                // frame) may still draw with it: it is kept until the next frame.
                if (color) retiredTextures_.push_back(color);
                color = device_.makeRenderTarget(MTL::PixelFormatBGRA8Unorm_sRGB, wanted.width, wanted.height);
            }
            RenderTarget viewTarget;
            viewTarget.texture = color.get();
            cameraOverride = wanted.camera;
            drawsDebugLines_ = wanted.showsDebugLines;
            swapTargets(i + 1);
            renderView(commandBuffer, scene, int(i) == canvasView ? canvas : noCanvas, viewTarget, time, false);
            swapTargets(i + 1);
        }
        cameraOverride = cameraBefore;
        sceneViewport = viewportBefore;
        drawsDebugLines_ = true;
        bool canvasIsInAView = canvasView >= 0 && size_t(canvasView) < views.size() && views[size_t(canvasView)].width > 0;
        renderInterface(commandBuffer, canvasIsInAView ? noCanvas : canvas, target);
    }
    debug.clear();

    if (target.drawable) commandBuffer->presentDrawable(target.drawable);
    dispatch_semaphore_t semaphore = frameSemaphore_;
    commandBuffer->addCompletedHandler([semaphore](MTL::CommandBuffer*) { dispatch_semaphore_signal(semaphore); });
    commandBuffer->commit();
}

void Renderer::renderView(MTL::CommandBuffer* commandBuffer, Scene& scene, const Canvas& canvas, const RenderTarget& target, double time,
                          bool withOverlay) {
    Rect area = sceneRect(target.texture);
    int width = std::max(1, int(float(area.width) * settings.renderScale));
    int height = std::max(1, int(float(area.height) * settings.renderScale));
    ensureTargets(width, height);
    ensureShadowMap();

    // ---- what is in the scene
    View view = findView(scene, float(width) / float(height));
    std::vector<DrawItem> items;
    collectItems(scene, view, items);

    gpu::FrameUniforms frame{};
    gpu::LightData lights[gpu::MMMaxLights];
    bool sunCastsShadows = false;
    int lightCount = collectLights(scene, lights, frame, sunCastsShadows);
    frame.lightCount = uint32_t(lightCount);
    stats.lights = std::max(stats.lights, lightCount);

    // Ray-traced sun shadows replace the shadow maps where they are available and asked for.
    bool wantsTracedShadows = settings.shadowsEnabled && sunCastsShadows && settings.rayTracedShadows && canTraceRays();

    Cascades cascades;
    if (settings.shadowsEnabled && sunCastsShadows && !wantsTracedShadows) {
        cascades = computeCascades(view.world, view.fovY, view.aspect, view.nearPlane, frame.sunDirection, settings.cascades);
        frame.cascadeCount = uint32_t(cascades.count);
        for (int i = 0; i < cascades.count; i++) {
            frame.cascadeViewProjection[i] = cascades.viewProjection[i];
            frame.cascadeSplits[i] = cascades.splitFar[i];
            frame.cascadeTexelSize[i] = 2.0f * cascades.radius[i] / float(settings.cascades.resolution);
        }
    }
    frame.shadowBias = settings.shadowBias;
    frame.shadowNormalOffset = settings.shadowNormalOffset;
    frame.shadowMapSize = float(shadowMapSize_);
    frame.shadowDistance = settings.cascades.shadowDistance;
    frame.environmentIntensity = settings.environmentIntensity;
    frame.prefilteredMipCount = environment_.prefilteredMipCount();
    frame.ambientOcclusionStrength = settings.ambientOcclusionEnabled ? 1.0f : 0.0f;
    frame.skyZenithColor = settings.sky.zenithColor;
    frame.skyHorizonColor = settings.sky.horizonColor;
    frame.skyGroundColor = settings.sky.groundColor;
    frame.skyIntensity = settings.sky.intensity;
    frame.sunDiskIntensity = settings.sky.sunDiskIntensity;
    frame.sunDiskSize = std::cos(settings.sky.sunAngularRadius);
    frame.fogColor = settings.fogColor;
    frame.fogDensity = settings.fogDensity;
    frame.fogHeightFalloff = std::max(settings.fogHeightFalloff, 0.0f);
    frame.fogSunScatter = std::max(settings.fogSunScatter, 0.0f);
    frame.cloudCoverage = settings.sky.cloudCoverage;
    frame.volumetricClouds = settings.sky.cloudsHaveDepth ? 1u : 0u;
    frame.fogShaftSteps = settings.fogDensity > 0 ? uint32_t(std::clamp(settings.fogShaftSteps, 0, 48)) : 0u;
    frame.cloudScale = settings.sky.cloudScale;
    frame.cloudOffset = settings.sky.cloudSpeed * float(time);
    frame.sunAngularRadius = settings.sky.sunAngularRadius;
    time_ = time;

    // ---- what the camera can see, in the order it will be drawn
    Frustum frustum(view.viewProjection);
    std::vector<const DrawItem*> opaqueItems, transparentItems;
    for (const DrawItem& item : items) {
        if (!frustum.intersects(item.bounds)) {
            stats.culledObjects++;
            continue;
        }
        stats.visibleObjects++;
        (item.material->alphaMode == AlphaMode::Blend ? transparentItems : opaqueItems).push_back(&item);
    }
    // Transparent surfaces must be blended from the farthest to the nearest.
    std::sort(transparentItems.begin(), transparentItems.end(),
              [](const DrawItem* a, const DrawItem* b) { return a->distanceSquared > b->distanceSquared; });
    std::vector<Batch> opaque = makeBatches(opaqueItems, true);
    std::vector<Batch> transparent = makeBatches(transparentItems, false);

    // ---- the passes

    // The structure rays are traced against holds everything that casts a shadow, seen
    // by the camera or not: what is behind the camera still shades what is in front.
    // It is the same for every picture of a frame: built for the first that wants it.
    if (wantsTracedShadows && !hasSceneStructureThisFrame_) hasSceneStructureThisFrame_ = buildSceneAccelerationStructure(commandBuffer, items);
    tracesRaysThisFrame_ = wantsTracedShadows && hasSceneStructureThisFrame_;
    frame.shadowRayCount = tracesRaysThisFrame_ ? uint32_t(std::clamp(settings.shadowRayCount, 1, 16)) : 0u;
    frame.occlusionRayCount = tracesRaysThisFrame_ && settings.rayTracedOcclusion ? uint32_t(std::clamp(settings.occlusionRayCount, 1, 16)) : 0u;
    frame.tracesReflections = tracesRaysThisFrame_ && settings.rayTracedReflections ? 1u : 0u;
    frame.reflectionMaxRoughness = std::clamp(settings.reflectionMaxRoughness, 0.01f, 1.0f);
    frame.reflectionStrength = std::max(settings.reflectionStrength, 0.0f);
    frame.bounceIntensity = tracesRaysThisFrame_ && settings.rayTracedOcclusion && settings.rayTracedBounce ? std::max(settings.bounceIntensity, 0.0f) : 0.0f;

    FrameAllocator::Allocation passUniforms = allocator().copy(
        makePassUniforms(view.view, view.projection, view.position, width, height, view.nearPlane, view.farPlane, time));
    FrameAllocator::Allocation frameUniforms = allocator().copy(frame);
    FrameAllocator::Allocation lightBuffer = allocator().copyArray(lights, size_t(lightCount));

    environment_.updateIfNeeded(commandBuffer, frame);
    if (frame.cascadeCount > 0) renderShadows(commandBuffer, items, cascades);
    if (settings.ambientOcclusionEnabled) renderOcclusion(commandBuffer, opaque, view, passUniforms);
    renderScene(commandBuffer, scene, opaque, transparent, view, passUniforms, frameUniforms, lightBuffer);
    renderBloom(commandBuffer);
    renderFinal(commandBuffer, canvas, target, withOverlay);
}

}  // namespace molten
