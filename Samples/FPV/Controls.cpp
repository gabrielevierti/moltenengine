#include "Controls.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

namespace fpv {

void Controls::reset() {
    leftKnob_ = Vec2{0, -1};
    rightKnob_ = Vec2{0, 0};
    keyboardThrottle_ = 0;
    keys_ = Vec3{0, 0, 0};
}

StickInput Controls::read(Engine& engine, float dt) {
    const Input& input = engine.input;
    StickInput sticks;
    if (input.isGamepadConnected()) {
        // A controller, or an RC radio that presents itself as one. Mode 2.
        float leftY = input.axis(GamepadAxis::LeftY);
        sticks.throttle = throttleUsesFullStick ? (leftY + 1) * 0.5f : std::max(leftY, 0.0f);
        sticks.yaw = input.axis(GamepadAxis::LeftX);
        sticks.pitch = input.axis(GamepadAxis::RightY);
        sticks.roll = input.axis(GamepadAxis::RightX);
    } else {
        // The keyboard. W and S move the throttle, which stays put like a radio's.
        if (input.isDown(Key::W)) keyboardThrottle_ = std::min(keyboardThrottle_ + dt * 0.8f, 1.0f);
        if (input.isDown(Key::S)) keyboardThrottle_ = std::max(keyboardThrottle_ - dt * 0.8f, 0.0f);
        // Keys are all or nothing. Ease toward a part of the travel, or every tap is a full-rate flip.
        Vec3 wanted{float(input.isDown(Key::D)) - float(input.isDown(Key::A)), float(input.isDown(Key::Up)) - float(input.isDown(Key::Down)),
                    float(input.isDown(Key::Right)) - float(input.isDown(Key::Left))};
        keys_ = damp(keys_, wanted * 0.65f, 14.0f, dt);
        sticks.throttle = keyboardThrottle_;
        sticks.yaw = keys_.x;
        sticks.pitch = keys_.y;
        sticks.roll = keys_.z;
    }
    leftKnob_ = Vec2{sticks.yaw, sticks.throttle * 2 - 1};
    rightKnob_ = Vec2{sticks.roll, sticks.pitch};
    return sticks;
}

void Controls::draw(Engine& engine, Canvas& canvas) const {
    if (!showsSticks) return;
    Vec2 size = canvas.size();
    const float radius = 58;
    float y = size.y - radius - 44;
    const Vec2 centers[2] = {Vec2{radius + 70, y}, Vec2{size.x - radius - 70, y}};
    const Vec2 knobs[2] = {leftKnob_, rightKnob_};
    for (int i = 0; i < 2; i++) {
        canvas.circle(centers[i], radius, Vec4{0, 0, 0, 0.22f});
        canvas.ring(centers[i], radius, 1.5f, Vec4{1, 1, 1, 0.35f});
        canvas.circle(centers[i] + Vec2{knobs[i].x, -knobs[i].y} * radius, 22, Vec4{1, 1, 1, 0.6f});
    }
}

}  // namespace fpv
