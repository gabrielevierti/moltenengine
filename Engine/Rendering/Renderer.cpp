#include "Rendering/Renderer.hpp"
#include "Rendering/Quality.hpp"
#include "Water/Ocean.hpp"

#include <cstring>
#include "Rendering/LevelOfDetail.hpp"

#include "Core/Log.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <chrono>
#include <functional>

namespace molten {

Renderer::Renderer(GraphicsDevice& device, const std::string& shaderDirectory)
    : device_(device), shaders_(device.device(), shaderDirectory, device.supportsRayTracing()), environment_(device, shaders_) {
    frameSemaphore_ = dispatch_semaphore_create(kFramesInFlight);
    for (FrameAllocator& allocator : allocators_) allocator.initialize(device.device(), 1 << 20);
    white_ = Texture::solidColor(device, Vec4{1, 1, 1, 1});
    flatNormal_ = Texture::solidColor(device, Vec4{0.5f, 0.5f, 1.0f, 1.0f});
    // The clouds' noise: half a second of arithmetic, done while the rest of the engine starts.
    cloudNoiseMaking_ = std::async(std::launch::async, [] { return makeCloudNoise(); });
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
    bool wantOcclusion = wantsDepth();
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
        postColor_ = device_.makeRenderTarget(kSceneFormat, width, height);
    } else {
        prepassDepth_ = prepassNormal_ = occlusionRaw_ = occlusion_ = postColor_ = NS::SharedPtr<MTL::Texture>();
    }
    // How bright the scene is: a small picture of its brightness, averaged down to one pixel.
    // Their size does not follow the view's, and the exposure they keep must not start over
    // while a view is being resized (that blacked the picture out while a divider was dragged).
    if (!luminance_) luminance_ = device_.makeRenderTarget(MTL::PixelFormatR16Float, 64, 64, 1, false, 7);
    if (!adapted_[0]) adapted_[0] = device_.makeRenderTarget(MTL::PixelFormatR16Float, 1, 1);
    if (!adapted_[1]) adapted_[1] = device_.makeRenderTarget(MTL::PixelFormatR16Float, 1, 1);
    hasPreviousView_ = false;

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
    scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!renderer.isVisible || !renderer.mesh || renderer.isBeyondItsDetail) return;
        // Many copies: one item each, those too far away left out.
        if (const MeshInstances* copies = scene.get<MeshInstances>(entity)) {
            float far = copies->drawDistance * detailDistanceScale();
            float reach = copies->drawDistance > 0 ? far * far : 1e30f;
            const Material* material = renderer.material ? renderer.material.get() : &defaultMaterial_;
            const AABB local = renderer.mesh->bounds();
            for (const Mat4& place : copies->places) {
                DrawItem item;
                item.world = transform.world * place;
                item.distanceSquared = simd::length_squared(xyz(item.world.columns[3]) - view.position);
                if (item.distanceSquared > reach) continue;
                item.mesh = renderer.mesh.get();
                item.material = material;
                item.tint = renderer.tint;
                item.bounds = local.transformed(item.world);
                item.castsShadows = renderer.castsShadows;
                items.push_back(item);
            }
            return;
        }
        DrawItem item;
        item.mesh = renderer.mesh.get();
        item.material = renderer.material ? renderer.material.get() : &defaultMaterial_;
        // Water on a flat mesh of few vertices (a plane): drawn as a fine grid of the same size,
        // so that its waves can rise and fall.
        if (item.material->isWater) item.mesh = waterGridFor(renderer.mesh.get());
        item.world = transform.world;
        item.tint = renderer.tint;
        item.bounds = renderer.mesh->bounds().transformed(transform.world);
        item.distanceSquared = simd::length_squared(item.bounds.center() - view.position);
        item.castsShadows = renderer.castsShadows;
        items.push_back(item);
    });
}

Mesh* Renderer::waterGridFor(Mesh* mesh) const {
    const AABB& box = mesh->bounds();
    Vec3 size = box.max - box.min;
    bool isFlat = size.y <= std::max(size.x, size.z) * 0.01f + 1e-4f;
    if (!isFlat || mesh->isDynamic || mesh->indexCount() > 6000 || size.x <= 0 || size.z <= 0) return mesh;
    auto found = waterGrids_.find(mesh);
    if (found != waterGrids_.end()) return found->second.get();
    MeshData grid = MeshData::plane(size.x, size.z, 160, 1.0f);
    Vec3 center = box.center();
    for (Vec3& p : grid.positions) p += Vec3{center.x, box.max.y, center.z};
    auto made = std::make_unique<Mesh>(device_, grid);
    Mesh* kept = made.get();
    waterGrids_[mesh] = std::move(made);
    return kept;
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
        data.areaWidth = std::max(light.areaSize.x, 0.01f) * std::abs(transform.scale.x);
        data.areaHeight = std::max(light.areaSize.y, 0.01f) * std::abs(transform.scale.y);

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
        frame.skySunDirection = sun.direction;
        frame.sunColor = sun.color;
        if (settings.sky.sunsetColors) {
            // Low in the sky, the sun's light crosses more air: gold, then orange and red, then none.
            frame.sunColor = sun.color * sunlightThroughAir(-sun.direction.y);
            // Once it has set, the moon across the sky from it lights the scene: cold, dim, with its own shadows.
            float night = std::clamp((0.02f - (-sun.direction.y)) / 0.14f, 0.0f, 1.0f);
            night = night * night * (3 - 2 * night);
            Vec3 moonlight = Vec3{0.55f, 0.66f, 1.0f} * (0.16f * std::max(settings.sky.moon, 0.0f) * night);
            auto brightness = [](Vec3 c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };
            if (brightness(moonlight) > brightness(frame.sunColor)) {
                frame.sunDirection = -sun.direction;
                frame.sunColor = moonlight;
                lights[0].direction = frame.sunDirection;
            }
        }
        lights[0].color = frame.sunColor;
    } else {
        frame.sunDirection = Vec3{0, -1, 0};
        frame.skySunDirection = Vec3{0, -1, 0};
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
    uniforms.wind = settings.wind;
    uniforms.windGust = std::clamp(settings.windGustiness, 0.0f, 1.0f);
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
        if (!meshStructure || !item.castsShadows || item.material->isSeenThrough()) continue;
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

void Renderer::takeCloudNoise() {
    if (!cloudNoiseMaking_.valid() || cloudNoiseMaking_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    CloudNoise noise = cloudNoiseMaking_.get();
    if (noise.isEmpty()) return;
    // A block of points with all its smaller copies, which the far clouds are read from.
    auto makeBlock = [&](int width, int height, int depth, std::vector<uint8_t> pixels) {
        auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
        descriptor->setTextureType(MTL::TextureType3D);
        descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        descriptor->setWidth(NS::UInteger(width));
        descriptor->setHeight(NS::UInteger(height));
        descriptor->setDepth(NS::UInteger(depth));
        int levels = 1;
        while ((1 << levels) <= std::max(width, std::max(height, depth))) levels++;
        descriptor->setMipmapLevelCount(NS::UInteger(levels));
        descriptor->setUsage(MTL::TextureUsageShaderRead);
        descriptor->setStorageMode(MTL::StorageModeShared);
        NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(device_.device()->newTexture(descriptor.get()));
        for (int level = 0; texture && level < levels && !pixels.empty(); level++) {
            texture->replaceRegion(MTL::Region::Make3D(0, 0, 0, NS::UInteger(width), NS::UInteger(height), NS::UInteger(depth)), NS::UInteger(level), 0,
                                   pixels.data(), NS::UInteger(width) * 4, NS::UInteger(width) * NS::UInteger(height) * 4);
            pixels = halvedNoise(pixels, width, height, depth);
        }
        return texture;
    };
    cloudShape_ = makeBlock(noise.shapeWidth, noise.shapeHeight, noise.shapeDepth, std::move(noise.shape));
    cloudDetail_ = makeBlock(noise.detailSize, noise.detailSize, noise.detailSize, std::move(noise.detail));
    if (!cloudShape_ || !cloudDetail_) cloudShape_ = cloudDetail_ = NS::SharedPtr<MTL::Texture>();
}

void Renderer::adjustDynamicScale() {
    // The scene's own settings say whether, or, for a scene that does not ask, the project's quality.
    bool isOn = settings.dynamicResolution;
    float targetFrameRate = settings.targetFrameRate, lowest = settings.lowestDynamicScale;
    if (quality) quality->dynamicResolutionFor(isOn, targetFrameRate, lowest);
    if (!isOn) {
        dynamicScale_ = 1.0f;
        return;
    }
    // Not more often than every third of a second: each change makes new pictures to draw into,
    // and the GPU's time is that of a frame drawn a few frames ago.
    framesSinceScaleChange_++;
    if (framesSinceScaleChange_ < 20) return;
    float before = dynamicScale_;
    dynamicScale_ = nextDynamicScale(dynamicScale_, lastGpuMilliseconds(), targetFrameRate, lowest);
    if (dynamicScale_ != before) framesSinceScaleChange_ = 0;
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
    std::swap(kept.upscaler, upscaler_);
    std::swap(kept.postColor, postColor_);
    std::swap(kept.cloudColor, cloudColor_);
    std::swap(kept.tracedLight[0], tracedLight_[0]);
    std::swap(kept.tracedLight[1], tracedLight_[1]);
    std::swap(kept.tracedSurface, tracedSurface_);
    std::swap(kept.luminance, luminance_);
    std::swap(kept.adapted[0], adapted_[0]);
    std::swap(kept.adapted[1], adapted_[1]);
    std::swap(kept.adaptIndex, adaptIndex_);
    std::swap(kept.previousViewProjection, previousViewProjection_);
    std::swap(kept.hasPreviousView, hasPreviousView_);
    std::swap(kept.lastViewTime, lastViewTime_);
}

void Renderer::render(Scene& scene, const Canvas& canvas, const RenderTarget& target, double time) {
    if (!target.texture) return;

    // At most three frames may be waiting for the GPU. If it is behind, wait here.
    {
        Profiler::Scope timed(profilerOrIdle(), "Waiting for the GPU");
        dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
    }
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    allocator().reset();
    frameNumber_++;
    stats = RenderStats{};
    adjustDynamicScale();
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
    takeCloudNoise();
    prepareOcean(commandBuffer, time);

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
        viewStats_.assign(views.size(), RenderStats{});
        for (size_t i = 0; i < views.size(); i++) {
            const SceneView& wanted = views[i];
            if (wanted.width < 1 || wanted.height < 1) continue;
            const RenderStats before = stats;
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
            viewStats_[i] = stats.since(before);
        }
        cameraOverride = cameraBefore;
        sceneViewport = viewportBefore;
        drawsDebugLines_ = true;
        bool canvasIsInAView = canvasView >= 0 && size_t(canvasView) < views.size() && views[size_t(canvasView)].width > 0;
        Profiler::Scope timed(profilerOrIdle(), "Windows over the views");
        renderInterface(commandBuffer, canvasIsInAView ? noCanvas : canvas, target);
    }
    debug.clear();

    if (target.drawable) commandBuffer->presentDrawable(target.drawable);
    dispatch_semaphore_t semaphore = frameSemaphore_;
    std::atomic<float>* gpuTime = &lastGpuMilliseconds_;
    commandBuffer->addCompletedHandler([semaphore, gpuTime](MTL::CommandBuffer* finished) {
        double seconds = finished->GPUEndTime() - finished->GPUStartTime();
        if (seconds > 0) gpuTime->store(float(seconds * 1000.0), std::memory_order_relaxed);
        dispatch_semaphore_signal(semaphore);
    });
    commandBuffer->commit();
}

void Renderer::renderView(MTL::CommandBuffer* commandBuffer, Scene& scene, const Canvas& canvas, const RenderTarget& target, double time,
                          bool withOverlay) {
    // The project's quality, over what the scene says, for this view only: the scene keeps its own.
    struct QualityScope {
        RenderSettings& settings;
        RenderSettings saved;
        bool isOn;
        QualityScope(RenderSettings& s, const GameQuality* quality) : settings(s), saved(s), isOn(quality != nullptr) {
            if (quality) quality->applyTo(settings);
        }
        ~QualityScope() {
            if (isOn) settings = saved;
        }
    } qualityScope(settings, quality);

    Rect area = sceneRect(target.texture);
    float scale = settings.renderScale * dynamicScale_ * (settings.followsDeviceHeat ? heatScale : 1.0f);
    int width = std::max(1, int(float(area.width) * scale));
    int height = std::max(1, int(float(area.height) * scale));
    ensureTargets(width, height);
    ensureShadowMap();

    // ---- what is in the scene
    Profiler& timing = profilerOrIdle();
    Profiler::Scope timedView(timing, withOverlay ? "The picture" : "A view of the editor");
    View view = findView(scene, float(width) / float(height));
    std::vector<DrawItem> items;
    {
        Profiler::Scope timed(timing, "Collect and cull");
        collectItems(scene, view, items);
    }

    gpu::FrameUniforms frame{};
    gpu::LightData lights[gpu::MMMaxLights];
    bool sunCastsShadows = false;
    int lightCount = collectLights(scene, lights, frame, sunCastsShadows);
    frame.lightCount = uint32_t(lightCount);
    stats.lights = std::max(stats.lights, lightCount);
    if (withOverlay) {
        // The main view: where its sun is, for the lens flare of the final pass.
        flareSunDirection_ = frame.sunDirection;
        flareSunColor_ = frame.sunColor;
        flareViewProjection_ = view.viewProjection;
    }

    // Ray-traced sun shadows replace the shadow maps where they are available and asked for.
    bool wantsTracedShadows = settings.shadowsEnabled && sunCastsShadows && settings.rayTracedShadows && canTraceRays();

    Cascades cascades;
    std::vector<ShadowCaster> casters;
    if (settings.shadowsEnabled && sunCastsShadows && !wantsTracedShadows) {
        cascades = computeCascades(view.world, view.fovY, view.aspect, view.nearPlane, frame.sunDirection, settings.cascades);
        casters = findShadowCasters(items, cascades);
        // Nothing whose shadow reaches the view: no maps to draw, and none for the scene to read.
        if (casters.empty()) cascades.count = 0;
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
    // Clouds with depth, in so many steps; none where they cannot be walked (the noise is still
    // being made, the shaders are without the extras): the flat layer then.
    const bool hasCloudsWithDepth = marchesClouds();
    frame.volumetricClouds = hasCloudsWithDepth ? uint32_t(std::clamp(settings.sky.cloudSteps, 16, 128)) : 0u;
    frame.fogShaftSteps = settings.fogDensity > 0 ? uint32_t(std::clamp(settings.fogShaftSteps, 0, 48)) : 0u;
    frame.cloudScale = settings.sky.cloudScale;
    frame.cloudOffset = settings.sky.cloudSpeed * float(time);
    frame.sunAngularRadius = settings.sky.sunAngularRadius;
    frame.sceneDepthReady = wantsDepth() ? 1.0f : 0.0f;
    frame.cameraPosition = view.position;
    frame.cloudBase = std::max(settings.sky.cloudHeight, 50.0f);
    frame.cloudTop = frame.cloudBase + std::max(settings.sky.cloudThickness, 50.0f);
    // Inside the clouds (a plane flying through them): the world itself fades into their white.
    if (settings.sky.cloudsHaveDepth && settings.sky.cloudCoverage > 0) {
        float y = view.world.columns[3].y;
        float inside = std::clamp((y - frame.cloudBase) / 80.0f, 0.0f, 1.0f) * std::clamp((frame.cloudTop - y) / 80.0f, 0.0f, 1.0f);
        if (inside > 0) {
            frame.fogDensity = std::max(frame.fogDensity, inside * settings.sky.cloudCoverage * 0.02f);
            Vec3 white = frame.skyHorizonColor * 1.2f + frame.sunColor * 0.08f;
            frame.fogColor = frame.fogColor + (white - frame.fogColor) * inside;
            frame.fogHeightFalloff = 0;
        }
    }
    frame.sunsetColors = settings.sky.sunsetColors ? 1u : 0u;
    frame.moonBrightness = settings.sky.sunsetColors ? std::max(settings.sky.moon, 0.0f) : 0.0f;
    frame.starBrightness = std::max(settings.sky.stars, 0.0f);
    frame.starTime = float(std::fmod(time, 86400.0));
    float deltaTime = lastViewTime_ >= 0 ? float(std::clamp(time - lastViewTime_, 0.0, 0.25)) : 0.0f;
    lastViewTime_ = time;
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
        (item.material->isSeenThrough() ? transparentItems : opaqueItems).push_back(&item);
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
    if (wantsTracedShadows && !hasSceneStructureThisFrame_) {
        Profiler::Scope timed(timing, "Ray tracing structure");
        hasSceneStructureThisFrame_ = buildSceneAccelerationStructure(commandBuffer, items);
    }
    tracesRaysThisFrame_ = wantsTracedShadows && hasSceneStructureThisFrame_;
    frame.shadowRayCount = tracesRaysThisFrame_ ? uint32_t(std::clamp(settings.shadowRayCount, 1, 16)) : 0u;
    frame.occlusionRayCount = tracesRaysThisFrame_ && settings.rayTracedOcclusion ? uint32_t(std::clamp(settings.occlusionRayCount, 1, 16)) : 0u;
    frame.tracesReflections = tracesRaysThisFrame_ && settings.rayTracedReflections ? 1u : 0u;
    frame.reflectionMaxRoughness = std::clamp(settings.reflectionMaxRoughness, 0.01f, 1.0f);
    frame.reflectionStrength = std::max(settings.reflectionStrength, 0.0f);
    frame.bounceIntensity = tracesRaysThisFrame_ && settings.rayTracedOcclusion && settings.rayTracedBounce ? std::max(settings.bounceIntensity, 0.0f) : 0.0f;
    // The light around things (the sky's, hidden in corners; what is bounced): traced before the
    // scene for one pixel in four and smoothed, where that pass can be drawn. The scene's pass
    // then reads it and sends no rays for it; and the screen's own ambient occlusion is not needed.
    const bool tracesAmbientFirst = frame.occlusionRayCount > 0 && wantsTracedAmbient() && prepassDepth_ && prepassNormal_ && canTraceAmbientFirst();
    frame.tracedAmbientReady = tracesAmbientFirst ? 1u : 0u;

    FrameAllocator::Allocation passUniforms = allocator().copy(
        makePassUniforms(view.view, view.projection, view.position, width, height, view.nearPlane, view.farPlane, time));
    FrameAllocator::Allocation frameUniforms = allocator().copy(frame);
    FrameAllocator::Allocation lightBuffer = allocator().copyArray(lights, size_t(lightCount));

    // What each pass costs to prepare and encode on the CPU (the GPU's time is the frame's, below).
    environment_.updateIfNeeded(commandBuffer, frame, hasCloudsWithDepth ? cloudShape_.get() : nullptr, hasCloudsWithDepth ? cloudDetail_.get() : nullptr);
    if (frame.cascadeCount > 0) {
        Profiler::Scope timed(timing, "Shadow maps");
        renderShadows(commandBuffer, casters, cascades, passUniforms, frameUniforms);
    }
    // Decals this frame: they need the depth of what is under them.
    hasDecals_ = false;
    scene.each<Decal, Transform>([&](Entity, Decal& decal, Transform& transform) {
        hasDecals_ = hasDecals_ || (transform.isActiveInHierarchy && !decal.picture.empty());
    });
    if (wantsDepth() && !prepassDepth_) ensureTargets(width, height);
    if (wantsDepth()) {
        Profiler::Scope timed(timing, "Ambient occlusion");
        renderPrepass(commandBuffer, opaque, passUniforms);
        if (settings.ambientOcclusionEnabled && !tracesAmbientFirst) renderOcclusion(commandBuffer, opaque, view, passUniforms);
    }
    tracedLightDrawn_ = nullptr;
    if (tracesAmbientFirst) {
        Profiler::Scope timed(timing, "Traced ambient light");
        tracedLightDrawn_ = renderTracedAmbient(commandBuffer, view, width, height, time, frameUniforms, frame.bounceIntensity > 0);
    }
    cloudsDrawn_ = false;
    if (hasCloudsWithDepth) {
        Profiler::Scope timed(timing, "Clouds");
        cloudsDrawn_ = renderClouds(commandBuffer, view, width, height, time, frameUniforms);
    }
    {
        Profiler::Scope timed(timing, "Scene pass");
        renderScene(commandBuffer, scene, opaque, transparent, view, passUniforms, frameUniforms, lightBuffer);
    }
    {
        Profiler::Scope timed(timing, "Bloom and final");
        renderEffects(commandBuffer, view, deltaTime);
        renderBloom(commandBuffer);
        renderFinal(commandBuffer, canvas, target, withOverlay);
    }
}

}  // namespace molten

namespace molten {

namespace {

/// A float as the 16 bits of a half float (no infinities expected: waves and foam).
uint16_t halfBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    uint32_t sign = (bits >> 16) & 0x8000u;
    int exponent = int((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) return uint16_t(sign);
    if (exponent >= 31) return uint16_t(sign | 0x7BFFu);
    return uint16_t(sign | uint32_t(exponent) << 10 | (mantissa + 0x1000u) >> 13);
}

}  // namespace

void Renderer::showOcean(const Ocean& ocean, Vec2 origin) {
    int size = ocean.resolution();
    oceanTexels_.resize(size_t(size) * size_t(size) * 4);
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            Vec3 n = ocean.normal(i, j);
            float up = std::max(n.y, 0.05f);
            uint16_t* texel = &oceanTexels_[(size_t(j) * size_t(size) + size_t(i)) * 4];
            texel[0] = halfBits(-n.x / up);
            texel[1] = halfBits(-n.z / up);
            texel[2] = halfBits(ocean.foam(i, j));
            texel[3] = halfBits(ocean.displacement(i, j).y);
        }
    }
    oceanSize_ = size;
    oceanPatch_ = ocean.settings().patchSize;
    oceanOrigin_ = origin;
    oceanFed_ = true;
}

void Renderer::prepareOcean(MTL::CommandBuffer* commandBuffer, double time) {
    // Water seen last frame and no Water component handing its sea: the renderer's own, from the wind.
    if (waterSeen_ && !oceanFed_) {
        Vec3 wind = settings.wind;
        float speed = simd::length(Vec2{wind.x, wind.z});
        if (!defaultOcean_ || simd::length(wind - defaultOceanWind_) > 0.01f) {
            OceanSettings made;
            made.resolution = 128;
            made.patchSize = 60.0f;
            made.windSpeed = speed > 0.5f ? speed : 7.0f;
            made.windDirection = speed > 0.5f ? Vec2{wind.x, wind.z} : Vec2{1.0f, 0.3f};
            defaultOcean_ = std::make_shared<Ocean>(made);
            defaultOceanWind_ = wind;
        }
        defaultOcean_->update(float(std::fmod(time, 3600.0)));
        showOcean(*defaultOcean_, Vec2{0, 0});
    }
    waterLastFrame_ = waterSeen_;
    waterSeen_ = false;
    oceanTexture_ = nullptr;
    if (!oceanFed_ || oceanSize_ < 2) return;
    oceanFed_ = false;
    // One picture per frame in flight: the GPU may still read the one of a frame ago.
    NS::SharedPtr<MTL::Texture>& texture = oceanTextures_[frameIndex_];
    if (!texture || int(texture->width()) != oceanSize_) {
        auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
        descriptor->setTextureType(MTL::TextureType2D);
        descriptor->setPixelFormat(MTL::PixelFormatRGBA16Float);
        descriptor->setWidth(NS::UInteger(oceanSize_));
        descriptor->setHeight(NS::UInteger(oceanSize_));
        int levels = 1;
        while ((1 << levels) <= oceanSize_) levels++;
        descriptor->setMipmapLevelCount(NS::UInteger(levels));
        descriptor->setUsage(MTL::TextureUsageShaderRead);
        descriptor->setStorageMode(MTL::StorageModeShared);
        texture = NS::TransferPtr(device_.device()->newTexture(descriptor.get()));
        if (!texture) return;
    }
    texture->replaceRegion(MTL::Region::Make2D(0, 0, NS::UInteger(oceanSize_), NS::UInteger(oceanSize_)), 0, oceanTexels_.data(),
                           NS::UInteger(oceanSize_) * 8);
    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    blit->generateMipmaps(texture.get());
    blit->endEncoding();
    oceanTexture_ = texture.get();
}

}  // namespace molten
