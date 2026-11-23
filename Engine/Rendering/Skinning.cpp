#include "Rendering/Skinning.hpp"

#include "Assets/Gltf.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

std::shared_ptr<SkinSource> SkinSource::build(const GltfModel& model) {
    auto source = std::make_shared<SkinSource>();
    source->nodeCount = model.nodes.size();
    size_t entries = model.nodes.size();
    for (const GltfSkin& skin : model.skins) {
        source->skinStarts.push_back(entries);
        entries += skin.joints.size();
    }
    source->paletteSize = entries;
    if (entries > 65535) return source;                 // more than four bytes' worth of joints: not this way
    source->reach.assign(entries, AABB{});

    // The vertices in the order GltfModel::assembled lays them down, each in the space of what
    // moves it: a skinned part's as modeled (its joints' matrices carry the inverse bind), any
    // other part's in its node's own space.
    MeshData modeled;
    for (size_t n = 0; n < model.nodes.size(); n++) {
        const GltfNode& node = model.nodes[n];
        if (node.mesh < 0 || size_t(node.mesh) >= model.meshes.size()) continue;
        const bool hasSkin = node.skin >= 0 && size_t(node.skin) < model.skins.size();
        const size_t jointCount = hasSkin ? model.skins[size_t(node.skin)].joints.size() : 0;
        for (const GltfPrimitive& primitive : model.meshes[size_t(node.mesh)]) {
            const uint32_t base = uint32_t(modeled.positions.size());
            const bool isSkinned = hasSkin && primitive.joints.size() == primitive.mesh.positions.size();
            for (size_t v = 0; v < primitive.mesh.positions.size(); v++) {
                const Vec3 position = primitive.mesh.positions[v];
                modeled.positions.push_back(position);
                modeled.normals.push_back(v < primitive.mesh.normals.size() ? primitive.mesh.normals[v] : Vec3{0, 1, 0});
                modeled.uvs.push_back(v < primitive.mesh.uvs.size() ? primitive.mesh.uvs[v] : Vec2{0, 0});
                std::array<uint16_t, 4> joints{0, 0, 0, 0};
                Vec4 weights{0, 0, 0, 0};
                if (isSkinned) {
                    for (int k = 0; k < 4; k++) {
                        const size_t joint = primitive.joints[v][size_t(k)];
                        const float weight = v < primitive.weights.size() ? primitive.weights[v][k] : 0.0f;
                        // As assembled() does: a joint the skin does not have moves nothing.
                        if (weight <= 0 || joint >= jointCount) continue;
                        joints[size_t(k)] = uint16_t(source->skinStarts[size_t(node.skin)] + joint);
                        weights[k] = weight;
                    }
                } else {
                    joints[0] = uint16_t(n);
                    weights[0] = 1.0f;
                }
                for (int k = 0; k < 4; k++) {
                    if (weights[k] > 0) source->reach[joints[size_t(k)]].expand(position);
                }
                source->joints.push_back(joints);
                source->weights.push_back(weights);
            }
            for (uint32_t index : primitive.mesh.indices) modeled.indices.push_back(base + index);
        }
    }
    // The tangents follow the surface and its pictures: worked out once, as modeled, and turned
    // with each vertex from then on (assembled() works them out again every frame).
    modeled.computeTangents();
    source->rest = modeled.interleaved();
    source->vertexCount = uint32_t(modeled.positions.size());
    return source;
}

void SkinSource::palette(const GltfModel& model, const GltfPose& pose, std::vector<Mat4>& out) const {
    out = model.globalMatrices(pose);
    out.resize(paletteSize, identity());
    for (size_t s = 0; s < model.skins.size() && s < skinStarts.size(); s++) {
        const GltfSkin& skin = model.skins[s];
        for (size_t j = 0; j < skin.joints.size(); j++) {
            const int joint = skin.joints[j];
            const Mat4 place = joint >= 0 && size_t(joint) < model.nodes.size() ? out[size_t(joint)] : identity();
            out[skinStarts[s] + j] = place * (j < skin.inverseBind.size() ? skin.inverseBind[j] : identity());
        }
    }
}

AABB SkinSource::bounds(const std::vector<Mat4>& palette) const {
    AABB box;
    for (size_t i = 0; i < reach.size() && i < palette.size(); i++) {
        if (!reach[i].isEmpty()) box.expand(reach[i].transformed(palette[i]));
    }
    return box;
}

void SkinSource::poseOnCpu(const std::vector<Mat4>& palette, std::vector<float>& posed) const {
    posed.assign(rest.size(), 0.0f);
    for (uint32_t i = 0; i < vertexCount; i++) {
        const float* from = &rest[size_t(i) * MeshData::kFloatsPerVertex];
        float* to = &posed[size_t(i) * MeshData::kFloatsPerVertex];
        const Vec3 position{from[0], from[1], from[2]}, normal{from[3], from[4], from[5]}, tangent{from[6], from[7], from[8]};
        Vec3 p{0, 0, 0}, n{0, 0, 0}, t{0, 0, 0};
        for (int k = 0; k < 4; k++) {
            const float w = weights[i][k];
            if (w <= 0) continue;
            const Mat4& m = palette[joints[i][size_t(k)]];
            p += transformPoint(m, position) * w;
            n += transformDirection(m, normal) * w;
            t += transformDirection(m, tangent) * w;
        }
        n = simd::dot(n, n) > 1e-12f ? simd::normalize(n) : normal;
        t = simd::dot(t, t) > 1e-12f ? simd::normalize(t) : tangent;
        const float out[12] = {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z, from[9], from[10], from[11]};
        std::copy(out, out + 12, to);
    }
}

}  // namespace molten
