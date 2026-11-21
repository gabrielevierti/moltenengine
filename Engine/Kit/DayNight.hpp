// DayNight.hpp
// The time of day: the sun crosses the sky, its light warms at dawn and dusk and goes at night
// (a dim blue moon then), and the sky, the light of the sky and the fog follow. Put it on any
// entity; it moves the scene's sun (a Directional Light, named in `sun`).
#pragma once

#include "Core/World.hpp"

namespace molten::kit {

/// The look of the world at an hour: what the sun and the sky are then.
struct DayNightLook {
    Vec3 towardSun{0, 1, 0};            // from the ground to the sun (or the moon, at night)
    Vec3 lightColor{1, 1, 1};
    float lightShare = 1;               // of the sun's full intensity
    Vec3 zenith, horizon, ground;       // the sky's colors
    float skyShare = 1;                 // of the sky's full brightness
    float night = 0;                    // 0 day, 1 deep night: for lamps, windows, stars
};

struct DayNight {
    float hour = 14.0f;                 // 0 to 24: 6 dawn, 12 noon, 18 dusk
    float dayMinutes = 0.0f;            // real minutes for a whole day; 0 stops the clock
    std::string sun = "Sun";            // the entity with the Directional Light it moves
    float sunIntensity = 4.0f;          // the sun's light at noon
    float highest = 70.0f;              // degrees the sun is above the horizon at noon
    float facing = 0.0f;                // degrees: turns the sun's path about the vertical

    void update(World& world, Entity self, float dt);
    /// The look at an hour; pure, for tests and for tools.
    static DayNightLook lookAt(float hour, float highest, float facing);

    bool hasSkyBase = false;            // the scene's own sky, read once: the day's colors are its
    Vec3 dayZenith, dayHorizon, dayGround;
    float daySky = 1, dayEnvironment = 1;
    Vec3 dayFog;
};

void linkDayNight();

}  // namespace molten::kit
