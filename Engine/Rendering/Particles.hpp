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
    float rotation = 0;                     // radians, of its picture
    float spin = 0;                         // radians a second
    float sizeScale = 1;                    // its own size, from the emitter's size variation
    float shade = 1;                        // its own brightness, from the emitter's shade variation
    float frame = 0;                        // the frame of the sheet it starts on
};

/// Component: a particle system, made of the same modules as Unity's. Main and Emission say
/// how many and for how long; Shape where they are born; Velocity, Forces and Noise how they
/// move; Collision what they meet; Color and Size how they look over their life; Renderer how
/// they are drawn. The emitter sits at its entity and emits along `direction` in the entity's
/// own axes; with `space` World a particle is then left where it is, with Local it moves with the entity.
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
    /// Cone: inside a ball of `spawnRadius`, leaving along `direction` within `spread` (what a
    /// file without a shape means). Box, Ring and Line: inside a box, on a circle across the
    /// direction, along a line `boxSize.x` long; they also leave along the direction. Sphere and
    /// Hemisphere: on or in a ball, leaving outward from its middle.
    enum class Shape : uint8_t { Cone, Box, Ring, Sphere, Hemisphere, Line };
    Shape shape = Shape::Cone;
    bool fromShell = false;                 // Sphere, Hemisphere, Box: born on the surface, not inside
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
    float stretch = 0;                      // drawn as a streak this many seconds of its movement long: rain, sparks (0: a dot)
    float turbulence = 0;                   // meters per second of swirling, as in smoke that curls
    float turbulenceSize = 1.0f;            // how large the swirls are, in meters
    // Its picture: a file of Assets with clear around the shape (Particles/Smoke.png), drawn on
    // each particle, tinted by its color. A sheet of frames (columns by rows) is played over its life.
    std::string texture;
    int sheetColumns = 1, sheetRows = 1;
    bool randomRotation = false;            // each starts turned at random (smoke, debris)
    float spinMin = 0, spinMax = 0;         // radians a second each turns at, chosen between these
    bool collides = false;                  // bounces off the colliders of the scene (boxes, spheres, capsules)
    bool bounces = false;                   // off a level floor
    float floorHeight = 0;
    float bounciness = 0.4f;                // how much of its speed a particle keeps when it lands

    // ---- main
    enum class Space : uint8_t { World, Local };
    Space space = Space::World;             // World: left behind as the entity moves (smoke, a trail). Local: carried along (a halo)
    float simulationSpeed = 1.0f;           // 2 plays it twice as fast
    float startDelay = 0;                   // seconds before it starts emitting
    bool prewarm = false;                   // starts as if it had been running already: a fire already burning
    float sizeVariation = 0;                // 0 to 1: each particle a little smaller or larger
    float shadeVariation = 0;               // 0 to 1: each a little darker or brighter
    float inheritVelocity = 0;              // the share of the entity's own movement each is born with
    float ratePerMeter = 0;                 // emitted for every meter the entity moves: a trail

    // ---- velocity and forces over the life
    Vec3 drift{0, 0, 0};                    // meters a second every particle moves along, in the system's space
    float radial = 0;                       // meters a second away from the emitter (below 0: toward it)
    float orbital = 0;                      // turns round the emitter's direction, radians a second
    float buoyancy = 0;                     // meters a second each second upward at birth, cooling to none: hot gas rises
    float turbulenceSpeed = 1.0f;           // how fast the swirls themselves change
    int turbulenceOctaves = 1;              // 1 to 3: swirls inside swirls, as real smoke has

    // ---- collision
    float friction = 0.2f;                  // how much of its speed along a surface a particle loses when it hits
    float lifetimeLoss = 0;                 // the share of its life a particle loses each time it hits
    bool diesOnHit = false;                 // gone when it hits: rain on a roof

    // ---- frames of the picture
    float sheetFps = 0;                     // frames a second; 0: the whole sheet once over its life
    bool randomFrame = false;               // each starts on a frame of its own

    // ---- renderer
    /// Billboard: a square facing the camera. Stretched: a streak `stretch` seconds of its way long.
    /// Horizontal: lying flat (ripples, a glow on the ground). Vertical: upright, turning to the camera round its up axis.
    enum class RenderMode : uint8_t { Billboard, Stretched, Horizontal, Vertical };
    RenderMode renderMode = RenderMode::Billboard;
    float softness = 0;                     // meters: fades out where it meets a surface, instead of a hard line
    float lit = 0;                          // 0: its own color, glowing; 1: lit by the sun and the sky like smoke or dust

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
    Vec3 lastOrigin{0, 0, 0};               // where the emitter was the update before: what it emits is spread along the way
    bool hasLastOrigin = false;
    float distanceAccumulator = 0;          // meters moved, for the rate per meter
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
/// The same for the settings that are words ("texture"). False for a name nobody knows.
bool setEffectText(ParticleEmitter& emitter, const std::string& key, const std::string& value);
/// The settings that the two `particles` lines of a scene file do not hold, as "key numbers" lines; only what is not the usual.
std::vector<std::string> effectExtras(const ParticleEmitter& emitter);

/// Where a particle is in the world: its own position, carried by the entity's matrix if it lives in its space.
inline Vec3 particleWorldPosition(const ParticleEmitter& emitter, const Particle& particle, const Mat4& world) {
    return emitter.space == ParticleEmitter::Space::Local ? transformPoint(world, particle.position) : particle.position;
}

/// The air's velocity at a point and a time: the scene's wind, stronger and weaker in
/// gusts that travel with it. `gustiness` is 0 for a steady wind and 1 for one that
/// swings between nothing and double.
Vec3 windAt(Vec3 wind, float gustiness, Vec3 position, double time);

/// Advances every emitter in the scene. Needs up-to-date Transform::world matrices.
/// `wind` is the scene's wind in meters per second; `time` moves its gusts.
void updateParticles(Scene& scene, float dt, Vec3 wind = Vec3{0, 0, 0}, float gustiness = 0, double time = 0);

}  // namespace molten
