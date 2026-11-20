// TyreSmoke.cpp
// Smoke from a tire that slides. Put it on an entity with a Particle System, under a wheel:
// it smokes as much as the wheel is asked for more than it can hold.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

struct TyreSmoke {
    float most = 90.0f;                 // puffs a second when it slides hardest

    void update(World& world, Entity self, float) {
        ParticleEmitter* emitter = world.scene.get<ParticleEmitter>(self);
        const Transform* own = world.scene.get<Transform>(self);
        if (!emitter || !own) return;
        const kit::Wheel* wheel = world.scene.get<kit::Wheel>(own->parent);
        if (!wheel) return;
        float excess = std::max(std::abs(wheel->state.slipRatio) / 0.16f, std::abs(wheel->state.slipAngle) / 0.2f) - 1.0f;
        float sliding = wheel->state.isOnGround ? saturate(excess) * saturate(wheel->state.load / 1500.0f) : 0.0f;
        // Only at some speed: a parked car spinning a wheel on the spot is another matter.
        float rolling = saturate(std::abs(wheel->state.spin) * wheel->radius / 5.0f);
        emitter->rate = most * sliding * rolling;
        emitter->isEmitting = emitter->rate > 1.0f;
    }
};

MOLTEN_COMPONENT(TyreSmoke, MOLTEN_FIELD(most).range(0, 400))
