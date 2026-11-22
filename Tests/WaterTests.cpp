#include "Test.hpp"

#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/SceneFile.hpp"
#include "Water/Ocean.hpp"
#include "Water/Water.hpp"

using namespace molten;

TEST(the_fourier_transform_finds_a_wave_and_puts_it_back) {
    // One wave of three crests across sixteen points.
    std::complex<float> values[16], original[16];
    for (int i = 0; i < 16; i++) original[i] = values[i] = std::complex<float>(std::cos(2 * kPi * 3 * float(i) / 16), 0);
    fourierTransform(values, 16, false);
    for (int i = 0; i < 16; i++) CHECK_NEAR(std::abs(values[i]), (i == 3 || i == 13) ? 8.0 : 0.0, 1e-4);
    fourierTransform(values, 16, true);
    for (int i = 0; i < 16; i++) CHECK_NEAR(values[i].real() / 16, original[i].real(), 1e-5);
}

TEST(an_ocean_has_the_waves_its_wind_raises) {
    OceanSettings settings;
    settings.windSpeed = 10;
    Ocean sea(settings);
    int n = sea.resolution();
    CHECK(n == 64);
    auto measure = [&](const Ocean& ocean) {
        double sum = 0, squares = 0, steepest = 0;
        for (int j = 0; j < n; j++) {
            for (int i = 0; i < n; i++) {
                float h = ocean.displacement(i, j).y;
                sum += h;
                squares += double(h) * h;
                steepest = std::max(steepest, 1.0 - double(ocean.normal(i, j).y));
            }
        }
        double mean = sum / (n * n);
        return std::array<double, 3>{mean, std::sqrt(squares / (n * n) - mean * mean), steepest};
    };
    auto fresh = measure(sea);
    CHECK_NEAR(fresh[0], 0.0, 0.02);                                    // as much above the level as below
    // A fresh breeze: waves of about two meters, trough to crest.
    CHECK_NEAR(sea.significantHeight(), 2.14, 0.05);
    CHECK(fresh[1] * 4 > 1.2 && fresh[1] * 4 < 3.2);
    CHECK(fresh[2] > 0.01);                                             // and they slope
    // A gale raises much more; a light air hardly anything.
    settings.windSpeed = 20;
    CHECK(measure(Ocean(settings))[1] > fresh[1] * 2.5);
    settings.windSpeed = 3;
    CHECK(measure(Ocean(settings))[1] < fresh[1] * 0.25);

    // It moves, and comes round without a seam: the patch repeats.
    Vec3 before = sea.displacement(10, 20);
    sea.update(1.5f);
    CHECK(simd::length(sea.displacement(10, 20) - before) > 0.05f);
    float patch = sea.settings().patchSize;
    CHECK_NEAR(sea.displacementAt(3.3f, 7.1f).y, sea.displacementAt(3.3f + patch, 7.1f - patch).y, 1e-4);
    CHECK_NEAR(sea.displacementAt(-0.01f, 5).y, sea.displacementAt(0.01f, 5).y, 0.05);
    // The same seed is the same sea; another seed, another.
    settings.windSpeed = 10;
    Ocean again(settings);
    again.update(1.5f);
    CHECK_NEAR(again.displacement(10, 20).y, sea.displacement(10, 20).y, 1e-5);
    settings.seed = 8;
    CHECK(std::abs(Ocean(settings).displacement(10, 20).y - before.y) > 1e-4f);
    // Sharp crests fold over and foam; a calm sea has none.
    settings.windSpeed = 16;
    settings.choppiness = 1.6f;
    Ocean rough(settings);
    float most = 0;
    for (int step = 0; step < 20; step++) {
        rough.update(float(step) * 0.2f);
        for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) most = std::max(most, rough.foam(i, j));
    }
    CHECK(most > 0.3f);
    settings.windSpeed = 3;
    settings.choppiness = 1;
    Ocean calm(settings);
    float any = 0;
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) any = std::max(any, calm.foam(i, j));
    CHECK(any == 0);
}

// ---------------------------------------------------------------- water: a material, and the sea the wind raises

namespace {
/// A sheet of water in a world: a plane with a water material, `side` meters a side, its surface at a height.
Entity addSea(World& world, float side, float level, std::shared_ptr<Material>* look = nullptr) {
    Entity sea = world.scene.create("Sea");
    Transform at;
    at.position = Vec3{0, level, 0};
    at.scale = Vec3{side, 1, side};
    world.scene.add(sea, at);
    MeshRenderer plane;
    plane.meshName = "plane";
    plane.material = std::make_shared<Material>();
    plane.material->isWater = true;
    if (look) *look = plane.material;
    world.scene.add(sea, plane);
    world.scene.updateTransforms();
    return sea;
}
}  // namespace

TEST(the_sea_is_as_high_and_as_long_as_its_wind_and_its_material_say) {
    WaterSea sea;
    sea.wind = Vec3{8, 0, 0};
    // Four times the root of the mean square of its heights is its height: measured over a wide stretch and a long while.
    double sum = 0;
    int count = 0;
    float highest = 0;
    for (int t = 0; t < 40; t++) {
        for (int i = 0; i < 60; i++) {
            for (int j = 0; j < 60; j++) {
                float y = sea.moved(Vec2{float(i) * 3.7f, float(j) * 4.3f}, float(t) * 1.9f).y;
                sum += double(y) * y, count++;
                highest = std::max(highest, std::abs(y));
            }
        }
    }
    float measured = 4.0f * float(std::sqrt(sum / count));
    CHECK(std::abs(measured - sea.height()) < sea.height() * 0.12f);
    CHECK(highest < sea.height() * 1.6f);                               // no wave towers over the rest
    CHECK_NEAR(sea.height(), 0.0126 * 64, 1e-3);
    CHECK_NEAR(sea.longest(), 0.55 * 64, 1e-3);
    // More wind, a higher and longer sea; the material makes of it what it says.
    WaterSea gale = sea;
    gale.wind = Vec3{0, 0, -16};
    CHECK(gale.height() > sea.height() * 3 && gale.longest() > sea.longest() * 3);
    WaterSea tall = sea, wide = sea, flat = sea;
    tall.waveHeight = 2, wide.waveSize = 2, flat.waveHeight = 0;
    CHECK_NEAR(tall.height(), sea.height() * 2, 1e-4);
    CHECK_NEAR(tall.longest(), sea.longest(), 1e-4);
    CHECK_NEAR(wide.longest(), sea.longest() * 2, 1e-4);
    CHECK_NEAR(wide.height(), sea.height(), 1e-4);
    CHECK(simd::length(flat.moved(Vec2{13, 7}, 3.0f)) < 1e-6f);
    // No sea stands steeper than water can: asked for more, it is as high as its length allows.
    WaterSea steep = sea;
    steep.waveHeight = 50;
    CHECK_NEAR(steep.height(), steep.longest() * 0.074, 1e-3);
    // The air is never quite still: without wind the water still moves, a little.
    WaterSea calm;
    CHECK(calm.height() > 0.03f && calm.height() < 0.08f && calm.longest() < 3.0f);
    // The waves run the way the wind blows: the same water a moment later is the water that was upwind.
    float later = 0.5f, speed = std::sqrt(9.81f * sea.longest() / (2 * kPi));     // of the longest
    float followed = 0, against = 0;
    for (int i = 0; i < 300; i++) {
        Vec2 here{float(i % 17) * 5.3f, float(i / 17) * 6.1f};
        float was = sea.moved(here, 10).y;
        followed += std::abs(sea.moved(here + Vec2{speed * later, 0}, 10 + later).y - was);
        against += std::abs(sea.moved(here - Vec2{speed * later, 0}, 10 + later).y - was);
    }
    CHECK(followed < against * 0.6f);
}

TEST(the_height_of_water_over_a_place_is_that_of_the_water_brought_there) {
    WaterSea sea;
    sea.wind = Vec3{9, 0, 4};
    sea.waveHeight = 2.5f;                                              // sharp crests: the water is drawn well aside
    float worst = 0, farthest = 0;
    for (int i = 0; i < 400; i++) {
        Vec2 place{float(i % 20) * 3.1f - 30, float(i / 20) * 2.7f - 25};
        float time = float(i) * 0.37f;
        float height = sea.heightAt(place, time);
        // Found the long way: among the water resting all about the place, that which is over it now.
        float best = 1e9f, found = 0;
        for (int a = -40; a <= 40; a++) {
            for (int b = -40; b <= 40; b++) {
                Vec2 rest = place + Vec2{float(a), float(b)} * 0.08f;
                Vec3 moved = sea.moved(rest, time);
                float miss = simd::length(rest + Vec2{moved.x, moved.z} - place);
                if (miss < best) best = miss, found = moved.y;
            }
        }
        worst = std::max(worst, std::abs(height - found));
        farthest = std::max(farthest, simd::length(Vec2{sea.moved(place, time).x, sea.moved(place, time).z}));
    }
    CHECK(farthest > 0.3f);                                             // it does matter
    CHECK(worst < 0.06f);
}

TEST(the_water_sheet_is_rings_about_the_eye_that_all_face_up) {
    MeshData sheet = waterSheetMesh();
    CHECK(sheet.positions.size() > 20000 && sheet.positions.size() < 90000);
    CHECK(sheet.normals.size() == sheet.positions.size() && sheet.uvs.size() == sheet.positions.size() && sheet.tangents.size() == sheet.positions.size());
    float farthest = 0;
    for (const Vec3& point : sheet.positions) {
        CHECK(point.y == 0);
        farthest = std::max(farthest, simd::length(point));
    }
    CHECK(farthest >= 8000.0f && farthest < 9000.0f);
    // Every triangle faces up and has some size; each point says how far apart the points are there.
    int wrong = 0;
    float worstCell = 0;
    for (size_t i = 0; i + 2 < sheet.indices.size(); i += 3) {
        Vec3 a = sheet.positions[sheet.indices[i]], b = sheet.positions[sheet.indices[i + 1]], c = sheet.positions[sheet.indices[i + 2]];
        Vec3 normal = simd::cross(b - a, c - a);
        if (normal.y <= 1e-9f) wrong++;
        float longestEdge = std::max({simd::length(b - a), simd::length(c - b), simd::length(a - c)});
        float cell = std::max({sheet.uvs[sheet.indices[i]].x, sheet.uvs[sheet.indices[i + 1]].x, sheet.uvs[sheet.indices[i + 2]].x});
        worstCell = std::max(worstCell, longestEdge / cell);
    }
    CHECK(wrong == 0);
    CHECK(worstCell < 1.6f);                                            // (the diagonal of a square is 1.41 of its side)
    // Close together near the eye, far apart far off.
    CHECK(sheet.uvs[1 + 192].x < 0.02f && sheet.uvs.back().x > 100.0f);
    // No hole in it: the area of its triangles is the disc's.
    double area = 0;
    for (size_t i = 0; i + 2 < sheet.indices.size(); i += 3) {
        Vec3 a = sheet.positions[sheet.indices[i]], b = sheet.positions[sheet.indices[i + 1]], c = sheet.positions[sheet.indices[i + 2]];
        area += 0.5 * double(simd::cross(b - a, c - a).y);
    }
    CHECK(std::abs(area - double(kPi) * farthest * farthest) < 0.002 * double(kPi) * farthest * farthest);
}

TEST(things_float_on_the_seas_waves_and_a_calm_sea_is_a_mirror) {
    kit::registerKit();
    World world;
    world.sceneSettings().wind = Vec3{11, 0, 3};
    world.sceneSettings().sea.enabled = true;
    world.sceneSettings().sea.level = 2;
    Entity buoy = world.scene.create("Buoy");
    Transform start;
    start.position = Vec3{10, 6, -15};
    world.scene.add(buoy, start);
    world.scene.add(buoy, RigidBody{});
    world.scene.add(buoy, Collider::sphere(0.4f));
    kit::Buoyancy floats;
    floats.waterLevel = -100;                                           // ignored: there is water under it
    world.scene.add(buoy, floats);

    float lowestBuoy = 100, highestBuoy = -100, worst = 0, lowestSea = 100, highestSea = -100;
    for (int i = 0; i < 60 * 14; i++) {
        world.advance(1.0 / 60.0);
        if (i < 60 * 4) continue;                                       // once it has settled into the water
        float y = world.scene.get<RigidBody>(buoy)->position.y, surface = 0;
        CHECK(waterHeightAt(world, world.scene.get<RigidBody>(buoy)->position, surface));
        lowestBuoy = std::min(lowestBuoy, y), highestBuoy = std::max(highestBuoy, y);
        lowestSea = std::min(lowestSea, surface), highestSea = std::max(highestSea, surface);
        worst = std::max(worst, std::abs(y - surface));
    }
    CHECK(highestSea - lowestSea > 0.5f && lowestSea > 0.5f && highestSea < 3.5f);      // about its level, 2
    CHECK(highestBuoy - lowestBuoy > 0.3f);                             // it rides up and down
    CHECK(worst < 1.3f);                                                // and stays with the surface
    // With no wind, it is flat at its level; with no sea, there is no water.
    float height = 0;
    world.sceneSettings().wind = Vec3{0, 0, 0};
    CHECK(waterHeightAt(world, Vec3{10, 50, -15}, height) && std::abs(height - 2) < 0.05f);
    world.sceneSettings().sea.enabled = false;
    CHECK(!waterHeightAt(world, Vec3{10, 50, -15}, height));
}

TEST(a_scene_whose_water_was_a_component_or_a_material_reads_with_its_sea_there) {
    kit::registerKit();
    Scene old;
    Entity sea = old.create("Sea");
    Transform at;
    at.position = Vec3{0, -1.5f, 0};
    old.add(sea, at);
    Water said;
    said.size = 1600;
    said.windSpeed = 6.5f;
    said.windDirection = Vec2{0, -2};
    old.add(sea, said);
    RenderSettings settings;
    std::string text = writeScene(old, {}, settings);
    CHECK(text.find("Water") != std::string::npos);

    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings read;
    CHECK(readScene(text, scene, materials, read, SceneAssets{}));
    Entity found;
    scene.each<Name, Transform>([&](Entity entity, Name& name, Transform&) {
        if (name.value == "Sea") found = entity;
    });
    CHECK(scene.isAlive(found) && !scene.has<Water>(found));
    CHECK(read.sea.enabled && std::abs(read.sea.level + 1.5f) < 1e-4f);     // the sea is where it was
    CHECK(std::abs(read.wind.z + 6.5f) < 1e-4f && std::abs(read.wind.x) < 1e-4f);
    // A scene that has a wind keeps it.
    RenderSettings windy;
    windy.wind = Vec3{3, 0, 0};
    Scene again;
    CHECK(readScene(writeScene(old, {}, windy), again, materials, windy, SceneAssets{}) && windy.wind.x == 3 && windy.wind.z == 0);

    // Water that was a material: a plane as large as a sea becomes the sea, in its colors, and is
    // hidden; a pool stays, a plane seen through.
    const std::string before =
        "molten-scene 1\n"
        "material \"Ocean\"\n  baseColor 0.05 0.2 0.3 0.4\n  waves 1 1 1\n"
        "material \"Pool\"\n  baseColor 0.2 0.5 0.7 0.6\n  waves 1 1 1\n"
        "entity \"Ocean\"\n  transform 0 0.25 0  0 0 0 1  2000 1 2000\n  mesh \"plane\" 0\n"
        "entity \"Pool\"\n  transform 0 3 0  0 0 0 1  8 1 5\n  mesh \"plane\" 1\n";
    Scene lagoon;
    std::vector<NamedMaterial> looks;
    RenderSettings lagoonSettings;
    CHECK(readScene(before, lagoon, looks, lagoonSettings, SceneAssets{}));
    CHECK(lagoonSettings.sea.enabled && std::abs(lagoonSettings.sea.level - 0.25f) < 0.01f);
    CHECK_NEAR(lagoonSettings.sea.color.z, 0.3, 1e-5);
    int hidden = 0, shown = 0;
    lagoon.each<Name, MeshRenderer>([&](Entity, Name& name, MeshRenderer& renderer) {
        if (name.value == "Ocean") hidden += !renderer.isVisible;
        if (name.value == "Pool") shown += renderer.isVisible && renderer.material && renderer.material->alphaMode == AlphaMode::Blend && !renderer.material->isWater;
    });
    CHECK(hidden == 1 && shown == 1);
}

#include "Rendering/Particles.hpp"
#include "Water/Liquid.hpp"

TEST(the_scenes_sea_is_water_everywhere_below_its_level_and_things_float_on_it) {
    kit::registerKit();
    World world;
    world.sceneSettings().wind = Vec3{8, 0, 2};
    SeaSettings& sea = world.sceneSettings().sea;
    float height = 0;
    CHECK(!waterHeightAt(world, Vec3{0, 0, 0}, height));               // none unless it is on
    sea.enabled = true;
    sea.level = 3;
    // Everywhere, near and far, about its level: the wind's waves on it.
    float lowest = 100, highest = -100;
    for (float x : {0.0f, 37.0f, 5000.0f, -80000.0f}) {
        for (float t = 0; t < 8; t += 0.5f) {
            world.effectsTime = t;
            CHECK(waterHeightAt(world, Vec3{x, -50, x * 0.3f}, height));
            lowest = std::min(lowest, height), highest = std::max(highest, height);
        }
    }
    CHECK(lowest > 1.5f && highest < 4.5f && highest - lowest > 0.3f);

    // A buoy dropped in it rides on its waves.
    World open;
    open.sceneSettings().wind = Vec3{8, 0, 2};
    open.sceneSettings().sea.enabled = true;
    open.sceneSettings().sea.level = -2;
    Entity buoy = open.scene.create("Buoy");
    Transform start;
    start.position = Vec3{4, 3, 4};
    open.scene.add(buoy, start);
    open.scene.add(buoy, RigidBody{});
    open.scene.add(buoy, Collider::sphere(0.4f));
    open.scene.add(buoy, kit::Buoyancy{});
    for (int i = 0; i < 60 * 8; i++) open.advance(1.0 / 60.0);
    CHECK(std::abs(open.scene.get<RigidBody>(buoy)->position.y + 2) < 1.2f);
    CHECK(open.scene.get<kit::Buoyancy>(buoy)->isInWater);
}

TEST(the_sea_is_kept_with_the_scene_and_a_scene_of_before_has_none) {
    RenderSettings settings;
    CHECK(!settings.sea.enabled);                                       // a scene made in code: none
    settings.sea.enabled = true;
    settings.sea.level = -1.25f;
    settings.sea.color = Vec3{0.05f, 0.2f, 0.25f};
    settings.sea.shallowColor = Vec3{0.3f, 0.8f, 0.7f};
    settings.sea.clarity = 20.0f;
    settings.sea.scattering = 1.5f;
    settings.sea.reflections = 0.6f;
    settings.sea.ripples = 0.4f;
    settings.sea.caustics = 2.0f;
    settings.sea.foam = 0.5f;
    settings.sea.whitecaps = 0.0f;
    Scene empty, read;
    std::vector<NamedMaterial> none;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none, settings), read, none, back, SceneAssets{}));
    CHECK(back.sea.enabled && back.sea.level == -1.25f && back.sea.clarity == 20.0f && back.sea.foam == 0.5f);
    CHECK(back.sea.scattering == 1.5f && back.sea.reflections == 0.6f && back.sea.ripples == 0.4f);
    CHECK(back.sea.caustics == 2.0f && back.sea.whitecaps == 0.0f);
    CHECK_NEAR(back.sea.color.y, 0.2, 1e-6);
    CHECK_NEAR(back.sea.shallowColor.y, 0.8, 1e-6);
    // A sea line of before (its murk where clarity is now) still reads, clear or murky as it was.
    CHECK(readScene("molten-scene 1\nsettings\nsea 1 -2 0.05 0.2 0.25 0.3 0.5\n", read, none, back, SceneAssets{}));
    CHECK(back.sea.enabled && back.sea.level == -2.0f && back.sea.clarity > 5.0f && back.sea.foam == 0.5f);
    // Read over settings that had one, a scene without it has none.
    RenderSettings dry;
    CHECK(readScene(writeScene(empty, none, dry), read, none, back, SceneAssets{}) && !back.sea.enabled);
    // Its water takes red first, then green, and blue least: the deeper, the bluer.
    Vec3 taken = settings.sea.absorption();
    CHECK(taken.x > taken.y && taken.y > taken.z && taken.z > 0.0f);
    settings.sea.clarity = 2.0f;
    CHECK(settings.sea.absorption().z > taken.z * 5.0f);              // murky water hides every color
}

TEST(liquid_fills_what_holds_it) {
    kit::registerKit();
    World world;
    auto wall = [&](const char* name, Vec3 position, Vec3 half) {
        Entity entity = world.scene.create(name);
        Transform transform;
        transform.position = position;
        world.scene.add(entity, transform);
        world.scene.add(entity, Collider::box(half));
    };
    // A tank a meter square, open above.
    wall("Floor", Vec3{0, -0.5f, 0}, Vec3{3, 0.5f, 3});
    wall("Left", Vec3{-0.6f, 1, 0}, Vec3{0.1f, 1, 0.7f});
    wall("Right", Vec3{0.6f, 1, 0}, Vec3{0.1f, 1, 0.7f});
    wall("Front", Vec3{0, 1, -0.6f}, Vec3{0.7f, 1, 0.1f});
    wall("Back", Vec3{0, 1, 0.6f}, Vec3{0.7f, 1, 0.1f});
    Entity entity = world.scene.create("Water");
    Transform transform;
    transform.position = Vec3{-0.2f, 0.7f, 0};                          // a column of it, to one side
    world.scene.add(entity, transform);
    Liquid liquid;
    liquid.fill = Vec3{0.4f, 0.8f, 0.6f};
    liquid.dropRadius = 0.04f;
    world.scene.add(entity, liquid);
    world.scene.add(entity, ParticleEmitter{});
    world.advance(1.0 / 60.0);
    const Liquid* drops = world.scene.get<Liquid>(entity);
    size_t count = drops->dropCount();
    CHECK(count == 5 * 10 * 7);
    CHECK(world.scene.get<ParticleEmitter>(entity)->particles.size() == count);         // drawn by the emitter
    for (int i = 0; i < 60 * 5; i++) world.advance(1.0 / 60.0);

    // It is all still in the tank, it has spread across it, and it has come to rest.
    float highest = -100, lowest = 100, left = 100, right = -100, fastest = 0;
    double height = 0;
    for (size_t i = 0; i < count; i++) {
        Vec3 p = drops->positions[i];
        highest = std::max(highest, p.y);
        lowest = std::min(lowest, p.y);
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        height += p.y;
        fastest = std::max(fastest, simd::length(drops->velocities[i]));
        CHECK(std::abs(p.x) < 0.52f && std::abs(p.z) < 0.52f);
    }
    CHECK(lowest > 0.0f && lowest < 0.09f);                             // resting on the floor, not in it
    CHECK(left < -0.38f && right > 0.38f);                              // from wall to wall
    // 350 drops of 8 cm in a square meter: about a fifth of a meter deep, not the 0.8 it started as.
    CHECK(highest < 0.45f && highest > 0.1f);
    CHECK(height / double(count) < 0.2);
    CHECK(fastest < 0.6f);
    // Level: the top of the left half is as high as the top of the right half.
    float topLeft = 0, topRight = 0;
    for (size_t i = 0; i < count; i++) {
        Vec3 p = drops->positions[i];
        if (p.x < 0) topLeft = std::max(topLeft, p.y);
        else topRight = std::max(topRight, p.y);
    }
    CHECK_NEAR(topLeft, topRight, 0.09);
    // And it does not squeeze: no two drops are much closer than a liquid lets them be.
    float closest = 100;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) closest = std::min(closest, simd::length(drops->positions[i] - drops->positions[j]));
    }
    CHECK(closest > 0.03f);
}

TEST(liquid_pours) {
    kit::registerKit();
    World world;
    Entity floor = world.scene.create("Floor");
    Transform under;
    under.position = Vec3{0, -0.5f, 0};
    world.scene.add(floor, under);
    world.scene.add(floor, Collider::box(Vec3{5, 0.5f, 5}));
    Entity tap = world.scene.create("Tap");
    Transform transform;
    transform.position = Vec3{0, 1, 0};
    world.scene.add(tap, transform);
    Liquid liquid;
    liquid.fill = Vec3{0, 0, 0};
    liquid.pourRate = 120;
    liquid.maxDrops = 200;
    world.scene.add(tap, liquid);
    for (int i = 0; i < 60; i++) world.advance(1.0 / 60.0);
    const Liquid* drops = world.scene.get<Liquid>(tap);
    CHECK(drops->dropCount() > 100 && drops->dropCount() <= 122);
    for (int i = 0; i < 180; i++) world.advance(1.0 / 60.0);
    CHECK(drops->dropCount() == 200);                                   // no more than it is allowed
    float lowest = 100, widest = 0;
    for (Vec3 p : drops->positions) {
        lowest = std::min(lowest, p.y);
        widest = std::max(widest, std::sqrt(p.x * p.x + p.z * p.z));
    }
    CHECK(lowest > 0.0f && lowest < 0.1f);                              // a puddle on the floor
    CHECK(widest > 0.25f);                                              // that has spread
}
