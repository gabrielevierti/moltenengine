#include "Water/Water.hpp"

#include "Assets/MeshData.hpp"
#include "Game/Component.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

void Water::update(World& world, Entity self, float dt) {
    if (!ocean || madeWind != windSpeed || madePatch != patchSize || madeDirection.x != windDirection.x || madeDirection.y != windDirection.y) {
        OceanSettings settings;
        settings.windSpeed = windSpeed;
        settings.windDirection = windDirection;
        settings.patchSize = patchSize;
        ocean = std::make_shared<Ocean>(settings);
        madeWind = windSpeed;
        madePatch = patchSize;
        madeDirection = windDirection;
    }
    time += dt;
    ocean->update(time);

    // The sheet: a grid of points, each moved as the ocean moves the water there.
    const int cells = std::clamp(resolution, 2, 400), points = cells + 1;
    const float step = std::max(size, 1.0f) / float(cells), half = std::max(size, 1.0f) * 0.5f;
    MeshData sheet;
    sheet.positions.reserve(size_t(points) * size_t(points));
    sheet.normals.reserve(sheet.positions.capacity());
    sheet.uvs.reserve(sheet.positions.capacity());
    for (int j = 0; j < points; j++) {
        for (int i = 0; i < points; i++) {
            float x = float(i) * step - half, z = float(j) * step - half;
            Vec3 moved = ocean->displacementAt(x, z);
            // At the rim the water is still, so that the sheet meets its shore.
            float rim = std::min({float(i), float(j), float(cells - i), float(cells - j)}) / 4.0f;
            float calm = std::clamp(rim, 0.0f, 1.0f);
            sheet.positions.push_back(Vec3{x + moved.x * choppiness * calm, moved.y * waveHeight * calm, z + moved.z * choppiness * calm});
            // Straight up: the water's shader reads the slope of every wave, pixel by pixel, from the
            // same sea (showOcean below), sharper than the sheet's points could hold it.
            sheet.normals.push_back(Vec3{0, 1, 0});
            // The water shader reads foam from here: 2 and above, so that it is not taken for a picture's coordinates.
            sheet.uvs.push_back(Vec2{2.0f + std::clamp(ocean->foamAt(x, z) * foam * calm, 0.0f, 1.0f), 0.0f});
        }
    }
    sheet.indices.reserve(size_t(cells) * size_t(cells) * 6);
    for (int j = 0; j < cells; j++) {
        for (int i = 0; i < cells; i++) {
            uint32_t a = uint32_t(j * points + i), b = a + 1, c = a + uint32_t(points), d = c + 1;
            sheet.indices.insert(sheet.indices.end(), {a, c, b, b, c, d});
        }
    }
    world.showMesh(self, sheet);
    const Transform* transform = world.scene.get<Transform>(self);
    Vec3 origin = transform ? transform->worldPosition() : Vec3{0, 0, 0};
    world.showOcean(*ocean, Vec2{origin.x, origin.z});
}

float Water::heightAt(const World& world, Entity self, Vec3 position) const {
    const Transform* transform = world.scene.get<Transform>(self);
    Vec3 origin = transform ? transform->worldPosition() : Vec3{0, 0, 0};
    if (!ocean) return origin.y;
    float half = std::max(size, 1.0f) * 0.5f;
    float x = position.x - origin.x, z = position.z - origin.z;
    float rim = std::min({x + half, z + half, half - x, half - z}) / (std::max(size, 1.0f) / float(std::clamp(resolution, 2, 400)) * 4.0f);
    return origin.y + ocean->heightAt(x, z) * waveHeight * std::clamp(rim, 0.0f, 1.0f);
}

bool waterHeightAt(World& world, Vec3 position, float& height) {
    bool found = false;
    world.scene.each<Water, Transform>([&](Entity entity, Water& water, Transform& transform) {
        if (found || !transform.isActiveInHierarchy) return;
        Vec3 origin = transform.worldPosition();
        float half = std::max(water.size, 1.0f) * 0.5f;
        if (std::abs(position.x - origin.x) > half || std::abs(position.z - origin.z) > half) return;
        height = water.heightAt(world, entity, position);
        found = true;
    });
    return found;
}

MOLTEN_COMPONENT(Water, MOLTEN_FIELD(size).range(4, 20000).help("Meters along each side"),
                 MOLTEN_FIELD(resolution).range(8, 400).help("Squares along each side of the sheet: more shows smaller waves and costs more"),
                 MOLTEN_FIELD(windSpeed).range(0.5f, 30).help("Meters per second: 3 ripples a pond, 9 is a fresh breeze, 20 a gale"), MOLTEN_FIELD(windDirection),
                 MOLTEN_FIELD(patchSize).range(10, 2000).help("Meters before the pattern of waves repeats"), MOLTEN_FIELD(choppiness).range(0, 2),
                 MOLTEN_FIELD(waveHeight).range(0, 4).help("Times what that wind really raises"), MOLTEN_FIELD(foam).range(0, 4))

}  // namespace molten
