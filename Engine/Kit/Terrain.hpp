// Terrain.hpp
// Ground of hills and valleys: a grid of heights, drawn as one mesh, solid to drive and walk
// on, shaped in the editor with brushes (raise, lower, smooth, flatten). Its heights are a
// file of Assets ("Terrains/Island.terrain"), written when a brush stroke ends.
#pragma once

#include "Core/World.hpp"

#include <optional>
#include <string>
#include <vector>

namespace molten::kit {

enum class TerrainBrush : int { Raise = 0, Lower = 1, Smooth = 2, Flatten = 3 };

struct Terrain {
    std::string heights;                // the file of its heights in Assets; empty: flat, until it is shaped
    Vec2 size{200, 200};                // meters, across x and z, centered on the entity
    int resolution = 128;               // squares along each side
    float uvTiling = 8.0f;              // meters per repeat of its material's texture
    bool hasCollider = true;

    void start(World& world, Entity self);
    void update(World& world, Entity self, float);

    /// Its heights, read from the file (or flat), at its resolution. False if there was nothing to read.
    bool load(World& world);
    /// Writes its heights to the file it names. False if it cannot.
    bool save(World& world) const;
    /// The mesh and the collider made again from its heights.
    void rebuild(World& world, Entity self);
    static MeshData makeMesh(const std::vector<float>& samples, int resolution, Vec2 size, float uvTiling);

    /// The height of the ground at a point of the entity's space (x and z), between the samples.
    float heightAt(Vec2 local) const;
    Vec3 normalAt(Vec2 local) const;
    /// Where a ray in the world first meets the ground, if it does.
    std::optional<Vec3> raycast(const World& world, Entity self, Vec3 origin, Vec3 direction, float maxDistance = 5000) const;
    /// Shapes the ground around a point of the entity's space. `amount` is meters (raise and
    /// lower) or a share of the way (smooth and flatten) at the middle, fading to nothing at `radius`.
    /// Flatten brings the heights to `level`.
    void sculpt(Vec2 local, float radius, float amount, TerrainBrush brush, float level = 0);

    std::vector<float> samples;         // (resolution + 1) squared, row by row along x, rows along z
    int samplesResolution = 0;
    std::string loadedFrom = "\n";
    bool needsRebuild = true;
    bool isLoaded = false;

    int side() const { return samplesResolution + 1; }
    float& sample(int x, int z) { return samples[size_t(z) * size_t(side()) + size_t(x)]; }
    float sample(int x, int z) const { return samples[size_t(z) * size_t(side()) + size_t(x)]; }
};

void linkTerrain();

}  // namespace molten::kit
