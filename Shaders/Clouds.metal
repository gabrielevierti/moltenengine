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
// And only one pixel in four is walked each frame, a different one of each two by two in turn
// (fragment_clouds, into a picture half as wide and high); the other three are the last frame's,
// moved to where their line of sight is seen now, and kept within what was just walked round
// them, so that nothing that has moved trails behind (fragment_clouds_resolve). Each pixel is
// walked anew every four frames, with a start of its own each time, and what it keeps of the
// times before smooths the grain away. Clouds move slowly: the four frames do not show. It costs
// a quarter of walking them all.
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
    // Where the wind has carried them: read upwind of here, so that they go the way the wind blows
    // (the way the waves run, Mesh.metal).
    float3 p = at - float3(frame.cloudOffset.x, 0.0, frame.cloudOffset.y) * 2400.0;
    // Their tops lean a little downwind, the more the harder it blows. (Their shapes are fixed to
    // the sky, not to the wind's direction: a change of wind carries them on from where they are,
    // it does not turn or stretch the whole sky.)
    float speed = length(frame.cloudWind);
    float2 along = speed > 0.1 ? frame.cloudWind / speed : float2(1.0, 0.0);
    p.xz -= along * (height * (frame.cloudTop - frame.cloudBase) * min(speed, 30.0) * 0.004);
    float2 windward = p.xz;

    // Where the clouds are: the same noise read very large, so that the sky has stretches
    // with many and stretches with few, each several kilometers across; and, the fewer the
    // clouds, the more they gather into groups between wide clear stretches, as fair-weather
    // clouds do, instead of standing evenly over the whole sky.
    float where = shapeNoise.sample(noiseSampler, float3(windward.x, windward.y, 0.0) * (scale / 61000.0) + float3(0.31, 0.57, 0.4), level(1.0)).r;
    float4 group = shapeNoise.sample(noiseSampler, float3(windward.x, windward.y, 0.0) * (scale / 17000.0) + float3(0.73, 0.19, 0.62), level(1.0));
    float coverage = frame.cloudCoverage;
    float cover = saturate(coverage + (where - 0.5) * 0.7 * (1.0 - abs(coverage * 2.0 - 1.0) * 0.6)
                         + (group.r - 0.5) * 1.1 * pow(1.0 - coverage, 1.5));
    if (cover <= 0.0) {
        return 0.0;
    }

    // Their body: swells (half of the block is above 0.5) with round lumps on them. The
    // block repeats every 6.4 km across and 1.6 km up; read a little bent (so that its
    // pattern does not show), and moving slowly up through it with the time, so that each
    // cloud slowly changes its shape as it goes.
    float3 inBlock = float3(windward.x, windward.y, p.y * 4.0) * (scale / 6400.0);
    inBlock.xy += (group.gb - 0.5) * 0.3;
    inBlock.z += frame.starTime * 0.00035;
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
    // Torn faster than the body moves: the wisps run on downwind, and fray.
    float3 fine = detailNoise.sample(noiseSampler, float3(windward.x - frame.starTime * speed * 0.15, windward.y, p.y) * (scale / 800.0), level(blur)).rgb;
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
    const int lightLooks = steps >= 56 ? 6 : steps >= 36 ? 5 : 4;
    const float lookGrowth = lightLooks == 6 ? 1.75 : lightLooks == 5 ? 2.1 : 2.9;
    const float lookBlur = 3.0 / float(lightLooks);
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
        // How much cloud lies between here and the light: a few looks toward it, each further than
        // the last, over the same kilometer and a half however many (fewer at a lower quality).
        float between = 0.0, lightStep = 45.0, lightAt = 0.0;
        for (int k = 0; k < lightLooks; k++) {
            lightAt += lightStep;
            between += cloudDensity(at + toLight * lightAt, frame, shapeNoise, detailNoise, noiseSampler, false, 0.3 + float(k) * lookBlur) * lightStep;
            lightStep *= lookGrowth;
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
        // A storm's lightning: the cloud lit from inside round the stroke, brightest where it is thick.
        if (frame.lightning > 0.0) light += float3(0.78, 0.82, 1.0) * (lightningGlow(at, frame) * 12.0 * (0.35 + 0.65 * saturate(amount * 2.0)));

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
    // A start of its own each frame: over the frames its pixel keeps, the grain smooths away.
    float2 turn = float2(float(frame.frameIndex % 61u) * 7.31, float(frame.frameIndex % 53u) * 3.17);
    return cloudsAlong(in.direction, pixelNoise(in.position.xy + turn), frame.volumetricClouds, frame, shapeNoise, detailNoise, noiseSampler);
}

// A picture read between its pixels without blurring it: a curve through four pixels each way
// (Catmull-Rom), in five reads that each blend four. Read so frame after frame, what is kept
// stays as sharp as it was walked.
static float4 sampleSharp(texture2d<float> picture, sampler linearSampler, float2 uv, float2 size) {
    float2 at = uv * size;
    float2 middle = floor(at - 0.5) + 0.5;
    float2 f = at - middle;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 between = w2 / w12;
    float2 at0 = (middle - 1.0) / size, at3 = (middle + 2.0) / size, at12 = (middle + between) / size;
    float4 sum = picture.sample(linearSampler, float2(at12.x, at0.y)) * w12.x * w0.y
               + picture.sample(linearSampler, float2(at0.x, at12.y)) * w0.x * w12.y
               + picture.sample(linearSampler, at12) * w12.x * w12.y
               + picture.sample(linearSampler, float2(at3.x, at12.y)) * w3.x * w12.y
               + picture.sample(linearSampler, float2(at12.x, at3.y)) * w12.x * w3.y;
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return sum / weight;
}

// The clouds of the picture, whole: the pixels walked this frame where they are (one of each two
// by two, `fresh`), and the others from the last frame's picture (`before`), where their line of
// sight was seen then, kept within what was walked round them.
fragment float4 fragment_clouds_resolve(FullscreenOut in [[stage_in]],
                                        constant CloudResolveUniforms &resolve [[buffer(MMBufferPass)]],
                                        texture2d<float> fresh [[texture(0)]],
                                        texture2d<float> before [[texture(1)]],
                                        sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float2 size = float2(before.get_width(), before.get_height());
    uint2 pixel = uint2(in.position.xy);
    int2 block = int2(pixel / 2u);
    int2 freshSize = int2(fresh.get_width(), fresh.get_height());
    // What was walked round here this frame: the range the clouds now have here.
    float4 lowest = float4(1e9), highest = float4(-1e9), walked = float4(0.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float4 seen = fresh.read(uint2(clamp(block + int2(x, y), int2(0), freshSize - 1)));
            lowest = min(lowest, seen);
            highest = max(highest, seen);
            if (x == 0 && y == 0) walked = seen;
        }
    }
    bool isWalked = all(float2(pixel % 2u) == resolve.offset);
    // Where this pixel's line of sight was in the last frame's picture.
    float2 ndc = (in.position.xy / size) * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 far = resolve.inverseViewProjection * float4(ndc, 1.0, 1.0);
    float3 direction = far.xyz / far.w - resolve.camera.xyz;
    float4 then = resolve.previousViewProjection * float4(direction, 0.0);
    float2 thenNdc = then.xy / max(then.w, 1e-6);
    bool wasSeen = resolve.hasHistory > 0.5 && then.w > 0.0 && all(abs(thenNdc) < 1.0);
    if (!wasSeen) {
        // Nothing kept: the walked picture, smoothed up (its pixels sit at the walked ones of each two by two).
        if (isWalked) return walked;
        float2 at = (in.position.xy - 0.5 - resolve.offset) * 0.5 + 0.5;
        return fresh.sample(linearSampler, at / float2(freshSize));
    }
    float2 uv = thenNdc * float2(0.5, -0.5) + 0.5;
    float4 kept = clamp(sampleSharp(before, linearSampler, uv, size), lowest, highest);
    // A pixel walked anew takes some of its new walk; the others keep theirs.
    return isWalked ? mix(kept, walked, 0.4) : kept;
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
