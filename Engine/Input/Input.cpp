#include "Input/Input.hpp"

namespace molten {

float Input::axis(GamepadAxis axis) const {
    if (axis == GamepadAxis::None || !gamepad_.connected) return 0.0f;
    float value = gamepad_.axes[size_t(axis)];
    // Remove the dead zone, then stretch what is left back to the full 0..1 range,
    // so the value does not jump when the stick leaves the dead zone.
    float magnitude = std::abs(value);
    if (magnitude < stickDeadZone) return 0.0f;
    float scaled = (magnitude - stickDeadZone) / (1.0f - stickDeadZone);
    return value < 0 ? -scaled : scaled;
}

void Input::bindButton(const std::string& action, std::initializer_list<Key> keys,
                       std::initializer_list<GamepadButton> buttons,
                       std::initializer_list<MouseButton> mouseButtons) {
    buttonBindings_[action] = ButtonBinding{keys, buttons, mouseButtons};
}

void Input::bindAxis(const std::string& action, Key negative, Key positive, GamepadAxis gamepadAxis, bool invertGamepadAxis) {
    axisBindings_[action] = AxisBinding{negative, positive, gamepadAxis, invertGamepadAxis};
}

bool Input::isDown(const std::string& action) const {
    auto found = buttonBindings_.find(action);
    if (found == buttonBindings_.end()) return false;
    for (Key key : found->second.keys) if (isDown(key)) return true;
    for (GamepadButton button : found->second.buttons) if (isDown(button)) return true;
    for (MouseButton button : found->second.mouseButtons) if (isDown(button)) return true;
    return false;
}

bool Input::wasPressed(const std::string& action) const {
    auto found = buttonBindings_.find(action);
    if (found == buttonBindings_.end()) return false;
    for (Key key : found->second.keys) if (wasPressed(key)) return true;
    for (GamepadButton button : found->second.buttons) if (wasPressed(button)) return true;
    for (MouseButton button : found->second.mouseButtons) if (wasPressed(button)) return true;
    return false;
}

float Input::axis(const std::string& action) const {
    auto found = axisBindings_.find(action);
    if (found == axisBindings_.end()) return 0.0f;
    const AxisBinding& binding = found->second;
    float value = 0.0f;
    if (isDown(binding.negative)) value -= 1.0f;
    if (isDown(binding.positive)) value += 1.0f;
    float stick = axis(binding.gamepadAxis);
    if (binding.invert) stick = -stick;
    if (std::abs(stick) > std::abs(value)) value = stick;      // whichever device is pushed harder wins
    return value;
}

void Input::handleKey(Key key, bool down) {
    if (key == Key::Unknown || key == Key::Count) return;
    keys_[size_t(key)].set(down);
}

void Input::handleMouseButton(MouseButton button, bool down) {
    if (button == MouseButton::Count) return;
    mouseButtons_[size_t(button)].set(down);
}

void Input::handleMouseMove(Vec2 position, Vec2 delta) {
    mousePosition_ = position;
    mouseDelta_ += delta;
}

void Input::handleScroll(float delta) { scrollDelta_ += delta; }

void Input::handleGamepad(const GamepadState& state) {
    gamepad_ = state;
    for (size_t i = 0; i < gamepadButtons_.size(); i++) {
        gamepadButtons_[i].set(state.connected && state.buttons[i]);
    }
}

void Input::endFrame() {
    for (ButtonState& state : keys_) state.endFrame();
    for (ButtonState& state : mouseButtons_) state.endFrame();
    for (ButtonState& state : gamepadButtons_) state.endFrame();
    mouseDelta_ = Vec2{0, 0};
    scrollDelta_ = 0;
    textInput_.clear();
}

void Input::handleText(uint32_t codepoint) {
    // Control characters (Enter, Backspace, Escape) arrive as keys, not as text.
    if (codepoint < 32 || codepoint == 127) return;
    // Encode the code point as UTF-8.
    if (codepoint < 0x80) {
        textInput_ += char(codepoint);
    } else if (codepoint < 0x800) {
        textInput_ += char(0xC0 | (codepoint >> 6));
        textInput_ += char(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        textInput_ += char(0xE0 | (codepoint >> 12));
        textInput_ += char(0x80 | ((codepoint >> 6) & 0x3F));
        textInput_ += char(0x80 | (codepoint & 0x3F));
    } else {
        textInput_ += char(0xF0 | (codepoint >> 18));
        textInput_ += char(0x80 | ((codepoint >> 12) & 0x3F));
        textInput_ += char(0x80 | ((codepoint >> 6) & 0x3F));
        textInput_ += char(0x80 | (codepoint & 0x3F));
    }
}

}  // namespace molten
