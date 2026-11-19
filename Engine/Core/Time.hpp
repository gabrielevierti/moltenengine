// Time.hpp
// Frame timing and the fixed-step clock.
//
// Rendering runs as fast as the display allows, with a different duration every frame.
// Physics is only stable and repeatable with a constant step. The classic answer is an
// accumulator: real time is poured in each frame and consumed in fixed-size steps.
// Whatever is left over, as a fraction of a step, is `alpha`: how far the renderer
// should blend from the previous physics state to the current one.
#pragma once

#include <algorithm>
#include <cstdint>

namespace molten {

struct Time {
    double elapsed = 0;                 // seconds of game time since the start
    float delta = 0;                    // duration of this frame, scaled by timeScale
    float unscaledDelta = 0;
    float fixedDelta = 1.0f / 60.0f;    // duration of one physics step
    float alpha = 0;                    // 0..1: position of this frame between two physics steps
    float timeScale = 1.0f;             // 0 pauses, 0.5 is slow motion
    float framesPerSecond = 0;          // smoothed
    uint64_t frameCount = 0;
    int maxStepsPerFrame = 5;           // after a long hitch, drop time instead of spiralling

    /// Starts a frame that lasted `realDelta` seconds. Returns how many fixed steps to run now.
    int beginFrame(double realDelta) {
        realDelta = std::clamp(realDelta, 0.0, 0.25);
        unscaledDelta = float(realDelta);
        delta = unscaledDelta * timeScale;
        elapsed += delta;
        frameCount++;
        if (realDelta > 0) {
            float instant = float(1.0 / realDelta);
            framesPerSecond = framesPerSecond <= 0 ? instant : framesPerSecond + (instant - framesPerSecond) * 0.05f;
        }

        accumulator_ += delta;
        int steps = 0;
        while (accumulator_ >= fixedDelta && steps < maxStepsPerFrame) {
            accumulator_ -= fixedDelta;
            steps++;
        }
        if (steps == maxStepsPerFrame) accumulator_ = std::min(accumulator_, double(fixedDelta));
        alpha = float(accumulator_ / fixedDelta);
        return steps;
    }

private:
    double accumulator_ = 0;
};

}  // namespace molten
