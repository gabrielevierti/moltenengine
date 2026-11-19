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

vertex MeshVertexOut vertex_mesh(MeshVertexIn in [[stage_in]],
                                 uint instanceID [[instance_id]],
                                 constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                 constant InstanceData *instances [[buffer(MMBufferInstances)]]) {
    InstanceData instance = instances[instanceID];
    float4 world = instance.model * float4(in.position, 1.0);

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
