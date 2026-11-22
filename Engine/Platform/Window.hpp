// Window.hpp
// The window, and the events that arrive through it.
//
// GLFW creates the window and reports keyboard, mouse and gamepad input. It knows
// nothing about Metal, so one small Objective-C++ function (MetalLayerBridge.mm) gives
// the window a CAMetalLayer: the object that owns the textures shown on screen.
#pragma once

#include "Input/Input.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <string>

struct GLFWwindow;

namespace molten {

class Window {
public:
    Window(const std::string& title, int width, int height, bool fullscreen, MTL::Device* device);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void requestClose();

    /// Handles the events that arrived since the last call and reports input to `input`.
    void pollEvents(Input& input);

    /// The next texture to draw into. It reaches the screen when the frame presents it.
    /// May be nullptr while the window is hidden or being resized: skip the frame.
    CA::MetalDrawable* nextDrawable();

    Vec2 sizeInPoints() const;
    /// Pixels per point: 2 on a Retina display.
    float contentScale() const;

    /// Waits for the display's refresh before showing each frame. On by default.
    void setVSync(bool enabled);
    void setFullscreen(bool fullscreen);
    bool isFullscreen() const { return isFullscreen_; }
    /// Hides the pointer and lets it move without limits: for first-person cameras.
    void setCursorCaptured(bool captured);
    void setTitle(const std::string& title);
    /// The program's icon while it runs, from a picture file (a square PNG). Nothing on a phone, where the application's own is used.
    void setIcon(const std::string& picturePath);

    /// The GLFW window itself: for tools, like the editor's interface, that listen to its events.
    GLFWwindow* nativeHandle() const { return window_; }

    /// Seconds since the window system started. The engine's clock.
    static double now();

    /// The pixel format of the drawable: 8 bits per channel, sRGB-encoded.
    static constexpr MTL::PixelFormat kPixelFormat = MTL::PixelFormatBGRA8Unorm_sRGB;

private:
    void pollGamepad(Input& input);

    GLFWwindow* window_ = nullptr;
    CA::MetalLayer* layer_ = nullptr;       // owned by the window's view
    Input* input_ = nullptr;                // only while pollEvents runs
    Vec2 lastMousePosition_{0, 0};
    bool hasMousePosition_ = false;
    bool isCursorCaptured_ = false;
    bool isFullscreen_ = false;
    int windowedX_ = 0, windowedY_ = 0, windowedWidth_ = 1280, windowedHeight_ = 720;
};

}  // namespace molten
