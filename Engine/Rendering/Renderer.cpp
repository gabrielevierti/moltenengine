#include "Rendering/Renderer.hpp"
#include "Rendering/Quality.hpp"
#include "Water/Waves.hpp"

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
    gpuTimer_ = std::make_unique<GpuTimer>(device.device());
    setLightProbes(BakedLighting{});
}

void Renderer::setLightProbes(const BakedLighting& baked) {
    probeVolumes_ = gpu::ProbeVolumes{};
    std::vector<Vec4> cells;
    for (const ProbeGrid& grid : baked.grids) {
        if (probeVolumes_.count >= gpu::MMMaxProbeVolumes) break;
        if (grid.cells.size() != grid.probeCount() * 6 || grid.probeCount() == 0) continue;
        gpu::ProbeVolume& volume = probeVolumes_.volumes[probeVolumes_.count++];
        Vec3 far = grid.farCorner();
        volume.origin = Vec4{grid.origin.x, grid.origin.y, grid.origin.z, grid.spacing};
        volume.farCorner = Vec4{far.x, far.y, far.z, grid.fade};
        volume.counts = Vec4{float(grid.countX), float(grid.countY), float(grid.countZ), float(cells.size() / 6)};
        cells.insert(cells.end(), grid.cells.begin(), grid.cells.end());
    }
    lightProbeCount_ = cells.size() / 6;
    if (cells.empty()) cells.assign(6, Vec4{0, 0, 0, 1});
    probes_ = NS::TransferPtr(device_.device()->newBuffer(cells.data(), cells.size() * sizeof(Vec4), MTL::ResourceStorageModeShared));
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
    // With the sun's shadow traced, the maps are only for the light shafts (two slices, near the
    // camera, blurred by the shafts' own steps): a quarter of the texels of a 4096 map is plenty.
    if (settings.shadowsEnabled && settings.rayTracedShadows && canTraceRays()) size = std::min(size, 2048), slices = std::min(slices, 2);
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
                item.previousWorld = previousPlace((uint64_t(entity.index) << 32 | entity.generation) ^ (uint64_t(&place - copies->places.data() + 1) << 44), item.world);
                items.push_back(item);
            }
            return;
        }
        DrawItem item;
        item.mesh = renderer.mesh.get();
        item.material = renderer.material ? renderer.material.get() : &defaultMaterial_;
        item.world = transform.world;
        item.tint = renderer.tint;
        item.bounds = renderer.mesh->bounds().transformed(transform.world);
        item.distanceSquared = simd::length_squared(item.bounds.center() - view.position);
        item.castsShadows = renderer.castsShadows;
        item.previousWorld = previousPlace(uint64_t(entity.index) << 32 | entity.generation, item.world);
        items.push_back(item);
    });
    // The scene's sea: the sheet round the eye at its level, a square far larger than anything
    // seen, so that its rim never comes in sight. Both sides: from under it, it is its underside.
    if (settings.sea.enabled) {
        const SeaSettings& sea = settings.sea;
        sea_.isWater = true;
        sea_.isSea = true;
        sea_.isDoubleSided = true;
        sea_.baseColor = Vec4{sea.color.x, sea.color.y, sea.color.z, std::clamp(3.0f / std::max(sea.clarity, 0.2f), 0.1f, 1.0f)};
        sea_.roughness = 0.05f;
        sea_.waveSize = 1.0f;
        sea_.waveHeight = 1.0f;                   // the wind's and the weather's waves, as they are
        sea_.foam = std::max(sea.foam, 0.0f);
        sea_.alphaMode = AlphaMode::Blend;        // where the GPU cannot read what is behind it: seen through all the same
        constexpr float kAcross = 200000.0f;
        DrawItem item;
        item.mesh = waterSheet();
        item.material = &sea_;
        item.world = translation(Vec3{view.position.x, sea.level, view.position.z}) * scaling(Vec3{kAcross, 1, kAcross});
        item.tint = Vec4{1, 1, 1, 1};
        item.bounds = AABB(Vec3{view.position.x - 20000.0f, sea.level - 40.0f, view.position.z - 20000.0f},
                           Vec3{view.position.x + 20000.0f, sea.level + 40.0f, view.position.z + 20000.0f});
        item.distanceSquared = 1e12f;                // the farthest of what is seen through: drawn first of them
        item.castsShadows = false;
        items.push_back(item);
    }
}

const Mesh* Renderer::waterSheet() const {
    if (!waterSheet_) waterSheet_ = std::make_unique<Mesh>(device_, waterSheetMesh());
    return waterSheet_.get();
}

int Renderer::collectLights(Scene& scene, gpu::LightData* lights, gpu::FrameUniforms& frame, bool& sunCastsShadows) const {
    bool hasSun = false;
    gpu::LightData sun{};
    std::vector<gpu::LightData> others;
    sunCastsShadows = false;
    cookiesWanted_.clear();
    hasLightsInAir_ = false;

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
        data.right = safeNormalize(xyz(transform.world.columns[0]), Vec3{1, 0, 0});              // the entity's X axis
        if (light.type == LightType::Spot && !light.cookie.empty()) {
            // The same picture for many lights is one slice.
            auto found = std::find(cookiesWanted_.begin(), cookiesWanted_.end(), light.cookie);
            if (found == cookiesWanted_.end() && cookiesWanted_.size() < size_t(gpu::MMMaxCookies)) found = cookiesWanted_.insert(cookiesWanted_.end(), light.cookie);
            if (found != cookiesWanted_.end()) data.cookie = float(found - cookiesWanted_.begin() + 1);
        }
        // Asked for: whether it gets one is chosen with the others (chooseLightShadows).
        if (light.castsShadows && (light.type == LightType::Point || light.type == LightType::Spot)) data.shadow = -1.0f;
        if (settings.volumetricLights && light.isVolumetric && (light.type == LightType::Point || light.type == LightType::Spot) && light.volumetricStrength > 0) {
            data.volumetric = light.volumetricStrength;
            hasLightsInAir_ = true;
        }

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
        sunAtNoon_ = sun.color;
        frame.sunDirection = sun.direction;
        frame.skySunDirection = sun.direction;
        frame.sunColor = sun.color;
        if (settings.sky.sunsetColors) {
            // Low in the sky, the sun's light crosses more air: gold, then orange and red, then none.
            frame.sunColor = sun.color * sunlightThroughAir(-sun.direction.y);
            // Once it has set, the moon across the sky from it lights the scene: cold, dim, with its own shadows.
            // Only once the sun's last light has gone (it is gone at -0.08): where the two are lit,
            // the light that wins changes side, and the clouds that were lit from the west by the
            // sun below them would be lit at once from the east. In the dusk between, neither lights
            // much, and the change is not seen.
            float night = std::clamp((-0.07f - (-sun.direction.y)) / 0.13f, 0.0f, 1.0f);
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
        sunAtNoon_ = Vec3{0, 0, 0};
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
            instances.push_back(gpu::InstanceData{items[next]->world, normalMatrix(items[next]->world), items[next]->tint, items[next]->previousWorld});
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

Mat4 Renderer::previousPlace(uint64_t key, const Mat4& now) const {
    auto found = motion_.find(key);
    if (found == motion_.end()) {
        motion_.emplace(key, Motion{now, now, frameNumber_});
        return now;
    }
    Motion& motion = found->second;
    if (motion.frame != frameNumber_) {
        // A new frame: what it was is where it was the last time (if that was the frame just gone).
        motion.last = motion.frame + 1 == frameNumber_ ? motion.now : now;
        motion.now = now;
        motion.frame = frameNumber_;
    }
    return motion.last;
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
    uniforms.wind = windWithWeather(settings);
    uniforms.windGust = std::clamp(gustsWithWeather(settings), 0.0f, 1.0f);
    uniforms.previousViewProjection = uniforms.viewProjection;
    uniforms.deltaTime = frameDelta_;
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

void Renderer::chooseLightShadows(gpu::LightData* lights, int count, const View& view) {
    lightShadowViewCount_ = 0;
    struct Wanted {
        int index;
        float weight;
        int views;
    };
    std::vector<Wanted> wanted;
    Frustum seen(view.viewProjection);
    for (int i = 0; i < count; i++) {
        if (lights[i].shadow >= 0.0f) continue;
        lights[i].shadow = 0.0f;
        if (!settings.shadowsEnabled || settings.shadowedLights <= 0) continue;
        // Only if its light reaches something seen.
        if (!seen.intersects(AABB::fromCenter(lights[i].position, Vec3{1, 1, 1} * lights[i].range))) continue;
        float away = std::max(simd::length(lights[i].position - view.position) - lights[i].range, 0.0f);
        Vec3 c = lights[i].color;
        float bright = c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f;
        wanted.push_back(Wanted{i, bright * lights[i].range * lights[i].range / (1.0f + away * away),
                                lights[i].type == gpu::MMLightPoint ? 6 : 1});
    }
    std::sort(wanted.begin(), wanted.end(), [](const Wanted& a, const Wanted& b) { return a.weight > b.weight; });
    std::vector<const Wanted*> chosen;
    int views = 0;
    for (const Wanted& light : wanted) {
        if (int(chosen.size()) >= settings.shadowedLights) break;
        if (views + light.views > int(gpu::MMMaxLightShadowViews)) continue;
        chosen.push_back(&light);
        views += light.views;
    }
    if (views == 0) return;

    // The atlas: squares in rows, as sharp as asked while they fit in 4096 texels.
    int columns = int(std::ceil(std::sqrt(double(views))));
    int tile = std::clamp(settings.lightShadowResolution, 64, 2048);
    tile = std::min(tile, 4096 / columns);
    int rows = (views + columns - 1) / columns;
    int width = columns * tile, height = rows * tile;
    if (!lightShadowAtlas_ || int(lightShadowAtlas_->width()) < width || int(lightShadowAtlas_->height()) < height) {
        // Made larger when more is needed, never smaller (no new texture each time a light comes and goes).
        int w = std::max(width, lightShadowAtlas_ ? int(lightShadowAtlas_->width()) : 0);
        int h = std::max(height, lightShadowAtlas_ ? int(lightShadowAtlas_->height()) : 0);
        auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::texture2DDescriptor(kDepthFormat, NS::UInteger(w), NS::UInteger(h), false));
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        descriptor->setStorageMode(MTL::StorageModePrivate);
        lightShadowAtlas_ = NS::TransferPtr(device_.device()->newTexture(descriptor.get()));
        if (!lightShadowAtlas_) return;
        lightShadowAtlas_->setLabel(NS::String::string("Light shadow maps", NS::UTF8StringEncoding));
    }
    lightShadowTile_ = tile;
    const float atlasWidth = float(lightShadowAtlas_->width()), atlasHeight = float(lightShadowAtlas_->height());
    auto add = [&](const Mat4& viewProjection) {
        int n = lightShadowViewCount_++;
        lightShadowViews_.viewProjection[n] = viewProjection;
        lightShadowViews_.tile[n] = Vec4{float((n % columns) * tile) / atlasWidth, float((n / columns) * tile) / atlasHeight,
                                         float(tile) / atlasWidth, float(tile) / atlasHeight};
    };
    for (const Wanted* light : chosen) {
        gpu::LightData& data = lights[light->index];
        data.shadow = float(lightShadowViewCount_ + 1);
        float range = std::max(data.range, 0.01f);
        float nearPlane = std::clamp(range * 0.002f, 0.02f, 0.2f);
        if (data.type == gpu::MMLightSpot) {
            // Through its cone, a little wider, so that its edge is soft inside the map.
            float cosOuter = std::clamp(-data.spotOffset / data.spotScale, 0.05f, 0.9999f);
            float fov = std::min(2.0f * std::acos(cosOuter) + radians(4.0f), radians(170.0f));
            Vec3 up = std::abs(data.direction.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            add(perspective(fov, 1.0f, nearPlane, range) * lookAt(data.position, data.position + data.direction, up));
            data.shadowTexel = 2.0f * std::tan(fov * 0.5f) / float(tile) * std::max(settings.shadowNormalOffset, 0.0f) * 0.5f;      // (Lighting > Shadows > Normal Offset)
        } else {
            // Six faces of a cube, each a little wider than a quarter turn: its corners meet the next.
            float widen = 1.0f + 3.0f / float(tile);
            float fov = 2.0f * std::atan(widen);
            const Vec3 faces[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const Vec3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
            for (int f = 0; f < 6; f++) add(perspective(fov, 1.0f, nearPlane, range) * lookAt(data.position, data.position + faces[f], ups[f]));
            data.shadowTexel = 2.0f * widen / float(tile) * std::max(settings.shadowNormalOffset, 0.0f) * 0.5f;
        }
    }
    stats.shadowedLights = int(chosen.size());
}

/// Builds, for this frame, the structure rays are traced against: one entry per opaque object
/// (those that cast no shadow only seen by the rays that do not look for shadows), each pointing at its mesh's own structure with the object's
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
        // Glass and other blended materials let light through: rays go through them. What casts no
        // shadow is still seen (in reflections, by the light it bounces): only shadow rays pass it.
        if (!meshStructure || item.material->isSeenThrough() || item.mesh == waterSheet_.get()) continue;
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
        instance.mask = item.castsShadows ? uint32_t(gpu::MMRayMaskShadow | gpu::MMRayMaskSeen) : uint32_t(gpu::MMRayMaskSeen);
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

// ---------------------------------------------------------------- meshes that move

void Renderer::skin(const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<const SkinSource>& source, std::vector<Mat4> palette) {
    if (!mesh || !source || palette.size() < source->paletteSize) return;
    // Asked twice in a frame (the editor's views, a second Animator update): the last pose wins.
    for (SkinJob& job : skinJobs_) {
        if (job.mesh == mesh) {
            job.palette = std::move(palette);
            return;
        }
    }
    skinJobs_.push_back(SkinJob{mesh, source, std::move(palette)});
}

void Renderer::meshMoved(const std::shared_ptr<Mesh>& mesh) {
    if (mesh && device_.supportsRayTracing() && std::find(movedMeshes_.begin(), movedMeshes_.end(), mesh) == movedMeshes_.end()) movedMeshes_.push_back(mesh);
}

/// Each skinned mesh's vertices, as its bones have them this frame, written by the GPU into the
/// mesh's own buffer: one dispatch per mesh, a thread per vertex. The palettes go in this frame's
/// memory; everything drawn later in the frame reads the posed vertices.
void Renderer::poseSkinnedMeshes(MTL::CommandBuffer* commandBuffer) {
    if (skinJobs_.empty()) return;
    MTL::ComputePipelineState* pipeline = shaders_.computePipeline("skin_vertices");
    if (!pipeline) {
        // No kernel (a library from before it): the models stay as they were made this frame,
        // and are posed on the processor from the next.
        canSkin_ = false;
        logWarning("no skin_vertices kernel: models with bones are posed on the processor");
        skinJobs_.clear();
        return;
    }
    Profiler::Scope timed(profilerOrIdle(), "Posing models with bones");
    MTL::ComputeCommandEncoder* encoder = commandBuffer->computeCommandEncoder();
    encoder->setLabel(NS::String::string("Skinning", NS::UTF8StringEncoding));
    encoder->setComputePipelineState(pipeline);
    const NS::UInteger group = std::min<NS::UInteger>(pipeline->maxTotalThreadsPerThreadgroup(), 64);
    for (const SkinJob& job : skinJobs_) {
        const SkinSource& source = *job.source;
        MTL::Buffer* target = job.mesh->vertexBuffer();
        if (!target || !source.restBuffer || source.vertexCount == 0 || target->length() < source.rest.size() * sizeof(float)) continue;
        FrameAllocator::Allocation palette = allocator().copyArray(job.palette.data(), job.palette.size());
        const uint32_t count = source.vertexCount;
        encoder->setBuffer(static_cast<MTL::Buffer*>(source.restBuffer.get()), 0, 0);
        encoder->setBuffer(static_cast<MTL::Buffer*>(source.jointBuffer.get()), 0, 1);
        encoder->setBuffer(static_cast<MTL::Buffer*>(source.weightBuffer.get()), 0, 2);
        encoder->setBuffer(palette.buffer, palette.offset, 3);
        encoder->setBuffer(target, 0, 4);
        encoder->setBytes(&count, sizeof(count), 5);
        encoder->dispatchThreadgroups(MTL::Size((count + group - 1) / group, 1, 1), MTL::Size(group, 1, 1));
        meshMoved(job.mesh);
    }
    encoder->endEncoding();
    skinJobs_.clear();
}

/// The ray-tracing structures of the meshes that moved, fitted to their vertices as they are now:
/// after the skinning above (the same command buffer runs in order), before any ray is traced.
void Renderer::refitMovedMeshes(MTL::CommandBuffer* commandBuffer) {
    if (movedMeshes_.empty()) return;
    MTL::AccelerationStructureCommandEncoder* encoder = commandBuffer->accelerationStructureCommandEncoder();
    encoder->setLabel(NS::String::string("Refitting what moved", NS::UTF8StringEncoding));
    for (const std::shared_ptr<Mesh>& mesh : movedMeshes_) mesh->refit(encoder);
    encoder->endEncoding();
    movedMeshes_.clear();
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

void Renderer::updateLensWater(const View& view, const gpu::FrameUniforms& frame, const RenderSettings& settings, double) {
    // On the clock of the screen, not of the scene: the glass dries while the scene stands still too.
    const double time = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    lensWater_ = lensSea_ = lensMore_ = Vec4{0, 0, 0, 0};
    const bool hasSea = settings.sea.enabled && settings.sea.lensThroughSurface;
    const int isUnder = hasSea && frame.cameraUnderwater > 0.0f ? 1 : 0;
    // Gone through since the last picture of this view (not a cut: a view drawn again within a moment).
    const bool isFollowing = lensWasUnder_ >= 0 && time - lensSeenAt_ < 0.5 && time >= lensSeenAt_;
    if (hasSea && isFollowing && isUnder != lensWasUnder_) (isUnder ? lensPlungedAt_ : lensSurfacedAt_) = time;
    lensWasUnder_ = hasSea ? isUnder : -1;
    lensSeenAt_ = time;
    if (!hasSea) return;
    // The glass is taken as this tall, in meters, where the surface crosses it: as the camera goes
    // through, the water's edge sweeps across the whole picture instead of flicking past.
    const float glass = 0.35f;
    Vec3 right = safeNormalize(xyz(view.world.columns[0]), Vec3{1, 0, 0});
    Vec3 up = safeNormalize(xyz(view.world.columns[1]), Vec3{0, 1, 0});
    float aspect = std::max(view.aspect, 0.1f);
    float above = -frame.cameraUnderwater;              // the middle of the glass over the water, meters
    // Each place of the glass (u across, v down, 0 to 1) is that far over the water: x u + y v + z.
    Vec3 line{glass * aspect * right.y, -glass * up.y, 0.0f};
    line.z = above - 0.5f * (line.x + line.y);
    float reach = std::abs(line.x) + std::abs(line.y);
    if (std::abs(above) < reach * 0.5f + 0.03f) lensWater_ = Vec4{line.x, line.y, line.z, isUnder ? 2.0f : 1.0f};
    Vec3 taken = settings.sea.absorption();
    float wet = time - lensSurfacedAt_ < 8.0 && !isUnder ? float(std::exp(-(time - lensSurfacedAt_) / 1.8)) : 0.0f;
    lensSea_ = Vec4{taken.x, taken.y, taken.z, wet};
    float shake = time - lensPlungedAt_ < 3.0 ? float(std::exp(-(time - lensPlungedAt_) / 0.35)) : 0.0f;
    lensMore_ = Vec4{shake, 0, 0, 0};
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
    std::swap(kept.cloudFresh, cloudFresh_);
    std::swap(kept.cloudColorBefore, cloudColorBefore_);
    std::swap(kept.cloudViewProjectionBefore, cloudViewProjectionBefore_);
    std::swap(kept.cloudHasHistory, cloudHasHistory_);
    std::swap(kept.cloudTurn, cloudTurn_);
    std::swap(kept.lensWasUnder, lensWasUnder_);
    std::swap(kept.lensSurfacedAt, lensSurfacedAt_);
    std::swap(kept.lensPlungedAt, lensPlungedAt_);
    std::swap(kept.lensSeenAt, lensSeenAt_);
    std::swap(kept.tracedLight[0], tracedLight_[0]);
    std::swap(kept.tracedLight[1], tracedLight_[1]);
    std::swap(kept.tracedSurface, tracedSurface_);
    std::swap(kept.tracedLightGathered, tracedLightGathered_);
    std::swap(kept.tracedLightGatheredBefore, tracedLightGatheredBefore_);
    std::swap(kept.tracedFrames, tracedFrames_);
    std::swap(kept.tracedFramesBefore, tracedFramesBefore_);
    std::swap(kept.tracedSurfaceBefore, tracedSurfaceBefore_);
    std::swap(kept.tracedViewProjectionBefore, tracedViewProjectionBefore_);
    std::swap(kept.tracedViewBefore, tracedViewBefore_);
    std::swap(kept.tracedHasHistory, tracedHasHistory_);
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
    if (gpuTimer_) gpuTimer_->beginFrame(frameIndex_);
    frameNumber_++;
    frameDelta_ = lastFrameTime_ >= 0 ? float(std::clamp(time - lastFrameTime_, 0.0, 0.25)) : 0.0f;
    lastFrameTime_ = time;
    // Things not drawn for a while are forgotten.
    if (frameNumber_ % 120 == 0) {
        for (auto it = motion_.begin(); it != motion_.end();) it = it->second.frame + 2 < frameNumber_ ? motion_.erase(it) : std::next(it);
    }
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
    // Before anything is drawn: the models with bones posed, and what moved refitted for the rays.
    poseSkinnedMeshes(commandBuffer);
    refitMovedMeshes(commandBuffer);
    takeCloudNoise();
    waterLastFrame_ = waterSeen_;
    waterSeen_ = false;

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
            isEditorCamera_ = wanted.isEditorCamera;
            swapTargets(i + 1);
            renderView(commandBuffer, scene, int(i) == canvasView ? canvas : noCanvas, viewTarget, time, false);
            swapTargets(i + 1);
            viewStats_[i] = stats.since(before);
        }
        cameraOverride = cameraBefore;
        sceneViewport = viewportBefore;
        drawsDebugLines_ = true;
        isEditorCamera_ = false;
        bool canvasIsInAView = canvasView >= 0 && size_t(canvasView) < views.size() && views[size_t(canvasView)].width > 0;
        Profiler::Scope timed(profilerOrIdle(), "Windows over the views");
        renderInterface(commandBuffer, canvasIsInAView ? noCanvas : canvas, target);
    }
    debug.clear();

    if (target.drawable) commandBuffer->presentDrawable(target.drawable);
    if (gpuTimer_) gpuTimer_->endFrame(commandBuffer);        // (read before the slot is given back below)
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
    // And, for the editor's own camera, none of what a camera's lens does to the picture.
    struct QualityScope {
        RenderSettings& settings;
        RenderSettings saved;
        bool isOn;
        QualityScope(RenderSettings& s, const GameQuality* quality, bool isEditorCamera) : settings(s), saved(s), isOn(quality != nullptr || isEditorCamera) {
            if (quality) quality->applyTo(settings);
            if (isEditorCamera) {
                settings.lensDistortion = settings.chromaticAberration = settings.filmGrain = settings.interference = 0.0f;
                settings.vignette = 0.0f;
                settings.lensDirt.clear();
                settings.depthOfField = settings.motionBlur = false;
                settings.sea.lensThroughSurface = false;
            }
        }
        ~QualityScope() {
            if (isOn) settings = saved;
        }
    } qualityScope(settings, quality, isEditorCamera_);

    Rect area = sceneRect(target.texture);
    float scale = settings.renderScale * dynamicScale_ * (settings.followsDeviceHeat ? heatScale : 1.0f);
    int width = std::max(1, int(float(area.width) * scale));
    int height = std::max(1, int(float(area.height) * scale));
    outputWidth_ = std::max(1, int(area.width));
    outputHeight_ = std::max(1, int(area.height));
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
    updateCookies(commandBuffer);
    chooseLightShadows(lights, lightCount, view);
    // The weather (Kit/Weather.hpp): rain and storms cloud the sky over and hide the sun behind
    // it; lightning lights the clouds from inside, and the world with them, for a moment.
    const float clouded = settings.weather.clouded();
    const float flash = std::clamp(settings.weather.flash, 0.0f, 1.0f);
    if (clouded > 0 && lightCount > 0 && simd::length(sunAtNoon_) > 0) {           // a sun to hide (it is light 0)
        float dimmed = 1.0f - 0.8f * clouded;
        frame.sunColor = frame.sunColor * dimmed;
        lights[0].color = lights[0].color * dimmed;
    }
    frame.lightning = flash;
    // Where the stroke is: the clouds glow round it, and its bolt (if it has one) goes down from them there.
    frame.lightningX = settings.weather.strikePlace.x;
    frame.lightningZ = settings.weather.strikePlace.z;
    frame.lightningGround = settings.weather.strikePlace.y;
    frame.lightningSeed = flash > 0 ? std::max(settings.weather.bolt, 0.0f) : 0.0f;
    // The world is lit by it as much as it is near: a stroke overhead lights everything, one
    // on the horizon only the sky that way.
    const float strikeAway = std::hypot(settings.weather.strikePlace.x - view.position.x, settings.weather.strikePlace.z - view.position.z);
    const float flashLight = flash * (0.25f + 0.75f / (1.0f + (strikeAway / 1800.0f) * (strikeAway / 1800.0f)));
    frame.lightCount = uint32_t(lightCount);
    stats.lights = std::max(stats.lights, lightCount);

    // Ray-traced sun shadows replace the shadow maps where they are available and asked for.
    bool wantsTracedShadows = settings.shadowsEnabled && sunCastsShadows && settings.rayTracedShadows && canTraceRays();

    Cascades cascades;
    std::vector<ShadowCaster> casters;
    // With the sun's shadow traced, shadow maps are still drawn for the light shafts, which walk
    // through the air in many steps and look up each: a ray for each would cost far too much.
    // Two slices, near the camera: the shafts are seen nearby, and blurred by their own steps.
    const bool shaftsWantMaps = wantsTracedShadows && settings.fogShaftSteps > 0 && settings.fogDensity > 0;
    CascadeSettings cascadeSettings = settings.cascades;
    if (shaftsWantMaps) {
        cascadeSettings.count = std::min(cascadeSettings.count, 2);
        cascadeSettings.shadowDistance = std::min(cascadeSettings.shadowDistance, 150.0f);
    }
    if (settings.shadowsEnabled && sunCastsShadows && (!wantsTracedShadows || shaftsWantMaps)) {
        cascades = computeCascades(view.world, view.fovY, view.aspect, view.nearPlane, frame.sunDirection, cascadeSettings);
        casters = findShadowCasters(items, cascades);
        // Nothing whose shadow reaches the view: no maps to draw, and none for the scene to read.
        if (casters.empty()) cascades.count = 0;
        frame.cascadeCount = uint32_t(cascades.count);
        for (int i = 0; i < cascades.count; i++) {
            frame.cascadeViewProjection[i] = cascades.viewProjection[i];
            frame.cascadeSplits[i] = cascades.splitFar[i];
            frame.cascadeTexelSize[i] = 2.0f * cascades.radius[i] / float(std::max(shadowMapSize_, 1));
        }
    }
    frame.shadowBias = settings.shadowBias;
    frame.shadowNormalOffset = settings.shadowNormalOffset;
    frame.shadowMapSize = float(shadowMapSize_);
    frame.shadowDistance = cascadeSettings.shadowDistance;
    frame.environmentIntensity = settings.environmentIntensity * (1.0f - 0.4f * clouded) * (1.0f + 2.5f * flashLight);
    frame.prefilteredMipCount = environment_.prefilteredMipCount();
    frame.ambientOcclusionStrength = settings.ambientOcclusionEnabled ? 1.0f : 0.0f;
    frame.skyZenithColor = settings.sky.zenithColor;
    frame.skyHorizonColor = settings.sky.horizonColor;
    frame.skyGroundColor = settings.sky.groundColor;
    frame.skyIntensity = settings.sky.intensity * (1.0f - 0.45f * clouded) * (1.0f + 1.5f * flashLight);
    frame.sunDiskIntensity = settings.sky.sunDiskIntensity;
    frame.sunDiskSize = std::cos(settings.sky.sunAngularRadius);
    frame.fogColor = settings.fogColor;
    // The air is lit as the rest is: by the sun, white by day, gold and red as it sets, dark at
    // night but for the moon. The fog's color is its color at noon.
    if (settings.fogLitBySun && settings.sky.sunsetColors) frame.fogColor = settings.fogColor * fogDaylight(sunAtNoon_, frame.sunColor);
    frame.fogDensity = settings.fogDensity + 0.004f * settings.weather.rain + 0.003f * settings.weather.snow;     // the air thickens in the rain, and the snow
    frame.fogHeightFalloff = std::max(settings.fogHeightFalloff, 0.0f);
    frame.fogSunScatter = std::max(settings.fogSunScatter, 0.0f);
    // Rain falls from a sky full of cloud: as much as it rains, at least.
    frame.cloudCoverage = clouded > 0 ? std::max(settings.sky.cloudCoverage, 0.5f + 0.5f * clouded) : settings.sky.cloudCoverage;
    // Clouds with depth, in so many steps; none where they cannot be walked (the noise is still
    // being made, the shaders are without the extras): the flat layer then.
    const bool hasCloudsWithDepth = marchesClouds();
    frame.volumetricClouds = hasCloudsWithDepth ? uint32_t(std::clamp(settings.sky.cloudSteps, 16, 128)) : 0u;
    frame.fogShaftSteps = settings.fogDensity > 0 ? uint32_t(std::clamp(settings.fogShaftSteps, 0, 48)) : 0u;
    frame.cloudScale = settings.sky.cloudScale;
    // The clouds drift as their own setting says, and with the scene's wind, which blows harder
    // up where they are (about twice as hard as near the ground): carried along it, and drawn out
    // along it. The offset is in units of 2.4 km, the clouds' own.
    const Vec3 blowing = windWithWeather(settings);
    frame.cloudWind = Vec2{blowing.x, blowing.z} * 2.0f;
    // Carried a step each frame, as far as the wind blows now: a wind that rises with the weather
    // speeds them up from where they are, instead of putting them where a harder wind would have
    // carried them from the start.
    // Their own Drift the same way: changing either moves them on at the new speed from where they
    // are (a speed times the whole time since the start would send them racing across the sky
    // while the value is being dragged).
    if (cloudDriftTime_ >= 0.0 && time > cloudDriftTime_) {
        cloudDrift_ += (frame.cloudWind / 2400.0f + settings.sky.cloudSpeed) * float(std::min(time - cloudDriftTime_, 0.5));
    }
    if (time != cloudDriftTime_) cloudDriftTime_ = time;
    cloudDrift_ = Vec2{std::fmod(cloudDrift_.x, 1000.0f), std::fmod(cloudDrift_.y, 1000.0f)};
    frame.cloudOffset = cloudDrift_;
    // The traced shadows' softness: the sun as wide as it is drawn, up to a little over twice the
    // real one (0.27 degrees across its radius). A sun drawn large for looks would otherwise
    // blur every shadow of the scene into a smudge.
    frame.sunAngularRadius = std::min(settings.sky.sunAngularRadius, 0.0105f);
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
    // The sun's shadow, and the light around things (the sky's, hidden in corners; what is
    // bounced): traced before the scene for one pixel in four, gathered over frames and smoothed,
    // where that pass can be drawn. The scene's pass then reads them and sends no rays for them;
    // and where the light around things is traced, the screen's own ambient occlusion is not needed.
    frame.frameIndex = pictureCount_++;
    const bool tracesAmbientFirst = tracesRaysThisFrame_ && frame.occlusionRayCount > 0 && wantsTracedAmbient() && prepassDepth_ && prepassNormal_ &&
                                    canTraceAmbientFirst();
    const bool tracesLightFirst = tracesAmbientFirst;
    frame.tracedAmbientReady = tracesAmbientFirst ? 1u : 0u;
    // The sun's shadow is traced in the scene's pass, from the exact place of each pixel, the same
    // rays every frame: sharp where things touch, soft away from them, and still while the camera moves.
    frame.tracedSunReady = 0u;

    // While it rains, what is out in it gets wet: the scene round the camera seen from above
    // says what is under a roof. A square of so many meters, its middle where the camera is,
    // moved by whole texels so that its edges do not crawl as the camera moves.
    rainMapDrawn_ = false;
    frame.wetness = 0.0f;
    frame.snow = Vec4{0, 0, 0, 0};
    Mat4 rainViewProjection = identity();
    // (The same map says where settled snow lies: not under a roof either.)
    const float snowCover = settings.weather.snowSettles ? std::clamp(settings.weather.snowCover, 0.0f, 1.0f) : 0.0f;
    if (settings.weather.rain > 0.0f || snowCover > 0.0f) {
        constexpr float half = 40.0f, texel = 2.0f * half / float(kRainMapSize);
        Vec3 middle{std::round(view.position.x / texel) * texel, view.position.y, std::round(view.position.z / texel) * texel};
        Vec3 above = middle + Vec3{0, 200.0f, 0};
        rainViewProjection = orthographic(-half, half, -half, half, 1.0f, 400.0f) * lookAt(above, middle, Vec3{0, 0, -1});
        if (renderRainMap(commandBuffer, items, rainViewProjection)) {
            rainMapDrawn_ = true;
            frame.wetness = std::clamp(settings.weather.rain, 0.0f, 1.0f);
            frame.snow = Vec4{snowCover, 0, 0, 0};
        }
    }
    frame.rainViewProjection = rainViewProjection;
    // The sea: its level and its water, and how far under its waves the camera is.
    frame.cameraUnderwater = -1.0f;
    if (settings.sea.enabled) {
        Vec3 taken = settings.sea.absorption();
        frame.seaAbsorb = Vec4{taken.x, taken.y, taken.z, 1.0f};
        frame.seaColor = Vec4{settings.sea.color.x, settings.sea.color.y, settings.sea.color.z, settings.sea.level};
        const SeaSettings& look = settings.sea;
        frame.seaShallow = Vec4{look.shallowColor.x, look.shallowColor.y, look.shallowColor.z, std::max(look.clarity, 0.2f)};
        frame.seaLook = Vec4{std::max(look.scattering, 0.0f), std::max(look.reflections, 0.0f), std::max(look.ripples, 0.0f), std::max(look.caustics, 0.0f)};
        frame.seaFoam = Vec4{std::max(look.foam, 0.0f), std::max(look.whitecaps, 0.0f), 0.0f, 0.0f};
        WaterSea sea;
        sea.wind = windWithWeather(settings);
        float surface = settings.sea.level + sea.heightAt(Vec2{view.position.x, view.position.z}, float(time));
        frame.cameraUnderwater = surface - view.position.y;
    }
    updateLensWater(view, frame, settings, time);

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
    if (lightShadowViewCount_ > 0) {
        Profiler::Scope timed(timing, "Light shadow maps");
        renderLightShadows(commandBuffer, items);
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
    tracedAmbientDrawn_ = false;
    if (tracesLightFirst) {
        Profiler::Scope timed(timing, "Traced light");
        tracedLightDrawn_ = renderTracedAmbient(commandBuffer, view, width, height, time, frameUniforms, frame.bounceIntensity > 0);
        tracedAmbientDrawn_ = tracedLightDrawn_ && tracesAmbientFirst;
    }
    cloudsDrawn_ = false;
    if (hasCloudsWithDepth) {
        Profiler::Scope timed(timing, "Clouds");
        cloudsDrawn_ = renderClouds(commandBuffer, view, width, height, time, frameUniforms);
    }
    if (!cloudsDrawn_) cloudHasHistory_ = false;        // what was kept is of another sky by the time they are back
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
