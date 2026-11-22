// Cloth: a sheet that hangs from its pins, falls, drapes over what it meets and goes with the wind.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Cloth.hpp"
#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"
#include "Scene/SceneFile.hpp"

#include <cmath>

using namespace molten;

namespace {

/// The plane stood up (turned a quarter about x), `width` across and `height` high, its middle at `at`.
Mat4 standing(Vec3 at, float width, float height) {
    return compose(at, axisAngle(Vec3{1, 0, 0}, kPi * 0.5f), Vec3{width, 1, height});
}

void run(kit::ClothSimulation& cloth, float seconds, const Mat4& place, Vec3 wind = Vec3{0, 0, 0},
         const std::vector<ShapeInstance>& obstacles = {}) {
    double time = 0;
    for (int i = 0; i < int(seconds * 60); i++) {
        time += 1.0 / 60.0;
        cloth.step(1.0f / 60.0f, place, Vec3{0, -9.81f, 0}, [&](Vec3) { return wind; }, obstacles, time);
    }
}

}  // namespace

TEST(cloth_pins_the_edge_that_is_asked) {
    MeshData sheet = MeshData::plane(1, 1, 10);
    // The plane lies in x and z: its top, once stood up, is its -z edge.
    std::vector<size_t> top = kit::pinnedPoints(sheet.positions, "top edge", 0.1f);
    CHECK(top.size() == 11);
    for (size_t i : top) CHECK(std::abs(sheet.positions[i].z + 0.5f) < 1e-5f);
    CHECK(kit::pinnedPoints(sheet.positions, "top corners", 0.1f).size() == 2);
    CHECK(kit::pinnedPoints(sheet.positions, "corners", 0.1f).size() == 4);
    std::vector<size_t> left = kit::pinnedPoints(sheet.positions, "left edge", 0.1f);
    CHECK(left.size() == 11);
    for (size_t i : left) CHECK(std::abs(sheet.positions[i].x + 0.5f) < 1e-5f);
    CHECK(kit::pinnedPoints(sheet.positions, "none", 0.1f).empty());
    // A model standing up (thinnest along z): its top is +y.
    std::vector<Vec3> upright = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 2, 0}, Vec3{1, 2, 0}, Vec3{0.5f, 1, 0.01f}};
    std::vector<size_t> held = kit::pinnedPoints(upright, "top edge", 0.5f);
    CHECK(held.size() == 2 && upright[held[0]].y == 2 && upright[held[1]].y == 2);
}

TEST(a_curtain_hangs_straight_from_its_top_without_stretching) {
    kit::ClothSimulation curtain;
    Mat4 place = standing(Vec3{0, 3, 0}, 2.0f, 1.5f);
    CHECK(curtain.build(MeshData::plane(1, 1, 20), place, "top edge", kit::ClothSettings{}));
    CHECK(curtain.pointCount() == 21 * 21 && curtain.pinned().size() == 21);
    run(curtain, 4.0f, place);
    AABB box = curtain.bounds();
    // From the rail at 3.75 m down a meter and a half, hardly longer, and still.
    CHECK(std::abs(box.max.y - 3.75f) < 0.01f);
    CHECK(box.min.y > 2.25f - 0.08f && box.min.y < 2.25f + 0.05f);
    CHECK(curtain.mostStretched() < 1.08f);
    float fastest = 0;
    for (Vec3 v : curtain.velocities()) fastest = std::max(fastest, simd::length(v));
    CHECK(fastest < 0.2f);
    // The pins stay where the entity puts them, and go where it goes.
    Mat4 moved = standing(Vec3{0.5f, 3, 0}, 2.0f, 1.5f);
    run(curtain, 1.0f, moved);
    for (size_t pin : curtain.pinned()) CHECK(std::abs(curtain.points()[pin].y - 3.75f) < 1e-3f);
    CHECK(curtain.bounds().max.x > 1.49f);
}

TEST(cloth_falls_and_drapes_over_a_ball) {
    kit::ClothSimulation sheet;
    Mat4 place = compose(Vec3{0, 2, 0}, Quat{}, Vec3{2, 1, 2});      // flat, two meters square, two meters up
    kit::ClothSettings settings;
    settings.thickness = 0.02f;
    CHECK(sheet.build(MeshData::plane(1, 1, 24), place, "none", settings));
    ShapeInstance ball;
    ball.shape = ColliderShape::Sphere;
    ball.center = Vec3{0, 0.5f, 0};
    ball.radius = 0.5f;
    run(sheet, 3.0f, place, Vec3{0, 0, 0}, {ball});
    // Nothing inside the ball; the middle on top of it, the corners hanging round it, lower.
    float deepest = 1e9f;
    for (Vec3 p : sheet.points()) deepest = std::min(deepest, simd::length(p - ball.center));
    CHECK(deepest > 0.5f - 0.01f);
    float middle = -1e9f, corner = 1e9f;
    for (Vec3 p : sheet.points()) {
        if (std::abs(p.x) < 0.05f && std::abs(p.z) < 0.05f) middle = std::max(middle, p.y);
    }
    corner = sheet.bounds().min.y;
    CHECK(std::abs(middle - 1.02f) < 0.04f);
    CHECK(corner < middle - 0.4f);
    CHECK(sheet.mostStretched() < 1.15f);
}

TEST(the_wind_lifts_a_flag_out_from_its_pole) {
    auto spread = [](Vec3 wind) {
        kit::ClothSimulation flag;
        // On its pole along y at x = 0, the cloth reaching along +x.
        Mat4 place = standing(Vec3{0.75f, 3, 0}, 1.5f, 1.0f);
        flag.build(MeshData::plane(1, 1, 16), place, "left edge", kit::ClothSettings{});
        run(flag, 4.0f, place, wind);
        return flag.bounds();
    };
    AABB still = spread(Vec3{0, 0, 0}), blown = spread(Vec3{8, 0, 0});
    // Still, it hangs down by the pole; in the wind it flies out from it.
    CHECK(still.max.x < 0.9f);
    CHECK(blown.max.x > 1.2f);
    CHECK(blown.min.y > still.min.y + 0.3f);
}

TEST(the_cloth_component_runs_on_its_entity_and_keeps_its_fields) {
    kit::registerKit();
    World world;
    Entity curtain = world.scene.create("Curtain");
    Transform place;
    place.position = Vec3{0, 3, 0};
    place.rotation = axisAngle(Vec3{1, 0, 0}, kPi * 0.5f);
    place.scale = Vec3{2, 1, 1.5f};
    world.scene.add(curtain, place);
    world.scene.add(curtain, MeshRenderer{});
    kit::Cloth cloth;
    cloth.segments = 12;
    cloth.weight = 0.4f;
    cloth.pinned = "top corners";
    world.scene.add(curtain, cloth);
    for (int i = 0; i < 60 * 2; i++) world.advance(1.0 / 60.0);
    const kit::Cloth* running = world.scene.get<kit::Cloth>(curtain);
    CHECK(running && running->simulation && running->simulation->pointCount() == 13 * 13);
    CHECK(running->simulation->pinned().size() == 2);
    // Held at two corners, it sags between them.
    CHECK(running->simulation->bounds().max.y < 3.75f + 1e-3f && running->simulation->bounds().min.y < 2.3f);
    // Kept with the scene.
    std::vector<NamedMaterial> none;
    std::string text = writeScene(world.scene, none, RenderSettings{});
    Scene back;
    RenderSettings settings;
    CHECK(readScene(text, back, none, settings, SceneAssets{}));
    bool found = false;
    back.each<kit::Cloth>([&](Entity, kit::Cloth& c) { found = c.segments == 12 && c.weight == 0.4f && c.pinned == "top corners"; });
    CHECK(found);
}

TEST(cloth_lies_on_ground_that_is_a_mesh_collider) {
    MeshData ground = MeshData::plane(20, 20, 4);
    std::shared_ptr<const MeshShape> shape = MeshShape::build(ground.positions, ground.indices, Vec3{1, 1, 1});
    ShapeInstance floor;
    floor.shape = ColliderShape::Mesh;
    floor.mesh = shape.get();
    kit::ClothSimulation sheet;
    Mat4 place = compose(Vec3{0, 1, 0}, Quat{}, Vec3{1, 1, 1});
    CHECK(sheet.build(MeshData::plane(1, 1, 10), place, "none", kit::ClothSettings{}));
    run(sheet, 2.0f, place, Vec3{0, 0, 0}, {floor});
    AABB box = sheet.bounds();
    CHECK(box.min.y > 0.0f && box.max.y < 0.06f);
}
