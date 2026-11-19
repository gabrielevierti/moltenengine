#include "Test.hpp"

#include "Math/Math.hpp"

using namespace molten;

TEST(perspective_maps_near_to_0_and_far_to_1) {
    Mat4 p = perspective(radians(60), 1.5f, 0.1f, 100.0f);
    Vec4 nearPoint = p * Vec4{0, 0, -0.1f, 1}, farPoint = p * Vec4{0, 0, -100, 1};
    CHECK_NEAR(nearPoint.z / nearPoint.w, 0, 1e-5);
    CHECK_NEAR(farPoint.z / farPoint.w, 1, 1e-4);
    // A point on the top edge of the view lands on y = 1.
    Vec4 top = p * Vec4{0, std::tan(radians(30)) * 10, -10, 1};
    CHECK_NEAR(top.y / top.w, 1, 1e-4);
}

TEST(orthographic_maps_box_to_clip_space) {
    Mat4 o = orthographic(-2, 2, -1, 1, 1, 11);
    Vec4 a = o * Vec4{2, 1, -1, 1}, b = o * Vec4{-2, -1, -11, 1};
    CHECK_NEAR(a.x, 1, 1e-6); CHECK_NEAR(a.y, 1, 1e-6); CHECK_NEAR(a.z, 0, 1e-6);
    CHECK_NEAR(b.x, -1, 1e-6); CHECK_NEAR(b.y, -1, 1e-6); CHECK_NEAR(b.z, 1, 1e-6);
}

TEST(look_at_puts_target_on_negative_z) {
    Mat4 view = lookAt(Vec3{3, 4, 5}, Vec3{0, 1, 0});
    Vec3 target = transformPoint(view, Vec3{0, 1, 0});
    CHECK_NEAR(target.x, 0, 1e-5); CHECK_NEAR(target.y, 0, 1e-5);
    CHECK(target.z < 0);
    CHECK_NEAR(-target.z, simd::distance(Vec3{3, 4, 5}, Vec3{0, 1, 0}), 1e-4);
}

TEST(look_rotation_turns_minus_z_to_forward) {
    for (Vec3 forward : {Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{1, 1, 1}, Vec3{0, 1, 0}, Vec3{0, -1, 0}}) {
        Quat q = lookRotation(forward);
        Vec3 result = rotate(q, Vec3{0, 0, -1});
        Vec3 expected = simd::normalize(forward);
        CHECK_NEAR(simd::distance(result, expected), 0, 1e-4);
    }
    Vec3 up = rotate(lookRotation(Vec3{1, 0, 0}), Vec3{0, 1, 0});
    CHECK_NEAR(up.y, 1, 1e-5);
}

TEST(compose_equals_translate_rotate_scale) {
    Vec3 t{1, 2, 3}, s{2, 3, 4};
    Quat r = eulerAngles(0.3f, 1.1f, -0.4f);
    Mat4 expected = translation(t) * rotation(r) * scaling(s);
    CHECK(simd::almost_equal_elements(compose(t, r, s), expected, 1e-5f));
}

TEST(normal_matrix_keeps_normals_perpendicular) {
    Mat4 model = compose(Vec3{0, 0, 0}, eulerAngles(0.5f, 0.2f), Vec3{1, 4, 0.5f});
    Vec3 tangent = transformDirection(model, Vec3{1, 0, 0});
    Vec3 normal = transformDirection(normalMatrix(model), Vec3{0, 1, 0});
    CHECK_NEAR(simd::dot(tangent, normal), 0, 1e-5);
}

TEST(frustum_culls_boxes_outside_the_view) {
    Mat4 viewProjection = perspective(radians(60), 1.0f, 0.1f, 100.0f) * lookAt(Vec3{0, 0, 0}, Vec3{0, 0, -1});
    Frustum frustum(viewProjection);
    Vec3 half{0.5f, 0.5f, 0.5f};
    CHECK(frustum.intersects(AABB::fromCenter(Vec3{0, 0, -10}, half)));
    CHECK(!frustum.intersects(AABB::fromCenter(Vec3{0, 0, 10}, half)));        // behind the camera
    CHECK(!frustum.intersects(AABB::fromCenter(Vec3{0, 0, -200}, half)));      // beyond the far plane
    CHECK(!frustum.intersects(AABB::fromCenter(Vec3{-30, 0, -10}, half)));     // far to the left
    CHECK(frustum.intersects(AABB::fromCenter(Vec3{-5.9f, 0, -10}, half)));    // straddles the left plane
    CHECK(!frustum.intersects(AABB::fromCenter(Vec3{0, 0, -0.05f}, Vec3{0.02f, 0.02f, 0.02f})));   // between the camera and the near plane
}

TEST(frustum_can_ignore_its_near_plane) {
    // A shadow map's box: whatever stands between the light and the box must still count.
    Mat4 viewProjection = orthographic(-5, 5, -5, 5, 0, 20) * lookAt(Vec3{0, 0, 0}, Vec3{0, 0, -1});
    Frustum frustum(viewProjection);
    Vec3 half{0.5f, 0.5f, 0.5f};
    AABB inFront = AABB::fromCenter(Vec3{0, 0, 30}, half);                      // on the light's side of the near plane
    CHECK(!frustum.intersects(inFront));
    CHECK(frustum.intersects(inFront, false));
    CHECK(!frustum.intersects(AABB::fromCenter(Vec3{0, 0, -30}, half), false)); // beyond the far plane: still out
    CHECK(!frustum.intersects(AABB::fromCenter(Vec3{9, 0, 30}, half), false));  // off to the side: still out
}

TEST(aabb_transformed_encloses_rotated_box) {
    AABB box(Vec3{-1, -1, -1}, Vec3{1, 1, 1});
    AABB turned = box.transformed(rotation(axisAngle(Vec3{0, 1, 0}, radians(45))));
    CHECK_NEAR(turned.max.x, std::sqrt(2.0f), 1e-5);
    CHECK_NEAR(turned.max.y, 1, 1e-5);
    AABB moved = box.transformed(translation(Vec3{5, 0, 0}));
    CHECK_NEAR(moved.min.x, 4, 1e-5);
}

TEST(ray_intersections) {
    Ray ray{Vec3{0, 0, 5}, Vec3{0, 0, -1}};
    auto sphere = intersectSphere(ray, Vec3{0, 0, 0}, 1);
    CHECK(sphere.has_value());
    CHECK_NEAR(*sphere, 4, 1e-5);
    CHECK(!intersectSphere(ray, Vec3{3, 0, 0}, 1).has_value());
    auto box = intersectAABB(ray, AABB(Vec3{-1, -1, -1}, Vec3{1, 1, 2}));
    CHECK(box.has_value());
    CHECK_NEAR(*box, 3, 1e-5);
    CHECK(!intersectAABB(Ray{Vec3{0, 5, 5}, Vec3{0, 0, -1}}, AABB(Vec3{-1, -1, -1}, Vec3{1, 1, 1})).has_value());
    auto plane = intersectPlane(Ray{Vec3{0, 3, 0}, Vec3{0, -1, 0}}, Plane{Vec3{0, 1, 0}, 0});
    CHECK(plane.has_value());
    CHECK_NEAR(*plane, 3, 1e-5);
}

TEST(random_is_deterministic_and_in_range) {
    // These exact values are also checked by the Swift engine's tests: both
    // implementations must produce the same sequence.
    Random rng(42);
    CHECK(rng.nextUInt() == 3270867926u);
    CHECK(rng.nextUInt() == 1795671209u);
    CHECK(rng.nextUInt() == 1924641435u);
    CHECK_NEAR(rng.next(), 0.266133, 1e-5);

    Random other(7);
    for (int i = 0; i < 1000; i++) {
        float value = other.range(2.0f, 3.0f);
        CHECK(value >= 2.0f && value < 3.0f);
        CHECK_NEAR(simd::length(other.onUnitSphere()), 1, 1e-4);
        Vec3 inCone = other.inCone(Vec3{0, 1, 0}, radians(20));
        CHECK(inCone.y >= std::cos(radians(20)) - 1e-4f);
    }
}

TEST(damp_is_frame_rate_independent) {
    float a = 0, b = 0;
    for (int i = 0; i < 60; i++) a = damp(a, 10, 5, 1.0f / 60.0f);
    for (int i = 0; i < 240; i++) b = damp(b, 10, 5, 1.0f / 240.0f);
    CHECK_NEAR(a, b, 1e-3);
    CHECK(a > 9.9f);
}

TEST(euler_angles_come_back_out_of_a_rotation) {
    const Vec3 cases[] = {{0.3f, -1.2f, 0.5f}, {-1.0f, 2.5f, -0.7f}, {0, 0, 0}, {1.2f, 0.1f, 3.0f}, {0.0f, -3.0f, 0.0f}};
    for (Vec3 angles : cases) {
        Vec3 back = toEulerAngles(eulerAngles(angles.x, angles.y, angles.z));
        CHECK_NEAR(back.x, angles.x, 1e-4);
        CHECK_NEAR(back.y, angles.y, 1e-4);
        CHECK_NEAR(back.z, angles.z, 1e-4);
    }
    // Straight up: only the rotation as a whole can be recovered, not the split between yaw and roll.
    Quat up = eulerAngles(kPi / 2, 0.4f, 0.3f);
    Vec3 angles = toEulerAngles(up);
    Quat again = eulerAngles(angles.x, angles.y, angles.z);
    CHECK_NEAR(std::abs(simd::dot(up.vector, again.vector)), 1, 1e-4);
}

TEST(noise_is_repeatable_smooth_and_stays_in_range) {
    CHECK(hashNoise(3, -7, 1) == hashNoise(3, -7, 1));
    CHECK(hashNoise(3, -7, 1) != hashNoise(3, -7, 2));
    CHECK(hashNoise(3, -7, 1) != hashNoise(4, -7, 1));
    float lowest = 1, highest = 0, sum = 0;
    int count = 0;
    for (float y = -6; y < 6; y += 0.173f) {
        for (float x = -6; x < 6; x += 0.191f) {
            float n = fractalNoise(Vec2{x, y}, 4, 9);
            CHECK(n >= 0 && n < 1);
            lowest = std::min(lowest, n);
            highest = std::max(highest, n);
            sum += n;
            count++;
            // Smooth: a small step changes the value a little.
            CHECK(std::abs(valueNoise(Vec2{x + 0.01f, y}) - valueNoise(Vec2{x, y})) < 0.03f);
        }
    }
    CHECK(lowest < 0.3f && highest > 0.7f);                 // it uses its range
    CHECK_NEAR(sum / float(count), 0.5f, 0.05f);
    // At the grid points it is exactly the hash.
    CHECK_NEAR(valueNoise(Vec2{2, 5}, 3), hashNoise(2, 5, 3), 1e-6);
}

TEST(decompose_is_the_reverse_of_compose) {
    Vec3 position{1.5f, -2.0f, 7.25f};
    Quat rotation = eulerAngles(radians(25.0f), radians(-130.0f), radians(40.0f));
    Vec3 scale{2.0f, 0.5f, 3.0f};
    Vec3 p, s;
    Quat r;
    decompose(compose(position, rotation, scale), p, r, s);
    CHECK_NEAR(simd::distance(p, position), 0, 1e-5);
    CHECK_NEAR(simd::distance(s, scale), 0, 1e-4);
    // q and -q are the same rotation.
    CHECK_NEAR(std::abs(simd::dot(r.vector, rotation.vector)), 1, 1e-4);

    // Taking a child out of its parent and putting it back gives the same world matrix:
    // what the editor does when an entity is dragged under another.
    Mat4 parent = compose(Vec3{3, 1, -2}, eulerAngles(0.3f, 1.1f, -0.4f), Vec3{2, 2, 2});
    Mat4 world = compose(position, rotation, scale);
    decompose(simd::inverse(parent) * world, p, r, s);
    CHECK(simd::almost_equal_elements(parent * compose(p, r, s), world, 1e-3f));

    // A matrix with no size has no rotation to recover: it comes back as none, not as NaN.
    decompose(scaling(Vec3{0, 0, 0}), p, r, s);
    CHECK(std::abs(simd::dot(r.vector, r.vector) - 1.0f) < 1e-5f || simd::dot(r.vector, r.vector) == 0.0f);
    CHECK(!std::isnan(r.vector.x));
}
