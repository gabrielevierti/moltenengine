// Terrain.cpp
#include "Kit/Terrain.hpp"

#include "Assets/MeshData.hpp"
#include "Core/Log.hpp"
#include "Game/Component.hpp"
#include "Physics/Collision.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace molten::kit {

void linkTerrain() {}

namespace {
const char kMagic[8] = {'M', 'T', 'E', 'R', 'R', 'A', '1', '\n'};
}

bool Terrain::load(World& world) {
    int wanted = std::clamp(resolution, 2, 1024);
    std::vector<float> read;
    int readResolution = 0;
    if (!heights.empty()) {
        std::ifstream in(world.assetPath(heights), std::ios::binary);
        char magic[8] = {};
        int32_t count = 0;
        if (in.read(magic, 8) && std::memcmp(magic, kMagic, 8) == 0 && in.read(reinterpret_cast<char*>(&count), 4) && count >= 2 && count <= 4096) {
            read.resize(size_t(count + 1) * size_t(count + 1));
            if (in.read(reinterpret_cast<char*>(read.data()), std::streamsize(read.size() * sizeof(float)))) readResolution = count;
            else read.clear();
        }
    }
    samplesResolution = wanted;
    samples.assign(size_t(wanted + 1) * size_t(wanted + 1), 0.0f);
    if (readResolution > 0) {
        // Its resolution may have been changed since: the file is read at the new one.
        int readSide = readResolution + 1;
        for (int z = 0; z <= wanted; z++) {
            for (int x = 0; x <= wanted; x++) {
                float fx = float(x) / float(wanted) * float(readResolution), fz = float(z) / float(wanted) * float(readResolution);
                int x0 = std::min(int(fx), readResolution - 1), z0 = std::min(int(fz), readResolution - 1);
                float tx = fx - float(x0), tz = fz - float(z0);
                auto at = [&](int ix, int iz) { return read[size_t(iz) * size_t(readSide) + size_t(ix)]; };
                float a = at(x0, z0) + (at(x0 + 1, z0) - at(x0, z0)) * tx;
                float b = at(x0, z0 + 1) + (at(x0 + 1, z0 + 1) - at(x0, z0 + 1)) * tx;
                sample(x, z) = a + (b - a) * tz;
            }
        }
    }
    loadedFrom = heights;
    isLoaded = true;
    needsRebuild = true;
    return readResolution > 0;
}

bool Terrain::save(World& world) const {
    if (heights.empty() || samples.empty()) return false;
    std::string path = world.assetPath(heights);
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), code);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    int32_t count = samplesResolution;
    out.write(kMagic, 8);
    out.write(reinterpret_cast<const char*>(&count), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), std::streamsize(samples.size() * sizeof(float)));
    if (!out) {
        logError("cannot write the terrain %s", path.c_str());
        return false;
    }
    return true;
}

float Terrain::heightAt(Vec2 local) const {
    if (samples.empty()) return 0;
    float fx = (local.x / size.x + 0.5f) * float(samplesResolution), fz = (local.y / size.y + 0.5f) * float(samplesResolution);
    fx = std::clamp(fx, 0.0f, float(samplesResolution));
    fz = std::clamp(fz, 0.0f, float(samplesResolution));
    int x0 = std::min(int(fx), samplesResolution - 1), z0 = std::min(int(fz), samplesResolution - 1);
    float tx = fx - float(x0), tz = fz - float(z0);
    // On the same two triangles the mesh is made of, so that what stands on it stands on what is seen.
    float h00 = sample(x0, z0), h10 = sample(x0 + 1, z0), h01 = sample(x0, z0 + 1), h11 = sample(x0 + 1, z0 + 1);
    if (tx + tz <= 1) return h00 + (h10 - h00) * tx + (h01 - h00) * tz;
    return h11 + (h01 - h11) * (1 - tx) + (h10 - h11) * (1 - tz);
}

Vec3 Terrain::normalAt(Vec2 local) const {
    float step = std::max(size.x, size.y) / float(std::max(samplesResolution, 1));
    float dx = heightAt(local + Vec2{step, 0}) - heightAt(local - Vec2{step, 0});
    float dz = heightAt(local + Vec2{0, step}) - heightAt(local - Vec2{0, step});
    return simd::normalize(Vec3{-dx, 2 * step, -dz});
}

MeshData Terrain::makeMesh(const std::vector<float>& samples, int resolution, Vec2 size, float uvTiling) {
    MeshData mesh;
    int side = resolution + 1;
    if (resolution < 1 || samples.size() < size_t(side) * size_t(side)) return mesh;
    auto at = [&](int x, int z) { return samples[size_t(std::clamp(z, 0, resolution)) * size_t(side) + size_t(std::clamp(x, 0, resolution))]; };
    Vec2 cell{size.x / float(resolution), size.y / float(resolution)};
    float tiling = std::max(uvTiling, 0.01f);
    mesh.positions.reserve(size_t(side) * size_t(side));
    for (int z = 0; z <= resolution; z++) {
        for (int x = 0; x <= resolution; x++) {
            Vec3 position{-size.x * 0.5f + cell.x * float(x), at(x, z), -size.y * 0.5f + cell.y * float(z)};
            mesh.positions.push_back(position);
            float dx = (at(x + 1, z) - at(x - 1, z)) / (2 * cell.x), dz = (at(x, z + 1) - at(x, z - 1)) / (2 * cell.y);
            mesh.normals.push_back(simd::normalize(Vec3{-dx, 1, -dz}));
            mesh.uvs.push_back(Vec2{position.x / tiling, position.z / tiling});
        }
    }
    mesh.indices.reserve(size_t(resolution) * size_t(resolution) * 6);
    for (int z = 0; z < resolution; z++) {
        for (int x = 0; x < resolution; x++) {
            uint32_t a = uint32_t(z * side + x), b = a + 1, c = a + uint32_t(side), d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});        // counter-clockwise from above
        }
    }
    mesh.computeTangents();
    return mesh;
}

void Terrain::rebuild(World& world, Entity self) {
    needsRebuild = false;
    if (samples.empty()) return;
    MeshData mesh = makeMesh(samples, samplesResolution, size, uvTiling);
    if (!world.scene.has<MeshRenderer>(self)) world.scene.add(self, MeshRenderer{});
    world.showMesh(self, mesh);
    if (!hasCollider) return;
    if (!world.scene.has<Collider>(self)) world.scene.add(self, Collider{});
    Collider* collider = world.scene.get<Collider>(self);
    const Transform* transform = world.scene.get<Transform>(self);
    Vec3 scale = transform ? transform->scale : Vec3{1, 1, 1};
    collider->shape = ColliderShape::Mesh;
    collider->isConvex = false;
    collider->meshName.clear();
    collider->mesh = MeshShape::build(mesh.positions, mesh.indices, scale);
    collider->convex = nullptr;
    collider->meshScale = scale;
}

void Terrain::start(World& world, Entity self) {
    if (!isLoaded || loadedFrom != heights || samplesResolution != std::clamp(resolution, 2, 1024)) load(world);
    rebuild(world, self);
}

void Terrain::update(World& world, Entity self, float) {
    if (!isLoaded || loadedFrom != heights || samplesResolution != std::clamp(resolution, 2, 1024)) load(world);
    if (needsRebuild) rebuild(world, self);
}

std::optional<Vec3> Terrain::raycast(const World& world, Entity self, Vec3 origin, Vec3 direction, float maxDistance) const {
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform || samples.empty()) return std::nullopt;
    Mat4 toLocal = simd::inverse(transform->world);
    Vec3 from = xyz(toLocal * Vec4{origin.x, origin.y, origin.z, 1});
    Vec3 along = xyz(toLocal * Vec4{direction.x, direction.y, direction.z, 0});
    float length = simd::length(along);
    if (length < 1e-8f) return std::nullopt;
    along /= length;
    float reach = maxDistance * length;
    // Marched in steps of half a square, then the crossing found more closely between two steps.
    float step = std::min(size.x, size.y) / float(std::max(samplesResolution, 1)) * 0.5f;
    auto above = [&](float t) {
        Vec3 p = from + along * t;
        return p.y - heightAt(Vec2{p.x, p.z});
    };
    auto inside = [&](float t) {
        Vec3 p = from + along * t;
        return std::abs(p.x) <= size.x * 0.5f && std::abs(p.z) <= size.y * 0.5f;
    };
    float t = 0, before = above(0);
    for (int i = 0; i < 200000 && t < reach; i++) {
        float next = t + step;
        float now = above(next);
        if (before > 0 && now <= 0 && inside(next)) {
            float low = t, high = next;
            for (int k = 0; k < 24; k++) {
                float middle = (low + high) * 0.5f;
                (above(middle) > 0 ? low : high) = middle;
            }
            Vec3 hit = from + along * high;
            return xyz(transform->world * Vec4{hit.x, hit.y, hit.z, 1});
        }
        t = next;
        before = now;
    }
    return std::nullopt;
}

void Terrain::sculpt(Vec2 local, float radius, float amount, TerrainBrush brush, float level) {
    if (samples.empty() || radius <= 0) return;
    Vec2 cell{size.x / float(samplesResolution), size.y / float(samplesResolution)};
    int x0 = std::max(0, int(std::floor((local.x - radius + size.x * 0.5f) / cell.x)));
    int x1 = std::min(samplesResolution, int(std::ceil((local.x + radius + size.x * 0.5f) / cell.x)));
    int z0 = std::max(0, int(std::floor((local.y - radius + size.y * 0.5f) / cell.y)));
    int z1 = std::min(samplesResolution, int(std::ceil((local.y + radius + size.y * 0.5f) / cell.y)));
    std::vector<float> before = samples;
    auto old = [&](int x, int z) { return before[size_t(std::clamp(z, 0, samplesResolution)) * size_t(side()) + size_t(std::clamp(x, 0, samplesResolution))]; };
    for (int z = z0; z <= z1; z++) {
        for (int x = x0; x <= x1; x++) {
            Vec2 at{-size.x * 0.5f + cell.x * float(x), -size.y * 0.5f + cell.y * float(z)};
            float distance = simd::length(at - local) / radius;
            if (distance >= 1) continue;
            float falloff = 0.5f + 0.5f * std::cos(distance * 3.14159265f);     // soft at the edge
            float& height = sample(x, z);
            switch (brush) {
            case TerrainBrush::Raise: height += amount * falloff; break;
            case TerrainBrush::Lower: height -= amount * falloff; break;
            case TerrainBrush::Smooth: {
                float around = (old(x - 1, z) + old(x + 1, z) + old(x, z - 1) + old(x, z + 1) + old(x, z) * 4) / 8;
                height += (around - height) * std::clamp(amount * falloff, 0.0f, 1.0f);
                break;
            }
            case TerrainBrush::Flatten: height += (level - height) * std::clamp(amount * falloff, 0.0f, 1.0f); break;
            }
        }
    }
    needsRebuild = true;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Terrain, MOLTEN_FIELD(heights).picks("terrain").help("The file of its heights in Assets: Terrains/Island.terrain. Made when it is first shaped"),
                 MOLTEN_FIELD(size).help("Meters across x and z"), MOLTEN_FIELD(resolution).range(2, 1024).help("Squares along each side"),
                 MOLTEN_FIELD(uvTiling).range(0.1f, 200).help("Meters per repeat of its material's texture"), MOLTEN_FIELD(hasCollider))
