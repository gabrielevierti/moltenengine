// Input.hpp
// Keyboard, mouse and gamepad state, plus named actions.
//
// The platform layer (GLFW here, AppKit in the Swift engine) reports events through the
// handle... functions. Game code only asks questions:
//
//     input.bindAxis("moveX", Key::A, Key::D, GamepadAxis::LeftX);
//     input.bindButton("jump", {Key::Space}, {GamepadButton::A});
//     float x = input.axis("moveX");
//     if (input.wasPressed("jump")) { ... }
#pragma once

#include "Math/Math.hpp"

#include <array>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

enum class Key : uint8_t {
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace,
    Left, Right, Up, Down,
    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

enum class MouseButton : uint8_t { Left, Right, Middle, Count };

enum class GamepadButton : uint8_t {
    A, B, X, Y, LeftShoulder, RightShoulder, Back, Start, LeftStick, RightStick,
    DpadUp, DpadDown, DpadLeft, DpadRight,
    Count
};

enum class GamepadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count, None };

/// One snapshot of a controller, as the platform layer reads it each frame.
struct GamepadState {
    bool connected = false;
    std::array<bool, size_t(GamepadButton::Count)> buttons{};
    std::array<float, size_t(GamepadAxis::Count)> axes{};      // sticks: -1 to 1, +Y is up. Triggers: 0 to 1
};

class Input {
public:
    // ---- raw state. "Down" is true while held; "pressed" and "released" are true for one frame.

    bool isDown(Key key) const { return keys_[size_t(key)].down; }
    bool wasPressed(Key key) const { return keys_[size_t(key)].pressed; }
    bool wasReleased(Key key) const { return keys_[size_t(key)].released; }

    bool isDown(MouseButton button) const { return mouseButtons_[size_t(button)].down; }
    bool wasPressed(MouseButton button) const { return mouseButtons_[size_t(button)].pressed; }
    bool wasReleased(MouseButton button) const { return mouseButtons_[size_t(button)].released; }

    /// The fingers on the screen right now, on devices that have one. The first finger also
    /// acts as the mouse with its left button down, so that code written for a mouse works
    /// under a thumb; games that want several fingers read this list.
    struct Touch {
        uint64_t id = 0;                // the same for a finger from the moment it lands until it lifts
        Vec2 position{0, 0};            // in points, origin at the top left
        Vec2 delta{0, 0};               // movement during this frame
        bool hasBegun = false;          // landed during this frame
        bool hasEnded = false;          // lifted during this frame: gone from the list next frame
    };
    const std::vector<Touch>& touches() const { return touches_; }
    /// Called by the platform layer once per frame.
    void handleTouches(std::vector<Touch> touches) { touches_ = std::move(touches); }

    Vec2 mousePosition() const { return mousePosition_; }      // in points, origin at the top left
    Vec2 mouseDelta() const { return mouseDelta_; }            // movement during this frame
    float scrollDelta() const { return scrollDelta_; }

    /// The characters typed during this frame, as UTF-8: what a text field should insert.
    /// Unlike key presses, this respects the keyboard layout, Shift and key repeat.
    const std::string& textInput() const { return textInput_; }

    bool isGamepadConnected() const { return gamepad_.connected; }
    bool isDown(GamepadButton button) const { return gamepadButtons_[size_t(button)].down; }
    bool wasPressed(GamepadButton button) const { return gamepadButtons_[size_t(button)].pressed; }
    /// A stick or trigger value with the dead zone already removed.
    float axis(GamepadAxis axis) const;

    // ---- actions: names for "whatever the player uses to do this"

    void bindButton(const std::string& action, std::initializer_list<Key> keys,
                    std::initializer_list<GamepadButton> buttons = {},
                    std::initializer_list<MouseButton> mouseButtons = {});

    /// An axis is -1 while `negative` is held, +1 while `positive` is held, or the gamepad axis value.
    void bindAxis(const std::string& action, Key negative, Key positive,
                  GamepadAxis gamepadAxis = GamepadAxis::None, bool invertGamepadAxis = false);

    bool isDown(const std::string& action) const;
    bool wasPressed(const std::string& action) const;
    float axis(const std::string& action) const;

    // ---- fed by the platform layer

    void handleKey(Key key, bool down);
    void handleMouseButton(MouseButton button, bool down);
    void handleMouseMove(Vec2 position, Vec2 delta);
    void handleScroll(float delta);
    void handleText(uint32_t codepoint);
    void handleGamepad(const GamepadState& state);

    /// Clears the one-frame flags. The engine calls this after every frame.
    void endFrame();

    float stickDeadZone = 0.15f;

private:
    struct ButtonState {
        bool down = false, pressed = false, released = false;
        void set(bool isDown) {
            if (isDown && !down) pressed = true;
            if (!isDown && down) released = true;
            down = isDown;
        }
        void endFrame() { pressed = released = false; }
    };

    struct ButtonBinding {
        std::vector<Key> keys;
        std::vector<GamepadButton> buttons;
        std::vector<MouseButton> mouseButtons;
    };

    struct AxisBinding {
        Key negative = Key::Unknown, positive = Key::Unknown;
        GamepadAxis gamepadAxis = GamepadAxis::None;
        bool invert = false;
    };

    std::array<ButtonState, size_t(Key::Count)> keys_{};
    std::array<ButtonState, size_t(MouseButton::Count)> mouseButtons_{};
    std::array<ButtonState, size_t(GamepadButton::Count)> gamepadButtons_{};
    GamepadState gamepad_{};
    std::vector<Touch> touches_;
    Vec2 mousePosition_{0, 0};
    Vec2 mouseDelta_{0, 0};
    float scrollDelta_ = 0;
    std::string textInput_;
    std::unordered_map<std::string, ButtonBinding> buttonBindings_;
    std::unordered_map<std::string, AxisBinding> axisBindings_;
};

}  // namespace molten
