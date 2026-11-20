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

#include <string>
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
    /// How much the scene's wind carries the particles: 0 for sparks that ignore it,
    /// 1 for smoke that goes wherever the air goes.
    float windInfluence = 0.0f;
    /// Particles emitted at once when the emitter first runs: an explosion, a puff of dust.
    int burstAtStart = 0;

    // ---- where particles are born
    enum class Shape : uint8_t { Sphere, Box, Ring };
    Shape shape = Shape::Sphere;            // Sphere: inside `spawnRadius`. Ring: on a circle of that radius, across the direction
    Vec3 boxSize{1, 1, 1};                  // Box: inside a box this large, in the entity's own axes

    // ---- a third color and size on the way from birth to death
    bool hasMiddle = false;                 // fire: yellow, then orange, then smoke
    float middleAt = 0.3f;                  // at this share of the life
    Vec4 colorMiddle{1, 1, 1, 1};
    float sizeMiddle = 0.15f;

    // ---- bursts that repeat, and effects that end
    float burstEvery = 0;                   // seconds between bursts; 0 for none
    int burstCount = 10;                    // particles in each
    float duration = 0;                     // it emits for this long and stops; 0 for as long as it is there
    bool loops = false;                     // with a duration: starts over when everything it made is gone

    // ---- what the air and the ground do to them
    float turbulence = 0;                   // meters per second of swirling, as in smoke that curls
    float turbulenceSize = 1.0f;            // how large the swirls are, in meters
    bool bounces = false;                   // off a level floor
    float floorHeight = 0;
    float bounciness = 0.4f;                // how much of its speed a particle keeps when it lands

    /// Emits `count` particles at once on the next update.
    void burst(int count) { pendingBurst += count; }

    // State.
    /// Off, it is left alone and shows nothing: an editor turns it off for emitters nobody
    /// is looking at, so that a scene full of them costs nothing while it is being built.
    bool isSimulated = true;
    std::vector<Particle> particles;
    float spawnAccumulator = 0;
    int pendingBurst = 0;
    float burstTimer = 0, age = 0;          // since the last burst, and since it began
    Random random{0};
    bool isSeeded = false;
};

/// The color and the size of a particle at a share of its life from 0 (born) to 1 (gone).
Vec4 particleColor(const ParticleEmitter& emitter, float t);
float particleSize(const ParticleEmitter& emitter, float t);

/// An effect as text, one setting per line, and back: what a `.effect` file holds, so that
/// an effect is made once and put on as many entities, in as many scenes, as use it.
std::string writeEffect(const ParticleEmitter& emitter);
bool readEffect(const std::string& text, ParticleEmitter& emitter);
/// One setting by name ("rate", "colorStart"...) from the numbers that follow it. False for a name nobody knows.
bool setEffectValue(ParticleEmitter& emitter, const std::string& key, const std::vector<float>& values);
/// The settings that the two `particles` lines of a scene file do not hold, as "key numbers" lines; only what is not the usual.
std::vector<std::string> effectExtras(const ParticleEmitter& emitter);

/// Effects to start from: "Fire", "Smoke", "Sparks", "Explosion", "Dust", "Rain", "Snow", "Magic", "Fountain".
const std::vector<std::string>& effectPresets();
ParticleEmitter effectPreset(const std::string& name);

/// The air's velocity at a point and a time: the scene's wind, stronger and weaker in
/// gusts that travel with it. `gustiness` is 0 for a steady wind and 1 for one that
/// swings between nothing and double.
Vec3 windAt(Vec3 wind, float gustiness, Vec3 position, double time);

/// Advances every emitter in the scene. Needs up-to-date Transform::world matrices.
/// `wind` is the scene's wind in meters per second; `time` moves its gusts.
void updateParticles(Scene& scene, float dt, Vec3 wind = Vec3{0, 0, 0}, float gustiness = 0, double time = 0);

}  // namespace molten
