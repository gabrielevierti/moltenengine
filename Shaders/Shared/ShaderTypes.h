// ShaderTypes.h
// The data layouts that cross the CPU / GPU boundary, written once.
//
// This file is included by two compilers:
//   - the Metal shader compiler (through the engine's #include resolver),
//   - the C++ compiler (Engine/Rendering/GPUTypes.hpp wraps it in namespace molten::gpu).
// Because both read the same declarations, a struct can never be laid out
// differently on the two sides of a buffer.
//
// Rules for anything added here: only float, uint32_t, simd vectors and simd matrices,
// and no bool (its size differs between languages). Remember that a float3 occupies
// 16 bytes, so a float that follows it starts a new 16-byte row; the unit tests check
// the resulting sizes.

#ifndef MOLTEN_SHADER_TYPES_H
#define MOLTEN_SHADER_TYPES_H

#ifdef __METAL_VERSION__
typedef float2 simd_float2;
typedef float3 simd_float3;
typedef float4 simd_float4;
typedef float3x3 simd_float3x3;
typedef float4x4 simd_float4x4;
#else
#include <simd/simd.h>
#include <stdint.h>
#endif

// ---------------------------------------------------------------- binding slots

enum {
    MMBufferVertices  = 0,   // mesh vertices, described by the vertex descriptor
    MMBufferPass      = 1,   // PassUniforms: the camera of the current pass
    MMBufferInstances = 2,   // InstanceData[], indexed by instance_id
    MMBufferMaterial  = 3,   // MaterialUniforms
    MMBufferFrame     = 4,   // FrameUniforms: sun, shadows, environment, fog
    MMBufferLights    = 5,   // LightData[]
    MMBufferAccelerationStructure = 6,   // the scene for ray tracing, when the GPU can trace rays
    MMBufferTracedSurfaces = 7,          // TracedSurface[], one for each instance of that scene: what a ray that hits it sees
    MMBufferShadowCopies = 8,            // ShadowCopy[], indexed by instance_id in the pass that draws the sun's shadow maps
    MMBufferWakes = 9,                   // WaterWakes: what floats, and the wakes behind it, for the water to draw
};

enum {
    MMTextureBaseColor         = 0,
    MMTextureNormal            = 1,
    MMTextureMetallicRoughness = 2,   // g = roughness, b = metallic (glTF convention)
    MMTextureOcclusion         = 3,
    MMTextureEmissive          = 4,
    MMTextureShadowMap         = 5,   // depth2d_array, one slice per cascade
    MMTextureIrradiance        = 6,   // cube
    MMTexturePrefiltered       = 7,   // cube with a roughness mip chain
    MMTextureBRDF              = 8,   // split-sum lookup table
    MMTextureAmbientOcclusion  = 9,   // screen-space ambient occlusion, or white
    MMTextureSceneDepth        = 10,  // the depth of what is opaque (the prepass), for water: how deep it is under each pixel
    MMTextureClouds            = 11,  // clouds with depth, walked through into a smaller picture: their light, and how much shows through them
    MMTextureTracedLight       = 12,  // the light around each pixel, traced before the scene at half the size: rgb what is bounced to it, a how much of the sky's light arrives
    MMTextureTracedSurface     = 13,  // and the surface each of those pixels was traced for: xyz which way it faces in the world, a how far it is
    MMTextureTracedSun         = 14,  // how much of the sun reaches each of those pixels, gathered over frames (MMTextureTracedLight's size)
    MMTextureRainMap           = 15,  // depth2d: the scene round the camera seen from above, nearest first: under a roof it does not rain
    MMTextureSceneNormal       = 16,  // the normals of what is opaque (the prepass), in the view's axes: which way the bottom under water faces
    MMTextureLightCookies      = 17,  // texture2d_array: the spot lights' cookies, a slice each (LightData::cookie), MMCookieSize square
};

enum {
    MMSamplerMaterial    = 0,   // trilinear, anisotropic, repeat
    MMSamplerShadow      = 1,   // comparison sampler
    MMSamplerLinearClamp = 2,
};

enum {
    MMMaxLights   = 64,
    MMMaxCookies  = 16,      // different cookies in a scene at once; lights past these shine without theirs
    MMCookieSize  = 256,
    MMMaxCascades = 4,
    MMMaxWakes    = 8,
    MMWakeTrail   = 16,
};

enum {
    MMLightDirectional = 0,
    MMLightPoint       = 1,
    MMLightSpot        = 2,
    MMLightArea        = 3,   // a glowing rectangle (a window, a panel, a screen), facing `direction`
};

// ---------------------------------------------------------------- per pass

// The camera a pass renders from. The shadow passes fill it with the light's matrices.
typedef struct {
    simd_float4x4 viewProjection;
    simd_float4x4 view;
    simd_float4x4 projection;
    simd_float4x4 inverseViewProjection;
    simd_float4x4 inverseProjection;
    simd_float3 cameraPosition;
    float time;
    simd_float2 viewportSize;        // in pixels
    float nearPlane;
    float farPlane;
    simd_float3 wind;                // meters per second: what sways grass and trees (materials with sway)
    float windGust;                  // 0 steady, 1 wild
} PassUniforms;

// ---------------------------------------------------------------- per frame

typedef struct {
    // The sun and its cascaded shadow map.
    simd_float4x4 cascadeViewProjection[MMMaxCascades];
    simd_float4 cascadeSplits;       // view-space distance at which each cascade ends
    simd_float4 cascadeTexelSize;    // world-space size of one shadow-map texel in each cascade
    simd_float3 sunDirection;        // the direction the light travels
    uint32_t cascadeCount;           // 0 turns sun shadows off. With shadows on, the sun is light 0
    simd_float3 sunColor;            // linear color times intensity
    float shadowBias;
    float shadowNormalOffset;
    float shadowMapSize;             // in texels
    float shadowDistance;            // shadows fade out before this view distance
    uint32_t lightCount;             // entries in the LightData buffer

    // Image-based lighting.
    float environmentIntensity;
    float prefilteredMipCount;
    float ambientOcclusionStrength;

    // Procedural sky.
    simd_float3 skyZenithColor;
    float sunDiskIntensity;
    simd_float3 skyHorizonColor;
    float sunDiskSize;               // cosine of the angular radius
    simd_float3 skyGroundColor;
    float skyIntensity;

    // Fog.
    simd_float3 fogColor;
    float fogDensity;

    // Clouds, drawn by the sky.
    float cloudCoverage;             // 0 clear sky, 1 overcast
    simd_float2 cloudOffset;         // how far the wind has carried them
    float cloudScale;                // larger numbers make smaller clouds

    // Ray tracing.
    uint32_t shadowRayCount;         // rays per pixel toward the sun; 0 means shadow maps (or no shadows)
    float sunAngularRadius;          // in radians: a larger sun gives softer ray-traced shadows

    // The extras (compiled where MOLTEN_EXTRAS is defined).
    float fogHeightFalloff;          // fog thins with height at this rate per meter; 0 is the same everywhere
    float fogSunScatter;             // how much the fog glows toward the sun
    uint32_t occlusionRayCount;      // rays per pixel for traced ambient occlusion; 0 is off
    uint32_t tracesReflections;      // 1: smooth surfaces send a ray to see what they mirror
    float bounceIntensity;           // above zero: the rays for ambient light also bring back the light of what they hit
    uint32_t fogShaftSteps;          // above zero: the fog is walked through in this many steps, lit where the sun reaches it
    uint32_t volumetricClouds;       // above zero: clouds have depth, walked through in this many steps
    float reflectionMaxRoughness;    // rougher surfaces send no reflection ray: the sky's blurred light is enough for them
    float reflectionStrength;        // how much of what a ray sees is mirrored: 1 as physics says
    // The night: the moon, and the stars.
    float moonBrightness;            // 0 no moon; 1 a full moon, which lights the scene once the sun has set
    uint32_t tracedAmbientReady;     // 1: the light around each pixel was traced before the scene (MMTextureTracedLight): read there, no rays for it in the scene's pass
    float starBrightness;            // 0 none; 1 a clear night far from town
    float starTime;                  // seconds: the stars turn slowly, and twinkle
    uint32_t sunsetColors;           // 1: the sky glows gold and red around a low sun, and darkens when it has set
    float cloudBase, cloudTop;       // meters: the layer of clouds with depth, flown through
    float sceneDepthReady;           // 1: MMTextureSceneDepth holds the opaque depth this frame
    // The sun's direction for the sky (its colors, its disk). `sunDirection` is the light the scene
    // is lit by: the sun, or at night the moon.
    simd_float3 skySunDirection;
    simd_float3 cameraPosition;      // where the camera is: the clouds with depth are walked through from there
    uint32_t frameIndex;             // counts the pictures drawn: rays are sent in a pattern that turns with it, and gathered over frames
    uint32_t tracedSunReady;         // 1: the sun's shadow was traced before the scene (MMTextureTracedSun): read there, no rays for it in the scene's pass
    simd_float2 cloudWind;           // the wind where the clouds are, meters per second along x and z: it draws them out along it, and carries them
    float lightning;                 // a storm's flash now: the clouds lit from inside, 0 none, 1 the brightest
    // Where the last stroke of lightning is: x and z in meters, and the height of the ground it
    // strikes. The clouds glow round it. `lightningSeed` is the shape of its bolt, which goes
    // down from the clouds to that ground: 0 for none (it flashes inside the clouds only).
    float lightningX, lightningZ, lightningGround, lightningSeed;
    float wetness;                   // the rain on what is out in it: 0 dry, 1 a downpour (sheltered places, by MMTextureRainMap, stay dry)
    simd_float4x4 rainViewProjection;   // the world from above, onto MMTextureRainMap: what the rain reaches first
    // The sea (Lighting > Water): its level, below which all is under water, seen through it,
    // lit through it, and with the camera under it, seen from inside it.
    simd_float4 seaAbsorb;           // xyz: what a meter of it takes of each color of light; w: 1 if there is a sea
    simd_float4 seaColor;            // rgb: the color it scatters back (its deep color); w: its level, in meters
    float cameraUnderwater;          // how far under its surface the camera is, in meters; 0 or less: above it
    simd_float4 seaShallow;          // rgb: the color it takes over a light bottom, a meter or two down; w: how far one sees through it, meters
    simd_float4 seaLook;             // x how much light its body scatters back, y how much it mirrors, z its ripples, w its caustics
    simd_float4 seaFoam;             // x foam where it meets what stands in it, y white crests where waves break
    simd_float4 snow;                // x how much snow has settled on what faces up and is out in it, 0 to 1 (WeatherSettings::snowSettles)
} FrameUniforms;

// The clouds are walked for one pixel in four each frame (in turn), and the rest kept from the
// frames before, moved to where they are now seen (Clouds.metal, fragment_clouds_resolve).
typedef struct {
    simd_float4x4 inverseViewProjection;     // this frame's camera, for the line of sight of each pixel
    simd_float4x4 previousViewProjection;    // the last frame's, to find where a line of sight was seen then
    simd_float4 camera;                      // xyz where the camera is
    simd_float2 offset;                      // which pixel of each two by two was walked this frame
    float hasHistory;                        // 0: nothing kept yet (the first frame, a new size)
    float pad;
} CloudResolveUniforms;

// What floats on water (a boat, a buoy, a crate): foam where the water meets it, and, behind it
// as it moves, the churned white trail and the two arms of its wake. Kit/Wakes.hpp keeps them.
typedef struct {
    simd_float4 trail[MMWakeTrail];  // where its stern has been, newest first (the first is where it is now): xyz, and w how many seconds ago; w below 0: no more
    simd_float4 trailSpeed[MMWakeTrail / 4];    // and how fast it went there, meters a second, four to each
    simd_float4 body;                // its middle at the waterline (xyz), and w its speed now
    simd_float4 shape;               // xy which way it heads along the water (x, z), z half its width, w half its length
} WaterWake;

typedef struct {
    WaterWake wakes[MMMaxWakes];
    uint32_t count;
    float pad0, pad1, pad2;
} WaterWakes;

// What the light traced before the scene was last frame: where its camera was, to find each
// pixel in last frame's picture, and keep what was found there.
typedef struct {
    simd_float4x4 previousViewProjection;
    simd_float4x4 previousView;
    uint32_t hasHistory;             // 0: nothing to keep (the first frame, a new size)
    float mostFrames;                // gathered over at most this many frames: more is smoother, and slower to follow a change
} TracedHistoryUniforms;

// What a ray sees of the thing it hits: its color and its glow. One for each instance of
// the ray tracing scene, in the same order.
typedef struct {
    simd_float4 baseColor;
    simd_float4 emissive;            // xyz; w is unused
} TracedSurface;

typedef struct {
    simd_float3 position;            // world space, for point and spot lights
    float range;
    simd_float3 direction;           // the direction the light travels, for directional and spot lights
    uint32_t type;                   // MMLightDirectional, MMLightPoint or MMLightSpot
    simd_float3 color;               // linear color times intensity
    float spotScale;                 // 1 / (cos(inner) - cos(outer))
    float spotOffset;                // -cos(outer) * spotScale
    float areaWidth, areaHeight;     // an area light's rectangle, in meters
    simd_float3 right;               // the light's own x axis: which way a cookie's picture lies across its cone
    float cookie;                    // a spot light's cookie: 0 none, else 1 + its slice of MMTextureLightCookies
    float volumetric;                // how much it lights the air it passes through (the fog), 0 none: beams in the mist
} LightData;

// ---------------------------------------------------------------- per draw

typedef struct {
    simd_float4x4 model;
    simd_float4x4 normalMatrix;      // inverse transpose of model
    simd_float4 tint;                // multiplies the material's base color
} InstanceData;

// The sun's shadow maps are drawn in one pass: each copy of a mesh says which of its
// instances it is, and which cascade's map it goes into.
typedef struct {
    uint32_t instance;               // an index into InstanceData[]
    uint32_t cascade;                // the slice of the shadow map array
} ShadowCopy;

typedef struct {
    simd_float4 baseColor;
    simd_float3 emissive;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;               // used by the alpha-tested variants
    simd_float2 uvScale;
    float water;                     // above zero: the surface is water, and this is how fine its waves are (1 / the material's Wave Size)
    float waveHeight;                // times what the wind raises
    float customShader;              // above zero: which hand-written surface shader runs, counted from 1
    simd_float4 custom[8];           // its properties, in the order the shader declares them
    float sway;                      // above zero: the wind moves it, the more the higher above its base (grass, leaves)
    float foam;                      // water: how much white shows where its waves break and where it meets what stands in it
    float isSea;                     // 1: the scene's sea (Lighting > Water), whose bottom is lit through it in the scene's own pass
} MaterialUniforms;

// ---------------------------------------------------------------- water's waves
//
// What the wind raises on open water is drawn (Mesh.metal, Water.metal) and floated on
// (Engine/Water/Waves.cpp) from the same numbers: eight trains of waves, the longest first.
// Each one's length as a part of the longest, its share of the height (four times the root of
// half the sum of their squares is 1: the "significant height" of a sea), and how far it runs
// from the way the wind blows (the cosine and the sine of the angle).
#define MM_WAVE_TRAINS 8
#define MM_WAVE_LENGTHS 1.0f, 0.71f, 0.52f, 0.37f, 0.27f, 0.19f, 0.14f, 0.10f
#define MM_WAVE_SHARES 0.2550f, 0.1750f, 0.1242f, 0.0854f, 0.0604f, 0.0410f, 0.0293f, 0.0203f
#define MM_WAVE_TURN_COS 1.00000f, 0.91355f, 0.94552f, 0.75471f, 0.81915f, 0.98163f, 0.61566f, 0.45399f
#define MM_WAVE_TURN_SIN 0.00000f, 0.40674f, -0.32557f, 0.65606f, -0.57358f, 0.19081f, -0.78801f, 0.89101f
// Water under a crest is drawn toward it: by this much of the wave's height. It is what makes crests sharp and troughs wide.
#define MM_WAVE_PINCH 1.0f
// The longest wave is this many times the square of the wind's speed (meters, meters per second), and the
// sea's height this many: a sheltered sea, lower and shorter than the open ocean's under the same wind.
#define MM_WAVE_LENGTH_PER_WIND 0.55f
#define MM_WAVE_HEIGHT_PER_WIND 0.0126f
// No sea stands steeper than this (height over longest length): beyond it crests would fold over themselves.
#define MM_WAVE_STEEPEST 0.074f
// The air is never quite still: water moves as under at least this much wind, meters per second.
#define MM_WAVE_LEAST_WIND 2.0f
// A sheet of water is still at its rim, and moves freely this many meters in from it.
#define MM_WATER_RIM 3.0f

// ---------------------------------------------------------------- post-processing and capture

typedef struct {
    float exposure;                  // multiplies scene color before tone mapping
    float bloomIntensity;
    float bloomThreshold;
    float bloomKnee;
    simd_float2 sourceTexelSize;     // 1 / size of the texture being sampled
    float vignette;
    uint32_t toneMapper;             // 0 ACES, 1 Reinhard, 2 none
    float filterRadius;
    // The camera's lens and its video signal.
    float lensDistortion;            // 0 none; above, straight lines bow outward as through a wide lens
    float chromaticAberration;       // colors separate toward the corners
    float grain;                     // fine noise, as from a small sensor
    float interference;              // 0 a clean picture; 1 an analog video link about to drop
    float time;                      // seconds, so that noise moves
    // Color grading with a lookup table, and exposure that adapts.
    float lutSize;                   // the table's edge: 32 for a picture 1024 by 32; 0 none
    float lutAmount;                 // 0 the colors as they were, 1 as the table makes them
    uint32_t autoExposure;           // 1: the exposure follows how bright the scene is (texture 3)
    float exposureCompensation;      // in stops, on top of what the adapting exposure says
    // Color, and the glass.
    float contrast;
    float saturation;
    float temperature;
    float tint;
    float sharpen;
    float lensDirt;                  // how much the dirt on the glass (texture 4) lights up under the bloom; 0 none
    // The camera going through the sea's surface (SeaSettings::lensThroughSurface).
    simd_float4 lensWater;           // xyz: how high each place of the glass is over the water, meters, as x * u + y * v + z (u, v the picture's 0 to 1); w 0 off, 1 the picture is of above the water, 2 of under it
    simd_float4 lensSea;             // xyz what a meter of the sea takes of each color; w how wet the glass still is from coming out, 0 to 1
    simd_float4 lensMore;            // x how much the picture still shakes from going in, 0 to 1
} PostUniforms;

// The effects between the scene and the bloom: depth of field, motion blur, how bright the
// scene is.
typedef struct {
    simd_float4x4 inverseViewProjection;
    simd_float4x4 previousViewProjection;    // the camera a frame ago: where each point was then
    simd_float2 texelSize;
    float focusDistance;             // meters: what is sharp
    float focusRange;                // meters around it that stay sharp
    float blurSize;                  // the widest blur, in pixels of a picture 1080 high
    float motionBlur;                // how long the shutter is open, as part of a frame: 0 none, 0.5 a film's
    float deltaTime;                 // seconds since the last frame, for the exposure to adapt
    float adaptSpeed;                // how quickly it does: 1 is about a second
    float darkestExposure;           // the least and most it may be
    float brightestExposure;
    float pictureHeight;             // in pixels
    float padding;
} EffectUniforms;

// A decal: a picture projected down its box onto whatever is there.
typedef struct {
    simd_float4x4 model;             // its box, a unit cube, in the world
    simd_float4x4 inverseModel;
    simd_float4 color;               // tint, and opacity in w
    float darkens;                   // 1: multiplies what is under it (dirt, cracks, marks); 0: painted over it
    float padding0, padding1, padding2;
} DecalUniforms;

typedef struct {
    simd_float4x4 projection;
    simd_float4x4 inverseProjection;
    simd_float2 texelSize;           // 1 / size of the occlusion target
    float radius;
    float bias;
    float intensity;
    float power;
} AmbientOcclusionUniforms;

// Rendering one face of a cubemap: used to capture the sky and to filter it.
typedef struct {
    uint32_t face;                   // 0 to 5: +X, -X, +Y, -Y, +Z, -Z
    float roughness;
    float sourceSize;                // edge of the source cubemap, in texels
} CaptureUniforms;

// ---------------------------------------------------------------- 2D, lines, particles

// These three are read straight from a buffer by vertex_id or instance_id,
// so they need no vertex descriptor.

typedef struct {
    simd_float2 position;            // in points, origin at the top left
    simd_float2 uv;
    simd_float4 color;               // straight alpha, sRGB
} CanvasVertex;

typedef struct {
    simd_float4x4 projection;
    float distanceRange;             // signed distance field spread, in atlas texels
} CanvasUniforms;

typedef struct {
    simd_float3 position;
    simd_float4 color;
} LineVertex;

typedef struct {
    simd_float4 positionAndSize;     // xyz: world position of the center, w: edge length
    simd_float4 color;
    simd_float4 velocityAndStretch;  // xyz: its velocity, w: seconds of it drawn as a streak (0: a round dot)
    simd_float4 look;                // x: its turn (radians), y: the frame of its sheet, z, w: the sheet's columns and rows (0: no picture)
    simd_float4 shading;             // x: how lit by the sun and sky (0 glows), y: soft edge in meters (0 hard), z: render mode, w: 1 a ring (a ripple) instead of a dot
} ParticleInstance;

#endif
