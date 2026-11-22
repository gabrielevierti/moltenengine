#include "Water/Waves.hpp"

#include "Core/World.hpp"
#include "Rendering/GPUTypes.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

namespace {

// The same numbers the shaders draw the sea with: see ShaderTypes.h, and waterSwell in Mesh.metal.
const float kLengths[MM_WAVE_TRAINS] = {MM_WAVE_LENGTHS};
const float kShares[MM_WAVE_TRAINS] = {MM_WAVE_SHARES};
const float kTurnCos[MM_WAVE_TRAINS] = {MM_WAVE_TURN_COS};
const float kTurnSin[MM_WAVE_TRAINS] = {MM_WAVE_TURN_SIN};

Vec2 windDirection(Vec3 wind) {
    Vec2 along{wind.x, wind.z};
    float speed = simd::length(along);
    return speed > 0.01f ? along / speed : Vec2{0.958f, 0.287f};
}

float windSpeed(Vec3 wind) { return std::max(simd::length(Vec2{wind.x, wind.z}), MM_WAVE_LEAST_WIND); }

}  // namespace

float WaterSea::longest() const {
    float speed = windSpeed(wind);
    return std::clamp(MM_WAVE_LENGTH_PER_WIND * speed * speed, 1.5f, 150.0f) * std::clamp(waveSize, 0.01f, 100.0f);
}

float WaterSea::height() const {
    float speed = windSpeed(wind);
    float high = std::clamp(MM_WAVE_HEIGHT_PER_WIND * speed * speed, 0.02f, 4.0f) * std::max(waveHeight, 0.0f);
    return std::min(high, longest() * MM_WAVE_STEEPEST);
}

Vec3 WaterSea::moved(Vec2 rest, float time) const {
    const float length = longest(), high = height();
    const Vec2 along = windDirection(wind);
    Vec3 moved{0, 0, 0};
    for (int i = 0; i < MM_WAVE_TRAINS; i++) {
        float number = 6.2831853f / (length * kLengths[i]);
        float amplitude = high * kShares[i];
        Vec2 d{along.x * kTurnCos[i] - along.y * kTurnSin[i], along.x * kTurnSin[i] + along.y * kTurnCos[i]};
        float phase = number * simd::dot(d, rest) - std::sqrt(9.81f * number) * time;
        float s = std::sin(phase), c = std::cos(phase);
        moved.y += amplitude * c;
        moved.x -= d.x * amplitude * MM_WAVE_PINCH * s;
        moved.z -= d.y * amplitude * MM_WAVE_PINCH * s;
    }
    return moved;
}

float WaterSea::heightAt(Vec2 place, float time) const {
    // The water over a place rests where the waves, moving it, bring it there: found by trying
    // (a few steps are enough, since no wave moves water as far as its own length).
    Vec2 rest = place;
    Vec3 there{0, 0, 0};
    for (int step = 0; step < 4; step++) {
        there = moved(rest, time);
        rest = place - Vec2{there.x, there.z};
    }
    return moved(rest, time).y;
}

MeshData waterSheetMesh(int around, float nearest, float farthest) {
    around = std::clamp(around, 8, 1024);
    nearest = std::max(nearest, 0.01f);
    const float step = 2.0f * kPi / float(around), grow = 1.0f + step;      // squares, about: as long outward as around
    MeshData sheet;
    auto point = [&](float x, float z, float cell) {
        sheet.positions.push_back(Vec3{x, 0, z});
        sheet.normals.push_back(Vec3{0, 1, 0});
        sheet.tangents.push_back(Vec4{1, 0, 0, 1});
        sheet.uvs.push_back(Vec2{cell, 0});
    };
    // (The middle is a fan, its points as far apart as the first ring is wide: they say so.)
    point(0, 0, nearest);
    int rings = 0;
    for (float radius = nearest; ; radius *= grow) {
        for (int j = 0; j < around; j++) {
            float angle = step * float(j);
            point(std::cos(angle) * radius, std::sin(angle) * radius, rings == 0 ? nearest : radius * step);
        }
        rings++;
        if (radius >= farthest) break;
    }
    // The middle: a fan. Then ring to ring, two triangles a square. All face up.
    for (int j = 0; j < around; j++) {
        uint32_t a = uint32_t(1 + j), b = uint32_t(1 + (j + 1) % around);
        sheet.indices.insert(sheet.indices.end(), {0u, b, a});
    }
    for (int ring = 0; ring + 1 < rings; ring++) {
        uint32_t inner = uint32_t(1 + ring * around), outer = inner + uint32_t(around);
        for (int j = 0; j < around; j++) {
            uint32_t next = uint32_t((j + 1) % around);
            uint32_t a = inner + uint32_t(j), b = inner + next, c = outer + uint32_t(j), d = outer + next;
            sheet.indices.insert(sheet.indices.end(), {a, d, c, a, b, d});
        }
    }
    return sheet;
}

bool isWaterSheet(const AABB& bounds) {
    Vec3 size = bounds.max - bounds.min;
    return size.x > 0 && size.z > 0 && size.y <= std::max(size.x, size.z) * 0.01f + 1e-4f;
}

bool waterHeightAt(World& world, Vec3 position, float& height) {
    // The scene's sea, everywhere: at its level, with the wind's and the weather's waves.
    const SeaSettings& sea = world.sea();
    if (!sea.enabled) return false;
    WaterSea waves;
    waves.wind = world.wind();
    height = sea.level + waves.heightAt(Vec2{position.x, position.z}, float(world.effectsTime));
    return true;
}

}  // namespace molten
