// Particles.metal
// Each particle is a square drawn as one instance of a four-vertex triangle strip: facing the
// camera (a billboard), stretched along its way, lying flat, or standing upright. It may be lit
// by the sun and the sky like smoke, and fade where it meets a surface instead of cutting it.

#include "Common.metal"

struct ParticleOut {
    float4 position [[position]];
    float2 corner;                  // -1..1 across the square
    float4 color;
    float isStreak;
    float2 uv;                      // on its picture, turned, in its frame of the sheet
    float hasPicture;
    float3 normal;                  // bulging toward the camera, as a puff of smoke is round
    float3 toEye;
    float viewDepth;                // meters in front of the camera
    float4 shading;                 // lit, softness, render mode
};

vertex ParticleOut vertex_particle(uint vertexID [[vertex_id]],
                                   uint instanceID [[instance_id]],
                                   constant ParticleInstance *particles [[buffer(MMBufferInstances)]],
                                   constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    ParticleInstance particle = particles[instanceID];
    float2 corner = float2(float(vertexID & 1), float(vertexID >> 1)) * 2.0 - 1.0;
    float3 center = particle.positionAndSize.xyz;
    float3 eye = float3(pass.cameraPosition.xyz);
    float3 toEye = normalize(eye - center);
    int mode = int(particle.shading.z + 0.5);

    // The camera's right and up axes in world space are the first two rows of the view matrix.
    float3 right = float3(pass.view[0][0], pass.view[1][0], pass.view[2][0]);
    float3 up = float3(pass.view[0][1], pass.view[1][1], pass.view[2][1]);
    float3 facing = toEye;
    if (mode == 2) {
        // Lying flat: a ripple, a glow on the ground.
        right = float3(1.0, 0.0, 0.0);
        up = float3(0.0, 0.0, 1.0);
        facing = float3(0.0, 1.0, 0.0);
    } else if (mode == 3) {
        // Upright, turned to the camera round its own up axis: grass, a flame on a candle.
        float3 level = float3(toEye.x, 0.0, toEye.z);
        level = length(level) > 1e-4 ? normalize(level) : float3(0.0, 0.0, 1.0);
        up = float3(0.0, 1.0, 0.0);
        right = normalize(cross(up, level));
        facing = level;
    }
    float radius = particle.positionAndSize.w * 0.5;
    float3 world = center + (right * corner.x + up * corner.y) * radius;

    // A streak: as long as the way it goes in `stretch` seconds, its width across the view.
    float3 velocity = particle.velocityAndStretch.xyz;
    float stretch = particle.velocityAndStretch.w;
    float speed = length(velocity);
    ParticleOut out;
    out.isStreak = 0.0;
    if (stretch > 0.0 && speed > 0.001) {
        float3 along = velocity / speed;
        float3 across = cross(along, toEye);
        float acrossLength = length(across);
        across = acrossLength > 0.001 ? across / acrossLength : right;
        world = center + across * (corner.x * radius) + along * (corner.y * (radius + speed * stretch * 0.5));
        out.isStreak = 1.0;
    }
    out.position = pass.viewProjection * float4(world, 1.0);
    out.viewDepth = -(pass.view * float4(world, 1.0)).z;
    out.corner = corner;
    out.color = particle.color;
    out.normal = normalize(right * (corner.x * 0.75) + up * (corner.y * 0.75) + facing * 0.65);
    out.toEye = toEye;
    out.shading = particle.shading;
    // Its picture: turned by its rotation, and the frame of the sheet it has come to.
    float4 look = particle.look;
    out.hasPicture = look.z > 0.5 ? 1.0 : 0.0;
    float c = cos(look.x), s = sin(look.x);
    float2 turned = out.isStreak > 0.5 ? corner : float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
    float2 inFrame = turned * float2(0.5, -0.5) + 0.5;
    float columns = max(look.z, 1.0), rows = max(look.w, 1.0);
    float sheetFrame = look.y;
    float2 cell = float2(fmod(sheetFrame, columns), floor(sheetFrame / columns));
    out.uv = (cell + clamp(inFrame, 0.0, 1.0)) / float2(columns, rows);
    return out;
}

// Its picture tinted by its color, or a soft round dot (a streak soft across and fading toward
// its ends). With additive blending, colors above 1 make the bloom pick it up.
fragment float4 fragment_particle(ParticleOut in [[stage_in]],
                                  constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                  constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                  texture2d<float> picture [[texture(0)]],
                                  texturecube<float> irradianceMap [[texture(MMTextureIrradiance)]],
                                  depth2d<float> sceneDepth [[texture(MMTextureSceneDepth)]],
                                  sampler linearSampler [[sampler(MMSamplerLinearClamp)]]) {
    float4 result;
    if (in.hasPicture > 0.5) {
        // Where the picture is clear, so is the particle.
        float4 texel = picture.sample(linearSampler, in.uv);
        result = float4(in.color.rgb * texel.rgb, in.color.a * texel.a);
    } else {
        float falloff = in.isStreak > 0.5 ? saturate(1.0 - in.corner.x * in.corner.x) * saturate(1.0 - abs(in.corner.y))
                                          : saturate(1.0 - dot(in.corner, in.corner));
        result = float4(in.color.rgb, in.color.a * falloff * falloff);
    }

    // Lit like smoke: the sky all round, the sun wrapping round its side, and brighter still
    // when the sun is behind it and shines through (light scattered forward, as in a cloud).
    float lit = in.shading.x;
    if (lit > 0.0) {
        float3 n = normalize(in.normal);
        float3 toSun = -normalize(frame.sunDirection);
        float wrapped = saturate((dot(n, toSun) + 0.6) / 1.6);
        float g = 0.6;
        float cosine = dot(normalize(frame.sunDirection), normalize(in.toEye));
        float through = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosine, 1e-3), 1.5) * 0.08;
        float3 sky = irradianceMap.sample(linearSampler, n).rgb * frame.environmentIntensity;
        float3 sun = frame.sunColor * (wrapped * 0.85 + through);
        result.rgb = mix(result.rgb, result.rgb * (sky + sun), lit);
    }

    // Soft: fading out as it nears the surface behind it, instead of a hard line where it cuts in.
    float softness = in.shading.y;
    if (softness > 0.0 && frame.sceneDepthReady > 0.5 && !is_null_texture(sceneDepth)) {
        uint2 pixel = uint2(in.position.xy);
        if (pixel.x < sceneDepth.get_width() && pixel.y < sceneDepth.get_height()) {
            float stored = sceneDepth.read(pixel);
            if (stored < 1.0) {
                float behind = pass.projection[3][2] / (stored + pass.projection[2][2]);
                result.a *= saturate((behind - in.viewDepth) / softness);
            }
        }
    }
    // And gone before it touches the lens: a big puff right at the camera fills the screen otherwise.
    result.a *= saturate((in.viewDepth - pass.nearPlane) / 0.5);
    return result;
}
