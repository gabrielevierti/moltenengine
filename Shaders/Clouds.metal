// Clouds.metal
// Clouds with depth: a layer of sky between two heights, walked through along each line
// of sight.
//
// At every step: how much cloud there is, read from two blocks of noise made once on the
// CPU (Engine/Rendering/CloudNoise.hpp: swells with round lumps pushed into them for the
// body, finer lumps to eat wisps out of the edges); how much of the sun's light reaches
// that place through the cloud that lies toward the sun; and the light of the sky, from
// above and from below. Light in a cloud is scattered many times, mostly onward, which is
// why clouds are white inside, bright along the edge that faces the sun, and silver-lined
// when the sun is behind them.
//
// It is drawn before the scene, into a picture smaller than the scene's (clouds are soft:
// half the size shows no loss, and costs a quarter), and the sky reads it (Sky.metal). The
// steps are short near the eye and longer with the distance, and each pixel starts its walk
// a little differently, so that what a few dozen steps miss becomes fine grain instead of
// bands.
//
// Part of the extras: without them the clouds are the flat layer of Sky.metal.

#include "Sky.metal"

#if MOLTEN_EXTRAS

// `value` from the range low..high brought to the range 0..1, without limits.
static float cloudRemap(float value, float low, float high) {
    return (value - low) / (high - low);
}

// How much cloud there is at a place, 0 to 1. Heaped clouds (cumulus): flat underneath, where
// the air reaches the height at which its water condenses; rounded and narrowing toward the
// top; taller where the sky has more of them. `detailed` false is the body alone, enough to
// ask whether there is any cloud and for the light's way to the sun; true adds the wisps.
static float cloudDensity(float3 at, constant FrameUniforms &frame, texture3d<float> shapeNoise, texture3d<float> detailNoise,
                          sampler noiseSampler, bool detailed, float blur) {
    float height = (at.y - frame.cloudBase) / (frame.cloudTop - frame.cloudBase);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    float scale = max(frame.cloudScale, 0.05);
    float3 p = at + float3(frame.cloudOffset.x, 0.0, frame.cloudOffset.y) * 2400.0;     // where the wind has carried them

    // Where the clouds are: the same noise read very large, so that the sky has stretches
    // with many and stretches with few, each several kilometers across.
    float where = shapeNoise.sample(noiseSampler, float3(p.x, p.z, 0.0) * (scale / 61000.0) + float3(0.31, 0.57, 0.4), level(1.0)).r;
    float cover = saturate(frame.cloudCoverage + (where - 0.5) * 0.7 * (1.0 - abs(frame.cloudCoverage * 2.0 - 1.0) * 0.6));
    if (cover <= 0.0) {
        return 0.0;
    }

    // Their body: swells (half of the block is above 0.5) with round lumps on them. The
    // block repeats every 6.4 km across and 1.6 km up.
    float3 inBlock = float3(p.x, p.z, p.y * 4.0) * (scale / 6400.0);
    float4 shape = shapeNoise.sample(noiseSampler, inBlock, level(blur));
    float lumps = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float body = shape.r + (lumps - 0.5) * 0.42;
    // Flat bottoms, round tops; the more cloud there is, the higher they tower.
    float tall = mix(0.3, 0.75, cover);
    float profile = smoothstep(0.0, 0.16, height) * saturate(cloudRemap(height, 1.0, tall));
    // The densest part of the sky is cloud: as much of it as `cover` says. Thick soon inside the edge.
    // (Less soon under a sky full of cloud, where what shows is not edges but how thick it is.)
    float amount = saturate((body * profile - (1.0 - cover)) * mix(4.8, 1.7, saturate(cover * 2.0 - 0.8)));
    if (!detailed || amount <= 0.0) {
        return amount;
    }
    // Wisps: fine lumps eat into the edges, as rags underneath and as billows on top.
    float3 fine = detailNoise.sample(noiseSampler, float3(p.x, p.z, p.y) * (scale / 800.0), level(blur)).rgb;
    float bites = fine.r * 0.625 + fine.g * 0.25 + fine.b * 0.125;
    bites = mix(bites, 1.0 - bites, saturate(height * 5.0));
    return saturate(cloudRemap(amount, bites * 0.6, 1.0));
}

// Light scattered once by a droplet goes mostly on the way it was going (Henyey and
// Greenstein's shape for it): `g` near 1 almost all forward, 0 the same every way.
static float cloudPhase(float cosine, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * cosine, 1e-4), 1.5));
}

// The clouds along a line of sight from the camera: the light they send toward the eye, and
// in `a` how much of what is behind them still shows. `start`, 0 to 1, is where in its first
// step the walk begins: different for each pixel, so that what the steps miss is grain.
static float4 cloudsAlong(float3 direction, float start, uint stepCount, constant FrameUniforms &frame,
                          texture3d<float> shapeNoise, texture3d<float> detailNoise, sampler noiseSampler) {
    const float4 nothing = float4(0.0, 0.0, 0.0, 1.0);
    float3 d = normalize(direction);
    float3 eye = frame.cameraPosition;
    float base = frame.cloudBase, top = frame.cloudTop;
    // Where the line of sight enters the layer and leaves it: from below, looking up; from
    // above, looking down on it; from inside, through it.
    float enter = -1.0, leave = -1.0;
    if (eye.y < base) {
        if (d.y > 0.004) enter = (base - eye.y) / d.y, leave = (top - eye.y) / d.y;
    } else if (eye.y > top) {
        if (d.y < -0.004) enter = (top - eye.y) / d.y, leave = (base - eye.y) / d.y;
    } else {
        enter = 0.0;
        leave = d.y > 0.001 ? (top - eye.y) / d.y : d.y < -0.001 ? (base - eye.y) / d.y : 40000.0;
    }
    const float farthest = 34000.0;                             // beyond it they are lost in the haze of the horizon
    if (frame.cloudCoverage <= 0.0 || enter < 0.0 || enter > farthest) {
        return nothing;
    }
    leave = min(leave, min(enter + 22000.0, farthest));
    float reach = leave - enter;

    float3 toSun = -frame.skySunDirection;
    float3 toLight = -frame.sunDirection;                       // the sun, or the moon at night
    float toward = dot(d, toLight);
    // Two lobes: most light goes on forward (the silver lining of a cloud in front of the sun), some comes back.
    float phase = min(mix(cloudPhase(toward, -0.25), cloudPhase(toward, 0.78), 0.55), 1.1);
    float phaseWide = mix(cloudPhase(toward, -0.1), cloudPhase(toward, 0.35), 0.6);    // of light already scattered several times

    // The light the clouds get (the sun's, redder when it is low, or the moon's), and the sky's from above and below.
    float3 sunLight = cloudSunlight(frame, d, toSun);
    float dusk = frame.sunsetColors != 0u ? (1.0 - smoothstep(0.0, 0.4, toSun.y)) * smoothstep(-0.25, 0.0, toSun.y) : 0.0;
    float dark = frame.sunsetColors != 0u ? mix(0.12 + 0.1 * frame.moonBrightness, 1.0, smoothstep(-0.2, 0.08, toSun.y)) : 1.0;
    float3 skyAbove = (frame.skyZenithColor * 1.1 + frame.skyHorizonColor * 0.55) * dark;
    float3 skyBelow = frame.skyHorizonColor * 0.34 * dark + frame.skyGroundColor * 0.25 * dark + float3(0.03, 0.02, 0.04) * dusk;

    const int steps = int(clamp(stepCount, 8u, 128u));
    // Short steps near the eye, longer with the distance: the same detail on the screen for each.
    float growth = clamp(log2(max(reach / 2400.0, 1.0)), 0.0, 3.6);
    float spread = exp2(growth) - 1.0;
    const float extinction = 0.045;                             // per meter of full cloud

    float through = 1.0;                                        // how much of what is behind still shows
    float3 gathered = float3(0.0);
    float distanceSum = 0.0, weightSum = 0.0;
    float before = 0.0;
    for (int i = 0; i < steps && through > 0.012; i++) {
        float share = (float(i) + start) / float(steps);
        float along = spread > 0.001 ? (exp2(share * growth) - 1.0) / spread : share;
        float t = enter + reach * along;
        float stride = max(reach * along - before, 1.0);
        before = reach * along;
        float3 at = eye + d * t;
        // Far away the blocks are read a little blurred: what is finer than a step would only glitter.
        float blur = clamp(log2(stride / 60.0), 0.0, 3.0);
        if (cloudDensity(at, frame, shapeNoise, detailNoise, noiseSampler, false, blur) <= 0.0) {
            continue;
        }
        float amount = cloudDensity(at, frame, shapeNoise, detailNoise, noiseSampler, true, blur);
        if (amount <= 0.002) {
            continue;
        }
        // How much cloud lies between here and the light: six looks toward it, each further than the last.
        float between = 0.0, lightStep = 45.0, lightAt = 0.0;
        for (int k = 0; k < 6; k++) {
            lightAt += lightStep;
            between += cloudDensity(at + toLight * lightAt, frame, shapeNoise, detailNoise, noiseSampler, false, 0.3 + float(k) * 0.5) * lightStep;
            lightStep *= 1.75;
        }
        float depth = between * extinction;
        // The light that came straight through, and the light scattered again and again on the
        // way: fainter, but it reaches much deeper, and goes every way. It is why the shaded
        // side of a cloud is grey and not black.
        float3 sun = sunLight * (exp(-depth) * phase + 0.5 * exp(-depth * 0.28) * phaseWide + 0.3 * exp(-depth * 0.06) * (0.25 / kPi));
        // Where the cloud is thin (its very edge, seen against the light) less is scattered toward the eye.
        float powder = 1.0 - exp(-amount * 9.0 - depth * 2.5);
        sun *= mix(1.0, powder, 0.55 * saturate(0.6 - toward * 0.5));
        float heightIn = saturate((at.y - base) / (top - base));
        float3 sky = mix(skyBelow, skyAbove, smoothstep(0.0, 0.85, heightIn)) * (0.5 + 0.5 * exp(-amount * 2.2));
        float3 light = sun * 7.5 + sky * 0.42;

        // What this step adds, exactly, for a step this long through cloud this thick.
        float thick = amount * extinction;
        float passed = exp(-thick * stride);
        gathered += light * (through * (1.0 - passed));
        distanceSum += t * through * (1.0 - passed);
        weightSum += through * (1.0 - passed);
        through *= passed;
    }
    if (through < 0.012) {
        through = 0.0;
    }
    float cover = 1.0 - through;
    if (cover <= 0.0) {
        return nothing;
    }
    // Far clouds sink into the haze of the horizon: paler, bluer, and at last gone.
    float distance = weightSum > 0.0 ? distanceSum / weightSum : enter;
    float clear = exp(-distance * 0.00007);
    float3 haze = (frame.skyHorizonColor * dark + sunsetHue(toSun.y) * dusk * 0.25) * cover;
    gathered = mix(haze, gathered, clear);
    float fade = 1.0 - smoothstep(farthest * 0.55, farthest, distance);
    return float4(gathered * fade, 1.0 - cover * fade);
}

// The clouds of the picture: drawn before the scene into a smaller picture of its own.
fragment float4 fragment_clouds(SkyOut in [[stage_in]],
                                constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                texture3d<float> shapeNoise [[texture(0)]],
                                texture3d<float> detailNoise [[texture(1)]],
                                depth2d<float> sceneDepth [[texture(2)]],
                                sampler noiseSampler [[sampler(0)]],
                                sampler pointSampler [[sampler(1)]]) {
    // Where something opaque is in front of the sky, no cloud will be seen: no walk.
    if (frame.sceneDepthReady > 0.5 && !is_null_texture(sceneDepth)) {
        float2 uv = in.position.xy / pass.viewportSize;
        // This picture is smaller than the depth's: a few looks around, so that a thin thing
        // against the sky does not leave a hole in the clouds beside it.
        float2 texel = 0.6 / pass.viewportSize;
        float farthestSeen = max(sceneDepth.sample(pointSampler, uv), sceneDepth.sample(pointSampler, uv + texel));
        farthestSeen = max(farthestSeen, max(sceneDepth.sample(pointSampler, uv + texel * float2(-1.0, 1.0)), sceneDepth.sample(pointSampler, uv + texel * float2(1.0, -1.0))));
        farthestSeen = max(farthestSeen, sceneDepth.sample(pointSampler, uv - texel));
        if (farthestSeen < 1.0) {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
    }
    if (frame.volumetricClouds == 0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    return cloudsAlong(in.direction, pixelNoise(in.position.xy), frame.volumetricClouds, frame, shapeNoise, detailNoise, noiseSampler);
}

// One face of the sky's cubemap with the same clouds in it, so that what water and chrome
// mirror is the sky the player sees. Walked in fewer steps: it is small, and read blurred.
fragment float4 fragment_sky_capture_clouds(FullscreenOut in [[stage_in]],
                                            constant CaptureUniforms &capture [[buffer(MMBufferPass)]],
                                            constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                            texture3d<float> shapeNoise [[texture(1)]],
                                            texture3d<float> detailNoise [[texture(2)]],
                                            sampler noiseSampler [[sampler(0)]]) {
    float3 direction = cubeDirection(capture.face, in.uv);
    float4 seen = cloudsAlong(direction, pixelNoise(in.position.xy), 28u, frame, shapeNoise, detailNoise, noiseSampler);
    return float4(skyRadianceWithClouds(direction, frame, false, seen, true), 1.0);
}

#endif
