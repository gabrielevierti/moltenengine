// Water.metal
// Water one sees into.
//
// A sheet of color blended over what is under it never looks like water: water has no
// color painted on it. What reaches the eye from it is three things. The sky (or the hull,
// or the pier) mirrored in its surface, more of it the flatter one looks along it. What is
// under it, seen through it: the sand, the rocks, with the red taken out of their light
// first and then the green, the further the light has to come, which is all the color water
// has. And the sun: its glitter on the waves, its light gathered by them into bright lines
// that crawl over the bottom, foam where waves break and where the water meets the shore.
//
// To take the red out of what is under it, the fragment function has to know what is under
// it: the color already drawn at this pixel. Apple GPUs hand it over (`[[color(0)]]`, the
// picture as it stands), so the water is drawn with no blending at all: it reads what is
// there, does to it what water does, and writes the result. How far down the bottom is
// comes from the depth of what is opaque, drawn before the scene (MMTextureSceneDepth).
//
// Part of the extras. Without them (or on a GPU that cannot read the picture back) water
// is the simpler sheet of Forward.metal, blended.

#include "Forward.metal"
#include "Sky.metal"

#if MOLTEN_EXTRAS

// Sunlight gathered by the waves into bright lines on the bottom: two sheets of ripples
// drifting across each other, bent a little so that they do not look ruled. Light gathers
// along the crests of each, and most where two cross. 0 between the lines, about 1 on them.
static float waterCaustics(float2 p, float t) {
    float2 bend = float2(cloudValueNoise(p * 0.37 + float2(t * 0.050, 1.7)), cloudValueNoise(p * 0.37 + float2(5.2, t * 0.043))) - 0.5;
    float2 q = p * 1.25 + bend * 2.6;
    float a = cloudValueNoise(q + float2(t * 0.21, t * 0.12));
    float b = cloudValueNoise(q * 1.37 + float2(3.7 - t * 0.16, 9.1 + t * 0.19));
    float linesA = 1.0 - abs(a * 2.0 - 1.0), linesB = 1.0 - abs(b * 2.0 - 1.0);
    float lines = linesA * linesB;
    return lines * lines * lines * 2.6;
}

// The slope of the water's surface at a pixel, its foam and the height of its waves there:
// the sea's own (worked out by a Fourier transform, handed over as a picture), or a handful
// of crossing waves where there is none.
struct WaterSurface {
    float2 slope;
    float foam;
    float crest;
};

static WaterSurface waterSurfaceAt(MeshVertexOut in, constant MaterialUniforms &material, constant PassUniforms &pass,
                                   texture2d<float> seaTexture, sampler materialSampler) {
    WaterSurface surface;
    surface.foam = 0.0;
    surface.crest = 0.0;
    if (material.oceanScale > 0.0) {
        // A patch that repeats, read twice: as it is, and smaller and turned, so that the repeat
        // does not show and the near water has ripples on its swell.
        float2 at = (in.worldPosition.xz + material.oceanOffset) * material.oceanScale;
        float4 large = seaTexture.sample(materialSampler, at);
        float2 turned = float2(at.x * 0.8 - at.y * 0.6, at.x * 0.6 + at.y * 0.8) * 4.7 + float2(0.37, 0.71);
        float4 small = seaTexture.sample(materialSampler, turned);
        surface.slope = large.xy + small.xy * 0.45;
        surface.foam = saturate(large.z + small.z * 0.3);
        surface.crest = large.w + small.w * 0.2;
    } else {
        float t = pass.time;
        float2 p = in.worldPosition.xz * material.water;
        float2 slope = float2(0.0);
        slope += float2(0.80, 0.60) * (cos(dot(p, float2(0.80, 0.60)) * 0.9 + t * 0.9) * 0.050);
        slope += float2(-0.50, 0.86) * (cos(dot(p, float2(-0.50, 0.86)) * 2.3 + t * 1.5) * 0.040);
        slope += float2(0.30, -0.95) * (cos(dot(p, float2(0.30, -0.95)) * 5.7 + t * 2.4) * 0.028);
        slope += float2(-0.90, -0.40) * (cos(dot(p, float2(-0.90, -0.40)) * 12.1 + t * 3.7) * 0.018);
        slope += float2(0.10, 0.99) * (cos(dot(p, float2(0.10, 0.99)) * 27.0 + t * 5.9) * 0.010);
        surface.slope = slope;
    }
    return surface;
}

// The color of a pixel of water. `behind` is what was drawn there before it; `sunLight` how
// much of the sun reaches the surface; `mirror` what a traced ray found in the mirrored
// direction (its light, and 1 in `a`), nothing where no ray was sent or it reached the sky.
static float3 shadeWater(MeshVertexOut in, float3 behind, float depthIn, WaterSurface surface, float sunLight, float4 mirror,
                         constant MaterialUniforms &material, constant PassUniforms &pass, constant FrameUniforms &frame,
                         texturecube<float> irradianceMap, texturecube<float> prefilteredMap, sampler linearSampler) {
    float t = pass.time;
    float waveHeight = material.waveHeight;
    float3 deep = material.baseColor.rgb * in.tint.rgb;
    float murk = saturate(material.baseColor.a * in.tint.a);

    // ---- the surface: which way it faces here
    float3 toCamera = pass.cameraPosition - in.worldPosition;
    float range = max(length(toCamera), 0.001);
    float3 toEye = toCamera / range;
    float3 toSun = normalize(-frame.sunDirection);
    // Far away the small waves blur into a smoother sheet, as they do to the eye.
    float2 slope = surface.slope * waveHeight * mix(1.0, 0.35, saturate(in.viewDepth / 500.0));
    float3 swell = normalize(in.normal);
    float3 N = normalize(float3(swell.x - slope.x, max(swell.y, 0.05), swell.z - slope.y));
    float facing = saturate(dot(N, toEye));

    // ---- how much water the eye looks through to the bottom
    bool hasBottom = depthIn < 900.0;
    float along = hasBottom ? depthIn * range / max(in.viewDepth, 0.001) : 1000.0;      // along the line of sight, not the view's axis
    // Where the water meets what stands in it there is hardly any water: it thins to nothing,
    // and so do its mirror and its glitter, instead of ending in a line.
    float edge = hasBottom ? smoothstep(0.0, 0.07, depthIn) : 1.0;

    // What crossing a meter of this water does to light. Its color is what is left of white
    // light after a long way through it: red goes first, then green. `murk` (the material's
    // opacity) is how soon: a clear sea shows its bottom ten meters down, a pond not one.
    float3 left = deep / max(max(deep.r, max(deep.g, deep.b)), 0.001);
    float colorDepth = mix(16.0, 1.2, murk);
    float3 absorbed = -log(clamp(left, 0.03, 1.0)) / colorDepth;
    float clouded = mix(0.012, 0.6, murk * murk);              // what is stirred into it hides all colors alike
    float3 through = exp(-(absorbed + clouded) * along);
    float hidden = 1.0 - exp(-(clouded + 0.35 * (absorbed.r + absorbed.g + absorbed.b)) * along);

    // ---- the light around: the sky's, and the sun's where it is not shaded
    float3 around = irradianceMap.sample(linearSampler, float3(0.0, 1.0, 0.0)).rgb * frame.environmentIntensity;
    float3 sun = frame.sunColor * sunLight;

    // ---- what is under it, with the sun's light gathered into moving lines by the waves
    float3 bottom = behind;
    if (hasBottom) {
        float3 bottomAt = pass.cameraPosition - toCamera * ((in.viewDepth + depthIn) / max(in.viewDepth, 0.001));
        float down = max(in.worldPosition.y - bottomAt.y, 0.0);
        float lines = waterCaustics(bottomAt.xz * 1.1, t) * 0.7 + waterCaustics(bottomAt.xz * 2.3 + 11.0, t * 1.3) * 0.3;
        // Strongest a hand's depth down, gone a few meters down; none in shade or under a low sun.
        float strength = sunLight * saturate(toSun.y * 2.5) * smoothstep(0.02, 0.35, down) * exp(-down * 0.28) * saturate(waveHeight + 0.3);
        bottom *= 1.0 + (lines - 0.22) * strength * 1.15;
    }

    // ---- the water's own light: what it scatters back toward the eye, in its color
    float3 body = deep * (around * 0.95 + sun * (0.05 + 0.11 * saturate(dot(N, toSun))));
    // A wave between the eye and a low sun glows through its crest.
    float glow = pow(saturate(dot(toEye, -toSun) * 0.5 + 0.5), 4.0) * saturate(surface.crest * 0.6 + 0.5) * saturate(1.0 - toEye.y * 1.5);
    body += deep * float3(0.7, 1.9, 1.6) * sun * glow * 0.6 * saturate(waveHeight);
    float3 seen = bottom * through + body * hidden;

    // ---- what its surface mirrors: the sky, or what a traced ray found there
    float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);
    float3 mirrored = reflect(-toEye, N);
    mirrored.y = max(mirrored.y, 0.015);                        // a ray that would go into the water is bent back above the horizon
    mirrored = normalize(mirrored);
    // Sharp nearby; far away each pixel holds many waves, and mirrors a wider patch of sky.
    float blur = 0.4 + 1.6 * saturate(in.viewDepth / 300.0);
    float3 sky = prefilteredMap.sample(linearSampler, mirrored, level(blur)).rgb * frame.environmentIntensity;
    float3 reflection = mix(sky, mirror.rgb, saturate(mirror.a));
    float3 color = mix(seen, reflection, fresnel * edge);

    // ---- the sun's glitter: a sharp highlight on a nearly smooth surface, a wider sheen round it
    float3 halfway = normalize(toSun + toEye);
    float nh = saturate(dot(N, halfway));
    const float a2 = 0.06 * 0.06 * 0.06 * 0.06;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float glitter = min(a2 / (kPi * d * d), 400.0) * (0.02 + 0.98 * pow(1.0 - saturate(dot(halfway, toEye)), 5.0)) * 0.25;
    float sheen = pow(saturate(dot(mirrored, toSun)), 40.0) * 0.15;
    color += sun * (glitter + sheen) * saturate(dot(N, toSun) * 4.0) * edge * (1.0 - saturate(mirror.a));

    // ---- foam: where waves break, and where the water laps at what stands in it
    float sheetFoam = saturate(in.uv.x - 2.0);                  // a Water component's moving sheet says so in its texture coordinate
    float foam = max(sheetFoam, saturate(surface.foam * 1.2 - 0.2) * saturate(waveHeight));
    if (hasBottom) {
        // A band the waves push up the shore and pull back, thinner where the sea is calm,
        // with a brighter line at its edge and lace behind it.
        float reach = 0.10 + 0.16 * saturate(waveHeight);
        float lap = 0.5 + 0.5 * sin(t * 1.3 + dot(in.worldPosition.xz, float2(0.35, 0.27)) * 2.0);
        float band = 1.0 - smoothstep(0.0, reach * (0.55 + 0.75 * lap), depthIn);
        float lace = cloudValueNoise(in.worldPosition.xz * 5.5 + float2(t * 0.25, -t * 0.18)) * cloudValueNoise(in.worldPosition.xz * 13.0 - float2(t * 0.4, t * 0.3));
        float line = 1.0 - smoothstep(0.0, 0.035, depthIn);
        foam = max(foam, saturate(band * (0.35 + 2.4 * lace) + line * 0.8) * edge);
    }
    // Patchy, as foam is: holes open in it as it thins.
    float holes = cloudValueNoise(in.worldPosition.xz * 9.0 + float2(-t * 0.15, t * 0.1));
    foam = saturate((foam - holes * 0.35) * 1.6);
    float3 white = float3(0.92, 0.95, 0.97) * (around * 1.1 + sun * (0.35 + 0.35 * saturate(dot(N, toSun))));
    color = mix(color, white, foam);

    // Whatever went wrong on the way (a normal of no length, a number out of range), it is still water.
    if (any(isnan(color)) || any(isinf(color))) {
        color = deep * (around + frame.sunColor * 0.3);
    }
    return applyFogAlong(color, in.worldPosition, pass.cameraPosition, 1.0, frame);
}

fragment float4 fragment_water(MeshVertexOut in [[stage_in]],
                               float4 behind [[color(0)]],
                               constant PassUniforms &pass [[buffer(MMBufferPass)]],
                               constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                               constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                               texture2d<float> seaTexture [[texture(MMTextureNormal)]],
                               depth2d_array<float> shadowMap [[texture(MMTextureShadowMap)]],
                               texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                               texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                               depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                               sampler materialSampler [[sampler(MMSamplerMaterial)]],
                               sampler shadowSampler [[sampler(MMSamplerShadow)]],
                               sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    WaterSurface surface = waterSurfaceAt(in, material, pass, seaTexture, materialSampler);
    float sunLight = sunVisibility(in.worldPosition, float3(0.0, 1.0, 0.0), in.viewDepth, frame, shadowMap, shadowSampler);
    float depthIn = waterDepthAt(in, pass, frame, sceneDepth);
    float3 color = shadeWater(in, behind.rgb, depthIn, surface, sunLight, float4(0.0), material, pass, frame, irradianceMap, prefilteredMap, linearSampler);
    return float4(color, 1.0);
}

#if MOLTEN_RAYTRACING
// The same with rays: the sun's shadow on the water traced, and what stands over the water
// (a hull, a pier, the shore) mirrored in it for real instead of the sky alone.
fragment float4 fragment_water_traced(MeshVertexOut in [[stage_in]],
                                      float4 behind [[color(0)]],
                                      constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                      constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                      constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                      texture2d<float> seaTexture [[texture(MMTextureNormal)]],
                                      raytracing::instance_acceleration_structure tracedScene [[buffer(MMBufferAccelerationStructure)]],
                                      constant TracedSurface *tracedSurfaces [[buffer(MMBufferTracedSurfaces)]],
                                      texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                      texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                                      depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                                      sampler materialSampler [[sampler(MMSamplerMaterial)]],
                                      sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    WaterSurface surface = waterSurfaceAt(in, material, pass, seaTexture, materialSampler);
    float3 up = float3(0.0, 1.0, 0.0);
    float sunLight = sunVisibilityTraced(in.worldPosition, up, -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
    float depthIn = waterDepthAt(in, pass, frame, sceneDepth);
    // The mirror ray leaves the swell, not each ripple: ripples would scatter a few rays into
    // grain. They still bend the picture a little, as they do. And it leaves upward: a ray
    // bent down by a wave would find the sea's own bottom, and mirror it.
    float2 slope = surface.slope * material.waveHeight * 0.35;
    float3 toEye = normalize(pass.cameraPosition - in.worldPosition);
    float3 mirrored = reflect(-toEye, normalize(float3(-slope.x, 1.0, -slope.y)));
    mirrored.y = max(mirrored.y, 0.02);
    float3 N = normalize(toEye + normalize(mirrored));          // the facing that mirrors the eye's line that way
    float4 mirror = reflectionTraced(in.worldPosition, N, toEye, 0.0, in.viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    // What is far is lost in the haze of the distance: the sky takes over.
    mirror.a *= 1.0 - smoothstep(250.0, 500.0, in.viewDepth);
    float3 color = shadeWater(in, behind.rgb, depthIn, surface, sunLight, mirror, material, pass, frame, irradianceMap, prefilteredMap, linearSampler);
    return float4(color, 1.0);
}
#endif

#endif
