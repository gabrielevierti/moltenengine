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

// ---------------------------------------------------------------- the sky from the air itself

// Sunlight scattered by the air (Rayleigh: blue, all round) and by haze (Mie: white, toward
// the sun), along the line of sight, after Preetham, Shirley and Smits, "A Practical Analytic
// Model for Daylight". The sun's height decides everything: blue at noon, gold and red as it
// sets, dark when it has gone.
static float3 physicalSkyColor(float3 d, float3 toSun, float haze) {
    const float3 up = float3(0.0, 1.0, 0.0);
    float sunHeight = dot(toSun, up);
    // How much sunlight there is at all: less as the sun nears the horizon, none well below it.
    float sunLight = 1000.0 * max(0.0, 1.0 - exp(-((kPi / 1.95 - acos(clamp(sunHeight, -1.0, 1.0))) / 1.5)));
    float sunFade = 1.0 - saturate(1.0 - exp(sunHeight));
    float3 rayleigh = float3(5.804542996e-6, 1.356291141e-5, 3.026590246e-5) * (1.0 - (1.0 - sunFade));
    float turbidity = 2.0 + haze * 8.0;
    float3 mie = 0.434 * (0.2 * turbidity * 10e-18) * float3(1.839991851e14, 2.779802391e14, 4.079047954e14) * 0.005;
    // How much air the eye looks through: more toward the horizon.
    float zenith = acos(max(0.0, dot(up, d)));
    float air = 1.0 / (cos(zenith) + 0.15 * pow(max(93.885 - zenith * 180.0 / kPi, 0.001), -1.253));
    float3 through = exp(-(rayleigh * 8.4e3 * air + mie * 1.25e3 * air));
    float cosine = dot(d, toSun);
    float rayleighPhase = 3.0 / (16.0 * kPi) * (1.0 + pow(cosine * 0.5 + 0.5, 2.0));
    const float g = 0.8;
    float miePhase = 1.0 / (4.0 * kPi) * (1.0 - g * g) / pow(max(1.0 - 2.0 * g * cosine + g * g, 0.0001), 1.5);
    float3 scattered = (rayleigh * rayleighPhase + mie * miePhase) / max(rayleigh + mie, float3(1e-12));
    float3 light = pow(sunLight * scattered * (1.0 - through), float3(1.5));
    light *= mix(float3(1.0), pow(max(sunLight * scattered * through, float3(0.0)), float3(0.5)), saturate(pow(1.0 - sunHeight, 5.0)));
    float3 color = (light + 0.1 * through) * 0.04 + float3(0.0, 0.0003, 0.00075);
    return color * 0.35;
}

// A field of stars, fixed to the sky and turning with it, each twinkling.
static float3 starsAt(float3 d, float time, float brightness) {
    // The sky turns about the pole, once a day.
    float turn = time * (2.0 * kPi / 86400.0);
    float c = cos(turn), s = sin(turn);
    float3 p = float3(d.x * c - d.z * s, d.y, d.x * s + d.z * c) * 160.0;
    float3 cell = floor(p);
    float3 inCell = p - cell;
    float3 seed = fract(sin(float3(dot(cell, float3(127.1, 311.7, 74.7)), dot(cell, float3(269.5, 183.3, 246.1)),
                                   dot(cell, float3(113.5, 271.9, 124.6)))) * 43758.5453);
    if (seed.x < 0.93) {
        return float3(0.0);                                     // most cells have no star
    }
    float3 at = 0.2 + 0.6 * seed;
    float distance = length(inCell - at);
    float size = 0.06 + 0.06 * seed.y;
    float glow = smoothstep(size, 0.0, distance);
    float twinkle = 0.75 + 0.25 * sin(time * (2.0 + seed.z * 5.0) + seed.y * 40.0);
    float strength = pow((seed.x - 0.93) / 0.07, 6.0) * 3.0 + 0.15;
    // Some a little blue, some a little gold.
    float3 tint = mix(float3(1.0, 0.85, 0.7), float3(0.75, 0.85, 1.0), seed.z);
    return tint * glow * twinkle * strength * brightness;
}

// ---------------------------------------------------------------- sunrise and sunset

// The color of sunlight that has crossed the air to a sun this high (the sine of its elevation):
// white overhead, gold, orange and red toward the horizon, as bright as it can be (the dimming is
// the CPU's, on the sun's light). Rayleigh scattering takes the blue first.
static float3 sunsetHue(float height) {
    float h = max(height, 0.0);
    float zenithDegrees = acos(min(h, 1.0)) * 180.0 / kPi;
    float air = max(1.0 / (h + 0.15 * pow(max(93.885 - zenithDegrees, 0.001), -1.253)), 1.0);
    float3 through = exp(-float3(0.069, 0.133, 0.298) * (air - 1.0));
    return through / max(max(through.r, through.g), 0.0001);
}

// A sky of three colors made to follow the sun down: the horizon glows gold and red round the
// sun, the zenith deepens, the opposite side turns pink above a darker band, and once the sun
// has set it all fades through the blue hour into night.
static float3 sunsetSky(float3 color, float3 d, float3 toSun) {
    float h = toSun.y;
    float low = 1.0 - smoothstep(0.0, 0.4, h);              // 0 high up, 1 on the horizon
    float after = smoothstep(-0.22, -0.01, h);              // the glow left after it has set
    float3 hue = sunsetHue(h);
    float daylight = mix(0.025, 1.0, smoothstep(-0.2, 0.08, h));
    color = mix(float3(0.008, 0.013, 0.03), color, daylight);
    color *= mix(float3(1.0), float3(0.72, 0.78, 1.0), low * after);
    float2 flatSky = normalize(d.xz + float2(1e-5, 0.0));
    float2 flatSun = normalize(toSun.xz + float2(1e-5, 0.0));
    float toward = dot(flatSky, flatSun) * 0.5 + 0.5;
    float band = exp(-max(d.y, 0.0) * mix(10.0, 3.0, toward * toward));
    float glow = band * (0.12 + 0.88 * pow(toward, 3.0)) * low * after;
    color += hue * glow * 1.4;
    // Across the sky from the sun: the pink belt, a little above the horizon.
    float away = 1.0 - toward;
    float belt = exp(-abs(d.y - 0.1) * 18.0) * away * away * low * smoothstep(-0.12, 0.02, h);
    color += float3(0.75, 0.4, 0.5) * belt * 0.18;
    return color;
}

// The light the clouds get when the sun is low, or has just set: they are higher than the
// ground and still see it, red, from below.
static float3 cloudSunlight(constant FrameUniforms &frame, float3 d, float3 toSun) {
    if (frame.sunsetColors == 0u) {
        return frame.sunColor;
    }
    float h = toSun.y;
    float low = 1.0 - smoothstep(0.0, 0.4, h);
    float still = smoothstep(-0.14, 0.0, h);                // above the clouds' height the sun sets later
    float2 flatSky = normalize(d.xz + float2(1e-5, 0.0));
    float2 flatSun = normalize(toSun.xz + float2(1e-5, 0.0));
    float toward = dot(flatSky, flatSun) * 0.5 + 0.5;
    return frame.sunColor + sunsetHue(h) * (low * still * (0.5 + 1.5 * toward * toward) * 1.6);
}

static float3 skyRadiance(float3 direction, constant FrameUniforms &frame, bool withSunDisk) {
    float3 d = normalize(direction);
    float3 toSun = -frame.sunDirection;

    float3 color;
    if (frame.physicalSky != 0u) {
        color = physicalSkyColor(d, toSun, frame.skyHaze);
    } else {
        // Horizon to zenith. The square root keeps the horizon band thin, as in a real sky.
        color = mix(frame.skyHorizonColor, frame.skyZenithColor, sqrt(saturate(d.y)));
        if (frame.sunsetColors != 0u) color = sunsetSky(color, d, toSun);
    }
    float3 cloudSun = cloudSunlight(frame, d, toSun);
    // The clouds' shaded side at dusk: grey-violet, with a little of the glow.
    float dusk = frame.sunsetColors != 0u ? (1.0 - smoothstep(0.0, 0.4, toSun.y)) * smoothstep(-0.25, 0.0, toSun.y) : 0.0;
    float dark = frame.sunsetColors != 0u ? mix(0.05, 1.0, smoothstep(-0.2, 0.08, toSun.y)) : 1.0;
    // The stars, when the sun is down; dimmed toward the horizon, where the air is thick.
    if (frame.starBrightness > 0.0 && withSunDisk && d.y > 0.0) {
        float night = smoothstep(0.05, -0.12, toSun.y);
        if (night > 0.0) color += starsAt(d, frame.starTime, frame.starBrightness) * night * smoothstep(0.0, 0.25, d.y);
    }
    // Below the horizon, blend quickly into the ground color.
    color = mix(frame.skyGroundColor * dark, color, smoothstep(-0.12, 0.0, d.y));

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
                float3 light = cloudSun * (sunReaching * forward * 0.32) + skyLight * 0.55 * dark;
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
        float3 shade = (frame.skyHorizonColor * 0.9 + frame.skyZenithColor * 0.35) * dark;
        shade = mix(shade, shade * float3(0.62, 0.5, 0.66) + sunsetHue(toSun.y) * 0.18 * dark, dusk);
        float3 lit = cloudSun * 0.3 + frame.skyHorizonColor * 0.5 * dark;
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
