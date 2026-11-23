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
    // The thing's own axes, for what sticks to it (the rain on it, Forward.metal: wetten): where
    // the point is in them, in meters, and the axes themselves in the world.
    float3 localPosition;
    float3 axisX;
    float3 axisY;
    float3 previousWorldPosition;           // where this point was a frame ago (as worldPosition): how fast it moves
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

// ---------------------------------------------------------------- water's waves
//
// What the wind raises on open water: eight trains of waves of different lengths, each running
// a little aside of the wind, each at the speed water waves of its length travel. Under a
// crest the water is drawn toward it, which makes crests sharp and troughs wide ("Gerstner"
// waves). The numbers are in ShaderTypes.h, shared with Engine/Water/Waves.cpp, which floats
// things on the same sea: what is changed here is changed there.

struct WaterSwell {
    float3 moved;       // how far the water that rests at the place is moved: up, and toward the crests
    float2 slope;       // the surface's slope there: the height's change along x and along z
    float pinched;      // 1 where the surface is as it rests, toward 0 where it is drawn together
    float crowding;     // how much the waves crowd the water together there, of the most they can: 1 where all their crests meet
    float crest;        // the height there over the sea's: -1 in a deep trough, 1 on a high crest
};

// The way the wind blows over the ground; as from the west and a little south when there is none.
static float2 waterWindDirection(float3 wind) {
    float2 along = float2(wind.x, wind.z);
    float speed = length(along);
    if (speed > 0.01) {
        return along / speed;
    }
    return float2(0.958, 0.287);
}

// The longest wave (x) and the height of the sea (y, the average of its highest third, trough
// to crest), in meters, for a wind. `fineness` is 1 over the material's Wave Size.
static float2 waterSeaState(float3 wind, float fineness, float heightScale) {
    float speed = max(length(float2(wind.x, wind.z)), MM_WAVE_LEAST_WIND);
    float longest = clamp(MM_WAVE_LENGTH_PER_WIND * speed * speed, 1.5, 150.0) / max(fineness, 0.01);
    float high = clamp(MM_WAVE_HEIGHT_PER_WIND * speed * speed, 0.02, 4.0) * max(heightScale, 0.0);
    return float2(longest, min(high, longest * MM_WAVE_STEEPEST));
}

// The sea at the place where a bit of water rests (before the waves move it), at a time.
// `cell` is how far apart the points are that will carry it (the squares of a sheet; 0 for a
// pixel, which carries everything): a wave too short for them is left out, not drawn wrong.
static WaterSwell waterSwell(float2 rest, float time, float3 wind, float fineness, float heightScale, float cell) {
    const float lengths[MM_WAVE_TRAINS] = {MM_WAVE_LENGTHS};
    const float shares[MM_WAVE_TRAINS] = {MM_WAVE_SHARES};
    const float turnCos[MM_WAVE_TRAINS] = {MM_WAVE_TURN_COS};
    const float turnSin[MM_WAVE_TRAINS] = {MM_WAVE_TURN_SIN};
    float2 state = waterSeaState(wind, fineness, heightScale);
    float2 along = waterWindDirection(wind);
    float2 aside = float2(0.0);
    float2 slope = float2(0.0);
    float height = 0.0;
    float sxx = 0.0, szz = 0.0, sxz = 0.0, most = 0.0;
    for (int i = 0; i < MM_WAVE_TRAINS; i++) {
        float waveLength = state.x * lengths[i];
        float carried = cell > 0.0 ? saturate(waveLength / (cell * 2.0) - 1.0) : 1.0;
        float number = 6.2831853 / waveLength;
        float amplitude = state.y * shares[i] * carried;
        float2 d = float2(along.x * turnCos[i] - along.y * turnSin[i], along.x * turnSin[i] + along.y * turnCos[i]);
        float phase = number * dot(d, rest) - sqrt(9.81 * number) * time;
        float s = sin(phase), c = cos(phase);
        height += amplitude * c;
        aside -= d * (amplitude * MM_WAVE_PINCH * s);
        slope -= d * (amplitude * number * s);
        float drawn = amplitude * number * MM_WAVE_PINCH * c;
        most += amplitude * number * MM_WAVE_PINCH;
        sxx += drawn * d.x * d.x;
        szz += drawn * d.y * d.y;
        sxz += drawn * d.x * d.y;
    }
    WaterSwell swell;
    swell.moved = float3(aside.x, height, aside.y);
    swell.pinched = (1.0 - sxx) * (1.0 - szz) - sxz * sxz;
    // The slope above is over the water at rest. Where it is drawn together the same rise is over less ground: steeper.
    float steeper = 1.0 / max(swell.pinched, 0.2);
    swell.slope = float2((1.0 - szz) * slope.x + sxz * slope.y, sxz * slope.x + (1.0 - sxx) * slope.y) * steeper;
    swell.crowding = (sxx + szz) / max(most, 0.0001);
    swell.crest = height / max(state.y * 0.5, 0.01);
    return swell;
}

static float waterHash(float2 p) {
    p = fract(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Random values at the corners of each square, blended smoothly between them.
static float waterNoise(float2 p) {
    float2 cell = floor(p), f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float top = mix(waterHash(cell), waterHash(cell + float2(1.0, 0.0)), u.x);
    float bottom = mix(waterHash(cell + float2(0.0, 1.0)), waterHash(cell + float2(1.0, 1.0)), u.x);
    return mix(top, bottom, u.y);
}

// The small waves no sheet's points carry, for a pixel: chop on the swell (four more trains,
// shorter than its shortest) and ripples on the chop (noise that drifts with the wind). Their
// slope alone. `footprint` is how much water the pixel covers, in meters: what is finer than
// a pixel is left out, since it could only flicker.
static float2 waterRipples(float2 place, float time, float3 wind, float fineness, float heightScale, float footprint) {
    const float lengths[4] = {0.072, 0.052, 0.037, 0.027};
    // In a light air the trains would all be as short as ripples get: each has a shortest of its
    // own, none a multiple of another, or four of one length crossing make a grid.
    const float shortest[4] = {0.43, 0.34, 0.27, 0.21};
    const float turnCos[4] = {0.88295, 0.68200, 0.48481, 0.95630};
    const float turnSin[4] = {-0.46947, 0.73135, -0.87462, 0.29237};
    float2 along = waterWindDirection(wind);
    float speed = max(length(float2(wind.x, wind.z)), MM_WAVE_LEAST_WIND);
    float longest = waterSeaState(wind, fineness, 1.0).x;
    float strength = clamp(speed / 7.0, 0.35, 1.6) * min(max(heightScale, 0.0), 2.0);
    float2 slope = float2(0.0);
    // Their crests are not straight: bent this way and that over a meter or two, as real ripples are.
    float2 bend = float2(waterNoise(place * 0.47 + float2(3.7, 1.3)), waterNoise(place * 0.47 + float2(-6.1, 8.9))) - 0.5;
    for (int i = 0; i < 4; i++) {
        float waveLength = max(longest * lengths[i], shortest[i]);
        float seen = saturate(waveLength / max(footprint * 3.0, 0.0001) - 0.6);
        float number = 6.2831853 / waveLength;
        float2 d = float2(along.x * turnCos[i] - along.y * turnSin[i], along.x * turnSin[i] + along.y * turnCos[i]);
        float phase = number * dot(d, place) - sqrt(9.81 * number) * time + dot(bend, i % 2 == 0 ? float2(5.1, 2.3) : float2(-2.7, 4.9));
        slope -= d * (0.055 * strength * seen * sin(phase));
    }
    // Ripples: two sizes of noise carried downwind, their slope taken by looking a little to each side.
    float2 drift = along * time;
    for (int j = 0; j < 2; j++) {
        float size = (j == 0 ? 0.9 : 0.27) / max(fineness, 0.25);            // meters across one ripple
        float seen = saturate(size / max(footprint * 3.0, 0.0001) - 0.6);
        if (seen > 0.0) {
            float2 p = (place - drift * (j == 0 ? 0.55 : 0.9)) / size + float2(j == 0 ? 0.0 : 31.7, j == 0 ? 0.0 : 12.3);
            float here = waterNoise(p);
            float2 change = float2(waterNoise(p + float2(0.35, 0.0)) - here, waterNoise(p + float2(0.0, 0.35)) - here);
            slope += change * ((j == 0 ? 0.30 : 0.20) * strength * seen);
        }
    }
    return slope;
}

// How white the water is where its waves break: where their crests meet, the more of them the
// harder it blows. None in a calm or a breeze; from a fresh breeze (about 5 meters a second) the first white
// crests; in a gale streaks of them. `crowding` is WaterSwell's.
// How crowded a crest has to be to break, at a wind of `speed` meters a second. Set so that the
// share of a sea that is white follows what is measured at sea (Monahan: about 0.2% at 6 m/s, 2%
// at 12, 7% at 18): the waves are the same shape at every wind, only larger, so it is the
// threshold that the wind lowers. Below 5 m/s none break.
static float waterBreakingFrom(float speed) {
    return clamp(0.9 - 0.034 * speed, 0.05, 0.95);
}

static float waterBreaking(float crowding, float3 wind) {
    float speed = length(float2(wind.x, wind.z));
    return saturate((crowding - waterBreakingFrom(speed) - 0.1) * 7.0) * saturate((speed - 5.0) / 2.0);
}

vertex MeshVertexOut vertex_mesh(MeshVertexIn in [[stage_in]],
                                 uint instanceID [[instance_id]],
                                 constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                 constant InstanceData *instances [[buffer(MMBufferInstances)]],
                                 constant MaterialUniforms &material [[buffer(MMBufferMaterial)]]) {
    InstanceData instance = instances[instanceID];
    float4 world = instance.model * float4(in.position, 1.0);
    // A frame ago: from where the thing was then (still, if it was not drawn then).
    float3 before = instance.previousModel[3][3] != 0.0 ? (instance.previousModel * float4(in.position, 1.0)).xyz : world.xyz;
    float3 axisX = normalize(instance.model[0].xyz), axisY = normalize(instance.model[1].xyz), axisZ = normalize(instance.model[2].xyz);
    float3 fromOrigin = world.xyz - instance.model[3].xyz;
    float3 local = float3(dot(fromOrigin, axisX), dot(fromOrigin, axisY), dot(fromOrigin, axisZ));
    if (material.sway > 0.0) {
        float3 swayed = windSway(in.position.y, instance.model[3].xyz, world.xyz, pass, material.sway);
        world.xyz += swayed;
        before += swayed;
    }

    MeshVertexOut out;
    out.localPosition = local;
    out.axisX = axisX;
    out.axisY = axisY;
    out.previousWorldPosition = before;
    out.position = pass.viewProjection * world;
    out.worldPosition = world.xyz;
    out.normal = (instance.normalMatrix * float4(in.normal, 0.0)).xyz;
    out.tangent = float4((instance.model * float4(in.tangent.xyz, 0.0)).xyz, in.tangent.w);
    out.uv = in.uv;
    out.tint = instance.tint;
    out.viewDepth = -(pass.view * world).z;
    return out;
}

// A sheet of water (the flat square mesh, with a water material) is not drawn with its own
// four corners. A grid laid around the eye stands in for it: fine nearby and coarser the
// farther off, some kilometers across (Engine/Water/Waves.cpp makes it; each point's `uv.x`
// is how far apart the points are there). Here every point of it is put on the sheet (those
// beyond its edges on the edge: the sheet ends where its own square does), and moved by the
// swell. At its rim the water is still, so that it meets its shore.
vertex MeshVertexOut vertex_water(MeshVertexIn in [[stage_in]],
                                  uint instanceID [[instance_id]],
                                  constant PassUniforms &pass [[buffer(MMBufferPass)]],
                                  constant InstanceData *instances [[buffer(MMBufferInstances)]],
                                  constant MaterialUniforms &material [[buffer(MMBufferMaterial)]]) {
    InstanceData instance = instances[instanceID];
    float3 across = instance.model[0].xyz, along = instance.model[2].xyz, middle = instance.model[3].xyz;
    float2 around = float2(pass.cameraPosition.x, pass.cameraPosition.z) + float2(in.position.x, in.position.z);
    float3 from = float3(around.x, middle.y, around.y) - middle;
    float2 local = float2(dot(from, across) / max(dot(across, across), 0.000001), dot(from, along) / max(dot(along, along), 0.000001));
    local = clamp(local, float2(-0.5), float2(0.5));
    float3 rest = middle + across * local.x + along * local.y;
    float2 toRim = (float2(0.5) - abs(local)) * float2(length(across), length(along));
    float calm = saturate(min(toRim.x, toRim.y) / MM_WATER_RIM);
    WaterSwell swell = waterSwell(float2(rest.x, rest.z), pass.time, pass.wind, material.water, material.waveHeight, in.uv.x);
    float4 world = float4(rest + swell.moved * calm, 1.0);

    MeshVertexOut out;
    out.position = pass.viewProjection * world;
    out.worldPosition = world.xyz;
    out.normal = float3(0.0, 1.0, 0.0);
    out.tangent = float4(1.0, 0.0, 0.0, 2.0);       // w above 1 says: `uv` is where this water rests
    out.uv = float2(rest.x, rest.z);
    out.tint = instance.tint;
    out.localPosition = world.xyz;
    out.axisX = float3(1.0, 0.0, 0.0);
    out.axisY = float3(0.0, 1.0, 0.0);
    out.previousWorldPosition = world.xyz;
    out.viewDepth = -(pass.view * world).z;
    return out;
}

// Where the water of a pixel rests: a sheet's points say (see vertex_water); other water is not moved.
static float2 waterRestingPlace(MeshVertexOut in) {
    return in.tangent.w > 1.5 ? in.uv : float2(in.worldPosition.x, in.worldPosition.z);
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

// ---------------------------------------------------------------- skinning

// A model with bones, posed on the GPU before anything is drawn (Rendering/Skinning.hpp): each
// vertex as it was modeled, moved by up to four matrices of the palette (its bones', or its
// node's for a part that is not skinned) and blended by how much it follows each, written into
// the mesh's own vertices. Twelve floats a vertex, as MeshData::interleaved lays them out:
// position, normal, tangent and its handedness, uv.
kernel void skin_vertices(device const float* rest [[buffer(0)]],
                          device const ushort4* joints [[buffer(1)]],
                          device const float4* weights [[buffer(2)]],
                          constant float4x4* palette [[buffer(3)]],
                          device float* posed [[buffer(4)]],
                          constant uint& count [[buffer(5)]],
                          uint index [[thread_position_in_grid]]) {
    if (index >= count) return;
    device const float* from = rest + index * 12;
    const float4 position = float4(from[0], from[1], from[2], 1.0);
    const float4 normal = float4(from[3], from[4], from[5], 0.0);
    const float4 tangent = float4(from[6], from[7], from[8], 0.0);
    const ushort4 joint = joints[index];
    const float4 weight = weights[index];
    float3 p = float3(0.0);
    float3 n = float3(0.0);
    float3 t = float3(0.0);
    for (int k = 0; k < 4; k++) {
        if (weight[k] <= 0.0) continue;
        const float4x4 m = palette[joint[k]];
        p += (m * position).xyz * weight[k];
        n += (m * normal).xyz * weight[k];
        t += (m * tangent).xyz * weight[k];
    }
    n = dot(n, n) > 1e-12 ? normalize(n) : normal.xyz;
    t = dot(t, t) > 1e-12 ? normalize(t) : tangent.xyz;
    device float* to = posed + index * 12;
    to[0] = p.x;  to[1] = p.y;  to[2] = p.z;
    to[3] = n.x;  to[4] = n.y;  to[5] = n.z;
    to[6] = t.x;  to[7] = t.y;  to[8] = t.z;  to[9] = from[9];
    to[10] = from[10];  to[11] = from[11];
}
