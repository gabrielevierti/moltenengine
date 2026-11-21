// Foliage.cpp
#include "Kit/Foliage.hpp"

#include "Core/Log.hpp"
#include "Game/Component.hpp"
#include "Rendering/Material.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace molten::kit {

void linkFoliage() {}

namespace {
const char kMagic[8] = {'M', 'F', 'O', 'L', 'I', 'A', '1', '\n'};
}

bool Foliage::load(World& world) {
    loadedFrom = places;
    copies.clear();
    needsShowing = true;
    if (places.empty()) return false;
    std::ifstream in(world.assetPath(places), std::ios::binary);
    char magic[8] = {};
    uint32_t count = 0;
    if (!in.read(magic, 8) || std::memcmp(magic, kMagic, 8) != 0 || !in.read(reinterpret_cast<char*>(&count), 4) || count > 4000000) return false;
    std::vector<float> values(size_t(count) * 5);
    if (!in.read(reinterpret_cast<char*>(values.data()), std::streamsize(values.size() * sizeof(float)))) return false;
    copies.resize(count);
    for (size_t i = 0; i < count; i++) {
        const float* v = &values[i * 5];
        copies[i] = FoliageCopy{Vec3{v[0], v[1], v[2]}, v[3], v[4]};
    }
    return true;
}

bool Foliage::save(World& world) const {
    if (places.empty()) return false;
    std::string path = world.assetPath(places);
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), code);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    uint32_t count = uint32_t(copies.size());
    out.write(kMagic, 8);
    out.write(reinterpret_cast<const char*>(&count), 4);
    for (const FoliageCopy& copy : copies) {
        float v[5] = {copy.position.x, copy.position.y, copy.position.z, copy.turn, copy.scale};
        out.write(reinterpret_cast<const char*>(v), sizeof(v));
    }
    if (!out) {
        logError("cannot write the foliage %s", path.c_str());
        return false;
    }
    return true;
}

void Foliage::show(World& world, Entity self) {
    needsShowing = false;
    MeshInstances shown;
    shown.drawDistance = drawDistance;
    shown.places.reserve(copies.size());
    for (const FoliageCopy& copy : copies) {
        shown.places.push_back(compose(copy.position, Quat(copy.turn, Vec3{0, 1, 0}), Vec3{copy.scale, copy.scale, copy.scale}));
    }
    world.scene.add(self, std::move(shown));
}

void Foliage::start(World& world, Entity self) {
    if (loadedFrom != places) load(world);
    show(world, self);
}

void Foliage::update(World& world, Entity self, float) {
    if (loadedFrom != places) load(world);
    if (needsShowing) show(world, self);
    if (MeshInstances* shown = world.scene.get<MeshInstances>(self)) shown->drawDistance = drawDistance;
}

int Foliage::paint(Vec2 middle, float radius, const std::function<std::optional<Vec3>(Vec2)>& ground, float share) {
    if (radius <= 0 || density <= 0) return 0;
    float area = 3.14159265f * radius * radius;
    float wanted = area * density * std::clamp(share, 0.0f, 1.0f);
    int tries = int(wanted);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (unit(random) < wanted - float(tries)) tries++;
    // Copies already near: a fast look through those inside the circle and a spacing more.
    std::vector<Vec2> near;
    float reach = radius + spacing;
    for (const FoliageCopy& copy : copies) {
        Vec2 at{copy.position.x, copy.position.z};
        if (simd::length_squared(at - middle) <= reach * reach) near.push_back(at);
    }
    int put = 0;
    for (int k = 0; k < tries; k++) {
        float angle = unit(random) * 6.2831853f, distance = radius * std::sqrt(unit(random));
        Vec2 at = middle + Vec2{std::cos(angle), std::sin(angle)} * distance;
        bool isCrowded = false;
        for (const Vec2& other : near) {
            if (simd::length_squared(other - at) < spacing * spacing) {
                isCrowded = true;
                break;
            }
        }
        if (isCrowded) continue;
        std::optional<Vec3> found = ground(at);
        if (!found) continue;
        FoliageCopy copy;
        copy.position = *found;
        copy.turn = unit(random) * 6.2831853f;
        copy.scale = smallest + (largest - smallest) * unit(random);
        copies.push_back(copy);
        near.push_back(at);
        put++;
    }
    if (put > 0) needsShowing = true;
    return put;
}

int Foliage::erase(Vec2 middle, float radius) {
    size_t before = copies.size();
    copies.erase(std::remove_if(copies.begin(), copies.end(),
                                [&](const FoliageCopy& copy) { return simd::length_squared(Vec2{copy.position.x, copy.position.z} - middle) <= radius * radius; }),
                 copies.end());
    int taken = int(before - copies.size());
    if (taken > 0) needsShowing = true;
    return taken;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Foliage, MOLTEN_FIELD(places).picks("foliage").help("The file of its copies in Assets: Foliage/Grass.foliage. Made when first painted"),
                 MOLTEN_FIELD(drawDistance).range(0, 2000).help("Meters: copies farther away are not drawn; 0 draws them all"),
                 MOLTEN_FIELD(density).range(0.01f, 50).help("Copies a square meter, where the brush paints"),
                 MOLTEN_FIELD(spacing).range(0, 50).help("Meters: no two copies closer"), MOLTEN_FIELD(smallest).range(0.05f, 10),
                 MOLTEN_FIELD(largest).range(0.05f, 10))
