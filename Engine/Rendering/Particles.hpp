// Particles.hpp
// Particle emitters simulated on the CPU and drawn as camera-facing quads with one
// instanced draw call per emitter.
//
// CPU particles are the right default for gameplay effects: a few hundred sparks, puffs
// of dust, a burst when something is collected. They can collide with game logic and
// cost almost nothing at these counts. (For hundreds of thousands of particles, move the
// simulation into a compute shader, as the Compute Shaders chapter of the site does.)
#pragma once

#include "Scene/Scene.hpp"

#include <vector>

namespace molten {

struct Particle {
    Vec3 position{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    float age = 0;
    float lifetime = 1;
};

/// Component. The emitter sits at its entity's position and emits along `direction`
/// in the entity's local space. Particles then live in world space.
struct ParticleEmitter {
    // What to emit.
    float rate = 20.0f;                     // particles per second while isEmitting
    float lifetimeMin = 0.6f, lifetimeMax = 1.2f;
    float speedMin = 1.0f, speedMax = 2.0f;
    Vec3 direction{0, 1, 0};
    float spread = radians(25.0f);          // half-angle of the emission cone; pi emits in every direction
    float spawnRadius = 0.0f;               // particles start inside a sphere of this radius
    Vec3 gravity{0, -2.0f, 0};
    float drag = 0.5f;

    // How a particle looks over its life.
    float sizeStart = 0.15f, sizeEnd = 0.0f;
    Vec4 colorStart{1, 1, 1, 1}, colorEnd{1, 1, 1, 0};     // linear RGB; values above 1 feed the bloom
    bool isAdditive = true;

    bool isEmitting = true;
    int maxParticles = 512;

    /// Emits `count` particles at once on the next update.
    void burst(int count) { pendingBurst += count; }

    // State.
    std::vector<Particle> particles;
    float spawnAccumulator = 0;
    int pendingBurst = 0;
    Random random{0};
    bool isSeeded = false;
};

/// Advances every emitter in the scene. Needs up-to-date Transform::world matrices.
void updateParticles(Scene& scene, float dt);

}  // namespace molten
