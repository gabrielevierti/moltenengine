#include "Assets/ModelFormats.hpp"

#include "ufbx.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <unordered_map>

namespace molten {

namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

std::optional<std::string> readWhole(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// A model of one mesh, one node, no material: what STL and PLY files are.
GltfModel modelOfOneMesh(MeshData mesh, const std::string& name) {
    GltfModel model;
    if (mesh.normals.size() != mesh.positions.size()) {
        mesh.normals.clear();
        mesh.computeNormals();
    }
    if (mesh.uvs.size() != mesh.positions.size()) mesh.uvs.assign(mesh.positions.size(), Vec2{0, 0});
    GltfPrimitive primitive;
    primitive.mesh = std::move(mesh);
    model.meshes.push_back({std::move(primitive)});
    GltfNode node;
    node.name = name;
    node.mesh = 0;
    model.nodes.push_back(node);
    return model;
}

std::string stemOf(const std::string& path) { return std::filesystem::path(path).stem().string(); }

// ---------------------------------------------------------------- FBX and OBJ, through ufbx

Vec3 toVec3(ufbx_vec3 v) { return Vec3{float(v.x), float(v.y), float(v.z)}; }

Mat4 toMat4(const ufbx_matrix& m) {
    Mat4 out = identity();
    for (int c = 0; c < 4; c++) out.columns[c] = Vec4{float(m.cols[c].x), float(m.cols[c].y), float(m.cols[c].z), c == 3 ? 1.0f : 0.0f};
    return out;
}

GltfPicture pictureOf(const ufbx_texture* texture, GltfModel& model, std::map<const ufbx_texture*, int>& carried) {
    GltfPicture picture;
    if (!texture) return picture;
    // A layered texture: its first layer is the picture.
    if (texture->type == UFBX_TEXTURE_LAYERED && texture->layers.count > 0) texture = texture->layers.data[0].texture;
    if (!texture) return picture;
    if (texture->content.size > 0) {
        auto found = carried.find(texture);
        if (found == carried.end()) {
            GltfImage image;
            image.name = texture->name.length > 0 ? std::string(texture->name.data) : "Picture " + std::to_string(model.images.size() + 1);
            const uint8_t* bytes = static_cast<const uint8_t*>(texture->content.data);
            image.bytes.assign(bytes, bytes + texture->content.size);
            image.extension = image.bytes.size() > 2 && image.bytes[0] == 0xFF && image.bytes[1] == 0xD8 ? ".jpg" : ".png";
            model.images.push_back(std::move(image));
            found = carried.emplace(texture, int(model.images.size()) - 1).first;
        }
        picture.image = found->second;
        return picture;
    }
    picture.file = texture->relative_filename.length > 0 ? std::string(texture->relative_filename.data) : std::string(texture->filename.data);
    return picture;
}

GltfMaterial materialOf(const ufbx_material* material, GltfModel& model, std::map<const ufbx_texture*, int>& carried, bool isObj) {
    GltfMaterial out;
    out.name = material->name.length > 0 ? std::string(material->name.data) : "Material";
    const ufbx_material_pbr_maps& pbr = material->pbr;
    ufbx_vec4 color = pbr.base_color.value_vec4;
    float factor = pbr.base_factor.has_value ? float(pbr.base_factor.value_real) : 1.0f;
    out.baseColor = Vec4{float(color.x) * factor, float(color.y) * factor, float(color.z) * factor, 1.0f};
    out.metallic = pbr.metalness.has_value ? std::clamp(float(pbr.metalness.value_real), 0.0f, 1.0f) : 0.0f;
    out.roughness = pbr.roughness.has_value ? std::clamp(float(pbr.roughness.value_real), 0.0f, 1.0f) : 0.6f;
    // An OBJ's shininess (Ns) as Blender writes it: (1 - roughness)^2 * 1000.
    if (isObj && material->fbx.specular_exponent.has_value) {
        out.roughness = 1.0f - std::sqrt(std::clamp(float(material->fbx.specular_exponent.value_real) / 1000.0f, 0.0f, 1.0f));
    }
    ufbx_vec3 glow = pbr.emission_color.value_vec3;
    float glowFactor = pbr.emission_factor.has_value ? float(pbr.emission_factor.value_real) : 1.0f;
    out.emissive = Vec3{float(glow.x), float(glow.y), float(glow.z)} * glowFactor;
    out.colorMap = pictureOf(pbr.base_color.texture, model, carried);
    if (!out.colorMap.has() && material->fbx.diffuse_color.texture) out.colorMap = pictureOf(material->fbx.diffuse_color.texture, model, carried);
    out.baseColorTexture = out.colorMap.file;
    // Painted with a picture: the picture is its color, as it is in the program it was made in.
    if (out.colorMap.has()) out.baseColor = Vec4{1, 1, 1, 1};
    out.normalMap = pictureOf(pbr.normal_map.texture, model, carried);
    if (!out.normalMap.has() && material->fbx.normal_map.texture) out.normalMap = pictureOf(material->fbx.normal_map.texture, model, carried);
    if (!out.normalMap.has() && material->fbx.bump.texture) out.normalMap = pictureOf(material->fbx.bump.texture, model, carried);
    out.emissiveMap = pictureOf(pbr.emission_color.texture, model, carried);
    if (out.emissiveMap.has() && glow.x + glow.y + glow.z <= 0.0) out.emissive = Vec3{1, 1, 1} * glowFactor;
    // Metal and roughness in one picture, as glTF has them (a model converted from one): blue and green.
    if (pbr.roughness.texture && pbr.roughness.texture == pbr.metalness.texture) out.metalRoughMap = pictureOf(pbr.roughness.texture, model, carried);
    out.occlusionMap = pictureOf(pbr.ambient_occlusion.texture, model, carried);
    // Seen through: less than whole, or a picture of how much (an OBJ's map_d: the alpha of its color).
    float opacity = pbr.opacity.has_value ? float(pbr.opacity.value_real) : 1.0f;
    // Nothing is made to be wholly invisible: an opacity of 0 without a picture is an exporter's
    // mistake (some write "d 0" for opaque), and is taken as opaque.
    if (opacity <= 0.001f && !pbr.opacity.texture) opacity = 1.0f;
    if (opacity < 0.999f || pbr.opacity.texture) {
        out.isBlended = true;
        out.baseColor.w = std::clamp(opacity, 0.0f, 1.0f);
    }
    out.isDoubleSided = material->features.double_sided.enabled;
    return out;
}

}  // namespace

std::optional<GltfModel> loadWithUfbx(const std::string& path, std::string* error) {
    ufbx_load_opts options = {};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0f;
    options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    options.generate_missing_normals = true;
    options.load_external_files = true;             // an OBJ's .mtl beside it
    options.ignore_missing_external_files = true;
    ufbx_error failure;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &options, &failure);
    if (!scene) {
        fail(error, "cannot read " + std::filesystem::path(path).filename().string() + ": " + std::string(failure.description.data, failure.description.length));
        return std::nullopt;
    }
    GltfModel model;
    std::map<const ufbx_texture*, int> carried;
    const bool isObj = scene->metadata.file_format == UFBX_FILE_FORMAT_OBJ;
    for (size_t i = 0; i < scene->materials.count; i++) model.materials.push_back(materialOf(scene->materials.data[i], model, carried, isObj));
    std::unordered_map<uint32_t, int> nodeAt;                      // by ufbx's typed id
    for (size_t i = 0; i < scene->nodes.count; i++) nodeAt[scene->nodes.data[i]->typed_id] = int(i);

    for (size_t i = 0; i < scene->nodes.count; i++) {
        const ufbx_node* node = scene->nodes.data[i];
        GltfNode out;
        out.name = node->name.length > 0 ? std::string(node->name.data) : (node->is_root ? stemOf(path) : "Node " + std::to_string(i));
        out.translation = toVec3(node->local_transform.translation);
        out.rotation = simd::normalize(Quat(float(node->local_transform.rotation.x), float(node->local_transform.rotation.y), float(node->local_transform.rotation.z),
                                            float(node->local_transform.rotation.w)));
        out.scale = toVec3(node->local_transform.scale);
        if (node->parent) out.parent = nodeAt.count(node->parent->typed_id) ? nodeAt[node->parent->typed_id] : -1;
        model.nodes.push_back(out);
    }
    for (size_t i = 0; i < model.nodes.size(); i++) {
        if (model.nodes[i].parent >= 0) model.nodes[size_t(model.nodes[i].parent)].children.push_back(int(i));
    }

    std::vector<uint32_t> corners;
    for (size_t i = 0; i < scene->nodes.count; i++) {
        const ufbx_node* node = scene->nodes.data[i];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->num_triangles == 0) continue;
        // The mesh in its node's own space: what FBX keeps apart (the geometry's offset) is put in.
        const ufbx_matrix toNode = node->geometry_to_node;
        const ufbx_matrix forNormals = ufbx_matrix_for_normals(&toNode);
        const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
        std::vector<GltfPrimitive> primitives;
        corners.resize(mesh->max_face_triangles * 3);
        for (size_t p = 0; p < mesh->material_parts.count; p++) {
            const ufbx_mesh_part& part = mesh->material_parts.data[p];
            if (part.num_triangles == 0) continue;
            GltfPrimitive primitive;
            const ufbx_material* material = part.index < mesh->materials.count ? mesh->materials.data[part.index] : nullptr;
            primitive.material = -1;
            for (size_t m = 0; material && m < scene->materials.count; m++) {
                if (scene->materials.data[m] == material) primitive.material = int(m);
            }
            // Corners that share a position, a normal and a place on the picture are one vertex.
            std::map<std::array<uint32_t, 3>, uint32_t> made;
            for (uint32_t faceIndex : part.face_indices) {
                ufbx_face face = mesh->faces.data[faceIndex];
                uint32_t triangles = ufbx_triangulate_face(corners.data(), corners.size(), mesh, face);
                for (uint32_t t = 0; t < triangles * 3; t++) {
                    uint32_t corner = corners[t];
                    std::array<uint32_t, 3> key{mesh->vertex_position.indices.data[corner],
                                                mesh->vertex_normal.exists ? mesh->vertex_normal.indices.data[corner] : 0u,
                                                mesh->vertex_uv.exists ? mesh->vertex_uv.indices.data[corner] : 0u};
                    auto found = made.find(key);
                    if (found == made.end()) {
                        uint32_t index = uint32_t(primitive.mesh.positions.size());
                        primitive.mesh.positions.push_back(toVec3(ufbx_transform_position(&toNode, ufbx_get_vertex_vec3(&mesh->vertex_position, corner))));
                        Vec3 normal = mesh->vertex_normal.exists ? toVec3(ufbx_transform_direction(&forNormals, ufbx_get_vertex_vec3(&mesh->vertex_normal, corner))) : Vec3{0, 1, 0};
                        primitive.mesh.normals.push_back(safeNormalize(normal));
                        Vec2 uv{0, 0};
                        if (mesh->vertex_uv.exists) {
                            ufbx_vec2 read = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);
                            uv = Vec2{float(read.x), 1.0f - float(read.y)};         // from the bottom left to the top left
                        }
                        primitive.mesh.uvs.push_back(uv);
                        if (skin) {
                            // Its four strongest bones (the weights come strongest first), made to sum to one.
                            const ufbx_skin_vertex& weights = skin->vertices.data[mesh->vertex_indices.data[corner]];
                            std::array<uint16_t, 4> joints{0, 0, 0, 0};
                            Vec4 weight{0, 0, 0, 0};
                            for (uint32_t w = 0; w < std::min(weights.num_weights, 4u); w++) {
                                const ufbx_skin_weight& one = skin->weights.data[weights.weight_begin + w];
                                joints[w] = uint16_t(one.cluster_index);
                                weight[int(w)] = float(one.weight);
                            }
                            float sum = weight.x + weight.y + weight.z + weight.w;
                            primitive.joints.push_back(joints);
                            primitive.weights.push_back(sum > 1e-6f ? weight / sum : Vec4{1, 0, 0, 0});
                        }
                        found = made.emplace(key, index).first;
                    }
                    primitive.mesh.indices.push_back(found->second);
                }
            }
            if (!primitive.mesh.indices.empty()) primitives.push_back(std::move(primitive));
        }
        if (primitives.empty()) continue;
        model.meshes.push_back(std::move(primitives));
        model.nodes[i].mesh = int(model.meshes.size()) - 1;
        if (skin && skin->clusters.count > 0) {
            // Each bone: the node it is, and what takes the mesh, as it was bound, into its own space.
            GltfSkin made;
            ufbx_matrix fromNode = ufbx_matrix_invert(&toNode);
            for (size_t c = 0; c < skin->clusters.count; c++) {
                const ufbx_skin_cluster* cluster = skin->clusters.data[c];
                made.joints.push_back(cluster->bone_node && nodeAt.count(cluster->bone_node->typed_id) ? nodeAt[cluster->bone_node->typed_id] : 0);
                made.inverseBind.push_back(toMat4(cluster->geometry_to_bone) * toMat4(fromNode));
            }
            model.skins.push_back(std::move(made));
            model.nodes[i].skin = int(model.skins.size()) - 1;
        }
    }

    // Animations: each of the file's takes, sampled thirty times a second, for the nodes it moves.
    for (size_t s = 0; s < scene->anim_stacks.count; s++) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[s];
        double begin = stack->time_begin, end = stack->time_end;
        if (!(end > begin)) continue;
        const int samples = std::clamp(int(std::ceil((end - begin) * 30.0)) + 1, 2, 30 * 600);
        GltfAnimation animation;
        animation.name = stack->name.length > 0 ? std::string(stack->name.data) : "Take " + std::to_string(s + 1);
        animation.duration = float(end - begin);
        for (size_t n = 0; n < scene->nodes.count; n++) {
            const ufbx_node* node = scene->nodes.data[n];
            if (node->is_root) continue;
            GltfChannel moves, turns, sizes;
            moves.node = turns.node = sizes.node = int(n);
            moves.path = GltfChannel::Path::Translation;
            turns.path = GltfChannel::Path::Rotation;
            sizes.path = GltfChannel::Path::Scale;
            bool moved = false, turned = false, sized = false;
            Vec4 firstMove{0, 0, 0, 0}, firstTurn{0, 0, 0, 1}, firstSize{1, 1, 1, 0};
            for (int k = 0; k < samples; k++) {
                double at = begin + (end - begin) * double(k) / double(samples - 1);
                ufbx_transform t = ufbx_evaluate_transform(stack->anim, node, at);
                Vec4 move{float(t.translation.x), float(t.translation.y), float(t.translation.z), 0};
                Vec4 turn{float(t.rotation.x), float(t.rotation.y), float(t.rotation.z), float(t.rotation.w)};
                Vec4 size{float(t.scale.x), float(t.scale.y), float(t.scale.z), 0};
                if (k == 0) firstMove = move, firstTurn = turn, firstSize = size;
                // The shorter way round from the one before: no spin between two samples.
                if (!turns.values.empty() && simd::dot(turns.values.back(), turn) < 0) turn = -turn;
                moved = moved || simd::length(move - firstMove) > 1e-4f;
                turned = turned || std::abs(std::abs(simd::dot(turn, firstTurn)) - 1.0f) > 1e-6f;
                sized = sized || simd::length(size - firstSize) > 1e-4f;
                float time = float(at - begin);
                moves.times.push_back(time), turns.times.push_back(time), sizes.times.push_back(time);
                moves.values.push_back(move), turns.values.push_back(turn), sizes.values.push_back(size);
            }
            if (moved) animation.channels.push_back(std::move(moves));
            if (turned) animation.channels.push_back(std::move(turns));
            if (sized) animation.channels.push_back(std::move(sizes));
        }
        if (!animation.channels.empty()) model.animations.push_back(std::move(animation));
    }
    ufbx_free_scene(scene);
    if (model.meshes.empty()) {
        fail(error, std::filesystem::path(path).filename().string() + " has no triangles");
        return std::nullopt;
    }
    return model;
}

// ---------------------------------------------------------------- STL

std::optional<GltfModel> parseStl(const std::string& bytes, std::string* error) {
    MeshData mesh;
    auto triangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        Vec3 normal = safeNormalize(simd::cross(b - a, c - a));
        uint32_t first = uint32_t(mesh.positions.size());
        for (Vec3 p : {a, b, c}) {
            mesh.positions.push_back(p);
            mesh.normals.push_back(normal);
        }
        for (uint32_t k = 0; k < 3; k++) mesh.indices.push_back(first + k);
    };
    uint32_t count = 0;
    if (bytes.size() >= 84) std::memcpy(&count, bytes.data() + 80, 4);
    if (bytes.size() >= 84 && bytes.size() == 84 + size_t(count) * 50) {
        // Binary: 80 bytes of anything, how many triangles, then each its normal, its three corners and two spare bytes.
        for (uint32_t i = 0; i < count; i++) {
            float v[12];
            std::memcpy(v, bytes.data() + 84 + size_t(i) * 50, 48);
            triangle(Vec3{v[3], v[4], v[5]}, Vec3{v[6], v[7], v[8]}, Vec3{v[9], v[10], v[11]});
        }
    } else {
        // Text: "solid name", then for each face "facet normal ... outer loop vertex x y z (three times) endloop endfacet".
        std::istringstream text(bytes);
        std::string word;
        std::vector<Vec3> corners;
        while (text >> word) {
            if (word != "vertex") continue;
            float x = 0, y = 0, z = 0;
            text >> x >> y >> z;
            corners.push_back(Vec3{x, y, z});
            if (corners.size() == 3) {
                triangle(corners[0], corners[1], corners[2]);
                corners.clear();
            }
        }
    }
    if (mesh.indices.empty()) {
        fail(error, "no triangles in the STL file");
        return std::nullopt;
    }
    return modelOfOneMesh(std::move(mesh), "Mesh");
}

std::optional<GltfModel> loadStl(const std::string& path, std::string* error) {
    std::optional<std::string> bytes = readWhole(path);
    if (!bytes) {
        fail(error, "cannot read " + path);
        return std::nullopt;
    }
    std::optional<GltfModel> model = parseStl(*bytes, error);
    if (model) model->nodes[0].name = stemOf(path);
    return model;
}

// ---------------------------------------------------------------- PLY

std::optional<GltfModel> parsePly(const std::string& bytes, std::string* error) {
    // The header: what elements there are, how many of each, and the properties of each, in order.
    struct Property {
        std::string name, type, countType;          // countType: a list's
        bool isList = false;
    };
    struct Element {
        std::string name;
        size_t count = 0;
        std::vector<Property> properties;
    };
    std::vector<Element> elements;
    std::string format;
    size_t at = 0;
    if (bytes.compare(0, 3, "ply") != 0) {
        fail(error, "not a PLY file");
        return std::nullopt;
    }
    while (at < bytes.size()) {
        size_t end = bytes.find('\n', at);
        if (end == std::string::npos) break;
        std::istringstream line(bytes.substr(at, end - at));
        at = end + 1;
        std::string word;
        line >> word;
        if (word == "format") line >> format;
        else if (word == "element") {
            Element element;
            line >> element.name >> element.count;
            elements.push_back(element);
        } else if (word == "property" && !elements.empty()) {
            Property property;
            line >> property.type;
            if (property.type == "list") {
                property.isList = true;
                line >> property.countType >> property.type;
            }
            line >> property.name;
            elements.back().properties.push_back(property);
        } else if (word == "end_header") {
            break;
        }
    }
    const bool isText = format == "ascii";
    const bool isBigEndian = format == "binary_big_endian";
    auto sizeOf = [](const std::string& type) -> size_t {
        if (type == "char" || type == "uchar" || type == "int8" || type == "uint8") return 1;
        if (type == "short" || type == "ushort" || type == "int16" || type == "uint16") return 2;
        if (type == "double" || type == "float64") return 8;
        return 4;
    };
    std::istringstream text(isText ? bytes.substr(at) : std::string());
    auto next = [&](const std::string& type) -> double {
        if (isText) {
            double value = 0;
            text >> value;
            return value;
        }
        size_t size = sizeOf(type);
        if (at + size > bytes.size()) return 0;
        unsigned char raw[8];
        std::memcpy(raw, bytes.data() + at, size);
        at += size;
        if (isBigEndian) std::reverse(raw, raw + size);
        if (type == "char" || type == "int8") return double(int8_t(raw[0]));
        if (type == "uchar" || type == "uint8") return double(raw[0]);
        if (type == "short" || type == "int16") { int16_t v; std::memcpy(&v, raw, 2); return v; }
        if (type == "ushort" || type == "uint16") { uint16_t v; std::memcpy(&v, raw, 2); return v; }
        if (type == "int" || type == "int32") { int32_t v; std::memcpy(&v, raw, 4); return v; }
        if (type == "uint" || type == "uint32") { uint32_t v; std::memcpy(&v, raw, 4); return v; }
        if (type == "double" || type == "float64") { double v; std::memcpy(&v, raw, 8); return v; }
        float v;
        std::memcpy(&v, raw, 4);
        return v;
    };
    MeshData mesh;
    bool hasNormals = false, hasUvs = false;
    for (const Element& element : elements) {
        for (size_t i = 0; i < element.count; i++) {
            Vec3 position{0, 0, 0}, normal{0, 0, 0};
            Vec2 uv{0, 0};
            std::vector<uint32_t> polygon;
            for (const Property& property : element.properties) {
                if (property.isList) {
                    size_t count = size_t(std::max(next(property.countType), 0.0));
                    for (size_t k = 0; k < count; k++) polygon.push_back(uint32_t(std::max(next(property.type), 0.0)));
                    continue;
                }
                float value = float(next(property.type));
                const std::string& n = property.name;
                if (n == "x") position.x = value;
                else if (n == "y") position.y = value;
                else if (n == "z") position.z = value;
                else if (n == "nx") normal.x = value, hasNormals = true;
                else if (n == "ny") normal.y = value;
                else if (n == "nz") normal.z = value;
                else if (n == "s" || n == "u" || n == "texture_u") uv.x = value, hasUvs = true;
                else if (n == "t" || n == "v" || n == "texture_v") uv.y = 1.0f - value;
            }
            if (element.name == "vertex") {
                mesh.positions.push_back(position);
                mesh.normals.push_back(normal);
                mesh.uvs.push_back(uv);
            } else if (element.name == "face") {
                // A polygon of any size, as a fan of triangles.
                for (size_t k = 2; k < polygon.size(); k++) {
                    mesh.indices.push_back(polygon[0]);
                    mesh.indices.push_back(polygon[k - 1]);
                    mesh.indices.push_back(polygon[k]);
                }
            }
        }
    }
    for (uint32_t index : mesh.indices) {
        if (index >= mesh.positions.size()) {
            fail(error, "the PLY file's faces name corners it does not have");
            return std::nullopt;
        }
    }
    if (mesh.indices.empty()) {
        fail(error, "no faces in the PLY file");
        return std::nullopt;
    }
    if (!hasNormals) mesh.normals.clear();
    if (!hasUvs) mesh.uvs.assign(mesh.positions.size(), Vec2{0, 0});
    return modelOfOneMesh(std::move(mesh), "Mesh");
}

std::optional<GltfModel> loadPly(const std::string& path, std::string* error) {
    std::optional<std::string> bytes = readWhole(path);
    if (!bytes) {
        fail(error, "cannot read " + path);
        return std::nullopt;
    }
    std::optional<GltfModel> model = parsePly(*bytes, error);
    if (model) model->nodes[0].name = stemOf(path);
    return model;
}

// ---------------------------------------------------------------- Collada

namespace {

/// Just enough XML for Collada: elements, their attributes, their text. No namespaces are understood.
struct Xml {
    std::string name, text;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Xml> children;

    std::string attribute(const char* key) const {
        for (const auto& [k, v] : attributes) {
            if (k == key) return v;
        }
        return {};
    }
    const Xml* child(const char* key) const {
        for (const Xml& c : children) {
            if (c.name == key) return &c;
        }
        return nullptr;
    }
    std::vector<const Xml*> all(const char* key) const {
        std::vector<const Xml*> out;
        for (const Xml& c : children) {
            if (c.name == key) out.push_back(&c);
        }
        return out;
    }
};

std::string unescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        size_t end = text.find(';', i);
        std::string entity = end == std::string::npos ? std::string() : text.substr(i + 1, end - i - 1);
        if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "amp") out += '&';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else if (entity == "#32") out += ' ';
        else {
            out += '&';
            continue;
        }
        i = end;
    }
    return out;
}

struct XmlReader {
    const std::string& s;
    size_t at = 0;
    int depth = 0;

    void skipSpace() {
        while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) at++;
    }
    /// Skips <?...?>, <!-- -->, <!DOCTYPE ...>. True if it skipped one.
    bool skipOther() {
        if (s.compare(at, 4, "<!--") == 0) {
            size_t end = s.find("-->", at);
            at = end == std::string::npos ? s.size() : end + 3;
            return true;
        }
        if (s.compare(at, 2, "<?") == 0 || (s.compare(at, 2, "<!") == 0 && s.compare(at, 9, "<![CDATA[") != 0)) {
            size_t end = s.find('>', at);
            at = end == std::string::npos ? s.size() : end + 1;
            return true;
        }
        return false;
    }
    bool element(Xml& out) {
        if (++depth > 200) return false;
        if (at >= s.size() || s[at] != '<') return false;
        at++;
        size_t start = at;
        while (at < s.size() && !std::isspace(static_cast<unsigned char>(s[at])) && s[at] != '>' && s[at] != '/') at++;
        out.name = s.substr(start, at - start);
        // Attributes.
        while (true) {
            skipSpace();
            if (at >= s.size()) return false;
            if (s[at] == '/') {
                at = s.find('>', at);
                if (at == std::string::npos) return false;
                at++;
                depth--;
                return true;
            }
            if (s[at] == '>') {
                at++;
                break;
            }
            size_t keyStart = at;
            while (at < s.size() && s[at] != '=' && !std::isspace(static_cast<unsigned char>(s[at])) && s[at] != '>') at++;
            std::string key = s.substr(keyStart, at - keyStart);
            skipSpace();
            std::string value;
            if (at < s.size() && s[at] == '=') {
                at++;
                skipSpace();
                char quote = at < s.size() ? s[at] : '"';
                if (quote == '"' || quote == '\'') {
                    size_t end = s.find(quote, at + 1);
                    if (end == std::string::npos) return false;
                    value = unescape(s.substr(at + 1, end - at - 1));
                    at = end + 1;
                }
            }
            out.attributes.push_back({key, value});
        }
        // Contents: text and elements, up to the closing tag.
        while (at < s.size()) {
            if (s[at] == '<') {
                if (s.compare(at, 2, "</") == 0) {
                    size_t end = s.find('>', at);
                    if (end == std::string::npos) return false;
                    at = end + 1;
                    depth--;
                    return true;
                }
                if (s.compare(at, 9, "<![CDATA[") == 0) {
                    size_t end = s.find("]]>", at);
                    if (end == std::string::npos) return false;
                    out.text += s.substr(at + 9, end - at - 9);
                    at = end + 3;
                    continue;
                }
                if (skipOther()) continue;
                Xml child;
                if (!element(child)) return false;
                out.children.push_back(std::move(child));
            } else {
                size_t end = s.find('<', at);
                if (end == std::string::npos) end = s.size();
                out.text += unescape(s.substr(at, end - at));
                at = end;
            }
        }
        return false;
    }
};

std::vector<float> numbersOf(const std::string& text) {
    std::vector<float> out;
    const char* p = text.c_str();
    char* end = nullptr;
    while (*p) {
        float value = std::strtof(p, &end);
        if (end == p) {
            p++;
            continue;
        }
        out.push_back(value);
        p = end;
    }
    return out;
}

std::vector<int> integersOf(const std::string& text) {
    std::vector<int> out;
    const char* p = text.c_str();
    char* end = nullptr;
    while (*p) {
        long value = std::strtol(p, &end, 10);
        if (end == p) {
            p++;
            continue;
        }
        out.push_back(int(value));
        p = end;
    }
    return out;
}

std::string withoutHash(const std::string& url) { return !url.empty() && url[0] == '#' ? url.substr(1) : url; }

void collectIds(const Xml& node, std::unordered_map<std::string, const Xml*>& ids) {
    std::string id = node.attribute("id");
    if (!id.empty()) ids[id] = &node;
    for (const Xml& child : node.children) collectIds(child, ids);
}

}  // namespace

std::optional<GltfModel> parseCollada(const std::string& text, std::string* error) {
    XmlReader reader{text};
    Xml root;
    bool read = false;
    while (reader.at < text.size()) {
        reader.skipSpace();
        if (reader.skipOther()) continue;
        if (reader.at < text.size() && text[reader.at] == '<') {
            read = reader.element(root);
            break;
        }
        reader.at++;
    }
    if (!read || root.name != "COLLADA") {
        fail(error, "not a Collada file");
        return std::nullopt;
    }
    std::unordered_map<std::string, const Xml*> ids;
    collectIds(root, ids);
    auto byId = [&](const std::string& url) -> const Xml* {
        auto found = ids.find(withoutHash(url));
        return found == ids.end() ? nullptr : found->second;
    };

    GltfModel model;
    // ---- materials: each material's effect, its colors and the picture of its color.
    std::unordered_map<std::string, int> materialAt;
    if (const Xml* library = root.child("library_materials")) {
        for (const Xml* material : library->all("material")) {
            GltfMaterial out;
            out.name = material->attribute("name").empty() ? material->attribute("id") : material->attribute("name");
            out.metallic = 0.0f;
            out.roughness = 0.6f;
            const Xml* instance = material->child("instance_effect");
            const Xml* effect = instance ? byId(instance->attribute("url")) : nullptr;
            const Xml* profile = effect ? effect->child("profile_COMMON") : nullptr;
            const Xml* technique = profile ? profile->child("technique") : nullptr;
            const Xml* shading = nullptr;
            for (const char* kind : {"phong", "blinn", "lambert", "constant"}) {
                if (!shading && technique) shading = technique->child(kind);
            }
            auto colorOf = [&](const char* slot, Vec4 fallback) {
                const Xml* holder = shading ? shading->child(slot) : nullptr;
                const Xml* color = holder ? holder->child("color") : nullptr;
                std::vector<float> v = color ? numbersOf(color->text) : std::vector<float>{};
                return v.size() >= 3 ? Vec4{v[0], v[1], v[2], v.size() >= 4 ? v[3] : 1.0f} : fallback;
            };
            out.baseColor = colorOf("diffuse", Vec4{0.8f, 0.8f, 0.8f, 1});
            Vec4 glow = colorOf("emission", Vec4{0, 0, 0, 1});
            out.emissive = Vec3{glow.x, glow.y, glow.z};
            // A picture of its color: the texture names a sampler, which names a surface, which names an image (1.4); or the image itself.
            const Xml* diffuse = shading ? shading->child("diffuse") : nullptr;
            const Xml* texture = diffuse ? diffuse->child("texture") : nullptr;
            if (texture) {
                std::string name = texture->attribute("texture");
                const Xml* image = nullptr;
                for (const Xml* parameter : profile->all("newparam")) {
                    if (parameter->attribute("sid") != name) continue;
                    const Xml* sampler = parameter->child("sampler2D");
                    const Xml* source = sampler ? sampler->child("source") : nullptr;
                    const Xml* instanceImage = sampler ? sampler->child("instance_image") : nullptr;
                    if (instanceImage) image = byId(instanceImage->attribute("url"));
                    std::string surfaceName = source ? source->text : std::string();
                    for (const Xml* other : profile->all("newparam")) {
                        if (other->attribute("sid") != surfaceName) continue;
                        const Xml* surface = other->child("surface");
                        const Xml* from = surface ? surface->child("init_from") : nullptr;
                        if (from) image = byId(from->text);
                    }
                }
                if (!image) image = byId(name);
                const Xml* from = image ? image->child("init_from") : nullptr;
                std::string file = from ? (from->child("ref") ? from->child("ref")->text : from->text) : std::string();
                while (!file.empty() && std::isspace(static_cast<unsigned char>(file.back()))) file.pop_back();
                while (!file.empty() && std::isspace(static_cast<unsigned char>(file.front()))) file.erase(0, 1);
                if (file.rfind("file://", 0) == 0) file = file.substr(7);
                if (!file.empty()) {
                    out.colorMap.file = file;
                    out.baseColorTexture = file;
                    out.baseColor = Vec4{1, 1, 1, out.baseColor.w};
                }
            }
            const Xml* transparency = shading ? shading->child("transparency") : nullptr;
            const Xml* amount = transparency ? transparency->child("float") : nullptr;
            if (amount) {
                float covers = numbersOf(amount->text).empty() ? 1.0f : numbersOf(amount->text)[0];
                if (covers > 0.001f && covers < 0.999f) {
                    out.isBlended = true;
                    out.baseColor.w = covers;
                }
            }
            materialAt[material->attribute("id")] = int(model.materials.size());
            model.materials.push_back(out);
        }
    }

    // ---- geometries: each a list of parts, each part one material's triangles. Its material is a
    // name the node that places it binds to a material.
    struct Geometry {
        std::vector<GltfPrimitive> parts;
        std::vector<std::string> symbols;
    };
    std::unordered_map<std::string, Geometry> geometries;
    if (const Xml* library = root.child("library_geometries")) {
        for (const Xml* geometry : library->all("geometry")) {
            const Xml* mesh = geometry->child("mesh");
            if (!mesh) continue;
            struct Source {
                std::vector<float> values;
                int stride = 1;
            };
            std::unordered_map<std::string, Source> sources;
            for (const Xml* source : mesh->all("source")) {
                Source made;
                if (const Xml* array = source->child("float_array")) made.values = numbersOf(array->text);
                const Xml* common = source->child("technique_common");
                const Xml* accessor = common ? common->child("accessor") : nullptr;
                if (accessor && !accessor->attribute("stride").empty()) made.stride = std::max(std::atoi(accessor->attribute("stride").c_str()), 1);
                sources[source->attribute("id")] = std::move(made);
            }
            // <vertices> names where positions (and sometimes normals) are.
            std::string positionsOf, normalsInVertices;
            std::string verticesId;
            if (const Xml* vertices = mesh->child("vertices")) {
                verticesId = vertices->attribute("id");
                for (const Xml* input : vertices->all("input")) {
                    if (input->attribute("semantic") == "POSITION") positionsOf = withoutHash(input->attribute("source"));
                    if (input->attribute("semantic") == "NORMAL") normalsInVertices = withoutHash(input->attribute("source"));
                }
            }
            Geometry made;
            for (const Xml& primitive : mesh->children) {
                if (primitive.name != "triangles" && primitive.name != "polylist" && primitive.name != "polygons") continue;
                int vertexOffset = 0, normalOffset = -1, uvOffset = -1, stride = 1;
                std::string normalSource = normalsInVertices, uvSource;
                bool normalFromVertex = !normalsInVertices.empty();
                for (const Xml* input : primitive.all("input")) {
                    int offset = std::atoi(input->attribute("offset").c_str());
                    stride = std::max(stride, offset + 1);
                    std::string semantic = input->attribute("semantic");
                    if (semantic == "VERTEX") vertexOffset = offset;
                    else if (semantic == "NORMAL") normalOffset = offset, normalSource = withoutHash(input->attribute("source")), normalFromVertex = false;
                    else if (semantic == "TEXCOORD" && uvOffset < 0) uvOffset = offset, uvSource = withoutHash(input->attribute("source"));
                }
                // The polygons, each its count of corners, each corner `stride` indices.
                std::vector<std::vector<int>> polygons;
                std::vector<int> indices;
                for (const Xml* p : primitive.all("p")) {
                    std::vector<int> some = integersOf(p->text);
                    if (primitive.name == "polygons") polygons.push_back(some);
                    else indices.insert(indices.end(), some.begin(), some.end());
                }
                if (primitive.name == "triangles") {
                    for (size_t i = 0; i + size_t(stride) * 3 <= indices.size(); i += size_t(stride) * 3) polygons.emplace_back(indices.begin() + long(i), indices.begin() + long(i) + stride * 3);
                } else if (primitive.name == "polylist") {
                    const Xml* counts = primitive.child("vcount");
                    size_t at = 0;
                    for (int count : counts ? integersOf(counts->text) : std::vector<int>{}) {
                        size_t length = size_t(count) * size_t(stride);
                        if (at + length > indices.size()) break;
                        polygons.emplace_back(indices.begin() + long(at), indices.begin() + long(at + length));
                        at += length;
                    }
                }
                const Source* positions = sources.count(positionsOf) ? &sources[positionsOf] : nullptr;
                const Source* normals = sources.count(normalSource) ? &sources[normalSource] : nullptr;
                const Source* uvs = sources.count(uvSource) ? &sources[uvSource] : nullptr;
                if (!positions) continue;
                GltfPrimitive part;
                std::map<std::array<int, 3>, uint32_t> corners;
                auto corner = [&](const std::vector<int>& polygon, size_t k) -> uint32_t {
                    int v = polygon[k * size_t(stride) + size_t(vertexOffset)];
                    int n = normalFromVertex ? v : normalOffset >= 0 ? polygon[k * size_t(stride) + size_t(normalOffset)] : -1;
                    int t = uvOffset >= 0 ? polygon[k * size_t(stride) + size_t(uvOffset)] : -1;
                    std::array<int, 3> key{v, n, t};
                    auto found = corners.find(key);
                    if (found != corners.end()) return found->second;
                    auto at = [](const Source* s, int i, int c) { return s && i >= 0 && size_t(i * s->stride + c) < s->values.size() ? s->values[size_t(i * s->stride + c)] : 0.0f; };
                    part.mesh.positions.push_back(Vec3{at(positions, v, 0), at(positions, v, 1), at(positions, v, 2)});
                    part.mesh.normals.push_back(normals ? safeNormalize(Vec3{at(normals, n, 0), at(normals, n, 1), at(normals, n, 2)}) : Vec3{0, 0, 0});
                    part.mesh.uvs.push_back(Vec2{at(uvs, t, 0), 1.0f - at(uvs, t, 1)});
                    uint32_t index = uint32_t(part.mesh.positions.size()) - 1;
                    corners[key] = index;
                    return index;
                };
                for (const std::vector<int>& polygon : polygons) {
                    size_t count = polygon.size() / size_t(stride);
                    for (size_t k = 2; k < count; k++) {
                        part.mesh.indices.push_back(corner(polygon, 0));
                        part.mesh.indices.push_back(corner(polygon, k - 1));
                        part.mesh.indices.push_back(corner(polygon, k));
                    }
                }
                if (part.mesh.indices.empty()) continue;
                if (!normals) {
                    part.mesh.normals.clear();
                    part.mesh.computeNormals();
                }
                made.parts.push_back(std::move(part));
                made.symbols.push_back(primitive.attribute("material"));
            }
            if (!made.parts.empty()) geometries[geometry->attribute("id")] = std::move(made);
        }
    }

    // ---- the scene: nodes, where each is, what each shows.
    const Xml* sceneRef = root.child("scene") ? root.child("scene")->child("instance_visual_scene") : nullptr;
    const Xml* visual = sceneRef ? byId(sceneRef->attribute("url")) : nullptr;
    if (!visual && root.child("library_visual_scenes")) visual = root.child("library_visual_scenes")->child("visual_scene");
    // The model's own root: its axes and its units brought to the engine's.
    GltfNode top;
    top.name = "Scene";
    const Xml* asset = root.child("asset");
    std::string up = asset && asset->child("up_axis") ? asset->child("up_axis")->text : std::string("Y_UP");
    if (up.find("Z_UP") != std::string::npos) top.rotation = Quat(radians(-90.0f), Vec3{1, 0, 0});
    else if (up.find("X_UP") != std::string::npos) top.rotation = Quat(radians(90.0f), Vec3{0, 0, 1});
    if (asset && asset->child("unit") && !asset->child("unit")->attribute("meter").empty()) {
        float meter = std::strtof(asset->child("unit")->attribute("meter").c_str(), nullptr);
        if (meter > 0) top.scale = Vec3{meter, meter, meter};
    }
    model.nodes.push_back(top);

    std::function<void(const Xml&, int, int)> addNode = [&](const Xml& node, int parent, int depth) {
        if (depth > 64) return;
        GltfNode out;
        out.name = node.attribute("name").empty() ? node.attribute("id") : node.attribute("name");
        out.parent = parent;
        // Its transforms, applied in the order written.
        Mat4 place = identity();
        for (const Xml& step : node.children) {
            std::vector<float> v = numbersOf(step.text);
            if (step.name == "matrix" && v.size() == 16) {
                Mat4 m;
                for (int r = 0; r < 4; r++) {
                    for (int c = 0; c < 4; c++) m.columns[c][r] = v[size_t(r * 4 + c)];         // written row by row
                }
                place = place * m;
            } else if (step.name == "translate" && v.size() >= 3) {
                place = place * translation(Vec3{v[0], v[1], v[2]});
            } else if (step.name == "rotate" && v.size() >= 4) {
                place = place * rotation(Quat(radians(v[3]), safeNormalize(Vec3{v[0], v[1], v[2]}, Vec3{0, 1, 0})));
            } else if (step.name == "scale" && v.size() >= 3) {
                place = place * scaling(Vec3{v[0], v[1], v[2]});
            }
        }
        decompose(place, out.translation, out.rotation, out.scale);
        int index = int(model.nodes.size());
        model.nodes.push_back(out);
        model.nodes[size_t(parent)].children.push_back(index);
        // What it shows: each geometry, with its materials bound.
        bool hasMesh = false;
        for (const Xml* instance : node.all("instance_geometry")) {
            auto found = geometries.find(withoutHash(instance->attribute("url")));
            if (found == geometries.end()) continue;
            std::unordered_map<std::string, int> bound;
            const Xml* bind = instance->child("bind_material");
            const Xml* common = bind ? bind->child("technique_common") : nullptr;
            for (const Xml* material : common ? common->all("instance_material") : std::vector<const Xml*>{}) {
                auto at = materialAt.find(withoutHash(material->attribute("target")));
                if (at != materialAt.end()) bound[material->attribute("symbol")] = at->second;
            }
            std::vector<GltfPrimitive> parts = found->second.parts;
            for (size_t p = 0; p < parts.size(); p++) {
                const std::string& symbol = found->second.symbols[p];
                parts[p].material = bound.count(symbol) ? bound[symbol] : materialAt.count(symbol) ? materialAt[symbol] : -1;
            }
            model.meshes.push_back(std::move(parts));
            int target = index;
            if (hasMesh) {
                // A second geometry on one node: on a node of its own under it.
                GltfNode more;
                more.name = out.name;
                more.parent = index;
                target = int(model.nodes.size());
                model.nodes.push_back(more);
                model.nodes[size_t(index)].children.push_back(target);
            }
            model.nodes[size_t(target)].mesh = int(model.meshes.size()) - 1;
            hasMesh = true;
        }
        for (const Xml& child : node.children) {
            if (child.name == "node") addNode(child, index, depth + 1);
            if (child.name == "instance_node") {
                if (const Xml* shared = byId(child.attribute("url"))) addNode(*shared, index, depth + 1);
            }
        }
    };
    if (visual) {
        for (const Xml* node : visual->all("node")) addNode(*node, 0, 0);
    }
    if (model.meshes.empty()) {
        fail(error, "no triangles in the Collada file");
        return std::nullopt;
    }
    return model;
}

std::optional<GltfModel> loadCollada(const std::string& path, std::string* error) {
    std::optional<std::string> text = readWhole(path);
    if (!text) {
        fail(error, "cannot read " + path);
        return std::nullopt;
    }
    std::optional<GltfModel> model = parseCollada(*text, error);
    if (model) model->nodes[0].name = stemOf(path);
    return model;
}

}  // namespace molten
