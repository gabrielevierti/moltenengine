// Controls.hpp
// From a keyboard or a controller to stick positions. Whatever is used, the result is
// the same StickInput, so the flight model never knows.
//
// (The Swift version of the game also has two sticks on the screen, for an iPhone.)
#pragma once

#include "FlightModel.hpp"
#include "MoltenMetal.hpp"

namespace fpv {

class Controls {
public:
    /// How a controller's left stick becomes throttle.
    ///   true:  the whole travel, bottom = 0, top = 1. For RC radios, whose throttle stick
    ///          stays where you leave it.
    ///   false: only the upper half, center = 0. For gamepads, whose sticks spring back
    ///          to the middle (and would otherwise sit at half throttle).
    bool throttleUsesFullStick = true;
    /// Draw the two sticks on screen, to see what the pilot is doing.
    bool showsSticks = false;

    void reset();
    StickInput read(molten::Engine& engine, float dt);
    void draw(molten::Engine& engine, molten::Canvas& canvas) const;

private:
    molten::Vec2 leftKnob_{0, -1};          // x right, y up, each from -1 to 1
    molten::Vec2 rightKnob_{0, 0};
    float keyboardThrottle_ = 0;
    molten::Vec3 keys_{0, 0, 0};            // yaw, pitch, roll from the keyboard, eased
};

}  // namespace fpv
