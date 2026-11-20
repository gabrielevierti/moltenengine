// Weather.cpp
// The sky over the circuit: sun, clouds, rain, or a day that changes from one to the other.
// Put it on any one entity of the scene. It moves the scene's own settings from what they
// were made with toward an overcast, wet evening, as much as it rains:
//
//   the sky        clouds cover it, the sun dims behind them, the light goes grey
//   the air        mist comes up in the rain
//   the road       the material "Asphalt" darkens and shines as it gets wet, and dries
//                  again slowly when it stops; how wet it is, is the shared value
//                  "wetness", which the tyres grip by
//   the rain       a Particle System on the entity named in `rain` (one that follows the
//                  camera) falls as hard as it rains, and `rainSound` loops with it
//
// The weather comes from the preference "weather" (0 sun, 1 clouds, 2 rain, 3 changing),
// which the game's settings choose, or from `weather` when there is none.
// Shared values: {weather}, {rain} 0 to 1, {wetness} 0 to 1.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using namespace molten;

struct Weather {
    int weather = 0;                    // 0 sun, 1 clouds, 2 rain, 3 changing
    float changeEvery = 90.0f;          // changing: seconds between one sky and the next, about
    std::string rain = "Rain";
    std::string sun = "Sun";
    std::string road = "Asphalt";
    std::string rainSound = "Audio/rain.wav";

    float cloud = 0, rainNow = 0, wetness = 0;
    float target = 0, nextChange = 0;   // changing: where the sky is going, and when it next turns
    uint32_t rainVoice = 0;
    // The scene as it was made: everything goes from there.
    bool hasBase = false;
    SkySettings baseSky;
    Vec3 baseFogColor{0, 0, 0}, baseWind{0, 0, 0};
    float baseFog = 0, baseEnvironment = 1, baseSun = 1, baseRoughness = 0.8f;
    Vec4 baseRoad{0, 0, 0, 1};
    uint32_t seed = 12345;

    float random01() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    }

    /// How much it rains for each kind of weather (clouds follow the rain).
    static float rainFor(int kind) { return kind == 2 ? 1.0f : 0.0f; }
    static float cloudFor(int kind) { return kind == 0 ? 0.0f : kind == 1 ? 0.75f : 1.0f; }

    void start(World& world, Entity) {
        weather = std::clamp(int(world.preference("weather", float(weather))), 0, 3);
        RenderSettings& settings = world.sceneSettings();
        baseSky = settings.sky;
        baseFog = settings.fogDensity;
        baseFogColor = settings.fogColor;
        baseEnvironment = settings.environmentIntensity;
        baseWind = settings.wind;
        if (const Light* light = world.scene.get<Light>(world.find(sun))) baseSun = light->intensity;
        for (NamedMaterial& named : world.materials()) {
            if (named.name == road && named.material) {
                baseRoad = named.material->baseColor;
                baseRoughness = named.material->roughness;
            }
        }
        hasBase = true;
        // It starts as it is: no waiting for the rain to come or the road to dry.
        int now = weather == 3 ? (random01() < 0.5f ? 0 : 2) : weather;
        target = float(now);
        cloud = cloudFor(now);
        rainNow = rainFor(now);
        wetness = rainNow;
        nextChange = changeEvery * (0.6f + 0.8f * random01());
    }

    void update(World& world, Entity, float dt) {
        if (!hasBase) return;
        // The settings may choose another weather while the game runs.
        int wanted = std::clamp(int(world.preference("weather", float(weather))), 0, 3);
        if (wanted != weather) {
            weather = wanted;
            nextChange = 0;
        }
        int now = weather;
        if (weather == 3) {
            nextChange -= dt;
            if (nextChange <= 0) {
                // The next sky: sun, clouds or rain, rarely the same twice.
                int next = int(random01() * 3.0f) % 3;
                if (next == int(target)) next = (next + 1) % 3;
                target = float(next);
                nextChange = changeEvery * (0.6f + 0.8f * random01());
            }
            now = int(target);
        }
        // Skies change over some seconds; the road wets in a minute and dries in three.
        float cloudWanted = cloudFor(now), rainWanted = rainFor(now);
        cloud += (cloudWanted - cloud) * std::min(1.0f, dt / 12.0f);
        rainNow += (rainWanted - rainNow) * std::min(1.0f, dt / (rainWanted > rainNow ? 10.0f : 6.0f));
        if (rainNow > 0.05f) wetness = std::min(1.0f, wetness + rainNow * dt / 45.0f);
        else wetness = std::max(0.0f, wetness - dt / 180.0f);

        RenderSettings& settings = world.sceneSettings();
        settings.sky = baseSky;
        settings.sky.cloudCoverage = baseSky.cloudCoverage + (1.0f - baseSky.cloudCoverage) * cloud;
        Vec3 grey{0.42f, 0.45f, 0.5f};
        float overcast = cloud * 0.8f;
        settings.sky.horizonColor = baseSky.horizonColor + (grey - baseSky.horizonColor) * overcast;
        settings.sky.zenithColor = baseSky.zenithColor + (grey * 0.6f - baseSky.zenithColor) * overcast;
        settings.sky.sunDiskIntensity = baseSky.sunDiskIntensity * (1.0f - cloud * 0.95f);
        settings.environmentIntensity = baseEnvironment * (1.0f - 0.25f * cloud);
        settings.fogDensity = baseFog + 0.007f * rainNow;
        settings.fogColor = baseFogColor + (grey - baseFogColor) * overcast;
        settings.wind = baseWind + Vec3{3.0f, 0, 1.5f} * (cloud * 0.5f + rainNow);
        if (Light* light = world.scene.get<Light>(world.find(sun))) light->intensity = baseSun * (1.0f - 0.72f * cloud);
        // A wet road is darker and shines: it mirrors the sky and the lamps.
        for (NamedMaterial& named : world.materials()) {
            if (named.name != road || !named.material) continue;
            float darker = 1.0f - 0.4f * wetness;
            named.material->baseColor = Vec4{baseRoad.x * darker, baseRoad.y * darker, baseRoad.z * darker, baseRoad.w};
            named.material->roughness = baseRoughness + (0.12f - baseRoughness) * wetness;
        }
        if (ParticleEmitter* drops = world.scene.get<ParticleEmitter>(world.find(rain))) {
            drops->rate = 2600.0f * rainNow;
            drops->isEmitting = rainNow > 0.02f;
        }
        if (!rainSound.empty()) {
            if (rainVoice == 0 && rainNow > 0.02f) rainVoice = world.startSound(rainSound, 0.0f, 1.0f, true);
            world.changeSound(rainVoice, 0.55f * rainNow, 1.0f);
        }
        world.setValue("weather", float(now));
        world.setValue("rain", rainNow);
        world.setValue("wetness", wetness);
    }
};

MOLTEN_COMPONENT(Weather, MOLTEN_FIELD(weather).range(0, 3).help("0 sun, 1 clouds, 2 rain, 3 changing; the preference \"weather\" comes first"),
                 MOLTEN_FIELD(changeEvery).range(10, 1200), MOLTEN_FIELD(rain), MOLTEN_FIELD(sun), MOLTEN_FIELD(road), MOLTEN_FIELD(rainSound))
