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
    float waterFoam;        // how much white shows where its waves break and where it meets what stands in it
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
    result.waterFoam = material.foam;
    result.waterDepth = 1000.0;
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

// ---------------------------------------------------------------- under the sea

// Which way, and how far across for each meter down, the sun's light goes on under the sea's
// surface where it came through it at `place`: bent by the slope of the waves there.
static float2 seaRefractedDrift(float2 place, float3 sunWay, constant PassUniforms &pass, float finest, float ripples) {
    // The swell's longer trains only: the shortest few, nearly square to each other, would bend
    // the light into a grid of cells all alike. What is smaller is the bumps below, which are not.
    float longest = waterSeaState(pass.wind, 1.0, 1.0).x;
    WaterSwell swell = waterSwell(place, pass.time, pass.wind, 1.0, 1.0, max(finest, longest * 0.11));
    float2 along = waterWindDirection(pass.wind);
    // The wind never ruffles a sea evenly: it comes down on it in patches (cat's paws) that run
    // downwind, ruffled hard in some, nearly glassy between them. Where it is ruffled the lines
    // of light are many and sharp; where it is glassy, few, wide and soft, only the swell's.
    float2 gustAt = (place - along * pass.time * 1.6) * 0.07;
    float paws = smoothstep(0.18, 0.82, waterNoise(gustAt + float2(5.3, 1.7)) * 0.7 + waterNoise(gustAt * 2.3 + float2(-2.1, 7.4)) * 0.3);
    float ruffled = mix(0.2, 1.45, paws);
    float2 slope = swell.slope;
    // And the small waves no train of them is: a sea is never as regular as its trains, and its
    // lines of light would be a grid without this. Three sizes of bumps, carried with the wind, each
    // a little larger here and smaller there, so that no two cells of light are alike.
    float strength = clamp(length(float2(pass.wind.x, pass.wind.z)) / 7.0, 0.35, 1.6);
    float grain = mix(0.7, 1.4, waterNoise(place * 0.045 + float2(11.2, -3.9)));
    for (int j = 0; j < 3; j++) {
        float size = (j == 0 ? 1.4 : j == 1 ? 0.55 : 0.21) * grain;
        if (size < finest * 1.5) break;
        float2 p = (place - along * (pass.time * (j == 0 ? 0.3 : j == 1 ? 0.45 : 0.8))) / size + float2(j * 17.3, j * 41.9);
        float2 warp = float2(waterNoise(p * 0.37 + 3.1), waterNoise(p * 0.37 + 9.7)) * 1.8;
        float here = waterNoise(p + warp);
        float2 change = float2(waterNoise(p + warp + float2(0.3, 0.0)) - here, waterNoise(p + warp + float2(0.0, 0.3)) - here);
        float weight = j == 0 ? 0.34 : j == 1 ? 0.5 : 0.36;
        slope += change * (weight * strength * ripples * (j == 0 ? 1.0 : ruffled));
    }
    float3 n = normalize(float3(-slope.x, 1.0, -slope.y));
    float3 bent = refract(sunWay, n, 1.0 / 1.333);
    return bent.xz / max(-bent.y, 0.05);
}

// The sun's light on the bottom of the sea and on what is in it, `below` meters under the
// surface: the waves gather it into bright lines that crawl as they move, as lenses would, and
// leave it dim between them. The light that came through a small patch of the surface spreads
// over a patch of the bottom as large as the waves' slopes, there, make it: as bright as it is
// gathered (one over how much that patch is stretched). 1 on average, up to several on a line.
// `footprint` is how much the pixel covers: what is finer is left out, and so is what the
// sun's own width blurs away, the more the deeper.
static float seaCaustics(float3 at, float below, float3 sunWay, constant PassUniforms &pass, float footprint, float ripples) {
    float3 still = refract(sunWay, float3(0.0, 1.0, 0.0), 1.0 / 1.333);
    float2 across = still.xz / max(-still.y, 0.05);
    float2 entered = at.xz - across * below;                // where its light came through, were the sea still
    float finest = max(footprint * 2.0, below * 0.012 + 0.03);
    float e = finest * 0.35;
    float2 here = seaRefractedDrift(entered, sunWay, pass, finest, ripples);
    float2 dx = (seaRefractedDrift(entered + float2(e, 0.0), sunWay, pass, finest, ripples) - here) * (below / e);
    float2 dz = (seaRefractedDrift(entered + float2(0.0, e), sunWay, pass, finest, ripples) - here) * (below / e);
    float stretched = abs((1.0 + dx.x) * (1.0 + dz.y) - dx.y * dz.x);
    return min(1.0 / max(stretched, 0.12), 6.0);
}

// ---------------------------------------------------------------- rain on surfaces

// How much of the rain reaches this place: 1 out in the open, 0 under a roof. The rain map is the
// scene round the camera seen from above, nearest first: a place is out in the rain if nothing
// in it is above. Looked up a little off the surface (more off a wall, which from above is a
// line), in four looks for a soft edge to a roof's shelter.
static float rainReaches(float3 worldPosition, float3 normal, constant FrameUniforms &frame, depth2d<float> rainMap, sampler shadowSampler) {
    if (is_null_texture(rainMap)) return 1.0;
    float off = mix(0.05, 0.28, 1.0 - abs(normal.y));
    float4 clip = frame.rainViewProjection * float4(worldPosition + normal * off, 1.0);
    float2 uv = clip.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 1.0;          // beyond the square: out in it, as far as is known
    float depth = clip.z - 0.0004;                                                   // 16 cm: what lies just on it is no roof
    float texel = 1.0 / float(rainMap.get_width());
    float open = rainMap.sample_compare(shadowSampler, uv + float2(-0.5, -0.5) * texel, depth) +
                 rainMap.sample_compare(shadowSampler, uv + float2(0.5, -0.5) * texel, depth) +
                 rainMap.sample_compare(shadowSampler, uv + float2(-0.5, 0.5) * texel, depth) +
                 rainMap.sample_compare(shadowSampler, uv + float2(0.5, 0.5) * texel, depth);
    return open * 0.25;
}

static float rainHash(float2 p) {
    return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// ---------------------------------------------------------------- the rain on things

// The water that sits on a thing and runs down it sticks to it: it is worked out in the thing's
// own axes (MeshVertexOut::localPosition), so that a car drives off with its drops, and the air it
// drives through pushes them back over it.
struct RainOn {
    float3 bend;                            // how the water tilts the surface, in the thing's axes
    float water;                            // how much of the pixel is water: a bead, a drop, its trail
};

// Beads sitting on a plane of the thing's axes (`p` in meters along `axisU` and `axisV`): one in
// some of the squares of a grid `cell` meters across, more of them the wetter. Each is a little
// dome, drawn out along `along` (where it is pulled: down a slope, back in the wind) by `stretch`.
// `fade`: how much of it a pixel can show (too small, and it would only flicker).
static void beadsOn(thread RainOn &out, float2 p, float2 along, float cell, float wet, float stretch, float seed,
                    float3 axisU, float3 axisV, float fade) {
    if (fade <= 0.0 || wet <= 0.0) return;
    float2 q = p / cell + seed * float2(7.13, 3.71);
    float2 square = floor(q);
    if (rainHash(square + seed * 11.0) > wet) return;
    float2 middle = float2(rainHash(square + 1.7 + seed), rainHash(square + 8.3 + seed)) * 0.5 + 0.25;
    float radius = mix(0.12, 0.24, rainHash(square + 4.1 + seed)) / sqrt(stretch);
    float2 off = fract(q) - middle;
    // Leaning the way it is pulled, its tail the longer.
    float a = dot(off, along), b = dot(off, float2(-along.y, along.x));
    float r = length(float2(a / (a < 0.0 ? stretch * 1.4 : stretch), b)) / radius;
    if (r >= 1.0) return;
    float bulge = sqrt(1.0 - r * r);
    float2 tilt = off / max(length(off), 1e-5) * (1.0 - bulge) * 1.8 * fade;      // the rim tilts outward
    out.bend += axisU * tilt.x + axisV * tilt.y;
    out.water = max(out.water, smoothstep(1.0, 0.75, r) * fade);
}

// Drops that run, once what pulls them beats how they cling: each down a lane of its own (`side`
// across them, `goes` the way they go, in the thing's axes), at `speed` meters a second and in
// fits, as a drop sticks and slips; wandering a little from side to side, a wet trail behind it.
// `swept` says where the beads were gathered up by one going by.
static void dropsRunning(thread RainOn &out, thread float &swept, float3 lp, float3 goes, float3 side, float speed,
                         float wet, float time, float pixel) {
    const float lane = 0.035;
    float u = dot(lp, side) / lane, v = dot(lp, goes);
    for (int k = 0; k < 2; k++) {                           // two sets of lanes, half a lane apart
        float uu = u + float(k) * 0.5;
        float which = floor(uu) + float(k) * 101.0;
        float seed = rainHash(float2(which, 7.31));
        if (seed > wet * 0.7) continue;
        float headR = mix(0.0022, 0.0038, rainHash(float2(which, 2.9)));
        float fade = smoothstep(1.0, 2.5, headR / max(pixel, 1e-6));
        if (fade <= 0.0) continue;
        float pace = speed * mix(0.55, 1.45, rainHash(float2(which, 1.7)));
        float travelled = pace * (time + 0.22 * sin(time * 2.7 + seed * 40.0));      // in fits, never back
        float gap = mix(0.25, 0.7, rainHash(float2(which, 5.9)));                     // meters between drops down a lane
        float f = fract((v - travelled) / gap + seed * 9.0);
        float behind = f < 0.5 ? f * gap : -(1.0 - f) * gap;     // along the lane from the nearest drop: + ahead of it, - behind
        float wander = 0.18 * sin(v * 9.0 + seed * 6.28) + 0.08 * sin(v * 31.0 + seed * 3.1);
        float across = (fract(uu) - 0.5 - wander) * lane;
        // Its trail: thin, behind it, drying as it goes.
        float trailLength = 0.08 + 0.3 * saturate(pace);
        if (behind < 0.0 && -behind < trailLength) {
            float width = 0.0009 + 0.0012 * (1.0 + behind / trailLength);
            float trail = (1.0 - smoothstep(width * 0.5, width, abs(across))) * (1.0 + behind / trailLength) * fade;
            out.water = max(out.water, trail * 0.7);
            out.bend += side * (sign(across) * 0.25 * trail);
            swept = max(swept, (1.0 - smoothstep(headR, headR * 2.5, abs(across))) * (1.0 + behind / trailLength));
        }
        // The drop: a little longer than wide, its back drawn out.
        float2 h = float2(across / headR, behind / (behind > 0.0 ? headR : headR * 1.7));
        float r = length(h);
        if (r < 1.0) {
            float bulge = sqrt(1.0 - r * r);
            float2 tilt = h / max(r, 1e-4) * (1.0 - bulge) * 1.6 * fade;
            out.bend += side * tilt.x + goes * tilt.y;
            out.water = max(out.water, smoothstep(1.0, 0.7, r) * fade);
            swept = 1.0;
        }
    }
}

// What the rain does to a surface out in it. What soaks water up (stone, wood, asphalt, cloth: rough
// and not metal) darkens, and where it lies flat the drops' rings spread on the water standing on
// it. On what is smooth or metal (paint, glass, plastic, steel) the water stands in beads; when
// what pulls them (their weight down a slope, the air going by: the wind, and the thing's own
// speed through it) beats how they cling, they run, down the surface and back over it in the
// air, gathering the beads in their way.
static void wetten(thread MaterialSample &m, MeshVertexOut in, constant FrameUniforms &frame, constant PassUniforms &pass, float reached) {
    float3 n = normalize(in.normal);
    float wet = frame.wetness * reached * smoothstep(-0.45, 0.05, n.y);
    if (wet <= 0.002) return;
    float up = saturate(n.y);
    float porous = (1.0 - m.metallic) * smoothstep(0.35, 0.6, m.roughness);
    float beady = 1.0 - porous;
    m.baseColor.rgb *= 1.0 - 0.45 * wet * porous;
    float film = wet * mix(0.6, 1.0, up * up);
    m.roughness = mix(m.roughness, min(m.roughness, mix(0.3, 0.07, up)), film);
    m.normal = normalize(mix(m.normal, n, film * up * 0.7));
    float pixel = max(length(dfdx(in.worldPosition)), length(dfdy(in.worldPosition)));      // meters a pixel covers here

    // ---- beads and running drops, near enough to be seen (further, the film above is all there is)
    if (beady > 0.05 && pixel < 0.003) {
        float3 X = normalize(in.axisX), Y = normalize(in.axisY), Z = cross(X, Y);
        float3x3 toWorld = float3x3(X, Y, Z), toLocal = transpose(toWorld);
        float3 ln = toLocal * n;
        float3 lp = in.localPosition;
        // What pulls a drop along the surface: its weight, and the air going by it, harder the faster.
        float3 velocity = pass.deltaTime > 0.0 ? (in.worldPosition - in.previousWorldPosition) / pass.deltaTime : float3(0.0);
        float3 air = toLocal * (float3(pass.wind.x, 0.0, pass.wind.z) - velocity);
        float3 down = toLocal * float3(0.0, -9.8, 0.0);
        float3 weight = down - ln * dot(down, ln);
        float3 airAlong = air - ln * dot(air, ln);
        float facing = 0.35 + 0.65 * saturate(-dot(ln, air) / max(length(air), 1e-3));      // the side the air meets, more
        float3 pull = weight + airAlong * (length(airAlong) * 0.12 * facing);
        float strength = length(pull);
        float3 goes = strength > 1e-3 ? pull / strength : normalize(cross(ln, abs(ln.x) < 0.9 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0)));
        float3 side = normalize(cross(ln, goes));
        float runs = smoothstep(2.0, 5.0, strength);               // they cling until pulled at about a quarter of their weight
        float speed = clamp((strength - 2.0) * 0.06, 0.0, 2.5);

        RainOn rain;
        rain.bend = float3(0.0);
        rain.water = 0.0;
        // Beads on the three planes of its axes, as much of each as it faces.
        float3 w = pow(abs(ln), float3(4.0));
        w /= max(w.x + w.y + w.z, 1e-4);
        float stretch = 1.0 + 0.4 * saturate(length(weight) / 9.8) + 2.5 * runs * saturate(speed);
        float sitting = wet * beady * (1.0 - 0.6 * runs);
        float fadeSmall = smoothstep(1.2, 2.5, 0.0025 / pixel), fadeLarge = smoothstep(1.2, 2.5, 0.005 / pixel);
        for (int i = 0; i < 3; i++) {
            if (w[i] < 0.02) continue;
            float3 U = i == 0 ? float3(0.0, 1.0, 0.0) : i == 1 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
            float3 V = i == 0 ? float3(0.0, 0.0, 1.0) : i == 1 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
            float2 p = float2(dot(lp, U), dot(lp, V));
            float2 along = float2(dot(goes, U), dot(goes, V));
            along = dot(along, along) > 1e-6 ? normalize(along) : float2(0.0, -1.0);
            RainOn plane;
            plane.bend = float3(0.0);
            plane.water = 0.0;
            beadsOn(plane, p, along, 0.011, sitting * 0.8, stretch, float(i), U, V, fadeSmall);
            beadsOn(plane, p, along, 0.024, sitting * 0.4, stretch, float(i) + 3.0, U, V, fadeLarge);
            rain.bend += plane.bend * w[i];
            rain.water += plane.water * w[i];
        }
        if (runs > 0.01) {
            float swept = 0.0;
            RainOn running;
            running.bend = float3(0.0);
            running.water = 0.0;
            dropsRunning(running, swept, lp, goes, side, speed, wet * beady * runs, pass.time, pixel);
            rain.bend = rain.bend * (1.0 - swept) + running.bend;
            rain.water = max(rain.water * (1.0 - swept), running.water);
        }
        m.normal = normalize(m.normal + (toWorld * rain.bend) * 0.6);
        m.roughness = mix(m.roughness, 0.02, rain.water);
        m.baseColor.rgb *= 1.0 - 0.12 * rain.water;              // seen through a drop, what is under it is a little darker
    }
    // ---- on what lies flat and soaks water up, where it stands: the small rings of the drops landing in it
    if (up > 0.97 && porous > 0.2 && in.viewDepth < 25.0) {
        float2 p = in.worldPosition.xz * 4.0;                  // squares of 25 cm, a drop now and then in each
        float2 tilt = float2(0.0);
        for (int layer = 0; layer < 2; layer++) {
            float2 q = p + float(layer) * float2(0.37, 0.61);
            float2 cell = floor(q) + float(layer) * 17.0;
            float2 center = float2(rainHash(cell + 3.1), rainHash(cell + 9.7)) * 0.5 + 0.25;
            float phase = fract(pass.time * 1.3 + rainHash(cell + 5.3));
            float2 away = fract(q) - center;
            float r = length(away);
            float radius = phase * 0.3;                          // up to 7 or 8 cm
            float band = saturate(1.0 - abs(r - radius) * 16.0);
            float ring = sin(clamp((r - radius) * 50.0, -3.14159, 3.14159));
            tilt += away / max(r, 1e-3) * (ring * band * (1.0 - phase) * (1.0 - phase));
        }
        float pools = wet * porous * smoothstep(0.97, 0.995, up) * (1.0 - smoothstep(10.0, 25.0, in.viewDepth));
        m.normal = normalize(m.normal + float3(tilt.x, 0.0, tilt.y) * (0.25 * pools));
    }
}

// Snow that has settled (WeatherSettings::snowSettles): on what faces up and is out in it, the more
// the flatter, none on what is steeper than it can lie on; unevenly, in patches that spread and
// join. White, rough, the grooves under it filled in.
static void snowOn(thread MaterialSample &m, MeshVertexOut in, constant FrameUniforms &frame, float reached) {
    float cover = frame.snow.x;
    float3 n = normalize(in.normal);
    float lies = smoothstep(0.4, 0.85, n.y) * reached;
    if (lies <= 0.0 || cover <= 0.0) return;
    float2 p = in.worldPosition.xz;
    float drifts = waterNoise(p * 0.45) * 0.55 + waterNoise(p * 1.9 + 7.3) * 0.3 + waterNoise(p * 7.0 - 3.1) * 0.15;
    drifts = saturate((drifts - 0.5) * 2.2 + 0.5);              // spread over 0 to 1
    // White patches that spread and join as it builds up (about as much of the ground as has
    // settled), and between them a thin dusting from the first flakes.
    float spread = cover * lies * 1.3;
    float patches = smoothstep(drifts - 0.05, drifts + 0.08, spread);
    float dust = 0.3 * saturate(cover * 3.0) * lies;
    float amount = max(patches, dust);
    m.baseColor.rgb = mix(m.baseColor.rgb, float3(0.88, 0.9, 0.94), amount);
    m.metallic = mix(m.metallic, 0.0, amount);
    m.roughness = mix(m.roughness, 0.75, amount);
    m.normal = normalize(mix(m.normal, n, amount * 0.85));
    m.emissive *= 1.0 - amount;
}

// Surface shaders written by hand (`.shader` assets) are put here by the engine, each in a
// namespace of its own, with `moltenCustomSurface` to run the one a material names.
// See Engine/Rendering/SurfaceShader.hpp. The next line is where; do not change it.
// MOLTEN_CUSTOM_SURFACES

// `sunVisibility` is how much of the sun reaches this pixel, 0 to 1: from the shadow
// maps or from tracing rays, whichever the entry point used.
static float4 shadePixel(MeshVertexOut in, MaterialSample m,
                         constant PassUniforms &pass, constant FrameUniforms &frame, constant LightData *lights,
                         texture2d_array<float> cookies, constant LightShadowViews &lightShadows, depth2d<float> lightShadowAtlas,
                         sampler shadowSampler, float sunLight, texturecube<float> irradianceMap,
                         texturecube<float> prefilteredMap, texture2d<float> brdfLUT,
                         texture2d<float> ambientOcclusion, sampler linearSampler,
                         constant ProbeVolumes &probeVolumes, device const float4 *probes) {
#if MOLTEN_CUSTOM
    // The material's own shader has its say first: what it returns is what is lit.
    if (m.custom > 0.5) {
        m = moltenCustomSurface(m, in, pass.cameraPosition, pass.time, pass.wind, m.waterDepth);
    }
#endif
    // Water is part of every variant of the shaders: it needs nothing the plainest lacks.
    if (m.water > 0.0) {
        // Water: the slope of its waves (the sea the wind raises, see Mesh.metal, and the small
        // waves only a pixel shows), then the sky it mirrors, the light that comes out of it
        // from below, the sun's glitter, and foam where waves break.
        float t = pass.time;
        float2 rest = waterRestingPlace(in);
        float footprint = max(length(dfdx(rest)), length(dfdy(rest)));
        WaterSwell raised = waterSwell(rest, t, pass.wind, m.water, m.waveHeight, footprint * 1.5);      // (none shorter than two pixels)
        float2 slope = raised.slope + waterRipples(rest, t, pass.wind, m.water, m.waveHeight, footprint);
        float crest = raised.crest;
        float seaFoam = waterBreaking(raised.crowding, pass.wind) * m.waterFoam;
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

        // Foam where waves break; patchy, as foam is.
        float foam = saturate(seaFoam);
        // Where the water meets something that stands in it (a rock, a pier, a hull, the shore):
        // a ring of foam that the waves push and pull, thicker the rougher the sea.
        float wash = 0.35 + 0.25 * saturate(m.waveHeight);
        float lap = 0.5 + 0.5 * sin(t * 1.6 + dot(in.worldPosition.xz, float2(0.9, 0.7)) * 2.0);
        float shore = (1.0 - smoothstep(0.0, wash * (0.7 + 0.6 * lap), depthIn)) * step(0.0, depthIn);
        float shoreRipples = 0.55 + 0.45 * sin(depthIn * 28.0 - t * 3.0);
        foam = max(foam, shore * shoreRipples * saturate(m.waterFoam));
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
    // Under the sea's level, the sea is between this place and the sky: the sun's light reaches
    // it dimmer and bluer the deeper (and gathered into lines by the waves), the sky's too.
    float3 sunThroughSea = float3(1.0), aroundThroughSea = float3(1.0);
    float footprint = max(length(dfdx(in.worldPosition)), length(dfdy(in.worldPosition)));
    if (hasSea(frame)) {
        float below = frame.seaColor.w - in.worldPosition.y;
        if (below > 0.0) {
            float3 sunWay = normalize(frame.sunDirection);
            float3 still = refract(sunWay, float3(0.0, 1.0, 0.0), 1.0 / 1.333);
            sunThroughSea = exp(-frame.seaAbsorb.xyz * (below / max(-still.y, 0.2)));
            aroundThroughSea = exp(-frame.seaAbsorb.xyz * below * 1.3);
            if (sunWay.y < -0.03 && below < 40.0 && in.viewDepth < 160.0 && frame.seaLook.w > 0.0) {
                float lines = seaCaustics(in.worldPosition, below, sunWay, pass, footprint, frame.seaLook.z);
                float fade = smoothstep(0.0, 0.25, below) * (1.0 - smoothstep(90.0, 160.0, in.viewDepth));
                sunThroughSea *= mix(1.0, max(1.0 + (lines - 1.0) * frame.seaLook.w, 0.0), fade);
            }
        }
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
        if (lights[i].cookie > 0.5) light.radiance *= lightCookie(lights[i], surface.position, cookies, linearSampler);
        if (lights[i].shadow > 0.5) light.radiance *= lightShadow(lights[i], surface.position, normalize(in.normal), lightShadows, lightShadowAtlas, shadowSampler);
        if (i == 0) {
            // By convention the shadow-casting sun is the first light. Without sun
            // shadows `sunLight` is 1 and this changes nothing.
            light.radiance *= sunLight * sunThroughSea;
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
    EnvironmentParts environment = environmentParts(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler, probeVolumes, probes);
    float traced = saturate(m.mirror);
    color += clamp(environment.diffuse + environment.specular * (1.0 - traced), 0.0, 60000.0) * aroundThroughSea;
    color += clamp(m.mirrored * environment.specularWeight * traced * frame.reflectionStrength, 0.0, 60000.0);
    // And the light bounced to it from its surroundings, taken up by what is not metal in it.
    color += clamp(m.bounced * m.baseColor.rgb * (1.0 - m.metallic) * m.occlusion, 0.0, 60000.0) * aroundThroughSea;
    // Seen from under the sea, through its water; otherwise through the air, and its fog.
    if (hasSea(frame) && frame.cameraUnderwater > 0.0) return float4(seenInSea(color, in.worldPosition, frame), m.baseColor.a);
    return float4(applyFogAlong(color, in.worldPosition, pass.cameraPosition, m.fogSun, frame), m.baseColor.a);
#else
    color += clamp(environmentLight(surface, frame, irradianceMap, prefilteredMap, brdfLUT, linearSampler, probeVolumes, probes), 0.0, 60000.0) * aroundThroughSea;
    if (hasSea(frame) && frame.cameraUnderwater > 0.0) return float4(seenInSea(color, in.worldPosition, frame), m.baseColor.a);
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
                             depth2d<float> rainMap [[texture(MMTextureRainMap)]],
                             texture2d_array<float> cookies [[texture(MMTextureLightCookies)]],
                             constant LightShadowViews &lightShadows [[buffer(MMBufferLightShadows)]],
                             constant ProbeVolumes &probeVolumes [[buffer(MMBufferProbeVolumes)]],
                             device const float4 *probes [[buffer(MMBufferProbes)]],
                             depth2d<float> lightShadowAtlas [[texture(MMTextureLightShadows)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler shadowSampler [[sampler(MMSamplerShadow)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    if ((frame.wetness > 0.0 || frame.snow.x > 0.0) && m.water <= 0.0) {
        float reached = rainReaches(in.worldPosition, normalize(in.normal), frame, rainMap, shadowSampler);      // (under a roof neither falls)
        if (frame.wetness > 0.0) wetten(m, in, frame, pass, reached);
        if (frame.snow.x > 0.0 && m.baseColor.a > 0.99) snowOn(m, in, frame, reached);
    }
    float sunLight = sunVisibility(in.worldPosition, normalize(in.normal), in.viewDepth, frame, shadowMap, shadowSampler);
#if MOLTEN_EXTRAS
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
#endif
#if MOLTEN_CUSTOM
    // How far what is behind it is: for water, and for a shader of the game's own that asks (in.behind).
    if (m.water > 0.0 || m.custom > 0.5) m.waterDepth = waterDepthAt(in, pass, frame, sceneDepth);
#else
    if (m.water > 0.0) m.waterDepth = waterDepthAt(in, pass, frame, sceneDepth);
#endif
    return shadePixel(in, m, pass, frame, lights, cookies, lightShadows, lightShadowAtlas, shadowSampler, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler, probeVolumes, probes);
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
                                    depth2d<float> rainMap [[texture(MMTextureRainMap)]],
                                    texture2d_array<float> cookies [[texture(MMTextureLightCookies)]],
                                    constant LightShadowViews &lightShadows [[buffer(MMBufferLightShadows)]],
                                    constant ProbeVolumes &probeVolumes [[buffer(MMBufferProbeVolumes)]],
                                    device const float4 *probes [[buffer(MMBufferProbes)]],
                                    depth2d<float> lightShadowAtlas [[texture(MMTextureLightShadows)]],
                                    sampler materialSampler [[sampler(MMSamplerMaterial)]],
                                    sampler shadowSampler [[sampler(MMSamplerShadow)]],
                                    sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    if (m.baseColor.a < material.alphaCutoff) {
        discard_fragment();
    }
    m.baseColor.a = 1.0;
    if ((frame.wetness > 0.0 || frame.snow.x > 0.0) && m.water <= 0.0) {
        float reached = rainReaches(in.worldPosition, normalize(in.normal), frame, rainMap, shadowSampler);      // (under a roof neither falls)
        if (frame.wetness > 0.0) wetten(m, in, frame, pass, reached);
        if (frame.snow.x > 0.0 && m.baseColor.a > 0.99) snowOn(m, in, frame, reached);
    }
    float sunLight = sunVisibility(in.worldPosition, normalize(in.normal), in.viewDepth, frame, shadowMap, shadowSampler);
#if MOLTEN_EXTRAS
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
#endif
    return shadePixel(in, m, pass, frame, lights, cookies, lightShadows, lightShadowAtlas, shadowSampler, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler, probeVolumes, probes);
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
                             depth2d_array<float> shadowMap [[texture(MMTextureShadowMap)]],
                             texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                             texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                             texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                             texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                             depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                             depth2d<float> rainMap [[texture(MMTextureRainMap)]],
                             texture2d_array<float> cookies [[texture(MMTextureLightCookies)]],
                             constant LightShadowViews &lightShadows [[buffer(MMBufferLightShadows)]],
                             constant ProbeVolumes &probeVolumes [[buffer(MMBufferProbeVolumes)]],
                             device const float4 *probes [[buffer(MMBufferProbes)]],
                             depth2d<float> lightShadowAtlas [[texture(MMTextureLightShadows)]],
                             sampler materialSampler [[sampler(MMSamplerMaterial)]],
                             sampler shadowSampler [[sampler(MMSamplerShadow)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    if ((frame.wetness > 0.0 || frame.snow.x > 0.0) && m.water <= 0.0) {
        float reached = rainReaches(in.worldPosition, normalize(in.normal), frame, rainMap, shadowSampler);      // (under a roof neither falls)
        if (frame.wetness > 0.0) wetten(m, in, frame, pass, reached);
        if (frame.snow.x > 0.0 && m.baseColor.a > 0.99) snowOn(m, in, frame, reached);
    }
    // The sun's shadow: a ray from this pixel's own place toward the sun (more along an edge),
    // from the side of the surface that is seen.
    float3 seenSide = isFrontFacing ? normalize(in.normal) : -normalize(in.normal);
    float sunLight = sunVisibilityTraced(in.worldPosition, seenSide, -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
#if MOLTEN_EXTRAS
    // The light around the pixel: traced before this pass at half the size where that could
    // be done (a quarter of the rays, and smoothed), and otherwise here, a pixel at a time.
    float4 around;
    if (frame.tracedAmbientReady != 0) {
        float3 facing = isFrontFacing ? normalize(in.normal) : -normalize(in.normal);
        around = tracedAmbientAt(in.position.xy, in.worldPosition, facing, in.viewDepth, pass, tracedLight, tracedSurface);
    } else {
        around = ambientTraced(in.worldPosition, normalize(in.normal), in.viewDepth, in.position.xy, false, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    }
    m.occlusion *= around.a;
    m.bounced = around.rgb;
    // The light shafts: shadow maps drawn for them alone, near the camera (see renderView).
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
    if (m.roughness < frame.reflectionMaxRoughness) {
        float4 seen = reflectionTraced(in.worldPosition, m.normal, normalize(pass.cameraPosition - in.worldPosition), m.roughness, in.viewDepth, in.position.xy, frame,
                                       tracedScene, tracedSurfaces, irradianceMap, linearSampler);
        // How much the ray counts: all of it on a mirror, fading to nothing at the roughness where rays stop.
        float fade = saturate(1.0 - m.roughness / frame.reflectionMaxRoughness);
        m.mirror = seen.a * fade * fade;
        m.mirrored = seen.rgb;
    }
#endif
#if MOLTEN_CUSTOM
    // How far what is behind it is: for water, and for a shader of the game's own that asks (in.behind).
    if (m.water > 0.0 || m.custom > 0.5) m.waterDepth = waterDepthAt(in, pass, frame, sceneDepth);
#else
    if (m.water > 0.0) m.waterDepth = waterDepthAt(in, pass, frame, sceneDepth);
#endif
    return shadePixel(in, m, pass, frame, lights, cookies, lightShadows, lightShadowAtlas, shadowSampler, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler, probeVolumes, probes);
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
                                    depth2d_array<float> shadowMap [[texture(MMTextureShadowMap)]],
                                    texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                    texturecube<float> prefilteredMap [[texture(MMTexturePrefiltered)]],
                                    texture2d<float> brdfLUT [[texture(MMTextureBRDF)]],
                                    texture2d<float> ambientOcclusion [[texture(MMTextureAmbientOcclusion)]],
                                    depth2d<float> rainMap [[texture(MMTextureRainMap)]],
                                    texture2d_array<float> cookies [[texture(MMTextureLightCookies)]],
                                    constant LightShadowViews &lightShadows [[buffer(MMBufferLightShadows)]],
                                    constant ProbeVolumes &probeVolumes [[buffer(MMBufferProbeVolumes)]],
                                    device const float4 *probes [[buffer(MMBufferProbes)]],
                                    depth2d<float> lightShadowAtlas [[texture(MMTextureLightShadows)]],
                                    sampler materialSampler [[sampler(MMSamplerMaterial)]],
                                    sampler shadowSampler [[sampler(MMSamplerShadow)]],
                                    sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    MaterialSample m = sampleMaterial(in, isFrontFacing, material, baseColorTexture, normalTexture,
                                      metallicRoughnessTexture, occlusionTexture, emissiveTexture, materialSampler);
    if (m.baseColor.a < material.alphaCutoff) {
        discard_fragment();
    }
    m.baseColor.a = 1.0;
    if ((frame.wetness > 0.0 || frame.snow.x > 0.0) && m.water <= 0.0) {
        float reached = rainReaches(in.worldPosition, normalize(in.normal), frame, rainMap, shadowSampler);      // (under a roof neither falls)
        if (frame.wetness > 0.0) wetten(m, in, frame, pass, reached);
        if (frame.snow.x > 0.0 && m.baseColor.a > 0.99) snowOn(m, in, frame, reached);
    }
    // The sun's shadow: a ray from this pixel's own place toward the sun (more along an edge),
    // from the side of the surface that is seen.
    float3 seenSide = isFrontFacing ? normalize(in.normal) : -normalize(in.normal);
    float sunLight = sunVisibilityTraced(in.worldPosition, seenSide, -frame.sunDirection, in.viewDepth, in.position.xy, frame, tracedScene);
#if MOLTEN_EXTRAS
    // The light around the pixel: traced before this pass at half the size where that could
    // be done (a quarter of the rays, and smoothed), and otherwise here, a pixel at a time.
    float4 around;
    if (frame.tracedAmbientReady != 0) {
        float3 facing = isFrontFacing ? normalize(in.normal) : -normalize(in.normal);
        around = tracedAmbientAt(in.position.xy, in.worldPosition, facing, in.viewDepth, pass, tracedLight, tracedSurface);
    } else {
        around = ambientTraced(in.worldPosition, normalize(in.normal), in.viewDepth, in.position.xy, false, frame, tracedScene, tracedSurfaces, irradianceMap, linearSampler);
    }
    m.occlusion *= around.a;
    m.bounced = around.rgb;
    // The light shafts: shadow maps drawn for them alone, near the camera (see renderView).
    m.fogSun = fogInSunlight(in.worldPosition, pass.cameraPosition, in.viewDepth, in.position.xy, frame, shadowMap, shadowSampler);
    if (m.roughness < frame.reflectionMaxRoughness) {
        float4 seen = reflectionTraced(in.worldPosition, m.normal, normalize(pass.cameraPosition - in.worldPosition), m.roughness, in.viewDepth, in.position.xy, frame,
                                       tracedScene, tracedSurfaces, irradianceMap, linearSampler);
        // How much the ray counts: all of it on a mirror, fading to nothing at the roughness where rays stop.
        float fade = saturate(1.0 - m.roughness / frame.reflectionMaxRoughness);
        m.mirror = seen.a * fade * fade;
        m.mirrored = seen.rgb;
    }
#endif
    return shadePixel(in, m, pass, frame, lights, cookies, lightShadows, lightShadowAtlas, shadowSampler, sunLight, irradianceMap, prefilteredMap, brdfLUT,
                      ambientOcclusion, linearSampler, probeVolumes, probes);
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
