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
};

enum {
    MMSamplerMaterial    = 0,   // trilinear, anisotropic, repeat
    MMSamplerShadow      = 1,   // comparison sampler
    MMSamplerLinearClamp = 2,
};

enum {
    MMMaxLights   = 64,
    MMMaxCascades = 4,
};

enum {
    MMLightDirectional = 0,
    MMLightPoint       = 1,
    MMLightSpot        = 2,
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
} FrameUniforms;

typedef struct {
    simd_float3 position;            // world space, for point and spot lights
    float range;
    simd_float3 direction;           // the direction the light travels, for directional and spot lights
    uint32_t type;                   // MMLightDirectional, MMLightPoint or MMLightSpot
    simd_float3 color;               // linear color times intensity
    float spotScale;                 // 1 / (cos(inner) - cos(outer))
    float spotOffset;                // -cos(outer) * spotScale
} LightData;

// ---------------------------------------------------------------- per draw

typedef struct {
    simd_float4x4 model;
    simd_float4x4 normalMatrix;      // inverse transpose of model
    simd_float4 tint;                // multiplies the material's base color
} InstanceData;

typedef struct {
    simd_float4 baseColor;
    simd_float3 emissive;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;               // used by the alpha-tested variants
    simd_float2 uvScale;
} MaterialUniforms;

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
} PostUniforms;

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
} ParticleInstance;

#endif
