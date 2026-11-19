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
    return result;
}

// `sunVisibility` is how much of the sun reaches this pixel, 0 to 1: from the shadow
// maps or from tracing rays, whichever the entry point used.
static float4 shadePixel(MeshVertexOut in, MaterialSample m,
                         constant PassUniforms &pass, constant FrameUniforms &frame, constant LightData *lights,
                         float sunLight, texturecube<float> irradianceMap,
                         texturecube<float> prefilteredMap, texture2d<float> brdfLUT,
                         texture2d<float> ambientOcclusion, sampler linearSampler) {
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
    color += environmentLight(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler);

    return float4(applyFog(color, in.viewDepth, frame), m.baseColor.a);
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
                             texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                             texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                             texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                             texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    float sunLight = sunVisibilityTraced(in.worldPosition, normalize(in.normal), -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
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
