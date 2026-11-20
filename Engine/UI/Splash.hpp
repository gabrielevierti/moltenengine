// Splash.hpp
// The screen shown for a moment when a game made with the engine starts: the engine's
// logo, and the game's name underneath. (The editor has none: it opens on its projects.)
#pragma once

#include "Rendering/Canvas.hpp"

#include <string_view>

namespace molten {

struct Splash {
    static constexpr float kDuration = 2.4f;        // seconds on screen, the fade at the end included

    /// Draws the splash over the whole canvas. `seconds` is the time since it appeared.
    /// It fades in, stays, and fades out to show what is behind it.
    /// With `logo` (the engine's Assets/Branding/logo.png, 1200 x 268) that picture is shown;
    /// without it, the name in letters.
    static void draw(Canvas& canvas, float seconds, std::string_view subtitle, const Texture* logo = nullptr);

    /// True once the splash has faded away and nothing more needs to be drawn.
    static bool isOver(float seconds) { return seconds >= kDuration; }
};

}  // namespace molten
