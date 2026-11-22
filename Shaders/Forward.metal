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
    float4 sea;             // water with the sea's waves: their slope (x, z), foam, height; w of the second scale below
    float hasSea;           // 1 when `sea` was read
    float waterDepth;       // meters of water under this pixel, along the view (what is under it seen through); large if unknown
#if MOLTEN_EXTRAS
    float mirror;           // the share of the mirrored light a traced ray found and brings: 0 when no ray was sent
    float3 mirrored;        // and the light of what it found
    float3 bounced;         // light that reaches the surface from what is around it: one bounce
    float fogSun;           // how much of the fog in front of it is in sunlight
#endif
#if MOLTEN_CUSTOM
    float custom;           // above zero: which hand-written surface shader the material uses
    float4 customValues[8]; // and its properties
#endif
};

// How deep the water is under a pixel: from the opaque depth drawn before it (the prepass) to the
// water's own surface, along the view. Large where nothing is known.
static float waterDepthAt(MeshVertexOut in, constant PassUniforms &pass, constant FrameUniforms &frame, depth2d<float> sceneDepth) {
    if (frame.sceneDepthReady < 0.5 || is_null_texture(sceneDepth)) return 1000.0;
    uint2 pixel = uint2(in.position.xy);
    if (pixel.x >= sceneDepth.get_width() || pixel.y >= sceneDepth.get_height()) return 1000.0;
    float stored = sceneDepth.read(pixel);
    if (stored >= 1.0) return 1000.0;
    // The distance in front of the camera, from the depth: the perspective undone.
    float behind = pass.projection[3][2] / (stored + pass.projection[2][2]);
    return max(behind - in.viewDepth, 0.0);
}

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
    result.hasSea = 0.0;
    result.waterDepth = 1000.0;
    result.sea = float4(0.0);
    if (material.water > 0.0 && material.oceanScale > 0.0) {
        // The sea's waves, a patch that repeats, read twice: as it is, and smaller and turned, so
        // that the repeat does not show and the near water has ripples on its swell.
        float2 at = (in.worldPosition.xz + material.oceanOffset) * material.oceanScale;
        float4 large = normalTexture.sample(materialSampler, at);
        float2 turned = float2(at.x * 0.8 - at.y * 0.6, at.x * 0.6 + at.y * 0.8) * 4.7 + float2(0.37, 0.71);
        float4 small = normalTexture.sample(materialSampler, turned);
        result.sea = float4(large.xy + small.xy * 0.45, saturate(large.z + small.z * 0.3), large.w + small.w * 0.2);
        result.hasSea = 1.0;
    }
#if MOLTEN_EXTRAS
    result.mirror = 0.0;                // no traced reflection: the sky is what is mirrored
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
        // Water: the slope of its waves (the sea's own, worked out by a Fourier transform, or a
        // handful of crossing waves where there is none), then the sky it mirrors, the light
        // that comes out of it from below, the sun's glitter, and foam where waves break.
        float t = pass.time;
        float2 p = in.worldPosition.xz * m.water;
        float2 slope;
        float crest = 0.0;
        float seaFoam = 0.0;
        if (m.hasSea > 0.5) {
            slope = m.sea.xy;
            seaFoam = m.sea.z;
            crest = m.sea.w;
        } else {
            slope = float2(0.0);
            slope += float2(0.80, 0.60) * (cos(dot(p, float2(0.80, 0.60)) * 0.9 + t * 0.9) * 0.050);
            slope += float2(-0.50, 0.86) * (cos(dot(p, float2(-0.50, 0.86)) * 2.3 + t * 1.5) * 0.040);
            slope += float2(0.30, -0.95) * (cos(dot(p, float2(0.30, -0.95)) * 5.7 + t * 2.4) * 0.028);
            slope += float2(-0.90, -0.40) * (cos(dot(p, float2(-0.90, -0.40)) * 12.1 + t * 3.7) * 0.018);
            slope += float2(0.10, 0.99) * (cos(dot(p, float2(0.10, 0.99)) * 27.0 + t * 5.9) * 0.010);
        }
        slope *= m.waveHeight;
        // Far away the small waves blur into a smoother sheet, as they do to the eye.
        slope *= mix(1.0, 0.45, saturate(in.viewDepth / 600.0));
        float3 swell = normalize(in.normal);
        float3 N = normalize(float3(swell.x - slope.x, max(swell.y, 0.05), swell.z - slope.y));
        m.normal = N;
        float3 toEye = normalize(pass.cameraPosition - in.worldPosition);
        float3 toSun = normalize(-frame.sunDirection);
        float facing = saturate(dot(N, toEye));
        // Fresnel, for water (it reflects 2% straight down, all of it at a grazing angle).
        float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);

        // The sky it mirrors: the environment the sky was captured into, clouds and sunset and
        // all. A ray that would go into the water is bent back just above the horizon.
        float3 mirrored = reflect(-toEye, N);
        mirrored.y = max(mirrored.y, 0.015);
        mirrored = normalize(mirrored);
        float3 sky = prefilteredMap.sample(linearSampler, mirrored, level(0.5)).rgb * frame.environmentIntensity;

        // The light that comes out of the water: the sky's from all around, the sun's from
        // above, in the water's color; brighter green-blue through the crests the sun shines
        // through (light scattered inside the wave).
        float3 around = irradianceMap.sample(linearSampler, float3(0.0, 1.0, 0.0)).rgb * frame.environmentIntensity;
        float3 sun = frame.sunColor * sunLight;
        float3 deep = m.baseColor.rgb;
        // Shallow water is clear and takes the color of what is under it; deep water its own.
        float depthIn = m.waterDepth;
        float shallow = exp(-depthIn * 0.6);
        float3 body = deep * (around * 1.2 + sun * (0.15 + 0.2 * saturate(dot(N, toSun))));
        body = mix(body, body * 0.6 + float3(0.12, 0.2, 0.18) * (around + sun * 0.2) * 0.5, shallow * 0.5);
        float through = pow(saturate(dot(toEye, -toSun) * 0.5 + 0.5), 4.0) * saturate(crest * 0.6 + 0.5) * saturate(1.0 - toEye.y * 1.5);
        body += deep * float3(0.7, 1.9, 1.6) * sun * through * 0.6 * saturate(m.waveHeight);

        // The sun's glitter: a sharp highlight on a nearly smooth surface (GGX, roughness 0.06),
        // with a wider sheen around it.
        float3 halfway = normalize(toSun + toEye);
        float nh = saturate(dot(N, halfway));
        const float a2 = 0.06 * 0.06 * 0.06 * 0.06;
        float d = nh * nh * (a2 - 1.0) + 1.0;
        float glitter = min(a2 / (kPi * d * d), 400.0) * (0.02 + 0.98 * pow(1.0 - saturate(dot(halfway, toEye)), 5.0)) * 0.25;
        float sheen = pow(saturate(dot(mirrored, toSun)), 40.0) * 0.15;
        float3 water = mix(body, sky, fresnel) + sun * (glitter + sheen) * saturate(dot(N, toSun) * 4.0);

        // Foam where waves break: the sea's own, or what a Water's moving sheet says (its texture
        // coordinate, from 2 up); patchy, as foam is.
        float sheetFoam = saturate(in.uv.x - 2.0);
        float foam = max(sheetFoam, saturate(seaFoam * 1.2 - 0.2) * saturate(m.waveHeight));
        // Where the water meets something that stands in it (a rock, a pier, a hull, the shore):
        // a ring of foam that the waves push and pull, thicker the rougher the sea.
        float wash = 0.35 + 0.25 * saturate(m.waveHeight);
        float lap = 0.5 + 0.5 * sin(t * 1.6 + dot(in.worldPosition.xz, float2(0.9, 0.7)) * 2.0);
        float shore = (1.0 - smoothstep(0.0, wash * (0.7 + 0.6 * lap), depthIn)) * step(0.0, depthIn);
        float shoreRipples = 0.55 + 0.45 * sin(depthIn * 28.0 - t * 3.0);
        foam = max(foam, shore * shoreRipples);
        float breakup = 0.65 + 0.35 * sin(dot(in.worldPosition.xz, float2(3.1, 1.7)) + t * 0.7) * sin(dot(in.worldPosition.xz, float2(-1.3, 2.9)) - t * 0.4);
        foam = saturate(foam * breakup * 1.3);
        float3 white = float3(0.9, 0.93, 0.95) * (around + sun * (0.35 + 0.35 * saturate(dot(N, toSun))));
        water = mix(water, white, foam);
        // The shallower, the more of what is under it shows; how soon it hides it is the material's opacity.
        float murk = m.baseColor.a;
        float hides = 1.0 - exp(-depthIn * mix(0.25, 2.5, murk * murk));
        float opacity = saturate(mix(mix(hides, 1.0, fresnel), 1.0, foam));
        // Whatever went wrong on the way (a normal of no length, a number out of range), it is still water.
        if (any(isnan(water)) || any(isinf(water))) {
            water = m.baseColor.rgb * (around + frame.sunColor * 0.3);
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
        // (Each part is kept between nothing and a great deal: one that is not a number
        // would otherwise blacken the whole pixel, and the bloom around it.)
        color += clamp(reflectedLight(surface, light.direction, light.radiance), 0.0, 60000.0);
    }
#if MOLTEN_EXTRAS
    // The sky's light, matte and mirrored. Where a traced ray found something in the mirrored
    // direction (m.mirror is that share, faded out on rougher surfaces), what it saw takes the
    // place of the sky there, weighed exactly as the sky would have been: a rough plastic
    // barely mirrors anything, chrome mirrors everything.
    EnvironmentParts environment = environmentParts(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler);
    float traced = saturate(m.mirror);
    color += clamp(environment.diffuse + environment.specular * (1.0 - traced), 0.0, 60000.0);
    color += clamp(m.mirrored * environment.specularWeight * traced * frame.reflectionStrength, 0.0, 60000.0);
    // And the light bounced to it from its surroundings, taken up by what is not metal in it.
    color += clamp(m.bounced * m.baseColor.rgb * (1.0 - m.metallic) * m.occlusion, 0.0, 60000.0);
    return float4(applyFogAlong(color, in.worldPosition, pass.cameraPosition, m.fogSun, frame), m.baseColor.a);
#else
    color += clamp(environmentLight(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler), 0.0, 60000.0);

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
                             depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler shadowSampler [[sampler(MMSamplerShadow)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    float sunLight = sunVisibility(in.worldPosition, normalize(in.normal), in.viewDepth, frame, shadowMap, shadowSampler);
#if MOLTEN_EXTRAS
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
#endif
    if (m.water > 0.0) m.waterDepth = waterDepthAt(in, pass, frame, sceneDepth);
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
                             texture2d<float> tracedLight [[texture(MMTextureTracedLight)]],
                             texture2d<float> tracedSurface [[texture(MMTextureTracedSurface)]],
#endif
                             texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                             texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                             texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                             texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                             depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    float sunLight = sunVisibilityTraced(in.worldPosition, normalize(in.normal), -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
#if MOLTEN_EXTRAS
    // The light around the pixel: traced before this pass at half the size where that could
    // be done (a quarter of the rays, and smoothed), and otherwise here, a pixel at a time.
    float4 around;
    if (frame.tracedAmbientReady != 0) {
        float3 facing = isFrontFacing ? normalize(in.normal) : -normalize(in.normal);
        around = tracedAmbientAt(in.position.xy, in.worldPosition, facing, in.viewDepth, pass, tracedLight, tracedSurface);
    } else {
        around = ambientTraced(in.worldPosition, normalize(in.normal), in.viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    }
    m.occlusion *= around.a;
    m.bounced = around.rgb;
    if (m.roughness < frame.reflectionMaxRoughness) {
        float4 seen = reflectionTraced(in.worldPosition, m.normal, normalize(pass.cameraPosition - in.worldPosition), m.roughness, in.viewDepth, in.position.xy, frame,
                                       tracedScene, tracedSurfaces, irradianceMap, linearSampler);
        // How much the ray counts: all of it on a mirror, fading to nothing at the roughness where rays stop.
        float fade = saturate(1.0 - m.roughness / frame.reflectionMaxRoughness);
        m.mirror = seen.a * fade * fade;
        m.mirrored = seen.rgb;
    }
#endif
    if (m.water > 0.0) m.waterDepth = waterDepthAt(in, pass, frame, sceneDepth);
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
                                    texture2d<float> tracedLight [[texture(MMTextureTracedLight)]],
                                    texture2d<float> tracedSurface [[texture(MMTextureTracedSurface)]],
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
    // The light around the pixel: traced before this pass at half the size where that could
    // be done (a quarter of the rays, and smoothed), and otherwise here, a pixel at a time.
    float4 around;
    if (frame.tracedAmbientReady != 0) {
        float3 facing = isFrontFacing ? normalize(in.normal) : -normalize(in.normal);
        around = tracedAmbientAt(in.position.xy, in.worldPosition, facing, in.viewDepth, pass, tracedLight, tracedSurface);
    } else {
        around = ambientTraced(in.worldPosition, normalize(in.normal), in.viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    }
    m.occlusion *= around.a;
    m.bounced = around.rgb;
    if (m.roughness < frame.reflectionMaxRoughness) {
        float4 seen = reflectionTraced(in.worldPosition, m.normal, normalize(pass.cameraPosition - in.worldPosition), m.roughness, in.viewDepth, in.position.xy, frame,
                                       tracedScene, tracedSurfaces, irradianceMap, linearSampler);
        // How much the ray counts: all of it on a mirror, fading to nothing at the roughness where rays stop.
        float fade = saturate(1.0 - m.roughness / frame.reflectionMaxRoughness);
        m.mirror = seen.a * fade * fade;
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

#ifdef MOLTEN_EXTRAS
// The same for the pass that draws every cascade at once (vertex_shadow_detailed).
fragment void fragment_shadow_masked(ShadowDetailVertexOut in [[stage_in]],
                                     constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                     texture2d<float> baseColorTexture [[texture(MMTextureBaseColor)]],
                                     sampler materialSampler [[sampler(MMSamplerMaterial)]]) {
    float alpha = material.baseColor.a * in.alpha * baseColorTexture.sample(materialSampler, in.uv * material.uvScale).a;
    if (alpha < material.alphaCutoff) {
        discard_fragment();
    }
}
#endif
