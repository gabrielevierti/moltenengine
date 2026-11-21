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
#include "Rendering/DebugDraw.hpp"
#include "Rendering/Environment.hpp"
#include "Rendering/FrameAllocator.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/SurfaceShader.hpp"
#include "Scene/Scene.hpp"

#include <dispatch/dispatch.h>

#include <atomic>

#include <functional>
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
    /// The sky worked out from how sunlight scatters in the air: blue by day, red and gold as
    /// the sun goes down, dark at night, all from the sun's height; the three colors above are
    /// then not used (the ground's still is).
    bool isPhysical = false;
    float haze = 0.3f;                          // 0 clear mountain air, 1 a hazy summer day
    float stars = 0.0f;                         // at night: 0 none, 1 a clear night far from any town
};

struct RenderSettings {
    int sampleCount = 4;                        // multisampling: 1 (off), 2 or 4
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
    int shadowRayCount = 4;                     // rays per pixel: more is smoother and slower
    /// With traced sun shadows on: the light of the sky is also hidden by what is really in
    /// the way (corners, the underside of a car), found with short rays from every pixel.
    bool rayTracedOcclusion = false;
    int occlusionRayCount = 4;
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
    int toneMapper = 0;                         // 0 ACES, 1 Reinhard, 2 none
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

inline void applyLightingQuality(RenderSettings& settings, LightingQuality quality) {
    settings.rayTracedShadows = quality != LightingQuality::Low;
    settings.shadowRayCount = quality == LightingQuality::Ultra ? 4 : quality == LightingQuality::High ? 2 : 1;
    settings.rayTracedOcclusion = quality == LightingQuality::High || quality == LightingQuality::Ultra;
    settings.occlusionRayCount = quality == LightingQuality::Ultra ? 4 : 2;
    settings.rayTracedReflections = settings.rayTracedOcclusion;
    settings.reflectionMaxRoughness = quality == LightingQuality::Ultra ? 0.5f : 0.25f;
    settings.rayTracedBounce = quality == LightingQuality::Ultra;
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

struct RenderStats {
    int drawCalls = 0;
    int instances = 0;
    int triangles = 0;
    int visibleObjects = 0;
    int culledObjects = 0;
    int lights = 0;
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

    /// Reads a picture of the project by its name: for decals and the color grading table. Set by the Engine.
    std::function<std::shared_ptr<Texture>(const std::string&, TextureFormat)> loadPicture;
    RenderSettings settings;
    RenderStats stats;                          // of the last frame
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

    /// Draws a frame into memory instead: `width` x `height` pixels, four bytes each (red,
    /// green, blue, alpha), rows from the top. For screenshots and for running without a window.
    std::vector<uint8_t> renderToImage(Scene& scene, const Canvas& canvas, int width, int height, double time);

    /// The picture of a view as it was last drawn: four bytes a pixel (red, green, blue,
    /// alpha), rows from the top. Empty if the view has no picture yet. Waits for the GPU.
    std::vector<uint8_t> readView(size_t index, int& width, int& height);

    /// True if this GPU traces rays and the ray tracing shaders are in use.
    /// True if the shaders compiled with the extras: water, fog with height, traced ambient light.
    bool hasExtras() const { return shaders_.hasExtras(); }
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
    void renderShadows(MTL::CommandBuffer* commandBuffer, const std::vector<DrawItem>& items, const Cascades& cascades);
    void renderOcclusion(MTL::CommandBuffer* commandBuffer, const std::vector<Batch>& opaque, const View& view,
                         const FrameAllocator::Allocation& passUniforms);
    /// Depth and normals of everything opaque, without multisampling: for ambient occlusion,
    /// decals, depth of field and motion blur.
    void renderPrepass(MTL::CommandBuffer* commandBuffer, const std::vector<Batch>& opaque, const FrameAllocator::Allocation& passUniforms);
    void drawDecals(MTL::RenderCommandEncoder* encoder, Scene& scene, const View& view);
    /// Depth of field, motion blur and the scene's brightness, between the scene and the bloom.
    void renderEffects(MTL::CommandBuffer* commandBuffer, const View& view, float deltaTime);
    bool wantsDepth() const { return settings.ambientOcclusionEnabled || settings.depthOfField || settings.motionBlur || hasDecals_; }
    void renderScene(MTL::CommandBuffer* commandBuffer, Scene& scene, const std::vector<Batch>& opaque,
                     const std::vector<Batch>& transparent, const View& view, const FrameAllocator::Allocation& passUniforms,
                     const FrameAllocator::Allocation& frameUniforms, const FrameAllocator::Allocation& lights);
    bool buildSceneAccelerationStructure(MTL::CommandBuffer* commandBuffer, const std::vector<DrawItem>& items);
    void renderBloom(MTL::CommandBuffer* commandBuffer);
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
    bool hasPreviousView_ = false;
    double lastViewTime_ = -1;
    bool hasDecals_ = false;
    std::shared_ptr<Texture> lut_;
    std::string lutName_;
    MTL::Texture* upscaleLUT_ = nullptr;            // what renderFinal hands to the tone mapping before MetalFX
    MTL::Texture* upscaleAdapted_ = nullptr;
    NS::SharedPtr<MTL::Texture> prepassDepth_, prepassNormal_, occlusionRaw_, occlusion_;
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
        NS::SharedPtr<MTL::Texture> postColor, luminance, adapted[2];
        int adaptIndex = 0;
        Mat4 previousViewProjection = identity();
        bool hasPreviousView = false;
        double lastViewTime = -1;
        std::vector<NS::SharedPtr<MTL::Texture>> bloom;
        Upscaler upscaler;
    };
    std::vector<TargetSet> targetSets_;                             // one per picture drawn in a frame
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
