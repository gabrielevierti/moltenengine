#include "Test.hpp"

#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"
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

namespace {
struct Shore : World {
    MeshData shown;
    void showMesh(Entity, const MeshData& data) override { shown = data; }
};
}  // namespace

TEST(water_is_a_moving_sheet_and_things_float_on_its_waves) {
    kit::registerKit();
    Shore world;
    Entity lake = world.scene.create("Sea");
    Transform at;
    at.position = Vec3{0, 2, 0};
    world.scene.add(lake, at);
    Water water;
    water.size = 200;
    water.resolution = 50;
    water.windSpeed = 12;
    world.scene.add(lake, water);
    Entity buoy = world.scene.create("Buoy");
    Transform start;
    start.position = Vec3{10, 6, -15};
    world.scene.add(buoy, start);
    world.scene.add(buoy, RigidBody{});
    world.scene.add(buoy, Collider::sphere(0.4f));
    kit::Buoyancy floats;
    floats.waterLevel = -100;                                           // ignored: there is a Water under it
    world.scene.add(buoy, floats);

    world.advance(1.0 / 60.0);
    CHECK(world.shown.positions.size() == 51 * 51 && world.shown.indices.size() == 50 * 50 * 6);
    CHECK(world.shown.uvs[0].x >= 2.0f);
    // Still at the rim, moving in the middle.
    CHECK(std::abs(world.shown.positions[0].y) < 1e-5f);
    float highest = 0;
    for (const Vec3& point : world.shown.positions) highest = std::max(highest, std::abs(point.y));
    CHECK(highest > 0.4f && highest < 6.0f);
    MeshData first = world.shown;
    float lowestBuoy = 100, highestBuoy = -100, worst = 0;
    for (int i = 0; i < 60 * 14; i++) {
        world.advance(1.0 / 60.0);
        if (i < 60 * 4) continue;                                       // once it has settled into the water
        float y = world.scene.get<RigidBody>(buoy)->position.y, surface = 0;
        CHECK(waterHeightAt(world, world.scene.get<RigidBody>(buoy)->position, surface));
        lowestBuoy = std::min(lowestBuoy, y);
        highestBuoy = std::max(highestBuoy, y);
        worst = std::max(worst, std::abs(y - surface));
    }
    CHECK(simd::length(world.shown.positions[51 * 25 + 25] - first.positions[51 * 25 + 25]) > 0.05f);
    CHECK(highestBuoy - lowestBuoy > 0.3f);                             // it rides up and down
    CHECK(worst < 1.3f);                                                // and stays with the surface
    float nowhere = 0;
    CHECK(!waterHeightAt(world, Vec3{500, 0, 0}, nowhere));
}

#include "Rendering/Particles.hpp"
#include "Water/Liquid.hpp"

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
