// Waves.hpp
// The sea the wind raises, as things float on it.
//
// The scene's sea (Environment > Water) is drawn by the shaders (Shaders/Mesh.metal, Water.metal)
// with the waves the scene's wind and weather raise. Here is the same sea worked out on the
// processor, from the same numbers (Shaders/Shared/ShaderTypes.h), for whatever needs to know
// how high the water stands at a place: a hull, a buoy, a swimmer.
#pragma once

#include "Assets/MeshData.hpp"
#include "Math/Math.hpp"

namespace molten {

class World;

struct WaterSea {
    Vec3 wind{0, 0, 0};             // meters per second: the scene's
    float waveSize = 1.0f;          // the material's: longer waves the larger
    float waveHeight = 1.0f;        // the material's: times what the wind raises

    /// The longest of its waves, in meters.
    float longest() const;
    /// Its height: the average of the highest third of its waves, trough to crest ("significant height").
    float height() const;
    /// How far the water that rests at a place is moved at a time: x and z toward the crests, y up.
    Vec3 moved(Vec2 rest, float time) const;
    /// How high the water stands over a place, above where it rests. (Not the same as moved().y:
    /// the water standing over a place rests a little way from it.)
    float heightAt(Vec2 place, float time) const;
};

/// The grid that stands in for a sheet of water when it is drawn: rings of points around the
/// eye, close together near it and farther apart the farther out, `around` to a ring. Each
/// point's uv.x is how far apart the points are there, in meters: the shader leaves out of
/// the swell the waves they are too few to carry.
MeshData waterSheetMesh(int around = 192, float nearest = 0.25f, float farthest = 8000.0f);

/// Whether a mesh's shape is a sheet's: flat, so that water on it is drawn with waves that rise.
bool isWaterSheet(const AABB& bounds);

/// The height of the water at a place: the scene's sea (Environment > Water), with its waves as
/// they are now. False if the scene has no sea.
bool waterHeightAt(World& world, Vec3 position, float& height);

}  // namespace molten
