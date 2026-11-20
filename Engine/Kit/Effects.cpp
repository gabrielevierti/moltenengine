#include "Kit/Effects.hpp"

#include "Kit/Kit.hpp"
#include "Scene/Components.hpp"

namespace molten::effects {

const char* name(Preset preset) {
    switch (preset) {
        case Preset::Fire: return "Fire";
        case Preset::Smoke: return "Smoke";
        case Preset::Sparks: return "Sparks";
        case Preset::Explosion: return "Explosion";
        case Preset::Rain: return "Rain";
        case Preset::Snow: return "Snow";
        case Preset::Count: break;
    }
    return "Effect";
}

ParticleEmitter emitter(Preset preset) {
    ParticleEmitter e;
    switch (preset) {
        case Preset::Fire:
            // Bright and yellow where it is born, red and gone a moment later; it rises
            // because hot air does, which here is gravity pointing up.
            e.rate = 70;
            e.lifetimeMin = 0.5f, e.lifetimeMax = 1.0f;
            e.speedMin = 0.4f, e.speedMax = 1.1f;
            e.spread = radians(18.0f);
            e.spawnRadius = 0.18f;
            e.gravity = Vec3{0, 1.6f, 0};
            e.drag = 1.2f;
            e.sizeStart = 0.42f, e.sizeEnd = 0.05f;
            e.colorStart = Vec4{3.2f, 1.5f, 0.35f, 1};
            e.colorEnd = Vec4{1.2f, 0.12f, 0.02f, 0};
            e.isAdditive = true;
            e.windInfluence = 0.35f;
            e.maxParticles = 160;
            break;
        case Preset::Smoke:
            // Not additive: smoke hides what is behind it instead of lighting it.
            e.rate = 14;
            e.lifetimeMin = 2.5f, e.lifetimeMax = 4.0f;
            e.speedMin = 0.5f, e.speedMax = 1.0f;
            e.spread = radians(14.0f);
            e.spawnRadius = 0.15f;
            e.gravity = Vec3{0, 0.35f, 0};
            e.drag = 0.6f;
            e.sizeStart = 0.35f, e.sizeEnd = 1.8f;           // it spreads as it rises
            e.colorStart = Vec4{0.22f, 0.22f, 0.24f, 0.55f};
            e.colorEnd = Vec4{0.45f, 0.45f, 0.48f, 0};
            e.isAdditive = false;
            e.windInfluence = 1.0f;
            e.maxParticles = 96;
            break;
        case Preset::Sparks:
            e.rate = 45;
            e.lifetimeMin = 0.4f, e.lifetimeMax = 1.1f;
            e.speedMin = 2.5f, e.speedMax = 6.0f;
            e.spread = radians(40.0f);
            e.gravity = Vec3{0, -9.0f, 0};
            e.drag = 0.4f;
            e.sizeStart = 0.06f, e.sizeEnd = 0.02f;
            e.colorStart = Vec4{4.0f, 2.6f, 0.9f, 1};
            e.colorEnd = Vec4{2.0f, 0.4f, 0.05f, 0};
            e.isAdditive = true;
            e.maxParticles = 128;
            break;
        case Preset::Explosion:
            // Everything at once, in every direction, then nothing.
            e.rate = 0;
            e.burstAtStart = 110;
            e.lifetimeMin = 0.35f, e.lifetimeMax = 1.0f;
            e.speedMin = 2.5f, e.speedMax = 10.0f;
            e.spread = kPi;
            e.spawnRadius = 0.25f;
            e.gravity = Vec3{0, -3.0f, 0};
            e.drag = 2.4f;
            e.sizeStart = 0.6f, e.sizeEnd = 0.0f;
            e.colorStart = Vec4{4.0f, 1.8f, 0.45f, 1};
            e.colorEnd = Vec4{1.6f, 0.15f, 0.02f, 0};
            e.isAdditive = true;
            e.windInfluence = 0.2f;
            e.maxParticles = 160;
            break;
        case Preset::Rain:
            // A wide patch of sky, straight down and fast. Put it above the camera.
            e.rate = 420;
            e.lifetimeMin = 0.9f, e.lifetimeMax = 1.2f;
            e.speedMin = 11.0f, e.speedMax = 14.0f;
            e.direction = Vec3{0, -1, 0};
            e.spread = radians(2.0f);
            e.spawnRadius = 9.0f;
            e.gravity = Vec3{0, -4.0f, 0};
            e.drag = 0;
            e.sizeStart = 0.035f, e.sizeEnd = 0.035f;
            e.colorStart = Vec4{0.6f, 0.7f, 0.85f, 0.5f};
            e.colorEnd = Vec4{0.6f, 0.7f, 0.85f, 0.35f};
            e.isAdditive = false;
            e.windInfluence = 0.6f;
            e.maxParticles = 640;
            break;
        case Preset::Snow:
            e.rate = 90;
            e.lifetimeMin = 5.0f, e.lifetimeMax = 7.0f;
            e.speedMin = 0.6f, e.speedMax = 1.2f;
            e.direction = Vec3{0, -1, 0};
            e.spread = radians(20.0f);
            e.spawnRadius = 9.0f;
            e.gravity = Vec3{0, -0.15f, 0};
            e.drag = 0.3f;
            e.sizeStart = 0.07f, e.sizeEnd = 0.05f;
            e.colorStart = Vec4{1, 1, 1, 0.9f};
            e.colorEnd = Vec4{1, 1, 1, 0};
            e.isAdditive = false;
            e.windInfluence = 1.0f;
            e.maxParticles = 700;
            break;
        case Preset::Count: break;
    }
    return e;
}

Entity create(Scene& scene, Preset preset, Vec3 position) {
    Entity entity = scene.create(name(preset));
    Transform transform;
    transform.position = position;
    if (preset == Preset::Rain || preset == Preset::Snow) transform.position.y += 9.0f;     // it falls from above
    scene.add(entity, transform);
    scene.add(entity, emitter(preset));
    if (preset == Preset::Fire) {
        Light light;
        light.type = LightType::Point;
        light.color = Vec3{1.0f, 0.55f, 0.2f};
        light.intensity = 9.0f;
        light.range = 8.0f;
        scene.add(entity, light);
        scene.add(entity, kit::Flicker{});
    } else if (preset == Preset::Explosion) {
        Light light;
        light.type = LightType::Point;
        light.color = Vec3{1.0f, 0.6f, 0.25f};
        light.intensity = 45.0f;
        light.range = 10.0f;
        scene.add(entity, light);
        scene.add(entity, kit::Explosion{});
        kit::Lifetime lifetime;
        lifetime.seconds = 1.4f;
        scene.add(entity, lifetime);
    }
    return entity;
}

}  // namespace molten::effects
