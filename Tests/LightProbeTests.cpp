// Baked light probes (Rendering/LightProbes.hpp): what they see of the sky, what is bounced to
// them, and the file they are kept in.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/LightProbeVolume.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/LightProbes.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <cmath>

using namespace molten;

namespace {

/// A rectangle from four corners, wound so that it faces the side `facing` points to.
void quad(BakeScene& scene, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 facing, Vec3 albedo = {0.5f, 0.5f, 0.5f}) {
    Vec3 n = simd::cross(b - a, c - a);
    if (simd::dot(n, facing) < 0) std::swap(b, d);
    BakeTriangle t;
    t.albedo = albedo;
    t.a = a, t.b = b, t.c = c;
    scene.add(t);
    t.a = a, t.b = c, t.c = d;
    scene.add(t);
}

/// A box of six walls facing in (a room) or out (a block).
void box(BakeScene& scene, Vec3 low, Vec3 high, bool facesIn, bool withRoof = true) {
    float s = facesIn ? 1.0f : -1.0f;
    Vec3 l = low, h = high;
    quad(scene, Vec3{l.x, l.y, l.z}, Vec3{h.x, l.y, l.z}, Vec3{h.x, l.y, h.z}, Vec3{l.x, l.y, h.z}, Vec3{0, s, 0});        // floor
    if (withRoof) quad(scene, Vec3{l.x, h.y, l.z}, Vec3{h.x, h.y, l.z}, Vec3{h.x, h.y, h.z}, Vec3{l.x, h.y, h.z}, Vec3{0, -s, 0});
    quad(scene, Vec3{l.x, l.y, l.z}, Vec3{l.x, h.y, l.z}, Vec3{l.x, h.y, h.z}, Vec3{l.x, l.y, h.z}, Vec3{s, 0, 0});
    quad(scene, Vec3{h.x, l.y, l.z}, Vec3{h.x, h.y, l.z}, Vec3{h.x, h.y, h.z}, Vec3{h.x, l.y, h.z}, Vec3{-s, 0, 0});
    quad(scene, Vec3{l.x, l.y, l.z}, Vec3{h.x, l.y, l.z}, Vec3{h.x, h.y, l.z}, Vec3{l.x, h.y, l.z}, Vec3{0, 0, s});
    quad(scene, Vec3{l.x, l.y, h.z}, Vec3{h.x, l.y, h.z}, Vec3{h.x, h.y, h.z}, Vec3{l.x, h.y, h.z}, Vec3{0, 0, -s});
}

BakeSettings quick() {
    BakeSettings settings;
    settings.raysPerProbe = 512;
    settings.threads = 2;
    return settings;
}

}  // namespace

TEST(a_probe_over_open_ground_sees_the_sky_above_and_the_lit_ground_below) {
    BakeScene scene;
    quad(scene, Vec3{-100, 0, -100}, Vec3{100, 0, -100}, Vec3{100, 0, 100}, Vec3{-100, 0, 100}, Vec3{0, 1, 0}, Vec3{0.8f, 0.2f, 0.2f});
    scene.build();
    BakeLight sun;
    sun.direction = Vec3{0, -1, 0};
    sun.radiance = Vec3{3, 3, 3};
    ProbeVolumeDesc volume{AABB(Vec3{-2, 1, -2}, Vec3{2, 3, 2}), 2.0f, 1.0f};
    BakeSettings settings = quick();
    settings.skyLight = Vec3{0, 0, 0};      // only the sun's light, to know what to expect
    BakedLighting baked = bakeLighting(scene, {sun}, {volume}, settings);
    CHECK(baked.grids.size() == 1);
    Vec4 up = baked.lightAt(Vec3{0, 2, 0}, Vec3{0, 1, 0});
    Vec4 down = baked.lightAt(Vec3{0, 2, 0}, Vec3{0, -1, 0});
    CHECK(up.w > 0.95f);                    // all sky
    CHECK(down.w < 0.05f);                  // all ground
    // The ground in full sun gives back its color: 0.8 of 3 / pi in red, a quarter of it in green.
    CHECK(std::fabs(down.x - 0.8f * 3.0f / 3.14159f) < 0.08f);
    CHECK(down.x > down.y * 3.0f);
    CHECK(up.x < 0.01f);                    // nothing bounced down from the open sky
}

TEST(a_closed_room_sees_no_sky_and_a_skylight_lets_some_in) {
    BakeScene closed;
    box(closed, Vec3{0, 0, 0}, Vec3{8, 4, 8}, true);
    closed.build();
    ProbeVolumeDesc volume{AABB(Vec3{1, 1, 1}, Vec3{7, 3, 7}), 2.0f, 1.0f};
    BakedLighting dark = bakeLighting(closed, {}, {volume}, quick());
    Vec4 middleUp = dark.lightAt(Vec3{4, 2, 4}, Vec3{0, 1, 0});
    CHECK(middleUp.w < 0.01f);

    BakeScene open;
    box(open, Vec3{0, 0, 0}, Vec3{8, 4, 8}, true, false);       // no roof
    open.build();
    BakedLighting light = bakeLighting(open, {}, {volume}, quick());
    Vec4 up = light.lightAt(Vec3{4, 2, 4}, Vec3{0, 1, 0});
    Vec4 side = light.lightAt(Vec3{4, 2, 4}, Vec3{1, 0, 0});
    CHECK(up.w > 0.5f);
    CHECK(side.w < up.w);
    // Outside the volume, the sky's light as it is.
    Vec4 outside = light.lightAt(Vec3{40, 2, 4}, Vec3{0, 1, 0});
    CHECK(outside.w == 1.0f && outside.x == 0.0f);
}

TEST(probes_inside_a_wall_take_their_neighbours_light) {
    BakeScene scene;
    quad(scene, Vec3{-50, 0, -50}, Vec3{50, 0, -50}, Vec3{50, 0, 50}, Vec3{-50, 0, 50}, Vec3{0, 1, 0});
    box(scene, Vec3{-1, 0, -1.5f}, Vec3{1, 3, 1.5f}, false);       // a pillar, with probes in it
    scene.build();
    ProbeVolumeDesc volume{AABB(Vec3{-4, 0.5f, 0}, Vec3{4, 2.5f, 0}), 2.0f, 1.0f};   // probes at -4, -2, 0, 2, 4 along x
    BakedLighting baked = bakeLighting(scene, {}, {volume}, quick());
    const ProbeGrid& grid = baked.grids[0];
    size_t inside = grid.indexOf(2, 0, 0);
    CHECK(grid.placeOf(2, 0, 0).x == 0.0f);
    // Left alone it would see only the pillar's inside: no sky at all. Filled in, it has the sky its neighbours see.
    CHECK(grid.cells[inside * 6 + 2].w > 0.3f);
}

TEST(baked_lighting_reads_back_what_it_wrote) {
    BakedLighting baked;
    ProbeGrid grid = gridFor(AABB(Vec3{0, 0, 0}, Vec3{4, 2, 6}), 2.0f);
    CHECK(grid.countX == 3 && grid.countY == 2 && grid.countZ == 4);
    grid.cells.resize(grid.probeCount() * 6);
    for (size_t i = 0; i < grid.cells.size(); i++) grid.cells[i] = Vec4{float(i) * 0.01f, 0.5f, 0.25f, float(i % 7) / 7.0f};
    baked.grids.push_back(grid);
    std::optional<BakedLighting> back = BakedLighting::fromText(baked.toText());
    CHECK(back.has_value());
    CHECK(back && back->grids.size() == 1 && back->grids[0].cells.size() == grid.cells.size());
    CHECK(back && std::fabs(back->grids[0].cells[17].x - grid.cells[17].x) < 1e-3f);
    CHECK(back && std::fabs(back->grids[0].origin.z - grid.origin.z) < 1e-5f);
    std::string why;
    CHECK(!BakedLighting::fromText("molten-lighting 1\ngrid 2 2 2 0 0 0 1 1\np 1 1 1 1\n", &why) && !why.empty());
    CHECK(!BakedLighting::fromText("molten-lighting 2\n", &why) && why.find("newer") != std::string::npos);
}

TEST(a_scene_is_read_for_baking_and_the_probes_go_where_its_volume_is) {
    World world;
    Entity floor = world.scene.create("Floor");
    Transform wide;
    wide.scale = Vec3{20, 1, 20};
    world.scene.add(floor, wide);
    MeshRenderer drawn;
    drawn.meshName = "plane";
    drawn.material = std::make_shared<Material>();
    drawn.material->baseColor = Vec4{0.9f, 0.9f, 0.9f, 1};
    world.scene.add(floor, drawn);
    Entity ball = world.scene.create("Ball");           // it moves: left out
    world.scene.add(ball, Transform{});
    MeshRenderer ballDrawn;
    ballDrawn.meshName = "sphere";
    world.scene.add(ball, ballDrawn);
    RigidBody body;
    body.type = BodyType::Dynamic;
    world.scene.add(ball, body);
    Entity sun = world.scene.create("Sun");
    Transform aimed;
    aimed.rotation = axisAngle(Vec3{1, 0, 0}, -kPi * 0.5f);    // shining down
    world.scene.add(sun, aimed);
    Light light;
    light.type = LightType::Directional;
    light.intensity = 2;
    world.scene.add(sun, light);
    Entity probes = world.scene.create("Probes");
    Transform there;
    there.position = Vec3{0, 2, 0};
    world.scene.add(probes, there);
    kit::LightProbeVolume volume;
    volume.size = Vec3{4, 2, 4};
    world.scene.add(probes, volume);

    kit::LightingBakeJob job = kit::prepareLightingBake(world, RenderSettings{}, 128);
    CHECK(job.volumes.size() == 1);
    CHECK(job.scene.triangleCount() == MeshData::plane().indices.size() / 3);
    CHECK(job.lights.size() == 1 && job.lights[0].direction.y < -0.99f);
    CHECK(std::fabs(job.volumes[0].box.min.y - 1.0f) < 1e-4f && std::fabs(job.volumes[0].box.max.x - 2.0f) < 1e-4f);
    job.settings.threads = 2;
    BakedLighting baked = bakeLighting(job.scene, job.lights, job.volumes, job.settings);
    Vec4 down = baked.lightAt(Vec3{0, 2, 0}, Vec3{0, -1, 0});
    CHECK(down.w < 0.5f && down.x > 0.3f);              // the lit floor, more than half of what is seen downward
    CHECK(kit::lightingFileFor("Scenes/Main.scene") == "Scenes/Main.lighting");
    CHECK(kit::lightingFileFor("Levels/v1.2/Hall") == "Levels/v1.2/Hall.lighting");
}
