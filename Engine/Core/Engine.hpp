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
#include "Core/World.hpp"
#include "Input/Input.hpp"
#include "Physics/Physics.hpp"
#include "Platform/Window.hpp"
#include "Rendering/Renderer.hpp"
#include "Rendering/Quality.hpp"
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

class Engine : public World {
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
    /// A frame that does not look at the window's events: drawn while the system holds them
    /// (a menu of the menu bar is open, the window is being resized), so that nothing stops.
    /// Only while a frame waits in the window's events, and never inside another one.
    void frameWhileEventsAreHeld();
    void quit() { isRunning_ = false; }
    const EngineConfig& config() const { return config_; }
    /// F1 statistics and the profiler, F2 colliders, F3 full screen, F4 a screenshot: on in a development build.
    void setDeveloperKeys(bool on) { config_.developerKeys = on; }

    // ---- the parts. A game uses them directly.


    Renderer& renderer() { return *renderer_; }
    /// The project's quality: read from its Settings/Quality.settings (none: High).
    /// Used by the renderer as each view is drawn.
    void loadQuality(const std::string& settingsDirectory);
    void setQuality(const GameQuality& quality);
    std::optional<GameQuality> quality;
    Assets& assets() { return *assets_; }
    Audio& audio() { return *audio_; }
    bool hasAudio() const { return audio_ != nullptr; }        // false where there is no sound device
    /// Writes pixels (four bytes each: red, green, blue, alpha; rows from the top) as a PNG file.
    bool writeImage(const std::string& path, const std::vector<uint8_t>& pixels, int width, int height);
    /// Lines in 3D for the next frame: engine.debug().line(a, b, color).
    DebugDraw& debug() { return renderer_->debug; }
    /// nullptr when running without a window (EngineConfig::capturePath).
    Window* window() { return window_.get(); }

    // ---- what a World leaves to an engine
    void playSound(const std::string& name, float volume = 1.0f, const std::string& group = "") override;
    std::string assetPath(const std::string& name) const override;
    std::shared_ptr<Mesh> loadMesh(const std::string& name) override;
    std::shared_ptr<Material> loadMaterial(const std::string& name) override;
    void showMesh(Entity entity, const MeshData& data) override;
    uint32_t beginSound(const std::string& name, float volume, float pitch, bool loops) override;
    void adjustSound(uint32_t sound, float volume, float pitch) override;
    void panSound(uint32_t sound, float pan) override;
    bool listener(Mat4& world) override;
    int deviceHeat() const override;
    bool isSavingBattery() const override;
    void endSound(uint32_t sound) override;
    void groupSound(uint32_t sound, const std::string& group) override;
    void mixGroup(const std::string& group, float volume, bool isMuted) override;
    void duckGroup(const std::string& group, const std::string& under, float to) override;
    void muffleSound(uint32_t sound, float muffle) override;
    void setRoom(float size, float damping, float wet) override;
    std::shared_ptr<Texture> loadTexture(const std::string& name, TextureFormat format) override;
    std::shared_ptr<Font> loadFont(const std::string& name) override;
    void preloadMesh(const std::string& name, MeshData data) override;
    void preloadPicture(const std::string& name, std::vector<uint8_t> pixels, int width, int height) override;
    std::function<std::string(const std::string&)> assetPathFinder() const override;
    Engine* asEngine() override { return this; }

    // ---- conveniences

    /// The drawable area in points: what canvas coordinates and the mouse position are measured in.
    Vec2 screenSize() const;

    /// The font Canvas::text draws with. Starts as the engine's own font.
    void setFont(std::shared_ptr<Font> font);

    /// The entity of the camera the renderer is using, if the scene has one.
    Entity activeCamera();

    /// The ray from the camera through a point of the screen: what the mouse is pointing at.
    Ray screenRay(Vec2 screenPoint) override;

    /// Plays a sound as if it came from a place in the world, as heard from the camera.
    SoundHandle playSoundAt(const std::shared_ptr<const AudioClip>& clip, Vec3 position, float volume = 1.0f, float pitch = 1.0f);

    /// Saves the next frame to a PNG file.
    void saveScreenshot(const std::string& path) { pendingScreenshot_ = path; }



    bool showsStatistics = false;       // F1
    bool showsColliders = false;        // F2

private:
    void simulate(Game& game, double realDelta);
    void onFixedUpdate(float dt) override;
    void onUpdate(float dt) override;
    const RenderSettings& effectSettings() override;
    void sceneWasLoaded(const std::string& sceneText) override;
    void runBehaviors(bool isFixedStep, float dt);
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
    Game* game_ = nullptr;              // between begin() and end()
    double previousTime_ = 0;
    uint64_t frameCount_ = 0;
    int capturedFrames_ = 0;
    bool captureSaved_ = false;
    bool isRunning_ = true;
    bool isInEvents_ = false;           // a frame is in the window's events (where a menu that opens holds it)
    bool isInHeldFrame_ = false;
    bool frame(bool looksAtEvents);
};

}  // namespace molten
