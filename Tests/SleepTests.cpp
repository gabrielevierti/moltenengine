// Bodies at rest fall asleep and cost nothing; whatever should wake them does.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Physics/Physics.hpp"
#include "Scene/Components.hpp"

#include <chrono>

using namespace molten;

namespace {

Entity ground(Scene& scene) {
    Entity floor = scene.create("Floor");
    scene.add(floor, Transform{});
    scene.add(floor, Collider::box(Vec3{40, 0.5f, 40}));
    scene.get<Transform>(floor)->position = Vec3{0, -0.5f, 0};
    return floor;
}

Entity crate(Scene& scene, Vec3 at) {
    Entity box = scene.create("Crate");
    Transform place;
    place.position = at;
    scene.add(box, place);
    scene.add(box, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    RigidBody body;
    body.rotates = true;
    scene.add(box, body);
    return box;
}

void run(Physics& physics, Scene& scene, float seconds) {
    for (int i = 0; i < int(seconds * 60); i++) {
        scene.updateTransforms();
        physics.step(scene, 1.0f / 60.0f);
        physics.writeTransforms(scene, 1.0f);
    }
}

}  // namespace

TEST(a_resting_body_falls_asleep_and_wakes_when_pushed) {
    Scene scene;
    Physics physics;
    ground(scene);
    Entity box = crate(scene, Vec3{0, 2, 0});
    run(physics, scene, 3.0f);
    RigidBody* body = scene.get<RigidBody>(box);
    CHECK(body->isSleeping);
    CHECK(std::abs(body->position.y - 0.5f) < 0.03f);           // asleep where it landed, not sinking
    float restingAt = body->position.y;
    run(physics, scene, 2.0f);
    CHECK(body->isSleeping && std::abs(body->position.y - restingAt) < 1e-5f);
    // A push wakes it and it slides.
    body->applyImpulse(Vec3{3, 0, 0}, body->position);
    run(physics, scene, 0.2f);
    CHECK(!body->isSleeping && body->position.x > 0.2f);
}

TEST(a_sleeping_body_wakes_when_hit_or_when_its_floor_goes) {
    Scene scene;
    Physics physics;
    Entity floor = ground(scene);
    Entity low = crate(scene, Vec3{0, 0.5f, 0});
    run(physics, scene, 2.0f);
    CHECK(scene.get<RigidBody>(low)->isSleeping);
    // Another dropped onto it wakes it on landing.
    Entity high = crate(scene, Vec3{0.2f, 3.0f, 0});
    int began = 0;
    physics.onContactBegin = [&](const Contact& contact) { began += (contact.a == low || contact.b == low) ? 1 : 0; };
    run(physics, scene, 0.9f);
    CHECK(began >= 1);
    run(physics, scene, 3.0f);
    CHECK(scene.get<RigidBody>(low)->isSleeping && scene.get<RigidBody>(high)->isSleeping);
    // Both asleep on each other: their touching does not "begin" again when they are woken.
    began = 0;
    // Their floor taken away: they fall.
    scene.destroy(floor);
    scene.flushDestroyed();                 // as the world does at the end of a frame
    run(physics, scene, 0.5f);
    CHECK(!scene.get<RigidBody>(low)->isSleeping);
    CHECK(scene.get<RigidBody>(low)->position.y < -0.5f);
    CHECK(began == 0);
}

TEST(a_body_that_may_not_sleep_stays_awake) {
    Scene scene;
    Physics physics;
    ground(scene);
    Entity box = crate(scene, Vec3{0, 0.5f, 0});
    scene.get<RigidBody>(box)->canSleep = false;
    run(physics, scene, 2.0f);
    CHECK(!scene.get<RigidBody>(box)->isSleeping);
}

TEST(a_heap_of_sleeping_crates_costs_little) {
    Scene scene;
    Physics physics;
    ground(scene);
    // Five hundred crates in rows on the ground, a little apart.
    for (int x = 0; x < 25; x++) {
        for (int z = 0; z < 20; z++) crate(scene, Vec3{float(x) * 1.2f - 15.0f, 0.52f, float(z) * 1.2f - 12.0f});
    }
    run(physics, scene, 1.5f);
    size_t asleep = 0;
    scene.each<RigidBody>([&](Entity, RigidBody& body) { asleep += body.isSleeping ? 1 : 0; });
    CHECK(asleep == 500);
    auto start = std::chrono::steady_clock::now();
    run(physics, scene, 1.0f);
    double asleepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60.0;
    scene.each<RigidBody>([&](Entity, RigidBody& body) {
        body.canSleep = false;
        body.wake();
    });
    start = std::chrono::steady_clock::now();
    run(physics, scene, 1.0f);
    double awakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 60.0;
    std::printf("    500 crates: %.3f ms a step asleep, %.3f ms awake\n", asleepMs, awakeMs);
    CHECK(asleepMs * 2.0 < awakeMs);
}
