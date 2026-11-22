// Renderer.hpp
// Draws a Scene.
//
// Every frame goes through the same sequence of passes:
//
//   1. shadow maps     the scene seen from the sun, once per cascade        (depth only)
//   2. depth pre-pass  depth and normals of what the camera sees            (only if SSAO is on)
//   3. SSAO            how hidden each pixel is from the sky, then a blur
//   4. main pass       opaque objects, sky, transparent objects, particles, debug lines
//                      into a multisampled, floating-point (HDR) target
//   5. bloom           bright areas blurred through a chain of smaller and smaller textures
//   6. final pass      tone mapping onto the screen, then the 2D canvas on top
//
// Before any of that, the renderer walks the scene once and builds plain lists of what
// to draw ("extraction"): it finds the camera and the lights, drops objects outside the
// view, sorts, and groups objects that share a mesh and a material into instanced draws.
#pragma once

#include "Core/Profiler.hpp"
#include <MetalFX/MetalFX.hpp>
#include "Rendering/Canvas.hpp"
#include "Rendering/Cascades.hpp"
#include "Rendering/CloudNoise.hpp"
#include "Rendering/DebugDraw.hpp"
#include "Rendering/Environment.hpp"
#include "Rendering/FrameAllocator.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/SurfaceShader.hpp"
#include "Scene/Scene.hpp"

#include <dispatch/dispatch.h>

#include <atomic>

#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace molten {

/// The colors are radiance, in the same units as light intensities. A clear sky is several
/// times darker than a white wall in sunlight; keeping that ratio is what makes shadows read.
struct SkySettings {
    Vec3 zenithColor{0.05f, 0.15f, 0.42f};
    Vec3 horizonColor{0.38f, 0.52f, 0.72f};
    Vec3 groundColor{0.11f, 0.14f, 0.19f};      // what is seen, and reflected, below the horizon
    float intensity = 1.0f;
    float sunDiskIntensity = 30.0f;             // how much brighter than the sunlight the disk itself looks
    float sunAngularRadius = radians(1.2f);
    float cloudCoverage = 0.0f;                 // 0 a clear sky, 1 overcast
    bool cloudsHaveDepth = false;               // clouds as volumes one looks through, lit from the sun's side, instead of a flat layer
    float cloudScale = 1.0f;                    // larger numbers make smaller clouds
    Vec2 cloudSpeed{0.012f, 0.004f};            // how fast the wind carries them
    float cloudHeight = 1500.0f;                // meters: the base of the clouds with depth, which a camera can fly into
    float cloudThickness = 1200.0f;             // meters from their base to their tops
    /// Clouds with depth are walked through along each line of sight: in so many steps (32 is
    /// coarse, 64 fine), into a picture this share of the scene's size (they are soft: half
    /// the size shows no loss and costs a quarter). The project's quality level sets both.
    int cloudSteps = 48;
    float cloudResolution = 0.5f;
    float stars = 0.0f;                         // at night: 0 none, 1 a clear night far from any town
    /// At night (with sunset colors), the moon: across the sky from the sun, it lights the scene
    /// with a cold, dim light and casts its shadows. 0 a night with no moon; 1 a full moon.
    float moon = 1.0f;
    /// The colors of sunrise and sunset by themselves: as the sun nears the horizon its light
    /// goes gold, orange and red through the thicker air, the sky around it glows, the clouds
    /// are lit from below, and when it has set the sky darkens into night.
    bool sunsetColors = true;
};

/// What reaches the ground of sunlight that crosses the air, for a sun `height` (the sine of its
/// elevation): white high up, gold, orange and red toward the horizon, none once it has set.
/// Relative to the sun overhead.
inline Vec3 sunlightThroughAir(float height) {
    if (height < -0.08f) return Vec3{0, 0, 0};
    float h = std::max(height, 0.0f);
    float zenithDegrees = std::acos(std::min(h, 1.0f)) * (180.0f / kPi);
    float air = std::max(1.0f / (h + 0.15f * std::pow(std::max(93.885f - zenithDegrees, 0.001f), -1.253f)), 1.0f);     // Kasten and Young
    const Vec3 thinning{0.069f, 0.133f, 0.298f};    // Rayleigh's blue-first scattering, and a little haze, for one air's depth
    Vec3 through{std::exp(-thinning.x * (air - 1)), std::exp(-thinning.y * (air - 1)), std::exp(-thinning.z * (air - 1))};
    float t = std::clamp((height + 0.08f) / 0.09f, 0.0f, 1.0f);
    return through * (t * t * (3 - 2 * t));          // once set, it is gone
}

struct RenderSettings {
    int sampleCount = 2;                        // multisampling: 1 (off), 2 or 4
    float renderScale = 1.0f;                   // below 1 renders the scene smaller and stretches it
    bool metalFXUpscaling = true;               // with renderScale below 1: stretched by MetalFX, sharp, where the GPU has it
    // Dynamic resolution: the scene drawn smaller while the GPU cannot keep up with `targetFrameRate`,
    // down to `lowestDynamicScale` of renderScale, and back up when it can.
    bool dynamicResolution = false;
    float targetFrameRate = 60.0f;
    float lowestDynamicScale = 0.6f;
    bool followsDeviceHeat = true;              // hot (thermal state serious or critical): the scene drawn smaller, until it cools

    bool shadowsEnabled = true;
    CascadeSettings cascades;
    float shadowBias = 0.0001f;                 // in shadow-map depth, on top of the normal offset
    float shadowNormalOffset = 2.0f;            // in shadow-map texels; 2 covers the 3 x 3 filter

    /// The wind of the scene, in meters per second, and how gusty it is (0 steady, 1 wild).
    /// Smoke, fire and anything else that asks (windAt in Rendering/Particles.hpp) follows it.
    Vec3 wind{0, 0, 0};
    float windGustiness = 0.4f;

    /// Sun shadows by tracing rays against the scene's triangles instead of looking them up
    /// in shadow maps: exact at any distance, sharp where an object touches the ground and
    /// softer away from it. Used only where the GPU can trace rays (Renderer::canTraceRays);
    /// elsewhere the shadow maps stay.
    bool rayTracedShadows = false;
    /// The most rays a pixel sends toward the sun. One answers for nearly every pixel; the rest
    /// are sent only along the edges of shadows, where part of the sun's disk shows, and make
    /// those edges smooth. So more costs little.
    int shadowRayCount = 4;
    /// With traced sun shadows on: the light of the sky is also hidden by what is really in
    /// the way (corners, the underside of a car), found with short rays. They are sent before
    /// the scene is drawn, for one pixel in four, and smoothed (Shaders/Traced.metal): one or
    /// two a pixel are enough.
    bool rayTracedOcclusion = false;
    int occlusionRayCount = 1;
    /// And smooth surfaces stop mirroring the sky where something stands between them and it.
    bool rayTracedReflections = false;
    /// Global illumination, one bounce: the rays of the ambient light also bring back the
    /// light of what they hit, so that a red wall in the sun tints the floor beside it.
    float reflectionMaxRoughness = 0.3f;        // traced reflections only on surfaces smoother than this, fading out toward it
    float reflectionStrength = 1.0f;            // 1 as the surface's own Fresnel says; less to tone them down
    bool rayTracedBounce = false;
    float bounceIntensity = 1.0f;

    SkySettings sky;
    float environmentIntensity = 1.0f;          // strength of the image-based lighting

    Vec3 fogColor{0.38f, 0.52f, 0.72f};
    float fogDensity = 0.0f;                    // 0 turns fog off
    float fogHeightFalloff = 0.0f;              // per meter: above zero the fog lies low and thins with height, as mist does
    float fogSunScatter = 0.0f;                 // how much the fog glows toward the sun
    /// Shafts of light: the fog is walked through from the eye to each thing seen, and it
    /// glows only where the sun reaches it, so that what stands in the sun's way casts
    /// beams of shadow through the mist. The number of steps; 0 is off.
    int fogShaftSteps = 0;

    bool ambientOcclusionEnabled = true;
    float ambientOcclusionRadius = 0.6f;        // in world units
    float ambientOcclusionIntensity = 1.0f;
    float ambientOcclusionPower = 1.5f;

    bool bloomEnabled = true;
    float bloomIntensity = 0.06f;
    float bloomThreshold = 1.0f;
    float bloomKnee = 0.5f;

    float exposure = 1.0f;
    int toneMapper = 2;                         // 0 ACES, 1 Reinhard, 2 none
    float contrast = 1.0f, saturation = 1.0f;   // 1 as it is
    float temperature = 0.0f, tint = 0.0f;      // -1 to 1: cooler or warmer; greener or more magenta
    float sharpen = 0.0f;                       // 0 to 1
    float lensFlare = 0.6f;                     // the sun's light scattered in the camera's glass; 0 none
    /// Smudges and specks on the camera's glass (a picture of Assets, mostly black): lit up where
    /// bright light falls on them, by the bloom and the flare. Empty: a clean lens.
    std::string lensDirt;
    float lensDirtAmount = 1.0f;
    float vignette = 0.25f;

    // The camera's lens and video signal: what makes a picture look filmed, not computed.
    float lensDistortion = 0.0f;                // 0 none; 0.3 an action camera; 0.6 a fisheye
    float chromaticAberration = 0.0f;           // colors separate toward the corners; 0.01 is plenty
    float filmGrain = 0.0f;                     // fine noise; 0.03 is visible
    float interference = 0.0f;                  // 0 clean, 1 an analog video link about to drop

    // The colors graded with a lookup table: a picture in Assets of 32 squares 32 pixels wide
    // side by side (1024 by 32, or 16 of 16: 256 by 16), red across each square, green down it,
    // blue from square to square. Assets/Grading/Neutral 32.png changes nothing: paint over a
    // copy of it, or make one in a photo editor from a screenshot.
    std::string colorGrading;
    float colorGradingAmount = 1.0f;
    /// Exposure that adapts, as an eye does: brighter in a tunnel, darker in the sun.
    bool autoExposure = false;
    float exposureCompensation = 0.0f;          // in stops, on top of what it adapts to
    float exposureAdaptSpeed = 1.5f;            // how quickly: 1 is about a second
    float darkestExposure = 0.25f, brightestExposure = 8.0f;

    /// Depth of field: what is nearer or farther than the focus is blurred, as by a camera's lens.
    bool depthOfField = false;
    float focusDistance = 10.0f;                // meters from the camera
    float focusRange = 4.0f;                    // meters around it that stay sharp
    float focusBlur = 8.0f;                     // the widest blur, in pixels of a picture 1080 high
    /// Motion blur of the camera's own moves: what streaks past as it turns or speeds along.
    bool motionBlur = false;
    float motionBlurAmount = 0.5f;              // how long the shutter is open, as part of a frame
};

/// Levels of lighting quality, from cheapest to dearest: what ray tracing does, and how many
/// rays it sends. Low uses shadow maps only; Medium traces the sun's shadows; High adds ambient
/// light and reflections of smooth things; Ultra more rays, rougher reflections, bounced light.
enum class LightingQuality { Low, Medium, High, Ultra, Custom };

///
/// What each costs, in rays for each pixel of the picture: Medium about 0.7 (one toward the
/// sun from what faces it); High about 1 (one more for each four pixels, for the sky's light,
/// and one from each smooth pixel for what it mirrors); Ultra about 1.3 to 1.5.
inline void applyLightingQuality(RenderSettings& settings, LightingQuality quality) {
    settings.rayTracedShadows = quality != LightingQuality::Low;
    settings.shadowRayCount = quality == LightingQuality::Ultra ? 6 : quality == LightingQuality::High ? 4 : 3;
    settings.rayTracedOcclusion = quality == LightingQuality::High || quality == LightingQuality::Ultra;
    settings.occlusionRayCount = quality == LightingQuality::Ultra ? 2 : 1;
    settings.rayTracedReflections = settings.rayTracedOcclusion;
    settings.reflectionMaxRoughness = quality == LightingQuality::Ultra ? 0.4f : 0.25f;
    settings.rayTracedBounce = quality == LightingQuality::Ultra;
}

/// A scene saved when the levels sent several times the rays (up to version 1.1: four toward
/// the sun and four for the sky's light from every pixel at Ultra) is given the level it was
/// at, as that level is now. True if the settings were one of those levels and were changed.
inline bool bringLightingQualityUpToDate(RenderSettings& settings) {
    if (!settings.rayTracedShadows) return false;
    bool wasMedium = !settings.rayTracedOcclusion && !settings.rayTracedReflections && settings.shadowRayCount == 1;
    bool wasHigh = settings.rayTracedOcclusion && settings.rayTracedReflections && !settings.rayTracedBounce && settings.shadowRayCount == 2 &&
                   settings.occlusionRayCount == 2 && settings.reflectionMaxRoughness == 0.25f;
    bool wasUltra = settings.rayTracedOcclusion && settings.rayTracedReflections && settings.rayTracedBounce && settings.shadowRayCount == 4 &&
                    settings.occlusionRayCount == 4 && settings.reflectionMaxRoughness == 0.5f;
    if (!wasMedium && !wasHigh && !wasUltra) return false;
    applyLightingQuality(settings, wasUltra ? LightingQuality::Ultra : wasHigh ? LightingQuality::High : LightingQuality::Medium);
    return true;
}

/// The settings with no more ray tracing than `most` allows: what a project's quality level
/// does to what a scene asks for. Low: none; Medium: the sun's shadows; High: and the sky's
/// light and reflections; Ultra: everything the scene asks.
inline void limitLightingQuality(RenderSettings& settings, LightingQuality most) {
    if (most == LightingQuality::Custom || most == LightingQuality::Ultra) return;
    RenderSettings level = settings;
    applyLightingQuality(level, most);
    settings.rayTracedShadows = settings.rayTracedShadows && level.rayTracedShadows;
    settings.shadowRayCount = std::min(settings.shadowRayCount, level.shadowRayCount);
    settings.rayTracedOcclusion = settings.rayTracedOcclusion && level.rayTracedOcclusion;
    settings.occlusionRayCount = std::min(settings.occlusionRayCount, level.occlusionRayCount);
    settings.rayTracedReflections = settings.rayTracedReflections && level.rayTracedReflections;
    settings.reflectionMaxRoughness = std::min(settings.reflectionMaxRoughness, level.reflectionMaxRoughness);
    settings.rayTracedBounce = settings.rayTracedBounce && level.rayTracedBounce;
}

/// The level the settings are at, or Custom if they were set one by one.
inline LightingQuality lightingQualityOf(const RenderSettings& settings) {
    for (LightingQuality quality : {LightingQuality::Low, LightingQuality::Medium, LightingQuality::High, LightingQuality::Ultra}) {
        RenderSettings level = settings;
        applyLightingQuality(level, quality);
        bool same = level.rayTracedShadows == settings.rayTracedShadows && level.rayTracedOcclusion == settings.rayTracedOcclusion &&
                    level.rayTracedReflections == settings.rayTracedReflections && level.rayTracedBounce == settings.rayTracedBounce &&
                    (!settings.rayTracedShadows || level.shadowRayCount == settings.shadowRayCount) &&
                    (!settings.rayTracedOcclusion || level.occlusionRayCount == settings.occlusionRayCount) &&
                    (!settings.rayTracedReflections || level.reflectionMaxRoughness == settings.reflectionMaxRoughness);
        if (same) return quality;
    }
    return LightingQuality::Custom;
}

/// What drawing a frame took. The GPU is asked for much more than the scene's own meshes, and
/// one number for all of it says little: a cube on a floor is two draw calls, but its shadows,
/// the depth the ambient occlusion reads and the dozen full-screen passes of the bloom and the
/// tone mapping would make it look like twenty-six. So each kind is counted on its own.
struct RenderStats {
    /// What the scene itself is drawn with: one call for each mesh with each material, however
    /// many copies of it there are, one for each emitter of particles, one for each decal.
    int drawCalls = 0;
    int shadowDraws = 0;        // the same meshes drawn again into the sun's shadow maps
    int depthDraws = 0;         // and again before the picture, for what needs to know how far things are (ambient occlusion, water, depth of field)
    int passes = 0;             // full-screen passes: the sky, ambient occlusion, the bloom's chain, tone mapping
    int overlayDraws = 0;       // over the scene: the game's interface, the editor's grid and outlines
    int instances = 0;
    int triangles = 0;
    int visibleObjects = 0;
    int culledObjects = 0;
    int lights = 0;
    int shadowCasters = 0;      // things whose shadow reaches the view: drawn into at least one cascade

    /// Everything the GPU was asked to draw.
    int total() const { return drawCalls + shadowDraws + depthDraws + passes + overlayDraws; }
    /// What was added since `before` was read: the cost of what was drawn in between.
    RenderStats since(const RenderStats& before) const {
        RenderStats added = *this;
        added.drawCalls -= before.drawCalls;
        added.shadowDraws -= before.shadowDraws;
        added.depthDraws -= before.depthDraws;
        added.passes -= before.passes;
        added.overlayDraws -= before.overlayDraws;
        added.instances -= before.instances;
        added.triangles -= before.triangles;
        added.visibleObjects -= before.visibleObjects;
        added.culledObjects -= before.culledObjects;
        added.shadowCasters -= before.shadowCasters;
        return added;
    }
};

/// Where the finished frame goes: a drawable's texture, or any texture when rendering off screen.
struct RenderTarget {
    MTL::Texture* texture = nullptr;
    MTL::Drawable* drawable = nullptr;          // presented after the frame, if set
};

class Renderer {
public:
    Renderer(GraphicsDevice& device, const std::string& shaderDirectory);
    ~Renderer();

    /// The waves of the sea, for every water material: a Water component hands its own each
    /// frame; without one, the renderer makes a sea of its own from the scene's wind.
    void showOcean(const class Ocean& ocean, Vec2 origin);
    /// Reads a picture of the project by its name: for decals and the color grading table. Set by the Engine.
    std::function<std::shared_ptr<Texture>(const std::string&, TextureFormat)> loadPicture;
    RenderSettings settings;
    RenderStats stats;                          // of the last frame: every picture drawn in it, together
    /// What one of `views` took in the last frame (an editor shows the scene and the game side
    /// by side: each has its own numbers). Nothing for a view that was not drawn.
    RenderStats statsOfView(size_t index) const { return index < viewStats_.size() ? viewStats_[index] : RenderStats{}; }
    Profiler* profiler = nullptr;               // sections of the frame on the CPU, if someone keeps them
    /// What the GPU took for the last frame it finished (a frame or two behind the CPU), in milliseconds.
    float lastGpuMilliseconds() const { return lastGpuMilliseconds_.load(std::memory_order_relaxed); }
    /// What dynamic resolution has made of renderScale now: 1 when it is off or the GPU keeps up.
    float dynamicScale() const { return dynamicScale_; }
    /// How much smaller the device's heat asks for (1 when it is cool); the engine sets it.
    float heatScale = 1.0f;
    /// The profiler, or one that keeps nothing.
    Profiler& profilerOrIdle() {
        static Profiler idle;
        return profiler ? *profiler : idle;
    }
    DebugDraw debug;                            // lines to draw with the next frame

    /// If this entity has a Camera, the renderer looks through it whatever the scene's own
    /// cameras say. An editor uses it to look around without touching the scene.
    Entity cameraOverride;

    /// If width and height are positive, the scene is drawn only into this rectangle of the
    /// target (in pixels, from the top left) and the rest is filled with `backgroundColor`.
    /// The canvas still covers the whole target. An editor shows the scene this way, in the
    /// space its panels leave free.
    struct Rect {
        int x = 0, y = 0, width = 0, height = 0;
    };
    Rect sceneViewport;
    Vec4 backgroundColor{0.08f, 0.09f, 0.11f, 1};   // linear

    /// Views of the scene drawn into textures of their own instead of the target, each
    /// through its own camera: an editor shows them in its windows (a scene view and a
    /// game view, side by side if wanted). While there are any, the target itself gets
    /// only the background, the canvas and the overlay.
    struct SceneView {
        int width = 0, height = 0;          // pixels; a view without size is skipped
        Entity camera;                      // looked through whatever the scene says; invalid: the scene's active camera
        bool showsDebugLines = false;       // the editor's grid and outlines belong to its own view
    };
    std::vector<SceneView> views;
    /// The view the canvas is drawn into (a game's interface belongs to the picture of the
    /// game); -1 draws it into the target, over everything.
    int canvasView = -1;
    /// The picture of a view, as of the last frame drawn; null before the first. Valid until
    /// the end of the next frame.
    MTL::Texture* viewTexture(size_t index) const { return index < viewColors_.size() ? viewColors_[index].get() : nullptr; }

    /// Called at the end of the last pass of every frame that goes to the screen, after the
    /// canvas, with the encoder still open: whatever it draws lands on top of everything.
    /// Not called for renderToImage. The editor draws its interface here.
    std::function<void(MTL::CommandBuffer*, MTL::RenderCommandEncoder*)> overlay;

    /// Draws the scene and the canvas into `target`. `time` is in seconds, for shaders.
    void render(Scene& scene, const Canvas& canvas, const RenderTarget& target, double time);
    /// The project's quality (Settings/Quality.settings), put over the scene's settings as each
    /// view is drawn; none: the scene's as they are.
    const struct GameQuality* quality = nullptr;

    /// Draws a frame into memory instead: `width` x `height` pixels, four bytes each (red,
    /// green, blue, alpha), rows from the top. For screenshots and for running without a window.
    std::vector<uint8_t> renderToImage(Scene& scene, const Canvas& canvas, int width, int height, double time);

    /// The picture of a view as it was last drawn: four bytes a pixel (red, green, blue,
    /// alpha), rows from the top. Empty if the view has no picture yet. Waits for the GPU.
    std::vector<uint8_t> readView(size_t index, int& width, int& height);

    /// True if this GPU traces rays and the ray tracing shaders are in use.
    /// True if the shaders compiled with the extras: water, fog with height, traced ambient light.
    bool hasExtras() const { return shaders_.hasExtras(); }
    /// Not empty when the engine is drawing with less than its full shaders, because those did
    /// not compile here: what the compiler said. An editor shows it; a game goes on without.
    const std::string& shaderProblem() const { return shaders_.degradedError(); }
    /// The hand-written surface shaders in use (materials name them: Material::shader).
    /// Set `surfaceShaders().pathOf` to say where their files are.
    SurfaceShaderSet& surfaceShaders() { return surfaceShaders_; }
    /// What the compiler said if they did not compile; empty when they did.
    const std::string& surfaceShaderError() const { return shaders_.surfaceError(); }
    bool canTraceRays() const { return device_.supportsRayTracing() && shaders_.hasRayTracing(); }

    GraphicsDevice& device() { return device_; }
    ShaderLibrary& shaders() { return shaders_; }
    /// Blocks until the GPU has finished every frame submitted so far.
    void waitUntilIdle();

private:
    struct DrawItem {
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;
        Mat4 world;
        Vec4 tint;
        AABB bounds;                            // in world space
        float distanceSquared = 0;              // from the camera
        bool castsShadows = true;
    };

    /// One instanced draw call: `count` copies of a mesh with one material.
    struct Batch {
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;
        FrameAllocator::Allocation instances;
        uint32_t count = 0;
    };

    struct View {
        Mat4 world, view, projection, viewProjection;
        Vec3 position{0, 0, 0};
        float fovY = radians(60.0f), aspect = 1, nearPlane = 0.1f, farPlane = 500;
    };

    enum class MeshPass { Shadow, Prepass, Forward };

    void ensureTargets(int width, int height);
    void ensureShadowMap();
    View findView(Scene& scene, float aspect) const;
    void collectItems(Scene& scene, const View& view, std::vector<DrawItem>& items) const;
    int collectLights(Scene& scene, gpu::LightData* lights, gpu::FrameUniforms& frame, bool& sunCastsShadows) const;
    std::vector<Batch> makeBatches(std::vector<const DrawItem*>& items, bool sortByState);
    gpu::PassUniforms makePassUniforms(const Mat4& view, const Mat4& projection, Vec3 position, int width, int height,
                                       float nearPlane, float farPlane, double time) const;

    void bindMaterial(MTL::RenderCommandEncoder* encoder, const Material& material);
    void drawBatches(MTL::RenderCommandEncoder* encoder, const std::vector<Batch>& batches, MeshPass pass,
                     MTL::PixelFormat colorFormat, int sampleCount);
    void drawParticles(MTL::RenderCommandEncoder* encoder, Scene& scene, const View& view);
    void drawDebugLines(MTL::RenderCommandEncoder* encoder);
    void drawCanvas(MTL::RenderCommandEncoder* encoder, const Canvas& canvas, MTL::Texture* target);
    /// A thing that casts a shadow into the view, and the cascades its shadow reaches (a bit each).
    struct ShadowCaster {
        const DrawItem* item = nullptr;
        uint32_t cascades = 0;
    };
    /// What casts into which cascade: only where its shadow can fall on something seen (Cascades::reaches).
    std::vector<ShadowCaster> findShadowCasters(const std::vector<DrawItem>& items, const Cascades& cascades);
    /// The sun's shadow maps, every cascade in one pass (see Mesh.metal): a draw for each mesh
    /// with each material, however many cascades it reaches. Where that pass cannot be made
    /// (shaders without the extras), a pass for each cascade as before.
    void renderShadows(MTL::CommandBuffer* commandBuffer, std::vector<ShadowCaster>& casters, const Cascades& cascades,
                       const FrameAllocator::Allocation& passUniforms, const FrameAllocator::Allocation& frameUniforms);
    void renderShadowsEachCascade(MTL::CommandBuffer* commandBuffer, const std::vector<ShadowCaster>& casters, const Cascades& cascades);
    void renderOcclusion(MTL::CommandBuffer* commandBuffer, const std::vector<Batch>& opaque, const View& view,
                         const FrameAllocator::Allocation& passUniforms);
    /// Depth and normals of everything opaque, without multisampling: for ambient occlusion,
    /// decals, depth of field and motion blur.
    void renderPrepass(MTL::CommandBuffer* commandBuffer, const std::vector<Batch>& opaque, const FrameAllocator::Allocation& passUniforms);
    void drawDecals(MTL::RenderCommandEncoder* encoder, Scene& scene, const View& view);
    /// Depth of field, motion blur and the scene's brightness, between the scene and the bloom.
    void renderEffects(MTL::CommandBuffer* commandBuffer, const View& view, float deltaTime);
    /// True where water can be drawn as something seen into: the shaders have the extras, and
    /// the GPU hands a fragment function the color already drawn at its pixel (every Apple GPU).
    bool readsBehindWater() const { return shaders_.hasExtras() && device_.supportsFramebufferRead(); }
    bool wantsDepth() const {
        return settings.ambientOcclusionEnabled || settings.depthOfField || settings.motionBlur || hasDecals_ || waterLastFrame_ || particlesWantDepth_ ||
               wantsTracedAmbient();
    }
    /// The light around things is asked to be found with rays: done before the scene, at half
    /// the size, from the depth and the normals drawn first (renderTracedAmbient).
    bool wantsTracedAmbient() const {
        return settings.shadowsEnabled && settings.rayTracedShadows && settings.rayTracedOcclusion && canTraceRays() && shaders_.hasExtras();
    }
    /// True if the shaders have the functions of that pass (Shaders/Traced.metal).
    bool canTraceAmbientFirst();
    /// The light around each pixel, traced at half the size and smoothed: the picture the
    /// scene's pass reads it from, or nothing if it could not be drawn.
    MTL::Texture* renderTracedAmbient(MTL::CommandBuffer* commandBuffer, const View& view, int width, int height, double time,
                                      const FrameAllocator::Allocation& frameUniforms, bool withBounce);
    void renderScene(MTL::CommandBuffer* commandBuffer, Scene& scene, const std::vector<Batch>& opaque,
                     const std::vector<Batch>& transparent, const View& view, const FrameAllocator::Allocation& passUniforms,
                     const FrameAllocator::Allocation& frameUniforms, const FrameAllocator::Allocation& lights);
    bool buildSceneAccelerationStructure(MTL::CommandBuffer* commandBuffer, const std::vector<DrawItem>& items);
    void renderBloom(MTL::CommandBuffer* commandBuffer);
    /// Clouds with depth (Clouds.metal): walked through into a picture smaller than the scene's,
    /// before the scene, for the sky to read. True if they were drawn.
    bool renderClouds(MTL::CommandBuffer* commandBuffer, const View& view, int width, int height, double time, const FrameAllocator::Allocation& frameUniforms);
    /// The blocks of noise the clouds are made of are worked out on another thread while the
    /// engine starts: taken here once they are done, and sent to the GPU.
    void takeCloudNoise();
    /// True when clouds with depth can be drawn now: asked for, some of them in the sky, the
    /// noise ready, the shaders with the extras.
    bool marchesClouds() const {
        return cloudShape_ && cloudDetail_ && shaders_.hasExtras() && settings.sky.cloudsHaveDepth && settings.sky.cloudCoverage > 0;
    }
    /// The bloom's chain of passes (eleven of them) is drawn only when it adds something to
    /// the picture: on, and with some strength.
    bool hasBloom() const { return settings.bloomEnabled && !bloom_.empty() && settings.bloomIntensity > 0; }
    void renderFinal(MTL::CommandBuffer* commandBuffer, const Canvas& canvas, const RenderTarget& target, bool withOverlay);
    /// One whole picture of the scene into `target`: everything from shadows to tone mapping.
    void renderView(MTL::CommandBuffer* commandBuffer, Scene& scene, const Canvas& canvas, const RenderTarget& target, double time,
                    bool withOverlay);
    /// The target when the scene is in views: background, canvas, overlay.
    void renderInterface(MTL::CommandBuffer* commandBuffer, const Canvas& canvas, const RenderTarget& target);
    /// Exchanges the working textures with those kept for picture number `slot`, so that
    /// pictures of different sizes each keep their own instead of reallocating every frame.
    void swapTargets(size_t slot);
    Rect sceneRect(MTL::Texture* target) const;
    void drawFullscreen(MTL::CommandBuffer* commandBuffer, MTL::Texture* target, const char* fragmentFunction, BlendMode blend,
                        bool keepContents, const void* uniforms, size_t uniformsSize, MTL::Texture* source0, MTL::Texture* source1,
                        MTL::SamplerState* sampler0, MTL::Texture* source2 = nullptr);

    FrameAllocator& allocator() { return allocators_[frameIndex_]; }

    static constexpr int kFramesInFlight = 3;
    static constexpr MTL::PixelFormat kSceneFormat = MTL::PixelFormatRGBA16Float;
    static constexpr MTL::PixelFormat kDepthFormat = MTL::PixelFormatDepth32Float;

    GraphicsDevice& device_;
    ShaderLibrary shaders_;
    SurfaceShaderSet surfaceShaders_;
    int compiledSurfaceShaders_ = 0;       // how many of them the shaders in use were compiled with
    Environment environment_;
    FrameAllocator allocators_[kFramesInFlight];
    int frameIndex_ = 0;
    uint64_t frameNumber_ = 0;
    double time_ = 0;                               // of the frame being drawn
    dispatch_semaphore_t frameSemaphore_;
    std::atomic<float> lastGpuMilliseconds_{0};
    float dynamicScale_ = 1.0f;
    int framesSinceScaleChange_ = 0;
    void adjustDynamicScale();

    std::shared_ptr<Texture> white_, flatNormal_;
    Material defaultMaterial_;

    int targetWidth_ = 0, targetHeight_ = 0, targetSamples_ = 0;
    bool targetHasOcclusion_ = false;
    NS::SharedPtr<MTL::Texture> sceneColorMultisample_, sceneDepth_, sceneColor_;
    // The effects: a second picture to work into, the scene's brightness (and its mips down to
    // one pixel), the exposure it adapted to (two, written in turn), and the camera a frame ago.
    NS::SharedPtr<MTL::Texture> postColor_, luminance_, adapted_[2];
    int adaptIndex_ = 0;
    Mat4 previousViewProjection_ = identity();
    Mat4 flareViewProjection_ = identity();      // the last view drawn, and the sun then: for the lens flare
    Vec3 flareSunDirection_{0, -1, 0};
    Vec3 flareSunColor_{0, 0, 0};
    bool hasPreviousView_ = false;
    double lastViewTime_ = -1;
    bool hasDecals_ = false;
    std::shared_ptr<Texture> lut_;
    std::string lutName_;
    std::shared_ptr<Texture> lensDirt_;
    std::string lensDirtName_;
    // The sea's waves, as a picture water materials read: slope across, slope along, foam, height.
    void prepareOcean(MTL::CommandBuffer* commandBuffer, double time);
    NS::SharedPtr<MTL::Texture> oceanTextures_[kFramesInFlight];
    MTL::Texture* oceanTexture_ = nullptr;      // this frame's
    std::vector<uint16_t> oceanTexels_;
    int oceanSize_ = 0;
    float oceanPatch_ = 0;
    Vec2 oceanOrigin_{0, 0};
    bool oceanFed_ = false, waterSeen_ = false, waterLastFrame_ = false;
    bool particlesWantDepth_ = false;       // some particles are soft: they read how far the opaque scene is
    std::unordered_map<std::string, std::shared_ptr<Texture>> particlePictures_;     // the pictures of particles, by name
    /// A fine grid in place of a plane with a water material, made once for each such mesh.
    Mesh* waterGridFor(Mesh* mesh) const;
    mutable std::unordered_map<const Mesh*, std::unique_ptr<Mesh>> waterGrids_;
    std::shared_ptr<class Ocean> defaultOcean_;
    Vec3 defaultOceanWind_{0, 0, 0};
    MTL::Texture* upscaleLUT_ = nullptr;            // what renderFinal hands to the tone mapping before MetalFX
    MTL::Texture* upscaleAdapted_ = nullptr;
    MTL::Texture* upscaleDirt_ = nullptr;
    NS::SharedPtr<MTL::Texture> prepassDepth_, prepassNormal_, occlusionRaw_, occlusion_;
    NS::SharedPtr<MTL::Texture> tracedLight_[2], tracedSurface_;       // the light around things traced before the scene, half its size
    MTL::Texture* tracedLightDrawn_ = nullptr;      // which of the two holds it, smoothed, for the picture being drawn; nothing if it was not traced
    NS::SharedPtr<MTL::Texture> cloudColor_;        // the clouds of this picture: their light, and how much shows through them
    bool cloudsDrawn_ = false;                      // into cloudColor_, for the picture being drawn
    std::future<CloudNoise> cloudNoiseMaking_;
    NS::SharedPtr<MTL::Texture> cloudShape_, cloudDetail_;      // the two blocks of noise, on the GPU
    std::vector<NS::SharedPtr<MTL::Texture>> bloom_;
    NS::SharedPtr<MTL::Texture> shadowMap_;
    int shadowMapSize_ = 0, shadowMapSlices_ = 0;
    NS::SharedPtr<MTL::Texture> imageTarget_;       // for renderToImage

    /// MetalFX's spatial upscaler, and the two pictures it reads and writes: one per view.
    struct Upscaler {
        NS::SharedPtr<MTLFX::SpatialScaler> scaler;
        NS::SharedPtr<MTL::Texture> input, output;
        int inputWidth = 0, inputHeight = 0, outputWidth = 0, outputHeight = 0;
    };
    Upscaler upscaler_;
    /// The scene's picture tone mapped and upscaled to `width` x `height` by MetalFX; nullptr
    /// where it cannot be (no MetalFX on this GPU, or nothing to upscale).
    MTL::Texture* upscaleWithMetalFX(MTL::CommandBuffer* commandBuffer, const void* postUniforms, size_t uniformsSize, bool hasBloom, int width, int height);
    struct TargetSet {
        int width = 0, height = 0, samples = 0;
        bool hasOcclusion = false;
        NS::SharedPtr<MTL::Texture> colorMultisample, depth, color, prepassDepth, prepassNormal, occlusionRaw, occlusion;
        NS::SharedPtr<MTL::Texture> postColor, luminance, adapted[2], cloudColor, tracedLight[2], tracedSurface;
        int adaptIndex = 0;
        Mat4 previousViewProjection = identity();
        bool hasPreviousView = false;
        double lastViewTime = -1;
        std::vector<NS::SharedPtr<MTL::Texture>> bloom;
        Upscaler upscaler;
    };
    std::vector<TargetSet> targetSets_;                             // one per picture drawn in a frame
    std::vector<RenderStats> viewStats_;                            // what each of `views` took, last frame
    std::vector<NS::SharedPtr<MTL::Texture>> viewColors_;           // the finished pictures of `views`
    std::vector<NS::SharedPtr<MTL::Texture>> retiredTextures_;      // replaced this frame; someone may still draw with them
    bool drawsDebugLines_ = true;                                   // in the picture being drawn
    bool hasSceneStructureThisFrame_ = false;

    // Ray tracing: the whole scene as one structure that refers to each mesh's own.
    NS::SharedPtr<MTL::AccelerationStructure> sceneAccelerationStructure_;
    NS::SharedPtr<MTL::Buffer> sceneAccelerationScratch_;
    size_t sceneAccelerationCapacity_ = 0, sceneAccelerationScratchCapacity_ = 0;
    FrameAllocator::Allocation tracedSurfaces_;                     // gpu::TracedSurface for each instance of the scene structure
    std::vector<const MTL::AccelerationStructure*> tracedMeshes_;   // what the scene structure refers to this frame
    bool tracesRaysThisFrame_ = false;
};

}  // namespace molten
