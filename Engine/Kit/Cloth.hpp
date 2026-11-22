// Cloth.hpp
// Cloth, as Unity's: the entity's mesh as a sheet of cloth that hangs, falls, folds, drapes over
// what it meets and flaps in the wind. A flag, a curtain, a cape, a sail, a tablecloth, washing
// on a line. Its points are joined by links along the mesh's edges (it barely stretches) and
// across them (it resists folding, a little or much); some of its points can be pinned, held where
// the entity puts them (a flag's edge on its pole, a curtain's top on its rail), and they go where
// the entity goes. The rest are pushed by gravity, by the air (the scene's wind and its gusts, and
// the still air it falls through) and kept out of the colliders around it.
//
// On an entity with a Mesh Renderer. With the built-in plane (or no mesh) it is a sheet of
// `segments` by `segments` squares, as large as the entity's scale makes the plane; with a model,
// the model's own points. It moves while the game runs.
#pragma once

#include "Assets/MeshData.hpp"
#include "Core/World.hpp"
#include "Physics/Collision.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace molten::kit {

/// What the cloth is like (the component's fields, see Cloth).
struct ClothSettings {
    float stretch = 0.95f;              // 1: it does not stretch at all; lower, as a knit does; 0 as rubber
    float stiffness = 0.2f;             // how it resists folding: 0 silk, 1 card
    float weight = 0.25f;               // kilograms per square meter: 0.05 silk, 0.25 cotton, 0.5 canvas
    float wind = 1.0f;                  // how much the air pushes it: 0 none (it still falls through still air)
    float damping = 0.1f;               // how soon it settles, per second
    float thickness = 0.02f;            // meters it keeps from what it collides with
    float friction = 0.4f;              // how much it holds on what it lies on: 0 slides off, 1 stays
};

/// The cloth itself, without a world: points joined by links, the pinned ones held, the others
/// pushed by gravity and the air and kept out of shapes. The Cloth component runs one; a test can
/// run one alone.
class ClothSimulation {
public:
    /// From `mesh` (in its own space), placed in the world by `place` (the entity's matrix).
    /// `pinned`: "top edge", "top corners", "left edge", "corners" or "none" (see Cloth::pinned).
    /// False if the mesh has no triangles.
    bool build(const MeshData& mesh, const Mat4& place, const std::string& pinned, const ClothSettings& settings);
    /// One step of `dt` seconds. The pinned points go to where `place` puts them now (smoothly from
    /// where it put them at the last step); `air` is the air's velocity at a point of the world.
    void step(float dt, const Mat4& place, Vec3 gravity, const std::function<Vec3(Vec3)>& air,
              const std::vector<ShapeInstance>& obstacles, double time);
    /// The mesh as the cloth is now, in the space `place` puts in the world, its normals worked out
    /// again; with `withBackFaces` its triangles once more facing the other way, so that a material
    /// that is not double-sided shows it from behind too.
    MeshData shape(const Mat4& place, bool withBackFaces) const;

    /// Where each point is, in the world.
    const std::vector<Vec3>& points() const { return positions_; }
    const std::vector<Vec3>& velocities() const { return velocities_; }
    /// Which points are pinned.
    std::vector<size_t> pinned() const;
    /// The box round it, in the world.
    AABB bounds() const;
    /// The longest a link is now against how long it is at rest: 1 if nothing is stretched.
    float mostStretched() const;
    size_t pointCount() const { return positions_.size(); }

    ClothSettings settings;
    int substeps = 16;                  // small steps make stiff cloth without many passes over the links

private:
    struct Link {
        uint32_t a, b;
        float rest;
        bool isBend;                    // across a fold (between the far corners of two triangles), not along an edge
    };
    struct Tether {                     // no point gets further from its nearest pin than it is at rest
        uint32_t point, pin;
        float most;
    };
    MeshData rest_;                     // the mesh as it was: its corners, their uvs, its triangles
    std::vector<uint32_t> pointOf_;     // for each corner of the mesh, its point (corners in one place are one point)
    std::vector<Vec3> restLocal_;       // each point, in the mesh's space
    std::vector<Vec3> positions_, velocities_;
    std::vector<float> inverseMass_;
    std::vector<uint32_t> triangles_;   // points, three by three
    std::vector<Link> links_;
    std::vector<Tether> tethers_;
    Mat4 lastPlace_ = identity();
};

/// Which points of a mesh `pinned` holds ("top edge"...), by index into `points` (in the mesh's
/// space). The top is the mesh's +Y; for a flat sheet lying in x and z (the built-in plane), its
/// -Z edge, which is on top once it is stood up (turned 90 degrees about x).
std::vector<size_t> pinnedPoints(const std::vector<Vec3>& points, const std::string& pinned, float spacing);

struct Cloth {
    /// Which of its points hold, where the entity puts them: "top edge" (a curtain, a flag on a
    /// rope, washing), "top corners" (a hammock, a banner), "left edge" (a flag on its pole),
    /// "corners" (a canopy, a sail), "none" (it falls: a tablecloth, a sheet thrown over something).
    std::string pinned = "top edge";
    int segments = 24;                  // for a plain sheet (the built-in plane): squares along each side
    float stretch = 0.95f;
    float stiffness = 0.2f;
    float weight = 0.25f;
    float wind = 1.0f;
    float damping = 0.1f;
    bool collides = true;               // with the colliders around it (not with itself)
    float thickness = 0.02f;

    void start(World& world, Entity self);
    void fixedUpdate(World& world, Entity self, float dt);
    void lateUpdate(World& world, Entity self, float dt);

    // ---- once made
    std::shared_ptr<ClothSimulation> simulation;
    ClothSettings settingsNow() const;
};

void linkCloth();

}  // namespace molten::kit
