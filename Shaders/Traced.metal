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
    float depth = sceneDepth.sample(pointSampler, in.uv);
    if (depth >= 1.0) {
        out.surface = float4(0.0, 0.0, 0.0, 1000000.0);         // the sky: nothing to light
        return out;
    }
    // Where the pixel is in the world, from how deep it is; and which way it faces.
    float4 clip = float4(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0, depth, 1.0);
    float4 world = pass.inverseViewProjection * clip;
    float3 position = world.xyz / world.w;
    float3 viewNormal = sceneNormal.sample(pointSampler, in.uv).xyz * 2.0 - 1.0;
    // The normals were drawn as the camera sees them: turned back to the world's directions.
    float3 normal = normalize(float3(dot(pass.view[0].xyz, viewNormal), dot(pass.view[1].xyz, viewNormal), dot(pass.view[2].xyz, viewNormal)));
    float viewDepth = -(pass.view * float4(position, 1.0)).z;
    out.surface = float4(normal, viewDepth);

    float nearness = 1.0 - smoothstep(0.7 * kTracedAmbientFar, kTracedAmbientFar, viewDepth);
    if (nearness <= 0.0) {
        return out;
    }
    float4 around = ambientTraced(position, normal, viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    out.light = float4(around.rgb * nearness, mix(1.0, around.a, nearness));
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
    if (here.a > 100000.0) {
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
