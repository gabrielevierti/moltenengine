#include "FPVGame.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;
using namespace fpv;

namespace {

const int kSubsteps = 8;                    // the flight model runs at 480 steps per second

Airframe airframeNumber(int index) {
    return index == 1 ? Airframe::whoop() : Airframe::fiveInch();
}

}  // namespace

// ---------------------------------------------------------------- setting up

void FPVGame::start(Engine& engine) {
    engine.input.stickDeadZone = 0.03f;         // radios are precise around the center: keep almost all of it

    RenderSettings& look = engine.renderer().settings;
    look.cascades.shadowDistance = 130;
    look.fogDensity = 0.0022f;
    look.fogColor = look.sky.horizonColor;
    look.sky.cloudCoverage = 0.5f;
    look.sky.cloudScale = 1.1f;
    look.bloomIntensity = 0.05f;

    world_ = buildWorld(engine);
    sounds_ = Sounds::make();
    for (int i = 0; i < 4; i++) {
        PlayOptions options;
        options.volume = 0;
        options.loops = true;
        options.pan = i % 2 == 0 ? 0.3f : -0.3f;
        motorSounds_[i] = engine.audio().play(sounds_.motor, options);
    }
    PlayOptions windOptions;
    windOptions.volume = 0;
    windOptions.loops = true;
    windSound_ = engine.audio().play(sounds_.wind, windOptions);

    camera_ = engine.scene.create("Camera");
    engine.scene.add(camera_, Transform{});
    Camera lens;
    lens.nearPlane = 0.02f;
    lens.farPlane = 1200.0f;
    engine.scene.add(camera_, lens);

    const EngineConfig& config = engine.config();
    if (config.hasArgument("--acro")) flightMode = FlightMode::Acro;
    if (config.hasArgument("--chase")) view = View::Chase;
    if (config.hasArgument("--sticks")) controls.showsSticks = true;
    chooseAirframe(engine, config.hasArgument("--whoop") ? 1 : 0);
    // These skip the menu: used when a picture of the game is captured without a screen.
    if (config.hasArgument("--race")) begin(engine, Mode::Race);
    if (config.hasArgument("--free")) begin(engine, Mode::Free);
}

void FPVGame::chooseAirframe(Engine& engine, int index) {
    airframe_ = index;
    drone = Drone(airframeNumber(index));
    cameraTilt = drone.airframe.cameraTilt;
    if (engine.scene.isAlive(model_.root)) model_.destroy(engine);
    model_ = DroneModel::build(engine, drone.airframe);
    placeAtStart();
}

/// How high the drone's center is when it sits on its belly.
float FPVGame::restHeight() const {
    return 0.035f * drone.airframe.armX / 0.0795f + 0.005f;
}

/// On the launch mat, with a fresh battery, facing the first gate.
void FPVGame::placeAtStart() {
    drone.place(world_.start + Vec3{0, restHeight() + 0.02f, 0}, courseGates()[0].yaw);
    drone.chargeBattery();
    drone.isArmed = true;
    controls.reset();
    isCrashed = isWaitingToArm = lostSignal_ = false;
    isOnGround_ = true;
    crashTimer_ = restTimer_ = linkLostTimer_ = flightTime_ = 0;
    link = 1;
    chaseEye_ = drone.position + Vec3{0, 0.4f, 1.2f};
}

void FPVGame::begin(Engine& engine, Mode newMode) {
    mode = newMode;
    race = Race(int(courseGates().size()), kLaps);
    isNewBest_ = false;
    screen = Screen::Flying;
    placeAtStart();
}

void FPVGame::toggleFlightMode() {
    flightMode = flightMode == FlightMode::Angle ? FlightMode::Acro : FlightMode::Angle;
}

/// The wind at a moment and a height: steady, with gusts that come and go, and weaker
/// close to the ground, where the grass and the trees slow it down.
Vec3 FPVGame::windAt(double time, float height) const {
    static const float speeds[3] = {0.0f, 3.0f, 7.0f};
    float speed = speeds[std::clamp(windSetting, 0, 2)];
    if (speed <= 0) return Vec3{0, 0, 0};
    float t = float(time);
    float gust = windSetting == 2 ? 0.55f + 0.9f * valueNoise(Vec2{t * 0.45f, 3.0f}) : 0.85f + 0.3f * valueNoise(Vec2{t * 0.2f, 3.0f});
    float swing = (valueNoise(Vec2{t * 0.13f, 9.0f}) - 0.5f) * 1.2f;                // the direction wanders
    Vec3 direction{std::cos(0.35f + swing), 0, std::sin(0.35f + swing)};
    float lift = (valueNoise(Vec2{t * 0.7f, 21.0f}) - 0.5f) * 0.25f * speed * float(windSetting == 2);
    return (direction * (speed * gust) + Vec3{0, lift, 0}) * saturate(0.35f + height / 10.0f);
}

// ---------------------------------------------------------------- flying

void FPVGame::fixedUpdate(Engine& engine, float dt) {
    if (screen != Screen::Flying) return;
    step_++;

    // The race: three beeps and a long one, like start lights.
    if (mode == Mode::Race) {
        int before = int(std::ceil(race.countdown));
        bool wasCounting = race.phase == Race::Phase::Countdown;
        race.update(dt);
        if (wasCounting && race.phase == Race::Phase::Running) engine.audio().play(sounds_.go);
        else if (wasCounting && int(std::ceil(race.countdown)) != before) engine.audio().play(sounds_.beep);
    }
    // During the countdown the drone sits armed on its mat, propellers idling.
    StickInput input = sticks;
    if (mode == Mode::Race && race.phase == Race::Phase::Countdown) input = StickInput{};

    // A flight controller refuses to start the motors with the throttle up.
    if (isWaitingToArm && sticks.throttle < 0.08f) {
        isWaitingToArm = false;
        drone.isArmed = true;
    }

    float h = dt / float(kSubsteps);
    for (int i = 0; i < kSubsteps; i++) {
        Vec3 previous = drone.position;
        float ground = groundHeight(drone.position.x, drone.position.z);
        drone.heightAboveGround = drone.position.y - ground;
        drone.wind = windAt(engine.time.elapsed, drone.heightAboveGround);
        drone.step(input, flightMode, h);
        collide(engine, previous, h, i % 2 == 0);

        if (mode == Mode::Race && race.phase == Race::Phase::Running) {
            const Gate& gate = courseGates()[size_t(race.nextGate)];
            if (gate.isPassed(previous, drone.position) && race.pass(race.nextGate)) {
                flashTimer_ = 0.25f;
                engine.audio().play(race.nextGate == 0 ? sounds_.lap : sounds_.gate);
                if (race.phase == Race::Phase::Finished) {
                    finish(engine);
                    return;
                }
            }
        }
    }
    if (drone.isArmed && !isOnGround_) flightTime_ += dt;

    // ---- the radio link: it weakens with distance, and with anything solid in the way
    Vec3 toDrone = drone.position - world_.pilot;
    float distance = simd::length(toDrone);
    if (step_ % 10 == 0 && distance > 1) {
        auto hit = engine.physics.raycast(engine.scene, Ray{world_.pilot, toDrone / distance}, distance - drone.airframe.radius * 2);
        isBlocked_ = hit.has_value();
    }
    float quality = 1.0f - saturate((distance / drone.airframe.range - 0.45f) / 0.55f);
    if (isBlocked_) quality *= 0.6f;
    link += (quality - link) * std::min(1.0f, dt * 5.0f);
    // No signal for half a second: the receiver gives up and stops the motors. "Failsafe".
    linkLostTimer_ = link < 0.03f ? linkLostTimer_ + dt : 0.0f;
    if (linkLostTimer_ > 0.5f && !isCrashed) {
        lostSignal_ = true;
        crash(engine, 0);
    }
    if (drone.charge() <= 0 && !isCrashed && isOnGround_ && drone.speed() < 0.5f) {
        notice_ = "Battery empty. A fresh one is in.";
        noticeTimer_ = 4;
        placeAtStart();
    }

    if (isCrashed) recover(engine);
}

/// Keeps the drone out of the ground and out of solid things, and decides whether a
/// touch was a bump or a crash.
void FPVGame::collide(Engine& engine, Vec3 previous, float dt, bool testSolids) {
    const Airframe& a = drone.airframe;
    isOnGround_ = false;

    // ---- the ground. Upright, the drone sits on its belly; tumbling, it is a ball.
    float ground = groundHeight(drone.position.x, drone.position.z);
    float upright = saturate(drone.up().y);
    float clearance = molten::lerp(a.radius * 0.6f, restHeight(), upright * upright);
    if (drone.position.y < ground + clearance) {
        Vec3 normal = groundNormal(drone.position.x, drone.position.z);
        float into = -simd::dot(drone.velocity, normal);
        Vec3 along = drone.velocity + normal * into;                    // the part of the motion that slides
        float sliding = simd::length(along);
        if (drone.isArmed && (into > a.crashSpeed * 1.4f || (upright < 0.35f && drone.speed() > 1.5f) || (sliding > 11.0f && into > 0.3f))) {
            crash(engine, std::max(into, sliding * 0.4f));
        }
        drone.position.y = ground + clearance;
        float bounce = into > 1.5f ? 0.25f : 0.0f;
        drone.velocity = along * (1.0f - std::min(1.0f, (drone.isArmed ? 3.0f : 9.0f) * dt)) + normal * (std::max(into, 0.0f) * bounce);
        isOnGround_ = true;
        if (!drone.isArmed) {
            drone.angularVelocity *= 1.0f - std::min(1.0f, 10.0f * dt);
        } else if (drone.thrust < drone.airframe.mass * 9.81f * 0.6f && drone.speed() < 2.0f) {
            // Landed: sit flat. (Otherwise the flight controller would fight the ground.)
            Vec3 nose = drone.forward();
            Quat flat = axisAngle(Vec3{0, 1, 0}, std::atan2(-nose.x, -nose.z));
            drone.rotation = simd::slerp(drone.rotation, flat, std::min(1.0f, 12.0f * dt));
            drone.angularVelocity = Vec3{0, 0, 0};
            // And it stays put: a breeze does not push a drone along the grass.
            if (drone.speed() < 0.6f) drone.velocity = Vec3{0, 0, 0};
        }
    }

    // ---- everything else that is solid: gates, trees, the building
    if (!testSolids) return;
    if (engine.physics.overlapSphere(engine.scene, drone.position, a.radius * 0.75f).empty()) return;
    // Which way does the surface face? Ask a ray along the path that led here.
    Vec3 motion = drone.position - previous;
    float travelled = simd::length(motion);
    Vec3 normal = -safeNormalize(drone.velocity, Vec3{0, 1, 0});
    if (travelled > 1e-5f) {
        if (auto hit = engine.physics.raycast(engine.scene, Ray{previous, motion / travelled}, travelled + a.radius * 2)) normal = hit->normal;
    }
    float into = -simd::dot(drone.velocity, normal);
    if (into > 0) {
        if (drone.isArmed && into > a.crashSpeed) crash(engine, into);
        // Bounce off, losing most of the speed that went into the surface.
        drone.velocity = (drone.velocity + normal * (into * 1.3f)) * 0.9f;
        if (drone.isArmed) drone.angularVelocity += random_.onUnitSphere() * (into * 0.6f);
        glitch_ = std::max(glitch_, saturate(into / 6.0f) * 0.5f);
    }
    drone.position = previous + normal * 0.004f;
}

void FPVGame::crash(Engine& engine, float impact) {
    if (isCrashed) return;
    isCrashed = true;
    drone.isArmed = false;
    crashTimer_ = restTimer_ = 0;
    drone.angularVelocity += random_.onUnitSphere() * std::min(impact * 2.5f, 25.0f);       // it tumbles
    glitch_ = 1;
    controls.reset();
    if (impact > 0) {
        PlayOptions options;
        options.volume = saturate(0.4f + impact / 12.0f);
        engine.audio().play(sounds_.crash, options);
    }
}

/// After a crash the wreck tumbles until it lies still. Then it is turned back onto its
/// feet where it stopped, as "turtle mode" does on a real quad, and the race goes on.
void FPVGame::recover(Engine& engine) {
    float dt = engine.time.fixedDelta;
    crashTimer_ += dt;
    restTimer_ = isOnGround_ && drone.speed() < 0.5f ? restTimer_ + dt : 0.0f;
    if (restTimer_ < 0.8f && crashTimer_ < 6.0f) return;

    if (lostSignal_) {
        // Too far to walk: the pilot carries it back.
        notice_ = "Signal lost. Back at the start.";
        noticeTimer_ = 4;
        placeAtStart();
        return;
    }
    Vec3 nose = drone.forward();
    float heading = std::atan2(-nose.x, -nose.z);
    Vec3 where = drone.position;
    where.y = groundHeight(where.x, where.z) + restHeight();
    drone.place(where, heading);
    isCrashed = false;
    isWaitingToArm = true;
}

void FPVGame::finish(Engine& engine) {
    screen = Screen::Finished;
    engine.audio().play(sounds_.finish);
    if (bestTime <= 0 || race.time < bestTime) {
        bestTime = race.time;
        isNewBest_ = true;
    }
}

// ---------------------------------------------------------------- every frame

void FPVGame::update(Engine& engine, float dt) {
    const Input& input = engine.input;
    sticks = screen == Screen::Flying ? controls.read(engine, dt) : StickInput{};
    flashTimer_ = std::max(0.0f, flashTimer_ - dt);
    noticeTimer_ = std::max(0.0f, noticeTimer_ - dt);
    glitch_ = std::max(0.0f, glitch_ - dt * 2.5f);

    if (screen == Screen::Flying) {
        if (input.wasPressed(GamepadButton::A) || input.wasPressed(Key::R)) begin(engine, mode);
        if (input.wasPressed(GamepadButton::X) || input.wasPressed(Key::M)) toggleFlightMode();
        if (input.wasPressed(GamepadButton::Y) || input.wasPressed(Key::C)) view = view == View::Onboard ? View::Chase : View::Onboard;
        if (input.wasPressed(GamepadButton::B) || input.wasPressed(Key::Escape)) screen = Screen::Menu;
    } else if (input.wasPressed(GamepadButton::Start) || input.wasPressed(Key::Enter)) {
        begin(engine, Mode::Race);
    }

    // The gate to fly through next is green.
    for (size_t i = 0; i < world_.gateParts.size(); i++) {
        bool isNext = mode == Mode::Race && screen == Screen::Flying && int(i) == race.nextGate;
        for (Entity part : world_.gateParts[i]) {
            if (MeshRenderer* renderer = engine.scene.get<MeshRenderer>(part)) renderer->material = isNext ? world_.nextGateMaterial : world_.gateMaterial;
        }
    }

    model_.update(engine, drone, dt);
    updateCamera(engine, dt);
    updateSound(engine, dt);
}

void FPVGame::updateCamera(Engine& engine, float dt) {
    Transform* transform = engine.scene.get<Transform>(camera_);
    Camera* lens = engine.scene.get<Camera>(camera_);
    RenderSettings& look = engine.renderer().settings;
    if (!transform || !lens) return;
    Vec2 size = engine.screenSize();
    float aspect = size.x / std::max(size.y, 1.0f);
    bool onboard = screen != Screen::Menu && view == View::Onboard;

    if (screen == Screen::Menu) {
        // The menu looks at the field from a slow circle above it.
        float angle = float(engine.time.elapsed) * 0.1f + 0.6f;
        Vec3 eye{-25 + std::cos(angle) * 72, 24, -18 + std::sin(angle) * 72};
        transform->position = eye;
        transform->rotation = lookRotation(Vec3{-25, 3, -18} - eye);
        lens->fovY = radians(55);
    } else if (onboard) {
        // The camera sits at the front of the drone, tilted up: flying fast means flying
        // nose down, and the tilt is what keeps the horizon in the picture.
        float s = drone.airframe.armX / 0.0795f;
        transform->position = drone.position + rotate(drone.rotation, Vec3{0, 0.022f * s, -0.094f * s});
        transform->rotation = drone.rotation * axisAngle(Vec3{1, 0, 0}, radians(cameraTilt));
        // FPV lenses are very wide: 125 degrees from side to side, whatever the shape of the screen.
        lens->fovY = std::min(2.0f * std::atan(std::tan(radians(62.5f)) / aspect), radians(112.0f));
    } else {
        // From behind and above, following the way the drone points.
        float s = 0.45f + 0.55f * drone.airframe.armX / 0.0795f;
        Vec3 nose = drone.forward();
        Vec3 behind = -safeNormalize(Vec3{nose.x, 0, nose.z}, Vec3{0, 0, -1});
        Vec3 wanted = drone.position + behind * (1.0f * s) + Vec3{0, 0.32f * s, 0};
        chaseEye_ = damp(chaseEye_, wanted, 9.0f, dt);
        chaseEye_.y = std::max(chaseEye_.y, groundHeight(chaseEye_.x, chaseEye_.z) + 0.15f);
        transform->position = chaseEye_;
        transform->rotation = lookRotation(drone.position - chaseEye_);
        lens->fovY = radians(62);
    }

    // The onboard picture comes through a small wide lens and a radio link, and shows it.
    look.lensDistortion = onboard ? 0.38f : 0.0f;
    look.chromaticAberration = onboard ? 0.007f : 0.0f;
    look.filmGrain = onboard ? 0.022f : 0.0f;
    look.vignette = onboard ? 0.4f : 0.25f;
    float weak = saturate((0.5f - link) / 0.5f);
    look.interference = onboard ? saturate(weak * std::sqrt(weak) * 0.9f + glitch_ * 0.6f) : 0.0f;
}

void FPVGame::updateSound(Engine& engine, float dt) {
    Mixer& mixer = engine.audio().mixer();
    bool flying = screen == Screen::Flying;
    // Four motors, four voices: each a little out of tune with the others, as real ones are.
    static const float detune[4] = {1.0f, 1.017f, 0.985f, 1.008f};
    for (int i = 0; i < 4; i++) {
        float speed = drone.motors[i];
        mixer.setPitch(motorSounds_[i], std::max(speed * drone.airframe.maxRevs / 100.0f, 0.05f) * detune[i]);
        mixer.setVolume(motorSounds_[i], flying && speed > 0.01f ? 0.03f + 0.22f * std::pow(speed, 1.5f) : 0.0f);
    }
    float airspeed = simd::length(drone.velocity - drone.wind);
    mixer.setVolume(windSound_, flying ? std::min(0.5f, airspeed * airspeed / 1600.0f * 0.6f) : 0.0f);
    mixer.setPitch(windSound_, 0.7f + airspeed / 45.0f);

    // The buzzer, when a cell is below 3.5 volts.
    alarmTimer_ -= dt;
    if (flying && drone.isArmed && drone.cellVoltage() < 3.5f && alarmTimer_ <= 0) {
        engine.audio().play(sounds_.alarm);
        alarmTimer_ = 2.5f;
    }
}
