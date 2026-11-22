// Mesh.metal
// The vertex stage for everything made of meshes.
//
// Every mesh draw is an instanced draw, even when there is a single instance: the
// model matrix always comes from the InstanceData buffer, indexed by instance_id.
// That way one code path serves a lone object and a thousand copies alike.

#include "Common.metal"

struct MeshVertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float4 tangent  [[attribute(2)]];       // w is the handedness of the bitangent
    float2 uv       [[attribute(3)]];
};

struct MeshVertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
    float4 tangent;
    float2 uv;
    float4 tint;
    float viewDepth;                        // distance in front of the camera
};

// How far the wind moves a point of a swaying thing: along the wind, the more the higher it is
// above the model's base, in waves that cross the field and gusts that come and go. Each copy
// (each tuft of grass, each tree) starts its sway at its own moment.
static float3 windSway(float height, float3 base, float3 world, constant PassUniforms &pass, float sway) {
    float3 wind = float3(pass.wind.x, 0.0, pass.wind.z);
    float speed = length(wind);
    if (speed < 0.001 || height <= 0.0) {
        return float3(0.0);
    }
    float3 along = wind / speed;
    float t = pass.time;
    float phase = dot(base.xz, float2(0.37, 0.23)) + dot(world.xz, along.xz) * 0.15;
    float wave = 0.65 + 0.35 * sin(t * (1.1 + speed * 0.12) - phase * 1.7);
    float gust = 1.0 + pass.windGust * (sin(t * 0.43 + phase * 0.21) * 0.5 + sin(t * 0.71 - phase * 0.13) * 0.5);
    float flutter = sin(t * 4.3 + phase * 5.1) * 0.08;
    float bend = sway * height * speed * 0.04 * (wave * gust + flutter);
    // Bent over, the tip comes down a little: it does not stretch.
    return along * bend - float3(0.0, bend * bend / max(height * 2.0, 0.05), 0.0);
}

vertex MeshVertexOut vertex_mesh(MeshVertexIn in [[stage_in]],
                                 uint instanceID [[instance_id]],
                                 constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                 constant InstanceData *instances [[buffer(MMBufferInstances)]],
                                 constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                 texture2d<float> ocean [[texture(MMTextureNormal)]]) {
    InstanceData instance = instances[instanceID];
    float4 world = instance.model * float4(in.position, 1.0);
    if (material.sway > 0.0) {
        world.xyz += windSway(in.position.y, instance.model[3].xyz, world.xyz, pass, material.sway);
    }
    // Water rises and falls with its waves: the sea's heights (its Fourier transform), or a few
    // crossing swells where there is none. Fading out far away, where they cannot be seen.
    if (material.water > 0.0) {
        float fade = 1.0 - saturate(length(world.xyz - pass.cameraPosition) / 1500.0);
        float rise = 0.0;
        if (material.oceanScale > 0.0) {
            constexpr sampler wrap(filter::linear, address::repeat);
            float2 at = (world.xz + material.oceanOffset) * material.oceanScale;
            rise = ocean.sample(wrap, at, level(0.0)).w * 0.6 + ocean.sample(wrap, at * 3.7 + 0.31, level(0.0)).w * 0.15;
        } else {
            float2 p = world.xz * material.water;
            float t = pass.time;
            rise = sin(dot(p, float2(0.8, 0.6)) * 0.9 + t * 0.9) * 0.35 + sin(dot(p, float2(-0.5, 0.86)) * 2.3 + t * 1.5) * 0.12 +
                   sin(dot(p, float2(0.3, -0.95)) * 5.7 + t * 2.4) * 0.04;
        }
        world.y += rise * material.waveHeight * 0.5 * fade;
    }

    MeshVertexOut out;
    out.position = pass.viewProjection * world;
    out.worldPosition = world.xyz;
    out.normal = (instance.normalMatrix * float4(in.normal, 0.0)).xyz;
    out.tangent = float4((instance.model * float4(in.tangent.xyz, 0.0)).xyz, in.tangent.w);
    out.uv = in.uv;
    out.tint = instance.tint;
    out.viewDepth = -(pass.view * world).z;
    return out;
}

// Depth-only passes (the shadow maps) need nothing but the position.
struct DepthVertexIn {
    float3 position [[attribute(0)]];
};

vertex float4 vertex_mesh_depth(DepthVertexIn in [[stage_in]],
                                uint instanceID [[instance_id]],
                                constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                constant InstanceData *instances [[buffer(MMBufferInstances)]]) {
    return pass.viewProjection * (instances[instanceID].model * float4(in.position, 1.0));
}

// ---------------------------------------------------------------- the sun's shadow maps
#ifdef MOLTEN_EXTRAS
//
// Every cascade is drawn in the same pass. The shadow map is an array of textures, and each
// copy of a mesh says which of them it goes into (render_target_array_index). A thing whose
// shadow reaches three cascades is one draw of three copies, where it was three draws in
// three passes. Which copies there are is decided on the CPU (Cascades::reaches).

struct ShadowVertexOut {
    float4 position [[position]];
    uint cascade [[render_target_array_index]];
};

vertex ShadowVertexOut vertex_shadow(DepthVertexIn in [[stage_in]],
                                     uint copyID [[instance_id]],
                                     constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                     constant InstanceData *instances [[buffer(MMBufferInstances)]],
                                     constant ShadowCopy *copies [[buffer(MMBufferShadowCopies)]]) {
    ShadowCopy copy = copies[copyID];
    ShadowVertexOut out;
    out.position = frame.cascadeViewProjection[copy.cascade] * (instances[copy.instance].model * float4(in.position, 1.0));
    out.cascade = copy.cascade;
    return out;
}

// Materials with holes cut in them, and those the wind sways: the same, with what the holes
// and the sway need, so that the shadow of a tree moves with the tree.
struct ShadowDetailVertexOut {
    float4 position [[position]];
    float2 uv;
    float alpha;                            // of the instance's tint
    uint cascade [[render_target_array_index]];
};

vertex ShadowDetailVertexOut vertex_shadow_detailed(MeshVertexIn in [[stage_in]],
                                                    uint copyID [[instance_id]],
                                                    constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                                    constant FrameUniforms &frame [[buffer(MMBufferFrame)]],
                                                    constant InstanceData *instances [[buffer(MMBufferInstances)]],
                                                    constant MaterialUniforms &material [[buffer(MMBufferMaterial)]],
                                                    constant ShadowCopy *copies [[buffer(MMBufferShadowCopies)]]) {
    ShadowCopy copy = copies[copyID];
    InstanceData instance = instances[copy.instance];
    float4 world = instance.model * float4(in.position, 1.0);
    if (material.sway > 0.0) {
        world.xyz += windSway(in.position.y, instance.model[3].xyz, world.xyz, pass, material.sway);
    }
    ShadowDetailVertexOut out;
    out.position = frame.cascadeViewProjection[copy.cascade] * world;
    out.uv = in.uv;
    out.alpha = instance.tint.a;
    out.cascade = copy.cascade;
    return out;
}
#endif
