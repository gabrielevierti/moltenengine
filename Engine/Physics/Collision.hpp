// Collision.hpp
// Shape-against-shape tests. Pure geometry: no entities, no velocities.
#pragma once

#include <vector>

#include <memory>

#include <algorithm>

#include "Math/Math.hpp"

#include <optional>

namespace molten {

enum class ColliderShape : uint8_t { Sphere, Box, Capsule, Mesh };

/// The triangles of a mesh collider: ground that is not flat, a track, a building as it
/// is shaped. In the collider's own space, already at its size, and sorted into a grid
/// over x and z so that a wheel or a ball only looks at the few triangles near it.
/// Meant for things that stand still: other shapes collide with it, it does not fall.
/// Its triangles are taken to face the way they wind (counter-clockwise from outside), so
/// that something that has sunk a little under the surface is still pushed back out.
struct MeshShape {
    std::vector<Vec3> points;
    std::vector<uint32_t> indices;
    AABB bounds;
    int cellsX = 1, cellsZ = 1;
    float cellSize = 1;
    std::vector<std::vector<uint32_t>> cells;       // for each square of the grid, the triangles that touch it

    static std::shared_ptr<const MeshShape> build(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, Vec3 scale);
    size_t triangleCount() const { return indices.size() / 3; }
    /// Calls `visit(triangle)` for every triangle that may touch a box given in the mesh's space. A triangle may come more than once.
    template <typename Visit>
    void forEachNear(const AABB& box, Visit&& visit) const {
        if (cells.empty() || box.max.x < bounds.min.x || box.min.x > bounds.max.x || box.max.z < bounds.min.z || box.min.z > bounds.max.z) return;
        int x0 = std::clamp(int((box.min.x - bounds.min.x) / cellSize), 0, cellsX - 1), x1 = std::clamp(int((box.max.x - bounds.min.x) / cellSize), 0, cellsX - 1);
        int z0 = std::clamp(int((box.min.z - bounds.min.z) / cellSize), 0, cellsZ - 1), z1 = std::clamp(int((box.max.z - bounds.min.z) / cellSize), 0, cellsZ - 1);
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) {
                for (uint32_t triangle : cells[size_t(z * cellsX + x)]) visit(triangle);
            }
        }
    }
};

/// A collider placed in the world.
struct ShapeInstance {
    ColliderShape shape = ColliderShape::Sphere;
    Vec3 center{0, 0, 0};
    Quat rotation{};
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};    // box
    float radius = 0.5f;                   // sphere and capsule
    float halfHeight = 0.5f;               // capsule: half the distance between the centers of its two caps
    const MeshShape* mesh = nullptr;       // mesh: its triangles; without them it is nothing

    AABB bounds() const;
    /// The ends of a capsule's inner segment.
    Vec3 capsuleA() const { return center - rotate(rotation, Vec3{0, halfHeight, 0}); }
    Vec3 capsuleB() const { return center + rotate(rotation, Vec3{0, halfHeight, 0}); }
};

struct ContactInfo {
    Vec3 normal{0, 1, 0};                  // unit vector pointing from the first shape to the second
    float depth = 0;                       // how far the shapes overlap along the normal
    Vec3 point{0, 0, 0};                   // a representative point of contact
    // Boxes touch at several points at once (a box lying flat, at four corners): each, with how deep it is.
    Vec3 points[8];
    float depths[8] = {};
    int pointCount = 0;
};

struct ShapeRayHit {
    float distance = 0;
    Vec3 normal{0, 1, 0};
};

/// True if the shapes overlap; `out` then says how to push them apart.
bool collide(const ShapeInstance& a, const ShapeInstance& b, ContactInfo& out);

std::optional<ShapeRayHit> raycastShape(const Ray& ray, const ShapeInstance& shape);

Vec3 closestPointOnSegment(Vec3 a, Vec3 b, Vec3 point);
/// The closest pair of points between segment p1-q1 and segment p2-q2.
void closestPointsBetweenSegments(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, Vec3& c1, Vec3& c2);

}  // namespace molten
