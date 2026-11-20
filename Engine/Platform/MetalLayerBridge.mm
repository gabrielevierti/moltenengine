// MetalLayerBridge.mm
// The only Objective-C++ in the engine. GLFW opens a plain Cocoa window; this function
// gives its content view a CAMetalLayer, the Core Animation layer whose drawables Metal
// renders into. Everything else talks to the layer through metal-cpp.
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

extern "C" void* mmAttachMetalLayer(GLFWwindow* window, void* device, unsigned long pixelFormat) {
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = (__bridge id<MTLDevice>)device;
    layer.pixelFormat = (MTLPixelFormat)pixelFormat;
    layer.framebufferOnly = YES;                        // drawables are only rendered to, never read
    layer.contentsScale = nsWindow.backingScaleFactor;
    nsWindow.contentView.wantsLayer = YES;
    nsWindow.contentView.layer = layer;                 // the view keeps the layer alive
    return (__bridge void*)layer;
}

/// Shows a picture as the program's icon in the Dock while it runs. A program that is an
/// application bundle has its icon from the bundle; one started as a plain executable (the
/// editor from its build folder, a game played from the editor's folder) has none but this.
extern "C" void mmSetDockIcon(const char* picturePath) {
    @autoreleasepool {
        NSImage* image = [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:picturePath]];
        if (image) [NSApp setApplicationIconImage:image];
    }
}
