// Navigation: the walkable ground found, a path round walls, an agent that walks it.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Character.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Navigation.hpp"

using namespace molten;
using namespace molten::kit;

namespace {

void wall(World& world, Vec3 at, Vec3 half) {
    Entity entity = world.scene.create("Wall");
    world.scene.add<Transform>(entity).position = at;
    world.scene.add(entity, Collider::box(half));
}

/// A floor 30 by 30 with a wall across it from x -15 to 10 at z 0: the way round is at x 10 to 15.
NavigationArea& level(World& world) {
    kit::registerKit();
    wall(world, Vec3{0, -0.5f, 0}, Vec3{15, 0.5f, 15});
    wall(world, Vec3{-2.5f, 1, 0}, Vec3{12.5f, 1, 0.3f});
    wall(world, Vec3{8, 0.15f, 8}, Vec3{1, 0.15f, 1});                       // a low block: a step, walked over
    Entity entity = world.scene.create("Navigation");
    world.scene.add<Transform>(entity);
    NavigationArea& area = world.scene.add<NavigationArea>(entity);
    area.size = Vec2{30, 30};
    world.scene.updateTransforms();
    area.bake(world, entity);
    return area;
}

}  // namespace

TEST(the_navigation_area_finds_the_ground_and_what_is_in_the_way) {
    World world;
    NavigationArea& area = level(world);
    CHECK(area.isBaked && area.columns == 60 && area.rows == 60);
    CHECK(area.isWalkable(Vec3{0, 0, 5}) && area.isWalkable(Vec3{12, 0, 0}));
    // The wall: its top, two meters up, is ground of a sort, but no step leads to it; beside it,
    // the agent's radius keeps it away.
    int onWall = area.cellOf(Vec3{0, 0, 0.1f}), beside = area.cellOf(Vec3{0, 0, 0.6f}), near = area.cellOf(Vec3{0, 0, 0.4f});
    CHECK(!area.canStep(beside, onWall) && !area.canStep(onWall, beside));
    CHECK(!area.walkable[size_t(near)] || area.heights[size_t(near)] > 1.9f);
    CHECK(area.isWalkable(Vec3{8, 0, 8}));                                     // on the low block
    CHECK_NEAR(area.heights[size_t(area.cellOf(Vec3{8, 0, 8}))], 0.3, 1e-3);
}

TEST(a_path_goes_round_a_wall) {
    World world;
    NavigationArea& area = level(world);
    std::vector<Vec3> path = area.findPath(Vec3{-5, 0, 5}, Vec3{-5, 0, -5});
    CHECK(path.size() >= 3);
    float east = -100, length = 0;
    for (size_t i = 0; i < path.size(); i++) {
        east = std::max(east, path[i].x);
        if (i > 0) length += simd::length(path[i] - path[i - 1]);
    }
    CHECK(east > 10);                                                          // through the gap at the east end
    CHECK(length < 40);                                                        // and not wandering: about 2 x 16 straight
    // Each leg of it can be walked straight.
    for (size_t i = 1; i < path.size(); i++) CHECK(area.isLineWalkable(path[i - 1], path[i]));
    CHECK(area.findPath(Vec3{-5, 0, 5}, Vec3{40, 0, 40}).size() >= 2);          // outside: to the nearest it can reach
}

TEST(a_nav_agent_walks_to_its_target) {
    World world;
    level(world);
    Entity goal = world.scene.create("Goal");
    world.scene.add<Transform>(goal).position = Vec3{-5, 0, -5};
    Entity walker = world.scene.create("Walker");
    world.scene.add<Transform>(walker).position = Vec3{-5, 0, 5};
    world.scene.add<CharacterController>(walker);
    NavAgent& agent = world.scene.add<NavAgent>(walker);
    agent.target = "Goal";
    agent.speed = 4;
    world.scene.updateTransforms();
    int frames = 0;
    for (; frames < 60 * 20 && !world.scene.get<NavAgent>(walker)->hasArrived(); frames++) world.advance(1.0 / 60.0);
    Vec3 at = world.scene.get<Transform>(walker)->position;
    CHECK(world.scene.get<NavAgent>(walker)->hasArrived());
    CHECK(simd::length(Vec3{at.x + 5, 0, at.z + 5}) < 0.8f);
    CHECK(frames < 60 * 12);                                                   // about 33 m at 4 m/s
}

TEST(a_guard_walks_its_round_chases_what_it_sees_and_gives_up) {
    World world;
    level(world);
    for (int k = 1; k <= 2; k++) {
        Entity point = world.scene.create("Patrol Point " + std::to_string(k));
        world.scene.add<Transform>(point).position = Vec3{k == 1 ? -10.0f : -2.0f, 0, 8};
    }
    Entity player = world.scene.create("Player");
    world.scene.add<Transform>(player).position = Vec3{-6, 0, -10};             // behind the wall
    Entity guard = world.scene.create("Guard");
    world.scene.add<Transform>(guard).position = Vec3{-6, 0, 8};
    world.scene.add<CharacterController>(guard);
    world.scene.add<NavAgent>(guard);
    world.scene.add<Guard>(guard);
    world.scene.updateTransforms();
    auto run = [&](float seconds) {
        for (int i = 0; i < int(seconds * 60); i++) world.advance(1.0 / 60.0);
    };
    run(4);
    const Guard* watching = world.scene.get<Guard>(guard);
    CHECK(watching->state == Guard::State::Patrol && !watching->seesPlayer);
    CHECK(world.scene.get<Transform>(guard)->position.x < -6.5f || watching->point == 2);  // walking its round
    // The player steps out in front of it (it is walking east, to its second point), within sight: it chases.
    Vec3 guardAt = world.scene.get<Transform>(guard)->position;
    Vec3 facing = transformDirection(world.scene.get<Transform>(guard)->world, Vec3{0, 0, -1});
    world.scene.get<Transform>(player)->position = guardAt + Vec3{facing.x, 0, facing.z} * 5.0f;
    world.scene.updateTransforms();
    run(0.5f);
    CHECK(world.scene.get<Guard>(guard)->state == Guard::State::Chase);
    // And away behind the wall again: it searches where it saw them, then goes back to its round.
    world.scene.get<Transform>(player)->position = Vec3{-12, 0, -12};
    world.scene.updateTransforms();
    run(1);
    CHECK(world.scene.get<Guard>(guard)->state == Guard::State::Search);
    run(6);
    CHECK(world.scene.get<Guard>(guard)->state == Guard::State::Return || world.scene.get<Guard>(guard)->state == Guard::State::Patrol);
}
