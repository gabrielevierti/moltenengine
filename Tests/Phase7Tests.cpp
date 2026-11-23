// What came with the second round of the editor's polish: particles that hit colliders and wear
// pictures, the project's quality settings, area lights, the sky's air, interface dials of pieces.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/Quality.hpp"
#include "Scene/Components.hpp"
#include "Scene/SceneFile.hpp"

#include <cmath>

using namespace molten;

namespace {
// Particle systems as a game might make them, for the tests (the engine has no presets).
ParticleEmitter testFire() {
    ParticleEmitter e;
    e.rate = 70, e.lifetimeMin = 0.5f, e.lifetimeMax = 1.1f, e.speedMin = 0.8f, e.speedMax = 1.8f, e.spread = radians(14.0f);
    e.spawnRadius = 0.18f, e.gravity = Vec3{0, 1.2f, 0}, e.drag = 0.8f, e.turbulence = 0.7f, e.turbulenceSize = 0.5f;
    e.hasMiddle = true, e.middleAt = 0.35f;
    e.colorStart = Vec4{4.0f, 2.6f, 0.9f, 1}, e.colorMiddle = Vec4{3.2f, 1.0f, 0.15f, 0.85f}, e.colorEnd = Vec4{0.5f, 0.1f, 0.02f, 0};
    e.sizeStart = 0.3f, e.sizeMiddle = 0.55f, e.sizeEnd = 0.2f, e.maxParticles = 400;
    e.texture = "Particles/Flame.png", e.spinMax = 0.6f, e.buoyancy = 2.0f, e.softness = 0.3f;
    return e;
}
ParticleEmitter testSparks() {
    ParticleEmitter e;
    e.rate = 60, e.lifetimeMin = 0.4f, e.lifetimeMax = 1.0f, e.speedMin = 3.0f, e.speedMax = 7.0f, e.spread = radians(40.0f);
    e.gravity = Vec3{0, -9.8f, 0}, e.drag = 0.3f, e.bounces = true, e.bounciness = 0.35f, e.collides = true, e.stretch = 0.025f;
    e.renderMode = ParticleEmitter::RenderMode::Stretched;
    e.colorStart = Vec4{6.0f, 3.6f, 1.2f, 1}, e.colorEnd = Vec4{2.0f, 0.4f, 0.05f, 0}, e.sizeStart = 0.03f, e.sizeEnd = 0.012f;
    return e;
}
ParticleEmitter testExplosion() {
    ParticleEmitter e;
    e.rate = 0, e.burstAtStart = 220, e.lifetimeMin = 0.5f, e.lifetimeMax = 1.4f, e.speedMin = 2.0f, e.speedMax = 11.0f, e.spread = radians(180.0f);
    e.spawnRadius = 0.2f, e.gravity = Vec3{0, -1.5f, 0}, e.drag = 2.6f, e.turbulence = 1.0f, e.duration = 0.1f, e.maxParticles = 400;
    return e;
}
ParticleEmitter testSmoke() {
    ParticleEmitter e;
    e.rate = 14, e.lifetimeMin = 2.5f, e.lifetimeMax = 4.5f, e.isAdditive = false, e.lit = 1.0f, e.buoyancy = 1.2f, e.turbulenceOctaves = 2;
    e.texture = "Particles/Smoke.png", e.randomRotation = true, e.spinMin = 0.1f, e.spinMax = 0.5f, e.collides = true;
    return e;
}
}  // namespace

TEST(particles_bounce_off_colliders) {
    Scene scene;
    Entity wall = scene.create("Wall");
    Transform at;
    at.position = Vec3{0, -0.5f, 0};
    scene.add(wall, at);
    Collider box;
    box.halfExtents = Vec3{5, 0.5f, 5};
    scene.add(wall, box);
    Entity source = scene.create("Sparks");
    Transform up;
    up.position = Vec3{0, 2, 0};
    scene.add(source, up);
    ParticleEmitter emitter = testSparks();
    emitter.bounces = false;
    emitter.direction = Vec3{0, -1, 0};
    emitter.spread = 0.05f;
    scene.add(source, emitter);
    scene.updateTransforms();
    float lowest = 100;
    for (int i = 0; i < 120; i++) {
        updateParticles(scene, 1.0f / 60.0f);
        for (const Particle& p : scene.get<ParticleEmitter>(source)->particles) lowest = std::min(lowest, p.position.y);
    }
    CHECK(!scene.get<ParticleEmitter>(source)->particles.empty());
    CHECK(lowest > -0.05f);                     // none went through the top of the box
}

TEST(a_particle_system_keeps_its_picture_and_its_modules) {
    ParticleEmitter smoke = testSmoke();
    CHECK(smoke.texture == "Particles/Smoke.png" && smoke.randomRotation && smoke.collides);
    smoke.space = ParticleEmitter::Space::Local;
    smoke.drift = Vec3{0.5f, 0, 0};
    smoke.renderMode = ParticleEmitter::RenderMode::Vertical;
    ParticleEmitter read;
    CHECK(readEffect(writeEffect(smoke), read));
    CHECK(read.texture == smoke.texture && read.spinMax == smoke.spinMax && read.collides);
    CHECK(read.space == ParticleEmitter::Space::Local && read.drift.x == 0.5f && read.renderMode == ParticleEmitter::RenderMode::Vertical);
    CHECK(read.lit == 1.0f && read.buoyancy == smoke.buoyancy && read.turbulenceOctaves == 2);
    smoke.sheetColumns = 4;
    smoke.sheetRows = 2;
    CHECK(readEffect(writeEffect(smoke), read) && read.sheetColumns == 4 && read.sheetRows == 2);
}

TEST(the_project_quality_is_read_and_put_on_the_settings) {
    GameQuality low = GameQuality::preset(GameQuality::Level::Low);
    CHECK(low.antiAliasing == 2 && low.renderScale < 1 && low.volumetricClouds && low.cloudSteps < 48);
    std::optional<QualityLevels> again = QualityLevels::parse(QualityLevels::standard().toText());
    CHECK(again && again->find("Low") && again->find("Low")->shadowResolution == low.shadowResolution);
    CHECK(!QualityLevels::parse("level High\n"));
    RenderSettings settings;
    settings.sky.cloudsHaveDepth = true;
    low.applyTo(settings);
    CHECK(settings.sampleCount == 2 && settings.sky.cloudsHaveDepth && settings.sky.cloudSteps == 24 && settings.cascades.resolution == 2048);
    // The defaults asked for: 2x MSAA, no tone mapping.
    CHECK(RenderSettings{}.sampleCount == 2 && RenderSettings{}.toneMapper == 2);
}

TEST(sunlight_goes_gold_then_red_then_out_as_the_sun_sets) {
    Vec3 noon = sunlightThroughAir(0.9f);
    CHECK(noon.x > 0.9f && noon.z > 0.85f);                       // white overhead
    Vec3 low = sunlightThroughAir(0.03f);
    CHECK(low.x > low.z * 2);                                       // red-orange near the horizon
    Vec3 gone = sunlightThroughAir(-0.2f);
    CHECK(gone.x == 0 && gone.y == 0 && gone.z == 0);               // none once it has set: the moon lights the night
}

TEST(area_lights_are_saved_with_their_size) {
    Scene scene;
    Entity window = scene.create("Window");
    scene.add(window, Transform{});
    Light light;
    light.type = LightType::Area;
    light.areaSize = Vec2{3, 1.5f};
    scene.add(window, light);
    RenderSettings settings;
    std::string text = writeScene(scene, {}, settings);
    Scene back;
    std::vector<NamedMaterial> materials;
    RenderSettings read;
    CHECK(readScene(text, back, materials, read, SceneAssets{}));
    bool found = false;
    back.each<Light>([&](Entity, Light& l) { found = l.type == LightType::Area && l.areaSize.x == 3 && l.areaSize.y == 1.5f; });
    CHECK(found);
}
