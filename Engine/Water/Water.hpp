// Water.hpp
// A sea, a lake: a sheet of water whose surface is an Ocean. Put it on an entity that
// draws a mesh with a material whose shader is Water: the mesh is replaced by the moving
// surface, and the material gives it its color, its small ripples and its shine.
#pragma once

#include "Core/World.hpp"
#include "Water/Ocean.hpp"

#include <memory>

namespace molten {

struct Water {
    float size = 400.0f;                // meters along each side
    int resolution = 128;               // squares along each side of the sheet: more shows smaller waves and costs more
    float windSpeed = 8.0f;             // meters per second: 3 ripples a pond, 9 is a fresh breeze, 20 a gale
    Vec2 windDirection{1.0f, 0.3f};
    float patchSize = 120.0f;           // meters before the pattern of waves repeats
    float choppiness = 1.0f;            // how sharp the crests are
    float waveHeight = 1.0f;            // times what that wind really raises
    float foam = 1.0f;                  // how much white shows where waves break; 0 none

    void update(World& world, Entity self, float dt);

    /// How high the water stands above a place in the world, now.
    float heightAt(const World& world, Entity self, Vec3 position) const;

    std::shared_ptr<Ocean> ocean;
    float time = 0;
    // What the ocean was made with, to make it again when a setting changes.
    float madeWind = -1, madePatch = -1;
    Vec2 madeDirection{0, 0};
};

/// The height of the water at a place, from whichever Water of the scene covers it.
/// False if there is none there.
bool waterHeightAt(World& world, Vec3 position, float& height);

}  // namespace molten
