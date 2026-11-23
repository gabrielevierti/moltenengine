// Sky.metal
// A small procedural sky: a gradient from the horizon to the zenith, the ground
// below, and the sun. It is drawn behind the scene, and it is also rendered into a
// cubemap from which the image-based lighting is computed, so that reflections and
// ambient light always match what the player sees.

#include "Common.metal"
#include "Lighting.metal"      // the lights, for the light they give the air (fragment_lights_in_air)

// ---------------------------------------------------------------- clouds

// A repeatable random number for each cell of a grid.
static float cloudHash(float2 p) {
    p = fract(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Random values at the corners of each cell, blended smoothly in between.
static float cloudValueNoise(float2 p) {
    float2 cell = floor(p), f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float top = mix(cloudHash(cell), cloudHash(cell + float2(1.0, 0.0)), u.x);
    float bottom = mix(cloudHash(cell + float2(0.0, 1.0)), cloudHash(cell + float2(1.0, 1.0)), u.x);
    return mix(top, bottom, u.y);
}

// Five layers of that noise, each twice as fine and half as strong as the one before:
// large shapes with ragged edges, which is what a cloud is.
static float cloudNoise(float2 p) {
    float sum = 0.0, strength = 0.5;
    for (int i = 0; i < 5; i++) {
        sum += strength * cloudValueNoise(p);
        p = p * 2.03 + float2(17.0, 9.0);
        strength *= 0.5;
    }
    return sum;
}

// How much flat cloud there is at a place `p` of the layer (its own units: about a kilometer), and
// below what it is clear sky (`threshold`). Bent, so that the noise's pattern does not show; and,
// the fewer the clouds, the more they gather in groups between clear stretches instead of standing
// evenly over the sky. (The wind only carries them: their shapes are fixed to the sky, so that a
// change of wind moves them on from where they are instead of reshaping the whole sky.)
static float flatCloudAt(float2 p, constant FrameUniforms &frame, thread float &threshold) {
    float2 q = p;
    q += (float2(cloudValueNoise(q * 0.31 + 3.1), cloudValueNoise(q * 0.31 + 8.7)) - 0.5) * 1.1;
    float group = cloudValueNoise(q * 0.17 + float2(21.0, 5.0));
    float coverage = frame.cloudCoverage;
    float local = saturate(coverage + (group - 0.5) * 1.2 * pow(1.0 - coverage, 1.5));
    threshold = 0.8 - 0.52 * local;
    return cloudNoise(q);
}

// A field of stars, fixed to the sky and turning with it, each twinkling.
static float3 starsAt(float3 d, float time, float brightness) {
    // The sky turns about the pole, once a day.
    float turn = time * (2.0 * kPi / 86400.0);
    float c = cos(turn), s = sin(turn);
    float3 p = float3(d.x * c - d.z * s, d.y, d.x * s + d.z * c) * 160.0;
    float3 cell = floor(p);
    float3 inCell = p - cell;
    float3 seed = fract(sin(float3(dot(cell, float3(127.1, 311.7, 74.7)), dot(cell, float3(269.5, 183.3, 246.1)),
                                   dot(cell, float3(113.5, 271.9, 124.6)))) * 43758.5453);
    if (seed.x < 0.93) {
        return float3(0.0);                                     // most cells have no star
    }
    float3 at = 0.2 + 0.6 * seed;
    float distance = length(inCell - at);
    float size = 0.06 + 0.06 * seed.y;
    float glow = smoothstep(size, 0.0, distance);
    float twinkle = 0.75 + 0.25 * sin(time * (2.0 + seed.z * 5.0) + seed.y * 40.0);
    float strength = pow((seed.x - 0.93) / 0.07, 6.0) * 3.0 + 0.15;
    // Some a little blue, some a little gold.
    float3 tint = mix(float3(1.0, 0.85, 0.7), float3(0.75, 0.85, 1.0), seed.z);
    return tint * glow * twinkle * strength * brightness;
}

// ---------------------------------------------------------------- sunrise and sunset

// The color of sunlight that has crossed the air to a sun this high (the sine of its elevation):
// white overhead, gold, orange and red toward the horizon, as bright as it can be (the dimming is
// the CPU's, on the sun's light). Rayleigh scattering takes the blue first.
static float3 sunsetHue(float height) {
    float h = max(height, 0.0);
    float zenithDegrees = acos(min(h, 1.0)) * 180.0 / kPi;
    float air = max(1.0 / (h + 0.15 * pow(max(93.885 - zenithDegrees, 0.001), -1.253)), 1.0);
    float3 through = exp(-float3(0.069, 0.133, 0.298) * (air - 1.0));
    return through / max(max(through.r, through.g), 0.0001);
}

// A sky of three colors made to follow the sun down: the horizon glows gold and red round the
// sun, the zenith deepens, the opposite side turns pink above a darker band, and once the sun
// has set it all fades through the blue hour into a night lit by the moon: dark blue, not black.
static float3 sunsetSky(float3 color, float3 d, float3 toSun, float moon) {
    float h = toSun.y;
    float low = 1.0 - smoothstep(0.0, 0.4, h);              // 0 high up, 1 on the horizon
    float after = smoothstep(-0.22, -0.01, h);              // the glow left after it has set
    float3 hue = sunsetHue(h);
    float daylight = smoothstep(-0.2, 0.08, h);
    // The night sky: deep blue overhead, paler toward the horizon, brighter under a full moon.
    float3 night = mix(float3(0.024, 0.036, 0.075), float3(0.012, 0.02, 0.05), sqrt(saturate(d.y))) * (0.45 + 0.55 * moon);
    color = mix(night, color, daylight);
    color *= mix(float3(1.0), float3(0.72, 0.78, 1.0), low * after);
    float2 flatSky = normalize(d.xz + float2(1e-5, 0.0));
    float2 flatSun = normalize(toSun.xz + float2(1e-5, 0.0));
    float toward = dot(flatSky, flatSun) * 0.5 + 0.5;
    float band = exp(-max(d.y, 0.0) * mix(10.0, 3.0, toward * toward));
    float glow = band * (0.12 + 0.88 * pow(toward, 3.0)) * low * after;
    color += hue * glow * 1.4;
    // Across the sky from the sun: the pink belt, a little above the horizon.
    float away = 1.0 - toward;
    float belt = exp(-abs(d.y - 0.1) * 18.0) * away * away * low * smoothstep(-0.12, 0.02, h);
    color += float3(0.75, 0.4, 0.5) * belt * 0.18;
    return color;
}

// The light the clouds get: the sun's (or the moon's at night) and, when the sun is low or has
// just set, its glow from below, red: they are higher than the ground and still see it.
static float3 cloudSunlight(constant FrameUniforms &frame, float3 d, float3 toSun) {
    if (frame.sunsetColors == 0u) {
        return frame.sunColor;
    }
    float h = toSun.y;
    float low = 1.0 - smoothstep(0.0, 0.4, h);
    float still = smoothstep(-0.14, 0.0, h);                // above the clouds' height the sun sets later
    float2 flatSky = normalize(d.xz + float2(1e-5, 0.0));
    float2 flatSun = normalize(toSun.xz + float2(1e-5, 0.0));
    float toward = dot(flatSky, flatSun) * 0.5 + 0.5;
    return frame.sunColor + sunsetHue(h) * (low * still * (0.5 + 1.5 * toward * toward) * 1.6);
}

// The moon: opposite the sun, grey with its darker seas, and a halo in the air round it.
static float3 moonAt(float3 d, float3 toMoon, float size, float brightness) {
    float c = dot(d, toMoon);
    float halo = pow(saturate(c), 900.0) * 0.06 + pow(saturate(c), 40.0) * 0.012;
    float disk = smoothstep(size, size + 0.00006, c);
    if (disk <= 0.0) {
        return float3(0.75, 0.82, 1.0) * halo * brightness;
    }
    // Where on its face: the seas are a few large blotches.
    float3 side = normalize(cross(toMoon, float3(0.0, 1.0, 0.0001)));
    float3 upOnIt = cross(side, toMoon);
    float radius = sqrt(max(1.0 - size * size, 1e-8));
    float2 face = float2(dot(d - toMoon * c, side), dot(d - toMoon * c, upOnIt)) / radius;
    float seas = cloudNoise(face * 3.0 + float2(7.0, 3.0));
    float shade = mix(1.0, 0.62, smoothstep(0.45, 0.65, seas));
    return float3(0.92, 0.94, 1.0) * (disk * shade * 6.0 + halo) * brightness;
}

// `clouds`, with `hasVolume`: the clouds with depth along this direction, walked through
// elsewhere (Clouds.metal, at a lower resolution): the light they send toward the eye, and in
// `a` how much of the sky behind them still shows. Without it the clouds are a flat layer.
// ---------------------------------------------------------------- lightning

static float boltRandom(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x) * (1.0 / 4294967296.0);
}

// How bright the line of sight `d` from `eye` is for passing near the stretch of bolt from `a`
// to `b`: a core as wide as the channel (never less than a pixel), and the glow of the air
// round it. `beside` is the angle to the nearest point of it, in radians.
static float boltPiece(float3 eye, float3 d, float3 a, float3 b, float width) {
    float3 u = b - a, w = a - eye;
    float du = dot(d, u), dw = dot(d, w), uu = dot(u, u), uw = dot(u, w);
    float s = saturate((dw * du - uw) / max(uu - du * du, 1e-3));
    float3 p = w + u * s;
    float t = dot(p, d);
    if (t <= 0.0) return 0.0;
    float beside = length(p - d * t) / t;
    float core = max(width / t, 0.0011);
    return exp(-(beside * beside) / (core * core)) * 70.0 + exp(-beside / 0.008) * 1.4 + exp(-beside / 0.05) * 0.18;
}

// A bolt down from the clouds to the ground where the stroke is: a channel that zigzags down in
// short straight pieces (two to each step of its way, the second bent a little off the first), and
// a few branches that leave it on the way and fade out. Its shape comes from the stroke's seed;
// the same for each flash of the stroke, as a real channel is lit again by each return stroke.
static float3 lightningBolt(float3 d, constant FrameUniforms &frame) {
    if (frame.lightningSeed <= 0.0 || frame.lightning <= 0.0) return float3(0.0);
    float3 eye = frame.cameraPosition;
    float top = frame.cloudBase + 60.0, ground = frame.lightningGround;
    float tall = top - ground;
    if (tall < 20.0) return float3(0.0);
    float2 strike = float2(frame.lightningX, frame.lightningZ);
    // Nowhere near it, for most of the sky: an upright cylinder round its way, and the glow round that.
    float reach = tall * 0.6 + 250.0;
    float2 flat = d.xz;
    float ahead = max(dot(strike - eye.xz, flat) / max(dot(flat, flat), 1e-6), 0.0);
    float height = eye.y + d.y * ahead;
    float far = length(strike - eye.xz) + 1.0;
    if (length(eye.xz + flat * ahead - strike) > reach + far * 0.08 || height < ground - reach || height > top + reach) return float3(0.0);

    uint seed = uint(frame.lightningSeed) * 7919u;
    const int steps = 26;
    float rise = tall / float(steps);
    // It leans as a whole, and zigzags on the way; the zigzags are evened out along it so that it
    // ends exactly where the stroke is.
    float2 lean = (float2(boltRandom(seed), boltRandom(seed + 1u)) - 0.5) * (tall * 0.35);
    float2 wander = float2(0.0);
    for (int i = 0; i < steps; i++) {
        uint h = seed + 100u + uint(i) * 6u;
        wander += (float2(boltRandom(h), boltRandom(h + 1u)) - 0.5) * 1.9;
    }
    float3 forks[4];
    int forkAt[4];
    for (int k = 0; k < 4; k++) forkAt[k] = 2 + int(boltRandom(seed + 40u + uint(k)) * float(steps) * 0.6);
    float light = 0.0;
    float2 walked = float2(0.0);
    float3 p = float3(strike.x + lean.x, top, strike.y + lean.y);
    for (int i = 0; i < steps; i++) {
        uint h = seed + 100u + uint(i) * 6u;
        walked += (float2(boltRandom(h), boltRandom(h + 1u)) - 0.5) * 1.9;
        float share = float(i + 1) / float(steps);
        float2 across = strike + lean * (1.0 - share) + (walked - wander * share) * rise;
        float3 q = float3(across.x, top - rise * float(i + 1), across.y);
        float3 bend = (p + q) * 0.5 + float3(boltRandom(h + 2u) - 0.5, 0.0, boltRandom(h + 3u) - 0.5) * (rise * 0.6);
        light += boltPiece(eye, d, p, bend, 1.4) + boltPiece(eye, d, bend, q, 1.4);
        for (int k = 0; k < 4; k++) {
            if (forkAt[k] == i) forks[k] = q;
        }
        p = q;
    }
    // The branches: off to one side and down, more steeply as they go, dimmer to their ends.
    for (int k = 0; k < 4; k++) {
        uint h = seed + 400u + uint(k) * 97u;
        float2 away = normalize(float2(boltRandom(h), boltRandom(h + 1u)) - 0.5 + 1e-4);
        float3 b = forks[k];
        const int pieces = 9;
        float stride = rise * (0.6 + 0.6 * boltRandom(h + 2u));
        for (int j = 0; j < pieces; j++) {
            float2 jag = (float2(boltRandom(h + 10u + uint(j) * 2u), boltRandom(h + 11u + uint(j) * 2u)) - 0.5) * 1.4;
            float3 c = b + float3((away.x * 1.2 + jag.x) * stride, -stride * (0.5 + 0.08 * float(j)), (away.y * 1.2 + jag.y) * stride);
            light += boltPiece(eye, d, b, c, 0.8) * (0.45 * (1.0 - float(j) / float(pieces)));
            b = c;
        }
    }
    // Through the rain and the haze, a far bolt is dimmer.
    float haze = exp(-far * frame.fogDensity * 0.35);
    return float3(0.86, 0.9, 1.0) * (light * frame.lightning * haze);
}

// Where the clouds glow from inside when lightning flashes: brightest round the stroke, fading
// over a kilometer or two; and a little everywhere, the light scattered on through them.
static float lightningGlow(float3 at, constant FrameUniforms &frame) {
    float3 heart = float3(frame.lightningX, frame.cloudBase + (frame.cloudTop - frame.cloudBase) * 0.3, frame.lightningZ);
    float3 away = (at - heart) / 1000.0;
    return frame.lightning * (0.08 + 1.6 / (1.0 + dot(away, away) * 5.0));
}

static float3 skyRadianceWithClouds(float3 direction, constant FrameUniforms &frame, bool withSunDisk, float4 clouds, bool hasVolume) {
    float3 d = normalize(direction);
    // The sky follows the sun; the clouds are lit by what lights the scene: the sun, or the moon at night.
    float3 toSun = -frame.skySunDirection;
    float3 toLight = -frame.sunDirection;
    float moon = frame.moonBrightness;

    // Horizon to zenith. The square root keeps the horizon band thin, as in a real sky.
    float3 color = mix(frame.skyHorizonColor, frame.skyZenithColor, sqrt(saturate(d.y)));
    if (frame.sunsetColors != 0u) color = sunsetSky(color, d, toSun, moon);
    float3 cloudSun = cloudSunlight(frame, d, toSun);
    // The clouds' shaded side at dusk: grey-violet, with a little of the glow.
    float dusk = frame.sunsetColors != 0u ? (1.0 - smoothstep(0.0, 0.4, toSun.y)) * smoothstep(-0.25, 0.0, toSun.y) : 0.0;
    float dark = frame.sunsetColors != 0u ? mix(0.12 + 0.1 * moon, 1.0, smoothstep(-0.2, 0.08, toSun.y)) : 1.0;
    float night = frame.sunsetColors != 0u ? smoothstep(0.05, -0.12, toSun.y) : 0.0;
    // The stars, when the sun is down; dimmed toward the horizon, where the air is thick.
    if (frame.starBrightness > 0.0 && withSunDisk && d.y > 0.0 && night > 0.0) {
        color += starsAt(d, frame.starTime, frame.starBrightness) * night * smoothstep(0.0, 0.25, d.y);
    }
    // The moon, at night, across the sky from the sun.
    if (moon > 0.0 && night > 0.0 && d.y > -0.02) {
        color += moonAt(d, -toSun, frame.sunDiskSize, moon * night) * (withSunDisk ? 1.0 : 0.25);
    }
    // Below the horizon, blend quickly into the ground color; or, with a sea, into the sea far
    // away, beyond what the camera draws of it: the sky mirrored in it, more of it the flatter
    // one looks along it, its own color under that, and the haze of the distance over both.
    float3 below = frame.skyGroundColor * dark;
    if (hasSea(frame) && frame.cameraPosition.y > frame.seaColor.w && d.y < 0.0) {
        float down = max(-d.y, 0.002);
        float3 mirrored = mix(frame.skyHorizonColor, frame.skyZenithColor, sqrt(down)) * dark;
        float fresnel = 0.02 + 0.98 * pow(1.0 - down, 5.0);
        float3 deep = frame.seaColor.rgb * (frame.skyHorizonColor * 0.9 + frame.sunColor * saturate(toSun.y) * 0.25) * dark;
        float3 sea = mix(deep, mirrored, fresnel);
        float away = (frame.cameraPosition.y - frame.seaColor.w) / down;
        below = mix(sea, frame.fogColor * dark, saturate(1.0 - exp(-frame.fogDensity * away)));
    }
    color = mix(below, color, smoothstep(hasSea(frame) ? -0.02 : -0.12, 0.0, d.y));

    // A glow around the sun: a tight one and a wide one.
    float towardSun = saturate(dot(d, toSun));
    float sunGlow = 0.35 * pow(towardSun, 64.0) + 0.08 * pow(towardSun, 6.0);
    color += frame.sunColor * sunGlow * smoothstep(-0.1, 0.05, d.y) * (1.0 - night);

    float cover = 0.0;
    if (hasVolume) {
        cover = 1.0 - clouds.a;
        color = color * clouds.a + clouds.rgb;
    } else
    if (frame.cloudCoverage > 0.0 && d.y > 0.0) {
        // Clouds as a flat layer overhead. Dividing by the height of the direction is the
        // perspective of looking along a ceiling: clouds get smaller and closer together
        // toward the horizon.
        float2 p = d.xz / (d.y + 0.1) * frame.cloudScale - frame.cloudOffset;      // read upwind: they go the way the wind blows
        float threshold = 0.0, ignored = 0.0;
        float density = flatCloudAt(p, frame, threshold);
        cover = smoothstep(threshold, threshold + 0.2, density) * smoothstep(0.0, 0.1, d.y);
        // A second look a little toward the light: where the cloud is thinner there than
        // here, this side is lit; where it is thicker, this side is in its shade.
        float2 sunward = toLight.xz / max(length(toLight.xz), 0.001) * 0.3;
        float light = saturate(0.62 + (density - flatCloudAt(p + sunward, frame, ignored)) * 3.0);
        float3 shade = (frame.skyHorizonColor * 0.9 + frame.skyZenithColor * 0.35) * dark;
        shade = mix(shade, shade * float3(0.62, 0.5, 0.66) + sunsetHue(toSun.y) * 0.18 * dark, dusk);
        float3 lit = cloudSun * 0.3 + frame.skyHorizonColor * 0.5 * dark;
        // Lightning lights them from inside: round the stroke, where this line of sight meets them.
        float glow = frame.lightning > 0.0 ? lightningGlow(frame.cameraPosition + d * max((frame.cloudBase - frame.cameraPosition.y) / max(d.y, 0.02), 0.0), frame) : 0.0;
        color = mix(color, mix(shade, lit, light) + float3(0.78, 0.82, 1.0) * (glow * 2.6), cover);
    }

    if (withSunDisk) {
        float c = dot(d, toSun);
        float disk = smoothstep(frame.sunDiskSize, frame.sunDiskSize + 0.00008, c);
        // Darker toward its rim (limb darkening): a ball of gas seen edge on.
        float across = saturate((1.0 - c) / max(1.0 - frame.sunDiskSize, 1e-6));
        float limb = 1.0 - 0.55 * (1.0 - sqrt(max(1.0 - across, 0.0)));
        float3 sunLight = frame.sunsetColors != 0u ? frame.sunColor * (1.0 - night) : frame.sunColor;
        color += sunLight * frame.sunDiskIntensity * disk * limb * (1.0 - cover);
    }
    color *= frame.skyIntensity;
    // A bolt is in front of the clouds it leaves, seen from under them; from above, they hide it.
    if (withSunDisk && frame.lightningSeed > 0.0) {
        color += lightningBolt(d, frame) * (frame.cameraPosition.y > frame.cloudBase ? 1.0 - cover : 1.0);
    }
    return color;
}

static float3 skyRadiance(float3 direction, constant FrameUniforms &frame, bool withSunDisk) {
    return skyRadianceWithClouds(direction, frame, withSunDisk, float4(0.0, 0.0, 0.0, 1.0), false);
}

// ---------------------------------------------------------------- the visible sky

struct SkyOut {
    float4 position [[position]];
    float3 direction;
};

// A full-screen triangle placed exactly on the far plane (z = w), drawn after the
// opaque geometry with a "less or equal" depth test: only the pixels that nothing
// else covered run the fragment function.
// The corner `vertexID` of a triangle that covers the screen, on the far plane, and the way the
// eye looks through it. (A vertex function cannot call another: both call this.)
static SkyOut skyCorner(uint vertexID, constant PassUniforms &pass) {
    float2 uv = float2(float(vertexID & 2), float((vertexID << 1) & 2));
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    SkyOut out;
    out.position = float4(ndc, 1.0, 1.0);
    // Where this corner of the far plane is in the world gives the view direction.
    float4 world = pass.inverseViewProjection * float4(ndc, 1.0, 1.0);
    out.direction = world.xyz / world.w - pass.cameraPosition;
    return out;
}

vertex SkyOut vertex_sky(uint vertexID [[vertex_id]],
                         constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    return skyCorner(vertexID, pass);
}

fragment float4 fragment_sky(SkyOut in [[stage_in]],
                             constant PassUniforms &pass [[buffer(MMBufferPass)]],
                             constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                             texture2d<float> clouds [[texture(MMTextureClouds)]],
                             sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    // Under the sea, what is below the horizon is the water itself, far away. (Above it the sky is
    // drawn as ever: the water's surface, drawn over it, shows it through its window overhead.)
    if (hasSea(frame) && frame.cameraUnderwater > 0.0 && in.direction.y <= 0.0) return float4(seaHaze(frame, frame.cameraUnderwater), 1.0);
#if MOLTEN_EXTRAS
    // Clouds with depth were walked through before this pass, into a smaller picture: read
    // here, smoothed up to the size of this one, and put in front of the sky.
    if (frame.volumetricClouds != 0 && !is_null_texture(clouds)) {
        // Four looks round the pixel, each already smoothed between four of the smaller
        // picture's: the grain its few steps leave is averaged away, and its edges are soft.
        float2 uv = in.position.xy / pass.viewportSize;
        float2 texel = 0.75 / float2(clouds.get_width(), clouds.get_height());
        float4 seen = clouds.sample(linearSampler, uv + texel * float2(1.0, 1.0)) + clouds.sample(linearSampler, uv + texel * float2(-1.0, 1.0)) +
                      clouds.sample(linearSampler, uv + texel * float2(1.0, -1.0)) + clouds.sample(linearSampler, uv + texel * float2(-1.0, -1.0));
        return float4(skyRadianceWithClouds(in.direction, frame, true, seen * 0.25, true), 1.0);
    }
#endif
    return float4(skyRadiance(in.direction, frame, true), 1.0);
}

// ---------------------------------------------------------------- lights in the air

// The air a light passes through, lit by it (Light::isVolumetric): along each line of sight, from
// the eye to the first thing seen there (the prepass's depth; the sky is as far as the light
// reaches), what the air scatters toward the eye of that light, added to the picture. A light is
// walked only where its sphere is crossed, in steps spread as its light falls off (as many for each
// angle seen from the light: "equiangular"), so that the glow right by a lamp is not missed; a spot
// light's cone, cookie and shadow are looked up at each. The air is thicker where the fog is (and low,
// if the fog lies low), and what is farther is dimmed by the fog in front of it. Scattered forward
// more than back, as by haze: brightest looking toward the light.
// (Drawn over the whole picture, nearer than anything: z is 0.)
// Where a line (from `eye`, along `d`) is inside a spot light's cone, between `t0` and `t1`: the
// span in it, or an empty one (x > y). Outside the cone the light gives the air nothing, so the
// steps are spent only inside.
static float2 insideCone(float3 eye, float3 d, LightData light, float t0, float t1) {
    float co = clamp(-light.spotOffset / light.spotScale, 0.0, 0.9999);          // the cosine of the outer half-angle
    float co2 = co * co;
    float3 v = eye - light.position;
    float da = dot(d, light.direction), va = dot(v, light.direction);
    float A = da * da - co2, B = 2.0 * (da * va - co2 * dot(v, d)), C = va * va - co2 * dot(v, v);
    // On the side the light shines toward (not the cone's mirror behind it).
    float lo = t0, hi = t1;
    if (abs(da) > 1e-6) {
        float turn = -va / da;
        if (da > 0.0) lo = max(lo, turn); else hi = min(hi, turn);
    } else if (va < 0.0) {
        return float2(1.0, 0.0);
    }
    if (lo >= hi) return float2(1.0, 0.0);
    // Inside the double cone: A t² + B t + C >= 0.
    if (abs(A) < 1e-6) {
        if (abs(B) < 1e-9) return C >= 0.0 ? float2(lo, hi) : float2(1.0, 0.0);
        float r = -C / B;
        if (B > 0.0) lo = max(lo, r); else hi = min(hi, r);
        return float2(lo, hi);
    }
    float disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return A > 0.0 ? float2(lo, hi) : float2(1.0, 0.0);
    float root = sqrt(disc);
    float r1 = (-B - root) / (2.0 * A), r2 = (-B + root) / (2.0 * A);
    if (r1 > r2) { float swap = r1; r1 = r2; r2 = swap; }
    if (A < 0.0) return float2(max(lo, r1), min(hi, r2));
    // Outside the two roots: of the two pieces, what is left on this side (the hull, if both are).
    float2 before = float2(lo, min(hi, r1)), after = float2(max(lo, r2), hi);
    bool hasBefore = before.x < before.y, hasAfter = after.x < after.y;
    if (hasBefore && hasAfter) return float2(before.x, after.y);
    return hasBefore ? before : after;
}

vertex SkyOut vertex_lights_in_air(uint vertexID [[vertex_id]],
                                   constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    SkyOut out = skyCorner(vertexID, pass);
    out.position.z = 0.0;
    return out;
}

fragment float4 fragment_lights_in_air(SkyOut in [[stage_in]],
                                       constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                       constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                       constant LightData *lights [[buffer(MMBufferLights)]],
                                       depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                                       texture2d_array<float> cookies [[texture(MMTextureLightCookies)]],
                                       constant LightShadowViews &lightShadows [[buffer(MMBufferLightShadows)]],
                                       depth2d<float> lightShadowAtlas [[texture(MMTextureLightShadows)]],
                                       sampler linearSampler [[sampler(MMSamplerLinearClamp)]],
                                       sampler shadowSampler [[sampler(MMSamplerShadow)]]) {
    if (hasSea(frame) && frame.cameraUnderwater > 0.0) return float4(0.0);       // in the water there is no air to light
    float3 eye = pass.cameraPosition;
    float3 d = normalize(in.direction);
    float farthest = 1e7;
    if (!is_null_texture(sceneDepth)) {
        uint2 pixel = min(uint2(in.position.xy), uint2(sceneDepth.get_width() - 1, sceneDepth.get_height() - 1));
        float stored = sceneDepth.read(pixel);
        if (stored < 1.0) {
            float2 ndc = in.position.xy / pass.viewportSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
            float4 world = pass.inverseViewProjection * float4(ndc, stored, 1.0);
            farthest = length(world.xyz / world.w - eye);
        }
    }
    const float g = 0.3;                                         // how much more it scatters forward
    float start = pixelNoise(in.position.xy);
    float3 total = float3(0.0);
    for (uint i = 0; i < frame.lightCount; i++) {
        LightData light = lights[i];
        if (light.volumetric <= 0.0 || (light.type != MMLightPoint && light.type != MMLightSpot)) continue;
        float range = light.range;
        float3 toLight = light.position - eye;
        float s = dot(toLight, d);                               // how far along the line it passes nearest the light
        float aside2 = max(dot(toLight, toLight) - s * s, 0.0);
        if (aside2 >= range * range) continue;
        float halfChord = sqrt(range * range - aside2);       // ("half" is a type in Metal)
        float t0 = max(s - halfChord, 0.0), t1 = min(s + halfChord, farthest);
        if (light.type == MMLightSpot) {
            float2 span = insideCone(eye, d, light, t0, t1);
            t0 = span.x, t1 = span.y;
        }
        if (t1 <= t0) continue;
        // Its light falls off as 1 / (r² + 1) (Lighting.metal): spread the steps by that, and what is
        // left at each (the window to its range, the cone, the cookie, the fog) is all they look up.
        float h = sqrt(aside2 + 1.0);
        float a0 = atan((t0 - s) / h), a1 = atan((t1 - s) / h);
        uint steps = light.type == MMLightSpot ? 16u : 6u;
        float3 sum = float3(0.0);
        for (uint k = 0; k < steps; k++) {
            float t = s + h * tan(mix(a0, a1, (float(k) + start) / float(steps)));
            float3 at = eye + d * t;
            float3 fromLight = at - light.position;
            float r2 = max(dot(fromLight, fromLight), 1e-6);
            float ratio2 = r2 / (range * range);
            float window = saturate(1.0 - ratio2 * ratio2);
            float lit = window * window;
            float3 way = fromLight * rsqrt(r2);
            if (light.type == MMLightSpot) {
                float cone = saturate(dot(way, light.direction) * light.spotScale + light.spotOffset);
                lit *= cone * cone;
            }
            if (lit <= 0.0) continue;
            float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * dot(way, -d), 1.5);
            float thick = frame.fogHeightFalloff > 0.00001 ? exp(clamp(-frame.fogHeightFalloff * at.y, -40.0, 40.0)) : 1.0;
            float3 seen = float3(lit * phase * thick * exp(-frame.fogDensity * thick * t));
            if (light.cookie > 0.5) seen *= lightCookie(light, at, cookies, linearSampler, 1.0);
            // What stands in its way shades the air behind it too: beams through a window, the dark behind a pillar.
            if (light.shadow > 0.5) seen *= lightShadow(light, at, float3(0.0), lightShadows, lightShadowAtlas, shadowSampler);
            sum += seen;
        }
        total += light.color * light.volumetric * sum * ((a1 - a0) / (h * float(steps)));
    }
    // Clear air lights a little; fog more.
    float air = 0.01 + 4.0 * max(frame.fogDensity, 0.0);
    return float4(clamp(total * air, 0.0, 60000.0), 1.0);
}

// ---------------------------------------------------------------- the sky as a cubemap

// The direction that a texel of a cubemap face looks at. `uv` has its origin at the
// top left of the face. This is the layout every graphics API shares.
static float3 cubeDirection(uint face, float2 uv) {
    float2 p = uv * 2.0 - 1.0;
    switch (face) {
        case 0: return float3(1.0, -p.y, -p.x);         // +X
        case 1: return float3(-1.0, -p.y, p.x);         // -X
        case 2: return float3(p.x, 1.0, p.y);           // +Y
        case 3: return float3(p.x, -1.0, -p.y);         // -Y
        case 4: return float3(p.x, -p.y, 1.0);          // +Z
        default: return float3(-p.x, -p.y, -1.0);       // -Z
    }
}

// Renders one face of the sky cubemap. The sun's disk is left out: direct sunlight is
// computed analytically, and a tiny, extremely bright spot would only add noise to the
// filtered maps.
fragment float4 fragment_sky_capture(FullscreenOut in [[stage_in]],
                                     constant CaptureUniforms &capture [[buffer(MMBufferPass)]],
                                     constant FrameUniforms &frame [[buffer(MMBufferFrame)]]) {
    return float4(skyRadiance(cubeDirection(capture.face, in.uv), frame, false), 1.0);
}
