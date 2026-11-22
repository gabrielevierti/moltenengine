// What came with Molten 1.1: the scene's own order (the Hierarchy's), files that follow a move,
// the interface's toggle, particles that leave a line behind a moving emitter.
#include "Test.hpp"

#include "Assets/AssetDatabase.hpp"
#include "Core/World.hpp"
#include "Kit/Layout.hpp"
#include "Rendering/Particles.hpp"
#include "Scene/Components.hpp"

#include <cmath>

using namespace molten;

TEST(the_scene_keeps_an_order_that_can_change_and_survives_deletion) {
    Scene scene;
    Entity a = scene.create("A"), b = scene.create("B"), c = scene.create("C"), d = scene.create("D");
    for (Entity e : {a, b, c, d}) scene.add(e, Transform{});
    auto order = [&]() {
        std::string out;
        scene.each<Transform>([&](Entity entity, Transform&) { out += scene.get<Name>(entity)->value; });
        return out;
    };
    CHECK(order() == "ABCD");
    scene.setOrder(d, 0);
    CHECK(order() == "DABC");
    scene.setOrder(d, scene.orderOf(b));
    CHECK(order() == "ABDC");
    scene.destroy(a);
    scene.flushDestroyed();
    CHECK(order() == "BDC");                    // closed up, not shuffled
}

TEST(a_moved_file_is_followed_by_what_names_it) {
    std::string text = "mesh \"Models/car.obj\"\nanimations \"Animations/Run.anim,Animations/Walk.anim\"\nother \"Models/car.objects\"";
    std::string moved = AssetDatabase::replaceReferences(text, "Animations/Run.anim", "Animations/Car/Run.anim");
    CHECK(moved.find("\"Animations/Car/Run.anim,Animations/Walk.anim\"") != std::string::npos);
    std::string folder = AssetDatabase::replaceReferences(text, "Models", "Cars");
    CHECK(folder.find("\"Cars/car.obj\"") != std::string::npos);
    CHECK(folder.find("\"Cars/car.objects\"") != std::string::npos);
    std::string near = AssetDatabase::replaceReferences(text, "Models/car.obj", "Models/van.obj");
    CHECK(near.find("\"Models/van.obj\"") != std::string::npos && near.find("\"Models/car.objects\"") != std::string::npos);
}

TEST(a_toggle_flips_its_shared_value) {
    World world;
    Entity entity = world.scene.create("Sound");
    world.scene.add(entity, Transform{});
    kit::UIToggle toggle;
    toggle.value = "sound";
    toggle.isOn = true;
    toggle.start(world, entity);
    CHECK(world.value("sound") == 1.0f);
    toggle.isOn = false;
    world.setValue("sound", 0.0f);
    CHECK(world.value("sound") == 0.0f);
}

TEST(a_moving_emitter_leaves_a_line_not_clumps) {
    Scene scene;
    Entity entity = scene.create("Trail");
    scene.add(entity, Transform{});
    ParticleEmitter emitter;
    emitter.rate = 600;
    emitter.speedMin = emitter.speedMax = 0;
    emitter.spread = 0;
    emitter.gravity = Vec3{0, 0, 0};
    emitter.lifetimeMin = emitter.lifetimeMax = 5;
    scene.add(entity, emitter);
    scene.updateTransforms();
    updateParticles(scene, 1.0f / 30.0f);
    // A big step sideways in one frame: what is born on the way is spread along it.
    scene.get<Transform>(entity)->position = Vec3{3, 0, 0};
    scene.updateTransforms();
    updateParticles(scene, 1.0f / 30.0f);
    int between = 0;
    for (const Particle& particle : scene.get<ParticleEmitter>(entity)->particles) between += particle.position.x > 0.5f && particle.position.x < 2.5f;
    CHECK(between > 5);

    // Living in the entity's space, they go where it goes.
    ParticleEmitter& own = *scene.get<ParticleEmitter>(entity);
    own.space = ParticleEmitter::Space::Local;
    own.particles.clear();
    updateParticles(scene, 1.0f / 30.0f);
    CHECK(!own.particles.empty());
    Vec3 before = particleWorldPosition(own, own.particles.front(), scene.get<Transform>(entity)->world);
    scene.get<Transform>(entity)->position = Vec3{10, 0, 0};
    scene.updateTransforms();
    Vec3 after = particleWorldPosition(own, own.particles.front(), scene.get<Transform>(entity)->world);
    CHECK(std::abs(after.x - before.x - 7.0f) < 1e-3f);
}
