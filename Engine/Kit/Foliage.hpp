// Foliage.hpp
// Grass, flowers, trees, rocks: many copies of one model, painted on the ground with a brush in
// the Scene view and drawn together in few draw calls. Beside a Mesh Renderer, whose model and
// material are what is painted. The places are a file of Assets ("Foliage/Grass.foliage").
#pragma once

#include "Core/World.hpp"

#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace molten::kit {

struct FoliageCopy {
    Vec3 position{0, 0, 0};             // in the entity's space
    float turn = 0;                     // radians about the vertical
    float scale = 1;
};

struct Foliage {
    std::string places;                 // the file of its copies in Assets; made when first painted
    float drawDistance = 120.0f;        // meters: copies farther away are not drawn (0: all)
    float density = 1.0f;               // copies a square meter, where the brush paints
    float spacing = 0.4f;               // meters: no two copies closer than this
    float smallest = 0.8f, largest = 1.25f;     // the size of each, chosen between these

    void start(World& world, Entity self);
    void update(World& world, Entity self, float);

    bool load(World& world);
    bool save(World& world) const;
    /// The MeshInstances drawn, made again from the copies.
    void show(World& world, Entity self);

    /// Puts copies in a circle (in the entity's space, across x and z): as many as the density
    /// says for its area, where `ground` finds the ground under a point (none: not there).
    /// Returns how many were put.
    int paint(Vec2 middle, float radius, const std::function<std::optional<Vec3>(Vec2)>& ground, float share = 1.0f);
    /// Takes away the copies in a circle; returns how many.
    int erase(Vec2 middle, float radius);

    std::vector<FoliageCopy> copies;
    std::string loadedFrom;             // the file `copies` came from (none yet: those painted, or nothing)
    bool needsShowing = true;
    std::mt19937 random{1234};
};

void linkFoliage();

}  // namespace molten::kit
