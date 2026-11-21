// Effects.metal
// Decals, and what happens to the picture between the scene and the bloom: motion blur, depth
// of field, and how bright the scene is, for the exposure to adapt to.

#include "Common.metal"

// ---------------------------------------------------------------- decals

struct DecalOut {
    float4 position [[position]];
};

// A unit cube: its corners, and its 12 triangles, each turned to face out.
constant float3 kDecalCorners[8] = {
    float3(-0.5, -0.5, -0.5), float3(0.5, -0.5, -0.5), float3(0.5, 0.5, -0.5), float3(-0.5, 0.5, -0.5),
    float3(-0.5, -0.5, 0.5),  float3(0.5, -0.5, 0.5),  float3(0.5, 0.5, 0.5),  float3(-0.5, 0.5, 0.5),
};
constant ushort kDecalFaces[36] = {
    0, 2, 1, 0, 3, 2,   4, 5, 6, 4, 6, 7,   0, 1, 5, 0, 5, 4,
    3, 6, 2, 3, 7, 6,   0, 4, 7, 0, 7, 3,   1, 2, 6, 1, 6, 5,
};

// The cube from the vertex index alone: no vertex buffer.
vertex DecalOut vertex_decal(uint vertexID [[vertex_id]],
                             constant PassUniforms &pass [[buffer(MMBufferPass)]],
                             constant DecalUniforms &decal [[buffer(MMBufferMaterial)]]) {
    DecalOut out;
    out.position = pass.viewProjection * (decal.model * float4(kDecalCorners[kDecalFaces[vertexID % 36u]], 1.0));
    return out;
}

// Where the scene is under this pixel (from the depth of the prepass), seen from the decal's
// box: inside it, the picture is laid along its x and z, fading toward its top and bottom.
fragment float4 fragment_decal(DecalOut in [[stage_in]],
                               constant PassUniforms &pass [[buffer(MMBufferPass)]],
                               constant DecalUniforms &decal [[buffer(MMBufferMaterial)]],
                               depth2d<float> depth [[texture(0)]],
                               texture2d<float> picture [[texture(1)]],
                               sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    uint2 pixel = uint2(in.position.xy);
    if (pixel.x >= depth.get_width() || pixel.y >= depth.get_height()) {
        discard_fragment();
    }
    float z = depth.read(pixel);
    if (z >= 1.0) {
        discard_fragment();
    }
    float2 uv = in.position.xy / pass.viewportSize;
    float4 world = pass.inverseViewProjection * float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0);
    float3 local = (decal.inverseModel * float4(world.xyz / world.w, 1.0)).xyz;
    if (any(abs(local) > float3(0.5))) {
        discard_fragment();
    }
    float4 color = picture.sample(linearSampler, float2(local.x + 0.5, 0.5 - local.z)) * decal.color;
    float fade = 1.0 - smoothstep(0.3, 0.5, abs(local.y));
    float amount = saturate(color.a * fade);
    if (decal.darkens > 0.5) {
        // Multiplied into what is there: white leaves it, the picture's color darkens it.
        return float4(mix(float3(1.0), color.rgb, amount), 1.0);
    }
    return float4(color.rgb, amount);
}

// ---------------------------------------------------------------- the picture's effects

// Where a pixel's point is in the world, from the depth there.
static float3 effectWorldPosition(float2 uv, float depth, float4x4 inverseViewProjection) {
    float4 world = inverseViewProjection * float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    return world.xyz / world.w;
}

// The camera's own movement: each pixel is smeared along the way its point moved on screen
// since the last frame, as by a shutter open that long.
fragment float4 fragment_motion_blur(FullscreenOut in [[stage_in]],
                                     constant EffectUniforms &effect [[buffer(MMBufferPass)]],
                                     texture2d<float> scene [[texture(0)]],
                                     depth2d<float> depth [[texture(1)]],
                                     sampler pointSampler [[sampler(0)]],
                                     sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float d = depth.sample(pointSampler, in.uv);
    // The sky is as far as it gets: it moves only as the camera turns.
    float3 world = effectWorldPosition(in.uv, min(d, 0.99999), effect.inverseViewProjection);
    float4 before = effect.previousViewProjection * float4(world, 1.0);
    float2 thenUV = before.w > 0.0 ? before.xy / before.w * float2(0.5, -0.5) + 0.5 : in.uv;
    float2 moved = (in.uv - thenUV) * effect.motionBlur;
    // No more than a tenth of the picture, however fast.
    float length2 = dot(moved, moved);
    if (length2 > 0.01) {
        moved *= 0.1 * rsqrt(length2);
    }
    float3 sum = scene.sample(linearSampler, in.uv).rgb;
    if (length2 * dot(1.0 / effect.texelSize, 1.0 / effect.texelSize) < 1.0) {
        return float4(sum, 1.0);                        // less than a pixel: as it is
    }
    const int taps = 8;
    for (int i = 1; i <= taps; i++) {
        float t = float(i) / float(taps) - 0.5;
        sum += scene.sample(linearSampler, in.uv + moved * t).rgb;
    }
    return float4(sum / float(taps + 1), 1.0);
}

// How blurred a point is: not at all within the range around the focus, more the further
// from it, as a lens's circle of confusion grows.
static float circleOfConfusion(float distance, constant EffectUniforms &effect) {
    float outside = max(abs(distance - effect.focusDistance) - effect.focusRange * 0.5, 0.0);
    float blur = outside / max(distance, 0.1) * 2.0 + outside / max(effect.focusDistance, 0.1) * 0.5;
    return saturate(blur);
}

// How far from the eye the point under a pixel is; the sky very far.
static float effectDistance(float2 uv, depth2d<float> depth, sampler pointSampler, float4x4 inverseViewProjection, float3 eye) {
    float d = depth.sample(pointSampler, uv);
    return d >= 1.0 ? 1e5 : length(effectWorldPosition(uv, d, inverseViewProjection) - eye);
}

// Depth of field: each pixel is the average of a disk of others, as wide as it is out of focus;
// a sharp thing in front does not smear into the blur behind it.
fragment float4 fragment_depth_of_field(FullscreenOut in [[stage_in]],
                                        constant EffectUniforms &effect [[buffer(MMBufferPass)]],
                                        texture2d<float> scene [[texture(0)]],
                                        depth2d<float> depth [[texture(1)]],
                                        sampler pointSampler [[sampler(0)]],
                                        sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float3 eye = effectWorldPosition(float2(0.5), 0.0, effect.inverseViewProjection);
    float here = effectDistance(in.uv, depth, pointSampler, effect.inverseViewProjection, eye);
    float coc = circleOfConfusion(here, effect);
    float pixels = coc * effect.blurSize * effect.pictureHeight / 1080.0;
    float3 center = scene.sample(linearSampler, in.uv).rgb;
    if (pixels < 0.5) {
        return float4(center, 1.0);
    }
    // A golden-angle spiral of taps: evenly over the disk, with no pattern to see.
    const int taps = 24;
    float3 sum = center;
    float weight = 1.0;
    for (int i = 1; i <= taps; i++) {
        float r = sqrt(float(i) / float(taps));
        float a = float(i) * 2.39996323;
        float2 offset = float2(cos(a), sin(a)) * r * pixels * effect.texelSize;
        float2 uv = in.uv + offset;
        float theirs = effectDistance(uv, depth, pointSampler, effect.inverseViewProjection, eye);
        // What is in front may cover us with its blur; what is behind and sharp may not.
        float reach = theirs < here ? circleOfConfusion(theirs, effect) : coc;
        float w = saturate(reach * effect.blurSize * effect.pictureHeight / 1080.0 - r * pixels + 1.0);
        sum += scene.sample(linearSampler, uv).rgb * w;
        weight += w;
    }
    return float4(sum / weight, 1.0);
}

// The scene's brightness, as a logarithm: averaged, it gives the scene's middle gray.
fragment float4 fragment_luminance(FullscreenOut in [[stage_in]],
                                   texture2d<float> scene [[texture(0)]],
                                   sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float3 color = scene.sample(linearSampler, in.uv).rgb;
    return float4(log2(max(luminance(color), 0.0001)));
}

// The exposure eased toward what makes the scene's average a middle gray: quickly when it
// gets brighter, more slowly when it gets darker, as eyes do.
fragment float4 fragment_adapt_exposure(FullscreenOut in [[stage_in]],
                                        constant EffectUniforms &effect [[buffer(MMBufferPass)]],
                                        texture2d<float> brightness [[texture(0)]],
                                        texture2d<float> previous [[texture(1)]]) {
    uint last = brightness.get_num_mip_levels() - 1;
    float average = exp2(brightness.read(uint2(0, 0), last).r);
    float wanted = clamp(0.18 / max(average, 0.0001), effect.darkestExposure, effect.brightestExposure);
    float before = previous.read(uint2(0, 0)).r;
    if (!(before > 0.0)) {
        return float4(wanted);
    }
    float speed = effect.adaptSpeed * (wanted < before ? 2.0 : 1.0);
    float now = before + (wanted - before) * (1.0 - exp(-effect.deltaTime * speed));
    return float4(now);
}
