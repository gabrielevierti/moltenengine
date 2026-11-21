// Characters: a capsule that walks, slides along walls, climbs steps, stands on slopes it can
// walk, slides off steeper ones, jumps and falls.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Character.hpp"
#include "Kit/Kit.hpp"

using namespace molten;
using namespace molten::kit;

namespace {

Entity block(World& world, Vec3 at, Vec3 half, Quat turned = Quat{}) {
    Entity entity = world.scene.create("Block");
    Transform& place = world.scene.add<Transform>(entity);
    place.position = at;
    place.rotation = turned;
    world.scene.add(entity, Collider::box(half));
    return entity;
}

Entity character(World& world, Vec3 feet) {
    Entity entity = world.scene.create("Player");
    world.scene.add<Transform>(entity).position = feet;
    world.scene.add<CharacterController>(entity);
    world.scene.updateTransforms();
    return entity;
}

/// Walks `self` by `step` each 1/60 s for `frames`, as a mover would, with gravity.
Vec3 walk(World& world, Entity self, Vec3 perSecond, int frames, float gravity = 20) {
    CharacterController* controller = world.scene.get<CharacterController>(self);
    float fall = 0;
    for (int i = 0; i < frames; i++) {
        fall = controller->isGrounded ? -2.0f : fall - gravity / 60.0f;
        controller->move(world, self, Vec3{perSecond.x, fall, perSecond.z} / 60.0f);
    }
    return world.scene.get<Transform>(self)->position;
}

}  // namespace

TEST(a_character_stands_walks_and_slides_along_a_wall) {
    kit::registerKit();
    World world;
    block(world, Vec3{0, -0.5f, 0}, Vec3{50, 0.5f, 50});                    // the ground, its top at 0
    block(world, Vec3{0, 1, -5}, Vec3{10, 1, 0.5f});                        // a wall across, at z -5
    Entity player = character(world, Vec3{0, 0.5f, 0});
    Vec3 at = walk(world, player, Vec3{0, 0, 0}, 40);
    CHECK_NEAR(at.y, 0, 0.03);                                              // fell onto the ground and stands on it
    CHECK(world.scene.get<CharacterController>(player)->isGrounded);
    // Into the wall at an angle: stopped by it, and sliding along it to the right.
    at = walk(world, player, Vec3{2, 0, -4}, 120);
    CHECK(at.z > -5 + 0.5f - 0.05f && at.z < -4.0f);                          // against the wall's face (radius 0.35 from it)
    CHECK(at.x > 3);
    CHECK_NEAR(at.y, 0, 0.03);
}

TEST(a_character_climbs_a_step_but_not_a_wall) {
    kit::registerKit();
    World world;
    block(world, Vec3{0, -0.5f, 0}, Vec3{50, 0.5f, 50});
    block(world, Vec3{0, 0.15f, -3}, Vec3{3, 0.15f, 1});                     // a step 0.3 high, from z -2 to -4
    block(world, Vec3{6, 0.4f, -3}, Vec3{1, 0.4f, 1});                       // a block 0.8 high: a wall for it
    Entity player = character(world, Vec3{0, 0, 0});
    walk(world, player, Vec3{0, 0, 0}, 10);
    Vec3 at = walk(world, player, Vec3{0, 0, -3}, 60);
    CHECK(at.z < -2.4f);                                                    // up and onto it
    CHECK_NEAR(at.y, 0.3, 0.04);
    Entity other = character(world, Vec3{6, 0, 0});
    walk(world, other, Vec3{0, 0, 0}, 10);
    at = walk(world, other, Vec3{0, 0, -3}, 60);
    CHECK(at.z > -2.0f + 0.3f && at.y < 0.05f);                             // stopped at its face
}

TEST(a_character_stands_on_a_walkable_slope_and_slides_off_a_steep_one) {
    kit::registerKit();
    World world;
    block(world, Vec3{0, -0.5f, 0}, Vec3{50, 0.5f, 50});
    block(world, Vec3{0, 0, -10}, Vec3{3, 0.2f, 6}, axisAngle(Vec3{1, 0, 0}, radians(25.0f)));      // 25 degrees
    block(world, Vec3{12, 0, -10}, Vec3{3, 0.2f, 6}, axisAngle(Vec3{1, 0, 0}, radians(65.0f)));     // 65 degrees
    world.scene.updateTransforms();
    // On the gentle one, standing still: it stays where it is.
    Entity gentle = character(world, Vec3{0, 3, -10});
    walk(world, gentle, Vec3{0, 0, 0}, 60);
    Vec3 settled = world.scene.get<Transform>(gentle)->position;
    Vec3 later = walk(world, gentle, Vec3{0, 0, 0}, 120);
    CHECK(world.scene.get<CharacterController>(gentle)->isGrounded);
    CHECK(simd::length(later - settled) < 0.05f);
    // On the steep one: not ground, so it slides down to the floor.
    Entity steep = character(world, Vec3{12, 4, -10});
    Vec3 end = walk(world, steep, Vec3{0, 0, 0}, 240);
    CHECK(end.y < 1.0f);
}

TEST(a_mover_walks_where_the_keys_say_and_jumps) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    block(world, Vec3{0, -0.5f, 0}, Vec3{50, 0.5f, 50});
    Entity player = character(world, Vec3{0, 0.1f, 0});
    world.scene.add<CharacterMover>(player);
    for (int i = 0; i < 30; i++) world.advance(1.0 / 60.0);
    world.input.handleKey(Key::W, true);
    for (int i = 0; i < 60; i++) world.advance(1.0 / 60.0);
    world.input.handleKey(Key::W, false);
    Vec3 at = world.scene.get<Transform>(player)->position;
    CHECK(at.z < -1.5f && std::abs(at.x) < 0.05f);                             // forward (no camera: along -z)
    const CharacterMover* mover = world.scene.get<CharacterMover>(player);
    CHECK(mover->isGrounded);
    // A jump: up to about its jump height, and back down.
    world.input.handleKey(Key::Space, true);
    world.advance(1.0 / 60.0);
    world.input.handleKey(Key::Space, false);
    float highest = 0;
    for (int i = 0; i < 90; i++) {
        world.advance(1.0 / 60.0);
        highest = std::max(highest, world.scene.get<Transform>(player)->position.y);
    }
    CHECK(highest > 0.9f && highest < 1.3f);
    CHECK(world.scene.get<CharacterMover>(player)->isGrounded);
}
