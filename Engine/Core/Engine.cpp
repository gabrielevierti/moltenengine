#include "Water/Ocean.hpp"
#include "Core/Engine.hpp"
#include "Platform/DeviceState.hpp"

#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Game/Component.hpp"
#include "Rendering/Particles.hpp"
#include "Kit/Kit.hpp"
#include "Script/Runtime.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace molten {

namespace {

double secondsNow() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

}  // namespace

// ---------------------------------------------------------------- configuration

EngineConfig EngineConfig::fromArguments(int argc, char** argv, EngineConfig defaults) {
    EngineConfig config = std::move(defaults);
    if (argc > 0) config.executablePath = argv[0];
    for (int i = 1; i < argc; i++) {
        auto is = [&](const char* name) { return std::strcmp(argv[i], name) == 0; };
        auto hasValue = [&] { return i + 1 < argc; };
        if (is("--fullscreen")) {
            config.fullscreen = true;
        } else if (is("--width") && hasValue()) {
            config.width = std::atoi(argv[++i]);
        } else if (is("--height") && hasValue()) {
            config.height = std::atoi(argv[++i]);
        } else if (is("--capture") && hasValue()) {
            config.capturePath = argv[++i];
        } else if (is("--frames") && hasValue()) {
            config.captureFrames = std::atoi(argv[++i]);
        } else if (is("--scale") && hasValue()) {
            config.captureScale = std::atoi(argv[++i]);
        } else {
            config.arguments.push_back(argv[i]);
        }
    }
    config.width = std::max(config.width, 16);
    config.height = std::max(config.height, 16);
    config.captureFrames = std::max(config.captureFrames, 1);
    config.captureScale = std::clamp(config.captureScale, 1, 4);
    return config;
}

EngineConfig EngineConfig::fromArguments(int argc, char** argv) {
    return fromArguments(argc, argv, EngineConfig());
}

bool EngineConfig::hasArgument(const std::string& name) const {
    return std::find(arguments.begin(), arguments.end(), name) != arguments.end();
}

// ---------------------------------------------------------------- setup

Engine::Engine(const EngineConfig& config) : config_(config) {
    AutoreleaseScope pool;
    bool isCapturing = !config_.capturePath.empty();
    std::string root = findRootDirectory(config_.executablePath);
    logInfo("shaders and assets from %s", root.c_str());

    if (!isCapturing) {
        window_ = std::make_unique<Window>(config_.title, config_.width, config_.height, config_.fullscreen, device_.device());
        window_->setVSync(config_.vsync);
    }
    renderer_ = std::make_unique<Renderer>(device_, root + "/Shaders");
    assets_ = std::make_unique<Assets>(device_, root + "/Assets");
    // The engine's own icon, until a game says it has one of its own.
    if (window_) window_->setIcon(root + "/Assets/Branding/icon.png");
    renderer_->surfaceShaders().pathOf = [this](const std::string& name) { return assets_->path(name); };
    renderer_->loadPicture = [this](const std::string& name, TextureFormat format) { return assets_->texture(name, format); };
    audio_ = std::make_unique<Audio>(!isCapturing);
    setFont(assets_->font("Fonts/Default"));
    script::registerStandardNodes();        // visual scripts: the nodes, and with them the VisualScript component
    kit::registerKit();                     // the ready-made gameplay components
}

Engine::~Engine() {
    // The GPU may still be drawing the last frames; let it finish before anything is freed.
    AutoreleaseScope pool;
    renderer_->waitUntilIdle();
}

Vec2 Engine::screenSize() const {
    return window_ ? window_->sizeInPoints() : Vec2{float(config_.width), float(config_.height)};
}

void Engine::setFont(std::shared_ptr<Font> font) {
    font_ = std::move(font);
    if (font_) {
        canvas_.setFont(&font_->metrics, font_->atlas.get());
    } else {
        canvas_.setFont(nullptr, nullptr);
    }
}

// ---------------------------------------------------------------- the loop

int Engine::run(Game& game) {
    begin(game);
    while (frame()) {
    }
    return end();
}

void Engine::begin(Game& game) {
    AutoreleaseScope pool;
    game_ = &game;
    capturedFrames_ = 0;
    captureSaved_ = false;
    game.start(*this);
    previousTime_ = secondsNow();
}

bool Engine::frame() {
    if (!game_ || !isRunning_) return false;
    AutoreleaseScope pool;
    Game& game = *game_;

    if (!window_) {
        // No window: a fixed number of frames at a fixed step, so the picture is the same every time.
        if (capturedFrames_ >= config_.captureFrames) return false;
        profiler.beginFrame();
        renderer_->profiler = &profiler;
        int pixelWidth = config_.width * config_.captureScale, pixelHeight = config_.height * config_.captureScale;
        simulate(game, 1.0 / 60.0);
        if (capturedFrames_ == config_.captureFrames - 1) {
            captureSaved_ = writeImage(config_.capturePath, pixelWidth, pixelHeight);
        } else {
            renderer_->renderToImage(scene, canvas_, pixelWidth, pixelHeight, time.elapsed);
        }
        input.endFrame();
        applyPendingScene();
        profiler.setDrawStatistics(renderer_->stats.drawCalls, renderer_->stats.triangles, renderer_->stats.visibleObjects);
        profiler.endFrame();
        capturedFrames_++;
        return capturedFrames_ < config_.captureFrames;
    }

    if (window_->shouldClose()) return false;
    profiler.beginFrame();
    renderer_->profiler = &profiler;
    {
        Profiler::Scope timed(profiler, "Events");
        window_->pollEvents(input);
    }
    if (config_.developerKeys) handleDeveloperKeys();

    double now = secondsNow();
    simulate(game, now - previousTime_);
    previousTime_ = now;

    CA::MetalDrawable* drawable = nullptr;
    {
        // Waiting here means the display (or the GPU behind it) is the limit, not the CPU.
        Profiler::Scope timed(profiler, "Waiting for a drawable");
        drawable = window_->nextDrawable();
    }
    if (drawable) {
        Profiler::Scope timed(profiler, "Render");
        RenderTarget target;
        target.texture = drawable->texture();
        target.drawable = drawable;
        renderer_->render(scene, canvas_, target, time.elapsed);
    }
    profiler.setGpuTime(renderer_->lastGpuMilliseconds());
    // The device's heat, read now and then: hot, the scene is drawn smaller until it cools.
    if (frameCount_++ % 120 == 0) {
        int heat = deviceHeat();
        renderer_->heatScale = heat >= 3 ? 0.6f : heat == 2 ? 0.75f : 1.0f;
    }
    profiler.setDrawStatistics(renderer_->stats.drawCalls, renderer_->stats.triangles, renderer_->stats.visibleObjects);
    if (!pendingScreenshot_.empty()) {
        Vec2 size = screenSize() * window_->contentScale();
        writeImage(pendingScreenshot_, int(size.x), int(size.y));
        pendingScreenshot_.clear();
    }
    input.endFrame();
    applyPendingScene();                    // a component asked for another scene: now that the frame is over
    profiler.endFrame();
    return isRunning_ && !window_->shouldClose();
}

int Engine::end() {
    if (!game_) return 0;
    AutoreleaseScope pool;
    game_->shutdown(*this);
    game_ = nullptr;
    return window_ || captureSaved_ ? 0 : 1;
}

void Engine::simulate(Game& game, double realDelta) {
    // Physics and the game's fixedUpdate advance in steps of constant length.
    // What was created or moved since the last frame (by an editor, by code between frames)
    // has its place in the world before any component asks for it.
    scene.updateTransforms();
    int steps = time.beginFrame(realDelta);
    for (int i = 0; i < steps; i++) {
        {
            Profiler::Scope timed(profiler, "Fixed update");
            game.fixedUpdate(*this, time.fixedDelta);
            runBehaviors(true, time.fixedDelta);
            runComponents(true, time.fixedDelta);
        }
        if (!isSimulationPaused) {
            Profiler::Scope timed(profiler, "Physics");
            prepareMeshColliders();
            physics.step(scene, time.fixedDelta);
            dispatchPhysicsEvents();        // components hear about collisions and triggers
        }
        scene.flushDestroyed();
    }
    // Show bodies where they are between the last two steps, for smooth motion at any frame rate.
    if (!isSimulationPaused) physics.writeTransforms(scene, time.alpha);

    {
        Profiler::Scope timed(profiler, "Update");
        game.update(*this, time.delta);
        runBehaviors(false, time.delta);
        runComponents(false, time.delta);
    }
    scene.flushDestroyed();
    scene.updateTransforms();
    {
        Profiler::Scope timed(profiler, "Late update");
        runComponents(false, time.delta, true);     // lateUpdate: cameras that follow, with everything where it will be drawn
    }
    scene.updateTransforms();
    {
        Profiler::Scope timed(profiler, "Particles");
        updateParticles(scene, time.delta, renderer_->settings.wind, renderer_->settings.windGustiness, time.elapsed);
    }

    // The canvas covers the screen, or, in an editor that shows the game in a view of its
    // own, that view: the game's interface is part of the picture of the game.
    float pixelsPerPoint = window_ ? window_->contentScale() : float(config_.captureScale);
    Vec2 canvasSize = screenSize();
    int canvasView = renderer_->canvasView;
    if (canvasView >= 0 && size_t(canvasView) < renderer_->views.size() && renderer_->views[size_t(canvasView)].width > 0) {
        const Renderer::SceneView& view = renderer_->views[size_t(canvasView)];
        canvasSize = Vec2{float(view.width), float(view.height)} / pixelsPerPoint;
    }
    canvas_.begin(canvasSize, pixelsPerPoint);
    {
        Profiler::Scope timed(profiler, "Interface");       // in the editor, the editor's own windows too
        game.draw(*this, canvas_);
        drawComponents(canvas_);                // the interface components draw: score, health, messages
        game.drawOver(*this, canvas_);
    }
    if (showsColliders) drawColliders();
    if (showsStatistics) drawStatistics();
}

void Engine::runBehaviors(bool isFixedStep, float dt) {
    // A behavior may create or destroy entities, even itself. So: first note who has
    // one, then call a copy of each function, looking the component up again every time.
    behaviorEntities_.clear();
    scene.each<Behavior>([&](Entity entity, Behavior&) { behaviorEntities_.push_back(entity); });
    for (Entity entity : behaviorEntities_) {
        Behavior* behavior = scene.get<Behavior>(entity);
        if (!behavior) continue;
        std::function<void(Engine&, Entity, float)> function = isFixedStep ? behavior->fixedUpdate : behavior->update;
        if (function) function(*this, entity, dt);
    }
}

// ---------------------------------------------------------------- what a World leaves to an engine

void Engine::playSound(const std::string& name, float volume, const std::string& group) {
    if (!audio_) return;
    if (std::shared_ptr<AudioClip> clip = assets_->sound(name)) {
        PlayOptions options;
        options.volume = volume * soundVolume;
        options.group = audio_->mixer().group(group);
        audio_->play(clip, options);
    }
}

std::string Engine::assetPath(const std::string& name) const {
    return assets_->path(name);
}

bool Engine::writeImage(const std::string& path, const std::vector<uint8_t>& pixels, int width, int height) {
    if (pixels.size() < size_t(width) * size_t(height) * 4) return false;
    return stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

uint32_t Engine::beginSound(const std::string& name, float volume, float pitch, bool loops) {
    std::shared_ptr<AudioClip> clip = assets_->sound(name);
    if (!clip) return 0;
    PlayOptions options;
    options.volume = volume;
    options.pitch = pitch;
    options.loops = loops;
    return audio_->play(clip, options);
}

void Engine::adjustSound(uint32_t sound, float volume, float pitch) {
    audio_->mixer().setVolume(sound, volume);
    audio_->mixer().setPitch(sound, pitch);
}

void Engine::panSound(uint32_t sound, float pan) {
    audio_->mixer().setPan(sound, pan);
}

int Engine::deviceHeat() const {
    return int(thermalState());
}

bool Engine::isSavingBattery() const {
    return isLowPowerModeOn();
}

bool Engine::listener(Mat4& world) {
    const Transform* ears = scene.get<Transform>(activeCamera());
    if (!ears) return false;
    world = ears->world;
    return true;
}

void Engine::endSound(uint32_t sound) {
    audio_->stop(sound);
}

void Engine::showOcean(const Ocean& ocean, Vec2 origin) {
    renderer_->showOcean(ocean, origin);
}

void Engine::groupSound(uint32_t sound, const std::string& group) {
    if (audio_) audio_->mixer().setGroup(sound, audio_->mixer().group(group));
}

void Engine::mixGroup(const std::string& group, float volume, bool isMuted) {
    if (!audio_) return;
    int index = audio_->mixer().group(group);
    audio_->mixer().setGroupVolume(index, volume);
    audio_->mixer().setGroupMuted(index, isMuted);
}

void Engine::duckGroup(const std::string& group, const std::string& under, float to) {
    if (audio_) audio_->mixer().duck(audio_->mixer().group(group), under.empty() ? -1 : audio_->mixer().group(under), to);
}

void Engine::muffleSound(uint32_t sound, float muffle) {
    if (audio_) audio_->mixer().setMuffle(sound, muffle);
}

void Engine::setRoom(float size, float damping, float wet) {
    if (audio_) audio_->mixer().setReverb(size, damping, wet);
}

void Engine::showMesh(Entity entity, const MeshData& data) {
    MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
    if (!renderer || data.indices.empty()) return;
    // The first time, a mesh of the entity's own; after that the same one with its vertices moved.
    if (!renderer->mesh || !renderer->mesh->isDynamic || !renderer->mesh->update(data)) {
        auto mesh = std::make_shared<Mesh>(device_, data);
        mesh->name = renderer->mesh ? renderer->mesh->name : std::string();     // so that the scene still names the file it came from
        mesh->isDynamic = true;
        renderer->mesh = mesh;
    }
}

std::shared_ptr<Material> Engine::loadMaterial(const std::string& name) {
    return assets_->material(name);
}

std::shared_ptr<Mesh> Engine::loadMesh(const std::string& name) {
    return assets_->mesh(name);
}

std::shared_ptr<Texture> Engine::loadTexture(const std::string& name, TextureFormat format) {
    return assets_->texture(name, format);
}

void Engine::preloadMesh(const std::string& name, MeshData data) {
    assets_->preload(name, std::move(data));
}

std::shared_ptr<Font> Engine::loadFont(const std::string& name) {
    return assets_->font(name);
}

/// A scene says how it wants to be rendered: sky, fog, shadows, exposure.
void Engine::sceneWasLoaded(const std::string&) {
    renderer_->settings = sceneSettings();
}

// ---------------------------------------------------------------- conveniences

Entity Engine::activeCamera() {
    Entity chosen = renderer_->cameraOverride;
    if (scene.has<Camera>(chosen) && scene.has<Transform>(chosen)) return chosen;
    Entity found;
    scene.each<Camera, Transform>([&](Entity entity, Camera& camera, Transform&) {
        if (!found.isValid() && camera.isActive) found = entity;
    });
    return found;
}

Ray Engine::screenRay(Vec2 screenPoint) {
    Entity cameraEntity = activeCamera();
    Camera* camera = scene.get<Camera>(cameraEntity);
    Transform* transform = scene.get<Transform>(cameraEntity);
    if (!camera || !transform) return Ray{};

    // From points to normalized device coordinates, then back through the camera:
    // the same math as drawing, in reverse.
    Vec2 size = screenSize();
    Vec2 ndc{screenPoint.x / size.x * 2.0f - 1.0f, 1.0f - screenPoint.y / size.y * 2.0f};
    // The view matrix is the inverse of the camera's world matrix, so the inverse of
    // (projection * view) is world * inverse(projection).
    Mat4 inverseViewProjection = transform->world * simd::inverse(camera->projection(size.x / size.y));
    Vec4 nearPoint = inverseViewProjection * Vec4{ndc.x, ndc.y, 0, 1};
    Vec4 farPoint = inverseViewProjection * Vec4{ndc.x, ndc.y, 1, 1};
    Vec3 origin = xyz(nearPoint) / nearPoint.w;
    return Ray{origin, safeNormalize(xyz(farPoint) / farPoint.w - origin, transform->forward())};
}

SoundHandle Engine::playSoundAt(const std::shared_ptr<const AudioClip>& clip, Vec3 position, float volume, float pitch) {
    Mat4 listener = identity();
    if (Transform* transform = scene.get<Transform>(activeCamera())) listener = transform->world;
    SpatialGain gain = spatialize(listener, position);
    if (gain.volume <= 0.001f) return 0;                // too far away to hear
    PlayOptions options;
    options.volume = volume * gain.volume;
    options.pitch = pitch;
    options.pan = gain.pan;
    return audio_->play(clip, options);
}

bool Engine::writeImage(const std::string& path, int width, int height) {
    std::vector<uint8_t> pixels = renderer_->renderToImage(scene, canvas_, width, height, time.elapsed);
    if (!stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4)) {
        logError("cannot write %s", path.c_str());
        return false;
    }
    logInfo("saved %s (%d x %d)", path.c_str(), width, height);
    return true;
}

// ---------------------------------------------------------------- developer tools

void Engine::handleDeveloperKeys() {
    if (input.wasPressed(Key::F1)) {
        showsStatistics = !showsStatistics;
        profiler.isEnabled = showsStatistics;           // the overlay shows where the time goes, too
    }
    if (input.wasPressed(Key::F2)) showsColliders = !showsColliders;
    if (input.wasPressed(Key::F3)) window_->setFullscreen(!window_->isFullscreen());
    if (input.wasPressed(Key::F4)) saveScreenshot("screenshot.png");
}

void Engine::drawStatistics() {
    const RenderStats& stats = renderer_->stats;        // of the previous frame
    char text[256];
    std::snprintf(text, sizeof(text), "%.0f fps   %.1f ms\n%d draw calls   %d triangles\n%d objects drawn   %d culled\n%d lights   %zu entities",
                  double(time.framesPerSecond), double(time.unscaledDelta) * 1000.0, stats.drawCalls, stats.triangles,
                  stats.visibleObjects, stats.culledObjects, stats.lights, scene.entityCount());
    // The parts of the last frame that took longest, from the profiler.
    std::string all = text;
    if (const Profiler::Frame* last = profiler.lastFrame()) {
        char line[128];
        std::snprintf(line, sizeof(line), "\nCPU %.2f ms   GPU %.2f ms", double(last->cpuMs), double(last->gpuMs));
        all += line;
        std::vector<const Profiler::Section*> top;
        for (const Profiler::Section& section : last->sections) {
            if (section.depth <= 1) top.push_back(&section);
        }
        std::sort(top.begin(), top.end(), [](const Profiler::Section* a, const Profiler::Section* b) { return a->ms > b->ms; });
        for (size_t i = 0; i < top.size() && i < 6; i++) {
            std::snprintf(line, sizeof(line), "\n  %-24s %6.2f ms", top[i]->name.c_str(), double(top[i]->ms));
            all += line;
        }
    }
    Vec2 size = canvas_.measureText(all, 14);
    canvas_.rect(Vec2{8, 8}, size + Vec2{20, 16}, Vec4{0, 0, 0, 0.6f});
    canvas_.text(all, Vec2{18, 16}, 14, Vec4{1, 1, 1, 1});
}

void Engine::drawColliders() {
    DebugDraw& lines = renderer_->debug;
    scene.each<Collider, Transform>([&](Entity, Collider& collider, Transform& transform) {
        Vec4 color = collider.isTrigger ? Vec4{1.0f, 0.85f, 0.2f, 1} : Vec4{0.3f, 1.0f, 0.4f, 1};
        Vec3 center = transform.position + rotate(transform.rotation, collider.offset);
        switch (collider.shape) {
            case ColliderShape::Mesh:
            case ColliderShape::Convex:
                break;                  // shaped as the model is: its triangles or its hull, nothing more to outline here
            case ColliderShape::Sphere:
                lines.sphere(center, collider.radius, color);
                break;
            case ColliderShape::Box:
                lines.box(center, collider.halfExtents, transform.rotation, color);
                break;
            case ColliderShape::Capsule: {
                Vec3 axis = rotate(transform.rotation, Vec3{0, collider.halfHeight, 0});
                lines.sphere(center + axis, collider.radius, color, 16);
                lines.sphere(center - axis, collider.radius, color, 16);
                for (Vec3 side : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}}) {
                    Vec3 offset = rotate(transform.rotation, side * collider.radius);
                    lines.line(center + axis + offset, center - axis + offset, color);
                }
                break;
            }
        }
    });
}

}  // namespace molten
