// Weather.hpp
// The scene's weather, built in: rain and storms, set in Window > Environment > Weather
// (RenderSettings::weather), none by default. A game that wants a weather of its own turns it
// off and makes its own, from the Rain effect (Kit/Effects.hpp) and its own code.
//
// Rain falls around whoever is looking (the camera of each view): drops that fall with the
// wind, each aimed, as it is born, at what it will hit: any collider (a car, a roof, the
// ground's mesh), or water. There it ends: on something solid it splashes, a few droplets
// thrown up and falling back; on water it leaves a ring that widens and fades. The sky
// clouds over as it rains, the sun dims behind it, the air thickens.
//
// A storm adds lightning: now and then the clouds light up from inside, once or a few times
// in quick succession, and the scene with them. (Its thunder is a sound the game plays: the
// moment of each flash is in World::weatherNow.)
//
// What it makes is three particle systems, of the world and not of the scene: no file
// holds them, the Hierarchy does not list them (EditorOnly).
#pragma once

#include "Math/Math.hpp"

namespace molten {
class World;
}

namespace molten::kit {

/// One frame of the weather: rain born, landed, splashed; lightning. `dt` in seconds.
void updateWeather(World& world, float dt);

}  // namespace molten::kit
