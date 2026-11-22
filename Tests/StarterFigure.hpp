// StarterFigure.hpp
// The person the Third Person Game is walked with: a figure of flat faces with bones in it and
// the animations a game's character needs (Idle, Walk, Run, Jump, Fall), made here, in code, and
// written as one .glb file, the way a character downloaded from a library of animations comes.
//
// It is laid out the way those are too, so that one of them can take its place: it stands on
// the origin, its head toward +y and its face toward +z (a scene turns its entity half way
// round, for the engine's "forward" is -z), in meters, 1.8 tall, and its clips are named by
// what they are. Put another model's file where this one is, with clips of the same names, and
// the game's Animator plays that one instead.
//
// How it moves is worked out rather than drawn. The feet come first: where each ankle is
// through a step (down on the heel, flat, up on the toes, swung forward) at the speed the clip
// is for, so that a foot on the ground stays where it is while the body goes over it. The legs
// are then bent to reach from the hips to the ankles, and the rest (the hips' rise and fall,
// the turn of the shoulders against the hips, the arms) follows the same step.
#pragma once

#include "Math/Math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace starter {

using namespace molten;

struct Figure {
    std::vector<uint8_t> glb;           // the model: its faces, bones and animations
    std::vector<uint8_t> palette;       // its colors, a small picture (.png) its faces point into

    /// How fast the Walk and Run clips cover the ground, meters per second: a character moved
    /// at these speeds does not slide on its feet.
    static constexpr float kWalkSpeed = 1.6f, kRunSpeed = 4.6f;
};

Figure makeFigure();

/// A picture of flat colors as a .png file's bytes (RGB, 8 bits each, not compressed).
std::vector<uint8_t> pngOf(int width, int height, const std::vector<uint8_t>& rgb);

}  // namespace starter
