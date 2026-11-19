// Collision.hpp
// Shape-against-shape tests. Pure geometry: no entities, no velocities.
#pragma once

#include "Math/Math.hpp"

#include <optional>

namespace molten {

enum class ColliderShape : uint8_t { Sphere, Box, Capsule };

/// A collider placed in the world.
struct ShapeInstance {
    ColliderShape shape = ColliderShape::Sphere;
    Vec3 center{0, 0, 0};
    Quat rotation{};
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};    // box
    float radius = 0.5f;                   // sphere and capsule
    float halfHeight = 0.5f;               // capsule: half the distance between the centers of its two caps

    AABB bounds() const;
    /// The ends of a capsule's inner segment.
    Vec3 capsuleA() const { return center - rotate(rotation, Vec3{0, halfHeight, 0}); }
    Vec3 capsuleB() const { return center + rotate(rotation, Vec3{0, halfHeight, 0}); }
};

struct ContactInfo {
    Vec3 normal{0, 1, 0};                  // unit vector pointing from the first shape to the second
    float depth = 0;                       // how far the shapes overlap along the normal
    Vec3 point{0, 0, 0};                   // a representative point of contact
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
