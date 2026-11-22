// Water.metal
// The scene's sea (Environment > Water), seen from above and from under it.
//
// A sheet of color blended over what is under it never looks like water: water has no
// color painted on it. What reaches the eye from it is three things. The sky (or the hull,
// or the pier) mirrored in its surface, more of it the flatter one looks along it. What is
// under it, seen through it: the sand, the rocks, with the red taken out of their light
// first and then the green, the further the light has to come, which is all the color water
// has. And the sun: its glitter on the waves, foam where waves break and where the water meets
// the shore. (The bottom is lit through the water, with the sun's lines of light the waves
// gather on it, in the scene's own pass: Forward.metal.)
//
// What water does to what is under it is to take some of each color of its light away and add
// its own: the picture there becomes `light + under * through`, `through` a color of its own
// (red goes first, then green). The fragment function says both, and the blending does the
// rest (two colors out of one fragment function, "dual source": WaterOut). It does not read
// the picture back, so with multisampling it runs once a pixel, not once a sample: a sea that
// fills half the screen costs a quarter of what it would at 4x. How far down the bottom is
// comes from the depth of what is opaque, drawn before the scene (MMTextureSceneDepth).
//
// How it looks is the scene's: its deep and shallow colors, how clear it is, how much it
// scatters and mirrors, its ripples, its foam (FrameUniforms: seaAbsorb, seaColor, seaShallow,
// seaLook, seaFoam).
//
// Part of the extras. Without them water is the simpler sheet of Forward.metal, blended.

#include "Forward.metal"
#include "Sky.metal"

#if MOLTEN_EXTRAS

// The slope of the water's surface at a pixel, how white its breaking waves make it and the
// height of the swell there: the sea the wind raises (Mesh.metal), with the small waves that
// only a pixel can show.
struct WaterSurface {
    float2 slope;
    float foam;
    float crest;
    float bubbles;                  // air churned into it (a boat's wake): paler, lighter water
};

// White on the crests of waves that break: none below a fresh breeze (about 5 meters a second),
// more the harder it blows, and only where the crests of several waves come together high and
// steep (`crowding`), drawn out along the wind in streaks, not lying about in patches.
static float seaWhitecaps(WaterSwell swell, float3 wind, float2 place, float t) {
    float speed = length(float2(wind.x, wind.z));
    if (speed <= 5.0) return 0.0;
    float windy = saturate((speed - 5.0) / 13.0);
    // Where the waves crowd together on a crest, past what this wind needs to break them.
    float breaking = saturate((swell.crowding * smoothstep(0.2, 0.8, swell.crest) - waterBreakingFrom(speed)) * 8.0) * saturate((speed - 5.0) / 2.0);
    if (breaking <= 0.0) return 0.0;
    float2 along = waterWindDirection(wind);
    float2 q = float2(dot(place, along) * 0.22, dot(place, float2(-along.y, along.x)) * 1.1);
    float streaks = smoothstep(0.3, 0.75, waterNoise(q + float2(t * 0.12, 0.0)));
    return breaking * (0.35 + 0.65 * windy) * (0.4 + 0.6 * streaks);
}

static WaterSurface waterSurfaceAt(MeshVertexOut in, constant MaterialUniforms &material, constant PassUniforms &pass, constant FrameUniforms &frame) {
    float2 rest = waterRestingPlace(in);
    float footprint = max(length(dfdx(rest)), length(dfdy(rest)));
    // Waves shorter than two pixels are left out: they could only glitter at random, far away and in a calm.
    WaterSwell swell = waterSwell(rest, pass.time, pass.wind, material.water, material.waveHeight, footprint * 1.5);
    WaterSurface surface;
    surface.slope = swell.slope + waterRipples(rest, pass.time, pass.wind, material.water, material.waveHeight, footprint) * frame.seaLook.z;
    surface.foam = seaWhitecaps(swell, pass.wind, rest, pass.time) * frame.seaFoam.y;
    surface.crest = swell.crest;
    surface.bubbles = 0.0;
    return surface;
}

// What floats on it (Kit/Wakes.hpp), at this place of the water: foam where the water meets a
// hull, thrown up at its bow as it moves; behind it the churned white trail it leaves, wider and
// fainter the older it is; and the two arms of its wake, each a few crests that open away from
// its way at the angle every wake opens at (19.5 degrees, whatever its speed). The arms bend
// the surface, and break white at speed near the hull.
static void addWakes(thread WaterSurface &surface, float3 at, float t, constant WaterWakes &wakes) {
    float foam = 0.0, bubbles = 0.0;
    float2 slope = float2(0.0);
    uint count = min(wakes.count, uint(MMMaxWakes));
    for (uint w = 0; w < count; w++) {
        constant WaterWake &wake = wakes.wakes[w];
        float2 ahead = wake.shape.xy;
        float halfWidth = max(wake.shape.z, 0.1), halfLength = max(wake.shape.w, 0.1);
        float speed = wake.body.w;
        float2 here = at.xz;
        // Nowhere near it or its trail: most of the sea.
        float reach = halfLength + halfWidth + 4.0;
        float2 boxLow = wake.body.xz - reach, boxHigh = wake.body.xz + reach;
        for (int i = 0; i < MMWakeTrail; i++) {
            float4 point = wake.trail[i];
            if (point.w < 0.0) break;
            float spread = 6.0 + point.w * 4.0;
            boxLow = min(boxLow, point.xz - spread);
            boxHigh = max(boxHigh, point.xz + spread);
        }
        if (here.x < boxLow.x || here.y < boxLow.y || here.x > boxHigh.x || here.y > boxHigh.y) continue;

        // Round the hull: the waterline as a rounded box of its size, foam just outside it.
        float2 rel = here - wake.body.xz;
        float along = dot(rel, ahead), across = dot(rel, float2(-ahead.y, ahead.x));
        float2 q = abs(float2(across, along)) - float2(halfWidth, halfLength) * 0.8;
        float rounding = min(halfWidth, halfLength) * 0.2;
        float outside = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rounding;
        float moving = saturate(speed / 4.0);
        float bow = smoothstep(-0.2, 1.0, along / halfLength) * moving;
        float ring = (1.0 - smoothstep(0.0, 0.3 + 0.5 * moving + 0.9 * bow, outside)) * step(-0.2, outside);
        foam = max(foam, ring * (0.45 + 0.5 * moving + 0.4 * bow));

        // Behind it: the nearest place on the way its stern went, how far back along it, and how long ago.
        float nearest = 1e9, back = 0.0, age = 0.0, then = 0.0, lateral = 0.0;
        float2 side = float2(0.0);
        float walked = 0.0;
        for (int i = 0; i + 1 < MMWakeTrail; i++) {
            float4 a = wake.trail[i], b = wake.trail[i + 1];
            if (b.w < 0.0) break;
            float2 u = b.xz - a.xz;
            float length2 = dot(u, u);
            float s = length2 > 1e-6 ? saturate(dot(here - a.xz, u) / length2) : 0.0;
            float2 closest = a.xz + u * s;
            float2 off = here - closest;
            float d = length(off);
            float segment = sqrt(length2);
            if (d < nearest) {
                nearest = d;
                back = walked + segment * s;
                age = mix(a.w, b.w, s);
                then = mix(wake.trailSpeed[i / 4][i % 4], wake.trailSpeed[(i + 1) / 4][(i + 1) % 4], s);
                side = d > 1e-4 ? off / d : float2(-ahead.y, ahead.x);
                lateral = (u.x * off.y - u.y * off.x) >= 0.0 ? d : -d;
            }
            walked += segment;
        }
        if (nearest > 1e8) continue;
        // Only behind the stern: in front of it the hull's own ring is the wake.
        float behindStern = smoothstep(-0.5, 0.5, -(along + halfLength));
        float fading = saturate(1.0 - age / 7.0);
        float going = saturate(then / 3.0);
        // The churned trail: as wide as the hull just behind it, spreading as it ages. White and
        // whole right behind the stern; further back it breaks into streaks drawn out along the
        // way, and the water between them is pale with the air churned into it.
        float width = halfWidth * (0.8 + 0.3 * age) + 0.04 * back;
        float churn = exp(-(nearest * nearest) / (width * width)) * fading * going * mix(1.0, behindStern, step(back, 0.5));
        float fresh = exp(-age / 0.9);
        float streaks = cloudValueNoise(float2(back * 0.22 - t * 0.05, lateral * 1.6)) * 0.65 + cloudValueNoise(float2(back * 0.6, lateral * 4.0) + 7.0) * 0.35;
        float lace = cloudValueNoise(here * 2.1 + float2(t * 0.11, -t * 0.07));
        float broken = smoothstep(0.42 + 0.25 * (1.0 - fading), 0.8, streaks * 0.75 + lace * 0.35);
        foam = max(foam, saturate(churn * (fresh * 0.95 + (1.0 - fresh) * broken * 1.2)));
        bubbles = max(bubbles, churn * (0.5 + 0.5 * fading));
        // The two arms: crests along the lines 19.5 degrees off its way, from the hull back.
        float arm = nearest - (halfWidth + back * 0.354);
        float armWidth = 0.6 + 0.05 * back;
        float onArm = exp(-(arm * arm) / (armWidth * armWidth)) * fading * going * smoothstep(0.0, 2.0, back);
        float crests = sin(arm * 3.5 - back * 0.6 + t * 1.5);
        slope += side * (crests * onArm * 0.22 * (0.5 + 0.5 * saturate(then / 8.0)));
        foam = max(foam, onArm * saturate((then - 2.5) / 8.0) * exp(-back / 40.0) * smoothstep(0.42, 0.8, streaks) * 1.2);
        bubbles = max(bubbles, onArm * 0.3 * saturate(then / 6.0));
    }
    surface.slope += slope;
    surface.foam = max(surface.foam, foam);
    surface.bubbles = max(surface.bubbles, bubbles);
}

// What a pixel of water makes of what is under it: `light + under * through`.
struct WaterLight {
    float3 light;
    float3 through;
};

// The two colors out of the water's fragment functions: the blending adds the first to what is
// drawn there times the second.
struct WaterOut {
    float4 light [[color(0), index(0)]];
    float4 through [[color(0), index(1)]];
};

static WaterOut waterOut(WaterLight water) {
    WaterOut out;
    out.light = float4(water.light, 1.0);
    out.through = float4(water.through, 0.0);
    return out;
}

// A pixel of water. `sunLight` how much of the sun reaches the surface; `mirror` what a traced
// ray found in the mirrored direction (its light, and 1 in `a`), nothing where no ray was sent
// or it reached the sky.
static WaterLight shadeWater(MeshVertexOut in, float depthIn, WaterSurface surface, float sunLight, float4 mirror,
                             constant MaterialUniforms &material, constant PassUniforms &pass, constant FrameUniforms &frame,
                             texturecube<float> irradianceMap, texturecube<float> prefilteredMap, sampler linearSampler) {
    float t = pass.time;
    float3 deep = frame.seaColor.rgb;
    float3 shallowTint = frame.seaShallow.rgb / max(max(frame.seaShallow.r, max(frame.seaShallow.g, frame.seaShallow.b)), 0.001);

    // ---- the surface: which way it faces here
    float3 toCamera = pass.cameraPosition - in.worldPosition;
    float range = max(length(toCamera), 0.001);
    float3 toEye = toCamera / range;
    float3 toSun = normalize(-frame.sunDirection);
    // Far away the small waves blur into a smoother sheet, as they do to the eye.
    float2 slope = surface.slope * mix(1.0, 0.35, saturate(in.viewDepth / 500.0));
    float3 swell = normalize(in.normal);
    float3 N = normalize(float3(swell.x - slope.x, max(swell.y, 0.05), swell.z - slope.y));
    float facing = saturate(dot(N, toEye));

    // ---- how much water the eye looks through to the bottom
    bool hasBottom = depthIn < 900.0;
    float along = hasBottom ? depthIn * range / max(in.viewDepth, 0.001) : 1000.0;      // along the line of sight, not the view's axis
    // Where the water meets what stands in it there is hardly any water: it thins to nothing,
    // and so do its mirror and its glitter, instead of ending in a line.
    float edge = hasBottom ? smoothstep(0.0, 0.07, depthIn) : 1.0;

    // What crossing a meter of it does to light: red goes first, then green, and how soon is
    // its clarity. In the first meter or two the bottom takes its shallow color.
    float3 absorbed = frame.seaAbsorb.xyz;
    float3 through = exp(-absorbed * along) * mix(float3(1.0), shallowTint, saturate(along / 1.5) * 0.85);
    float hidden = 1.0 - exp(-min(absorbed.x, min(absorbed.y, absorbed.z)) * along);

    // ---- the light around: the sky's, and the sun's where it is not shaded
    float3 around = irradianceMap.sample(linearSampler, float3(0.0, 1.0, 0.0)).rgb * frame.environmentIntensity;
    float3 sun = frame.sunColor * sunLight;

    // ---- the water's own light: what it scatters back toward the eye, in its color. More of it
    // comes out of a crest, where there is water for the light to cross, than out of a trough:
    // it is how waves show from above, where the surface mirrors little.
    float scattering = frame.seaLook.x;
    float3 body = deep * (around * 0.95 + sun * (0.05 + 0.11 * saturate(dot(N, toSun)))) * scattering;
    body *= mix(0.78, 1.4, saturate(surface.crest * 0.5 + 0.5));
    // A wave between the eye and a low sun glows through its crest.
    float glow = pow(saturate(dot(toEye, -toSun) * 0.5 + 0.5), 4.0) * saturate(surface.crest * 0.6 + 0.5) * saturate(1.0 - toEye.y * 1.5);
    body += deep * float3(0.7, 1.9, 1.6) * sun * glow * 0.6 * scattering;
    // Air churned into it scatters light back up: paler, greener-blue, and the bottom hidden.
    float churned = saturate(surface.bubbles);
    body = mix(body, (deep * 1.6 + float3(0.08, 0.16, 0.16)) * (around * 1.1 + sun * 0.25), churned * 0.75);
    hidden = mix(hidden, 1.0, churned * 0.6);
    // What is seen into it: the bottom times `through`, and its own light.
    float3 seen = body * hidden;

    // ---- what its surface mirrors: the sky, or what a traced ray found there
    float fresnel = (0.02 + 0.98 * pow(1.0 - facing, 5.0)) * frame.seaLook.y;
    float3 mirrored = reflect(-toEye, N);
    mirrored.y = max(mirrored.y, 0.015);                        // a ray that would go into the water is bent back above the horizon
    mirrored = normalize(mirrored);
    // Sharp nearby; far away each pixel holds many waves, and mirrors a wider patch of sky.
    float blur = 0.4 + 1.6 * saturate(in.viewDepth / 300.0);
    float3 sky = prefilteredMap.sample(linearSampler, mirrored, level(blur)).rgb * frame.environmentIntensity;
    float3 reflection = mix(sky, mirror.rgb, saturate(mirror.a));
    float mirrors = saturate(fresnel * edge);
    float3 color = mix(seen, reflection, mirrors);
    through *= 1.0 - mirrors;

    // ---- the sun's glitter: a sharp highlight on a nearly smooth surface, a wider sheen round it
    float3 halfway = normalize(toSun + toEye);
    float nh = saturate(dot(N, halfway));
    const float a2 = 0.06 * 0.06 * 0.06 * 0.06;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    float glitter = min(a2 / (kPi * d * d), 400.0) * (0.02 + 0.98 * pow(1.0 - saturate(dot(halfway, toEye)), 5.0)) * 0.25;
    float sheen = pow(saturate(dot(mirrored, toSun)), 40.0) * 0.15;
    color += sun * (glitter + sheen) * saturate(dot(N, toSun) * 4.0) * edge * (1.0 - saturate(mirror.a)) * frame.seaLook.y;

    // ---- foam: on breaking crests (in `surface`), and where the water laps at what stands in it
    float foam = saturate(surface.foam);
    if (hasBottom && frame.seaFoam.x > 0.0) {
        // A band the waves push up the shore and pull back, thinner where the sea is calm,
        // with a brighter line at its edge and lace behind it.
        float calm = saturate(length(float2(pass.wind.x, pass.wind.z)) / 10.0);
        float reach = (0.10 + 0.16 * calm) * frame.seaFoam.x;
        float lap = 0.5 + 0.5 * sin(t * 1.3 + dot(in.worldPosition.xz, float2(0.35, 0.27)) * 2.0);
        float band = 1.0 - smoothstep(0.0, reach * (0.55 + 0.75 * lap), depthIn);
        float lace = cloudValueNoise(in.worldPosition.xz * 5.5 + float2(t * 0.25, -t * 0.18)) * cloudValueNoise(in.worldPosition.xz * 13.0 - float2(t * 0.4, t * 0.3));
        float line = 1.0 - smoothstep(0.0, 0.035, depthIn);
        foam = max(foam, saturate(band * (0.35 + 2.4 * lace) + line * 0.8) * edge * saturate(frame.seaFoam.x));
    }
    if (foam > 0.0) {
        // Lacy, as foam is: holes open in it as it thins.
        float holes = cloudValueNoise(in.worldPosition.xz * 9.0 + float2(-t * 0.15, t * 0.1));
        foam = saturate((foam - holes * 0.35) * 1.6);
        float3 white = float3(0.92, 0.95, 0.97) * (around * 1.1 + sun * (0.35 + 0.35 * saturate(dot(N, toSun))));
        color = mix(color, white, foam);
        through *= 1.0 - foam;
    }

    // Whatever went wrong on the way (a normal of no length, a number out of range), it is still water.
    if (any(isnan(color)) || any(isinf(color)) || any(isnan(through))) {
        color = deep * (around + frame.sunColor * 0.3);
        through = float3(0.0);
    }
    WaterLight out;
    out.light = applyFogAlong(color, in.worldPosition, pass.cameraPosition, 1.0, frame);
    out.through = through * (1.0 - fogAmountAlong(in.worldPosition, pass.cameraPosition, frame));
    return out;
}

// The water's surface seen from under it. Looking up, the world above shows through a round
// window overhead (Snell's window: the whole sky squeezed into a cone 97 degrees across, the light
// bent as it comes in); outside it the surface is a mirror of the water itself, which sends back
// all the light that comes at it so slantwise. Then the water between it and the eye.
static WaterLight shadeWaterFromBelow(MeshVertexOut in, WaterSurface surface, constant PassUniforms &pass, constant FrameUniforms &frame) {
    float3 toEye = normalize(pass.cameraPosition - in.worldPosition);
    float2 slope = surface.slope * mix(1.0, 0.35, saturate(in.viewDepth / 200.0));
    float3 N = normalize(float3(-slope.x, 1.0, -slope.y));
    float3 up = -toEye;                                         // the line of sight, going up through it
    float3 leaving = refract(up, -N, 1.333);
    float through = 0.0;
    if (dot(leaving, leaving) > 1e-4) {
        // Fresnel from inside: less gets out the more slantwise it comes, none past the critical angle.
        float cosIn = saturate(dot(up, N)), cosOut = saturate(dot(normalize(leaving), N));
        float rs = (1.333 * cosIn - cosOut) / max(1.333 * cosIn + cosOut, 1e-4);
        float rp = (cosIn - 1.333 * cosOut) / max(cosIn + 1.333 * cosOut, 1e-4);
        through = saturate(1.0 - 0.5 * (rs * rs + rp * rp));
    }
    // What is above comes in times `through`; the rest is the water mirrored.
    float3 mirrored = seaHaze(frame, max(frame.seaColor.w - in.worldPosition.y, 0.0) + 2.0);
    float3 color = mirrored * (1.0 - through);
    float3 kept = float3(through);
    // Foam seen from under: the light through it, soft.
    float foam = saturate(surface.foam) * 0.5;
    color = mix(color, seaHaze(frame, 0.0) * 2.5, foam);
    kept *= 1.0 - foam;
    float range = length(pass.cameraPosition - in.worldPosition);
    float3 water = exp(-frame.seaAbsorb.xyz * range);
    WaterLight out;
    out.light = color * water + seaHaze(frame, max(frame.cameraUnderwater, 0.0)) * (1.0 - water);
    out.through = kept * water;
    return out;
}

fragment WaterOut fragment_water(MeshVertexOut in [[stage_in]],
                               bool isFrontFacing [[front_facing]],
                               constant PassUniforms &pass [[buffer(MMBufferPass)]],
                               constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                               constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                               constant WaterWakes &wakes [[buffer(MMBufferWakes)]],
                               depth2d_array<float> shadowMap [[texture(MMTextureShadowMap)]],
                               texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                               texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                               depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                               sampler shadowSampler [[sampler(MMSamplerShadow)]],
                               sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    WaterSurface surface = waterSurfaceAt(in, material, pass, frame);
    addWakes(surface, in.worldPosition, pass.time, wakes);
    if (!isFrontFacing) return waterOut(shadeWaterFromBelow(in, surface, pass, frame));
    float sunLight = sunVisibility(in.worldPosition, float3(0.0, 1.0, 0.0), in.viewDepth, frame, shadowMap, shadowSampler);
    float depthIn = waterDepthAt(in, pass, frame, sceneDepth);
    return waterOut(shadeWater(in, depthIn, surface, sunLight, float4(0.0), material, pass, frame, irradianceMap, prefilteredMap, linearSampler));
}

#if MOLTEN_RAYTRACING
// The same with rays: the sun's shadow on the water traced, and what stands over the water
// (a hull, a pier, the shore) mirrored in it for real instead of the sky alone.
fragment WaterOut fragment_water_traced(MeshVertexOut in [[stage_in]],
                                      bool isFrontFacing [[front_facing]],
                                      constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                      constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                      constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                      constant WaterWakes &wakes [[buffer(MMBufferWakes)]],
                                      raytracing::instance_acceleration_structure tracedScene [[buffer(MMBufferAccelerationStructure)]],
                                      constant TracedSurface *tracedSurfaces [[buffer(MMBufferTracedSurfaces)]],
                                      texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                      texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                                      depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    WaterSurface surface = waterSurfaceAt(in, material, pass, frame);
    addWakes(surface, in.worldPosition, pass.time, wakes);
    if (!isFrontFacing) return waterOut(shadeWaterFromBelow(in, surface, pass, frame));
    float3 up = float3(0.0, 1.0, 0.0);
    float sunLight = sunVisibilityTraced(in.worldPosition, up, -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
    float depthIn = waterDepthAt(in, pass, frame, sceneDepth);
    // The mirror ray leaves the swell, not each ripple: ripples would scatter a few rays into
    // grain. They still bend the picture a little, as they do. And it leaves upward: a ray
    // bent down by a wave would find the sea's own bottom, and mirror it.
    float2 slope = surface.slope * 0.35;
    float3 toEye = normalize(pass.cameraPosition - in.worldPosition);
    float3 mirrored = reflect(-toEye, normalize(float3(-slope.x, 1.0, -slope.y)));
    mirrored.y = max(mirrored.y, 0.02);
    float3 N = normalize(toEye + normalize(mirrored));          // the facing that mirrors the eye's line that way
    float4 mirror = reflectionTraced(in.worldPosition, N, toEye, 0.0, in.viewDepth, in.position.xy, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    // What is far is lost in the haze of the distance: the sky takes over.
    mirror.a *= 1.0 - smoothstep(250.0, 500.0, in.viewDepth);
    return waterOut(shadeWater(in, depthIn, surface, sunLight, mirror, material, pass, frame, irradianceMap, prefilteredMap, linearSampler));
}
#endif

#endif
