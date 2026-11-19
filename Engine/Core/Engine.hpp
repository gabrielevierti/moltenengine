// Engine.hpp
// The engine in one object: it owns the window, the renderer, the scene, the physics
// world, input, audio and assets, and it runs the loop that ties them together.
//
// Every frame does the same things in the same order:
//
//   1. read input
//   2. zero or more fixed steps: Game::fixedUpdate, then physics
//   3. Game::update, then Behavior components
//   4. remove destroyed entities, compute world matrices, move particles
//   5. Game::draw fills the 2D canvas
//   6. the renderer draws the scene, then the canvas on top
#pragma once

#include "Assets/Assets.hpp"
#include "Audio/Audio.hpp"
#include "Core/Game.hpp"
#include "Core/Time.hpp"
#include "Input/Input.hpp"
#include "Physics/Physics.hpp"
#include "Platform/Window.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace molten {

struct EngineConfig {
    std::string title = "Molten Metal Engine";
    int width = 1280;                   // of the window, in points
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    /// F1 statistics, F2 collider outlines, F3 full screen, F4 saves a screenshot.
    bool developerKeys = true;

    /// If set, there is no window and no sound: the game runs `captureFrames` frames at
    /// exactly 60 per second, the last one is saved to this PNG file, and run() returns.
    /// This is how the engine is tested on machines nobody is looking at.
    std::string capturePath;
    int captureFrames = 90;
    int captureScale = 1;               // pixels per point of the captured image: 2 gives a "Retina" picture

    /// The program's own path (argv[0]): lets a packaged game find the Shaders and Assets
    /// folders next to its executable.
    std::string executablePath;

    /// Whatever fromArguments() did not recognize: a game's own options.
    std::vector<std::string> arguments;
    bool hasArgument(const std::string& name) const;

    /// Starts from `defaults` and applies --width N, --height N, --fullscreen,
    /// --capture FILE.png, --frames N and --scale N from the command line.
    static EngineConfig fromArguments(int argc, char** argv, EngineConfig defaults);
    static EngineConfig fromArguments(int argc, char** argv);
};

class Engine {
public:
    explicit Engine(const EngineConfig& config = EngineConfig());
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Runs the game until the window closes or quit() is called. Returns the exit code for main().
    int run(Game& game);

    /// run() in three parts, for platforms where the system owns the loop and calls the
    /// program once per display refresh (iOS): begin() once, frame() each time until it
    /// returns false, end() once.
    void begin(Game& game);
    bool frame();
    int end();
    void quit() { isRunning_ = false; }
    const EngineConfig& config() const { return config_; }

    // ---- the parts. A game uses them directly.

    Scene scene;
    Physics physics;
    Input input;
    Time time;

    Renderer& renderer() { return *renderer_; }
    Assets& assets() { return *assets_; }
    Audio& audio() { return *audio_; }
    /// Lines in 3D for the next frame: engine.debug().line(a, b, color).
    DebugDraw& debug() { return renderer_->debug; }
    /// nullptr when running without a window (EngineConfig::capturePath).
    Window* window() { return window_.get(); }

    // ---- conveniences

    /// The drawable area in points: what canvas coordinates and the mouse position are measured in.
    Vec2 screenSize() const;

    /// The font Canvas::text draws with. Starts as the engine's own font.
    void setFont(std::shared_ptr<Font> font);

    /// The entity of the camera the renderer is using, if the scene has one.
    Entity activeCamera();

    /// The ray from the camera through a point of the screen: what the mouse is pointing at.
    Ray screenRay(Vec2 screenPoint);

    /// Plays a sound as if it came from a place in the world, as heard from the camera.
    SoundHandle playSoundAt(const std::shared_ptr<const AudioClip>& clip, Vec3 position, float volume = 1.0f, float pitch = 1.0f);

    /// Saves the next frame to a PNG file.
    void saveScreenshot(const std::string& path) { pendingScreenshot_ = path; }

    /// While true, physics stands still: bodies keep the place their Transform gives them.
    /// An editor sets this while a scene is being edited.
    bool isSimulationPaused = false;

    /// While true, the components a game declared with MOLTEN_COMPONENT run: start() once
    /// per component, then update() every frame and fixedUpdate() every physics step.
    /// An editor turns this off while a scene is being edited.
    bool runsComponents = true;
    /// Makes every component start() again: call it when a scene is loaded or play begins.
    void restartComponents() { startedComponents_.clear(); }

    bool showsStatistics = false;       // F1
    bool showsColliders = false;        // F2

private:
    void simulate(Game& game, double realDelta);
    void runBehaviors(bool isFixedStep, float dt);
    void runComponents(bool isFixedStep, float dt);
    void handleDeveloperKeys();
    void drawStatistics();
    void drawColliders();
    bool writeImage(const std::string& path, int width, int height);

    EngineConfig config_;
    GraphicsDevice device_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Assets> assets_;
    std::unique_ptr<Audio> audio_;
    Canvas canvas_;
    std::shared_ptr<Font> font_;
    std::string pendingScreenshot_;
    std::vector<Entity> behaviorEntities_;
    std::unordered_map<std::string, std::unordered_set<uint64_t>> startedComponents_;   // by type name: the entities whose start() ran
    Game* game_ = nullptr;              // between begin() and end()
    double previousTime_ = 0;
    int capturedFrames_ = 0;
    bool captureSaved_ = false;
    bool isRunning_ = true;
};

}  // namespace molten
