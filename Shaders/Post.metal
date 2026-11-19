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

// The last pass: the lens, scene plus bloom, exposure, tone mapping, vignette, and the
// flaws of a video link. The result is linear; the drawable has an sRGB format, so
// Metal applies the gamma curve itself.
fragment float4 fragment_tonemap(FullscreenOut in [[stage_in]],
                                 constant PostUniforms &post [[buffer(MMBufferPass)]],
                                 texture2d<float> scene [[texture(0)]],
                                 texture2d<float> bloom [[texture(1)]],
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
    float2 lensUV = centered * bend * 0.5 + 0.5;

    float3 hdr;
    if (post.chromaticAberration > 0.0) {
        // Glass bends red a little less than blue: the three colors come from slightly
        // different places, more so toward the corners.
        float spread = post.chromaticAberration * r2;
        hdr.r = scene.sample(linearSampler, centered * bend * (1.0 - spread) * 0.5 + 0.5).r;
        hdr.g = scene.sample(linearSampler, lensUV).g;
        hdr.b = scene.sample(linearSampler, centered * bend * (1.0 + spread) * 0.5 + 0.5).b;
    } else {
        hdr = scene.sample(linearSampler, lensUV).rgb;
    }
    hdr += bloom.sample(linearSampler, lensUV).rgb * post.bloomIntensity;
    hdr *= post.exposure;

    float3 mapped;
    if (post.toneMapper == 0) {
        mapped = acesFilm(hdr);
    } else if (post.toneMapper == 1) {
        mapped = hdr / (hdr + 1.0);                     // Reinhard
    } else {
        mapped = saturate(hdr);
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
