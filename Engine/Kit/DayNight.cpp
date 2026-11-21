// DayNight.cpp
#include "Kit/DayNight.hpp"

#include "Game/Component.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

void linkDayNight() {}

namespace {

float smooth(float from, float to, float x) {
    float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

const Vec3 kNightZenith{0.004f, 0.006f, 0.02f}, kNightHorizon{0.015f, 0.022f, 0.05f}, kNightGround{0.006f, 0.007f, 0.01f};
const Vec3 kDawnHorizon{0.95f, 0.42f, 0.18f}, kDawnZenith{0.12f, 0.13f, 0.3f};

}  // namespace

DayNightLook DayNight::lookAt(float hour, float highest, float facing) {
    DayNightLook look;
    hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
    // From the east at 6 through the south at 12 to the west at 18; below the horizon at night.
    float around = (hour - 6.0f) / 12.0f * 3.14159265f;           // 0 at dawn, pi at dusk
    float height = std::sin(around) * std::sin(radians(highest));
    float elevation = std::asin(std::clamp(height, -1.0f, 1.0f));
    float azimuth = radians(90.0f + facing) + around;
    Vec3 sunward{std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth)};
    float day = smooth(-0.08f, 0.2f, sunward.y);           // how much of the day's sky there is
    float dusk = std::exp(-(sunward.y / 0.14f) * (sunward.y / 0.14f));      // most at the horizon
    look.night = 1.0f - smooth(-0.2f, 0.02f, sunward.y);
    // The sun by day; the moon, across the sky from it, by night.
    look.towardSun = sunward.y > -0.05f ? sunward : simd::normalize(Vec3{-sunward.x, std::max(-sunward.y, 0.2f), -sunward.z});
    look.lightShare = sunward.y > -0.05f ? smooth(-0.05f, 0.18f, sunward.y) : 0.04f * smooth(0.05f, 0.25f, -sunward.y);
    look.lightColor = sunward.y > -0.05f ? mix(Vec3{1.0f, 0.5f, 0.25f}, Vec3{1.0f, 0.96f, 0.9f}, smooth(0.0f, 0.45f, sunward.y))
                                         : Vec3{0.55f, 0.65f, 1.0f};
    look.skyShare = 0.04f + 0.96f * day;
    look.zenith = mix(kNightZenith, kDawnZenith, smooth(-0.25f, 0.0f, sunward.y));
    look.horizon = mix(kNightHorizon, kDawnHorizon, smooth(-0.25f, 0.0f, sunward.y) * dusk);
    look.ground = kNightGround;
    return look;
}

void DayNight::update(World& world, Entity, float dt) {
    if (dayMinutes > 0) hour = std::fmod(hour + dt * 24.0f / (dayMinutes * 60.0f), 24.0f);
    RenderSettings& settings = world.sceneSettings();
    if (!hasSkyBase) {
        // The scene's sky is the day's: the night and the dawn are made from it.
        hasSkyBase = true;
        dayZenith = settings.sky.zenithColor;
        dayHorizon = settings.sky.horizonColor;
        dayGround = settings.sky.groundColor;
        daySky = settings.sky.intensity;
        dayEnvironment = settings.environmentIntensity;
        dayFog = settings.fogColor;
    }
    DayNightLook look = lookAt(hour, highest, facing);
    float day = 1.0f - look.night;
    float dusk = std::exp(-(look.towardSun.y / 0.14f) * (look.towardSun.y / 0.14f)) * (look.night < 0.5f ? 1.0f : 0.0f);
    // By day the scene's own colors, warmed near the horizon; at night the night's.
    Vec3 zenith = mix(look.zenith, dayZenith, smooth(0.0f, 0.3f, look.towardSun.y) * day);
    Vec3 horizon = mix(mix(look.horizon, dayHorizon, day), look.horizon, dusk * 0.6f);
    settings.sky.zenithColor = zenith;
    settings.sky.horizonColor = horizon;
    settings.sky.groundColor = mix(look.ground, dayGround, day);
    settings.sky.intensity = daySky * look.skyShare;
    settings.environmentIntensity = dayEnvironment * (0.08f + 0.92f * day);
    settings.fogColor = mix(look.horizon * 0.6f, dayFog, day);
    world.setValue("hour", hour);
    world.setValue("night", look.night);

    Entity sunEntity = world.find(sun);
    Transform* transform = world.scene.get<Transform>(sunEntity);
    Light* light = world.scene.get<Light>(sunEntity);
    if (!transform || !light) return;
    // A directional light shines along its forward: away from the sun.
    Quat facingDown = lookRotation(-look.towardSun, std::abs(look.towardSun.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0});
    if (const Transform* parent = world.scene.get<Transform>(transform->parent)) {
        Vec3 p, s;
        Quat parentRotation;
        decompose(parent->world, p, parentRotation, s);
        facingDown = simd::normalize(conjugate(parentRotation) * facingDown);
    }
    transform->rotation = facingDown;
    light->color = look.lightColor;
    light->intensity = sunIntensity * look.lightShare;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(DayNight, MOLTEN_FIELD(hour).range(0, 24).help("0 to 24: 6 dawn, 12 noon, 18 dusk"),
                 MOLTEN_FIELD(dayMinutes).range(0, 240).help("Real minutes for a whole day; 0 stops the clock"),
                 MOLTEN_FIELD(sun).help("The entity with the Directional Light it moves"), MOLTEN_FIELD(sunIntensity).range(0, 20),
                 MOLTEN_FIELD(highest).range(5, 90).help("Degrees the sun is above the horizon at noon"),
                 MOLTEN_FIELD(facing).range(-180, 180).help("Degrees: turns the sun's path about the vertical"))
