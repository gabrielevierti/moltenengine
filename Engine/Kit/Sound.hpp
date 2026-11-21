// Sound.hpp
// Rooms for sounds: a Reverb Zone makes what is heard inside it ring as a cave, a hall or a
// bathroom does. Where zones meet, the one the listener is most inside is heard; at a zone's
// edge it fades over `edge` meters. Outside every zone, sounds are dry.
//
// The groups sounds play in (Music, Effects, Voices, Interface) and their volumes are the
// World's: world.setVolume("Music", 0.5f). An Audio Source says its group.
#pragma once

#include "Core/World.hpp"

#include <string>

namespace molten::kit {

struct ReverbZone {
    std::string room = "Hall";          // Small Room, Hall, Cave, Bathroom, Cathedral, Forest, or Custom (the three below)
    float radius = 10;                  // meters from the entity, as a sphere; with `isBox`, half the box's size in each direction
    bool isBox = false;
    Vec3 halfSize{10, 4, 10};
    float edge = 3;                     // meters over which it fades in at its edge
    float size = 0.6f;                  // Custom: 0 small to 1 huge
    float damping = 0.4f;               // Custom: 0 bright (stone) to 1 dull (curtains)
    float wet = 0.35f;                  // Custom: how much of it is heard

    void lateUpdate(World& world, Entity self, float dt);
    /// How much the point is inside: 1 within, fading to 0 over the edge.
    float inside(const World& world, Entity self, Vec3 point) const;
};

/// The size, damping and wet of a room by name; false for Custom or a name it does not know.
bool roomPreset(const std::string& room, float& size, float& damping, float& wet);

void linkSound();

}  // namespace molten::kit
