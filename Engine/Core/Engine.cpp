#include "Core/Engine.hpp"

#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Game/Component.hpp"
#include "Rendering/Particles.hpp"

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
    audio_ = std::make_unique<Audio>(!isCapturing);
    setFont(assets_->font("Fonts/Default"));
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
        int pixelWidth = config_.width * config_.captureScale, pixelHeight = config_.height * config_.captureScale;
        simulate(game, 1.0 / 60.0);
        if (capturedFrames_ == config_.captureFrames - 1) {
            captureSaved_ = writeImage(config_.capturePath, pixelWidth, pixelHeight);
        } else {
            renderer_->renderToImage(scene, canvas_, pixelWidth, pixelHeight, time.elapsed);
        }
        input.endFrame();
        capturedFrames_++;
        return capturedFrames_ < config_.captureFrames;
    }

    if (window_->shouldClose()) return false;
    window_->pollEvents(input);
    if (config_.developerKeys) handleDeveloperKeys();

    double now = secondsNow();
    simulate(game, now - previousTime_);
    previousTime_ = now;

    if (CA::MetalDrawable* drawable = window_->nextDrawable()) {
        RenderTarget target;
        target.texture = drawable->texture();
        target.drawable = drawable;
        renderer_->render(scene, canvas_, target, time.elapsed);
    }
    if (!pendingScreenshot_.empty()) {
        Vec2 size = screenSize() * window_->contentScale();
        writeImage(pendingScreenshot_, int(size.x), int(size.y));
        pendingScreenshot_.clear();
    }
    input.endFrame();
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
    int steps = time.beginFrame(realDelta);
    for (int i = 0; i < steps; i++) {
        game.fixedUpdate(*this, time.fixedDelta);
        runBehaviors(true, time.fixedDelta);
        runComponents(true, time.fixedDelta);
        if (!isSimulationPaused) physics.step(scene, time.fixedDelta);
        scene.flushDestroyed();
    }
    // Show bodies where they are between the last two steps, for smooth motion at any frame rate.
    if (!isSimulationPaused) physics.writeTransforms(scene, time.alpha);

    game.update(*this, time.delta);
    runBehaviors(false, time.delta);
    runComponents(false, time.delta);
    scene.flushDestroyed();
    scene.updateTransforms();
    updateParticles(scene, time.delta);

    canvas_.begin(screenSize(), window_ ? window_->contentScale() : float(config_.captureScale));
    game.draw(*this, canvas_);
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

/// Runs the code of the components a game declared (Game/Component.hpp).
void Engine::runComponents(bool isFixedStep, float dt) {
    if (!runsComponents) return;
    // By index and by name: a component's code may load a game module or declare types,
    // which moves the list.
    for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
        ComponentType type = ComponentRegistry::instance().types()[i];
        if (!type.hasCode()) continue;
        // As with behaviors: first note who has one, then look each up again, because the
        // code may create or destroy entities and components, its own included.
        behaviorEntities_.clear();
        type.collect(scene, behaviorEntities_);
        std::unordered_set<uint64_t>& started = startedComponents_[type.name];
        std::vector<Entity> entities = behaviorEntities_;
        for (Entity entity : entities) {
            void* component = type.get(scene, entity);
            if (!component) continue;
            uint64_t key = (uint64_t(entity.index) << 32) | entity.generation;
            if (started.insert(key).second && type.start) {
                type.start(component, *this, entity);
                component = type.get(scene, entity);        // start() may have moved it
                if (!component) continue;
            }
            if (isFixedStep) {
                if (type.fixedUpdate) type.fixedUpdate(component, *this, entity, dt);
            } else if (type.update) {
                type.update(component, *this, entity, dt);
            }
        }
    }
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
    if (input.wasPressed(Key::F1)) showsStatistics = !showsStatistics;
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
    Vec2 size = canvas_.measureText(text, 14);
    canvas_.rect(Vec2{8, 8}, size + Vec2{20, 16}, Vec4{0, 0, 0, 0.6f});
    canvas_.text(text, Vec2{18, 16}, 14, Vec4{1, 1, 1, 1});
}

void Engine::drawColliders() {
    DebugDraw& lines = renderer_->debug;
    scene.each<Collider, Transform>([&](Entity, Collider& collider, Transform& transform) {
        Vec4 color = collider.isTrigger ? Vec4{1.0f, 0.85f, 0.2f, 1} : Vec4{0.3f, 1.0f, 0.4f, 1};
        Vec3 center = transform.position + rotate(transform.rotation, collider.offset);
        switch (collider.shape) {
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
