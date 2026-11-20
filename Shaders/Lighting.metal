// Lighting.metal
// Physically based shading: the Cook-Torrance reflectance model, the three kinds of
// light, cascaded shadows for the sun, image-based lighting and fog.
// The PBR chapters of the LearnMetal tutorials (gabrielevierti.github.io/learnmetal) derive every formula used here.

#include "Common.metal"

// ---------------------------------------------------------------- the reflectance model

// D: how many microfacets line up with the halfway vector.
static float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * denominator * denominator);
}

// G: microfacets shadowing each other. `k` depends on what the light is: a single
// direction (direct lighting) or the whole environment.
static float geometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

static float geometrySmithDirect(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

static float geometrySmithEnvironment(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness / 2.0;
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

// F: more reflection at grazing angles.
static float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// The same, softened for rough surfaces. Used for the environment, which has no single halfway vector.
static float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Everything the lighting needs to know about the point being shaded.
struct Surface {
    float3 position;
    float3 normal;
    float3 view;            // from the surface toward the camera
    float3 albedo;
    float3 F0;              // reflectance when looking straight at the surface
    float metallic;
    float roughness;
    float occlusion;        // 1 is fully open to the environment
    float NdotV;
};

// Light arriving from direction L with the given radiance, reflected toward the camera.
static float3 reflectedLight(Surface surface, float3 L, float3 radiance) {
    float NdotL = dot(surface.normal, L);
    if (NdotL <= 0.0) {
        return float3(0.0);
    }
    float3 H = normalize(surface.view + L);
    float NdotH = max(dot(surface.normal, H), 0.0);

    float D = distributionGGX(NdotH, surface.roughness);
    float G = geometrySmithDirect(surface.NdotV, NdotL, surface.roughness);
    float3 F = fresnelSchlick(max(dot(H, surface.view), 0.0), surface.F0);

    float3 specular = D * G * F / (4.0 * surface.NdotV * NdotL + 0.0001);
    float3 diffuse = (1.0 - F) * (1.0 - surface.metallic) * surface.albedo / kPi;
    return (diffuse + specular) * radiance * NdotL;
}

// ---------------------------------------------------------------- lights

struct LightSample {
    float3 direction;       // from the surface toward the light
    float3 radiance;        // what arrives at the surface
};

static LightSample sampleLight(LightData light, float3 position) {
    LightSample result;
    if (light.type == MMLightDirectional) {
        result.direction = -light.direction;
        result.radiance = light.color;
        return result;
    }

    float3 toLight = light.position - position;
    float distanceSquared = max(dot(toLight, toLight), 0.0001);
    float distance = sqrt(distanceSquared);
    result.direction = toLight / distance;

    // Inverse-square falloff, multiplied by a window that reaches exactly zero at the
    // light's range. Without the window a light would never quite stop, and every
    // light would have to be evaluated for every pixel of the scene.
    float ratio = distance / light.range;
    float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    float attenuation = window * window / (distanceSquared + 1.0);

    if (light.type == MMLightSpot) {
        float cosAngle = dot(-result.direction, light.direction);
        float cone = saturate(cosAngle * light.spotScale + light.spotOffset);
        attenuation *= cone * cone;
    }
    result.radiance = light.color * attenuation;
    return result;
}

// ---------------------------------------------------------------- sun shadows

// How much sunlight reaches the point: 0 in full shadow, 1 fully lit.
static float sunVisibility(float3 worldPosition, float3 normal, float viewDepth,
                           constant FrameUniforms &frame,
                           depth2d_array<float> shadowMap, sampler shadowSampler) {
    if (frame.cascadeCount == 0 || viewDepth >= frame.shadowDistance) {
        return 1.0;
    }

    // The first cascade whose slice of the view contains this point.
    uint cascade = frame.cascadeCount - 1;
    for (uint i = 0; i < frame.cascadeCount; i++) {
        if (viewDepth < frame.cascadeSplits[i]) {
            cascade = i;
            break;
        }
    }

    // Look up the shadow map from a point pushed slightly off the surface along its
    // normal, by an amount proportional to the size of a texel. This removes "shadow
    // acne" without the detached shadows a large depth bias would cause.
    float3 offsetPosition = worldPosition + normal * (frame.cascadeTexelSize[cascade] * frame.shadowNormalOffset);
    float4 clip = frame.cascadeViewProjection[cascade] * float4(offsetPosition, 1.0);
    float2 uv = clip.xy * float2(0.5, -0.5) + 0.5;         // NDC y is up, texture v is down
    float depth = clip.z - frame.shadowBias;

    // Percentage-closer filtering: average nine comparisons. The sampler compares and
    // interpolates in hardware, so each tap is already a smooth 2 x 2 comparison.
    float texel = 1.0 / frame.shadowMapSize;
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += shadowMap.sample_compare(shadowSampler, uv + float2(float(x), float(y)) * texel, cascade, depth);
        }
    }
    lit /= 9.0;

    // Fade the shadows out over the last tenth of their range instead of cutting them off.
    float fade = saturate((viewDepth - frame.shadowDistance * 0.9) / (frame.shadowDistance * 0.1));
    return mix(lit, 1.0, fade);
}

// ---------------------------------------------------------------- ray-traced sun shadows
//
// Compiled only where the GPU can trace rays (the engine defines MOLTEN_RAYTRACING).
// Instead of asking a shadow map "is something nearer to the sun than this point?",
// ask the scene itself: send a ray from the point toward the sun and see whether it
// hits a triangle. There is no resolution to run out of and no bias to tune per
// cascade, and because the sun is a disk and not a point, sending each ray to a
// different place on the disk gives shadows that are sharp where an object touches
// the ground and soft far from it, as real ones are.

#if MOLTEN_RAYTRACING
#include <metal_raytracing>

// A number in [0, 1) that differs from pixel to pixel in a pattern the eye reads as
// even grain and not as blotches ("interleaved gradient noise", Jimenez 2014).
static float pixelNoise(float2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, float2(0.06711056, 0.00583715))));
}

// How much sunlight reaches the point: 0 in full shadow, 1 fully lit.
// `toSun` points from the surface to the sun; `pixel` is the pixel's position on screen.
static float sunVisibilityTraced(float3 worldPosition, float3 normal, float3 toSun, float viewDepth, float2 pixel,
                                 constant FrameUniforms &frame,
                                 raytracing::instance_acceleration_structure scene) {
    if (frame.shadowRayCount == 0) {
        return 1.0;
    }
    // A surface facing away from the sun is dark whatever is in the way: no ray needed.
    if (dot(normal, toSun) <= 0.0) {
        return 0.0;
    }

    // Any hit will do: the question is only whether the sun is hidden, not by what.
    raytracing::intersector<raytracing::triangle_data, raytracing::instancing> tracer;
    tracer.accept_any_intersection(true);
    tracer.assume_geometry_type(raytracing::geometry_type::triangle);
    tracer.force_opacity(raytracing::forced_opacity::opaque);

    // Two directions across the sun's disk, and how wide the disk looks from here.
    float3 side = normalize(cross(toSun, abs(toSun.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0)));
    float3 up = cross(toSun, side);
    float diskRadius = tan(frame.sunAngularRadius);

    // Start a little off the surface, farther for distant pixels, whose positions are less
    // exact: otherwise a ray may hit the very triangle it starts from.
    raytracing::ray shadowRay;
    shadowRay.origin = worldPosition + normal * (0.002 + viewDepth * 0.0005);
    shadowRay.min_distance = 0.001;
    shadowRay.max_distance = 10000.0;

    // The rays of one pixel are spread over the disk in a spiral; the spiral is turned
    // by a different angle in each pixel, so that neighbors sample different places and
    // the few rays of each add up to a smooth edge across several pixels.
    float turn = pixelNoise(pixel) * 2.0 * kPi;
    float lit = 0.0;
    for (uint i = 0; i < frame.shadowRayCount; i++) {
        float radius = sqrt((float(i) + 0.5) / float(frame.shadowRayCount)) * diskRadius;
        float angle = float(i) * 2.39996323 + turn;             // the golden angle
        shadowRay.direction = normalize(toSun + (side * cos(angle) + up * sin(angle)) * radius);
        raytracing::intersection_result<raytracing::triangle_data, raytracing::instancing> hit = tracer.intersect(shadowRay, scene);
        lit += hit.type == raytracing::intersection_type::none ? 1.0 : 0.0;
    }
    return lit / float(frame.shadowRayCount);
}

#if MOLTEN_EXTRAS
// The light of the thing a ray hit, as seen from where the ray came from: its color under
// the sky and, if the sun reaches it, the sun; and its own glow. Which way the thing faces
// there is not known (the scene for rays holds shapes, not their surfaces), so it is taken
// to face back along the ray, tipped a little upward: right for what a floor sees of a
// wall and a wall of a floor, and never far wrong.
static float3 tracedSurfaceLight(float3 hitPoint, float3 backAlongRay, TracedSurface surface, constant FrameUniforms &frame,
                                 raytracing::instance_acceleration_structure scene, texturecube<float> irradianceMap, sampler linearSampler) {
    float3 facing = normalize(backAlongRay * 0.75 + float3(0.0, 0.35, 0.0));
    float3 toSun = -frame.sunDirection;
    raytracing::intersector<raytracing::triangle_data, raytracing::instancing> tracer;
    tracer.accept_any_intersection(true);
    tracer.assume_geometry_type(raytracing::geometry_type::triangle);
    tracer.force_opacity(raytracing::forced_opacity::opaque);
    raytracing::ray sunRay;
    sunRay.origin = hitPoint + backAlongRay * 0.02;
    sunRay.direction = toSun;
    sunRay.min_distance = 0.005;
    sunRay.max_distance = 1000.0;
    raytracing::intersection_result<raytracing::triangle_data, raytracing::instancing> blocked = tracer.intersect(sunRay, scene);
    float sun = blocked.type == raytracing::intersection_type::none ? max(dot(facing, toSun), 0.0) : 0.0;
    // What leaves a matte surface: its color times the light that falls on it, the sun's spread over the half-sphere.
    float3 light = irradianceMap.sample(linearSampler, facing).rgb * frame.environmentIntensity + frame.sunColor * (sun / kPi);
    return surface.baseColor.rgb * light + surface.emissive.rgb;
}

// What reaches the point from around it. Short rays over the half of the world the
// surface faces: the share of them that get away is how much of the sky's light arrives
// (in `a`); and, where that is asked for, what the others hit sends its own light back
// (in `rgb`): one bounce of global illumination.
static float4 ambientTraced(float3 worldPosition, float3 normal, float viewDepth, float2 pixel, constant FrameUniforms &frame,
                            raytracing::instance_acceleration_structure scene, constant TracedSurface *surfaces,
                            texturecube<float> irradianceMap, sampler linearSampler) {
    if (frame.occlusionRayCount == 0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    raytracing::intersector<raytracing::triangle_data, raytracing::instancing> tracer;
    tracer.accept_any_intersection(false);
    tracer.assume_geometry_type(raytracing::geometry_type::triangle);
    tracer.force_opacity(raytracing::forced_opacity::opaque);

    float3 side = normalize(cross(normal, abs(normal.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0)));
    float3 forward = cross(normal, side);
    raytracing::ray occlusionRay;
    occlusionRay.origin = worldPosition + normal * (0.004 + viewDepth * 0.0008);
    occlusionRay.min_distance = 0.002;
    bool bounces = frame.bounceIntensity > 0.0;
    const float reach = 3.0;                                    // meters: what is farther does not darken a corner
    occlusionRay.max_distance = bounces ? 40.0 : reach;         // but it can still light one

    float turn = pixelNoise(pixel) * 2.0 * kPi;
    float open = 0.0;
    float3 bounced = float3(0.0);
    for (uint i = 0; i < frame.occlusionRayCount; i++) {
        // Spread over the hemisphere, more toward the normal, where light counts for more.
        float share = (float(i) + 0.5) / float(frame.occlusionRayCount);
        float radius = sqrt(share);
        float angle = float(i) * 2.39996323 + turn;
        float3 direction = normalize(side * (cos(angle) * radius) + forward * (sin(angle) * radius) + normal * sqrt(max(1.0 - share, 0.0)));
        occlusionRay.direction = direction;
        raytracing::intersection_result<raytracing::triangle_data, raytracing::instancing> hit = tracer.intersect(occlusionRay, scene);
        if (hit.type == raytracing::intersection_type::none) {
            open += 1.0;
            continue;
        }
        // Something near hides more than something at the end of the ray's reach.
        open += saturate(hit.distance / reach) * 0.6;
        if (bounces) {
            bounced += tracedSurfaceLight(occlusionRay.origin + direction * hit.distance, -direction, surfaces[hit.instance_id], frame, scene, irradianceMap, linearSampler);
        }
    }
    float count = float(frame.occlusionRayCount);
    return float4(bounced * (frame.bounceIntensity / count), open / count);
}

// What a smooth surface mirrors: `a` is 0 where the mirrored direction is clear to the
// sky, and otherwise 1, with in `rgb` the light of the thing that is in the way.
static float4 reflectionTraced(float3 worldPosition, float3 normal, float3 view, float roughness, float viewDepth, float2 pixel,
                               constant FrameUniforms &frame, raytracing::instance_acceleration_structure scene, constant TracedSurface *surfaces,
                               texturecube<float> irradianceMap, sampler linearSampler) {
    if (frame.tracesReflections == 0) {
        return float4(0.0);
    }
    raytracing::intersector<raytracing::triangle_data, raytracing::instancing> tracer;
    tracer.accept_any_intersection(false);
    tracer.assume_geometry_type(raytracing::geometry_type::triangle);
    tracer.force_opacity(raytracing::forced_opacity::opaque);
    raytracing::ray mirrorRay;
    mirrorRay.origin = worldPosition + normal * (0.004 + viewDepth * 0.0008);
    // A rougher surface mirrors less sharply: its ray is thrown a little off, differently at each pixel.
    float3 mirrored = reflect(-view, normal);
    float spread = roughness * roughness * 0.6;
    float3 off = float3(pixelNoise(pixel) - 0.5, pixelNoise(pixel + 17.0) - 0.5, pixelNoise(pixel + 43.0) - 0.5);
    mirrorRay.direction = normalize(mirrored + off * spread);
    mirrorRay.min_distance = 0.002;
    mirrorRay.max_distance = 600.0;
    raytracing::intersection_result<raytracing::triangle_data, raytracing::instancing> hit = tracer.intersect(mirrorRay, scene);
    if (hit.type == raytracing::intersection_type::none) {
        return float4(0.0);
    }
    float3 seen = tracedSurfaceLight(mirrorRay.origin + mirrorRay.direction * hit.distance, -mirrorRay.direction, surfaces[hit.instance_id], frame, scene,
                                     irradianceMap, linearSampler);
    return float4(seen, 1.0);
}
#endif
#endif

// ---------------------------------------------------------------- image-based lighting

// Light from the whole environment: the irradiance map for the diffuse part, the
// prefiltered map and the BRDF lookup table (the "split sum") for the specular part.
// `mirrorShare` is how much of the mirrored part is kept (1: all of it). Where a traced ray
// found the mirrored direction hidden, that part is replaced by what the ray saw; the matte
// part, which comes from every direction, is never taken away with it.
static float3 environmentLight(Surface surface, constant FrameUniforms &frame,
                               texturecube<float> irradianceMap, texturecube<float> prefilteredMap,
                               texture2d<float> brdfLUT, sampler linearSampler, float mirrorShare) {
    float3 F = fresnelSchlickRoughness(surface.NdotV, surface.F0, surface.roughness);
    float3 diffuse = (1.0 - F) * (1.0 - surface.metallic) * surface.albedo
                   * irradianceMap.sample(linearSampler, surface.normal).rgb;

    float3 R = reflect(-surface.view, surface.normal);
    float mip = surface.roughness * (frame.prefilteredMipCount - 1.0);
    float3 prefiltered = prefilteredMap.sample(linearSampler, R, level(mip)).rgb;
    float2 brdf = brdfLUT.sample(linearSampler, float2(surface.NdotV, surface.roughness)).rg;
    float3 specular = prefiltered * (F * brdf.x + brdf.y);

    return (diffuse + specular * mirrorShare) * surface.occlusion * frame.environmentIntensity;
}

// ---------------------------------------------------------------- fog

#if MOLTEN_EXTRAS
// Fog that lies low and thins with height, as mist in a valley does, and glows where one
// looks through it toward the sun. The amount of fog along the line of sight is worked
// out exactly for a density that falls off evenly with height.
// How much of the fog between the eye and a point is in sunlight: the line of sight is
// walked in steps, each asked whether the sun reaches it (of the shadow maps, as
// surfaces are), and counted by how much fog is there. 1 where nothing shades the way.
static float fogInSunlight(float3 worldPosition, float3 cameraPosition, float viewDepth, float2 pixel, constant FrameUniforms &frame,
                           depth2d_array<float> shadowMap, sampler shadowSampler) {
    if (frame.fogShaftSteps == 0 || frame.cascadeCount == 0) {
        return 1.0;
    }
    float3 path = worldPosition - cameraPosition;
    float reach = min(1.0, frame.shadowDistance / max(viewDepth, 0.001));      // beyond the shadow maps nothing is known: lit
    float steps = float(frame.fogShaftSteps);
    float start = pixelNoise(pixel);                            // each pixel starts a little differently: grain instead of bands
    float lit = 0.0, weight = 0.0;
    for (uint i = 0; i < frame.fogShaftSteps; i++) {
        float t = (float(i) + start) / steps * reach;
        float3 at = cameraPosition + path * t;
        float thick = frame.fogHeightFalloff > 0.00001 ? exp(clamp(-frame.fogHeightFalloff * at.y, -40.0, 40.0)) : 1.0;
        lit += thick * sunVisibility(at, float3(0.0, 1.0, 0.0), viewDepth * t, frame, shadowMap, shadowSampler);
        weight += thick;
    }
    float inside = weight > 0.0 ? lit / weight : 1.0;
    return mix(1.0, inside, reach);
}

static float3 applyFogAlong(float3 color, float3 worldPosition, float3 cameraPosition, float sunShare, constant FrameUniforms &frame) {
    float3 path = worldPosition - cameraPosition;
    float range = length(path);
    float thickness = frame.fogDensity * range;
    float falloff = frame.fogHeightFalloff;
    if (falloff > 0.00001) {
        float climb = clamp(falloff * path.y, -40.0, 40.0);
        float along = abs(climb) > 0.0001 ? (1.0 - exp(-climb)) / climb : 1.0;
        thickness *= exp(clamp(-falloff * cameraPosition.y, -40.0, 40.0)) * along;
    }
    float amount = 1.0 - exp(-max(thickness, 0.0));
    float towardSun = max(dot(path / max(range, 0.0001), -frame.sunDirection), 0.0);
    // Fog in the sun glows, most toward the sun; fog in shadow is only the color of the sky's light in it.
    float3 fog = frame.fogColor * mix(0.55, 1.0, sunShare) + frame.sunColor * (frame.fogSunScatter * sunShare * (0.012 + 0.04 * pow(towardSun, 6.0)));
    return mix(color, fog, saturate(amount));
}
#endif

static float3 applyFog(float3 color, float viewDepth, constant FrameUniforms &frame) {
    float amount = 1.0 - exp(-frame.fogDensity * viewDepth);
    return mix(color, frame.fogColor, amount);
}
