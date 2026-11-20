// SoftRender.hpp
// Small pictures computed on the processor, for the editor's previews: a material on a
// ball, and a scene's triangles seen from above. No graphics device is involved, so they
// are the same on every machine and can be made for a file nobody has opened.
//
// Both are lit the way the renderer lights things (the same surface model: base color,
// metal, roughness, a sun and a sky), at the size of a thumbnail.
#pragma once

#include "Math/Math.hpp"

#include <cstdint>
#include <vector>

namespace molten::editor {

/// Pixels, four bytes each (red, green, blue, opacity), row by row from the top, sRGB.
struct SoftImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;

    bool isEmpty() const { return width <= 0 || height <= 0 || rgba.empty(); }
    /// The color at a place from 0 to 1 across and down, repeating outside, as linear light.
    Vec4 sample(float u, float v) const;
};

/// What a ball is made of: a Material's numbers, and its pictures if it has them.
struct BallLook {
    Vec4 baseColor{1, 1, 1, 1};
    float metallic = 0, roughness = 0.6f;
    Vec3 emissive{0, 0, 0};
    Vec2 uvScale{1, 1};
    bool isUnlit = false, isWater = false, isSeenThrough = false;
    const SoftImage* baseColorPicture = nullptr;
    const SoftImage* metallicRoughnessPicture = nullptr;        // roughness in green, metalness in blue
    const SoftImage* emissivePicture = nullptr;
    const SoftImage* normalPicture = nullptr;
};

/// A ball of the material under a sun and a sky, `size` pixels square, clear around it.
SoftImage renderBall(const BallLook& look, int size);

struct SoftTriangle {
    Vec3 a, b, c;
    Vec3 color{0.7f, 0.7f, 0.7f};       // linear
    float glow = 0;                     // 1: its color as it is, unlit
};

struct SoftView {
    Vec3 eye{0, 5, 10}, target{0, 0, 0};
    float fovY = 0.8f;                  // radians
    Vec3 sunDirection{-0.4f, -0.8f, -0.45f};        // the way the light travels
    Vec3 sunColor{1.0f, 0.96f, 0.88f};
    float sunIntensity = 3.0f;
    Vec3 skyZenith{0.2f, 0.4f, 0.8f}, skyHorizon{0.7f, 0.8f, 0.9f}, skyGround{0.2f, 0.2f, 0.2f};
    float fogDistance = 0;              // where things are half lost in the horizon's color; 0 for none
};

/// Something of a scene: where its middle is and how far it reaches from there.
struct SoftThing {
    Vec3 center;
    float reach = 0;
};

/// Puts the eye above and to one side of where most of the things are, far enough to hold
/// most of them. A floor a kilometer wide is one thing among many, and does not decide.
/// With `around` (where the game's own camera is, say), it looks at what is near there
/// instead: in a wide world, that is where the game happens.
void lookAtThings(SoftView& view, const std::vector<SoftThing>& things, const Vec3* around = nullptr);

/// The triangles from a place, nearest in front, under the view's sun and sky.
SoftImage renderTriangles(const std::vector<SoftTriangle>& triangles, const SoftView& view, int width, int height);

}  // namespace molten::editor
