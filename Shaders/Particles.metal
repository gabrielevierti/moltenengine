// Particles.metal
// Each particle is a square that always faces the camera (a "billboard"), drawn as
// one instance of a four-vertex triangle strip.

#include "Common.metal"

struct ParticleOut {
    float4 position [[position]];
    float2 corner;                  // -1..1 across the square
    float4 color;
    float isStreak;
};

vertex ParticleOut vertex_particle(uint vertexID [[vertex_id]],
                                   uint instanceID [[instance_id]],
                                   constant ParticleInstance *particles [[buffer(MMBufferInstances)]],
                                   constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    ParticleInstance particle = particles[instanceID];
    float2 corner = float2(float(vertexID & 1), float(vertexID >> 1)) * 2.0 - 1.0;

    // The camera's right and up axes in world space are the first two rows of the view matrix.
    float3 right = float3(pass.view[0][0], pass.view[1][0], pass.view[2][0]);
    float3 up = float3(pass.view[0][1], pass.view[1][1], pass.view[2][1]);
    float half = particle.positionAndSize.w * 0.5;
    float3 world = particle.positionAndSize.xyz + (right * corner.x + up * corner.y) * half;

    // A streak: as long as the way it goes in `stretch` seconds, its width across the view.
    float3 velocity = particle.velocityAndStretch.xyz;
    float stretch = particle.velocityAndStretch.w;
    float speed = length(velocity);
    ParticleOut out;
    out.isStreak = 0.0;
    if (stretch > 0.0 && speed > 0.001) {
        float3 along = velocity / speed;
        float3 eye = float3(pass.cameraPosition.xyz);
        float3 toEye = normalize(eye - particle.positionAndSize.xyz);
        float3 across = cross(along, toEye);
        float acrossLength = length(across);
        across = acrossLength > 0.001 ? across / acrossLength : right;
        world = particle.positionAndSize.xyz + across * (corner.x * half) + along * (corner.y * (half + speed * stretch * 0.5));
        out.isStreak = 1.0;
    }
    out.position = pass.viewProjection * float4(world, 1.0);
    out.corner = corner;
    out.color = particle.color;
    return out;
}

// A soft round dot, or a streak soft across and fading toward its ends. With additive
// blending, colors above 1 make the bloom pick it up.
fragment float4 fragment_particle(ParticleOut in [[stage_in]]) {
    float falloff = in.isStreak > 0.5 ? saturate(1.0 - in.corner.x * in.corner.x) * saturate(1.0 - abs(in.corner.y))
                                      : saturate(1.0 - dot(in.corner, in.corner));
    return float4(in.color.rgb, in.color.a * falloff * falloff);
}
