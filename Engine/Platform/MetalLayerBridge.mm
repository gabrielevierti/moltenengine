// MetalLayerBridge.mm
// The only Objective-C++ in the engine. GLFW opens a plain Cocoa window; this function
// gives its content view a CAMetalLayer, the Core Animation layer whose drawables Metal
// renders into. Everything else talks to the layer through metal-cpp. And the few things
// only Cocoa can do: the Dock's icon, and frames while a menu is open.
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

// ---------------------------------------------------------------- frames while the events are held
//
// A menu of the menu bar, while it is open, keeps the program in a loop of its own until it
// closes (so does a window being resized with the mouse): the engine's loop waits in its events
// all that while, and the game, and everything that moves by itself, would stand still. A timer
// that fires in that loop's mode draws the frames instead.

static void (*gHeldTick)(void*) = nullptr;
static void* gHeldContext = nullptr;
static NSTimer* gHeldTimer = nil;
static int gHeldCount = 0;

@interface MMHeldFrames : NSObject
@end

@implementation MMHeldFrames
- (void)began:(NSNotification*)note {
    (void)note;
    if (gHeldCount++ > 0 || gHeldTimer) return;
    gHeldTimer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:gHeldTimer forMode:NSEventTrackingRunLoopMode];
}
- (void)ended:(NSNotification*)note {
    (void)note;
    if (gHeldCount > 0) gHeldCount--;
    if (gHeldCount > 0 || !gHeldTimer) return;
    [gHeldTimer invalidate];
    gHeldTimer = nil;
}
- (void)tick:(NSTimer*)timer {
    (void)timer;
    if (gHeldTick) gHeldTick(gHeldContext);
}
@end

extern "C" void mmKeepFramesWhileEventsAreHeld(void (*tick)(void*), void* context) {
    @autoreleasepool {
        gHeldTick = tick;
        gHeldContext = context;
        static MMHeldFrames* watcher = nil;
        if (watcher) return;
        watcher = [MMHeldFrames new];
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center addObserver:watcher selector:@selector(began:) name:NSMenuDidBeginTrackingNotification object:nil];
        [center addObserver:watcher selector:@selector(ended:) name:NSMenuDidEndTrackingNotification object:nil];
        [center addObserver:watcher selector:@selector(began:) name:NSWindowWillStartLiveResizeNotification object:nil];
        [center addObserver:watcher selector:@selector(ended:) name:NSWindowDidEndLiveResizeNotification object:nil];
    }
}
