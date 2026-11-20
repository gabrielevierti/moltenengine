// Forward.metal
// The fragment stage of the main pass: one function shades a pixel completely,
// with every light, the sun's shadow, the environment and fog.
//
// "Forward" rendering is the natural fit for Apple GPUs. They find the visible
// surface of every pixel before any fragment function runs, so hidden surfaces cost
// nothing; it works with multisampling; and transparent objects use the same code.

#include "Lighting.metal"
#include "Mesh.metal"

// What a material's textures and factors say about this pixel.
struct MaterialSample {
    float4 baseColor;       // rgb in linear space, a is opacity
    float3 normal;          // world space, already perturbed by the normal map
    float3 emissive;
    float metallic;
    float roughness;
    float occlusion;
    float water;            // above zero: water, and how fine its waves are
    float waveHeight;
#if MOLTEN_EXTRAS
    float mirror;           // how much of the sky a smooth surface may mirror: 1 unless a traced ray says it is hidden
    float3 mirrored;        // and the light of what hides it, to mirror in its place
    float3 bounced;         // light that reaches the surface from what is around it: one bounce
    float fogSun;           // how much of the fog in front of it is in sunlight
#endif
#if MOLTEN_CUSTOM
    float custom;           // above zero: which hand-written surface shader the material uses
    float4 customValues[8]; // and its properties
#endif
};

static MaterialSample sampleMaterial(MeshVertexOut in, bool isFrontFacing,
                                     constant MaterialUniforms &material,
                                     texture2d<float> baseColorTexture, texture2d<float> normalTexture,
                                     texture2d<float> metallicRoughnessTexture, texture2d<float> occlusionTexture,
                                     texture2d<float> emissiveTexture, sampler materialSampler) {
    float2 uv = in.uv * material.uvScale;
    MaterialSample result;
    result.baseColor = material.baseColor * in.tint * baseColorTexture.sample(materialSampler, uv);

    // The tangent frame: T along u, B toward the top of the texture, N out of the surface.
    float3 N = normalize(in.normal);
    if (!isFrontFacing) {
        N = -N;                                         // the back of a double-sided surface
    }
    float3 T = normalize(in.tangent.xyz - N * dot(N, in.tangent.xyz));
    float3 B = cross(N, T) * in.tangent.w;
    float3 mapped = normalTexture.sample(materialSampler, uv).xyz * 2.0 - 1.0;
    mapped.xy *= material.normalScale;
    result.normal = normalize(T * mapped.x + B * mapped.y + N * mapped.z);

    // glTF packing: roughness in green, metalness in blue.
    float4 metallicRoughness = metallicRoughnessTexture.sample(materialSampler, uv);
    result.roughness = clamp(material.roughness * metallicRoughness.g, 0.045, 1.0);
    result.metallic = saturate(material.metallic * metallicRoughness.b);
    result.occlusion = 1.0 + material.occlusionStrength * (occlusionTexture.sample(materialSampler, uv).r - 1.0);
    result.emissive = material.emissive * emissiveTexture.sample(materialSampler, uv).rgb;
    result.water = material.water;
    result.waveHeight = material.waveHeight;
#if MOLTEN_EXTRAS
    result.mirror = 1.0;
    result.mirrored = float3(0.0);
    result.bounced = float3(0.0);
    result.fogSun = 1.0;
#endif
#if MOLTEN_CUSTOM
    result.custom = material.customShader;
    for (int i = 0; i < 8; i++) {
        result.customValues[i] = material.custom[i];
    }
#endif
    return result;
}

// Surface shaders written by hand (`.shader` assets) are put here by the engine, each in a
// namespace of its own, with `moltenCustomSurface` to run the one a material names.
// See Engine/Rendering/SurfaceShader.hpp. The next line is where; do not change it.
// MOLTEN_CUSTOM_SURFACES

// `sunVisibility` is how much of the sun reaches this pixel, 0 to 1: from the shadow
// maps or from tracing rays, whichever the entry point used.
static float4 shadePixel(MeshVertexOut in, MaterialSample m,
                         constant PassUniforms &pass, constant FrameUniforms &frame, constant LightData *lights,
                         float sunLight, texturecube<float> irradianceMap,
                         texturecube<float> prefilteredMap, texture2d<float> brdfLUT,
                         texture2d<float> ambientOcclusion, sampler linearSampler) {
#if MOLTEN_CUSTOM
    // The material's own shader has its say first: what it returns is what is lit.
    if (m.custom > 0.5) {
        m = moltenCustomSurface(m, in, pass.cameraPosition, pass.time);
    }
#endif
    // Water is part of every variant of the shaders: it needs nothing the plainest lacks.
    if (m.water > 0.0) {
        // Water: a level surface whose slope is a handful of waves crossing one another,
        // each moving at its own speed. Seen from above one looks into it; at a glancing
        // angle it is a mirror of the sky.
        float2 p = in.worldPosition.xz * m.water;
        float t = pass.time;
        float2 slope = float2(0.0);
        slope += float2(0.80, 0.60) * (cos(dot(p, float2(0.80, 0.60)) * 0.9 + t * 0.9) * 0.050);
        slope += float2(-0.50, 0.86) * (cos(dot(p, float2(-0.50, 0.86)) * 2.3 + t * 1.5) * 0.040);
        slope += float2(0.30, -0.95) * (cos(dot(p, float2(0.30, -0.95)) * 5.7 + t * 2.4) * 0.028);
        slope += float2(-0.90, -0.40) * (cos(dot(p, float2(-0.90, -0.40)) * 12.1 + t * 3.7) * 0.018);
        slope += float2(0.10, 0.99) * (cos(dot(p, float2(0.10, 0.99)) * 27.0 + t * 5.9) * 0.010);
        slope *= m.waveHeight;
        // The ripples are laid on the swell: a Water component moves the mesh itself, and its
        // normals lean with the large waves; on a flat sheet they point straight up.
        float3 swell = normalize(in.normal);
        m.normal = normalize(float3(swell.x - slope.x, max(swell.y, 0.05), swell.z - slope.y));
        // Water is shaded here, by itself, from what the frame says of the sun and the sky:
        // it does not go through the surface model below, which treats it as a dark
        // surface and leaves it black where it mirrors nothing.
        float3 toEye = normalize(pass.cameraPosition - in.worldPosition);
        float facing = saturate(dot(m.normal, toEye));
        float mirror = 0.02 + 0.98 * pow(saturate(1.0 - facing), 5.0);
        // Where waves break, white: a moving sheet says how much in its texture coordinate, from 2 up.
        float foam = saturate(in.uv.x - 2.0);
        foam = saturate(foam * 1.4 - 0.15 + foam * 0.3 * sin(dot(p, float2(31.0, 17.0)) + t));

        // The sky it mirrors: the horizon's color at a glancing angle, the zenith's from above.
        float3 mirrored = reflect(-toEye, m.normal);
        float height = saturate(abs(mirrored.y));
        float3 sky = mix(frame.skyHorizonColor, frame.skyZenithColor, pow(height, 0.6)) * frame.skyIntensity;
        // The light inside it: the sky's from all around and the sun's from above, in the water's own color.
        float3 skyAround = (frame.skyZenithColor + frame.skyHorizonColor * 2.0) / 3.0 * frame.skyIntensity * frame.environmentIntensity;
        float3 toSun = normalize(-frame.sunDirection);
        float3 sun = frame.sunColor * sunLight;
        float3 body = m.baseColor.rgb * (skyAround * 1.5 + sun * (0.25 + 0.35 * saturate(dot(m.normal, toSun))));
        // The sun's glitter on the ripples.
        float glint = pow(saturate(dot(mirrored, toSun)), 600.0) * 6.0 + pow(saturate(dot(mirrored, toSun)), 60.0) * 0.25;
        float3 water = mix(body, sky * frame.environmentIntensity, mirror) + sun * glint;
        float3 white = float3(0.86, 0.9, 0.92) * (skyAround + sun * 0.3);
        water = mix(water, white, foam);
        float opacity = saturate(mix(mix(m.baseColor.a, 1.0, mirror), 1.0, foam));
        // Whatever went wrong on the way (a normal of no length, a number out of range), it is still water.
        if (any(isnan(water)) || any(isinf(water))) {
            water = m.baseColor.rgb * (skyAround + frame.sunColor * 0.3);
            opacity = 1.0;
        }
        return float4(applyFog(water, in.viewDepth, frame), opacity);
    }
    Surface surface;
    surface.position = in.worldPosition;
    surface.normal = m.normal;
    surface.view = normalize(pass.cameraPosition - in.worldPosition);
    surface.albedo = m.baseColor.rgb;
    surface.metallic = m.metallic;
    surface.roughness = m.roughness;
    surface.F0 = mix(float3(0.04), m.baseColor.rgb, m.metallic);
    surface.NdotV = max(dot(surface.normal, surface.view), 0.0001);

    // Screen-space ambient occlusion is looked up by pixel position.
    float screenOcclusion = ambientOcclusion.sample(linearSampler, in.position.xy / pass.viewportSize).r;
    surface.occlusion = m.occlusion * mix(1.0, screenOcclusion, frame.ambientOcclusionStrength);

    float3 color = m.emissive;
    for (uint i = 0; i < frame.lightCount; i++) {
        LightSample light = sampleLight(lights[i], surface.position);
        if (i == 0) {
            // By convention the shadow-casting sun is the first light. Without sun
            // shadows `sunLight` is 1 and this changes nothing.
            light.radiance *= sunLight;
        }
        color += reflectedLight(surface, light.direction, light.radiance);
    }
#if MOLTEN_EXTRAS
    // Where a traced ray found the mirrored sky hidden, a smooth surface gets its rough
    // share of the sky's light only: the part that does not depend on the direction.
    float3 sky = environmentLight(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler);
    float smoothness = 1.0 - surface.roughness;
    color += sky * mix(1.0, m.mirror, smoothness * smoothness);
    // What a traced ray found in the mirrored direction, as strongly as this surface mirrors at this angle.
    float3 mirrorColor = mix(float3(0.04), m.baseColor.rgb, m.metallic);
    float glancing = pow(1.0 - saturate(dot(m.normal, normalize(pass.cameraPosition - in.worldPosition))), 5.0);
    color += m.mirrored * (mirrorColor + (1.0 - mirrorColor) * glancing) * (smoothness * smoothness);
    // And the light bounced to it from its surroundings, taken up by what is not metal in it.
    color += m.bounced * m.baseColor.rgb * (1.0 - m.metallic) * m.occlusion;
    return float4(applyFogAlong(color, in.worldPosition, pass.cameraPosition, m.fogSun, frame), m.baseColor.a);
#else
    color += environmentLight(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler);

    return float4(applyFog(color, in.viewDepth, frame), m.baseColor.a);
#endif
}

// ---------------------------------------------------------------- entry points

fragment float4 fragment_pbr(MeshVertexOut in [[stage_in]],
                             bool isFrontFacing [[front_facing]],
                             constant PassUniforms &pass [[buffer(MMBufferPass)]],
                             constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                             constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                             constant LightData *lights [[buffer(MMBufferLights)]],
                             texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                             texture2d<float> normalTexture [[texture(MMTextureNormal)]],
                             texture2d<float> metallicRoughnessTexture [[texture(MMTextureMetallicRoughness)]],
                             texture2d<float> occlusionTexture [[texture(MMTextureOcclusion)]],
                             texture2d<float> emissiveTexture [[texture(MMTextureEmissive)]],
                             depth2d_array<float> shadowMap [[texture(MMTextureShadowMap)]],
                             texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                             texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                             texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                             texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler shadowSampler [[sampler(MMSamplerShadow)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    float sunLight = sunVisibility(in.worldPosition, normalize(in.normal), in.viewDepth, frame, shadowMap, shadowSampler);
#if MOLTEN_EXTRAS
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
#endif
    return shadePixel(in, m, pass, frame, lights, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler);
}

// The same, for materials with holes cut by the alpha channel (leaves, fences).
// It is a separate function on purpose: a fragment function that can discard defeats
// the GPU's hidden surface removal, even for the pixels it keeps, so only the
// materials that need it should pay for it.
fragment float4 fragment_pbr_masked(MeshVertexOut in [[stage_in]],
                                    bool isFrontFacing [[front_facing]],
                                    constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                    constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                    constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                    constant LightData *lights [[buffer(MMBufferLights)]],
                                    texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                                    texture2d<float> normalTexture [[texture(MMTextureNormal)]],
                                    texture2d<float> metallicRoughnessTexture [[texture(MMTextureMetallicRoughness)]],
                                    texture2d<float> occlusionTexture [[texture(MMTextureOcclusion)]],
                                    texture2d<float> emissiveTexture [[texture(MMTextureEmissive)]],
                                    depth2d_array<float> shadowMap [[texture(MMTextureShadowMap)]],
                                    texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                    texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                                    texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                                    texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                                    sampler materialSampler [[sampler(MMSamplerMaterial)]],
                                    sampler shadowSampler [[sampler(MMSamplerShadow)]],
                                    sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    if (m.baseColor.a < material.alphaCutoff) {
        discard_fragment();
    }
    m.baseColor.a = 1.0;
    float sunLight = sunVisibility(in.worldPosition, normalize(in.normal), in.viewDepth, frame, shadowMap, shadowSampler);
#if MOLTEN_EXTRAS
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
#endif
    return shadePixel(in, m, pass, frame, lights, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler);
}

// The same two functions with the sun's shadow traced instead of looked up. They are
// separate entry points, and not a branch, so that a frame without ray tracing uses
// functions that do not mention the scene's structure at all and need nothing bound for it.
#if MOLTEN_RAYTRACING
fragment float4 fragment_pbr_traced(MeshVertexOut in [[stage_in]],
                             bool isFrontFacing [[front_facing]],
                             constant PassUniforms &pass [[buffer(MMBufferPass)]],
                             constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                             constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                             constant LightData *lights [[buffer(MMBufferLights)]],
                             texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                             texture2d<float> normalTexture [[texture(MMTextureNormal)]],
                             texture2d<float> metallicRoughnessTexture [[texture(MMTextureMetallicRoughness)]],
                             texture2d<float> occlusionTexture [[texture(MMTextureOcclusion)]],
                             texture2d<float> emissiveTexture [[texture(MMTextureEmissive)]],
                             raytracing::instance_acceleration_structure tracedScene [[buffer(MMBufferAccelerationStructure)]],
#if MOLTEN_EXTRAS
                             constant TracedSurface *tracedSurfaces [[buffer(MMBufferTracedSurfaces)]],
#endif
                             texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                             texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                             texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                             texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    float sunLight = sunVisibilityTraced(in.worldPosition, normalize(in.normal), -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
#if MOLTEN_EXTRAS
    float4 around = ambientTraced(in.worldPosition, normalize(in.normal), in.viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    m.occlusion *= around.a;
    m.bounced = around.rgb;
    if (m.roughness < 0.5) {
        float4 seen = reflectionTraced(in.worldPosition, m.normal, normalize(pass.cameraPosition - in.worldPosition), m.roughness, in.viewDepth, in.position.xy, frame,
                                       tracedScene, tracedSurfaces, irradianceMap, linearSampler);
        m.mirror = 1.0 - seen.a * 0.88;
        m.mirrored = seen.rgb;
    }
#endif
    return shadePixel(in, m, pass, frame, lights, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler);
}

// The same, for materials with holes cut by the alpha channel (leaves, fences).
fragment float4 fragment_pbr_masked_traced(MeshVertexOut in [[stage_in]],
                                    bool isFrontFacing [[front_facing]],
                                    constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                    constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                    constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                    constant LightData *lights [[buffer(MMBufferLights)]],
                                    texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                                    texture2d<float> normalTexture [[texture(MMTextureNormal)]],
                                    texture2d<float> metallicRoughnessTexture [[texture(MMTextureMetallicRoughness)]],
                                    texture2d<float> occlusionTexture [[texture(MMTextureOcclusion)]],
                                    texture2d<float> emissiveTexture [[texture(MMTextureEmissive)]],
                                    raytracing::instance_acceleration_structure tracedScene [[buffer(MMBufferAccelerationStructure)]],
#if MOLTEN_EXTRAS
                             constant TracedSurface *tracedSurfaces [[buffer(MMBufferTracedSurfaces)]],
#endif
                                    texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                    texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                                    texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                                    texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                                    sampler materialSampler [[sampler(MMSamplerMaterial)]],
                                    sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    if (m.baseColor.a < material.alphaCutoff) {
        discard_fragment();
    }
    m.baseColor.a = 1.0;
    float sunLight = sunVisibilityTraced(in.worldPosition, normalize(in.normal), -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
#if MOLTEN_EXTRAS
    float4 around = ambientTraced(in.worldPosition, normalize(in.normal), in.viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    m.occlusion *= around.a;
    m.bounced = around.rgb;
    if (m.roughness < 0.5) {
        float4 seen = reflectionTraced(in.worldPosition, m.normal, normalize(pass.cameraPosition - in.worldPosition), m.roughness, in.viewDepth, in.position.xy, frame,
                                       tracedScene, tracedSurfaces, irradianceMap, linearSampler);
        m.mirror = 1.0 - seen.a * 0.88;
        m.mirrored = seen.rgb;
    }
#endif
    return shadePixel(in, m, pass, frame, lights, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler);
}
#endif

// No lighting at all: the base color, the emissive color and fog. For glowing things,
// skies painted on meshes, and debugging.
fragment float4 fragment_unlit(MeshVertexOut in [[stage_in]],
                               constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                               constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                               texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                               texture2d<float> emissiveTexture [[texture(MMTextureEmissive)]],
                               sampler materialSampler [[sampler(MMSamplerMaterial)]]) {
    float2 uv = in.uv * material.uvScale;
    float4 base = material.baseColor * in.tint * baseColorTexture.sample(materialSampler, uv);
    float3 color = base.rgb + material.emissive * emissiveTexture.sample(materialSampler, uv).rgb;
    return float4(applyFog(color, in.viewDepth, frame), base.a);
}

// ---------------------------------------------------------------- depth-only passes

// The depth pre-pass used by screen-space ambient occlusion: it stores the view-space
// normal of the nearest surface in a color texture (depth goes to the depth texture).
static float4 encodedViewNormal(MeshVertexOut in, bool isFrontFacing, constant PassUniforms &pass) {
    float3 normal = normalize(in.normal);
    if (!isFrontFacing) {
        normal = -normal;
    }
    float3 viewNormal = normalize((pass.view * float4(normal, 0.0)).xyz);
    return float4(viewNormal * 0.5 + 0.5, 1.0);
}

fragment float4 fragment_prepass(MeshVertexOut in [[stage_in]],
                                 bool isFrontFacing [[front_facing]],
                                 constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    return encodedViewNormal(in, isFrontFacing, pass);
}

// Cut-out materials (leaves, fences, grates) must leave their holes in the depth-only
// passes too, or their shadows and their ambient occlusion would be solid rectangles.
static bool isCutAway(MeshVertexOut in, constant MaterialUniforms &material,
                      texture2d<float> baseColorTexture, sampler materialSampler) {
    float alpha = material.baseColor.a * in.tint.a
                * baseColorTexture.sample(materialSampler, in.uv * material.uvScale).a;
    return alpha < material.alphaCutoff;
}

fragment float4 fragment_prepass_masked(MeshVertexOut in [[stage_in]],
                                        bool isFrontFacing [[front_facing]],
                                        constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                        constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                        texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                                        sampler materialSampler [[sampler(MMSamplerMaterial)]]) {
    if (isCutAway(in, material, baseColorTexture, materialSampler)) {
        discard_fragment();
    }
    return encodedViewNormal(in, isFrontFacing, pass);
}

// The shadow maps have no color attachment: this function returns nothing, and exists
// only to throw away the pixels that are cut out.
fragment void fragment_depth_masked(MeshVertexOut in [[stage_in]],
                                    constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                    texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                                    sampler materialSampler [[sampler(MMSamplerMaterial)]]) {
    if (isCutAway(in, material, baseColorTexture, materialSampler)) {
        discard_fragment();
    }
}
