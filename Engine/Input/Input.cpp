#include "Input/Input.hpp"

#include <algorithm>
#include <cstdlib>

namespace molten {

float Input::axis(GamepadAxis axis) const {
    if (!isListening) return 0.0f;
    if (axis == GamepadAxis::None || !gamepad_.connected) return 0.0f;
    float value = gamepad_.axes[size_t(axis)];
    // Remove the dead zone, then stretch what is left back to the full 0..1 range,
    // so the value does not jump when the stick leaves the dead zone.
    float magnitude = std::abs(value);
    if (magnitude < stickDeadZone) return 0.0f;
    float scaled = (magnitude - stickDeadZone) / (1.0f - stickDeadZone);
    return value < 0 ? -scaled : scaled;
}

float Input::joystickAxis(int index) const {
    if (!isListening) return 0.0f;
    if (!joystick_.connected || index < 0 || size_t(index) >= joystick_.axes.size()) return 0.0f;
    float value = joystick_.axes[size_t(index)];
    const float dead = 0.02f;           // a radio's sticks have no slack to speak of
    if (std::abs(value) < dead) return 0.0f;
    return std::clamp((value < 0 ? value + dead : value - dead) / (1.0f - dead), -1.0f, 1.0f);
}

bool Input::hasJoystick(const std::string& action) const {
    auto found = axisBindings_.find(action);
    return found != axisBindings_.end() && joystick_.connected && found->second.joystickAxis >= 0 && size_t(found->second.joystickAxis) < joystick_.axes.size();
}

// ---------------------------------------------------------------- names

namespace {

const char* const kKeyNames[] = {
    "Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "Space", "Enter", "Escape", "Tab", "Backspace", "Left", "Right", "Up", "Down",
    "LeftShift", "RightShift", "LeftControl", "RightControl", "LeftAlt", "RightAlt",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
static_assert(sizeof(kKeyNames) / sizeof(kKeyNames[0]) == size_t(Key::Count), "one name per key");
const char* const kButtonNames[] = {"A", "B", "X", "Y", "LeftShoulder", "RightShoulder", "Back", "Start", "LeftStick", "RightStick",
                                    "DpadUp", "DpadDown", "DpadLeft", "DpadRight"};
static_assert(sizeof(kButtonNames) / sizeof(kButtonNames[0]) == size_t(GamepadButton::Count), "one name per button");
const char* const kAxisNames[] = {"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
const char* const kMouseNames[] = {"Left", "Right", "Middle"};

std::string lowered(const std::string& text) {
    std::string out;
    for (char c : text) out += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

}  // namespace

const char* keyName(Key key) {
    return size_t(key) < size_t(Key::Count) ? kKeyNames[size_t(key)] : "Unknown";
}

Key keyByName(const std::string& name) {
    if (name == " ") return Key::Space;
    std::string wanted = lowered(name);
    for (size_t i = 1; i < size_t(Key::Count); i++) {
        if (wanted == lowered(kKeyNames[i])) return Key(i);
    }
    // What people also call them.
    static const std::pair<const char*, Key> others[] = {{"return", Key::Enter}, {"esc", Key::Escape}, {"shift", Key::LeftShift},
                                                         {"control", Key::LeftControl}, {"ctrl", Key::LeftControl}, {"alt", Key::LeftAlt}};
    for (const auto& [text, key] : others) {
        if (wanted == text) return key;
    }
    return Key::Unknown;
}

const char* gamepadButtonName(GamepadButton button) {
    return size_t(button) < size_t(GamepadButton::Count) ? kButtonNames[size_t(button)] : "None";
}

GamepadButton gamepadButtonByName(const std::string& name) {
    for (size_t i = 0; i < size_t(GamepadButton::Count); i++) {
        if (lowered(name) == lowered(kButtonNames[i])) return GamepadButton(i);
    }
    return GamepadButton::Count;
}

const char* gamepadAxisName(GamepadAxis axis) {
    return size_t(axis) < size_t(GamepadAxis::Count) ? kAxisNames[size_t(axis)] : "None";
}

GamepadAxis gamepadAxisByName(const std::string& name) {
    for (size_t i = 0; i < size_t(GamepadAxis::Count); i++) {
        if (lowered(name) == lowered(kAxisNames[i])) return GamepadAxis(i);
    }
    return GamepadAxis::None;
}

// ---------------------------------------------------------------- the map of actions

InputMap InputMap::standard() {
    InputMap map;
    InputAction moveX;
    moveX.name = "moveX";
    moveX.isAxis = true;
    moveX.negative = Key::A, moveX.positive = Key::D, moveX.negativeAlt = Key::Left, moveX.positiveAlt = Key::Right;
    moveX.gamepadAxis = GamepadAxis::LeftX;
    InputAction moveZ;
    moveZ.name = "moveZ";                   // away from the viewer is -z: forward is negative
    moveZ.isAxis = true;
    moveZ.negative = Key::W, moveZ.positive = Key::S, moveZ.negativeAlt = Key::Up, moveZ.positiveAlt = Key::Down;
    moveZ.gamepadAxis = GamepadAxis::LeftY;
    moveZ.invert = true;                    // a stick pushed up reads +1
    InputAction jump;
    jump.name = "jump";
    jump.keys = {Key::Space};
    jump.buttons = {GamepadButton::A};
    InputAction fire;
    fire.name = "fire";
    fire.keys = {Key::Space};
    fire.buttons = {GamepadButton::X};
    fire.mouseButtons = {MouseButton::Left};
    InputAction pause;
    pause.name = "pause";
    pause.keys = {Key::Escape};
    pause.buttons = {GamepadButton::Start};
    moveX.joystickAxis = 0;                 // a radio's usual order: aileron, elevator, throttle, rudder
    moveZ.joystickAxis = 1;
    map.actions = {moveX, moveZ, jump, fire, pause};
    return map;
}

InputMap InputMap::flying() {
    InputMap map = standard();
    // For things that fly: a rudder, a throttle lever that stays where it is put, flaps, wheel brakes.
    InputAction yaw;
    yaw.name = "yaw";
    yaw.isAxis = true;
    yaw.negative = Key::Q, yaw.positive = Key::E;
    yaw.gamepadAxis = GamepadAxis::RightX;
    yaw.joystickAxis = 3;
    InputAction throttle;
    throttle.name = "throttle";
    throttle.isAxis = true;
    throttle.negative = Key::LeftControl, throttle.positive = Key::LeftShift;
    throttle.negativeAlt = Key::Z, throttle.positiveAlt = Key::X;
    // A throttle that says where it is: the left stick of a radio, the lever of a flight stick.
    InputAction lever;
    lever.name = "throttleLever";
    lever.isAxis = true;
    lever.joystickAxis = 2;
    InputAction flaps;
    flaps.name = "flaps";
    flaps.keys = {Key::F};
    flaps.buttons = {GamepadButton::Y};
    InputAction brake;
    brake.name = "brake";
    brake.keys = {Key::B};
    brake.buttons = {GamepadButton::B};
    for (const InputAction& action : {yaw, throttle, lever, flaps, brake}) map.actions.push_back(action);
    return map;
}

std::string InputMap::toText() const {
    auto quoted = [](const std::string& text) {
        std::string out = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\') out += '\\';
            if (c != '\n' && c != '\r') out += c;
        }
        return out + "\"";
    };
    std::string out = "molten-input 1\n";
    for (const InputAction& action : actions) {
        if (action.isAxis) {
            out += "axis " + quoted(action.name) + " negative";
            for (Key key : {action.negative, action.negativeAlt}) if (key != Key::Unknown) out += std::string(" ") + keyName(key);
            out += " positive";
            for (Key key : {action.positive, action.positiveAlt}) if (key != Key::Unknown) out += std::string(" ") + keyName(key);
            if (action.gamepadAxis != GamepadAxis::None) out += std::string(" gamepad ") + gamepadAxisName(action.gamepadAxis);
            if (action.invert) out += " inverted";
            if (action.joystickAxis >= 0) out += " joystick " + std::to_string(action.joystickAxis) + (action.joystickInvert ? " reversed" : "");
        } else {
            out += "button " + quoted(action.name) + " keys";
            for (Key key : action.keys) out += std::string(" ") + keyName(key);
            if (!action.buttons.empty()) out += " gamepad";
            for (GamepadButton button : action.buttons) out += std::string(" ") + gamepadButtonName(button);
            if (!action.mouseButtons.empty()) out += " mouse";
            for (MouseButton button : action.mouseButtons) out += std::string(" ") + kMouseNames[size_t(button)];
        }
        out += "\n";
    }
    return out;
}

InputMap InputMap::fromText(const std::string& text) {
    InputMap map;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        // Words, with the name in quotes as one.
        std::vector<std::string> words;
        for (size_t i = 0; i < line.size();) {
            if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
                i++;
            } else if (line[i] == '"') {
                std::string word;
                for (i++; i < line.size() && line[i] != '"'; i++) {
                    if (line[i] == '\\' && i + 1 < line.size()) i++;
                    word += line[i];
                }
                i++;
                words.push_back(word);
            } else {
                size_t stop = line.find_first_of(" \t\r", i);
                if (stop == std::string::npos) stop = line.size();
                words.push_back(line.substr(i, stop - i));
                i = stop;
            }
        }
        if (words.size() < 2 || (words[0] != "axis" && words[0] != "button")) continue;
        InputAction action;
        action.isAxis = words[0] == "axis";
        action.name = words[1];
        std::string group;
        for (size_t i = 2; i < words.size(); i++) {
            const std::string& word = words[i];
            if (word == "negative" || word == "positive" || word == "keys" || word == "gamepad" || word == "mouse" || word == "joystick") {
                group = word;
            } else if (word == "reversed") {
                action.joystickInvert = true;
            } else if (group == "joystick") {
                if (!word.empty() && word[0] >= '0' && word[0] <= '9') action.joystickAxis = std::atoi(word.c_str());
            } else if (word == "inverted") {
                action.invert = true;
            } else if (group == "negative") {
                (action.negative == Key::Unknown ? action.negative : action.negativeAlt) = keyByName(word);
            } else if (group == "positive") {
                (action.positive == Key::Unknown ? action.positive : action.positiveAlt) = keyByName(word);
            } else if (group == "keys") {
                if (Key key = keyByName(word); key != Key::Unknown) action.keys.push_back(key);
            } else if (group == "gamepad" && action.isAxis) {
                action.gamepadAxis = gamepadAxisByName(word);
            } else if (group == "gamepad") {
                if (GamepadButton button = gamepadButtonByName(word); button != GamepadButton::Count) action.buttons.push_back(button);
            } else if (group == "mouse") {
                for (size_t m = 0; m < size_t(MouseButton::Count); m++) {
                    if (lowered(word) == lowered(kMouseNames[m])) action.mouseButtons.push_back(MouseButton(m));
                }
            }
        }
        map.actions.push_back(std::move(action));
    }
    return map;
}

Input::Input() {
    apply(InputMap::standard());
}

void Input::apply(const InputMap& map) {
    buttonBindings_.clear();
    axisBindings_.clear();
    for (const InputAction& action : map.actions) {
        if (action.name.empty()) continue;
        if (action.isAxis) {
            axisBindings_[action.name] = AxisBinding{action.negative, action.positive, action.gamepadAxis, action.invert, action.negativeAlt, action.positiveAlt,
                                                     action.joystickAxis, action.joystickInvert};
        } else {
            buttonBindings_[action.name] = ButtonBinding{action.keys, action.buttons, action.mouseButtons};
        }
    }
}

void Input::bindButton(const std::string& action, std::initializer_list<Key> keys,
                       std::initializer_list<GamepadButton> buttons,
                       std::initializer_list<MouseButton> mouseButtons) {
    buttonBindings_[action] = ButtonBinding{keys, buttons, mouseButtons};
}

void Input::bindAxis(const std::string& action, Key negative, Key positive, GamepadAxis gamepadAxis, bool invertGamepadAxis) {
    axisBindings_[action] = AxisBinding{negative, positive, gamepadAxis, invertGamepadAxis};
}

void Input::setVirtualButton(const std::string& action, bool down) {
    VirtualButton& button = virtualButtons_[action];
    if (down && !button.down) button.pressed = true;
    button.down = down;
}

bool Input::isDown(const std::string& action) const {
    if (!isListening) return false;
    if (auto held = virtualButtons_.find(action); held != virtualButtons_.end() && held->second.down) return true;
    auto found = buttonBindings_.find(action);
    if (found == buttonBindings_.end()) return false;
    for (Key key : found->second.keys) if (isDown(key)) return true;
    for (GamepadButton button : found->second.buttons) if (isDown(button)) return true;
    for (MouseButton button : found->second.mouseButtons) if (isDown(button)) return true;
    return false;
}

bool Input::wasPressed(const std::string& action) const {
    if (!isListening) return false;
    if (auto held = virtualButtons_.find(action); held != virtualButtons_.end() && held->second.pressed) return true;
    auto found = buttonBindings_.find(action);
    if (found == buttonBindings_.end()) return false;
    for (Key key : found->second.keys) if (wasPressed(key)) return true;
    for (GamepadButton button : found->second.buttons) if (wasPressed(button)) return true;
    for (MouseButton button : found->second.mouseButtons) if (wasPressed(button)) return true;
    return false;
}

float Input::axis(const std::string& action) const {
    if (!isListening) return 0.0f;
    float onScreen = 0.0f;
    if (auto pushed = virtualAxes_.find(action); pushed != virtualAxes_.end()) onScreen = pushed->second;
    auto found = axisBindings_.find(action);
    if (found == axisBindings_.end()) return onScreen;
    const AxisBinding& binding = found->second;
    float value = 0.0f;
    if (isDown(binding.negative) || isDown(binding.negativeAlt)) value -= 1.0f;
    if (isDown(binding.positive) || isDown(binding.positiveAlt)) value += 1.0f;
    float stick = axis(binding.gamepadAxis);
    if (binding.invert) stick = -stick;
    if (std::abs(stick) > std::abs(value)) value = stick;      // whichever device is pushed harder wins
    float joystick = joystickAxis(binding.joystickAxis) * (binding.joystickInvert ? -1.0f : 1.0f);
    if (std::abs(joystick) > std::abs(value)) value = joystick;
    if (std::abs(onScreen) > std::abs(value)) value = onScreen;
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
    for (auto& button : virtualButtons_) button.second.pressed = false;
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
