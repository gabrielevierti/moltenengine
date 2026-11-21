// Ropes and chains made of links.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Rope.hpp"
#include "Physics/Physics.hpp"

#include <cmath>

using namespace molten;

TEST(points_hang_between_two_ends_as_long_as_the_rope) {
    std::vector<Vec3> straight = kit::hangingPoints(Vec3{0, 0, 0}, Vec3{4, 0, 0}, 4, 8);
    CHECK(straight.size() == 9 && std::abs(straight[4].x - 2) < 1e-3f && std::abs(straight[4].y) < 1e-4f);
    std::vector<Vec3> sagging = kit::hangingPoints(Vec3{0, 0, 0}, Vec3{4, 0, 0}, 5, 10);
    float total = 0;
    for (size_t i = 1; i < sagging.size(); i++) total += simd::length(sagging[i] - sagging[i - 1]);
    CHECK(std::abs(total - 5) < 0.02f);
    CHECK(sagging[5].y < -0.8f && simd::length(sagging.back() - Vec3{4, 0, 0}) < 1e-3f);
    // Evenly apart.
    for (size_t i = 1; i < sagging.size(); i++) CHECK(std::abs(simd::length(sagging[i] - sagging[i - 1]) - 0.5f) < 0.02f);
    MeshData tube = kit::ropeTube(sagging, 0.02f, 8);
    CHECK(tube.positions.size() == 11 * 9 && tube.indices.size() == 10 * 8 * 6);
    MeshData rings = kit::chainRings(sagging, 0.02f);
    CHECK(!rings.indices.empty() && rings.indices.size() % 3 == 0);
}

TEST(a_rope_hangs_holds_a_crate_and_is_tied_between_posts) {
    kit::registerKit();
    World world;
    // Hanging free from a hook three meters long.
    Entity hook = world.scene.create("Hook");
    Transform up;
    up.position = Vec3{0, 5, 0};
    world.scene.add(hook, up);
    world.scene.add(hook, Name{"Hook"});
    kit::Rope rope;
    rope.length = 3;
    rope.links = 10;
    world.scene.add(hook, rope);
    // Holding a crate from a beam.
    Entity beam = world.scene.create("Beam");
    Transform beamPlace;
    beamPlace.position = Vec3{10, 5, 0};
    world.scene.add(beam, beamPlace);
    world.scene.add(beam, Name{"Beam"});
    Entity crate = world.scene.create("Crate");
    Transform cratePlace;
    cratePlace.position = Vec3{10, 3, 0};
    world.scene.add(crate, cratePlace);
    world.scene.add(crate, Name{"Crate"});
    RigidBody heavy;
    heavy.mass = 20;
    world.scene.add(crate, heavy);
    world.scene.add(crate, Collider{});
    kit::Rope carrier;
    carrier.to = "Crate";
    carrier.links = 8;
    world.scene.add(beam, carrier);
    // Tied between two posts four meters apart, five long.
    Entity left = world.scene.create("Left Post");
    Transform l;
    l.position = Vec3{-10, 3, 0};
    world.scene.add(left, l);
    world.scene.add(left, Name{"Left Post"});
    Entity right = world.scene.create("Right Post");
    Transform r;
    r.position = Vec3{-6, 3, 0};
    world.scene.add(right, r);
    world.scene.add(right, Name{"Right Post"});
    kit::Rope line;
    line.to = "Right Post";
    line.length = 5;
    line.links = 12;
    line.isChain = true;
    world.scene.add(left, line);

    for (int i = 0; i < 60 * 5; i++) world.advance(1.0 / 60.0);
    const kit::Rope* free = world.scene.get<kit::Rope>(hook);
    std::vector<Vec3> points = free->points(world);
    CHECK(points.size() == 11);
    CHECK(simd::length(points.front() - Vec3{0, 5, 0}) < 0.05f);              // tied at the hook
    CHECK(std::abs(points.back().y - 2) < 0.25f && std::abs(points.back().x) < 0.2f);   // three meters straight down
    for (size_t i = 1; i < points.size(); i++) CHECK(simd::length(points[i] - points[i - 1]) < 0.36f);   // in one piece

    // The crate hangs two meters below the beam, still.
    const Transform* held = world.scene.get<Transform>(crate);

    CHECK(std::abs(held->position.y - 3) < 0.25f && std::abs(held->position.x - 10) < 0.2f);
    CHECK(simd::length(world.scene.get<RigidBody>(crate)->velocity) < 0.3f);

    // Between the posts: tied at both, sagging.
    std::vector<Vec3> sag = world.scene.get<kit::Rope>(left)->points(world);
    CHECK(simd::length(sag.front() - Vec3{-10, 3, 0}) < 0.05f && simd::length(sag.back() - Vec3{-6, 3, 0}) < 0.1f);
    CHECK(sag[6].y < 2.3f);
}
