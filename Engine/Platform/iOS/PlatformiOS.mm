// PlatformiOS.mm
// The engine on iPhone and iPad: what Window.cpp and Application.cpp are on a Mac.
//
// On iOS the system owns the loop. The program starts UIKit and is then called back:
// when its scene appears (make the view, start the engine), sixty or a hundred and
// twenty times per second (run one frame), when fingers touch the screen. The view is
// a plain UIView whose layer is a CAMetalLayer: the same object Metal draws into on a Mac.
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>

#include "Platform/Application.hpp"
#include "Platform/Window.hpp"

#include <memory>
#include <vector>

namespace {

/// What the UIKit side and the engine's Window share. One screen, one engine.
struct Shared {
    // Set by the program before UIKit starts.
    int argc = 0;
    char** argv = nullptr;
    molten::EngineConfig defaults;
    molten::GameFactory makeGame;

    // Set by the view.
    CAMetalLayer* layer = nil;
    molten::Vec2 sizeInPoints{0, 0};
    float scale = 1;
    std::vector<molten::Input::Touch> touches;      // every finger on the screen, and those that just lifted
    uint64_t primaryTouch = 0;                      // the finger that plays the mouse; 0 for none
    bool primaryBegan = false, primaryEnded = false;
    molten::Vec2 primaryPosition{0, 0}, primaryDelta{0, 0};

    std::unique_ptr<molten::Engine> engine;
    std::unique_ptr<molten::Game> game;
};

Shared& shared() {
    static Shared instance;
    return instance;
}

}  // namespace

// ---------------------------------------------------------------- the view

@interface MoltenView : UIView
@end

@implementation MoltenView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.opaque = YES;
        self.backgroundColor = UIColor.blackColor;
    }
    return self;
}

/// Keeps the layer's textures the size of the view, in pixels. Called when the view
/// first gets a size and whenever that changes: rotation, split view, a folding screen.
- (void)updateDrawableSize {
    UIScreen* screen = self.window.windowScene.screen;
    CGFloat scale = screen ? screen.nativeScale : self.traitCollection.displayScale;
    if (scale <= 0) scale = 1;
    self.contentScaleFactor = scale;
    CGSize size = self.bounds.size;
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    layer.contentsScale = scale;
    if (size.width > 0 && size.height > 0) layer.drawableSize = CGSizeMake(size.width * scale, size.height * scale);
    shared().sizeInPoints = molten::Vec2{float(size.width), float(size.height)};
    shared().scale = float(scale);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self updateDrawableSize];
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    [self updateDrawableSize];
}

- (void)noteTouches:(NSSet<UITouch*>*)touches began:(BOOL)began ended:(BOOL)ended {
    Shared& state = shared();
    for (UITouch* touch in touches) {
        uint64_t identity = uint64_t((__bridge void*)touch);       // UIKit keeps one object per finger for as long as it is down
        CGPoint point = [touch locationInView:self];
        molten::Vec2 position{float(point.x), float(point.y)};

        molten::Input::Touch* known = nullptr;
        for (molten::Input::Touch& candidate : state.touches) {
            if (candidate.id == identity && !candidate.hasEnded) known = &candidate;
        }
        if (!known) {
            molten::Input::Touch added;
            added.id = identity;
            added.position = position;
            added.hasBegun = true;
            state.touches.push_back(added);
            known = &state.touches.back();
        }
        known->delta += position - known->position;
        known->position = position;
        if (ended) known->hasEnded = true;

        if (began && state.primaryTouch == 0) {
            state.primaryTouch = identity;
            state.primaryBegan = true;
            state.primaryPosition = position;
            state.primaryDelta = molten::Vec2{0, 0};
        } else if (identity == state.primaryTouch) {
            state.primaryDelta += position - state.primaryPosition;
            state.primaryPosition = position;
            if (ended) state.primaryEnded = true;
        }
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self noteTouches:touches began:YES ended:NO];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self noteTouches:touches began:NO ended:NO];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self noteTouches:touches began:NO ended:YES];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self noteTouches:touches began:NO ended:YES];
}

@end

@interface MoltenViewController : UIViewController
@end

@implementation MoltenViewController

- (void)loadView {
    self.view = [[MoltenView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

/// A swipe from an edge must be made twice before the system takes it: a game uses its edges.
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
    return UIRectEdgeAll;
}

@end

// ---------------------------------------------------------------- the scene and the application

@interface MoltenSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(strong, nonatomic) UIWindow* window;
@property(strong, nonatomic) CADisplayLink* displayLink;
@end

@implementation MoltenSceneDelegate

- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    MoltenViewController* controller = [[MoltenViewController alloc] init];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    [controller.view layoutIfNeeded];

    Shared& state = shared();
    if (state.engine) return;                   // a scene reconnecting: the engine is already running
    state.layer = (CAMetalLayer*)controller.view.layer;

    // The engine, then the game, exactly as on a Mac; only who calls frame() differs.
    molten::EngineConfig config = molten::EngineConfig::fromArguments(state.argc, state.argv, state.defaults);
    config.capturePath.clear();                 // there is always a screen here
    config.width = int(state.sizeInPoints.x);
    config.height = int(state.sizeInPoints.y);
    state.engine = std::make_unique<molten::Engine>(config);
    state.game = state.makeGame ? state.makeGame(*state.engine) : nullptr;
    if (!state.game) return;
    state.engine->begin(*state.game);

    // The display calls back once per refresh: 60 times per second, or up to 120 on ProMotion screens.
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(step:)];
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)step:(CADisplayLink*)link {
    Shared& state = shared();
    if (state.engine && state.game) state.engine->frame();
}

/// In the background an application must not draw: the system would end it.
- (void)sceneWillResignActive:(UIScene*)scene {
    self.displayLink.paused = YES;
}

- (void)sceneDidBecomeActive:(UIScene*)scene {
    self.displayLink.paused = NO;
}

@end

@interface MoltenAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation MoltenAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    return YES;
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)connectingSceneSession
                                   options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* configuration = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:connectingSceneSession.role];
    configuration.delegateClass = [MoltenSceneDelegate class];
    return configuration;
}

- (void)applicationWillTerminate:(UIApplication*)application {
    Shared& state = shared();
    if (state.engine && state.game) state.engine->end();
}

@end

// ---------------------------------------------------------------- what the engine sees

namespace molten {

int runApplication(int argc, char** argv, EngineConfig defaults, GameFactory makeGame) {
    Shared& state = shared();
    state.argc = argc;
    state.argv = argv;
    state.defaults = std::move(defaults);
    state.makeGame = std::move(makeGame);
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MoltenAppDelegate class]));
    }
}

Window::Window(const std::string&, int width, int height, bool, MTL::Device* device) : windowedWidth_(width), windowedHeight_(height) {
    // The view was made before the engine; here its layer learns which GPU draws into it.
    CAMetalLayer* layer = shared().layer;
    layer.device = (__bridge id<MTLDevice>)(void*)device;
    layer.pixelFormat = (MTLPixelFormat)kPixelFormat;
    layer.framebufferOnly = YES;
    // The Objective-C object as a plain pointer first (that is the bridge), then as metal-cpp's name for it.
    layer_ = static_cast<CA::MetalLayer*>((__bridge void*)layer);
    isFullscreen_ = true;
}

Window::~Window() = default;

bool Window::shouldClose() const {
    return false;                               // an iOS application is closed by the system, not by itself
}

void Window::requestClose() {}

namespace {

/// The controller in use (the one last touched, or the first connected), as the engine reads controllers.
void pollController(Input& input) {
    GamepadState state;
    GCController* controller = GCController.current ? GCController.current : GCController.controllers.firstObject;
    GCExtendedGamepad* pad = controller.extendedGamepad;
    if (pad) {
        state.connected = true;
        auto set = [&](GamepadButton button, GCControllerButtonInput* from) { state.buttons[size_t(button)] = from != nil && from.isPressed; };
        set(GamepadButton::A, pad.buttonA);
        set(GamepadButton::B, pad.buttonB);
        set(GamepadButton::X, pad.buttonX);
        set(GamepadButton::Y, pad.buttonY);
        set(GamepadButton::LeftShoulder, pad.leftShoulder);
        set(GamepadButton::RightShoulder, pad.rightShoulder);
        set(GamepadButton::Back, pad.buttonOptions);
        set(GamepadButton::Start, pad.buttonMenu);
        set(GamepadButton::LeftStick, pad.leftThumbstickButton);
        set(GamepadButton::RightStick, pad.rightThumbstickButton);
        set(GamepadButton::DpadUp, pad.dpad.up);
        set(GamepadButton::DpadDown, pad.dpad.down);
        set(GamepadButton::DpadLeft, pad.dpad.left);
        set(GamepadButton::DpadRight, pad.dpad.right);
        state.axes[size_t(GamepadAxis::LeftX)] = pad.leftThumbstick.xAxis.value;
        state.axes[size_t(GamepadAxis::LeftY)] = pad.leftThumbstick.yAxis.value;          // up is positive, as the engine has it
        state.axes[size_t(GamepadAxis::RightX)] = pad.rightThumbstick.xAxis.value;
        state.axes[size_t(GamepadAxis::RightY)] = pad.rightThumbstick.yAxis.value;
        state.axes[size_t(GamepadAxis::LeftTrigger)] = pad.leftTrigger.value;
        state.axes[size_t(GamepadAxis::RightTrigger)] = pad.rightTrigger.value;
    }
    input.handleGamepad(state);
}

}  // namespace

void Window::pollEvents(Input& input) {
    pollController(input);
    Shared& state = shared();
    input.isTouchScreen = true;                 // the screen is how a phone is played: touch controls show from the start
    // The first finger is also the mouse.
    if (state.primaryBegan) {
        input.handleMouseMove(state.primaryPosition, Vec2{0, 0});
        input.handleMouseButton(MouseButton::Left, true);
    } else if (state.primaryTouch != 0) {
        input.handleMouseMove(state.primaryPosition, state.primaryDelta);
    }
    if (state.primaryEnded) {
        input.handleMouseButton(MouseButton::Left, false);
        state.primaryTouch = 0;
    }
    state.primaryBegan = state.primaryEnded = false;
    state.primaryDelta = Vec2{0, 0};

    input.handleTouches(state.touches);
    // For the next frame: lifted fingers are gone, and the others have not moved or just landed yet.
    std::vector<Input::Touch> remaining;
    for (Input::Touch touch : state.touches) {
        if (touch.hasEnded) continue;
        touch.hasBegun = false;
        touch.delta = Vec2{0, 0};
        remaining.push_back(touch);
    }
    state.touches = std::move(remaining);
}

CA::MetalDrawable* Window::nextDrawable() {
    if (shared().sizeInPoints.x <= 0 || shared().sizeInPoints.y <= 0) return nullptr;
    return layer_->nextDrawable();
}

Vec2 Window::sizeInPoints() const {
    return shared().sizeInPoints;
}

float Window::contentScale() const {
    return shared().scale;
}

void Window::setVSync(bool) {}                  // the display link is the display's own rhythm
void Window::setFullscreen(bool) {}
void Window::setCursorCaptured(bool) {}
void Window::setTitle(const std::string&) {}
void Window::setIcon(const std::string&) {}

double Window::now() {
    return CACurrentMediaTime();
}

}  // namespace molten
