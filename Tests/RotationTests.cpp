#include "Test.hpp"

#include "Physics/Physics.hpp"
#include "Scene/Components.hpp"

using namespace molten;

namespace {

struct Yard {
    Scene scene;
    Physics physics;
    Yard() { thing("Floor", Vec3{0, -0.5f, 0}, Collider::box(Vec3{50, 0.5f, 50}), false); }

    Entity thing(const char* name, Vec3 position, Collider collider, bool moves = true, bool rotates = true, Quat rotation = Quat{}) {
        Entity entity = scene.create(name);
        Transform transform;
        transform.position = position;
        transform.rotation = rotation;
        scene.add(entity, transform);
        scene.add(entity, collider);
        if (moves) {
            RigidBody body;
            body.rotates = rotates;
            scene.add(entity, body);
        }
        scene.updateTransforms();
        return entity;
    }
    RigidBody& body(Entity entity) { return *scene.get<RigidBody>(entity); }
    void run(float seconds) {
        for (int i = 0; i < int(seconds * 120); i++) {
            physics.step(scene, 1.0f / 120.0f);
            physics.writeTransforms(scene, 1.0f);
            scene.updateTransforms();
        }
    }
};

}  // namespace

TEST(a_ball_rolls_down_a_slope) {
    Yard yard;
    yard.thing("Slope", Vec3{0, 1, 0}, Collider::box(Vec3{6, 0.2f, 3}), false, false, axisAngle(Vec3{0, 0, 1}, radians(-20.0f)));
    Entity ball = yard.thing("Ball", Vec3{-3, 3.2f, 0}, Collider::sphere(0.3f));
    yard.body(ball).friction = 1.0f;
    yard.run(1.2f);
    const RigidBody& body = yard.body(ball);
    CHECK(body.velocity.x > 1.0f);                                      // down the slope
    CHECK(body.angularVelocity.z < -2.0f);                              // and turning the way a ball going that way rolls
    // Rolling, not sliding: its surface moves with the slope under it.
    CHECK_NEAR(simd::length(body.velocity), std::abs(body.angularVelocity.z) * 0.3f, 0.5);
}

TEST(a_box_hit_off_center_tumbles_and_one_that_does_not_rotate_stays_upright) {
    Yard yard;
    yard.thing("Step", Vec3{0, 0.25f, 0}, Collider::box(Vec3{0.5f, 0.25f, 2}), false);
    Entity box = yard.thing("Box", Vec3{0.75f, 2.0f, 0}, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));     // lands half on the step
    Entity stiff = yard.thing("Stiff", Vec3{0.75f, 2.0f, 1.2f}, Collider::box(Vec3{0.5f, 0.5f, 0.5f}), true, false);
    yard.run(0.9f);
    CHECK(simd::length(yard.body(box).angularVelocity) > 0.3f || std::abs(yard.scene.get<Transform>(box)->rotation.vector.z) > 0.05f);
    CHECK(simd::length(yard.body(stiff).angularVelocity) == 0);
    CHECK(std::abs(yard.scene.get<Transform>(stiff)->rotation.vector.w) > 0.9999f);
    yard.run(4);
    CHECK(yard.body(box).position.y > 0.2f && yard.body(box).position.y < 1.6f);       // it came to rest on something, not through it
    // A push at a point turns as well as moves.
    Entity free = yard.thing("Free", Vec3{8, 5, 0}, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    yard.physics.step(yard.scene, 1.0f / 120.0f);
    yard.body(free).applyImpulse(Vec3{0, 0, 2}, yard.body(free).position + Vec3{0.5f, 0, 0});
    CHECK(yard.body(free).velocity.z > 1.5f && yard.body(free).angularVelocity.y < -1.0f);
}

TEST(joints_hold_bodies_together) {
    Yard yard;
    // A weight on a ball joint, tied to the world at its anchor: a pendulum.
    Entity bob = yard.thing("Bob", Vec3{2, 6, 0}, Collider::sphere(0.25f));
    Joint swing;
    swing.anchor = Vec3{-2, 0, 0};                                      // the pivot is two meters to its left
    yard.scene.add(bob, swing);
    float lowest = 100, farthest = 0;
    for (int i = 0; i < 300; i++) {
        yard.run(1.0f / 60.0f);
        Vec3 position = yard.body(bob).position;
        lowest = std::min(lowest, position.y);
        farthest = std::max(farthest, std::abs(simd::length(position - Vec3{0, 6, 0}) - 2.0f));
    }
    CHECK(lowest < 4.3f && lowest > 3.8f);                              // it swung down to the bottom of its arc
    CHECK(farthest < 0.08f);                                            // and never left the circle

    // A rope: slack until it is taut, then it holds.
    Entity anchor = yard.thing("Hook", Vec3{10, 8, 0}, Collider::sphere(0.1f), false);
    (void)anchor;
    Entity load = yard.thing("Load", Vec3{10, 7.5f, 0}, Collider::sphere(0.2f));
    Joint rope;
    rope.kind = "distance";
    rope.connectedTo = "Hook";
    rope.length = 3;
    rope.isRope = true;
    yard.scene.add(load, rope);
    yard.run(0.4f);
    CHECK(yard.body(load).velocity.y < -2.0f);                          // falling freely while the rope is slack
    yard.run(3);
    CHECK_NEAR(yard.body(load).position.y, 5.0, 0.12);                  // hanging at its end

    // A door: a hinge about the vertical lets it swing and nothing else.
    Entity door = yard.thing("Door", Vec3{20, 3, 0}, Collider::box(Vec3{0.5f, 1, 0.05f}));
    Joint hinge;
    hinge.kind = "hinge";
    hinge.anchor = Vec3{-0.5f, 0, 0};
    hinge.axis = Vec3{0, 1, 0};
    yard.scene.add(door, hinge);
    yard.physics.step(yard.scene, 1.0f / 120.0f);
    yard.body(door).applyImpulse(Vec3{0, 0, 3}, yard.body(door).position + Vec3{0.5f, 0, 0});
    yard.run(1.5f);
    Vec3 up = rotate(yard.body(door).rotation, Vec3{0, 1, 0});
    CHECK(up.y > 0.99f);                                                // still upright
    CHECK_NEAR(yard.body(door).position.y, 3.0, 0.08);                  // still on its hinges, against gravity
    CHECK(std::abs(yard.body(door).position.z) > 0.1f);                 // and it has swung

    // Welded to another body, it goes where that goes; pulled too hard, a joint gives way.
    Entity cart = yard.thing("Cart", Vec3{30, 0.5f, 0}, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    Entity crate = yard.thing("Crate", Vec3{30, 1.5f, 0}, Collider::box(Vec3{0.4f, 0.4f, 0.4f}));
    Joint weld;
    weld.kind = "fixed";
    weld.connectedTo = "Cart";
    yard.scene.add(crate, weld);
    yard.run(0.5f);
    yard.body(cart).velocity.x = 3;
    yard.run(0.5f);
    CHECK_NEAR(yard.body(crate).position.x - yard.body(cart).position.x, 0.0, 0.15);
    Entity weak = yard.thing("Weak", Vec3{40, 6, 0}, Collider::sphere(0.25f));
    Joint thread;
    thread.breakForce = 2;                                              // it weighs ten
    yard.scene.add(weak, thread);
    yard.run(1.5f);
    CHECK(yard.scene.get<Joint>(weak)->isBroken && yard.body(weak).position.y < 2);
}

TEST(a_box_lying_flat_stays_still) {
    Yard yard;
    Entity box = yard.thing("Box", Vec3{0, 0.6f, 0}, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    Entity above = yard.thing("Above", Vec3{0.1f, 1.8f, 0}, Collider::box(Vec3{0.4f, 0.4f, 0.4f}));       // and one stacked on it
    yard.run(4);
    CHECK(simd::length(yard.body(box).angularVelocity) < 0.05f && simd::length(yard.body(above).angularVelocity) < 0.08f);
    CHECK(rotate(yard.body(box).rotation, Vec3{0, 1, 0}).y > 0.999f);
    CHECK_NEAR(yard.body(above).position.y, 1.4, 0.06);
}

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Vehicles.hpp"

TEST(mesh_colliders_are_solid_as_they_are_shaped) {
    // A slope made of two triangles, read from a file as a model would be.
    text::writeFile("/tmp/molten_slope.obj", "v -10 4 -5\nv 10 -4 -5\nv 10 -4 5\nv -10 4 5\nf 1 3 2\nf 1 4 3\n");
    World world;
    world.assetRoot = "/tmp";
    Entity slope = world.scene.create("Slope");
    world.scene.add(slope, Transform{});
    world.scene.add(slope, Collider::meshOf("molten_slope.obj"));
    Entity floor = world.scene.create("Floor");                         // a flat sheet of mesh, much bigger than the model it is made from
    Transform under;
    under.position = Vec3{0, -6, 0};
    under.scale = Vec3{80, 1, 80};
    world.scene.add(floor, under);
    world.scene.add(floor, Collider::meshOf("plane"));
    Entity ball = world.scene.create("Ball");
    Transform start;
    start.position = Vec3{-6, 4.5f, 0};
    world.scene.add(ball, start);
    RigidBody rolling;
    rolling.rotates = true;
    rolling.friction = 1;
    world.scene.add(ball, rolling);
    world.scene.add(ball, Collider::sphere(0.3f));
    Entity crate = world.scene.create("Crate");
    Transform beside;
    beside.position = Vec3{20, -4, 10};
    world.scene.add(crate, beside);
    RigidBody resting;
    resting.rotates = true;
    world.scene.add(crate, resting);
    world.scene.add(crate, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));

    world.advance(1.0 / 60.0);
    const Collider* shaped = world.scene.get<Collider>(slope);
    CHECK(shaped->mesh && shaped->mesh->triangleCount() == 2);
    CHECK(world.scene.get<Collider>(floor)->mesh != nullptr);
    // A ray from above meets the slope where it is, and reports which way it faces.
    std::optional<RaycastHit> hit = world.physics.raycast(world.scene, Ray{Vec3{5, 20, 0}, Vec3{0, -1, 0}}, 100);
    CHECK(hit.has_value());
    if (hit) {
        CHECK_NEAR(hit->point.y, -2.0, 1e-3);
        CHECK(hit->entity == slope && hit->normal.y > 0.9f && hit->normal.x > 0.3f);
    }
    CHECK(!world.physics.raycast(world.scene, Ray{Vec3{50, 20, 60}, Vec3{0, -1, 0}}, 10).has_value());

    for (int i = 0; i < 60 * 2; i++) world.advance(1.0 / 60.0);
    const RigidBody* body = world.scene.get<RigidBody>(ball);
    // It rolled down the slope, staying on it.
    CHECK(body->position.x > -3.0f && body->velocity.x > 1.5f && body->angularVelocity.z < -2.0f);
    float surface = -0.4f * body->position.x;                           // the slope's height under it
    if (body->position.x < 10) CHECK_NEAR(body->position.y, surface + 0.3f / std::cos(std::atan(0.4f)), 0.12);
    for (int i = 0; i < 60 * 6; i++) world.advance(1.0 / 60.0);
    CHECK(body->position.y > -5.9f && body->position.y < -5.5f);        // and came off its end onto the floor, not through it
    // The crate fell on the floor and lies flat on it.
    const RigidBody* box = world.scene.get<RigidBody>(crate);
    CHECK_NEAR(box->position.y, -5.5, 0.05);
    CHECK(rotate(box->rotation, Vec3{0, 1, 0}).y > 0.99f && simd::length(box->velocity) < 0.1f);

    // A car stands and drives on mesh ground as on any other.
    kit::registerKit();
    Entity car = kit::makeCar(world, Vec3{-20, -5.4f, -20});
    world.scene.get<kit::Motor>(car)->soundFolder.clear();
    kit::VehicleBody* vehicle = world.scene.get<kit::VehicleBody>(car);
    kit::Controls foot;
    foot.throttle = 1;
    vehicle->setControls(foot);
    for (int i = 0; i < 60 * 3; i++) world.advance(1.0 / 60.0);
    CHECK(vehicle->isOnGround && vehicle->speed() > 8 && std::abs(vehicle->body.position.y + 5.45f) < 0.25f);
}
