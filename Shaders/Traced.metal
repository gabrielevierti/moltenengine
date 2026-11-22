// Traced.metal
// The light around each pixel, found with rays before the scene is drawn.
//
// How much of the sky's light reaches a place (less in a corner, under a car), and what
// the things near it send it of their own light (the red of a wall in the sun, on the floor
// beside it): both change slowly from one pixel to the next. So they need not be found
// for every pixel, in the scene's pass, with several rays each. Here they are found for
// one pixel in four (a picture half the scene's size each way), from the depth and the
// normals drawn before the scene, with one to three rays a pixel sent in a pattern that
// differs from pixel to pixel; then smoothed twice across neighbors that belong to the same
// surface, which is where the few rays of each pixel add up to dozens. The scene's pass
// reads the result (tracedAmbientAt in Lighting.metal) and sends no rays of its own for
// this. A frame that asks for all of it costs a fraction of a ray a pixel.
//
// What each pixel found is added to what was found at the same place over the frames before
// (fragment_traced_accumulate), so that a still picture is as smooth as if dozens of rays had
// been sent for each pixel, and a moving one has no more grain than where something has just
// come into view. The sun's shadow is not found here: the scene's pass traces it from each
// pixel's own place, which a picture of half the size cannot give it exactly.
//
// Compiled where the GPU traces rays and the extras are in. Without this pass (its
// functions did not compile, or there is no depth to read) the scene's pass traces these
// rays itself, as before.

#include "Lighting.metal"

#if MOLTEN_RAYTRACING
#if MOLTEN_EXTRAS

struct TracedAmbientOut {
    float4 light [[color(0)]];      // rgb: the light bounced to the pixel; a: how much of the sky's light arrives
    float4 surface [[color(1)]];    // xyz: which way the surface faces, in the world; a: how far it is from the camera
};

// Farther than this from the camera no rays are sent: a corner that far is a pixel wide.
constant float kTracedAmbientFar = 160.0;

fragment TracedAmbientOut fragment_traced_ambient(FullscreenOut in [[stage_in]],
                                                  constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                                  constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                                  raytracing::instance_acceleration_structure tracedScene [[buffer(MMBufferAccelerationStructure)]],
                                                  constant TracedSurface *tracedSurfaces [[buffer(MMBufferTracedSurfaces)]],
                                                  depth2d<float> sceneDepth [[texture(0)]],
                                                  texture2d<float> sceneNormal [[texture(1)]],
                                                  texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                                  sampler pointSampler [[sampler(0)]],
                                                  sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    TracedAmbientOut out;
    out.light = float4(0.0, 0.0, 0.0, 1.0);
    // Each of these pixels stands for one of the scene's: its depth, its normal, and the middle of
    // that pixel to find its place from. (Not the middle of this one, which falls between four of
    // the scene's: a depth read there and placed here is off the surface by a little, below it in
    // some rows, and a ray from below the ground is in its shadow: dark lines across the picture.)
    uint2 fullSize = uint2(sceneDepth.get_width(), sceneDepth.get_height());
    uint2 full = min(uint2(max(in.uv * float2(fullSize) - 0.5, 0.0)), fullSize - 1u);
    float2 uv = (float2(full) + 0.5) / float2(fullSize);
    float depth = sceneDepth.read(full);
    if (depth >= 1.0) {
        out.surface = float4(0.0, 0.0, 0.0, kTracedSky);         // the sky: nothing to light
        return out;
    }
    // Where the pixel is in the world, from how deep it is; and which way it faces.
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 world = pass.inverseViewProjection * clip;
    float3 position = world.xyz / world.w;
    float3 viewNormal = sceneNormal.read(full).xyz * 2.0 - 1.0;
    // The normals were drawn as the camera sees them: turned back to the world's directions.
    // (One that says nothing, a pixel the depth has and the normals not, faces the camera.)
    viewNormal = dot(viewNormal, viewNormal) > 0.25 ? viewNormal : float3(0.0, 0.0, 1.0);
    float3 normal = normalize(float3(dot(pass.view[0].xyz, viewNormal), dot(pass.view[1].xyz, viewNormal), dot(pass.view[2].xyz, viewNormal)));
    float viewDepth = -(pass.view * float4(position, 1.0)).z;
    out.surface = float4(normal, viewDepth);

    float nearness = 1.0 - smoothstep(0.7 * kTracedAmbientFar, kTracedAmbientFar, viewDepth);
    if (nearness <= 0.0) {
        return out;
    }
    float4 around = ambientTraced(position, normal, viewDepth, in.position.xy, true, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    out.light = float4(around.rgb * nearness, mix(1.0, around.a, nearness));
    // Whatever went wrong on the way, nothing that is not a number is kept: it would be gathered
    // over frames, smoothed onto the pixels round it, and blacken (or, under water, whiten) them all.
    if (any(isnan(out.light)) || any(isinf(out.light))) {
        out.light = float4(0.0, 0.0, 0.0, 1.0);
    }
    return out;
}

// What was found this frame added to what was found before at the same place. Each pixel is
// looked for in last frame's picture (where its point of the world was seen then); if the same
// surface was there (as far from that camera as it should be, facing the same way), what was
// gathered there is kept, and this frame's rays count for one frame among the last dozen or two.
// Where it was not (it has just come into view from behind something), it starts over.
struct TracedHistoryOut {
    float4 light [[color(0)]];      // the light around it, gathered
    float4 frames [[color(1)]];     // g: over how many frames
};

fragment TracedHistoryOut fragment_traced_accumulate(FullscreenOut in [[stage_in]],
                                                     constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                                     constant TracedHistoryUniforms &history [[buffer(MMBufferFrame)]],
                                                     texture2d<float> light [[texture(0)]],
                                                     texture2d<float> surface [[texture(2)]],
                                                     texture2d<float> lastSurface [[texture(3)]],
                                                     texture2d<float> lastLight [[texture(4)]],
                                                     texture2d<float> lastFrames [[texture(5)]]) {
    uint2 pixel = uint2(in.position.xy);
    float2 size = float2(float(surface.get_width()), float(surface.get_height()));
    float4 found = light.read(pixel);
    TracedHistoryOut out;
    out.light = found;
    out.frames = float4(1.0, 1.0, 0.0, 1.0);
    float4 here = surface.read(pixel);
    if (history.hasHistory == 0 || here.a >= kTracedSky) {
        return out;
    }
    float3 position = worldPositionAt(in.position.xy / size, here.a, pass);
    float4 clip = history.previousViewProjection * float4(position, 1.0);
    if (clip.w <= 0.0001) {
        return out;
    }
    float2 before = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
    float expected = -(history.previousView * float4(position, 1.0)).z;
    // How much the depth may change from one pixel to the next on this surface: little on a wall
    // faced square, a lot on a road seen at a low angle, where the pixel beside is meters farther.
    // A pixel of last frame's picture a fraction of a pixel away is the same surface within that.
    float3 toPoint = normalize(position - pass.cameraPosition);
    float facing = max(abs(dot(toPoint, here.xyz)), 0.05);
    float pixelAngle = 2.0 / (abs(pass.projection[1][1]) * size.y);
    float allowed = expected * 0.03 + 0.05 + 1.5 * expected * pixelAngle / facing;
    float2 at = before * size - 0.5;
    float2 corner = floor(at);
    float2 part = at - corner;
    int2 last = int2(int(surface.get_width()) - 1, int(surface.get_height()) - 1);
    float4 keptLight = float4(0.0);
    float keptFrames = 0.0;
    float total = 0.0;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            int2 tap = int2(int(corner.x) + i, int(corner.y) + j);
            if (tap.x < 0 || tap.y < 0 || tap.x > last.x || tap.y > last.y) {
                continue;
            }
            float4 there = lastSurface.read(uint2(tap));
            bool same = there.a < kTracedSky && abs(there.a - expected) < allowed && dot(there.xyz, here.xyz) > 0.85;
            float weight = (i == 0 ? 1.0 - part.x : part.x) * (j == 0 ? 1.0 - part.y : part.y) * (same ? 1.0 : 0.0);
            keptLight += lastLight.read(uint2(tap)) * weight;
            keptFrames += lastFrames.read(uint2(tap)).g * weight;
            total += weight;
        }
    }
    if (total < 0.05) {
        return out;
    }
    keptLight /= total;
    keptFrames /= total;
    if (any(isnan(keptLight)) || any(isinf(keptLight))) {
        return out;                                             // nothing to keep: start over here
    }
    float frames = min(keptFrames + 1.0, max(history.mostFrames, 1.0));
    out.light = mix(keptLight, found, 1.0 / frames);
    out.frames = float4(1.0, frames, 0.0, 1.0);
    return out;
}

// One smoothing: each pixel becomes the mean of itself and the eight around it, `apart`
// pixels from it, each counted for as much as it lies on the same surface. Done with the
// pixels next to each other, then two apart, and for the bounced light, which has more grain
// in it than the shade has, four apart: fifteen pixels across in all, for the price of
// twenty-seven looks.
static float4 smoothedTracedLight(float2 pixel, int apart, bool keepsShade, constant PassUniforms &pass, texture2d<float> light, texture2d<float> surface) {
    int2 at = int2(int(pixel.x), int(pixel.y));
    float2 size = float2(float(light.get_width()), float(light.get_height()));
    int2 last = int2(int(light.get_width()) - 1, int(light.get_height()) - 1);
    float4 here = surface.read(uint2(uint(at.x), uint(at.y)));
    float4 own = light.read(uint2(uint(at.x), uint(at.y)));
    if (here.a >= kTracedSky) {
        return own;                                             // the sky
    }
    float3 position = worldPositionAt(pixel / size, here.a, pass);
    float4 sum = own;
    float total = 1.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            if (i == 0 && j == 0) {
                continue;
            }
            int2 tap = clamp(int2(at.x + i * apart, at.y + j * apart), int2(0, 0), last);
            float4 there = surface.read(uint2(uint(tap.x), uint(tap.y)));
            float weight = (i == 0 || j == 0) ? 0.5 : 0.25;
            weight *= sameSurface(there, (float2(float(tap.x), float(tap.y)) + 0.5) / size, position, here.xyz, here.a, 4.0, pass);
            sum += light.read(uint2(uint(tap.x), uint(tap.y))) * weight;
            total += weight;
        }
    }
    float4 smoothed = sum / total;
    if (keepsShade) {
        smoothed.a = own.a;
    }
    return smoothed;
}

fragment float4 fragment_traced_ambient_smooth(FullscreenOut in [[stage_in]],
                                               constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                               texture2d<float> light [[texture(0)]],
                                               texture2d<float> surface [[texture(1)]]) {
    return smoothedTracedLight(in.position.xy, 1, false, pass, light, surface);
}

fragment float4 fragment_traced_ambient_smooth_wide(FullscreenOut in [[stage_in]],
                                                    constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                                    texture2d<float> light [[texture(0)]],
                                                    texture2d<float> surface [[texture(1)]]) {
    return smoothedTracedLight(in.position.xy, 2, false, pass, light, surface);
}

// The third time, for the bounced light alone: the shade keeps the detail it has.
fragment float4 fragment_traced_ambient_smooth_widest(FullscreenOut in [[stage_in]],
                                                      constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                                      texture2d<float> light [[texture(0)]],
                                                      texture2d<float> surface [[texture(1)]]) {
    return smoothedTracedLight(in.position.xy, 4, true, pass, light, surface);
}

#endif
#endif
