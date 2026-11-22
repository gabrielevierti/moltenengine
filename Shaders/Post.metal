// Post.metal
// Everything that happens to the image after the scene is drawn: screen-space
// ambient occlusion, bloom, and the final tone mapping.

#include "IBL.metal"

// ---------------------------------------------------------------- bloom

// Keeps what is brighter than the threshold, with a soft "knee" below it so that
// pixels do not pop in and out of the bloom as their brightness changes.
static float3 softThreshold(float3 color, float threshold, float knee) {
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, brightness - threshold) / max(brightness, 0.00001);
    return color * contribution;
}

// First step: the bright part of the scene, at half resolution.
fragment float4 fragment_bloom_prefilter(FullscreenOut in [[stage_in]],
                                         constant PostUniforms &post [[buffer(MMBufferPass)]],
                                         texture2d<float> source [[texture(0)]],
                                         sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    // Four taps placed between texels: each one averages a 2 x 2 block.
    float2 o = post.sourceTexelSize;
    float3 color = source.sample(linearSampler, in.uv + float2(-o.x, -o.y)).rgb
                 + source.sample(linearSampler, in.uv + float2(o.x, -o.y)).rgb
                 + source.sample(linearSampler, in.uv + float2(-o.x, o.y)).rgb
                 + source.sample(linearSampler, in.uv + float2(o.x, o.y)).rgb;
    color *= 0.25;
    color = min(color, float3(64.0));                   // one absurdly bright pixel must not light up the screen
    return float4(softThreshold(color, post.bloomThreshold, post.bloomKnee), 1.0);
}

// Each level of the chain is half the size of the one before. ("Dual filtering" by
// Marius Bjorge: five taps going down, eight going up.)
fragment float4 fragment_bloom_downsample(FullscreenOut in [[stage_in]],
                                          constant PostUniforms &post [[buffer(MMBufferPass)]],
                                          texture2d<float> source [[texture(0)]],
                                          sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float2 h = post.sourceTexelSize * 0.5;
    float3 sum = source.sample(linearSampler, in.uv).rgb * 4.0;
    sum += source.sample(linearSampler, in.uv - h).rgb;
    sum += source.sample(linearSampler, in.uv + h).rgb;
    sum += source.sample(linearSampler, in.uv + float2(h.x, -h.y)).rgb;
    sum += source.sample(linearSampler, in.uv - float2(h.x, -h.y)).rgb;
    return float4(sum / 8.0, 1.0);
}

// Going back up, each level is blurred and added to the larger one (the pipeline
// blends additively), so wide and narrow glows pile up.
fragment float4 fragment_bloom_upsample(FullscreenOut in [[stage_in]],
                                        constant PostUniforms &post [[buffer(MMBufferPass)]],
                                        texture2d<float> source [[texture(0)]],
                                        sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float2 h = post.sourceTexelSize * 0.5 * post.filterRadius;
    float3 sum = source.sample(linearSampler, in.uv + float2(-h.x * 2.0, 0.0)).rgb;
    sum += source.sample(linearSampler, in.uv + float2(-h.x, h.y)).rgb * 2.0;
    sum += source.sample(linearSampler, in.uv + float2(0.0, h.y * 2.0)).rgb;
    sum += source.sample(linearSampler, in.uv + float2(h.x, h.y)).rgb * 2.0;
    sum += source.sample(linearSampler, in.uv + float2(h.x * 2.0, 0.0)).rgb;
    sum += source.sample(linearSampler, in.uv + float2(h.x, -h.y)).rgb * 2.0;
    sum += source.sample(linearSampler, in.uv + float2(0.0, -h.y * 2.0)).rgb;
    sum += source.sample(linearSampler, in.uv + float2(-h.x, -h.y)).rgb * 2.0;
    return float4(sum / 12.0, 1.0);
}

// ---------------------------------------------------------------- tone mapping

// The ACES filmic curve, as fitted by Krzysztof Narkowicz.
static float3 acesFilm(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// A different random number for every pixel and every moment.
static float videoNoise(float2 p, float time) {
    return fract(sin(dot(p + fract(time * 7.13) * float2(37.0, 17.0), float2(12.9898, 78.233))) * 43758.5453);
}

// Linear light to the numbers of an sRGB picture, and back.
static float3 gradeToSRGB(float3 c) {
    c = saturate(c);
    return select(1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, c * 12.92, c <= 0.0031308);
}
static float3 gradeToLinear(float3 c) {
    return select(pow((c + 0.055) / 1.055, float3(2.4)), c / 12.92, c <= 0.04045);
}

// The color grading table: `size` squares side by side, red across each, green down it, blue
// from one square to the next. The color is looked up between the two squares its blue falls
// between, as an sRGB picture painted by eye.
static float3 gradeWithTable(float3 linearColor, texture2d<float> table, sampler linearSampler, float size) {
    float3 c = gradeToSRGB(linearColor);
    float blue = c.b * (size - 1.0);
    float lower = floor(blue);
    float upper = min(lower + 1.0, size - 1.0);
    float x = (c.r * (size - 1.0) + 0.5) / (size * size);
    float y = (c.g * (size - 1.0) + 0.5) / size;
    float3 a = table.sample(linearSampler, float2(x + lower / size, y), level(0.0)).rgb;
    float3 b = table.sample(linearSampler, float2(x + upper / size, y), level(0.0)).rgb;
    return gradeToLinear(mix(a, b, blue - lower));
}

// ---------------------------------------------------------------- the camera through the sea's surface

static float glassHash(float2 p) {
    p = fract(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Water left on the glass when the camera comes up out of the sea: drops of all sizes that slide
// down it, each a small lens that shows what is behind it upside down, darker at its rim. Returns
// how far each pixel's look is moved (xy) and how much its rim darkens it (z).
static float3 lensDrops(float2 uv, float time, float aspect) {
    float2 p = float2(uv.x * aspect, uv.y) * 11.0;
    float3 result = float3(0.0);
    for (int layer = 0; layer < 2; layer++) {
        float2 q = p * (layer == 0 ? 1.0 : 1.9) + float2(layer * 3.7, 0.0);
        float column = floor(q.x);
        float slide = glassHash(float2(column, 7.0 + layer)) * 0.8 + 0.2;   // each column of drops slides at its own pace
        q.y -= time * slide * (layer == 0 ? 0.35 : 0.2);
        float2 cell = floor(q), inCell = fract(q) - 0.5;
        float there = glassHash(cell + float2(layer * 13.0, 5.0));
        if (there < 0.45) continue;
        float2 middle = (float2(glassHash(cell + 1.3), glassHash(cell + 9.1)) - 0.5) * 0.5;
        float radius = mix(0.12, 0.34, glassHash(cell + 4.7));
        float2 off = inCell - middle;
        off.y *= 0.85 + 0.3 * glassHash(cell + 2.2);                         // a little longer than wide: they run
        float r = length(off) / radius;
        if (r >= 1.0) continue;
        float bulge = sqrt(1.0 - r * r);
        result.xy += -off / 11.0 * (0.9 + 0.6 * bulge) * float2(1.0 / aspect, 1.0);
        result.z = max(result.z, smoothstep(0.7, 1.0, r) * 0.45);
    }
    return result;
}

// What the camera sees as it goes through the surface: where the sea's surface crosses its glass
// a wavering line, bent by the water's edge (a meniscus); on the other side of it, the other
// world: under the water from above (blurred, the red taken out), or above it from under (bright,
// blurred, as through the water's skin). `hdr` is the picture here; `uv` where on the glass.
static float3 lensThroughWater(float2 uv, float3 hdr, constant PostUniforms &post, texture2d<float> scene, sampler linearSampler, float aspect) {
    float3 line = post.lensWater.xyz;
    float slope = max(length(line.xy), 1e-4);
    float over = dot(line.xy, uv) + line.z;
    // The surface is not flat across the glass: small waves lap at it.
    float t = post.time;
    over += (sin(uv.x * aspect * 9.0 + t * 2.3) * 0.012 + sin(uv.x * aspect * 23.0 - t * 3.7) * 0.005 + sin(uv.y * 17.0 + t * 1.7) * 0.003);
    float across = over / slope;                                        // in the picture's heights, signed
    bool pictureIsUnder = post.lensWater.w > 1.5;
    bool hereIsUnder = over < 0.0;
    float3 color = hdr;
    if (hereIsUnder != pictureIsUnder) {
        // The other side: what this camera's picture is not of. Blurred, as through water on the glass.
        float2 texel = 1.0 / float2(scene.get_width(), scene.get_height());
        float3 blur = float3(0.0);
        for (int i = 0; i < 8; i++) {
            float a = float(i) * 0.785398;
            blur += scene.sample(linearSampler, uv + float2(cos(a), sin(a)) * texel * 9.0).rgb;
        }
        blur = blur * 0.125 * 0.6 + hdr * 0.4;
        if (pictureIsUnder) {
            // Above, from under: bright, washed out.
            float gray = dot(blur, float3(0.2126, 0.7152, 0.0722));
            color = mix(blur, float3(gray), 0.35) * 1.7 + gray * 0.25;
        } else {
            // Under, from above: through a little water, the red going first.
            color = blur * exp(-post.lensSea.xyz * 1.2) * 0.8;
        }
    }
    // The water's edge on the glass: it bends what is behind it, darker in its middle, bright at its lip.
    float edge = abs(across);
    float width = 0.012;
    if (edge < width) {
        float k = 1.0 - edge / width;
        float2 normal = line.xy / slope;
        float3 bent = scene.sample(linearSampler, uv - normal * (k * k * 0.03 * sign(across))).rgb;
        color = mix(color, bent, k);
        color *= 1.0 - 0.55 * smoothstep(0.55, 1.0, k);
        color += hdr * 0.4 * smoothstep(0.2, 0.5, k) * (1.0 - smoothstep(0.5, 0.8, k));
    }
    return color;
}

// The last pass: the lens, scene plus bloom, exposure, tone mapping, vignette, and the
// flaws of a video link. The result is linear; the drawable has an sRGB format, so
// Metal applies the gamma curve itself.
fragment float4 fragment_tonemap(FullscreenOut in [[stage_in]],
                                 constant PostUniforms &post [[buffer(MMBufferPass)]],
                                 texture2d<float> scene [[texture(0)]],
                                 texture2d<float> bloom [[texture(1)]],
                                 texture2d<float> gradingTable [[texture(2)]],
                                 texture2d<float> adaptedExposure [[texture(3)]],
                                 texture2d<float> lensDirt [[texture(4)]],
                                 sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float2 uv = in.uv;

    // A weak analog video signal tears: whole lines of the picture slide sideways.
    if (post.interference > 0.0) {
        float band = floor(uv.y * 90.0);
        float tear = videoNoise(float2(band, 3.0), floor(post.time * 24.0));
        uv.x += (tear - 0.5) * 0.12 * post.interference * step(1.0 - post.interference * 0.6, videoNoise(float2(7.0, band), floor(post.time * 12.0)));
    }

    // A wide lens bends the picture: the middle is magnified and the edges are squeezed,
    // so straight lines bow outward ("barrel distortion"). The scene is rendered with a
    // normal perspective; here every pixel of the screen looks up where its light would
    // have come from. The corners stay where they are, so nothing is cut off.
    float2 centered = uv * 2.0 - 1.0;
    float aspect = float(scene.get_width()) / float(scene.get_height());
    float2 q = centered * float2(aspect, 1.0) * rsqrt(aspect * aspect + 1.0);      // the corners are at distance 1
    float r2 = dot(q, q);
    float bend = (1.0 + post.lensDistortion * r2) / (1.0 + post.lensDistortion);
    // Water on the glass moves what each pixel shows: drops left from coming out of the sea, and
    // the shake of going into it.
    float2 wet = float2(0.0);
    float rimShade = 0.0;
    if (post.lensSea.w > 0.002) {
        float3 drops = lensDrops(in.uv, post.time, aspect);
        wet = drops.xy * post.lensSea.w;
        rimShade = drops.z * post.lensSea.w;
    }
    if (post.lensMore.x > 0.002) {
        wet += float2(sin(in.uv.y * 38.0 + post.time * 31.0), cos(in.uv.x * 33.0 + post.time * 27.0)) * (0.007 * post.lensMore.x);
    }
    float2 lensUV = centered * bend * 0.5 + 0.5 + wet;

    float3 hdr;
    if (post.chromaticAberration > 0.0) {
        // Glass bends red a little less than blue: the three colors come from slightly
        // different places, more so toward the corners.
        float spread = post.chromaticAberration * r2;
        hdr.r = scene.sample(linearSampler, centered * bend * (1.0 - spread) * 0.5 + 0.5 + wet).r;
        hdr.g = scene.sample(linearSampler, lensUV).g;
        hdr.b = scene.sample(linearSampler, centered * bend * (1.0 + spread) * 0.5 + 0.5 + wet).b;
    } else {
        hdr = scene.sample(linearSampler, lensUV).rgb;
    }
    if (post.lensWater.w > 0.5) hdr = lensThroughWater(lensUV, hdr, post, scene, linearSampler, aspect);
    hdr *= 1.0 - rimShade;
    // Crisper edges: the picture less what is round it.
    if (post.sharpen > 0.0) {
        float2 texel = 1.0 / float2(scene.get_width(), scene.get_height());
        float3 around = scene.sample(linearSampler, lensUV + float2(texel.x, 0)).rgb + scene.sample(linearSampler, lensUV - float2(texel.x, 0)).rgb +
                        scene.sample(linearSampler, lensUV + float2(0, texel.y)).rgb + scene.sample(linearSampler, lensUV - float2(0, texel.y)).rgb;
        hdr = max(hdr + (hdr - around * 0.25) * post.sharpen * 1.5, float3(0.0));
    }
    float3 glare = bloom.sample(linearSampler, lensUV).rgb * post.bloomIntensity;
    hdr += glare;
    // Dirt on the glass shows only where bright light falls on it: smudges lit up round the sun and the lamps.
    if (post.lensDirt > 0.0) hdr += glare * lensDirt.sample(linearSampler, in.uv).rgb * (post.lensDirt * 6.0);
    hdr *= post.exposure;
    if (post.autoExposure != 0u) {
        hdr *= adaptedExposure.read(uint2(0, 0)).r * exp2(post.exposureCompensation);
    }

    float3 mapped;
    if (post.toneMapper == 0) {
        mapped = acesFilm(hdr);
    } else if (post.toneMapper == 1) {
        mapped = hdr / (hdr + 1.0);                     // Reinhard
    } else {
        mapped = saturate(hdr);
    }

    // Color: warmer or cooler, greener or more magenta, contrast round a middle gray, saturation.
    mapped *= float3(1.0 + post.temperature * 0.12, 1.0 - post.tint * 0.08, 1.0 - post.temperature * 0.12) * float3(1.0 + post.tint * 0.04, 1.0, 1.0 + post.tint * 0.04);
    if (post.contrast != 1.0) mapped = saturate(pow(max(mapped, float3(1e-5)) / 0.5, float3(post.contrast)) * 0.5);
    if (post.saturation != 1.0) mapped = saturate(mix(float3(dot(mapped, float3(0.2126, 0.7152, 0.0722))), mapped, post.saturation));

    if (post.lutSize >= 2.0 && post.lutAmount > 0.0) {
        mapped = mix(mapped, gradeWithTable(mapped, gradingTable, linearSampler, post.lutSize), post.lutAmount);
    }

    float2 fromCenter = in.uv - 0.5;
    mapped *= 1.0 - post.vignette * dot(fromCenter, fromCenter) * 2.0;

    if (post.grain > 0.0 || post.interference > 0.0) {
        float noise = videoNoise(in.position.xy, post.time) - 0.5;
        // Static washes the colors out as well as covering them.
        float gray = dot(mapped, float3(0.299, 0.587, 0.114));
        mapped = mix(mapped, float3(gray), post.interference * 0.7);
        mapped = saturate(mapped + noise * (post.grain + post.interference * 0.55));
    }
    return float4(mapped, 1.0);
}

// ---------------------------------------------------------------- screen-space ambient occlusion

static float3 viewPositionFromDepth(float2 uv, float depth, float4x4 inverseProjection) {
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 view = inverseProjection * clip;
    return view.xyz / view.w;
}

// A cheap, well-distributed per-pixel random number (Jorge Jimenez).
static float interleavedGradientNoise(float2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, float2(0.06711056, 0.00583715))));
}

// For every pixel, test a handful of points in the hemisphere above the surface and
// count how many are behind what the depth buffer shows. Works in view space.
// A picture already tone mapped (and made larger by MetalFX), put on the screen as it is.
fragment float4 fragment_copy(FullscreenOut in [[stage_in]],
                              texture2d<float> picture [[texture(0)]],
                              sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    return float4(picture.sample(linearSampler, in.uv).rgb, 1.0);
}

// A spot light's cookie, put into its slice of the cookies' array (Renderer::updateCookies): what
// passes of the light is the picture's color where it is opaque, none where it is clear.
fragment float4 fragment_cookie(FullscreenOut in [[stage_in]],
                                texture2d<float> picture [[texture(0)]],
                                sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float4 texel = picture.sample(linearSampler, in.uv);
    return float4(texel.rgb * texel.a, 1.0);
}

fragment float4 fragment_ssao(FullscreenOut in [[stage_in]],
                              constant AmbientOcclusionUniforms &ao [[buffer(MMBufferPass)]],
                              depth2d<float> depthTexture [[texture(0)]],
                              texture2d<float> normalTexture [[texture(1)]],
                              sampler pointSampler [[sampler(0)]]) {
    float depth = depthTexture.sample(pointSampler, in.uv);
    if (depth >= 1.0) {
        return float4(1.0);                             // sky: nothing to occlude
    }
    float3 position = viewPositionFromDepth(in.uv, depth, ao.inverseProjection);
    float3 normal = normalize(normalTexture.sample(pointSampler, in.uv).xyz * 2.0 - 1.0);

    // Rotate the sample pattern by a different angle at every pixel; the blur pass
    // then turns the resulting noise into smooth shading.
    float angle = interleavedGradientNoise(in.position.xy) * 2.0 * kPi;
    float3 randomDirection = float3(cos(angle), sin(angle), 0.0);
    float3 tangent = normalize(randomDirection - normal * dot(randomDirection, normal));
    float3 bitangent = cross(normal, tangent);

    const uint sampleCount = 12u;
    float occlusion = 0.0;
    for (uint i = 0u; i < sampleCount; i++) {
        float2 u = hammersley(i, sampleCount);
        float phi = 2.0 * kPi * u.y;
        float cosTheta = sqrt(1.0 - u.x * 0.9);         // stay a little above the surface
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float3 direction = tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + normal * cosTheta;
        float reach = mix(0.15, 1.0, u.x * u.x);        // more samples close to the pixel
        float3 samplePosition = position + direction * (ao.radius * reach);

        // Where is that point on screen, and how deep is the scene there?
        float4 clip = ao.projection * float4(samplePosition, 1.0);
        float2 sampleUV = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        float sceneDepth = depthTexture.sample(pointSampler, sampleUV);
        float sceneZ = viewPositionFromDepth(sampleUV, sceneDepth, ao.inverseProjection).z;

        // View space looks down -Z: a larger z is closer to the camera. Far-away
        // surfaces in front of the sample do not count (the range check).
        float rangeCheck = smoothstep(0.0, 1.0, ao.radius / max(abs(position.z - sceneZ), 0.0001));
        occlusion += (sceneZ >= samplePosition.z + ao.bias ? 1.0 : 0.0) * rangeCheck;
    }
    float visibility = 1.0 - ao.intensity * occlusion / float(sampleCount);
    return float4(pow(saturate(visibility), ao.power));
}

// A 4 x 4 box blur that removes the per-pixel noise of the pass above.
fragment float4 fragment_ssao_blur(FullscreenOut in [[stage_in]],
                                   constant AmbientOcclusionUniforms &ao [[buffer(MMBufferPass)]],
                                   texture2d<float> source [[texture(0)]],
                                   sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float sum = 0.0;
    for (int y = -2; y < 2; y++) {
        for (int x = -2; x < 2; x++) {
            sum += source.sample(linearSampler, in.uv + (float2(float(x), float(y)) + 0.5) * ao.texelSize).r;
        }
    }
    return float4(sum / 16.0);
}
