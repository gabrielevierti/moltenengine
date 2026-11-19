#include "Test.hpp"

#include "Physics/Physics.hpp"

using namespace molten;

namespace {

ShapeInstance sphereAt(Vec3 center, float radius) {
    ShapeInstance s;
    s.shape = ColliderShape::Sphere;
    s.center = center;
    s.radius = radius;
    return s;
}

ShapeInstance boxAt(Vec3 center, Vec3 halfExtents, Quat rotation = Quat{}) {
    ShapeInstance s;
    s.shape = ColliderShape::Box;
    s.center = center;
    s.halfExtents = halfExtents;
    s.rotation = rotation;
    return s;
}

ShapeInstance capsuleAt(Vec3 center, float radius, float halfHeight) {
    ShapeInstance s;
    s.shape = ColliderShape::Capsule;
    s.center = center;
    s.radius = radius;
    s.halfHeight = halfHeight;
    return s;
}

Entity addStaticBox(Scene& scene, Vec3 center, Vec3 halfExtents, Quat rotation = Quat{}) {
    Entity e = scene.create();
    scene.add(e, Transform{.position = center, .rotation = rotation});
    scene.add(e, Collider::box(halfExtents));
    return e;
}

Entity addBall(Scene& scene, Vec3 center, float radius = 0.5f) {
    Entity e = scene.create();
    scene.add(e, Transform{.position = center});
    scene.add(e, Collider::sphere(radius));
    scene.add(e, RigidBody{});
    return e;
}

void run(Physics& physics, Scene& scene, float seconds) {
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < int(seconds / dt); i++) physics.step(scene, dt);
}

}  // namespace

TEST(sphere_sphere_contact) {
    ContactInfo info;
    CHECK(collide(sphereAt(Vec3{0, 0, 0}, 1), sphereAt(Vec3{1.5f, 0, 0}, 1), info));
    CHECK_NEAR(info.normal.x, 1, 1e-5);
    CHECK_NEAR(info.depth, 0.5, 1e-5);
    CHECK(!collide(sphereAt(Vec3{0, 0, 0}, 1), sphereAt(Vec3{2.5f, 0, 0}, 1), info));
}

TEST(sphere_box_contact_outside_inside_and_rotated) {
    ContactInfo info;
    // Resting on top of a box: the normal points from the sphere down to the box.
    CHECK(collide(sphereAt(Vec3{0, 1.4f, 0}, 0.5f), boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), info));
    CHECK_NEAR(info.normal.y, -1, 1e-5);
    CHECK_NEAR(info.depth, 0.1, 1e-5);
    // The reversed order gives the reversed normal.
    CHECK(collide(boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), sphereAt(Vec3{0, 1.4f, 0}, 0.5f), info));
    CHECK_NEAR(info.normal.y, 1, 1e-5);
    // Center inside the box: pushed out through the nearest face (+x here).
    CHECK(collide(sphereAt(Vec3{0.9f, 0, 0}, 0.5f), boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), info));
    CHECK_NEAR(info.normal.x, -1, 1e-5);
    CHECK_NEAR(info.depth, 0.6, 1e-5);
    // A box turned 45 degrees around Z has a corner at height sqrt(2).
    Quat turned = axisAngle(Vec3{0, 0, 1}, radians(45));
    CHECK(collide(sphereAt(Vec3{0, 1.7f, 0}, 0.5f), boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}, turned), info));
    CHECK(!collide(sphereAt(Vec3{1.4f, 1.4f, 0}, 0.5f), boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}, turned), info));
}

TEST(box_box_separating_axes) {
    ContactInfo info;
    CHECK(collide(boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), boxAt(Vec3{1.5f, 0, 0}, Vec3{1, 1, 1}), info));
    CHECK_NEAR(info.normal.x, 1, 1e-5);
    CHECK_NEAR(info.depth, 0.5, 1e-5);
    CHECK(!collide(boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), boxAt(Vec3{2.1f, 0, 0}, Vec3{1, 1, 1}), info));
    // Turned 45 degrees, the second box reaches sqrt(2) along x, so it touches at 2.3 but not at 2.5.
    Quat turned = axisAngle(Vec3{0, 1, 0}, radians(45));
    CHECK(collide(boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), boxAt(Vec3{2.3f, 0, 0}, Vec3{1, 1, 1}, turned), info));
    CHECK(!collide(boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}), boxAt(Vec3{2.5f, 0, 0}, Vec3{1, 1, 1}, turned), info));
}

TEST(contact_normal_and_depth_separate_the_shapes) {
    // For any pair that overlaps, moving the second shape along the contact normal by
    // the contact depth must be exactly enough to separate the two. This checks the
    // direction and the size of every shape-pair test at once.
    Random random(2024);
    int overlapping = 0;
    for (int i = 0; i < 4000; i++) {
        ShapeInstance shapes[2];
        for (ShapeInstance& s : shapes) {
            s.shape = ColliderShape(random.rangeInt(0, 3));
            s.center = random.insideUnitSphere() * 1.2f;
            s.rotation = Quat(random.range(0, 2 * kPi), random.onUnitSphere());
            s.halfExtents = Vec3{random.range(0.2f, 1.0f), random.range(0.2f, 1.0f), random.range(0.2f, 1.0f)};
            s.radius = random.range(0.2f, 0.8f);
            s.halfHeight = random.range(0.1f, 0.8f);
        }
        ContactInfo info;
        if (!collide(shapes[0], shapes[1], info)) continue;
        overlapping++;
        CHECK_NEAR(simd::length(info.normal), 1, 1e-3);
        CHECK(info.depth >= 0);

        float margin = 0.002f;
        ShapeInstance moved = shapes[1];
        moved.center += info.normal * (info.depth + margin);
        ContactInfo after;
        bool stillTouching = collide(shapes[0], moved, after);
        CHECK(!stillTouching || after.depth < margin * 2);

        moved = shapes[1];
        moved.center += info.normal * (info.depth * 0.5f);
        if (info.depth > 0.02f) CHECK(collide(shapes[0], moved, after));
    }
    CHECK(overlapping > 500);
}

TEST(capsule_contacts) {
    ContactInfo info;
    ShapeInstance capsule = capsuleAt(Vec3{0, 1, 0}, 0.5f, 0.5f);       // spans y = 0 .. 2
    CHECK(collide(capsule, sphereAt(Vec3{0.8f, 1.2f, 0}, 0.5f), info));
    CHECK_NEAR(info.normal.x, 1, 1e-5);
    CHECK_NEAR(info.depth, 0.2, 1e-5);
    CHECK(collide(capsule, boxAt(Vec3{0, -0.4f, 0}, Vec3{5, 0.5f, 5}), info));      // standing on a floor
    CHECK_NEAR(info.normal.y, -1, 1e-4);
    CHECK_NEAR(info.depth, 0.1, 1e-4);
    CHECK(collide(capsule, capsuleAt(Vec3{0.9f, 1, 0}, 0.5f, 0.5f), info));
    CHECK_NEAR(info.depth, 0.1, 1e-4);
    CHECK(!collide(capsule, capsuleAt(Vec3{1.1f, 1, 0}, 0.5f, 0.5f), info));
}

TEST(closest_points_between_segments) {
    Vec3 a, b;
    closestPointsBetweenSegments(Vec3{-1, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, -1}, Vec3{0, 1, 1}, a, b);
    CHECK_NEAR(simd::distance(a, Vec3{0, 0, 0}), 0, 1e-5);
    CHECK_NEAR(simd::distance(b, Vec3{0, 1, 0}), 0, 1e-5);
    closestPointsBetweenSegments(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{3, 1, 0}, Vec3{5, 1, 0}, a, b);
    CHECK_NEAR(a.x, 1, 1e-5); CHECK_NEAR(b.x, 3, 1e-5);
}

TEST(shape_raycasts) {
    Ray down{Vec3{0, 10, 0}, Vec3{0, -1, 0}};
    auto hit = raycastShape(down, sphereAt(Vec3{0, 0, 0}, 1));
    CHECK(hit.has_value());
    CHECK_NEAR(hit->distance, 9, 1e-5);
    CHECK_NEAR(hit->normal.y, 1, 1e-5);

    hit = raycastShape(Ray{Vec3{-10, 0.2f, 0.3f}, Vec3{1, 0, 0}}, boxAt(Vec3{0, 0, 0}, Vec3{1, 2, 3}));
    CHECK(hit.has_value());
    CHECK_NEAR(hit->distance, 9, 1e-5);
    CHECK_NEAR(hit->normal.x, -1, 1e-5);

    // A box turned 45 degrees around Y shows an edge to a ray coming along x.
    hit = raycastShape(Ray{Vec3{-10, 0, 0.5f}, Vec3{1, 0, 0}}, boxAt(Vec3{0, 0, 0}, Vec3{1, 1, 1}, axisAngle(Vec3{0, 1, 0}, radians(45))));
    CHECK(hit.has_value());
    CHECK_NEAR(hit->distance, 10 - (std::sqrt(2.0f) - 0.5f), 1e-4);
    CHECK(hit->normal.x < -0.7f && hit->normal.z > 0.7f);

    hit = raycastShape(Ray{Vec3{-10, 1, 0}, Vec3{1, 0, 0}}, capsuleAt(Vec3{0, 1, 0}, 0.5f, 0.5f));
    CHECK(hit.has_value());
    CHECK_NEAR(hit->distance, 9.5, 1e-4);
    hit = raycastShape(down, capsuleAt(Vec3{0, 1, 0}, 0.5f, 0.5f));               // along the axis: hits the top cap
    CHECK(hit.has_value());
    CHECK_NEAR(hit->distance, 8, 1e-4);
    CHECK(!raycastShape(Ray{Vec3{-10, 5, 0}, Vec3{1, 0, 0}}, capsuleAt(Vec3{0, 1, 0}, 0.5f, 0.5f)).has_value());
}

TEST(a_ball_falls_bounces_and_comes_to_rest_on_the_floor) {
    Scene scene;
    Physics physics;
    addStaticBox(scene, Vec3{0, -0.5f, 0}, Vec3{10, 0.5f, 10});
    Entity ball = addBall(scene, Vec3{0, 3, 0});
    scene.get<RigidBody>(ball)->restitution = 0.5f;

    run(physics, scene, 0.5f);
    CHECK(scene.get<RigidBody>(ball)->position.y < 3);                   // it fell
    run(physics, scene, 6.0f);
    const RigidBody& body = *scene.get<RigidBody>(ball);
    CHECK_NEAR(body.position.y, 0.5, 0.02);                              // resting on the floor, not sinking into it
    CHECK(simd::length(body.velocity) < 0.2f);
    CHECK(body.isGrounded);
    CHECK_NEAR(body.groundNormal.y, 1, 1e-3);
}

TEST(a_ball_never_bounces_higher_than_it_started) {
    Scene scene;
    Physics physics;
    addStaticBox(scene, Vec3{0, -0.5f, 0}, Vec3{10, 0.5f, 10});
    Entity ball = addBall(scene, Vec3{0, 4, 0});
    scene.get<RigidBody>(ball)->restitution = 0.9f;
    float highestAfterBounce = 0;
    bool bounced = false;
    for (int i = 0; i < 120 * 5; i++) {
        physics.step(scene, 1.0f / 120.0f);
        const RigidBody& body = *scene.get<RigidBody>(ball);
        if (body.position.y < 0.6f) bounced = true;
        if (bounced) highestAfterBounce = std::max(highestAfterBounce, body.position.y);
    }
    CHECK(bounced);
    CHECK(highestAfterBounce > 2.0f);            // it does bounce
    CHECK(highestAfterBounce < 4.0f);            // but loses energy
}

TEST(a_ball_rolls_down_a_ramp_and_friction_stops_a_box) {
    Scene scene;
    Physics physics;
    // A ramp tilted 20 degrees around Z: it goes down toward +x.
    addStaticBox(scene, Vec3{0, 0, 0}, Vec3{10, 0.5f, 10}, axisAngle(Vec3{0, 0, 1}, radians(-20)));
    Entity ball = addBall(scene, Vec3{0, 1.2f, 0});
    scene.get<RigidBody>(ball)->friction = 0.0f;
    run(physics, scene, 1.0f);
    CHECK(scene.get<RigidBody>(ball)->position.x > 0.5f);
    CHECK(scene.get<RigidBody>(ball)->velocity.x > 0.5f);

    // A sliding box on a flat floor is stopped by friction.
    Scene flat;
    Physics flatPhysics;
    addStaticBox(flat, Vec3{0, -0.5f, 0}, Vec3{50, 0.5f, 50});
    Entity crate = flat.create();
    flat.add(crate, Transform{.position = {0, 0.5f, 0}});
    flat.add(crate, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    flat.add(crate, RigidBody{.friction = 0.8f, .velocity = {5, 0, 0}});
    run(flatPhysics, flat, 3.0f);
    CHECK(std::abs(flat.get<RigidBody>(crate)->velocity.x) < 0.05f);
    CHECK(flat.get<RigidBody>(crate)->position.x > 0.5f);
    CHECK(flat.get<RigidBody>(crate)->position.x < 5.0f);
}

TEST(boxes_stack_and_dynamic_bodies_push_each_other) {
    Scene scene;
    Physics physics;
    addStaticBox(scene, Vec3{0, -0.5f, 0}, Vec3{10, 0.5f, 10});
    Entity lower = scene.create(), upper = scene.create();
    scene.add(lower, Transform{.position = {0, 0.5f, 0}});
    scene.add(lower, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    scene.add(lower, RigidBody{});
    scene.add(upper, Transform{.position = {0.1f, 1.6f, 0}});
    scene.add(upper, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
    scene.add(upper, RigidBody{});
    run(physics, scene, 3.0f);
    CHECK_NEAR(scene.get<RigidBody>(lower)->position.y, 0.5, 0.03);
    CHECK_NEAR(scene.get<RigidBody>(upper)->position.y, 1.5, 0.05);
    CHECK(scene.get<RigidBody>(upper)->isGrounded);
}

TEST(a_kinematic_platform_lifts_and_carries_a_ball) {
    Scene scene;
    Physics physics;
    Entity platform = scene.create();
    scene.add(platform, Transform{});
    scene.add(platform, Collider::box(Vec3{2, 0.25f, 2}));
    scene.add(platform, RigidBody{.type = BodyType::Kinematic, .velocity = {1, 1, 0}});
    Entity ball = addBall(scene, Vec3{0, 0.75f, 0});
    run(physics, scene, 2.0f);
    const RigidBody& body = *scene.get<RigidBody>(ball);
    CHECK_NEAR(scene.get<RigidBody>(platform)->position.y, 2, 0.02);     // the platform ignores the ball's weight
    CHECK_NEAR(body.position.y, 2.75, 0.05);                             // the ball rides on top
    CHECK(body.position.x > 1.0f);                                       // and friction drags it along
}

TEST(triggers_report_enter_and_exit_once) {
    Scene scene;
    Physics physics;
    physics.gravity = Vec3{0, 0, 0};
    Entity zone = scene.create();
    scene.add(zone, Transform{.position = {5, 0, 0}});
    Collider zoneCollider = Collider::box(Vec3{1, 1, 1});
    zoneCollider.isTrigger = true;
    scene.add(zone, zoneCollider);
    Entity ball = addBall(scene, Vec3{0, 0, 0});
    scene.get<RigidBody>(ball)->velocity = Vec3{4, 0, 0};
    scene.get<RigidBody>(ball)->linearDamping = 0;

    int entered = 0, exited = 0;
    physics.onTriggerEnter = [&](Entity trigger, Entity other) {
        entered++;
        CHECK(trigger == zone && other == ball);
    };
    physics.onTriggerExit = [&](Entity, Entity) { exited++; };
    run(physics, scene, 3.0f);
    CHECK(entered == 1 && exited == 1);
    CHECK_NEAR(scene.get<RigidBody>(ball)->velocity.x, 4, 1e-3);          // a trigger does not block
}

TEST(contact_begin_fires_once_per_touch_with_impact_speed) {
    Scene scene;
    Physics physics;
    addStaticBox(scene, Vec3{0, -0.5f, 0}, Vec3{10, 0.5f, 10});
    Entity ball = addBall(scene, Vec3{0, 2, 0});
    scene.get<RigidBody>(ball)->restitution = 0;
    int begins = 0;
    float impact = 0;
    physics.onContactBegin = [&](const Contact& contact) {
        begins++;
        impact = contact.impactSpeed;
    };
    run(physics, scene, 3.0f);
    CHECK(begins == 1);
    CHECK(impact > 4.0f && impact < 6.5f);       // sqrt(2 g h) with h = 1.5 is about 5.4
}

TEST(raycast_layers_and_overlap_queries) {
    Scene scene;
    Physics physics;
    Entity nearBox = addStaticBox(scene, Vec3{0, 0, -5}, Vec3{1, 1, 1});
    Entity farBox = addStaticBox(scene, Vec3{0, 0, -10}, Vec3{1, 1, 1});
    scene.get<Collider>(nearBox)->layer = 2;
    Ray ray{Vec3{0, 0, 0}, Vec3{0, 0, -1}};

    auto hit = physics.raycast(scene, ray);
    CHECK(hit.has_value() && hit->entity == nearBox);
    CHECK_NEAR(hit->distance, 4, 1e-5);
    CHECK_NEAR(hit->normal.z, 1, 1e-5);
    hit = physics.raycast(scene, ray, 1e9f, 1u);                           // only layer 1: skips the near box
    CHECK(hit.has_value() && hit->entity == farBox);
    CHECK(!physics.raycast(scene, ray, 3.0f).has_value());                 // too short
    hit = physics.raycast(scene, ray, 1e9f, 0xFFFFFFFFu, nearBox);         // ignoring an entity
    CHECK(hit.has_value() && hit->entity == farBox);

    CHECK(physics.overlapSphere(scene, Vec3{0, 0, -7.5f}, 2.0f).size() == 2);
    CHECK(physics.overlapSphere(scene, Vec3{0, 0, -5}, 0.5f).size() == 1);

    // Colliders on layers that do not mask each other pass through.
    Scene layered;
    Physics layeredPhysics;
    Entity floor = addStaticBox(layered, Vec3{0, -0.5f, 0}, Vec3{10, 0.5f, 10});
    layered.get<Collider>(floor)->layer = 4;
    Entity ghost = addBall(layered, Vec3{0, 1, 0});
    layered.get<Collider>(ghost)->mask = 1;
    run(layeredPhysics, layered, 2.0f);
    CHECK(layered.get<RigidBody>(ghost)->position.y < -1.0f);
}

TEST(teleport_and_interpolation) {
    Scene scene;
    Physics physics;
    physics.gravity = Vec3{0, 0, 0};
    Entity ball = addBall(scene, Vec3{0, 0, 0});
    scene.get<RigidBody>(ball)->velocity = Vec3{1.2f, 0, 0};
    scene.get<RigidBody>(ball)->linearDamping = 0;
    physics.step(scene, 0.1f);
    physics.step(scene, 0.1f);
    physics.writeTransforms(scene, 0.5f);
    CHECK_NEAR(scene.get<Transform>(ball)->position.x, 0.18, 1e-5);       // halfway between 0.12 and 0.24

    physics.teleport(scene, ball, Vec3{50, 0, 0});
    physics.writeTransforms(scene, 0.5f);
    CHECK_NEAR(scene.get<Transform>(ball)->position.x, 50, 1e-5);         // no smear across the jump
}

TEST(the_simulation_is_deterministic) {
    Vec3 results[2];
    for (int runIndex = 0; runIndex < 2; runIndex++) {
        Scene scene;
        Physics physics;
        addStaticBox(scene, Vec3{0, -0.5f, 0}, Vec3{10, 0.5f, 10}, axisAngle(Vec3{1, 0, 0}, radians(5)));
        Entity a = addBall(scene, Vec3{0, 2, 0}), b = addBall(scene, Vec3{0.3f, 3.5f, 0.1f});
        scene.get<RigidBody>(b)->restitution = 0.6f;
        run(physics, scene, 4.0f);
        results[runIndex] = scene.get<RigidBody>(a)->position + scene.get<RigidBody>(b)->position;
    }
    CHECK(simd::all(results[0] == results[1]));
}
