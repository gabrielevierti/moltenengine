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

/// Names for keys and controller parts, as files and the editor write them: "A", "Space",
/// "Left", "LeftX". Unknown names give Key::Unknown, GamepadButton::Count, GamepadAxis::None.
const char* keyName(Key key);
Key keyByName(const std::string& name);
const char* gamepadButtonName(GamepadButton button);
GamepadButton gamepadButtonByName(const std::string& name);
const char* gamepadAxisName(GamepadAxis axis);
GamepadAxis gamepadAxisByName(const std::string& name);

/// One thing the player can do, and everything that does it. A button is on or off; an
/// axis goes from -1 to 1.
struct InputAction {
    std::string name;
    bool isAxis = false;
    // A button: any of these.
    std::vector<Key> keys;
    std::vector<GamepadButton> buttons;
    std::vector<MouseButton> mouseButtons;
    // An axis: keys that push it each way (two of each, for W A S D and the arrows), and a stick.
    Key negative = Key::Unknown, positive = Key::Unknown;
    Key negativeAlt = Key::Unknown, positiveAlt = Key::Unknown;
    GamepadAxis gamepadAxis = GamepadAxis::None;
    bool invert = false;
    // A joystick that is not a game controller (a radio for models, a flight stick, a wheel):
    // its axes have no names, only numbers. -1 is none.
    int joystickAxis = -1;
    bool joystickInvert = false;
};

/// The actions of a game: what a project's Input.actions file holds and the editor's
/// Input page of Project Settings edits. Game code asks for actions by name and never for keys, so the
/// keys can change without the code.
struct InputMap {
    std::vector<InputAction> actions;

    std::string toText() const;
    static InputMap fromText(const std::string& text);
    /// What every game starts with: moveX, moveZ, jump, fire, pause.
    static InputMap standard();
    /// The standard actions and those FlightControls reads: "yaw" (Q E), "throttle" (Shift
    /// and Ctrl, or Z X), "throttleLever" (a radio's or flight stick's lever), "flaps" (F) and
    /// "brake" (B). For a game that flies: the Input page adds them with one button.
    static InputMap flying();
};

/// One snapshot of a controller, as the platform layer reads it each frame.
struct GamepadState {
    bool connected = false;
    std::array<bool, size_t(GamepadButton::Count)> buttons{};
    std::array<float, size_t(GamepadAxis::Count)> axes{};      // sticks: -1 to 1, +Y is up. Triggers: 0 to 1
};

/// A joystick that is not laid out like a game controller: a radio for model aircraft
/// plugged in by USB, a flight stick, a steering wheel. Axes and buttons by number.
struct JoystickState {
    bool connected = false;
    std::string name;
    std::vector<float> axes;            // -1 to 1
    std::vector<bool> buttons;
};

class Input {
public:
    /// Starts with the standard actions, so that a game can ask for "jump" from its first line.
    Input();

    // ---- raw state. "Down" is true while held; "pressed" and "released" are true for one frame.

    // ---- controls drawn on the screen (a thumb stick, buttons: Kit's TouchStick and TouchButton)
    /// Pushes an axis action from code, from -1 to 1, until it is set again: it counts as one
    /// more device, and whichever is pushed harder wins. What on-screen controls call.
    void setVirtualAxis(const std::string& action, float value) { virtualAxes_[action] = value; }
    /// Holds or lets go of a button action from code.
    void setVirtualButton(const std::string& action, bool down);
    /// True once a finger has touched the screen: this is a device to show touch controls on.
    /// True once the screen has been touched, or from the start on a device whose screen is
    /// how it is used (a phone, a tablet): touch controls hidden "until a touch" show at once there.
    bool hasTouched() const { return hasTouched_ || isTouchScreen; }
    bool isTouchScreen = false;

    /// Off, the game hears nothing: no key is down, no button, no stick is moved, nothing is
    /// typed. An editor turns it off while the game runs but its window is not the one
    /// being used, so that typing a name into a field does not also drive the car.
    bool isListening = true;

    /// A text field of the game is being typed in: the letters are its, not the game's (W does
    /// not drive the car while a name is typed). Only the keys that edit text are still seen.
    bool isTakingText = false;

    bool isDown(Key key) const { return isHeard(key) && keys_[size_t(key)].down; }
    bool wasPressed(Key key) const { return isHeard(key) && keys_[size_t(key)].pressed; }
    bool wasReleased(Key key) const { return isHeard(key) && keys_[size_t(key)].released; }
    bool isHeard(Key key) const {
        if (!isListening) return false;
        return !isTakingText || key == Key::Backspace || key == Key::Enter || key == Key::Escape || key == Key::Tab;
    }

    bool isDown(MouseButton button) const { return isListening && mouseButtons_[size_t(button)].down; }
    bool wasPressed(MouseButton button) const { return isListening && mouseButtons_[size_t(button)].pressed; }
    bool wasReleased(MouseButton button) const { return isListening && mouseButtons_[size_t(button)].released; }

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
    const std::vector<Touch>& touches() const { return isListening ? touches_ : noTouches_; }
    /// Called by the platform layer once per frame.
    void handleTouches(std::vector<Touch> touches) {
        touches_ = std::move(touches);
        hasTouched_ = hasTouched_ || !touches_.empty();
    }

    /// In points of the game's picture, origin at its top left. (In an editor the game is
    /// shown in a part of the window, perhaps smaller than it is: see setPicture.)
    /// While the game holds the pointer (see setPointerCaptured) there is none to be anywhere:
    /// it is nowhere near the screen, so that no button drawn there is under it.
    Vec2 mousePosition() const { return pointerIsCaptured_ ? Vec2{-1e6f, -1e6f} : (mousePosition_ - pictureOrigin_) * pictureScale_; }
    Vec2 mouseDelta() const { return isListening ? mouseDelta_ * pictureScale_ : Vec2{0, 0}; }     // movement during this frame

    /// A game that looks about with the mouse (a first-person view, a camera turned without a
    /// button held) asks for the pointer: it is hidden and kept in the window, however far the
    /// mouse goes, and only how it moves is told (mouseDelta). Asked again with false, it is
    /// given back: for a menu, or when Esc is pressed. The window does it (and an editor only
    /// while its Game view is the one in use); nothing happens where there is no pointer.
    void setPointerCaptured(bool wanted) { wantsPointer_ = wanted; }
    /// What the game asked for, while it is being listened to and no menu holds the pointer out.
    bool wantsPointerCaptured() const { return wantsPointer_ && isListening && !pointerIsFree_; }
    /// A menu is on the screen and needs the pointer: it stays out, whatever asked for it,
    /// until this is called again with false. What asked for it then has it back.
    void setPointerFree(bool isFree) { pointerIsFree_ = isFree; }
    bool isPointerFree() const { return pointerIsFree_; }
    /// True while the window really holds the pointer for the game.
    bool isPointerCaptured() const { return pointerIsCaptured_; }
    /// The window says so, each time it changes.
    void setPointerIsCaptured(bool isCaptured) { pointerIsCaptured_ = isCaptured; }
    /// Where the game's picture is in the window and how many of its points one of the
    /// window's is: an editor says so each frame, so that a click on a button drawn in
    /// the Game view is a click on that button. A game on its own never calls this.
    void setPicture(Vec2 origin, float scale) {
        pictureOrigin_ = origin;
        pictureScale_ = scale > 0 ? scale : 1.0f;
    }
    float scrollDelta() const { return isListening ? scrollDelta_ : 0.0f; }

    /// The characters typed during this frame, as UTF-8: what a text field should insert.
    /// Unlike key presses, this respects the keyboard layout, Shift and key repeat.
    const std::string& textInput() const { return isListening ? textInput_ : noText_; }

    /// The joystick as it is now. See JoystickState.
    const JoystickState& joystick() const { return joystick_; }
    /// One of its axes, with a small dead zone removed; 0 if there is no such axis.
    float joystickAxis(int index) const;
    /// True if an axis action is set to a joystick axis that is there to be read: a real
    /// stick, which says where it is, as keys cannot.
    bool hasJoystick(const std::string& action) const;
    void handleJoystick(JoystickState state) { joystick_ = std::move(state); }

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

    /// Replaces every action with those of a map.
    void apply(const InputMap& map);
    bool hasAction(const std::string& action) const { return buttonBindings_.count(action) || axisBindings_.count(action); }

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
        Key negativeAlt = Key::Unknown, positiveAlt = Key::Unknown;
        int joystickAxis = -1;
        bool joystickInvert = false;
    };
    JoystickState joystick_;
    struct VirtualButton {
        bool down = false, pressed = false;
    };
    std::unordered_map<std::string, float> virtualAxes_;
    std::unordered_map<std::string, VirtualButton> virtualButtons_;
    bool hasTouched_ = false;
    std::vector<Touch> noTouches_;      // what is heard while not listening
    std::string noText_;

    std::array<ButtonState, size_t(Key::Count)> keys_{};
    std::array<ButtonState, size_t(MouseButton::Count)> mouseButtons_{};
    std::array<ButtonState, size_t(GamepadButton::Count)> gamepadButtons_{};
    GamepadState gamepad_{};
    std::vector<Touch> touches_;
    Vec2 mousePosition_{0, 0};
    Vec2 pictureOrigin_{0, 0};
    float pictureScale_ = 1.0f;
    Vec2 mouseDelta_{0, 0};
    bool wantsPointer_ = false, pointerIsCaptured_ = false, pointerIsFree_ = false;
    float scrollDelta_ = 0;
    std::string textInput_;
    std::unordered_map<std::string, ButtonBinding> buttonBindings_;
    std::unordered_map<std::string, AxisBinding> axisBindings_;
};

}  // namespace molten
