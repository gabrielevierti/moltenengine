#include "Rendering/Particles.hpp"

namespace molten {

void updateParticles(Scene& scene, float dt) {
    scene.each<ParticleEmitter, Transform>([&](Entity entity, ParticleEmitter& emitter, Transform& transform) {
        if (!emitter.isSeeded) {
            emitter.random = Random(uint64_t(entity.index) * 7919u + 17u);     // a different pattern per emitter
            emitter.isSeeded = true;
        }

        // Age and move the living particles; drop the dead ones by swapping with the last.
        for (size_t i = 0; i < emitter.particles.size();) {
            Particle& particle = emitter.particles[i];
            particle.age += dt;
            if (particle.age >= particle.lifetime) {
                particle = emitter.particles.back();
                emitter.particles.pop_back();
                continue;
            }
            particle.velocity += emitter.gravity * dt;
            particle.velocity *= 1.0f / (1.0f + emitter.drag * dt);
            particle.position += particle.velocity * dt;
            i++;
        }

        // Spawn. The accumulator carries fractions of a particle from frame to frame.
        int toSpawn = emitter.pendingBurst;
        emitter.pendingBurst = 0;
        if (emitter.isEmitting && emitter.rate > 0) {
            emitter.spawnAccumulator += emitter.rate * dt;
            int whole = int(emitter.spawnAccumulator);
            emitter.spawnAccumulator -= float(whole);
            toSpawn += whole;
        }

        Vec3 origin = transform.worldPosition();
        Vec3 axis = safeNormalize(transformDirection(transform.world, emitter.direction));
        for (int i = 0; i < toSpawn && int(emitter.particles.size()) < emitter.maxParticles; i++) {
            Particle particle;
            particle.position = origin + emitter.random.insideUnitSphere() * emitter.spawnRadius;
            Vec3 direction = emitter.random.inCone(axis, emitter.spread);
            particle.velocity = direction * emitter.random.range(emitter.speedMin, emitter.speedMax);
            particle.lifetime = emitter.random.range(emitter.lifetimeMin, emitter.lifetimeMax);
            emitter.particles.push_back(particle);
        }
    });
}

}  // namespace molten
