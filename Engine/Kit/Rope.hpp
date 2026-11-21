// Rope.hpp
// Ropes and chains, made of links joined end to end: hanging from where their entity is, or
// held between it and another entity (a crate it carries, a post it is tied to). The links
// are bodies that swing, sag, wrap round what they meet and are pulled by what hangs from
// them; one mesh is drawn along them: a tube for a rope, rings for a chain.
//
// On the entity the rope starts at. If it has a RigidBody the rope moves with it (and pulls
// it); otherwise the rope is tied to where it is.
#pragma once

#include "Assets/MeshData.hpp"
#include "Core/World.hpp"

#include <string>
#include <vector>

namespace molten::kit {

struct Rope {
    std::string to;                     // the entity its end is tied to; empty: it hangs free
    float length = 0;                   // meters; 0: as far as `to` is (or 3 hanging free)
    int links = 16;                     // more: smoother, heavier to work out
    float thickness = 0.04f;            // meters across
    bool isChain = false;               // drawn as rings, a little stiffer
    float mass = 2;                     // of the whole rope, kilograms
    float bend = 0;                     // degrees each link bends at most from the next; 0: as a rope does

    void start(World& world, Entity self);
    void fixedUpdate(World& world, Entity self, float dt);
    void lateUpdate(World& world, Entity self, float dt);

    // ---- once made
    std::vector<Entity> parts;
    Entity look;                        // what is drawn
    Entity tiedTo;
    float linkLength = 0;
    /// Where the links are now: from the start to the end, one point at each joint.
    std::vector<Vec3> points(const World& world) const;
};

/// Points from `from` to `to`, `count` apart along a curve `length` long: straight when it is
/// no longer than the distance, sagging under gravity when longer.
std::vector<Vec3> hangingPoints(Vec3 from, Vec3 to, float length, int count);

/// A tube through the points, `radius` thick, with `sides` sides.
MeshData ropeTube(const std::vector<Vec3>& points, float radius, int sides = 8);
/// A ring for each stretch between two points, each turned a quarter from the one before.
MeshData chainRings(const std::vector<Vec3>& points, float thickness);

inline constexpr uint32_t kRopeLayer = 1u << 29;

void linkRope();

}  // namespace molten::kit
