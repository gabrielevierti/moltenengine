#include "Platform/Window.hpp"

#include "Core/Log.hpp"

#define GLFW_INCLUDE_NONE               // GLFW would otherwise include the OpenGL headers
#include <GLFW/glfw3.h>

#include <cstdlib>

// Defined in MetalLayerBridge.mm.
extern "C" void* mmAttachMetalLayer(GLFWwindow* window, void* device, unsigned long pixelFormat);
extern "C" void mmSetDockIcon(const char* picturePath);

namespace molten {

namespace {

Key keyFromGLFW(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return Key(int(Key::A) + (key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return Key(int(Key::Num0) + (key - GLFW_KEY_0));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return Key(int(Key::F1) + (key - GLFW_KEY_F1));
    switch (key) {
        case GLFW_KEY_SPACE: return Key::Space;
        case GLFW_KEY_ENTER: return Key::Enter;
        case GLFW_KEY_KP_ENTER: return Key::Enter;
        case GLFW_KEY_ESCAPE: return Key::Escape;
        case GLFW_KEY_TAB: return Key::Tab;
        case GLFW_KEY_BACKSPACE: return Key::Backspace;
        case GLFW_KEY_LEFT: return Key::Left;
        case GLFW_KEY_RIGHT: return Key::Right;
        case GLFW_KEY_UP: return Key::Up;
        case GLFW_KEY_DOWN: return Key::Down;
        case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
        case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
        case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
        case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
        default: return Key::Unknown;
    }
}

}  // namespace

Window::Window(const std::string& title, int width, int height, bool fullscreen, MTL::Device* device)
    : windowedWidth_(width), windowedHeight_(height) {
    if (!glfwInit()) {
        logError("cannot start GLFW");
        std::abort();
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);       // no OpenGL context: Metal draws into the window
    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        logError("cannot create the window");
        std::abort();
    }
    layer_ = static_cast<CA::MetalLayer*>(mmAttachMetalLayer(window_, device, static_cast<unsigned long>(kPixelFormat)));

    // GLFW calls plain functions; the user pointer leads them back to this object.
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int) {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if (self->input_ && action != GLFW_REPEAT) self->input_->handleKey(keyFromGLFW(key), action == GLFW_PRESS);
    });
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int) {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if (!self->input_ || button > GLFW_MOUSE_BUTTON_MIDDLE) return;
        MouseButton mapped = button == GLFW_MOUSE_BUTTON_LEFT ? MouseButton::Left
                           : button == GLFW_MOUSE_BUTTON_RIGHT ? MouseButton::Right : MouseButton::Middle;
        self->input_->handleMouseButton(mapped, action == GLFW_PRESS);
    });
    glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double x, double y) {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        Vec2 position{float(x), float(y)};
        Vec2 delta = self->hasMousePosition_ ? position - self->lastMousePosition_ : Vec2{0, 0};
        self->lastMousePosition_ = position;
        self->hasMousePosition_ = true;
        if (self->input_) self->input_->handleMouseMove(position, delta);
    });
    glfwSetScrollCallback(window_, [](GLFWwindow* window, double, double y) {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if (self->input_) self->input_->handleScroll(float(y));
    });
    glfwSetCharCallback(window_, [](GLFWwindow* window, unsigned int codepoint) {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if (self->input_) self->input_->handleText(codepoint);
    });

    if (fullscreen) setFullscreen(true);
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::requestClose() {
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void Window::pollEvents(Input& input) {
    input_ = &input;
    glfwPollEvents();                   // runs the callbacks above
    input_ = nullptr;
    pollGamepad(input);
}

void Window::pollGamepad(Input& input) {
    GamepadState state;
    GLFWgamepadstate pad;
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, &pad)) {
        state.connected = true;
        auto button = [&](GamepadButton to, int from) { state.buttons[size_t(to)] = pad.buttons[from] == GLFW_PRESS; };
        button(GamepadButton::A, GLFW_GAMEPAD_BUTTON_A);
        button(GamepadButton::B, GLFW_GAMEPAD_BUTTON_B);
        button(GamepadButton::X, GLFW_GAMEPAD_BUTTON_X);
        button(GamepadButton::Y, GLFW_GAMEPAD_BUTTON_Y);
        button(GamepadButton::LeftShoulder, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
        button(GamepadButton::RightShoulder, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
        button(GamepadButton::Back, GLFW_GAMEPAD_BUTTON_BACK);
        button(GamepadButton::Start, GLFW_GAMEPAD_BUTTON_START);
        button(GamepadButton::LeftStick, GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
        button(GamepadButton::RightStick, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
        button(GamepadButton::DpadUp, GLFW_GAMEPAD_BUTTON_DPAD_UP);
        button(GamepadButton::DpadDown, GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
        button(GamepadButton::DpadLeft, GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
        button(GamepadButton::DpadRight, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
        state.axes[size_t(GamepadAxis::LeftX)] = pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
        state.axes[size_t(GamepadAxis::LeftY)] = -pad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];       // GLFW: down is positive
        state.axes[size_t(GamepadAxis::RightX)] = pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
        state.axes[size_t(GamepadAxis::RightY)] = -pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
        // GLFW reports triggers from -1 (released) to 1 (fully pressed).
        state.axes[size_t(GamepadAxis::LeftTrigger)] = pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] * 0.5f + 0.5f;
        state.axes[size_t(GamepadAxis::RightTrigger)] = pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] * 0.5f + 0.5f;
    }
    input.handleGamepad(state);

    // And the first joystick that is not laid out like a controller: a radio, a flight stick.
    JoystickState joystick;
    for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST && !joystick.connected; id++) {
        if (!glfwJoystickPresent(id) || glfwJoystickIsGamepad(id)) continue;
        int axisCount = 0, buttonCount = 0;
        const float* axes = glfwGetJoystickAxes(id, &axisCount);
        const unsigned char* buttons = glfwGetJoystickButtons(id, &buttonCount);
        if (!axes || axisCount <= 0) continue;
        joystick.connected = true;
        if (const char* name = glfwGetJoystickName(id)) joystick.name = name;
        joystick.axes.assign(axes, axes + axisCount);
        for (int i = 0; buttons && i < buttonCount; i++) joystick.buttons.push_back(buttons[i] == GLFW_PRESS);
    }
    input.handleJoystick(std::move(joystick));
}

CA::MetalDrawable* Window::nextDrawable() {
    // Keep the layer's textures the size of the window, in pixels.
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) return nullptr;      // minimized
    CGSize size = layer_->drawableSize();
    if (int(size.width) != width || int(size.height) != height) {
        size.width = width;
        size.height = height;
        layer_->setDrawableSize(size);
    }
    return layer_->nextDrawable();
}

Vec2 Window::sizeInPoints() const {
    int width = 0, height = 0;
    glfwGetWindowSize(window_, &width, &height);
    return Vec2{float(width), float(height)};
}

float Window::contentScale() const {
    int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
    glfwGetWindowSize(window_, &width, &height);
    glfwGetFramebufferSize(window_, &pixelWidth, &pixelHeight);
    return width > 0 ? float(pixelWidth) / float(width) : 1.0f;
}

void Window::setVSync(bool enabled) {
    layer_->setDisplaySyncEnabled(enabled);
}

void Window::setFullscreen(bool fullscreen) {
    if (fullscreen == isFullscreen_) return;
    if (fullscreen) {
        // Remember where the window was, then cover the main display at its current resolution.
        glfwGetWindowPos(window_, &windowedX_, &windowedY_);
        glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, 0);
    }
    isFullscreen_ = fullscreen;
}

void Window::setCursorCaptured(bool captured) {
    glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    hasMousePosition_ = false;          // the pointer jumps when the mode changes: do not report that as movement
}

void Window::setIcon(const std::string& picturePath) {
    mmSetDockIcon(picturePath.c_str());
}

void Window::setTitle(const std::string& title) {
    glfwSetWindowTitle(window_, title.c_str());
}

double Window::now() {
    return glfwGetTime();
}

}  // namespace molten
