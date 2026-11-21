// The time of day: the sun's path, its light, the sky.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/DayNight.hpp"
#include "Kit/Kit.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <cmath>

using namespace molten;
using namespace molten::kit;

TEST(the_sun_crosses_the_sky) {
    DayNightLook noon = DayNight::lookAt(12, 70, 0), dawn = DayNight::lookAt(6.2f, 70, 0), dusk = DayNight::lookAt(17.8f, 70, 0);
    DayNightLook night = DayNight::lookAt(0, 70, 0), morning = DayNight::lookAt(9, 70, 0);
    CHECK(std::abs(std::asin(noon.towardSun.y) * 57.29578f - 70) < 0.5f);      // as high as it goes at noon
    CHECK(dawn.towardSun.y > 0 && dawn.towardSun.y < 0.1f && dusk.towardSun.y > 0 && dusk.towardSun.y < 0.1f);
    CHECK(dawn.towardSun.x * dusk.towardSun.x < 0);                             // rises on one side, sets on the other
    CHECK(noon.lightShare > 0.99f && noon.night < 0.01f);
    CHECK(dawn.lightColor.z < dawn.lightColor.x * 0.6f);                        // low sun: warm
    CHECK(noon.lightColor.z > 0.85f);
    CHECK(night.night > 0.99f && night.lightShare < 0.05f && night.towardSun.y > 0);    // the moon, dim and blue
    CHECK(night.lightColor.z > night.lightColor.x);
    CHECK(morning.lightShare > 0.9f && morning.towardSun.y < noon.towardSun.y);
    // A day is the same day again: 24 is 0, 36 is 12.
    CHECK(simd::length(DayNight::lookAt(36, 70, 0).towardSun - noon.towardSun) < 1e-4f);
}

TEST(day_and_night_light_the_scene) {
    World world;
    kit::registerKit();
    Entity sun = world.scene.create("Sun");
    world.scene.add(sun, Transform{});
    Light light;
    light.type = LightType::Directional;
    world.scene.add(sun, light);
    Entity clock = world.scene.create("Clock");
    world.scene.add(clock, Transform{});
    DayNight cycle;
    cycle.hour = 12;
    cycle.dayMinutes = 1;                       // a day in a minute: an hour in 2.5 seconds
    world.scene.add(clock, cycle);
    Vec3 dayZenith = world.sceneSettings().sky.zenithColor;
    world.advance(1.0 / 60.0);
    const Transform* placed = world.scene.get<Transform>(sun);
    Vec3 forward = rotate(placed->rotation, Vec3{0, 0, -1});
    CHECK(forward.y < -0.9f);                                                 // noon: shining down
    CHECK(world.scene.get<Light>(sun)->intensity > 3.9f);
    CHECK(simd::length(world.sceneSettings().sky.zenithColor - dayZenith) < 0.05f);
    // Twelve hours later: night.
    for (int i = 0; i < 30 * 60; i++) world.advance(1.0 / 60.0);
    CHECK(world.value("hour") > 23.5f || world.value("hour") < 0.5f);
    CHECK(world.value("night") > 0.9f);
    CHECK(world.scene.get<Light>(sun)->intensity < 0.2f);
    CHECK(world.sceneSettings().sky.zenithColor.z < dayZenith.z * 0.3f);
}
