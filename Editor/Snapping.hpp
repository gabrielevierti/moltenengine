// Snapping.hpp
// Putting things exactly where they go in the Scene view: onto the surface under the mouse
// (and turned to stand on it), onto the nearest corner of another model, and lining several up
// or spacing them evenly. The arithmetic only; the Viewport calls it.
#pragma once

#include "Math/Math.hpp"
#include "Physics/Collision.hpp"
#include "Scene/Scene.hpp"

#include <optional>
#include <vector>

namespace molten {

/// A model to snap against: its triangles and where it is.
struct SnapMesh {
    Entity entity;
    Mat4 world = identity();
    const std::vector<Vec3>* positions = nullptr;
    const std::vector<uint32_t>* indices = nullptr;
};

struct SurfaceHit {
    Entity entity;
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};           // of the triangle hit, in the world, toward the ray
    float distance = 0;
};

/// The nearest triangle the ray meets.
std::optional<SurfaceHit> hitSurface(const std::vector<SnapMesh>& meshes, const Ray& ray);

/// The corner of a model nearest the mouse on screen, within `maxPixels`. `viewProjection` takes
/// the world to the screen's -1..1; `screen` is the view's size in points, `mouse` within it.
std::optional<Vec3> nearestVertex(const std::vector<SnapMesh>& meshes, const Mat4& viewProjection, Vec2 screen, Vec2 mouse, float maxPixels = 24);

/// The turn that stands a thing on a surface: its up along the normal, keeping its heading as
/// far as it can.
Quat standOn(Vec3 normal, Quat current);

enum class AlignTo { Least, Middle, Most };
/// Lines the points up along one axis (0 x, 1 y, 2 z): at the least of them, their middle, or the most.
void alignPoints(std::vector<Vec3>& points, int axis, AlignTo to);
/// Spaces the points evenly along an axis, between the two furthest apart; their order kept.
void distributePoints(std::vector<Vec3>& points, int axis);

}  // namespace molten
