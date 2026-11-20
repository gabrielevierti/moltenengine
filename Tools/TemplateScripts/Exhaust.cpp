// Exhaust.cpp
// What comes out of the pipe. Put it on an entity with a Particle System, under the car,
// where the exhaust ends: the harder the engine works, the more smoke, and the faster it leaves.
// With `isFlame` it is the fire instead: nothing until the throttle is nearly to the floor.
#include "MoltenMetal.hpp"
#include "Motor.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

struct Exhaust {
    float idleRate = 8.0f;              // puffs a second with the engine idling
    float fullRate = 70.0f;             // and flat out
    bool isFlame = false;               // fire from the pipe on full throttle, instead of smoke

    void update(World& world, Entity self, float) {
        ParticleEmitter* emitter = world.scene.get<ParticleEmitter>(self);
        Entity car = kit::vehicleOf(world.scene, self);
        const kit::VehicleBody* body = world.scene.get<kit::VehicleBody>(car);
        if (!emitter || !body) return;
        // The Motor is on the car: how hard it is working.
        const Motor* motor = world.scene.get<Motor>(car);
        float throttle = body->controls.throttle;
        float revs = motor ? saturate((motor->rpm - 800.0f) / 5500.0f) : throttle;
        if (isFlame) {
            // Fire from the pipe on full throttle at speed, and a pop when the valve blows.
            emitter->isEmitting = (throttle > 0.85f && revs > 0.35f) || (motor && motor->blowsOff);
            emitter->rate = fullRate * (0.5f + 0.5f * revs);
        } else {
            emitter->isEmitting = true;
            emitter->rate = idleRate + (fullRate - idleRate) * std::max(throttle, revs * 0.4f);
            emitter->speedMin = 0.6f + 2.5f * throttle;
            emitter->speedMax = 1.2f + 4.0f * throttle;
        }
    }
};

MOLTEN_COMPONENT(Exhaust, MOLTEN_FIELD(idleRate).range(0, 100), MOLTEN_FIELD(fullRate).range(0, 400), MOLTEN_FIELD(isFlame))
