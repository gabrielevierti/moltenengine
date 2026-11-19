#include "Assets/MeshData.hpp"

#include <charconv>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace molten {

AABB MeshData::bounds() const {
    AABB box;
    for (Vec3 p : positions) box.expand(p);
    return box;
}

std::vector<float> MeshData::interleaved() const {
    std::vector<float> out;
    out.reserve(positions.size() * kFloatsPerVertex);
    for (size_t i = 0; i < positions.size(); i++) {
        Vec3 p = positions[i];
        Vec3 n = i < normals.size() ? normals[i] : Vec3{0, 1, 0};
        Vec4 t = i < tangents.size() ? tangents[i] : Vec4{1, 0, 0, 1};
        Vec2 uv = i < uvs.size() ? uvs[i] : Vec2{0, 0};
        out.insert(out.end(), {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z, t.w, uv.x, uv.y});
    }
    return out;
}

void MeshData::computeNormals() {
    normals.assign(positions.size(), Vec3{0, 0, 0});
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        // The cross product's length is twice the triangle's area, which is the weight we want.
        Vec3 faceNormal = simd::cross(positions[b] - positions[a], positions[c] - positions[a]);
        normals[a] += faceNormal;
        normals[b] += faceNormal;
        normals[c] += faceNormal;
    }
    for (Vec3& n : normals) n = safeNormalize(n);
}

void MeshData::computeTangents() {
    size_t count = positions.size();
    if (uvs.size() != count) uvs.assign(count, Vec2{0, 0});
    std::vector<Vec3> tan(count, Vec3{0, 0, 0}), bitan(count, Vec3{0, 0, 0});

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        Vec3 e1 = positions[b] - positions[a], e2 = positions[c] - positions[a];
        // v runs down the image, so flip it: we want the bitangent to point to the top of the texture.
        float du1 = uvs[b].x - uvs[a].x, dv1 = uvs[a].y - uvs[b].y;
        float du2 = uvs[c].x - uvs[a].x, dv2 = uvs[a].y - uvs[c].y;
        float determinant = du1 * dv2 - du2 * dv1;
        if (std::abs(determinant) < 1e-12f) continue;
        float r = 1.0f / determinant;
        Vec3 t = (e1 * dv2 - e2 * dv1) * r;
        Vec3 bt = (e2 * du1 - e1 * du2) * r;
        for (uint32_t v : {a, b, c}) {
            tan[v] += t;
            bitan[v] += bt;
        }
    }

    tangents.resize(count);
    for (size_t i = 0; i < count; i++) {
        Vec3 n = i < normals.size() ? normals[i] : Vec3{0, 1, 0};
        Vec3 t = tan[i] - n * simd::dot(n, tan[i]);                  // Gram-Schmidt: make it perpendicular to n
        if (simd::length_squared(t) < 1e-12f) {
            // No usable texture coordinates here: any direction perpendicular to n will do.
            t = simd::cross(std::abs(n.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, n);
        }
        t = simd::normalize(t);
        float w = simd::dot(simd::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
        tangents[i] = vec4(t, w);
    }
}

// ---------------------------------------------------------------- primitives

MeshData MeshData::cube(Vec3 size, float uvPerUnit) {
    MeshData mesh;
    Vec3 h = size * 0.5f;
    // Each face: its normal and the direction of increasing u.
    const Vec3 faces[6][2] = {
        {{1, 0, 0}, {0, 0, -1}}, {{-1, 0, 0}, {0, 0, 1}},
        {{0, 1, 0}, {1, 0, 0}},  {{0, -1, 0}, {1, 0, 0}},
        {{0, 0, 1}, {1, 0, 0}},  {{0, 0, -1}, {-1, 0, 0}},
    };
    for (const auto& face : faces) {
        Vec3 n = face[0], t = face[1], b = simd::cross(n, t);        // b points to the top of the texture
        uint32_t base = uint32_t(mesh.positions.size());
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};   // counter-clockwise from the bottom left
        // How many times the texture repeats across this face, horizontally and vertically.
        Vec2 repeats{1, 1};
        if (uvPerUnit > 0) repeats = Vec2{simd::dot(simd::abs(t), size), simd::dot(simd::abs(b), size)} * uvPerUnit;
        for (const auto& corner : corners) {
            mesh.positions.push_back((n + t * corner[0] + b * corner[1]) * h);
            mesh.normals.push_back(n);
            mesh.tangents.push_back(vec4(t, 1));
            mesh.uvs.push_back(Vec2{corner[0] * 0.5f + 0.5f, 0.5f - corner[1] * 0.5f} * repeats);
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

MeshData MeshData::plane(float width, float depth, int subdivisions, float uvScale) {
    MeshData mesh;
    int n = std::max(subdivisions, 1);
    for (int j = 0; j <= n; j++) {
        for (int i = 0; i <= n; i++) {
            float u = float(i) / float(n), v = float(j) / float(n);
            mesh.positions.push_back(Vec3{(u - 0.5f) * width, 0, (v - 0.5f) * depth});
            mesh.normals.push_back(Vec3{0, 1, 0});
            mesh.tangents.push_back(Vec4{1, 0, 0, 1});
            mesh.uvs.push_back(Vec2{u * uvScale, v * uvScale});
        }
    }
    uint32_t stride = uint32_t(n + 1);
    for (uint32_t j = 0; j < uint32_t(n); j++) {
        for (uint32_t i = 0; i < uint32_t(n); i++) {
            uint32_t a = j * stride + i, b = a + stride, c = b + 1, d = a + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
    return mesh;
}

MeshData MeshData::heightfield(float width, float depth, int columns, int rows, const std::function<float(float, float)>& height,
                               float uvScale) {
    MeshData mesh;
    columns = std::max(columns, 1);
    rows = std::max(rows, 1);
    float stepX = width / float(columns), stepZ = depth / float(rows);
    for (int j = 0; j <= rows; j++) {
        for (int i = 0; i <= columns; i++) {
            float u = float(i) / float(columns), v = float(j) / float(rows);
            float x = (u - 0.5f) * width, z = (v - 0.5f) * depth;
            // The slope in each direction, measured over one cell on either side: the
            // normal leans away from the uphill side.
            float slopeX = (height(x + stepX, z) - height(x - stepX, z)) / (2.0f * stepX);
            float slopeZ = (height(x, z + stepZ) - height(x, z - stepZ)) / (2.0f * stepZ);
            mesh.positions.push_back(Vec3{x, height(x, z), z});
            mesh.normals.push_back(simd::normalize(Vec3{-slopeX, 1, -slopeZ}));
            mesh.tangents.push_back(vec4(simd::normalize(Vec3{1, slopeX, 0}), 1));
            mesh.uvs.push_back(Vec2{u * uvScale, v * uvScale});
        }
    }
    uint32_t stride = uint32_t(columns + 1);
    for (uint32_t j = 0; j < uint32_t(rows); j++) {
        for (uint32_t i = 0; i < uint32_t(columns); i++) {
            uint32_t a = j * stride + i, b = a + stride, c = b + 1, d = a + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
    return mesh;
}

MeshData MeshData::cone(float radius, float height, int segments) {
    MeshData mesh;
    segments = std::max(segments, 3);
    float h = height * 0.5f;
    // The side leans inward: its normal points out and a little up.
    float slant = std::sqrt(radius * radius + height * height);
    float outward = height / slant, upward = radius / slant;

    // Side: a ring at the base, and one vertex at the point for every segment, because
    // each needs its own normal.
    for (int ring = 0; ring < 2; ring++) {
        for (int s = 0; s <= segments; s++) {
            float u = (float(s) + (ring == 0 ? 0.5f : 0.0f)) / float(segments), theta = u * 2.0f * kPi;
            Vec3 n{std::sin(theta) * outward, upward, std::cos(theta) * outward};
            mesh.positions.push_back(ring == 0 ? Vec3{0, h, 0} : Vec3{std::sin(theta) * radius, -h, std::cos(theta) * radius});
            mesh.normals.push_back(n);
            mesh.tangents.push_back(Vec4{std::cos(theta), 0, -std::sin(theta), 1});
            mesh.uvs.push_back(Vec2{u, float(ring)});
        }
    }
    uint32_t stride = uint32_t(segments + 1);
    for (uint32_t s = 0; s < uint32_t(segments); s++) {
        mesh.indices.insert(mesh.indices.end(), {s, stride + s, stride + s + 1});
    }

    // The base: a center vertex and a ring, facing down.
    uint32_t center = uint32_t(mesh.positions.size());
    mesh.positions.push_back(Vec3{0, -h, 0});
    mesh.normals.push_back(Vec3{0, -1, 0});
    mesh.tangents.push_back(Vec4{1, 0, 0, -1});
    mesh.uvs.push_back(Vec2{0.5f, 0.5f});
    for (int s = 0; s <= segments; s++) {
        float theta = float(s) / float(segments) * 2.0f * kPi;
        float x = std::sin(theta), z = std::cos(theta);
        mesh.positions.push_back(Vec3{x * radius, -h, z * radius});
        mesh.normals.push_back(Vec3{0, -1, 0});
        mesh.tangents.push_back(Vec4{1, 0, 0, -1});
        mesh.uvs.push_back(Vec2{x * 0.5f + 0.5f, z * 0.5f + 0.5f});
    }
    for (uint32_t s = 0; s < uint32_t(segments); s++) {
        mesh.indices.insert(mesh.indices.end(), {center, center + 2 + s, center + 1 + s});
    }
    return mesh;
}

MeshData MeshData::sphere(float radius, int segments, int rings) {
    MeshData mesh;
    segments = std::max(segments, 3);
    rings = std::max(rings, 2);
    for (int r = 0; r <= rings; r++) {
        float v = float(r) / float(rings), phi = v * kPi;             // 0 at the north pole
        // Exact values at the poles, so that every pole vertex sits on the same point.
        float sinPhi = (r == 0 || r == rings) ? 0.0f : std::sin(phi);
        float cosPhi = r == 0 ? 1.0f : r == rings ? -1.0f : std::cos(phi);
        for (int s = 0; s <= segments; s++) {
            float u = float(s) / float(segments), theta = u * 2.0f * kPi;
            Vec3 n{sinPhi * std::sin(theta), cosPhi, sinPhi * std::cos(theta)};
            mesh.positions.push_back(n * radius);
            mesh.normals.push_back(n);
            mesh.tangents.push_back(Vec4{std::cos(theta), 0, -std::sin(theta), 1});
            mesh.uvs.push_back(Vec2{u, v});
        }
    }
    uint32_t stride = uint32_t(segments + 1);
    for (uint32_t r = 0; r < uint32_t(rings); r++) {
        for (uint32_t s = 0; s < uint32_t(segments); s++) {
            uint32_t a = r * stride + s, b = a + stride, c = b + 1, d = a + 1;
            // At a pole one of the two triangles of the quad has no area: leave it out.
            if (r != uint32_t(rings) - 1) mesh.indices.insert(mesh.indices.end(), {a, b, c});
            if (r != 0) mesh.indices.insert(mesh.indices.end(), {a, c, d});
        }
    }
    return mesh;
}

MeshData MeshData::cylinder(float radius, float height, int segments) {
    MeshData mesh;
    segments = std::max(segments, 3);
    float h = height * 0.5f;

    // Side: two rings that share positions with the caps but not normals.
    for (int ring = 0; ring < 2; ring++) {
        for (int s = 0; s <= segments; s++) {
            float u = float(s) / float(segments), theta = u * 2.0f * kPi;
            Vec3 n{std::sin(theta), 0, std::cos(theta)};
            mesh.positions.push_back(Vec3{n.x * radius, ring == 0 ? h : -h, n.z * radius});
            mesh.normals.push_back(n);
            mesh.tangents.push_back(Vec4{std::cos(theta), 0, -std::sin(theta), 1});
            mesh.uvs.push_back(Vec2{u, float(ring)});
        }
    }
    uint32_t stride = uint32_t(segments + 1);
    for (uint32_t s = 0; s < uint32_t(segments); s++) {
        uint32_t a = s, b = a + stride, c = b + 1, d = a + 1;
        mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
    }

    // Caps: a center vertex and a ring, as a triangle fan.
    for (int cap = 0; cap < 2; cap++) {
        float y = cap == 0 ? h : -h, ny = cap == 0 ? 1.0f : -1.0f;
        uint32_t center = uint32_t(mesh.positions.size());
        mesh.positions.push_back(Vec3{0, y, 0});
        mesh.normals.push_back(Vec3{0, ny, 0});
        mesh.tangents.push_back(Vec4{1, 0, 0, ny});
        mesh.uvs.push_back(Vec2{0.5f, 0.5f});
        for (int s = 0; s <= segments; s++) {
            float theta = float(s) / float(segments) * 2.0f * kPi;
            float x = std::sin(theta), z = std::cos(theta);
            mesh.positions.push_back(Vec3{x * radius, y, z * radius});
            mesh.normals.push_back(Vec3{0, ny, 0});
            mesh.tangents.push_back(Vec4{1, 0, 0, ny});
            mesh.uvs.push_back(Vec2{x * 0.5f + 0.5f, z * 0.5f + 0.5f});
        }
        for (uint32_t s = 0; s < uint32_t(segments); s++) {
            uint32_t a = center + 1 + s, b = a + 1;
            if (cap == 0) mesh.indices.insert(mesh.indices.end(), {center, a, b});
            else mesh.indices.insert(mesh.indices.end(), {center, b, a});
        }
    }
    return mesh;
}

// ---------------------------------------------------------------- Wavefront OBJ

namespace {

/// Splits off the next run of non-space characters.
std::string_view nextToken(std::string_view& line) {
    size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string_view::npos) {
        line = {};
        return {};
    }
    size_t end = line.find_first_of(" \t\r", start);
    std::string_view token = line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    line = end == std::string_view::npos ? std::string_view{} : line.substr(end);
    return token;
}

float parseFloat(std::string_view token) {
    return token.empty() ? 0.0f : std::strtof(std::string(token).c_str(), nullptr);
}

int parseInt(std::string_view token) {
    int value = 0;
    std::from_chars(token.data(), token.data() + token.size(), value);
    return value;
}

/// OBJ indices start at 1; negative ones count back from the end. Returns -1 for "absent".
int resolveIndex(std::string_view token, size_t count) {
    if (token.empty()) return -1;
    int index = parseInt(token);
    if (index > 0) return index - 1;
    if (index < 0) return int(count) + index;
    return -1;
}

}  // namespace

std::optional<MeshData> MeshData::parseOBJ(std::string_view text) {
    std::vector<Vec3> filePositions, fileNormals;
    std::vector<Vec2> fileUVs;
    MeshData mesh;
    std::unordered_map<std::string, uint32_t> vertexLookup;       // "v/vt/vn" as written -> output vertex
    bool hasNormals = true;

    while (!text.empty()) {
        size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        std::string_view keyword = nextToken(line);
        if (keyword == "v") {
            float x = parseFloat(nextToken(line)), y = parseFloat(nextToken(line)), z = parseFloat(nextToken(line));
            filePositions.push_back(Vec3{x, y, z});
        } else if (keyword == "vn") {
            float x = parseFloat(nextToken(line)), y = parseFloat(nextToken(line)), z = parseFloat(nextToken(line));
            fileNormals.push_back(Vec3{x, y, z});
        } else if (keyword == "vt") {
            float u = parseFloat(nextToken(line)), v = parseFloat(nextToken(line));
            fileUVs.push_back(Vec2{u, 1.0f - v});                     // OBJ puts v = 0 at the bottom
        } else if (keyword == "f") {
            std::vector<uint32_t> corners;
            for (std::string_view corner = nextToken(line); !corner.empty(); corner = nextToken(line)) {
                auto found = vertexLookup.find(std::string(corner));
                if (found != vertexLookup.end()) {
                    corners.push_back(found->second);
                    continue;
                }
                // A corner is "v", "v/vt", "v//vn" or "v/vt/vn".
                size_t slash1 = corner.find('/');
                size_t slash2 = slash1 == std::string_view::npos ? std::string_view::npos : corner.find('/', slash1 + 1);
                std::string_view vToken = corner.substr(0, slash1);
                std::string_view tToken = slash1 == std::string_view::npos ? std::string_view{}
                    : corner.substr(slash1 + 1, slash2 == std::string_view::npos ? std::string_view::npos : slash2 - slash1 - 1);
                std::string_view nToken = slash2 == std::string_view::npos ? std::string_view{} : corner.substr(slash2 + 1);

                int p = resolveIndex(vToken, filePositions.size());
                int t = resolveIndex(tToken, fileUVs.size());
                int n = resolveIndex(nToken, fileNormals.size());
                if (p < 0 || p >= int(filePositions.size())) continue;

                uint32_t index = uint32_t(mesh.positions.size());
                mesh.positions.push_back(filePositions[size_t(p)]);
                if (t >= 0 && t < int(fileUVs.size())) mesh.uvs.push_back(fileUVs[size_t(t)]);
                else mesh.uvs.push_back(Vec2{0, 0});
                if (n >= 0 && n < int(fileNormals.size())) mesh.normals.push_back(simd::normalize(fileNormals[size_t(n)]));
                else { mesh.normals.push_back(Vec3{0, 1, 0}); hasNormals = false; }
                vertexLookup.emplace(std::string(corner), index);
                corners.push_back(index);
            }
            // A polygon with n corners becomes a fan of n - 2 triangles.
            for (size_t i = 1; i + 1 < corners.size(); i++) {
                mesh.indices.insert(mesh.indices.end(), {corners[0], corners[i], corners[i + 1]});
            }
        }
    }

    if (mesh.indices.empty()) return std::nullopt;
    if (!hasNormals) mesh.computeNormals();
    mesh.computeTangents();
    return mesh;
}

}  // namespace molten
