#include "MarbleGame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace molten;

namespace {
const float kAcceleration = 22.0f;      // meters per second, per second
const float kMaxSpeed = 9.0f;
const float kJumpSpeed = 6.2f;
const float kAirControl = 0.35f;        // how much steering is left while in the air
const float kFallHeight = -12.0f;       // below this the marble is lost
}

// ---------------------------------------------------------------- setup

void MarbleGame::start(Engine& engine) {
    sounds_ = Sounds::make();
    showsOverview = engine.window() == nullptr;
    isDemo = isDemo || engine.config().hasArgument("--demo");

    // Actions: the game asks "is jump pressed?" and never mentions a key or a button again.
    Input& input = engine.input;
    input.bindAxis("moveX", Key::A, Key::D, GamepadAxis::LeftX);
    input.bindAxis("moveY", Key::S, Key::W, GamepadAxis::LeftY);
    input.bindAxis("lookX", Key::Left, Key::Right, GamepadAxis::RightX);
    input.bindAxis("lookY", Key::Down, Key::Up, GamepadAxis::RightY);
    input.bindButton("jump", {Key::Space}, {GamepadButton::A});
    input.bindButton("restart", {Key::R}, {GamepadButton::Start});
    input.bindButton("quit", {Key::Escape}, {GamepadButton::Back});

    // The look of the game.
    RenderSettings& settings = engine.renderer().settings;
    settings.cascades.shadowDistance = 70.0f;
    settings.fogDensity = 0.002f;               // a little haze, in the color of the horizon
    settings.fogColor = settings.sky.horizonColor;
    settings.bloomIntensity = 0.05f;

    // Physics events. They arrive at the end of a physics step.
    engine.physics.onTriggerEnter = [this, &engine](Entity trigger, Entity other) {
        if (other != level_.marble) return;
        if (engine.scene.has<Gem>(trigger)) {
            collect(engine, trigger);
        } else if (trigger == level_.goal && collected_ == level_.gemCount && !hasWon_) {
            hasWon_ = true;
            engine.audio().play(sounds_.win);
            spawnBurst(engine, engine.scene.get<Transform>(level_.goal)->position + Vec3{0, 0.5f, 0}, Vec3{0.4f, 1.0f, 0.5f});
        }
    };
    engine.physics.onContactBegin = [this, &engine](const Contact& contact) {
        if (contact.a != level_.marble && contact.b != level_.marble) return;
        if (contact.impactSpeed < 2.5f) return;
        PlayOptions options;
        options.volume = saturate(contact.impactSpeed / 12.0f);     // harder hits are louder
        options.pitch = 0.9f + 0.2f * options.volume;
        engine.audio().play(sounds_.land, options);
    };

    restart(engine);
}

void MarbleGame::restart(Engine& engine) {
    engine.scene.clear();
    engine.physics.reset();
    level_ = buildLevel(engine);

    collected_ = 0;
    hasWon_ = false;
    hasMoved_ = false;
    playTime_ = 0;
    collectPulse_ = 0;
    jumpBuffer_ = 0;
    wish_ = Vec2{0, 0};
    lastSafePosition_ = level_.start;
    cameraYaw_ = 0;
    cameraPitch_ = 0.45f;
    cameraTarget_ = level_.start;
    autopilot_.reset();

    // The rolling rumble plays all the time; update() only changes how loud it is.
    engine.audio().stop(rollSound_);
    PlayOptions rolling;
    rolling.volume = 0;
    rolling.pitch = 0.1f;
    rolling.loops = true;
    rollSound_ = engine.audio().play(sounds_.roll, rolling);
}

// ---------------------------------------------------------------- the fixed step: forces

void MarbleGame::fixedUpdate(Engine& engine, float dt) {
    Scene& scene = engine.scene;
    RigidBody* body = scene.get<RigidBody>(level_.marble);
    if (!body) return;

    // Push the marble where the stick points, relative to where the camera looks.
    // (The autopilot thinks in compass directions instead: east and north.)
    if (!hasWon_) {
        Vec3 forward{-std::sin(cameraYaw_), 0, -std::cos(cameraYaw_)};
        Vec3 right{std::cos(cameraYaw_), 0, -std::sin(cameraYaw_)};
        if (isDemo) {
            Autopilot::Command command = autopilot_.steer(engine, level_);
            wish_ = command.push;
            if (command.jump) jumpBuffer_ = 0.15f;
            forward = Vec3{0, 0, -1};
            right = Vec3{1, 0, 0};
            if (simd::length(wish_) > 0.1f) hasMoved_ = true;
        }
        Vec3 push = right * wish_.x + forward * wish_.y;
        float control = body->isGrounded ? 1.0f : kAirControl;
        body->velocity += push * (kAcceleration * control * dt);

        // A speed limit on the ground plane only, so that falling stays fast.
        Vec3 flat{body->velocity.x, 0, body->velocity.z};
        float speed = simd::length(flat);
        if (speed > kMaxSpeed) {
            flat *= kMaxSpeed / speed;
            body->velocity = Vec3{flat.x, body->velocity.y, flat.z};
        }

        jumpBuffer_ -= dt;
        if (jumpBuffer_ > 0 && body->isGrounded) {
            body->velocity.y = kJumpSpeed;
            jumpBuffer_ = 0;
            engine.audio().play(sounds_.jump);
        }
    }

    // Remember the last place where the marble stood on solid, level, unmoving ground.
    for (const Contact& contact : engine.physics.contacts()) {
        bool marbleIsB = contact.b == level_.marble;
        if (!marbleIsB && contact.a != level_.marble) continue;
        Entity ground = marbleIsB ? contact.a : contact.b;
        float up = marbleIsB ? contact.normal.y : -contact.normal.y;
        if (up > 0.98f && !scene.has<RigidBody>(ground)) lastSafePosition_ = body->position;
    }

    // Moving platforms: a kinematic body goes where its velocity takes it, so the
    // platform is steered by setting the velocity that leads to where it should be next.
    float time = float(engine.time.elapsed);
    scene.each<MovingPlatform, RigidBody>([&](Entity, MovingPlatform& platform, RigidBody& platformBody) {
        float t = 0.5f - 0.5f * std::cos((time + dt) / platform.period * 2.0f * kPi);      // 0 -> 1 -> 0, easing at the ends
        Vec3 target = lerp(platform.from, platform.to, t);
        platformBody.velocity = (target - platformBody.position) / dt;
    });
}

// ---------------------------------------------------------------- once per frame

void MarbleGame::update(Engine& engine, float dt) {
    Scene& scene = engine.scene;
    Input& input = engine.input;

    if (input.wasPressed("quit")) engine.quit();
    if (input.wasPressed("restart")) {
        restart(engine);
        return;
    }

    // Input is read here, once per frame, and remembered for the fixed steps.
    if (!isDemo) {
        wish_ = Vec2{input.axis("moveX"), input.axis("moveY")};
        float length = simd::length(wish_);
        if (length > 1.0f) wish_ /= length;             // diagonals are not faster
        if (input.wasPressed("jump")) jumpBuffer_ = 0.15f;
        if (length > 0.1f) hasMoved_ = true;
    }
    if (hasMoved_ && !hasWon_) playTime_ += dt;
    collectPulse_ = std::max(0.0f, collectPulse_ - dt * 3.0f);

    Transform* marble = scene.get<Transform>(level_.marble);
    RigidBody* body = scene.get<RigidBody>(level_.marble);
    if (!marble || !body) return;

    if (body->position.y < kFallHeight) respawn(engine);

    // The physics moves the marble but does not spin it. Turn it by the angle a ball of
    // this size rolls through when it travels this far: distance divided by radius.
    Vec3 flat{body->velocity.x, 0, body->velocity.z};
    float speed = simd::length(flat);
    if (speed > 0.05f) {
        Vec3 axis = simd::normalize(simd::cross(Vec3{0, 1, 0}, flat));
        marble->rotation = simd::normalize(axisAngle(axis, speed * dt / Level::kMarbleRadius) * marble->rotation);
    }

    // The rumble gets louder and higher with speed, and stops in the air.
    float rolling = body->isGrounded ? saturate(speed / kMaxSpeed) : 0.0f;
    engine.audio().mixer().setVolume(rollSound_, 0.16f * rolling);
    engine.audio().mixer().setPitch(rollSound_, 0.08f + 0.1f * rolling);

    // Gems turn and bob; their lights and triggers move with them.
    float time = float(engine.time.elapsed);
    scene.each<Gem, Transform>([&](Entity, Gem& gem, Transform& transform) {
        transform.position = gem.home + Vec3{0, 0.18f * std::sin(time * 2.0f + gem.phase), 0};
        transform.rotation = axisAngle(Vec3{0, 1, 0}, time * 1.4f + gem.phase);
    });

    // The goal lights up green once every gem is collected.
    bool isOpen = collected_ == level_.gemCount;
    float glow = 1.0f + 0.3f * std::sin(time * 4.0f);
    level_.goalMaterial->emissive = (isOpen ? Vec3{0.2f, 1.6f, 0.4f} : Vec3{1.2f, 0.25f, 0.1f}) * glow;
    if (Light* goalLight = scene.get<Light>(level_.goal)) {
        goalLight->color = isOpen ? Vec3{0.3f, 1.0f, 0.4f} : Vec3{1.0f, 0.3f, 0.15f};
    }

    scene.each<Lifetime>([&](Entity entity, Lifetime& lifetime) {
        lifetime.seconds -= dt;
        if (lifetime.seconds <= 0) scene.destroy(entity);      // takes effect at the end of the frame
    });

    updateCamera(engine, dt);
}

void MarbleGame::updateCamera(Engine& engine, float dt) {
    Input& input = engine.input;
    Transform* camera = engine.scene.get<Transform>(level_.camera);
    Transform* marble = engine.scene.get<Transform>(level_.marble);
    if (!camera || !marble) return;

    if (showsOverview) {
        camera->position = Vec3{-13.0f, 15.0f, 21.0f};
        camera->rotation = lookRotation(Vec3{8.5f, 0.0f, -2.5f} - camera->position);
        return;
    }

    // Orbit with the right stick, the arrow keys, or by dragging the mouse.
    cameraYaw_ -= input.axis("lookX") * 2.2f * dt;
    cameraPitch_ += input.axis("lookY") * 1.5f * dt;
    if (input.isDown(MouseButton::Left)) {
        cameraYaw_ -= input.mouseDelta().x * 0.006f;
        cameraPitch_ += input.mouseDelta().y * 0.006f;
    }
    cameraPitch_ = std::clamp(cameraPitch_, 0.1f, 1.35f);
    cameraDistance_ = std::clamp(cameraDistance_ - input.scrollDelta() * 0.8f, 4.0f, 18.0f);

    // Follow the marble a little late: `damp` closes a fixed share of the gap per second,
    // whatever the frame rate.
    cameraTarget_ = damp(cameraTarget_, marble->position + Vec3{0, 0.6f, 0}, 10.0f, dt);
    Vec3 offset{std::sin(cameraYaw_) * std::cos(cameraPitch_), std::sin(cameraPitch_), std::cos(cameraYaw_) * std::cos(cameraPitch_)};
    camera->position = cameraTarget_ + offset * cameraDistance_;
    camera->rotation = lookRotation(cameraTarget_ - camera->position);
}

// ---------------------------------------------------------------- events

void MarbleGame::collect(Engine& engine, Entity gem) {
    const Gem* data = engine.scene.get<Gem>(gem);
    const Transform* transform = engine.scene.get<Transform>(gem);
    if (!data || !transform) return;
    collected_++;
    collectPulse_ = 1.0f;
    spawnBurst(engine, transform->position, data->color);

    // Each gem rings a little higher than the one before.
    PlayOptions options;
    options.pitch = 1.0f + 0.06f * float(collected_ - 1);
    engine.audio().play(sounds_.collect, options);
    engine.scene.destroy(gem);
}

void MarbleGame::spawnBurst(Engine& engine, Vec3 position, Vec3 color) {
    Entity burst = engine.scene.create("Burst");
    Transform transform;
    transform.position = position;
    engine.scene.add(burst, transform);
    ParticleEmitter emitter;
    emitter.rate = 0;                           // nothing continuous: one burst
    emitter.spread = kPi;                       // in every direction
    emitter.speedMin = 2.0f;
    emitter.speedMax = 5.0f;
    emitter.lifetimeMin = 0.4f;
    emitter.lifetimeMax = 0.9f;
    emitter.sizeStart = 0.22f;
    emitter.colorStart = vec4(color * 4.0f, 1);     // brighter than white: the bloom makes it glow
    emitter.colorEnd = vec4(color, 0);
    emitter.burst(48);
    engine.scene.add(burst, emitter);
    engine.scene.add(burst, Lifetime{1.2f});
}

void MarbleGame::respawn(Engine& engine) {
    engine.physics.teleport(engine.scene, level_.marble, lastSafePosition_ + Vec3{0, 0.3f, 0});
    if (RigidBody* body = engine.scene.get<RigidBody>(level_.marble)) body->velocity = Vec3{0, 0, 0};
    engine.audio().play(sounds_.fall);
}

// ---------------------------------------------------------------- interface

void MarbleGame::draw(Engine& engine, Canvas& canvas) {
    Vec2 size = canvas.size();
    Vec4 white{1, 1, 1, 1};
    Vec4 panel{0.04f, 0.06f, 0.1f, 0.6f};
    char text[96];

    // Gems, top left.
    canvas.rect(Vec2{16, 16}, Vec2{132, 44}, panel);
    canvas.circle(Vec2{40, 38}, 9, rgb(0x3fd0ff), 6);       // six segments: a hexagon, close enough to a gem
    std::snprintf(text, sizeof(text), "%d / %d", collected_, level_.gemCount);
    float pulse = 22.0f + 8.0f * collectPulse_;
    canvas.text(text, Vec2{60, 38 - pulse * 0.6f}, pulse, white);

    // The clock, top right.
    int minutes = int(playTime_) / 60;
    float seconds = playTime_ - float(minutes * 60);
    std::snprintf(text, sizeof(text), "%d:%05.2f", minutes, double(seconds));
    canvas.rect(Vec2{size.x - 136, 16}, Vec2{120, 44}, panel);
    canvas.text(text, Vec2{size.x - 28, 25}, 22, white, TextAlign::Right);

    if (isDemo) {
        canvas.text("Demo: the autopilot is playing", Vec2{size.x * 0.5f, size.y - 44}, 16, Vec4{1, 1, 1, 0.85f}, TextAlign::Center);
    } else if (!hasMoved_) {
        canvas.text("W A S D or left stick: roll     Space: jump     Arrows or drag: look     R: restart",
                    Vec2{size.x * 0.5f, size.y - 44}, 16, Vec4{1, 1, 1, 0.85f}, TextAlign::Center);
    }
    if (collected_ == level_.gemCount && !hasWon_) {
        canvas.text("The goal is open", Vec2{size.x * 0.5f, 28}, 20, rgb(0x7dff9a), TextAlign::Center);
    }
    if (hasWon_) {
        Vec2 box{420, 150};
        Vec2 corner = (size - box) * 0.5f;
        canvas.rect(corner, box, Vec4{0.04f, 0.06f, 0.1f, 0.8f});
        canvas.rectOutline(corner, box, 2, rgb(0xfe8b3c));
        canvas.text("Course complete", Vec2{size.x * 0.5f, corner.y + 20}, 34, rgb(0xfe8b3c), TextAlign::Center);
        std::snprintf(text, sizeof(text), "Time  %d:%05.2f", minutes, double(seconds));
        canvas.text(text, Vec2{size.x * 0.5f, corner.y + 70}, 22, white, TextAlign::Center);
        canvas.text("Press R to play again", Vec2{size.x * 0.5f, corner.y + 108}, 16, Vec4{1, 1, 1, 0.8f}, TextAlign::Center);
    }
}
