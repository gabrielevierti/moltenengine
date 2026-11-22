// Water.hpp
// Water in a scene is its sea (Environment > Water): everywhere below a level, with the waves
// the wind and the weather raise (RenderSettings::sea, Water/Waves.hpp). Water of a game's own,
// a pond or a pool, is a surface shader it writes (Assets > New > Water Shader).
//
// Scenes of before said water other ways, and still read: a material whose shader was Water on
// a plane, and before that a Water component beside it, which gave the sheet its size and the
// wind. When such a scene is read the first of those planes that is as large as a sea (300 m
// and more) becomes its sea, at the plane's height, in its material's colors, and the plane is
// hidden; a smaller one (a pool, a basin) stays, a plane of clear blue seen through. A Water
// component's wind becomes the scene's (where the scene has none). The components go (retireOldWater).
#pragma once

#include "Core/World.hpp"
#include "Water/Waves.hpp"

namespace molten {

struct Water {
    float size = 400.0f;                // meters along each side
    int resolution = 128;
    float windSpeed = 8.0f;             // meters per second
    Vec2 windDirection{1.0f, 0.3f};
    float patchSize = 120.0f;
    float choppiness = 1.0f;
    float waveHeight = 1.0f;
    float foam = 1.0f;
};

/// What old scenes said with water materials and Water components, said where it is said now:
/// the scene's sea, at the height of the first plane that was water, in its colors; the planes
/// hidden; the wind of a Water component the scene's, where the scene has none. The components go.
void retireOldWater(Scene& scene, struct RenderSettings& settings);

}  // namespace molten
