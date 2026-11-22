// IBL.metal
// Turns an environment cubemap into the three textures of image-based lighting:
//   - the irradiance map (diffuse light arriving at a surface, for every normal),
//   - the prefiltered map (blurry reflections, one roughness per mip level),
//   - the BRDF lookup table (the part of the specular integral that does not depend
//     on the environment).
// Each function renders one face of one mip level with the full-screen triangle.
// See the "Diffuse Irradiance" and "Specular IBL" chapters of the site.

#include "Lighting.metal"
#include "Sky.metal"

// The Van der Corput sequence: mirrors the bits of i around the binary point.
static float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;         // divide by 2^32
}

// Sample i of `count` well-spread points in the unit square.
static float2 hammersley(uint i, uint count) {
    return float2(float(i) / float(count), radicalInverse(i));
}

// Turns a direction given around +Z into the same direction around `normal`.
static float3 aroundNormal(float3 local, float3 normal) {
    float3 helper = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(helper, normal));
    float3 bitangent = cross(normal, tangent);
    return tangent * local.x + bitangent * local.y + normal * local.z;
}

// A halfway vector distributed like the GGX lobe of the given roughness.
static float3 importanceSampleGGX(float2 u, float3 normal, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * kPi * u.x;
    // Kept within 0 and 1: with no roughness the fraction is a number over itself, which the GPU's
    // quick division can make a hair more than 1, and the root of what is then below zero is not a
    // number. One such sample spoils the whole texel it is added to.
    float cosTheta = sqrt(saturate((1.0 - u.y) / max(1.0 + (a * a - 1.0) * u.y, 1e-7)));
    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    return aroundNormal(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), normal);
}

// ---------------------------------------------------------------- diffuse irradiance

fragment float4 fragment_irradiance(FullscreenOut in [[stage_in]],
                                    constant CaptureUniforms &capture [[buffer(MMBufferPass)]],
                                    texturecube<float> environment [[texture(0)]],
                                    sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float3 normal = normalize(cubeDirection(capture.face, in.uv));

    // Directions distributed proportionally to the cosine of their angle with the
    // normal. With that distribution the cosine-weighted integral is a plain average.
    // A blurry mip level of the environment stands in for many more samples.
    const uint sampleCount = 128u;
    float3 sum = float3(0.0);
    for (uint i = 0u; i < sampleCount; i++) {
        float2 u = hammersley(i, sampleCount);
        float phi = 2.0 * kPi * u.x;
        float cosTheta = sqrt(1.0 - u.y);
        float sinTheta = sqrt(u.y);
        float3 direction = aroundNormal(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), normal);
        sum += environment.sample(linearSampler, direction, level(4.0)).rgb;
    }
    return float4(sum / float(sampleCount), 1.0);
}

// ---------------------------------------------------------------- prefiltered reflections

fragment float4 fragment_prefilter(FullscreenOut in [[stage_in]],
                                   constant CaptureUniforms &capture [[buffer(MMBufferPass)]],
                                   texturecube<float> environment [[texture(0)]],
                                   sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    // The usual simplification: the view direction equals the normal.
    float3 N = normalize(cubeDirection(capture.face, in.uv));
    float3 V = N;
    // A mirror (the first level) reflects one direction: the sky's own, read as it is. (Through the
    // lobe below it came out not a number in places, and every water and smooth surface near the
    // camera, which reads this level, went flat.)
    if (capture.roughness <= 0.0) {
        return float4(environment.sample(linearSampler, N, level(0.0)).rgb, 1.0);
    }

    const uint sampleCount = 96u;
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < sampleCount; i++) {
        float3 H = importanceSampleGGX(hammersley(i, sampleCount), N, capture.roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = dot(N, L);
        if (NdotL <= 0.0) {
            continue;
        }

        // Where samples are sparse, read a blurrier mip level of the source: each
        // sample then stands for the whole area it represents, and the bright speckles
        // that plague prefiltered maps disappear.
        float NdotH = max(dot(N, H), 0.0);
        float pdf = distributionGGX(NdotH, capture.roughness) * NdotH / (4.0 * max(dot(H, V), 0.0001)) + 0.0001;
        float texelSolidAngle = 4.0 * kPi / (6.0 * capture.sourceSize * capture.sourceSize);
        float sampleSolidAngle = 1.0 / (float(sampleCount) * pdf);
        float mip = capture.roughness <= 0.0 ? 0.0 : max(0.5 * log2(sampleSolidAngle / texelSolidAngle), 0.0);

        sum += environment.sample(linearSampler, L, level(mip)).rgb * NdotL;
        weight += NdotL;
    }
    return float4(sum / max(weight, 0.0001), 1.0);
}

// ---------------------------------------------------------------- BRDF lookup table

// u is the cosine between normal and view, v is the roughness. The red channel is a
// scale and the green channel a bias to apply to F0.
fragment float4 fragment_brdf_lut(FullscreenOut in [[stage_in]]) {
    float NdotV = max(in.uv.x, 0.001);
    float roughness = in.uv.y;
    float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float3 N = float3(0.0, 0.0, 1.0);

    const uint sampleCount = 256u;
    float scale = 0.0;
    float bias = 0.0;
    for (uint i = 0u; i < sampleCount; i++) {
        float3 H = importanceSampleGGX(hammersley(i, sampleCount), N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        if (NdotL <= 0.0) {
            continue;
        }
        float G = geometrySmithEnvironment(NdotV, NdotL, roughness);
        float visibility = G * VdotH / max(NdotH * NdotV, 0.0001);
        float fresnel = pow(1.0 - VdotH, 5.0);
        scale += (1.0 - fresnel) * visibility;
        bias += fresnel * visibility;
    }
    return float4(scale / float(sampleCount), bias / float(sampleCount), 0.0, 1.0);
}
