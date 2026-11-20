#include "Assets/Gltf.hpp"

#include "Core/Json.hpp"

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace molten {

namespace {

std::optional<std::vector<uint8_t>> readBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> decodeBase64(const std::string& text) {
    std::vector<uint8_t> out;
    unsigned buffer = 0;
    int bits = 0;
    for (char c : text) {
        int value;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+' || c == '-') value = 62;
        else if (c == '/' || c == '_') value = 63;
        else continue;                                          // padding and line breaks
        buffer = (buffer << 6) | unsigned(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

/// The numbers of an accessor, as floats: `components` of them for each element.
struct Numbers {
    std::vector<float> values;
    int components = 1;
    size_t count() const { return components > 0 ? values.size() / size_t(components) : 0; }
};

struct Source {
    const Json& root;
    std::vector<std::vector<uint8_t>> buffers;

    Numbers read(int accessorIndex) const {
        Numbers out;
        const Json& accessor = root["accessors"][size_t(std::max(accessorIndex, 0))];
        if (accessorIndex < 0 || accessor.isNull()) return out;
        std::string type = accessor["type"].asString("SCALAR");
        out.components = type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : type == "MAT3" ? 9 : type == "MAT2" ? 4 : 1;
        size_t count = size_t(std::max(accessor["count"].asInt(), 0));
        int componentType = accessor["componentType"].asInt(5126);
        size_t componentSize = componentType == 5126 || componentType == 5125 ? 4 : componentType == 5122 || componentType == 5123 ? 2 : 1;
        bool isNormalized = accessor["normalized"].kind == Json::Kind::Bool && accessor["normalized"].boolean;
        out.values.assign(count * size_t(out.components), 0.0f);
        if (!accessor.has("bufferView")) return out;            // all zeros, as the format says
        const Json& view = root["bufferViews"][size_t(accessor["bufferView"].asInt())];
        size_t bufferIndex = size_t(view["buffer"].asInt());
        if (view.isNull() || bufferIndex >= buffers.size()) return out;
        const std::vector<uint8_t>& bytes = buffers[bufferIndex];
        size_t start = size_t(view["byteOffset"].asInt()) + size_t(accessor["byteOffset"].asInt());
        size_t elementSize = componentSize * size_t(out.components);
        size_t stride = view.has("byteStride") ? size_t(view["byteStride"].asInt()) : elementSize;
        for (size_t i = 0; i < count; i++) {
            size_t at = start + i * stride;
            if (at + elementSize > bytes.size()) {
                out.values.clear();                             // the file promises more than it holds
                return out;
            }
            for (int c = 0; c < out.components; c++) {
                const uint8_t* p = bytes.data() + at + size_t(c) * componentSize;
                float value = 0;
                switch (componentType) {
                    case 5126: std::memcpy(&value, p, 4); break;
                    case 5125: { uint32_t v; std::memcpy(&v, p, 4); value = float(v); break; }
                    case 5123: { uint16_t v; std::memcpy(&v, p, 2); value = isNormalized ? float(v) / 65535.0f : float(v); break; }
                    case 5122: { int16_t v; std::memcpy(&v, p, 2); value = isNormalized ? std::max(float(v) / 32767.0f, -1.0f) : float(v); break; }
                    case 5121: value = isNormalized ? float(*p) / 255.0f : float(*p); break;
                    case 5120: value = isNormalized ? std::max(float(int8_t(*p)) / 127.0f, -1.0f) : float(int8_t(*p)); break;
                    default: break;
                }
                out.values[i * size_t(out.components) + size_t(c)] = value;
            }
        }
        return out;
    }
};

Mat4 matrixFrom(const float* m) {
    Mat4 out;
    for (int column = 0; column < 4; column++) out.columns[column] = Vec4{m[column * 4], m[column * 4 + 1], m[column * 4 + 2], m[column * 4 + 3]};
    return out;
}

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

}  // namespace

std::string gltfFileOf(const std::string& name, int* part) {
    size_t hash = name.rfind('#');
    if (part) *part = -1;
    if (hash == std::string::npos) return name;
    if (part) *part = std::atoi(name.c_str() + hash + 1);
    return name.substr(0, hash);
}

std::vector<GltfModel::Part> GltfModel::parts() const {
    std::vector<Part> all;
    for (size_t n = 0; n < nodes.size(); n++) {
        const GltfNode& node = nodes[n];
        if (node.mesh < 0 || size_t(node.mesh) >= meshes.size()) continue;
        for (size_t p = 0; p < meshes[size_t(node.mesh)].size(); p++) {
            all.push_back(Part{int(n), int(p), node.name, meshes[size_t(node.mesh)][p].material});
        }
    }
    return all;
}

MeshData GltfModel::part(const Part& which) const {
    MeshData out;
    if (which.node < 0 || size_t(which.node) >= nodes.size()) return out;
    const GltfNode& node = nodes[size_t(which.node)];
    if (node.mesh < 0 || size_t(which.primitive) >= meshes[size_t(node.mesh)].size()) return out;
    Mat4 place = globalMatrices(restPose())[size_t(which.node)];
    out = meshes[size_t(node.mesh)][size_t(which.primitive)].mesh;
    for (Vec3& position : out.positions) position = transformPoint(place, position);
    for (Vec3& normal : out.normals) normal = safeNormalize(transformDirection(place, normal));
    out.computeTangents();
    return out;
}

bool isGltfFile(const std::string& path) {
    std::string extension = std::filesystem::path(gltfFileOf(path)).extension().string();
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".gltf" || extension == ".glb";
}

std::optional<GltfModel> GltfModel::load(const std::string& path, std::string* error) {
    std::optional<std::vector<uint8_t>> bytes = readBytes(path);
    if (!bytes) {
        fail(error, "cannot read " + path);
        return std::nullopt;
    }
    std::string directory = std::filesystem::path(path).parent_path().string();
    // A .glb is the JSON and the data in one file: a header, then chunks, each with its length and kind.
    if (bytes->size() >= 20 && std::memcmp(bytes->data(), "glTF", 4) == 0) {
        std::string json;
        std::vector<uint8_t> binary;
        size_t at = 12;
        while (at + 8 <= bytes->size()) {
            uint32_t length, kind;
            std::memcpy(&length, bytes->data() + at, 4);
            std::memcpy(&kind, bytes->data() + at + 4, 4);
            at += 8;
            if (at + length > bytes->size()) break;
            if (kind == 0x4E4F534A) json.assign(reinterpret_cast<const char*>(bytes->data() + at), length);                 // "JSON"
            else if (kind == 0x004E4942) binary.assign(bytes->begin() + long(at), bytes->begin() + long(at + length));      // "BIN"
            at += length;
        }
        return parse(json, binary, directory, error);
    }
    return parse(std::string(bytes->begin(), bytes->end()), {}, directory, error);
}

std::optional<GltfModel> GltfModel::parse(const std::string& jsonText, const std::vector<uint8_t>& binary, const std::string& directory, std::string* error) {
    std::optional<Json> parsed = Json::parse(jsonText);
    if (!parsed || parsed->kind != Json::Kind::Object) {
        fail(error, "not a glTF file: it is not JSON");
        return std::nullopt;
    }
    const Json& root = *parsed;
    Source source{root, {}};
    for (const Json& buffer : root["buffers"].array) {
        std::string uri = buffer["uri"].asString();
        if (uri.empty()) {
            source.buffers.push_back(binary);
        } else if (uri.rfind("data:", 0) == 0) {
            size_t comma = uri.find(',');
            source.buffers.push_back(comma == std::string::npos ? std::vector<uint8_t>{} : decodeBase64(uri.substr(comma + 1)));
        } else {
            std::optional<std::vector<uint8_t>> bytes = readBytes(directory.empty() ? uri : directory + "/" + uri);
            if (!bytes) {
                fail(error, "cannot read " + uri + ", which the model's data is in");
                return std::nullopt;
            }
            source.buffers.push_back(std::move(*bytes));
        }
    }

    GltfModel model;
    for (const Json& material : root["materials"].array) {
        GltfMaterial out;
        out.name = material["name"].asString();
        const Json& pbr = material["pbrMetallicRoughness"];
        if (pbr["baseColorFactor"].size() == 4) {
            out.baseColor = Vec4{float(pbr["baseColorFactor"][0].asNumber(1)), float(pbr["baseColorFactor"][1].asNumber(1)), float(pbr["baseColorFactor"][2].asNumber(1)),
                                 float(pbr["baseColorFactor"][3].asNumber(1))};
        }
        out.metallic = float(pbr["metallicFactor"].asNumber(1));
        out.roughness = float(pbr["roughnessFactor"].asNumber(1));
        if (material["emissiveFactor"].size() == 3) {
            out.emissive = Vec3{float(material["emissiveFactor"][0].asNumber()), float(material["emissiveFactor"][1].asNumber()), float(material["emissiveFactor"][2].asNumber())};
        }
        int texture = pbr["baseColorTexture"]["index"].asInt(-1);
        if (texture >= 0) {
            int image = root["textures"][texture]["source"].asInt(-1);
            if (image >= 0) out.baseColorTexture = root["images"][image]["uri"].asString();
            if (out.baseColorTexture.rfind("data:", 0) == 0) out.baseColorTexture.clear();
        }
        model.materials.push_back(out);
    }

    for (const Json& mesh : root["meshes"].array) {
        std::vector<GltfPrimitive> primitives;
        for (const Json& primitive : mesh["primitives"].array) {
            if (primitive["mode"].asInt(4) != 4) continue;      // triangles only
            const Json& attributes = primitive["attributes"];
            GltfPrimitive out;
            out.material = primitive["material"].asInt(-1);
            Numbers positions = source.read(attributes["POSITION"].asInt(-1));
            if (positions.components != 3 || positions.count() == 0) continue;
            size_t count = positions.count();
            for (size_t i = 0; i < count; i++) out.mesh.positions.push_back(Vec3{positions.values[i * 3], positions.values[i * 3 + 1], positions.values[i * 3 + 2]});
            Numbers normals = source.read(attributes["NORMAL"].asInt(-1));
            if (normals.components == 3 && normals.count() == count) {
                for (size_t i = 0; i < count; i++) out.mesh.normals.push_back(Vec3{normals.values[i * 3], normals.values[i * 3 + 1], normals.values[i * 3 + 2]});
            }
            Numbers uvs = source.read(attributes["TEXCOORD_0"].asInt(-1));
            if (uvs.components == 2 && uvs.count() == count) {
                for (size_t i = 0; i < count; i++) out.mesh.uvs.push_back(Vec2{uvs.values[i * 2], uvs.values[i * 2 + 1]});
            } else {
                out.mesh.uvs.assign(count, Vec2{0, 0});
            }
            if (primitive.has("indices")) {
                Numbers indices = source.read(primitive["indices"].asInt(-1));
                for (float index : indices.values) out.mesh.indices.push_back(uint32_t(index));
            } else {
                for (size_t i = 0; i < count; i++) out.mesh.indices.push_back(uint32_t(i));
            }
            out.mesh.indices.resize(out.mesh.indices.size() / 3 * 3);
            bool indicesAreGood = true;
            for (uint32_t index : out.mesh.indices) indicesAreGood = indicesAreGood && index < count;
            if (!indicesAreGood || out.mesh.indices.empty()) continue;
            if (out.mesh.normals.empty()) out.mesh.computeNormals();
            Numbers joints = source.read(attributes["JOINTS_0"].asInt(-1)), weights = source.read(attributes["WEIGHTS_0"].asInt(-1));
            if (joints.components == 4 && weights.components == 4 && joints.count() == count && weights.count() == count) {
                for (size_t i = 0; i < count; i++) {
                    out.joints.push_back({uint16_t(joints.values[i * 4]), uint16_t(joints.values[i * 4 + 1]), uint16_t(joints.values[i * 4 + 2]), uint16_t(joints.values[i * 4 + 3])});
                    Vec4 weight{weights.values[i * 4], weights.values[i * 4 + 1], weights.values[i * 4 + 2], weights.values[i * 4 + 3]};
                    float sum = weight.x + weight.y + weight.z + weight.w;
                    out.weights.push_back(sum > 1e-6f ? weight / sum : Vec4{1, 0, 0, 0});
                }
            }
            primitives.push_back(std::move(out));
        }
        model.meshes.push_back(std::move(primitives));
    }

    for (const Json& node : root["nodes"].array) {
        GltfNode out;
        out.name = node["name"].asString();
        out.mesh = node["mesh"].asInt(-1);
        out.skin = node["skin"].asInt(-1);
        if (node["matrix"].size() == 16) {
            float m[16];
            for (size_t i = 0; i < 16; i++) m[i] = float(node["matrix"][i].asNumber());
            decompose(matrixFrom(m), out.translation, out.rotation, out.scale);
        } else {
            if (node["translation"].size() == 3) out.translation = Vec3{float(node["translation"][0].asNumber()), float(node["translation"][1].asNumber()), float(node["translation"][2].asNumber())};
            if (node["rotation"].size() == 4) {
                out.rotation = simd::normalize(Quat(float(node["rotation"][0].asNumber()), float(node["rotation"][1].asNumber()), float(node["rotation"][2].asNumber()),
                                                    float(node["rotation"][3].asNumber(1))));
            }
            if (node["scale"].size() == 3) out.scale = Vec3{float(node["scale"][0].asNumber(1)), float(node["scale"][1].asNumber(1)), float(node["scale"][2].asNumber(1))};
        }
        for (const Json& child : node["children"].array) out.children.push_back(child.asInt(-1));
        model.nodes.push_back(std::move(out));
    }
    for (size_t i = 0; i < model.nodes.size(); i++) {
        for (int child : model.nodes[i].children) {
            if (child >= 0 && size_t(child) < model.nodes.size() && size_t(child) != i) model.nodes[size_t(child)].parent = int(i);
        }
    }

    for (const Json& skin : root["skins"].array) {
        GltfSkin out;
        for (const Json& joint : skin["joints"].array) out.joints.push_back(joint.asInt(-1));
        Numbers matrices = source.read(skin["inverseBindMatrices"].asInt(-1));
        for (size_t i = 0; i < out.joints.size(); i++) {
            out.inverseBind.push_back(matrices.components == 16 && i < matrices.count() ? matrixFrom(matrices.values.data() + i * 16) : identity());
        }
        model.skins.push_back(std::move(out));
    }

    for (const Json& animation : root["animations"].array) {
        GltfAnimation out;
        out.name = animation["name"].asString("Animation " + std::to_string(model.animations.size() + 1));
        for (const Json& channel : animation["channels"].array) {
            const Json& sampler = animation["samplers"][size_t(std::max(channel["sampler"].asInt(-1), 0))];
            std::string path = channel["target"]["path"].asString();
            GltfChannel made;
            made.node = channel["target"]["node"].asInt(-1);
            if (path == "translation") made.path = GltfChannel::Path::Translation;
            else if (path == "rotation") made.path = GltfChannel::Path::Rotation;
            else if (path == "scale") made.path = GltfChannel::Path::Scale;
            else continue;                                      // weights of blend shapes: not read yet
            if (made.node < 0 || size_t(made.node) >= model.nodes.size() || sampler.isNull()) continue;
            std::string interpolation = sampler["interpolation"].asString("LINEAR");
            made.isStep = interpolation == "STEP";
            Numbers times = source.read(sampler["input"].asInt(-1)), values = source.read(sampler["output"].asInt(-1));
            made.times = times.values;
            // A cubic spline stores a tangent before and after each value: the values alone are used, blended straight.
            size_t each = interpolation == "CUBICSPLINE" ? 3 : 1;
            size_t components = size_t(values.components);
            if (made.times.empty() || values.count() < made.times.size() * each || components < 3) continue;
            for (size_t i = 0; i < made.times.size(); i++) {
                const float* v = values.values.data() + (i * each + (each == 3 ? 1 : 0)) * components;
                made.values.push_back(Vec4{v[0], v[1], v[2], components >= 4 ? v[3] : 0.0f});
            }
            out.duration = std::max(out.duration, made.times.back());
            out.channels.push_back(std::move(made));
        }
        model.animations.push_back(std::move(out));
    }

    bool hasTriangles = false;
    for (const auto& mesh : model.meshes) hasTriangles = hasTriangles || !mesh.empty();
    if (!hasTriangles) {
        fail(error, "the model has no triangles");
        return std::nullopt;
    }
    return model;
}

int GltfModel::findAnimation(const std::string& name) const {
    for (size_t i = 0; i < animations.size(); i++) {
        if (animations[i].name == name) return int(i);
    }
    return -1;
}

GltfPose GltfModel::restPose() const {
    GltfPose pose;
    for (const GltfNode& node : nodes) {
        pose.translations.push_back(node.translation);
        pose.rotations.push_back(node.rotation);
        pose.scales.push_back(node.scale);
    }
    return pose;
}

void GltfModel::sample(int animation, float time, GltfPose& pose) const {
    if (animation < 0 || size_t(animation) >= animations.size() || pose.translations.size() != nodes.size()) return;
    for (const GltfChannel& channel : animations[size_t(animation)].channels) {
        // The two keys the time falls between, and how far from the first to the second.
        size_t next = size_t(std::upper_bound(channel.times.begin(), channel.times.end(), time) - channel.times.begin());
        size_t before = next == 0 ? 0 : next - 1;
        next = std::min(next, channel.times.size() - 1);
        float span = channel.times[next] - channel.times[before];
        float share = channel.isStep || span <= 1e-9f ? 0.0f : std::clamp((time - channel.times[before]) / span, 0.0f, 1.0f);
        Vec4 a = channel.values[before], b = channel.values[next];
        size_t node = size_t(channel.node);
        switch (channel.path) {
            case GltfChannel::Path::Translation: pose.translations[node] = Vec3{a.x, a.y, a.z} + (Vec3{b.x, b.y, b.z} - Vec3{a.x, a.y, a.z}) * share; break;
            case GltfChannel::Path::Scale: pose.scales[node] = Vec3{a.x, a.y, a.z} + (Vec3{b.x, b.y, b.z} - Vec3{a.x, a.y, a.z}) * share; break;
            case GltfChannel::Path::Rotation:
                pose.rotations[node] = simd::slerp(simd::normalize(Quat(a.x, a.y, a.z, a.w)), simd::normalize(Quat(b.x, b.y, b.z, b.w)), share);
                break;
        }
    }
}

std::vector<Mat4> GltfModel::globalMatrices(const GltfPose& pose) const {
    std::vector<Mat4> globals(nodes.size(), identity());
    std::vector<char> isDone(nodes.size(), 0);
    if (pose.translations.size() != nodes.size()) return globals;
    // A node's place is its parent's place, then its own: parents first, whatever order the file lists them in.
    std::vector<int> path;
    for (size_t start = 0; start < nodes.size(); start++) {
        path.clear();
        for (int at = int(start); at >= 0 && !isDone[size_t(at)] && path.size() <= nodes.size(); at = nodes[size_t(at)].parent) path.push_back(at);
        for (size_t i = path.size(); i-- > 0;) {
            size_t node = size_t(path[i]);
            Mat4 local = compose(pose.translations[node], pose.rotations[node], pose.scales[node]);
            int parent = nodes[node].parent;
            globals[node] = parent >= 0 ? globals[size_t(parent)] * local : local;
            isDone[node] = 1;
        }
    }
    return globals;
}

MeshData GltfModel::assembled(const GltfPose& pose) const {
    MeshData out;
    std::vector<Mat4> globals = globalMatrices(pose);
    std::vector<Mat4> jointMatrices;
    for (size_t n = 0; n < nodes.size(); n++) {
        const GltfNode& node = nodes[n];
        if (node.mesh < 0 || size_t(node.mesh) >= meshes.size()) continue;
        const GltfSkin* skin = node.skin >= 0 && size_t(node.skin) < skins.size() ? &skins[size_t(node.skin)] : nullptr;
        if (skin) {
            // Each joint's matrix takes a vertex from where it was modeled to where the bone is now.
            jointMatrices.resize(skin->joints.size());
            for (size_t j = 0; j < skin->joints.size(); j++) {
                int joint = skin->joints[j];
                jointMatrices[j] = (joint >= 0 && size_t(joint) < globals.size() ? globals[size_t(joint)] : identity()) * skin->inverseBind[j];
            }
        }
        for (const GltfPrimitive& primitive : meshes[size_t(node.mesh)]) {
            uint32_t base = uint32_t(out.positions.size());
            bool isSkinned = skin && primitive.joints.size() == primitive.mesh.positions.size();
            Mat4 place = globals[n];
            for (size_t v = 0; v < primitive.mesh.positions.size(); v++) {
                Vec3 position = primitive.mesh.positions[v], normal = primitive.mesh.normals[v];
                if (isSkinned) {
                    // Moved by each of its bones in turn, and the results blended by how much it follows each.
                    Vec3 moved{0, 0, 0}, turned{0, 0, 0};
                    for (int k = 0; k < 4; k++) {
                        float weight = primitive.weights[v][k];
                        size_t joint = primitive.joints[v][size_t(k)];
                        if (weight <= 0 || joint >= jointMatrices.size()) continue;
                        moved += transformPoint(jointMatrices[joint], position) * weight;
                        turned += transformDirection(jointMatrices[joint], normal) * weight;
                    }
                    out.positions.push_back(moved);
                    out.normals.push_back(safeNormalize(turned, normal));
                } else {
                    out.positions.push_back(transformPoint(place, position));
                    out.normals.push_back(safeNormalize(transformDirection(place, normal), normal));
                }
                out.uvs.push_back(primitive.mesh.uvs[v]);
            }
            for (uint32_t index : primitive.mesh.indices) out.indices.push_back(base + index);
        }
    }
    out.computeTangents();
    return out;
}

}  // namespace molten
