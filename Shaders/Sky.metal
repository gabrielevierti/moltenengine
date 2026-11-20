// Sky.metal
// A small procedural sky: a gradient from the horizon to the zenith, the ground
// below, and the sun. It is drawn behind the scene, and it is also rendered into a
// cubemap from which the image-based lighting is computed, so that reflections and
// ambient light always match what the player sees.

#include "Common.metal"

// ---------------------------------------------------------------- clouds

// A repeatable random number for each cell of a grid.
static float cloudHash(float2 p) {
    p = fract(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Random values at the corners of each cell, blended smoothly in between.
static float cloudValueNoise(float2 p) {
    float2 cell = floor(p), f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float top = mix(cloudHash(cell), cloudHash(cell + float2(1.0, 0.0)), u.x);
    float bottom = mix(cloudHash(cell + float2(0.0, 1.0)), cloudHash(cell + float2(1.0, 1.0)), u.x);
    return mix(top, bottom, u.y);
}

// Five layers of that noise, each twice as fine and half as strong as the one before:
// large shapes with ragged edges, which is what a cloud is.
static float cloudNoise(float2 p) {
    float sum = 0.0, strength = 0.5;
    for (int i = 0; i < 5; i++) {
        sum += strength * cloudValueNoise(p);
        p = p * 2.03 + float2(17.0, 9.0);
        strength *= 0.5;
    }
    return sum;
}

#if MOLTEN_EXTRAS
// The same noise in three dimensions: two slices of it, one above the other, blended.
static float cloudNoise3(float3 p, int layers) {
    float sum = 0.0, strength = 0.5;
    for (int i = 0; i < layers; i++) {
        float level = floor(p.y);
        float lower = cloudValueNoise(p.xz + level * 37.0);
        float upper = cloudValueNoise(p.xz + (level + 1.0) * 37.0);
        sum += strength * mix(lower, upper, smoothstep(0.0, 1.0, p.y - level));
        p = p * 2.03 + float3(11.3, 5.7, 3.1);
        strength *= 0.5;
    }
    return sum / (1.0 - strength * 2.0);                        // as if all the layers were there: 0 to 1 whatever their number
}

// How much cloud there is at a place: none below the layer's base or above its top, most
// in its lower middle, where clouds are flat underneath and heaped above.
static float cloudAmount(float3 at, float base, float top, constant FrameUniforms &frame, int layers) {
    float height = (at.y - base) / (top - base);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    float3 p = float3(at.x * 0.00042 * frame.cloudScale + frame.cloudOffset.x, at.y * 0.0011, at.z * 0.00042 * frame.cloudScale + frame.cloudOffset.y);
    float shape = cloudNoise3(p, layers);
    float profile = smoothstep(0.0, 0.12, height) * smoothstep(1.0, 0.45, height);
    float threshold = 0.74 - 0.47 * frame.cloudCoverage;
    return saturate((shape - threshold) * 4.0) * profile;
}
#endif

static float3 skyRadiance(float3 direction, constant FrameUniforms &frame, bool withSunDisk) {
    float3 d = normalize(direction);
    float3 toSun = -frame.sunDirection;

    // Horizon to zenith. The square root keeps the horizon band thin, as in a real sky.
    float3 color = mix(frame.skyHorizonColor, frame.skyZenithColor, sqrt(saturate(d.y)));
    // Below the horizon, blend quickly into the ground color.
    color = mix(frame.skyGroundColor, color, smoothstep(-0.12, 0.0, d.y));

    // A glow around the sun: a tight one and a wide one.
    float towardSun = saturate(dot(d, toSun));
    float glow = 0.35 * pow(towardSun, 64.0) + 0.08 * pow(towardSun, 6.0);
    color += frame.sunColor * glow * smoothstep(-0.1, 0.05, d.y);

    // Clouds: a flat layer overhead. Dividing by the height of the direction is the
    // perspective of looking along a ceiling: clouds get smaller and closer together
    // toward the horizon.
    float cover = 0.0;
#if MOLTEN_EXTRAS
    if (frame.volumetricClouds != 0 && withSunDisk && frame.cloudCoverage > 0.0 && d.y > 0.015) {
        // Clouds with depth: a layer a kilometer thick, walked through along the line of
        // sight. At each step, how much cloud is there, and how much light reaches it from
        // the sun through the cloud above it.
        const float base = 1500.0, top = 2700.0;
        float enter = base / d.y, leave = min(top / d.y, enter + 12000.0);
        const int steps = 16;
        float stride = (leave - enter) / float(steps);
        float t = enter + stride * cloudHash(d.xz * 4096.0);
        float through = 1.0;                                    // how much of what is behind still shows
        float3 gathered = float3(0.0);
        float3 skyLight = frame.skyHorizonColor * 0.7 + frame.skyZenithColor * 0.5;
        float toward = max(dot(d, toSun), 0.0);
        for (int i = 0; i < steps && through > 0.03; i++) {
            float3 at = d * t;
            float amount = cloudAmount(at, base, top, frame, 4);
            if (amount > 0.001) {
                // Toward the sun, three steps: the more cloud in the way, the darker here.
                float above = 0.0;
                for (int k = 1; k <= 3; k++) {
                    above += cloudAmount(at + toSun * (float(k) * 130.0), base, top, frame, 2);
                }
                float sunReaching = exp(-above * 130.0 * 0.012);
                // Light scattered in a cloud mostly goes on forward: brightest looking toward the sun, through thin cloud.
                float forward = 0.55 + 1.6 * pow(toward, 8.0);
                float3 light = frame.sunColor * (sunReaching * forward * 0.32) + skyLight * 0.55;
                float taken = 1.0 - exp(-amount * stride * 0.012);
                gathered += light * taken * through;
                through *= 1.0 - taken;
            }
            t += stride;
        }
        // Far away they sink into the haze of the horizon.
        float haze = exp(-enter * 0.00006);
        cover = (1.0 - through) * haze;
        color = color * (1.0 - cover) + gathered * haze;
    } else
#endif
    if (frame.cloudCoverage > 0.0 && d.y > 0.0) {
        float2 p = d.xz / (d.y + 0.1) * frame.cloudScale + frame.cloudOffset;
        float density = cloudNoise(p);
        float threshold = 0.78 - 0.5 * frame.cloudCoverage;
        cover = smoothstep(threshold, threshold + 0.2, density) * smoothstep(0.0, 0.1, d.y);
        // A second look a little toward the sun: where the cloud is thinner there than
        // here, this side is lit; where it is thicker, this side is in its shade.
        float2 sunward = toSun.xz / max(length(toSun.xz), 0.001) * 0.3;
        float light = saturate(0.62 + (density - cloudNoise(p + sunward)) * 3.0);
        float3 shade = frame.skyHorizonColor * 0.9 + frame.skyZenithColor * 0.35;
        float3 lit = frame.sunColor * 0.3 + frame.skyHorizonColor * 0.5;
        color = mix(color, mix(shade, lit, light), cover);
    }

    if (withSunDisk) {
        float disk = smoothstep(frame.sunDiskSize, frame.sunDiskSize + 0.00008, dot(d, toSun));
        color += frame.sunColor * frame.sunDiskIntensity * disk * (1.0 - cover);
    }
    return color * frame.skyIntensity;
}

// ---------------------------------------------------------------- the visible sky

struct SkyOut {
    float4 position [[position]];
    float3 direction;
};

// A full-screen triangle placed exactly on the far plane (z = w), drawn after the
// opaque geometry with a "less or equal" depth test: only the pixels that nothing
// else covered run the fragment function.
vertex SkyOut vertex_sky(uint vertexID [[vertex_id]],
                         constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    float2 uv = float2(float(vertexID & 2), float((vertexID << 1) & 2));
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    SkyOut out;
    out.position = float4(ndc, 1.0, 1.0);
    // Where this corner of the far plane is in the world gives the view direction.
    float4 world = pass.inverseViewProjection * float4(ndc, 1.0, 1.0);
    out.direction = world.xyz / world.w - pass.cameraPosition;
    return out;
}

fragment float4 fragment_sky(SkyOut in [[stage_in]],
                             constant FrameUniforms &frame [[buffer(MMBufferFrame)]]) {
    return float4(skyRadiance(in.direction, frame, true), 1.0);
}

// ---------------------------------------------------------------- the sky as a cubemap

// The direction that a texel of a cubemap face looks at. `uv` has its origin at the
// top left of the face. This is the layout every graphics API shares.
static float3 cubeDirection(uint face, float2 uv) {
    float2 p = uv * 2.0 - 1.0;
    switch (face) {
        case 0: return float3(1.0, -p.y, -p.x);         // +X
        case 1: return float3(-1.0, -p.y, p.x);         // -X
        case 2: return float3(p.x, 1.0, p.y);           // +Y
        case 3: return float3(p.x, -1.0, -p.y);         // -Y
        case 4: return float3(p.x, -p.y, 1.0);          // +Z
        default: return float3(-p.x, -p.y, -1.0);       // -Z
    }
}

// Renders one face of the sky cubemap. The sun's disk is left out: direct sunlight is
// computed analytically, and a tiny, extremely bright spot would only add noise to the
// filtered maps.
fragment float4 fragment_sky_capture(FullscreenOut in [[stage_in]],
                                     constant CaptureUniforms &capture [[buffer(MMBufferPass)]],
                                     constant FrameUniforms &frame [[buffer(MMBufferFrame)]]) {
    return float4(skyRadiance(cubeDirection(capture.face, in.uv), frame, false), 1.0);
}
