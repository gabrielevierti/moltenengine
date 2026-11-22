// Common.metal
// What every other shader file starts from: the shared data layouts, a few
// constants and color helpers, and the full-screen triangle.

#include <metal_stdlib>

using namespace metal;

#include "Shared/ShaderTypes.h"

constant float kPi = 3.14159265358979;

static float3 srgbToLinear(float3 color) {
    return pow(color, float3(2.2));
}

static float luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// A number in [0, 1) that differs from pixel to pixel in a pattern the eye reads as even
// grain and not as blotches ("interleaved gradient noise", Jimenez 2014). Whatever takes a
// few samples where it would want many (rays, steps through fog and cloud) starts each
// pixel's at a different place with it.
static float pixelNoise(float2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, float2(0.06711056, 0.00583715))));
}

// ---------------------------------------------------------------- the sea (Lighting > Water)

static bool hasSea(constant FrameUniforms &frame) {
    return frame.seaAbsorb.w > 0.5;
}

// The light the sea's water scatters toward the eye, `below` meters under its surface: the sky's
// and the sun's from above, in the water's own color, dimmer and bluer the deeper.
static float3 seaHaze(constant FrameUniforms &frame, float below) {
    float3 sky = (frame.skyZenithColor * 0.55 + frame.skyHorizonColor * 0.45) * frame.skyIntensity * frame.environmentIntensity;
    float3 sun = frame.sunColor * saturate(-frame.sunDirection.y) * 0.3;
    return frame.seaColor.rgb * (sky + sun) * 1.5 * exp(-frame.seaAbsorb.xyz * max(below, 0.0) * 0.8);
}

// What is under the sea's level, seen from a camera that is under it too: the water between them
// takes the colors away, red first, and puts its own haze in their place. What is above the level
// is seen through the surface, and the water's surface does that for it (Water.metal).
static float3 seenInSea(float3 color, float3 worldPosition, constant FrameUniforms &frame) {
    if (!hasSea(frame) || frame.cameraUnderwater <= 0.0 || worldPosition.y > frame.seaColor.w) return color;
    float range = length(worldPosition - frame.cameraPosition);
    float3 kept = exp(-frame.seaAbsorb.xyz * range);
    return color * kept + seaHaze(frame, frame.cameraUnderwater) * (1.0 - kept);
}

// ---------------------------------------------------------------- full-screen triangle

struct FullscreenOut {
    float4 position [[position]];
    float2 uv;                      // (0, 0) at the top left, like a texture
};

// One triangle that covers the whole target, with no vertex buffer: the three
// corners come from the vertex index. It is twice as large as the screen in both
// directions; the parts outside are clipped away. Compared with a quad made of two
// triangles there is no diagonal seam, so no pixel is shaded twice.
vertex FullscreenOut vertex_fullscreen(uint vertexID [[vertex_id]]) {
    float2 uv = float2(float(vertexID & 2), float((vertexID << 1) & 2));      // (0,0), (0,2), (2,0)
    FullscreenOut out;
    out.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    out.uv = uv;
    return out;
}
