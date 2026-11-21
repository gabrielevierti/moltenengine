// Things and boats on water.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"

#include <cmath>

using namespace molten;

namespace {
Entity raft(World& world, Quat turned) {
    Entity entity = world.scene.create("Raft");
    Transform place;
    place.position = Vec3{0, 1, 0};
    place.rotation = turned;
    world.scene.add(entity, place);
    RigidBody body;
    body.mass = 200;
    body.rotates = true;
    world.scene.add(entity, body);
    Collider collider;
    collider.halfExtents = Vec3{1, 0.25f, 2};
    world.scene.add(entity, collider);
    kit::Buoyancy floats;
    floats.hull = Vec3{1, 0.25f, 2};
    world.scene.add(entity, floats);
    return entity;
}
float tilt(const World& world, Entity entity) {
    Vec3 up = rotate(world.scene.get<Transform>(entity)->rotation, Vec3{0, 1, 0});
    return degrees(std::acos(std::clamp(up.y, -1.0f, 1.0f)));
}
}  // namespace

TEST(a_hull_floats_level_and_rights_itself) {
    kit::registerKit();
    World world;
    Entity entity = raft(world, Quat(radians(25), Vec3{0, 0, 1}));
    for (int i = 0; i < 60 * 8; i++) world.advance(1.0 / 60.0);
    const Transform* place = world.scene.get<Transform>(entity);
    // Half under (lift 2), about level, at rest.
    CHECK(std::abs(place->position.y - 0) < 0.08f);
    CHECK(tilt(world, entity) < 4);
    CHECK(simd::length(world.scene.get<RigidBody>(entity)->velocity) < 0.1f);
    CHECK(world.scene.get<kit::Buoyancy>(entity)->isInWater);
    // A river carries it along.
    world.scene.get<kit::Buoyancy>(entity)->current = Vec3{1.5f, 0, 0};
    for (int i = 0; i < 60 * 6; i++) world.advance(1.0 / 60.0);
    CHECK(world.scene.get<RigidBody>(entity)->velocity.x > 1.2f);
}

TEST(a_boat_goes_ahead_and_turns_on_the_water) {
    kit::registerKit();
    World world;
    Entity entity = raft(world, Quat{});
    kit::Boat boat;
    boat.usesInput = false;
    world.scene.add(entity, boat);
    for (int i = 0; i < 60 * 3; i++) world.advance(1.0 / 60.0);            // settled
    kit::Boat* driven = world.scene.get<kit::Boat>(entity);
    driven->throttle = 1;
    for (int i = 0; i < 60 * 4; i++) world.advance(1.0 / 60.0);
    CHECK(driven->speed > 4);
    CHECK(world.scene.get<Transform>(entity)->position.z < -8);               // ahead is -z
    // Hard right: it turns right, and goes where it points, not sliding sideways.
    driven->steer = 1;
    for (int i = 0; i < 60 * 2; i++) world.advance(1.0 / 60.0);
    Vec3 forward = rotate(world.scene.get<Transform>(entity)->rotation, Vec3{0, 0, -1});
    CHECK(forward.x > 0.3f);
    Vec3 velocity = world.scene.get<RigidBody>(entity)->velocity;
    Vec3 flat = safeNormalize(Vec3{velocity.x, 0, velocity.z}, Vec3{0, 0, -1});
    CHECK(simd::dot(flat, safeNormalize(Vec3{forward.x, 0, forward.z}, flat)) > 0.85f);
    CHECK(tilt(world, entity) < 15);
}
