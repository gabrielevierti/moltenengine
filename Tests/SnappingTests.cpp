// Snapping onto surfaces and corners, and lining things up.
#include "Test.hpp"

#include "../Editor/Snapping.hpp"

#include <cmath>

using namespace molten;

namespace {
// A floor two meters across at y = 0, and a box's top at y = 1 over x 3..4, z -0.5..0.5.
const std::vector<Vec3> kFloor = {Vec3{-1, 0, -1}, Vec3{1, 0, -1}, Vec3{1, 0, 1}, Vec3{-1, 0, 1}};
const std::vector<uint32_t> kTwo = {0, 2, 1, 0, 3, 2};
}  // namespace

TEST(things_snap_onto_surfaces_and_corners) {
    std::vector<SnapMesh> meshes;
    meshes.push_back(SnapMesh{Entity{}, identity(), &kFloor, &kTwo});
    // A ramp: the floor turned 45 degrees about z, put at x = 5.
    Mat4 ramp = translation(Vec3{5, 0, 0}) * rotation(Quat(kPi / 4, Vec3{0, 0, 1}));
    meshes.push_back(SnapMesh{Entity{}, ramp, &kFloor, &kTwo});

    std::optional<SurfaceHit> down = hitSurface(meshes, Ray{Vec3{0.3f, 5, 0.2f}, Vec3{0, -1, 0}});
    CHECK(down && std::abs(down->point.y) < 1e-5f && std::abs(down->distance - 5) < 1e-4f && down->normal.y > 0.999f);
    // From below, the normal faces the ray: down.
    std::optional<SurfaceHit> up = hitSurface(meshes, Ray{Vec3{0.3f, -5, 0.2f}, Vec3{0, 1, 0}});
    CHECK(up && up->normal.y < -0.999f);
    CHECK(!hitSurface(meshes, Ray{Vec3{3, 5, 3}, Vec3{0, -1, 0}}));
    std::optional<SurfaceHit> onRamp = hitSurface(meshes, Ray{Vec3{5, 5, 0}, Vec3{0, -1, 0}});
    CHECK(onRamp && std::abs(onRamp->normal.x + 0.7071f) < 1e-3f && std::abs(onRamp->normal.y - 0.7071f) < 1e-3f);

    // Stood on the ramp: its up along the ramp's, its heading kept as far as it can be.
    Quat stood = standOn(onRamp->normal, Quat{});
    CHECK(simd::length(rotate(stood, Vec3{0, 1, 0}) - onRamp->normal) < 1e-4f);
    CHECK(rotate(stood, Vec3{0, 0, -1}).z < -0.99f);

    // A corner: looking down from above, the floor's corner nearest the mouse.
    Mat4 view = simd::inverse(translation(Vec3{0, 10, 0}) * rotation(Quat(-kPi / 2, Vec3{1, 0, 0})));
    Mat4 projection = perspective(radians(60), 1, 0.1f, 100);
    Vec2 screen{800, 800};
    auto toScreen = [&](Vec3 p) {
        Vec4 clip = projection * view * Vec4{p.x, p.y, p.z, 1};
        return Vec2{(clip.x / clip.w * 0.5f + 0.5f) * screen.x, (0.5f - clip.y / clip.w * 0.5f) * screen.y};
    };
    std::vector<SnapMesh> floorOnly{meshes[0]};
    std::optional<Vec3> corner = nearestVertex(floorOnly, projection * view, screen, toScreen(Vec3{0.9f, 0, 0.95f}));
    CHECK(corner && simd::length(*corner - Vec3{1, 0, 1}) < 1e-5f);
    CHECK(!nearestVertex(floorOnly, projection * view, screen, toScreen(Vec3{0, 0, 0})));        // too far from any
}

TEST(several_are_lined_up_and_spaced_evenly) {
    std::vector<Vec3> points = {Vec3{0, 1, 0}, Vec3{5, 3, 1}, Vec3{2, 2, 2}, Vec3{9, 0, 3}};
    std::vector<Vec3> lined = points;
    alignPoints(lined, 1, AlignTo::Least);
    for (const Vec3& p : lined) CHECK(p.y == 0);
    CHECK(lined[1].x == 5 && lined[1].z == 1);
    alignPoints(lined = points, 0, AlignTo::Middle);
    for (const Vec3& p : lined) CHECK(p.x == 4.5f);
    alignPoints(lined = points, 2, AlignTo::Most);
    for (const Vec3& p : lined) CHECK(p.z == 3);
    // Spread along x: 0 and 9 stay, the two between at 3 and 6, in their order.
    std::vector<Vec3> spread = points;
    distributePoints(spread, 0);
    CHECK(spread[0].x == 0 && spread[3].x == 9 && spread[2].x == 3 && spread[1].x == 6 && spread[1].y == 3);
}
