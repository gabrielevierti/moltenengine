// Spline.hpp
// Curves through points: a road, a rail, the way a platform or a camera goes.
//
//   Spline          the curve. Its points are its children, in the order of the numbers in
//                   their names ("Point 1", "Point 2"... "Point 12"): moved in the Scene view
//                   like anything else. Smooth through every point (Catmull-Rom), open or closed.
//   SplineFollower  moves its entity along a Spline at a speed: round and round, back and
//                   forth, or once to the end
//   SplineRoad      on the Spline's own entity: a strip of road along the curve, with a collider
//                   to drive on. Seen in the Scene view while it is edited.
#pragma once

#include "Core/World.hpp"

#include <string>
#include <vector>

namespace molten::kit {

/// The curve itself, as points close together, measured along its length.
struct SplinePath {
    std::vector<Vec3> points;           // in the world
    std::vector<float> distances;       // from the start to each point
    bool isClosed = false;

    /// Through `controls` in order, `perSegment` points between two of them.
    void build(const std::vector<Vec3>& controls, bool closed, int perSegment = 16);
    float length() const { return distances.empty() ? 0.0f : distances.back(); }
    /// The point `distance` meters from the start: round again when closed, held at the ends when not.
    Vec3 pointAt(float distance) const;
    /// Which way the curve goes there (of length 1).
    Vec3 directionAt(float distance) const;
    /// How far along is the point of the curve nearest `point`.
    float closestDistance(Vec3 point) const;
};

/// A point of a Catmull-Rom curve (centripetal) between p1 (t = 0) and p2 (t = 1).
Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t);

struct Spline {
    bool closed = false;                // the end joins the start: a circuit
    int smoothness = 16;                // points made between two of its points

    void start(World& world, Entity self) { refresh(world, self); }
    void update(World& world, Entity self, float) { refresh(world, self); }

    /// Reads its points again. True if the curve changed since the last time.
    bool refresh(World& world, Entity self);
    /// Its points, in order (the entities, then where they are in the world).
    static std::vector<Entity> pointsOf(World& world, Entity self);

    SplinePath path;
    uint64_t signature = 0;             // of what the curve was made from
};

/// The Spline on the entity named `name`, with its curve up to date (and the entity, if asked).
Spline* findSpline(World& world, const std::string& name, Entity* entity = nullptr);

struct SplineFollower {
    std::string spline;                 // the name of the entity with the Spline
    float speed = 5.0f;                 // meters a second
    float distance = 0.0f;              // where it is along it: where it starts
    int ending = 0;                     // at the end: 0 round again (or from the start), 1 back, 2 stop
    bool facesForward = true;           // turned the way it goes
    Vec3 offset{0, 0, 0};               // from the curve, turned with it: (2, 0, 0) is two meters to its right

    void update(World& world, Entity self, float dt);

    float direction = 1.0f;             // -1 on the way back
    bool hasEnded = false;
};

struct SplineRoad {
    float width = 8.0f;
    float uvLength = 8.0f;              // meters of road per repeat of its texture
    float lift = 0.02f;                 // above the curve, so that it is not inside the ground it lies on
    bool hasCollider = true;            // to drive on

    void start(World& world, Entity self) { rebuild(world, self); }
    void update(World& world, Entity self, float);

    /// Makes the road again from the Spline on the same entity. False if there is none, or no curve.
    bool rebuild(World& world, Entity self);
    /// The road as a mesh in the entity's space (for the editor's view and tests).
    static MeshData makeMesh(const SplinePath& path, const Mat4& toLocal, float width, float uvLength, float lift);

    uint64_t builtFrom = 0;             // the Spline's signature it was made from
};

void linkSplines();

}  // namespace molten::kit
