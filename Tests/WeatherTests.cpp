#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Weather.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"
#include "Scene/SceneFile.hpp"

using namespace molten;

namespace {

// A world with a camera beside a wide floor (a box collider), over the sea if it has one, raining.
struct RainyWorld {
    World world;
    Entity floor;
    RainyWorld(float rain, bool withSea) {
        world.sceneSettings().weather.rain = rain;
        Entity camera = world.scene.create("Camera");
        Transform at;
        at.position = Vec3{0, 2, 0};
        world.scene.add(camera, at);
        world.scene.add(camera, Camera{});
        floor = world.scene.create("Floor");
        Transform ground;
        ground.position = Vec3{withSea ? 40.0f : 0.0f, -0.5f, 0};
        world.scene.add(floor, ground);
        Collider box;
        box.halfExtents = Vec3{30, 0.5f, 30};
        world.scene.add(floor, box);
        if (withSea) {
            // The scene's sea, a little under the floor's top: what falls beside the floor falls in it.
            world.sceneSettings().sea.enabled = true;
            world.sceneSettings().sea.level = -0.2f;
        }
        world.scene.updateTransforms();
    }
    void run(float seconds) {
        for (float t = 0; t < seconds; t += 1.0f / 60.0f) world.simulateFrame(1.0 / 60.0);
    }
    const ParticleEmitter* emitter(Entity entity) { return entity.isValid() ? world.scene.get<ParticleEmitter>(entity) : nullptr; }
};

}  // namespace

TEST(rain_falls_round_the_camera_and_splashes_on_what_it_hits) {
    RainyWorld rainy(1.0f, false);
    rainy.run(4.0f);
    const World::WeatherNow& now = rainy.world.weatherNow;
    const ParticleEmitter* drops = rainy.emitter(now.rain);
    const ParticleEmitter* splashes = rainy.emitter(now.splashes);
    CHECK(drops && splashes);
    if (!drops || !splashes) return;
    // Thousands of drops in the air, round the camera, falling.
    CHECK(drops->particles.size() > 3000);
    int near = 0;
    for (const Particle& drop : drops->particles) {
        near += std::abs(drop.position.x) < 40 && std::abs(drop.position.z) < 40;
        CHECK(drop.velocity.y < -8.0f);
    }
    CHECK(near > int(drops->particles.size()) * 9 / 10);
    // Each lands on the floor's top, and splashes there.
    CHECK(splashes->particles.size() > 50);
    float lowest = 100, highest = -100;
    for (const Particle& droplet : splashes->particles) {
        lowest = std::min(lowest, droplet.position.y);
        highest = std::max(highest, droplet.position.y);
    }
    CHECK(lowest > -0.3f && highest < 1.0f);
    // None of it is the scene's: no file holds it.
    CHECK(rainy.world.scene.has<EditorOnly>(now.rain) && rainy.world.scene.has<EditorOnly>(now.splashes));
}

TEST(rain_on_water_leaves_rings_and_a_dry_sky_makes_nothing) {
    RainyWorld rainy(0.8f, true);
    rainy.run(4.0f);
    const ParticleEmitter* rings = rainy.emitter(rainy.world.weatherNow.ripples);
    CHECK(rings && rings->particles.size() > 30 && rings->isRing && rings->renderMode == ParticleEmitter::RenderMode::Horizontal);
    if (rings) {
        // On the sea, about its level (the rain's breeze ruffles it), beside the floor and not on it.
        for (const Particle& ring : rings->particles) {
            CHECK_NEAR(ring.position.y, -0.2, 0.5);
            CHECK(ring.position.x < 10.5f);
        }
    }
    // It stops: what is falling lands, and no more is born.
    rainy.world.sceneSettings().weather.rain = 0;
    rainy.run(5.0f);
    const ParticleEmitter* drops = rainy.emitter(rainy.world.weatherNow.rain);
    CHECK(drops && drops->particles.empty());

    RainyWorld dry(0.0f, false);
    dry.run(1.0f);
    CHECK(!dry.world.weatherNow.rain.isValid());
}

TEST(a_storm_flashes_now_and_then_and_the_weather_is_kept_with_the_scene) {
    RainyWorld stormy(0.0f, false);
    stormy.world.sceneSettings().weather.storm = 1.0f;
    int flashes = 0;
    float brightest = 0, before = 0;
    for (int frame = 0; frame < 60 * 40; frame++) {
        stormy.world.simulateFrame(1.0 / 60.0);
        float flash = stormy.world.weatherNow.flash;
        if (flash > 0.4f && before <= 0.4f) flashes++;
        brightest = std::max(brightest, flash);
        before = flash;
    }
    CHECK(flashes >= 4 && flashes <= 60);           // a full storm: every few seconds, a few flashes a stroke
    CHECK(brightest > 0.5f && brightest <= 1.0f);
    // Clouded over as it rains; written with the scene and read back.
    RenderSettings settings;
    settings.weather.rain = 0.6f;
    settings.weather.storm = 0.3f;
    CHECK_NEAR(settings.weather.clouded(), 0.6, 1e-6);
    Scene empty, read;
    std::vector<NamedMaterial> none;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none, settings), read, none, back, SceneAssets{}));
    CHECK_NEAR(back.weather.rain, 0.6, 1e-5);
    CHECK_NEAR(back.weather.storm, 0.3, 1e-5);
    CHECK(back.weather.flash == 0.0f);
}

TEST(rain_driven_onto_a_wall_splashes_off_it) {
    RainyWorld rainy(1.0f, false);
    rainy.world.sceneSettings().wind = Vec3{8, 0, 0};           // blowing toward +x, onto the wall's face
    Entity wall = rainy.world.scene.create("Wall");
    Transform at;
    at.position = Vec3{6, 5, 0};
    rainy.world.scene.add(wall, at);
    Collider box;
    box.halfExtents = Vec3{0.5f, 6, 30};
    rainy.world.scene.add(wall, box);
    rainy.world.scene.updateTransforms();
    rainy.run(4.0f);
    const World::WeatherNow& now = rainy.world.weatherNow;
    const ParticleEmitter* splashes = rainy.emitter(now.splashes);
    const ParticleEmitter* runs = rainy.emitter(now.runs);
    CHECK(splashes && runs);
    if (!splashes || !runs) return;
    // Thrown off the wall's face (x = 5.5), back the way the rain came, not up as off a floor.
    int offTheWall = 0;
    for (const Particle& droplet : splashes->particles) {
        if (droplet.position.y > 1.0f && droplet.position.y < 10.0f && std::abs(droplet.position.x - 5.5f) < 0.4f) {      // (not its top, which is a floor)
            offTheWall++;
            CHECK(droplet.velocity.x < 0.3f);
        }
    }
    CHECK(offTheWall > 5);
    // What runs down it is the wall's own (drawn with it, Forward.metal): no particles left in the world.
    CHECK(runs->particles.empty());
}

TEST(snow_drifts_down_slowly_and_lies_a_moment_where_it_lands) {
    RainyWorld snowy(0.0f, false);
    snowy.world.sceneSettings().weather.snow = 1.0f;
    snowy.run(12.0f);
    const World::WeatherNow& now = snowy.world.weatherNow;
    const ParticleEmitter* flakes = snowy.emitter(now.snow);
    const ParticleEmitter* settled = snowy.emitter(now.settled);
    CHECK(flakes && settled && !now.rain.isValid());
    if (!flakes || !settled) return;
    CHECK(flakes->particles.size() > 2000 && int(flakes->particles.size()) < flakes->maxParticles);
    float fastest = 0;
    for (const Particle& flake : flakes->particles) fastest = std::max(fastest, -flake.velocity.y);
    CHECK(fastest < 2.5f);
    // Landed on the floor's top, and lying there.
    CHECK(settled->particles.size() > 50);
    for (const Particle& rest : settled->particles) {
        CHECK(std::abs(rest.position.y) < 0.1f);
        CHECK(simd::length(rest.velocity) == 0.0f);
    }
    // The sky clouds over; kept with the scene, and a file of before snow reads as none.
    RenderSettings settings;
    settings.weather.snow = 0.7f;
    CHECK_NEAR(settings.weather.clouded(), 0.56, 1e-5);
    Scene empty, read;
    std::vector<NamedMaterial> none;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none, settings), read, none, back, SceneAssets{}));
    CHECK_NEAR(back.weather.snow, 0.7, 1e-5);
    CHECK_NEAR(back.weather.rain, 0.0, 1e-6);
    // It snows harder in a stronger wind, and the wind rises with the weather.
    CHECK(settings.weather.addedWind() > 0.0f);
}

TEST(lightning_strikes_a_few_kilometers_round_some_bolts_to_the_ground) {
    RainyWorld stormy(0.0f, false);
    stormy.world.sceneSettings().weather.storm = 1.0f;
    double last = stormy.world.weatherNow.strikeAt;
    int strokes = 0, bolts = 0, inClouds = 0;
    for (int frame = 0; frame < 60 * 60; frame++) {
        stormy.world.simulateFrame(1.0 / 60.0);
        const World::WeatherNow& now = stormy.world.weatherNow;
        if (now.strikeAt == last) continue;
        last = now.strikeAt;
        strokes++;
        float away = std::hypot(now.strikePlace.x, now.strikePlace.z);
        CHECK(away > 400.0f && away < 5000.0f);
        (now.bolt > 0 ? bolts : inClouds)++;
    }
    CHECK(strokes >= 5);
    CHECK(bolts > 0 && inClouds > 0);
    // A storm makes a gale of the wind.
    WeatherSettings storm;
    storm.storm = 1;
    CHECK(storm.addedWind() >= 10.0f && storm.leastGusts() > 0.8f);
}

TEST(rain_and_snow_go_with_the_wind) {
    // The air carries a falling drop: it goes sideways as fast as the wind, so the rain slants along it.
    RainyWorld rainy(1.0f, false);
    rainy.world.sceneSettings().wind = Vec3{0, 0, 7};               // toward +z
    rainy.world.sceneSettings().windGustiness = 0;
    rainy.run(1.0f);
    const ParticleEmitter* drops = rainy.emitter(rainy.world.weatherNow.rain);
    CHECK(drops && drops->particles.size() > 500);
    if (!drops) return;
    double along = 0, across = 0;
    for (const Particle& drop : drops->particles) along += drop.velocity.z, across += std::abs(drop.velocity.x);
    along /= double(drops->particles.size()), across /= double(drops->particles.size());
    float wind = simd::length(rainy.world.wind());                  // the scene's, with what the rain adds to it
    CHECK(std::abs(along - wind) < 0.05 * wind && across < 0.05);
}

TEST(snow_that_settles_builds_up_while_it_snows_and_melts_after) {
    RainyWorld snowy(0.0f, false);
    WeatherSettings& weather = snowy.world.sceneSettings().weather;
    weather.snow = 1.0f;
    snowy.run(20.0f);
    CHECK(snowy.world.weatherNow.snowCover == 0.0f);                 // not asked to settle: it melts as it lands
    weather.snowSettles = true;
    snowy.run(45.0f);
    float half = snowy.world.weatherNow.snowCover;
    CHECK(half > 0.4f && half < 0.6f);                              // half a cover in 45 seconds of a heavy fall
    snowy.run(60.0f);
    CHECK(snowy.world.weatherNow.snowCover > 0.99f);
    weather.snow = 0.0f;
    snowy.run(30.0f);
    float after = snowy.world.weatherNow.snowCover;
    CHECK(after < 0.95f && after > 0.85f);                          // it melts slowly once it stops
    // Kept with the scene.
    Scene empty, read;
    std::vector<NamedMaterial> none;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none, snowy.world.sceneSettings()), read, none, back, SceneAssets{}) && back.weather.snowSettles);
}

TEST(a_light_keeps_its_cookie_and_its_light_in_the_air) {
    Scene scene, read;
    Entity lamp = scene.create("Lamp");
    scene.add(lamp, Transform{});
    Light light;
    light.type = LightType::Spot;
    light.cookie = "Textures/Window Frame.png";
    light.isVolumetric = true;
    light.volumetricStrength = 2.5f;
    scene.add(lamp, light);
    Entity plain = scene.create("Plain");
    scene.add(plain, Transform{});
    scene.add(plain, Light{});
    std::vector<NamedMaterial> none;
    RenderSettings settings, back;
    std::string text = writeScene(scene, none, settings);
    CHECK(text.find("lightAir") != std::string::npos);
    CHECK(readScene(text, read, none, back, SceneAssets{}));
    int found = 0;
    read.each<Light>([&](Entity, Light& l) {
        if (l.type == LightType::Spot && l.cookie == "Textures/Window Frame.png" && l.isVolumetric && l.volumetricStrength == 2.5f) found++;
        if (l.type == LightType::Point && l.cookie.empty() && !l.isVolumetric && l.volumetricStrength == 1.0f) found++;
    });
    CHECK(found == 2);
}
