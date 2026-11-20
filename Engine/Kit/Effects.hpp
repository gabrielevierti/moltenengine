// Effects.hpp
// Ready-made effects: fire, smoke, sparks, an explosion, rain, snow. Each is an ordinary
// entity with a particle emitter (and, for fire and explosions, a light), so once made
// it can be changed like anything else: these are starting points, not a closed list.
//
//     effects::create(engine.scene, effects::Preset::Fire, Vec3{0, 0, 0});
//
// The editor offers the same under Create > Effects.
#pragma once

#include "Rendering/Particles.hpp"
#include "Scene/Scene.hpp"

namespace molten::effects {

enum class Preset { Fire, Smoke, Sparks, Explosion, Rain, Snow, Count };

const char* name(Preset preset);

/// The emitter of a preset, without an entity: for code that wants to adjust it first.
ParticleEmitter emitter(Preset preset);

/// Makes the effect at `position` and returns its entity. An explosion goes off when the
/// game runs and removes itself afterwards; the others go on until they are stopped.
Entity create(Scene& scene, Preset preset, Vec3 position);

}  // namespace molten::effects
