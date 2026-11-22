// Triggers hear of everything that moves into them, however it moves: a body that physics
// moves, and a character, or anything a script or an animation moves, that has no body.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Character.hpp"
#include "Kit/Kit.hpp"

using namespace molten;

namespace {

Entity placed(Scene& scene, const char* name, Vec3 position, Collider collider) {
    Entity entity = scene.create(name);
    Transform transform;
    transform.position = position;
    scene.add(entity, transform);
    scene.add(entity, collider);
    scene.updateTransforms();
    return entity;
}

void moveTo(Scene& scene, Entity entity, Vec3 position) {
    scene.get<Transform>(entity)->position = position;
    scene.updateTransforms();
}

}  // namespace

TEST(a_trigger_hears_of_what_moves_without_a_body) {
    Scene scene;
    Physics physics;
    int left = 0;
    physics.onTriggerExit = [&](Entity, Entity) { left++; };
    Collider zoneShape = Collider::box(Vec3{1, 1, 1});
    zoneShape.isTrigger = true;
    Entity zone = placed(scene, "Zone", Vec3{0, 0, 0}, zoneShape);
    Entity walker = placed(scene, "Walker", Vec3{5, 0, 0}, Collider::capsule(0.3f, 0.5f));
    // Scenery that overlaps the zone from the start, and never moves: it never "enters".
    placed(scene, "Wall", Vec3{0.5f, 0, 0}, Collider::box(Vec3{0.5f, 2, 0.5f}));

    physics.step(scene, 1.0f / 60.0f);
    physics.step(scene, 1.0f / 60.0f);
    CHECK(physics.takeEnteredTriggers().empty());

    // It walks in: heard once, the trigger first.
    moveTo(scene, walker, Vec3{0.2f, 0, 0});
    physics.step(scene, 1.0f / 60.0f);
    std::vector<std::pair<Entity, Entity>> entered = physics.takeEnteredTriggers();
    CHECK(entered.size() == 1 && entered[0].first == zone && entered[0].second == walker);

    // Standing still inside, then moving about inside: it has not left, and does not enter again.
    for (int i = 0; i < 5; i++) physics.step(scene, 1.0f / 60.0f);
    moveTo(scene, walker, Vec3{-0.2f, 0, 0});
    physics.step(scene, 1.0f / 60.0f);
    for (int i = 0; i < 5; i++) physics.step(scene, 1.0f / 60.0f);
    CHECK(physics.takeEnteredTriggers().empty() && left == 0);

    // Out, and in again.
    moveTo(scene, walker, Vec3{5, 0, 0});
    physics.step(scene, 1.0f / 60.0f);
    CHECK(left == 1 && physics.takeEnteredTriggers().empty());
    moveTo(scene, walker, Vec3{0, 0, 0});
    physics.step(scene, 1.0f / 60.0f);
    CHECK(physics.takeEnteredTriggers().size() == 1);

    // Two things without bodies do not push each other: only triggers hear of them.
    CHECK(physics.takeBegunContacts().empty());
}

TEST(a_walking_character_collects_what_it_walks_into) {
    // The kit's own pieces, together: a character (no body) and a coin (a trigger with a Collectible).
    World world;
    Entity player = world.scene.create("Player");
    Transform start;
    start.position = Vec3{-3, 0, 0};
    world.scene.add(player, start);
    world.scene.add(player, kit::CharacterController{});
    Collider seen = Collider::capsule(0.35f, 0.55f);
    seen.offset = Vec3{0, 0.9f, 0};
    seen.isTrigger = true;
    world.scene.add(player, seen);

    Entity coin = world.scene.create("Coin");
    Transform there;
    there.position = Vec3{0, 0.9f, 0};
    world.scene.add(coin, there);
    Collider round = Collider::sphere(0.4f);
    round.isTrigger = true;
    world.scene.add(coin, round);
    world.scene.add(coin, kit::Collectible{});

    for (int i = 0; i < 10; i++) world.advance(1.0 / 60.0);
    CHECK(world.scene.isAlive(coin) && world.value("score") == 0);
    // It walks to the coin, a little each frame.
    for (int i = 0; i < 60; i++) {
        world.scene.get<Transform>(player)->position.x += 0.05f;
        world.advance(1.0 / 60.0);
    }
    CHECK(!world.scene.isAlive(coin));
    CHECK_NEAR(world.value("score"), 1.0, 1e-6);
}
