// Liquid.hpp
// Liquid made of particles: water poured into a glass, a tank that bursts, a puddle that
// spreads. Each particle is a drop that keeps its distance from its neighbors (liquids do
// not squeeze), flows with them, and stops at the scene's colliders.
//
// The method is "position based fluids": each step the drops are moved by gravity, and
// then nudged, a few times over, until nowhere are there more of them in a place than a
// liquid at rest would have there.
//
// Put it on an entity with a ParticleEmitter: the emitter's look (size, color) is how the
// drops are drawn. For the most liquid-looking result give it a size about three times
// `dropRadius`, no additive blending, and a color with some transparency.
#pragma once

#include "Core/World.hpp"

#include <vector>

namespace molten {

struct Liquid {
    Vec3 fill{0.6f, 0.6f, 0.6f};        // a block of liquid this size, around the entity, when the game starts; zero for none
    float pourRate = 0;                 // drops per second poured from the entity, along its -y
    float pourSpeed = 1.5f;
    float pourWidth = 0.08f;            // the radius of the stream
    int maxDrops = 2000;
    float dropRadius = 0.04f;           // meters: smaller is finer and needs more drops for the same amount
    float viscosity = 0.02f;            // 0 water to 0.5 honey
    float stickiness = 0.1f;            // how much it clings to what it touches, 0 to 1

    void start(World& world, Entity self);
    void fixedUpdate(World& world, Entity self, float dt);
    void update(World& world, Entity self, float dt);

    /// Adds a drop.
    void add(Vec3 position, Vec3 velocity = Vec3{0, 0, 0});
    size_t dropCount() const { return positions.size(); }

    // ---- the drops
    std::vector<Vec3> positions, velocities;
    float pourAccumulator = 0;
    bool hasStarted = false;

private:
    std::vector<Vec3> predicted_, moves_;
    std::vector<float> pressure_;
    std::vector<int> cellStart_, cellNext_;
    uint32_t pourCount_ = 0;
};

}  // namespace molten
