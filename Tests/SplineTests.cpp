// Splines: curves through points, things that follow them, and roads along them.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Spline.hpp"

#include <cmath>

using namespace molten;
using namespace molten::kit;

TEST(a_spline_goes_through_its_points) {
    SplinePath path;
    std::vector<Vec3> points = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{10, 0, 10}, Vec3{0, 0, 10}};
    path.build(points, false, 16);
    CHECK(path.points.size() == 3 * 16 + 1);
    for (size_t i = 0; i < points.size(); i++) CHECK(simd::length(path.points[i * 16] - points[i]) < 1e-4f);
    CHECK(path.length() > 30 && path.length() < 34);              // a little more than the 30 m of straight lines
    CHECK(simd::length(path.pointAt(-5) - points[0]) < 1e-4f);    // held at the ends when open
    CHECK(simd::length(path.pointAt(1000) - points[3]) < 1e-4f);
    CHECK(simd::dot(path.directionAt(0.1f), Vec3{1, 0, 0}) > 0.95f);
    // The nearest point of the curve, found again from where it is.
    float along = path.length() * 0.37f;
    CHECK(std::abs(path.closestDistance(path.pointAt(along) + Vec3{0, 1, 0}) - along) < 0.1f);

    // Closed: round again past the end, and the start is reached by going on.
    path.build(points, true, 16);
    CHECK(path.points.size() == 4 * 16 + 1 && simd::length(path.points.back() - points[0]) < 1e-4f);
    CHECK(simd::length(path.pointAt(path.length() + 1) - path.pointAt(1)) < 1e-3f);
    CHECK(simd::length(path.pointAt(-1) - path.pointAt(path.length() - 1)) < 1e-3f);

    // A straight line stays straight, and is as long as it is.
    path.build(std::vector<Vec3>{Vec3{0, 0, 0}, Vec3{5, 0, 0}, Vec3{10, 0, 0}}, false, 8);
    CHECK(std::abs(path.length() - 10) < 1e-3f);
    for (const Vec3& p : path.points) CHECK(std::abs(p.y) < 1e-5f && std::abs(p.z) < 1e-5f);
}

namespace {
Entity makeSpline(World& world, const char* name, std::vector<Vec3> points, bool closed) {
    Entity line = world.scene.create(name);
    world.scene.add(line, Transform{});
    Spline spline;
    spline.closed = closed;
    world.scene.add(line, spline);
    // Made in another order than their numbers say: it is the numbers that count.
    for (size_t k = points.size(); k-- > 0;) {
        Entity point = world.scene.create("Point " + std::to_string(k + 1));
        Transform transform;
        transform.position = points[k];
        transform.parent = line;
        world.scene.add(point, transform);
    }
    world.scene.updateTransforms();
    return line;
}
}  // namespace

TEST(a_spline_s_points_are_its_children_by_number) {
    World world;
    std::vector<Vec3> points;
    for (int k = 0; k < 12; k++) points.push_back(Vec3{float(k) * 3, 0, float(k % 2)});
    Entity line = makeSpline(world, "Line", points, false);
    std::vector<Entity> found = Spline::pointsOf(world, line);
    CHECK(found.size() == 12);
    CHECK(world.scene.get<Name>(found[1])->value == "Point 2" && world.scene.get<Name>(found[11])->value == "Point 12");
    Spline* spline = findSpline(world, "Line");
    CHECK(spline && spline->path.points.size() == 11 * 16 + 1);
    CHECK(!spline->refresh(world, line));                          // nothing moved: nothing made again
    world.scene.get<Transform>(found[4])->position.y = 2;
    world.scene.updateTransforms();
    CHECK(spline->refresh(world, line));
}

TEST(things_follow_a_spline) {
    World world;
    kit::registerKit();
    makeSpline(world, "Rail", std::vector<Vec3>{Vec3{0, 0, 0}, Vec3{20, 0, 0}}, false);
    Entity cart = world.scene.create("Cart");
    world.scene.add(cart, Transform{});
    SplineFollower follower;
    follower.spline = "Rail";
    follower.speed = 4;
    follower.ending = 1;                                            // back and forth
    world.scene.add(cart, follower);
    for (int i = 0; i < 60; i++) world.advance(1.0 / 60.0);
    Vec3 at = world.scene.get<Transform>(cart)->position;
    CHECK(std::abs(at.x - 4) < 0.2f && std::abs(at.z) < 1e-3f);
    // It looks the way it goes: forward is -z, so +x is a quarter turn.
    Vec3 forward = rotate(world.scene.get<Transform>(cart)->rotation, Vec3{0, 0, -1});
    CHECK(forward.x > 0.99f);
    for (int i = 0; i < 60 * 5; i++) world.advance(1.0 / 60.0);    // 24 m: to the end and 4 back
    at = world.scene.get<Transform>(cart)->position;
    CHECK(std::abs(at.x - 16) < 0.3f);
    forward = rotate(world.scene.get<Transform>(cart)->rotation, Vec3{0, 0, -1});
    CHECK(forward.x < -0.99f);
}

TEST(a_road_is_laid_along_a_spline_and_can_be_driven_on) {
    World world;
    kit::registerKit();
    Entity road = makeSpline(world, "Road", std::vector<Vec3>{Vec3{0, 0, 0}, Vec3{30, 0, 0}, Vec3{30, 0, 30}, Vec3{0, 0, 30}}, true);
    SplineRoad strip;
    strip.width = 6;
    world.scene.add(road, strip);
    world.scene.add(road, MeshRenderer{});
    CHECK(world.scene.get<SplineRoad>(road)->rebuild(world, road));
    // The mesh: two edges each point of the curve, 6 m apart.
    const Spline* spline = world.scene.get<Spline>(road);
    MeshData mesh = SplineRoad::makeMesh(spline->path, simd::inverse(world.scene.get<Transform>(road)->world), 6, 8, 0);
    CHECK(mesh.positions.size() == spline->path.points.size() * 2);
    CHECK(std::abs(simd::length(mesh.positions[0] - mesh.positions[1]) - 6) < 1e-3f);
    CHECK(mesh.normals[0].y > 0.99f && mesh.indices.size() == (spline->path.points.size() - 1) * 6);
    // Solid: a ray down onto the middle of the road hits it, and one beside it does not.
    const Collider* collider = world.scene.get<Collider>(road);
    CHECK(collider && collider->mesh != nullptr);
    world.advance(1.0 / 60.0);
    Vec3 middle = spline->path.pointAt(spline->path.length() * 0.1f);
    auto hit = world.physics.raycast(world.scene, Ray{middle + Vec3{0, 5, 0}, Vec3{0, -1, 0}}, 10);
    CHECK(hit && std::abs(hit->point.y - 0.02f) < 0.01f);
    Vec3 side = simd::cross(spline->path.directionAt(spline->path.length() * 0.1f), Vec3{0, 1, 0});
    auto beside = world.physics.raycast(world.scene, Ray{middle + side * 3.5f + Vec3{0, 5, 0}, Vec3{0, -1, 0}}, 10);
    CHECK(!beside);
}
