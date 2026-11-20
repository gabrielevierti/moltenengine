#include "Kit/Kit.hpp"

#include "Kit/Vehicles.hpp"
#include "Animation/Animator.hpp"
#include "Script/Convert.hpp"
#include "Water/Liquid.hpp"
#include "Water/Water.hpp"

#include "Game/Component.hpp"
#include "Rendering/Particles.hpp"
#include "Scene/Components.hpp"
#include "Script/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace molten::kit {

namespace {

std::string nameOf(World& world, Entity entity) {
    const Name* name = world.scene.get<Name>(entity);
    return name ? name->value : std::string();
}

/// True if `wanted` is empty, or the entity is called that, or its name starts with it
/// ("Enemy" matches "Enemy", and also "Enemy 2").
bool isCalled(World& world, Entity entity, const std::string& wanted) {
    if (wanted.empty()) return true;
    if (const Tag* tag = world.scene.get<Tag>(entity); tag && tag->value == wanted) return true;
    std::string name = nameOf(world, entity);
    return name.compare(0, wanted.size(), wanted) == 0;
}

/// Where the player wants to go: the actions moveX and moveZ (W A S D, the arrows and the
/// left stick, unless the project's Input window says otherwise). x to the right, z toward the viewer.
Vec3 moveInput(const Input& input, bool alongX, bool alongZ) {
    Vec3 direction{0, 0, 0};
    if (alongX) direction.x = input.axis("moveX");
    if (alongZ) direction.z = input.axis("moveZ");
    float length = simd::length(direction);
    return length > 1 ? direction / length : direction;         // diagonals are not faster
}

void spawnEffect(World& world, const std::string& prefab, Vec3 position) {
    if (!prefab.empty()) world.spawn(prefab, position);
}

}  // namespace

// ---------------------------------------------------------------- PlayerMover

void PlayerMover::update(World& world, Entity self, float dt) {
    if (jumpSpeed > 0 && world.input.wasPressed("jump")) wantsJump = true;

    RigidBody* body = world.scene.get<RigidBody>(self);
    Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    Vec3 direction = moveInput(world.input, movesAlongX, movesAlongZ);
    // Without a body the entity is moved here, once per frame. With one, physics moves it
    // and fixedUpdate only says how fast.
    if (!body) {
        transform->position += direction * (speed * dt);
        if (limitX > 0) transform->position.x = std::clamp(transform->position.x, -limitX, limitX);
    }
    if (facesMovement && simd::length_squared(direction) > 0.01f) {
        Quat wanted = lookRotation(direction);
        transform->rotation = simd::slerp(transform->rotation, wanted, std::min(12.0f * dt, 1.0f));
    }
}

void PlayerMover::fixedUpdate(World& world, Entity self, float dt) {
    RigidBody* body = world.scene.get<RigidBody>(self);
    if (!body) return;
    Vec3 direction = moveInput(world.input, movesAlongX, movesAlongZ);
    if (body->type == BodyType::Dynamic && rolls) {
        // A ball: pushed, never steered, so it keeps its momentum and has to be braked.
        body->velocity += direction * (acceleration * dt);
        Vec3 flat{body->velocity.x, 0, body->velocity.z};
        float flatSpeed = simd::length(flat);
        if (flatSpeed > speed) {
            flat *= speed / flatSpeed;
            body->velocity.x = flat.x;
            body->velocity.z = flat.z;
        }
    } else {
        body->velocity.x = direction.x * speed;
        body->velocity.z = direction.z * speed;
        if (body->type != BodyType::Dynamic) body->velocity.y = 0;
    }
    if (limitX > 0) {
        // Stop at the limits instead of pressing against them.
        if (body->position.x <= -limitX && body->velocity.x < 0) body->velocity.x = 0;
        if (body->position.x >= limitX && body->velocity.x > 0) body->velocity.x = 0;
    }
    if (wantsJump && body->type == BodyType::Dynamic && body->isGrounded) body->velocity.y = jumpSpeed;
    wantsJump = false;
}

// ---------------------------------------------------------------- Follow, Spin, Mover, Lifetime

void Follow::lateUpdate(World& world, Entity self, float dt) {
    Transform* transform = world.scene.get<Transform>(self);
    const Transform* followed = world.scene.get<Transform>(world.find(target));
    if (!transform || !followed) return;
    Vec3 center = followed->worldPosition();
    // Each frame covers a fixed share of what is left: quick when far, gentle when near,
    // and the same at any frame rate.
    float share = smoothness > 0 ? 1.0f - std::exp(-smoothness * dt) : 1.0f;
    if (turnsWithTarget) {
        // Behind a thing that moves fast, the camera is held at its distance exactly: easing
        // its position would let it fall back by a length that changes with every frame's
        // duration, and the whole picture would shake. What is eased is the direction.
        // Around the vertical only: a car that rolls over should not take the camera under the road.
        Vec3 ahead = rotate(followed->rotation, Vec3{0, 0, -1});
        ahead.y = 0;
        if (simd::length_squared(ahead) > 1e-4f) {
            ahead = simd::normalize(ahead);
            heading = simd::length_squared(heading) < 0.5f ? ahead : safeNormalize(heading + (ahead - heading) * share, ahead);
        }
        if (simd::length_squared(heading) < 0.5f) heading = Vec3{0, 0, -1};
        Quat behind = lookRotation(heading);
        transform->position = center + rotate(behind, offset);
        if (looksAtTarget) {
            // A little above the target and level with the horizon, not swinging with every move of it.
            Vec3 toTarget = center + Vec3{0, offset.y * 0.35f, 0} - transform->position;
            if (simd::length_squared(toTarget) > 1e-4f) transform->rotation = lookRotation(toTarget);
        }
        return;
    }
    Vec3 goal = center + offset;
    transform->position += (goal - transform->position) * share;
    if (looksAtTarget) {
        Vec3 toTarget = center - transform->position;
        if (simd::length_squared(toTarget) > 1e-4f) transform->rotation = lookRotation(toTarget);
    }
}

void Buoyancy::fixedUpdate(World& world, Entity self, float dt) {
    RigidBody* body = world.scene.get<RigidBody>(self);
    if (!body) return;
    // Over a Water, the surface is where its waves are now: what floats rides them.
    float level = waterLevel;
    waterHeightAt(world, body->position, level);
    float under = saturate((level - body->position.y) / std::max(depth, 0.01f) * 0.5f + 0.5f);     // 0 out, 1 fully under
    isInWater = under > 0;
    if (!isInWater) return;
    // Up by the weight of the water it pushes aside, and slowed by moving through it.
    body->velocity.y += 9.81f * body->gravityScale * lift * under * dt;
    body->velocity *= std::max(0.0f, 1.0f - drag * under * dt);
}

void Spin::update(World& world, Entity self, float dt) {
    if (Transform* transform = world.scene.get<Transform>(self)) {
        Quat turn = axisAngle(safeNormalize(axis), radians(degreesPerSecond) * dt);
        transform->rotation = simd::normalize(turn * transform->rotation);
    }
}

void Mover::update(World& world, Entity self, float dt) {
    Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    Vec3 step;
    if (!chases.empty()) {
        const Transform* prey = world.scene.get<Transform>(world.find(chases));
        if (!prey) return;                      // nobody to chase: wait
        Vec3 toPrey = prey->worldPosition() - transform->position;
        toPrey.y = 0;
        float distance = simd::length(toPrey);
        if (distance < 0.05f) return;
        Vec3 direction = toPrey / distance;
        transform->rotation = lookRotation(direction);
        step = direction * chaseSpeed;
    } else {
        step = isLocal ? rotate(transform->rotation, velocity) : velocity;
    }
    // A body is moved by physics, so that what it runs into is noticed; anything else directly.
    if (RigidBody* body = world.scene.get<RigidBody>(self)) body->velocity = step;
    else transform->position += step * dt;
}

void Lifetime::update(World& world, Entity self, float dt) {
    age += dt;
    if (age >= seconds) world.scene.destroy(self);
}

// ---------------------------------------------------------------- Collectible, Health, Damage

void Collectible::onTriggerEnter(World& world, Entity self, Entity other) {
    if (!isCalled(world, other, collector)) return;
    world.addValue(counter, amount);
    if (!sound.empty()) world.playSound(sound);
    if (const Transform* transform = world.scene.get<Transform>(self)) spawnEffect(world, effect, transform->worldPosition());
    world.scene.destroy(self);
}

void Health::start(World& world, Entity) {
    if (!counter.empty()) world.setValue(counter, amount);
}

void Health::damage(World& world, Entity self, float damageAmount) {
    if (amount <= 0) return;                    // already dying: one death is enough
    amount -= damageAmount;
    if (!counter.empty()) world.setValue(counter, std::max(amount, 0.0f));
    if (amount > 0) return;
    if (points != 0 && !scoreCounter.empty()) world.addValue(scoreCounter, points);
    // Copies first: spawning may move this component.
    std::string deathEffect = effect;
    bool restarts = restartsSceneOnDeath;
    if (const Transform* transform = world.scene.get<Transform>(self)) spawnEffect(world, deathEffect, transform->worldPosition());
    world.scene.destroy(self);
    if (restarts) world.reloadScene();
}

void Damage::hit(World& world, Entity self, Entity other) {
    if (!isCalled(world, other, onlyTo)) return;
    Health* health = world.scene.get<Health>(other);
    if (!health) return;
    float dealt = amount;
    bool usedUp = destroysSelf;
    health->damage(world, other, dealt);
    if (usedUp) world.scene.destroy(self);
}

void Damage::onTriggerEnter(World& world, Entity self, Entity other) { hit(world, self, other); }
void Damage::onCollision(World& world, Entity self, Entity other) { hit(world, self, other); }

// ---------------------------------------------------------------- Shooter, Spawner

void Shooter::update(World& world, Entity self, float dt) {
    cooldown -= dt;
    bool isFiring = world.input.isDown(keyByName(key)) || (pointerToo && world.input.isDown(MouseButton::Left));
    if (!isFiring || cooldown > 0) return;
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    cooldown = interval;
    Vec3 position = transformPoint(transform->world, muzzle);
    Quat rotation = transform->rotation;
    std::string shotSound = sound;
    Entity shot = world.spawn(prefab, position);
    // The shot flies the way the shooter faces.
    if (Transform* shotTransform = world.scene.get<Transform>(shot)) shotTransform->rotation = rotation;
    if (!shotSound.empty()) world.playSound(shotSound, 0.6f);
}

void Spawner::update(World& world, Entity self, float dt) {
    if (limit > 0 && spawned >= limit) return;
    waited += dt;
    float due = hasBegun ? interval : firstDelay;
    if (waited < due) return;
    waited -= due;
    hasBegun = true;
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    Vec3 position = transform->worldPosition();
    if (radius > 0) {
        float angle = random.range(0.0f, 2.0f * kPi), distance = radius * std::sqrt(random.next());
        position += Vec3{std::cos(angle) * distance, 0, std::sin(angle) * distance};
    }
    spawned++;
    world.spawn(prefab, position);
}

// ---------------------------------------------------------------- HudText, Goal, RestartWhenBelow

std::string HudText::filled(const World& world, const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t open = text.find('{', i);
        size_t close = open == std::string::npos ? std::string::npos : text.find('}', open);
        if (close == std::string::npos) {
            out += text.substr(i);
            break;
        }
        out += text.substr(i, open - i);
        std::string name = text.substr(open + 1, close - open - 1);
        // "{abs:off|on}": words for a value, the first for 0, the second for 1, and so on.
        if (size_t colon = name.find(':'); colon != std::string::npos) {
            // "{lap:time}": seconds as minutes, seconds and tenths; nothing yet as dashes.
            if (name.compare(colon + 1, std::string::npos, "time") == 0) {
                float seconds = world.value(name.substr(0, colon));
                char clock[32];
                if (seconds <= 0) std::snprintf(clock, sizeof(clock), "-:--.-");
                else std::snprintf(clock, sizeof(clock), "%d:%04.1f", int(seconds) / 60, double(std::fmod(seconds, 60.0f)));
                out += clock;
                i = close + 1;
                continue;
            }
            int which = std::max(0, int(std::lround(world.value(name.substr(0, colon)))));
            std::string words = name.substr(colon + 1), word;
            size_t from = 0;
            for (int k = 0;; k++) {
                size_t bar = words.find('|', from);
                word = words.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
                if (k == which || bar == std::string::npos) break;
                from = bar + 1;
            }
            out += word;
            i = close + 1;
            continue;
        }
        float value = world.value(name);
        char number[32];
        // Whole numbers without a decimal point: "Coins: 3", not "Coins: 3.0".
        if (std::abs(value - std::round(value)) < 1e-4f) std::snprintf(number, sizeof(number), "%d", int(std::lround(value)));
        else std::snprintf(number, sizeof(number), "%.1f", double(value));
        out += number;
        i = close + 1;
    }
    return out;
}

void HudText::draw(World& world, Entity, Canvas& canvas) {
    Vec2 size_ = canvas.size();
    Vec2 position{size_.x * anchor.x + offset.x, size_.y * anchor.y + offset.y};
    TextAlign align = anchor.x < 0.34f ? TextAlign::Left : anchor.x > 0.66f ? TextAlign::Right : TextAlign::Center;
    std::string shown = filled(world, text);
    // A dark copy one point down and right keeps the text readable over a bright scene.
    canvas.text(shown, Vec2{position.x + 1.5f, position.y + 1.5f}, size, Vec4{0, 0, 0, 0.55f * color.w}, align);
    canvas.text(shown, position, size, color, align);
}

void Goal::update(World& world, Entity, float dt) {
    if (sinceReached < 0) {
        if (world.value(counter) < target) return;
        sinceReached = 0;
        world.setValue("won", 1);
    }
    sinceReached += dt;
    if (!nextScene.empty() && sinceReached >= delay) {
        std::string next = nextScene;
        sinceReached = -1e9f;                   // asked once
        world.loadScene(next);
    }
}

void Goal::draw(World&, Entity, Canvas& canvas) {
    if (sinceReached < 0 || message.empty()) return;
    Vec2 size = canvas.size();
    float appear = saturate(sinceReached / 0.3f);
    float textSize = std::clamp(size.y * 0.09f, 28.0f, 84.0f);
    canvas.rect(Vec2{0, size.y * 0.5f - textSize}, Vec2{size.x, textSize * 2}, Vec4{0, 0, 0, 0.45f * appear});
    canvas.text(message, Vec2{size.x * 0.5f, size.y * 0.5f - textSize * 0.55f}, textSize, Vec4{1, 1, 1, appear}, TextAlign::Center);
}

void RestartWhenBelow::update(World& world, Entity self, float) {
    const Transform* transform = world.scene.get<Transform>(self);
    if (transform && transform->worldPosition().y < height) world.reloadScene();
}

// ---------------------------------------------------------------- ConstantSpeed, Breakable

void ConstantSpeed::fixedUpdate(World& world, Entity self, float) {
    RigidBody* body = world.scene.get<RigidBody>(self);
    if (!body) return;
    if (!hasStarted) {
        hasStarted = true;
        body->velocity = safeNormalize(startDirection, Vec3{0, 0, -1}) * speed;
        return;
    }
    Vec3 velocity = body->velocity;
    velocity.y = 0;                             // the game is played on a table
    float current = simd::length(velocity);
    if (current < 1e-3f) velocity = safeNormalize(startDirection, Vec3{0, 0, -1});
    else velocity /= current;
    // A ball going almost exactly sideways would take forever to come back: tilt it.
    if (std::abs(velocity.z) < minimumAlongZ) {
        velocity.z = velocity.z < 0 ? -minimumAlongZ : minimumAlongZ;
        velocity = safeNormalize(velocity, Vec3{0, 0, -1});
    }
    body->velocity = velocity * speed;
}

void Breakable::onCollision(World& world, Entity self, Entity other) {
    if (!by.empty() && !isCalled(world, other, by)) return;
    if (!counter.empty()) world.addValue(counter, points);
    if (!sound.empty()) world.playSound(sound);
    if (const Transform* transform = world.scene.get<Transform>(self)) spawnEffect(world, effect, transform->worldPosition());
    world.scene.destroy(self);
}

// ---------------------------------------------------------------- Explosion, Flicker

void Explosion::start(World& world, Entity self) {
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    Vec3 center = transform->worldPosition();
    float blastRadius = radius, blastForce = force, blastDamage = damage;
    for (Entity entity : world.physics.overlapSphere(world.scene, center, blastRadius)) {
        if (entity == self) continue;
        const Transform* other = world.scene.get<Transform>(entity);
        if (!other) continue;
        Vec3 away = other->worldPosition() - center;
        float distance = simd::length(away);
        float strength = 1.0f - saturate(distance / blastRadius);       // full at the center, nothing at the edge
        if (RigidBody* body = world.scene.get<RigidBody>(entity)) {
            if (body->type == BodyType::Dynamic) {
                // Outward and a little up, so that things on the ground are thrown rather than slid.
                Vec3 direction = safeNormalize(away + Vec3{0, 0.6f * std::max(distance, 0.5f), 0}, Vec3{0, 1, 0});
                body->velocity += direction * (blastForce * strength);
            }
        }
        if (blastDamage > 0) {
            if (Health* health = world.scene.get<Health>(entity)) health->damage(world, entity, blastDamage * strength);
        }
    }
}

void Explosion::update(World& world, Entity self, float dt) {
    // The flash: the entity's light, bright at first, gone in a moment.
    Light* light = world.scene.get<Light>(self);
    if (!light) return;
    if (lightIntensity < 0) lightIntensity = light->intensity;
    age += dt;
    float left = flashSeconds > 0 ? 1.0f - saturate(age / flashSeconds) : 0.0f;
    light->intensity = lightIntensity * left * left;
}

void Flicker::update(World& world, Entity self, float dt) {
    Light* light = world.scene.get<Light>(self);
    if (!light) return;
    if (baseIntensity < 0) {
        baseIntensity = light->intensity;
        phase = float(self.index) * 1.7f;       // two flames side by side do not waver together
    }
    phase += dt * speed;
    // Three waves that never line up look like chance.
    float wave = 0.5f * std::sin(phase) + 0.3f * std::sin(phase * 2.3f + 1.3f) + 0.2f * std::sin(phase * 4.1f + 0.4f);
    light->intensity = baseIntensity * std::max(0.0f, 1.0f + amount * wave);
}

// ---------------------------------------------------------------- AudioSource

void AudioSource::start(World& world, Entity) {
    if (playsOnStart) play(world);
}

void AudioSource::play(World& world) {
    stop(world);
    if (clip.empty()) return;
    sound = world.startSound(clip, volume, pitch, loops);
    shownVolume = volume;
    shownPitch = pitch;
}

void AudioSource::stop(World& world) {
    world.stopSound(sound);
    sound = 0;
}

void AudioSource::update(World& world, Entity, float) {
    // Volume and pitch can be changed while it plays, by the inspector or by code.
    if (sound != 0 && (volume != shownVolume || pitch != shownPitch)) {
        world.changeSound(sound, volume, pitch);
        shownVolume = volume;
        shownPitch = pitch;
    }
}

void registerKit() {
    (void)engineTorque("i4", 1000);
    (void)script::typeNameFor("kit");
    (void)&sharedGltfModel;                 // and the Animator       // and what scripts made from visual scripts are written with         // names something in Vehicles.cpp, so that a program that links the kit gets the vehicles too
    // Where each is found under Add Component.
    ComponentRegistry& registry = ComponentRegistry::instance();
    for (const char* name : {"PlayerMover", "Follow", "Spin", "Mover", "Lifetime", "Collectible", "Health", "Damage", "Shooter", "Spawner", "Goal",
                             "RestartWhenBelow", "ConstantSpeed", "Breakable"}) {
        registry.setCategory(name, "Gameplay");
    }
    registry.setCategory("Buoyancy", "Physics");
    registry.setCategory("Joint", "Physics");
    registry.setCategory("Animator", "Animation");
    registry.setCategory("Water", "Environment");
    registry.setCategory("Liquid", "Effects");
    (void)sizeof(Liquid);
    registry.setCategory("Explosion", "Effects");
    registry.setCategory("Flicker", "Effects");
    registry.setCategory("HudText", "UI");
    registry.setCategory("TouchStick", "UI");
    for (const char* name : {"UIPanel", "UIImage", "UIButton", "UIBar"}) registry.setCategory(name, "UI");
    registry.setCategory("TouchButton", "UI");
    registry.setCategory("AudioSource", "Audio");
    for (const char* name : {"VehicleBody", "Bumper", "Wheel", "Suspension", "DeformableTire", "Motor", "Wing", "Hinge", "Propeller", "DriveControls", "FlightControls"}) {
        registry.setCategory(name, "Vehicles");
    }
    registry.setCategory("VisualScript", "Scripts");
}


}  // namespace molten::kit

namespace molten::kit {

MOLTEN_COMPONENT(PlayerMover, MOLTEN_FIELD(speed).range(0, 30), MOLTEN_FIELD(jumpSpeed).range(0, 20), MOLTEN_FIELD(rolls),
                 MOLTEN_FIELD(acceleration).range(0, 80), MOLTEN_FIELD(movesAlongX), MOLTEN_FIELD(movesAlongZ), MOLTEN_FIELD(limitX),
                 MOLTEN_FIELD(facesMovement))
MOLTEN_COMPONENT(Buoyancy, MOLTEN_FIELD(waterLevel), MOLTEN_FIELD(lift).range(0, 6).help("Times its weight when fully under: above 1 it floats"), MOLTEN_FIELD(depth).range(0.05f, 10),
                 MOLTEN_FIELD(drag).range(0, 10))
MOLTEN_COMPONENT(Follow, MOLTEN_FIELD(target).help("The name of the entity to follow"), MOLTEN_FIELD(offset), MOLTEN_FIELD(smoothness).range(0, 20),
                 MOLTEN_FIELD(looksAtTarget), MOLTEN_FIELD(turnsWithTarget))
MOLTEN_COMPONENT(Spin, MOLTEN_FIELD(axis), MOLTEN_FIELD(degreesPerSecond))
MOLTEN_COMPONENT(Mover, MOLTEN_FIELD(velocity), MOLTEN_FIELD(isLocal), MOLTEN_FIELD(chases).help("If set: move toward the entity with this name"),
                 MOLTEN_FIELD(chaseSpeed))
MOLTEN_COMPONENT(Lifetime, MOLTEN_FIELD(seconds))
MOLTEN_COMPONENT(Collectible, MOLTEN_FIELD(collector).help("Who can pick it up, by name; empty for anyone"),
                 MOLTEN_FIELD(counter).help("The shared value that goes up"), MOLTEN_FIELD(amount), MOLTEN_FIELD(sound), MOLTEN_FIELD(effect))
MOLTEN_COMPONENT(Health, MOLTEN_FIELD(amount), MOLTEN_FIELD(counter).help("A shared value that shows it, for the screen"), MOLTEN_FIELD(scoreCounter),
                 MOLTEN_FIELD(points), MOLTEN_FIELD(effect).help("A prefab spawned where it died"), MOLTEN_FIELD(restartsSceneOnDeath))
MOLTEN_COMPONENT(Damage, MOLTEN_FIELD(amount), MOLTEN_FIELD(onlyTo).help("If set: only entities whose name starts with this"), MOLTEN_FIELD(destroysSelf))
MOLTEN_COMPONENT(Shooter, MOLTEN_FIELD(prefab), MOLTEN_FIELD(key).choices({"Space", "Enter", "F", "E", "Q", "X", "Z", "LeftShift", "LeftControl"}), MOLTEN_FIELD(pointerToo), MOLTEN_FIELD(interval).range(0.02f, 3), MOLTEN_FIELD(muzzle),
                 MOLTEN_FIELD(sound))
MOLTEN_COMPONENT(Spawner, MOLTEN_FIELD(prefab), MOLTEN_FIELD(interval), MOLTEN_FIELD(radius), MOLTEN_FIELD(limit), MOLTEN_FIELD(firstDelay))
// ---------------------------------------------------------------- touch controls

namespace {

/// The fingers on the screen. A mouse with its button down is one: that is how they are tried on a Mac.
struct Finger {
    uint64_t id;
    Vec2 position;
    bool hasBegun;
};
std::vector<Finger> fingersOn(const Input& input) {
    std::vector<Finger> fingers;
    for (const Input::Touch& touch : input.touches()) {
        if (!touch.hasEnded) fingers.push_back(Finger{touch.id, touch.position, touch.hasBegun});
    }
    if (fingers.empty() && input.isDown(MouseButton::Left)) {
        fingers.push_back(Finger{~0ull, input.mousePosition(), input.wasPressed(MouseButton::Left)});
    }
    return fingers;
}

Vec2 gScreen{1280, 720};        // the canvas's size the last time something was drawn on it

}  // namespace

void TouchStick::update(World& world, Entity, float) {
    Input& input = world.input;
    std::vector<Finger> fingers = fingersOn(input);
    Vec2 base = placeOnScreen(gScreen, anchor, offset);
    if (isHeld) {
        // Follow the finger that took it; let go when that finger is gone.
        const Finger* mine = nullptr;
        for (const Finger& candidate : fingers) {
            if (candidate.id == finger) mine = &candidate;
        }
        if (!mine) isHeld = false;
        else knob = mine->position;
    }
    if (!isHeld) {
        // A finger that lands on it, or close around it, takes it.
        for (const Finger& candidate : fingers) {
            if (!candidate.hasBegun || simd::length(candidate.position - base) > radius * 1.6f) continue;
            isHeld = true;
            finger = candidate.id;
            knob = candidate.position;
            break;
        }
    }
    Vec2 push{0, 0};
    if (isHeld) {
        push = (knob - base) / std::max(radius, 1.0f);
        float length = simd::length(push);
        if (length > 1.0f) push = push / length;        // dragged past its edge: full, never more
    }
    if (!horizontal.empty()) input.setVirtualAxis(horizontal, push.x);
    if (!vertical.empty()) input.setVirtualAxis(vertical, verticalIsReversed ? -push.y : push.y);
}

void TouchStick::draw(World& world, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    if (onlyOnTouchScreens && !world.input.hasTouched()) return;
    Vec2 base = placeOnScreen(gScreen, anchor, offset);
    Vec2 thumb = isHeld ? knob : base;
    if (isHeld && simd::length(thumb - base) > radius) thumb = base + simd::normalize(thumb - base) * radius;
    canvas.circle(base, radius, color * Vec4{1, 1, 1, 0.25f});
    canvas.ring(base, radius, 2.0f, color);
    canvas.circle(thumb, radius * 0.45f, color * Vec4{1, 1, 1, isHeld ? 1.4f : 0.9f});
}

void TouchButton::update(World& world, Entity, float) {
    Vec2 middle = place(gScreen);
    bool pressed = false;
    for (const Finger& finger : fingersOn(world.input)) {
        if (simd::length(finger.position - middle) <= radius * 1.25f) pressed = true;      // a little generous: thumbs are not pointers
    }
    isHeld = pressed;
    if (!action.empty()) world.input.setVirtualButton(action, pressed);
}

void TouchButton::draw(World& world, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    if (onlyOnTouchScreens && !world.input.hasTouched()) return;
    Vec2 middle = place(gScreen);
    canvas.circle(middle, radius, color * Vec4{1, 1, 1, isHeld ? 0.9f : 0.3f});
    canvas.ring(middle, radius, 2.0f, color);
    if (!label.empty()) {
        float size = radius * 0.7f;
        canvas.text(label, Vec2{middle.x, middle.y - size * 0.6f}, size, Vec4{color.x, color.y, color.z, 0.95f}, TextAlign::Center);
    }
}

// ---------------------------------------------------------------- panels, pictures, buttons, bars

void UIPanel::draw(World&, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    canvas.rect(corner, size, color);
    if (border > 0) canvas.rectOutline(corner, size, border, borderColor);
}

void UIImage::draw(World& world, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    if (loadedName != picture) {
        loadedName = picture;
        loaded = picture.empty() ? nullptr : world.loadTexture(picture, TextureFormat::RGBA8_sRGB);
    }
    if (loaded) {
        canvas.image(loaded.get(), corner, size, tint);
    } else {
        // No picture yet (or none that could be read): where it will be, so that it can be placed.
        canvas.rect(corner, size, Vec4{1, 1, 1, 0.12f});
        canvas.rectOutline(corner, size, 1.5f, Vec4{1, 1, 1, 0.5f});
    }
}

void UIButton::update(World& world, Entity, float) {
    Vec2 middle = placeOnScreen(gScreen, anchor, offset);
    auto isOn = [&](Vec2 point) { return std::abs(point.x - middle.x) <= size.x * 0.5f && std::abs(point.y - middle.y) <= size.y * 0.5f; };
    bool pressed = false;
    for (const Finger& finger : fingersOn(world.input)) pressed = pressed || isOn(finger.position);
    isHovered = isOn(world.input.mousePosition());
    // Clicked: let go while still on it, as buttons are everywhere.
    bool letGoOnIt = false;
    for (const Input::Touch& touch : world.input.touches()) letGoOnIt = letGoOnIt || (touch.hasEnded && isOn(touch.position));
    if (world.input.touches().empty() && world.input.wasReleased(MouseButton::Left) && isHovered) letGoOnIt = true;
    wasClicked = isHeld && !pressed && letGoOnIt;
    isHeld = pressed;
    if (!action.empty()) world.input.setVirtualButton(action, pressed);
    if (wasClicked) {
        if (!value.empty()) world.setValue(value, world.value(value) + amount);
        if (!scene.empty()) world.loadScene(scene);
    }
}

void UIButton::draw(World&, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    Vec4 shade = isHeld ? Vec4{0.75f, 0.75f, 0.75f, 1} : isHovered ? Vec4{1.12f, 1.12f, 1.12f, 1} : Vec4{1, 1, 1, 1};
    canvas.rect(corner, size, color * shade);
    if (!label.empty()) {
        float height = std::min(size.y * 0.5f, 28.0f);
        canvas.text(label, Vec2{corner.x + size.x * 0.5f, corner.y + (size.y - height * 1.2f) * 0.5f}, height, textColor, TextAlign::Center);
    }
}

void UIBar::draw(World& world, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    float share = most > 0 ? saturate(world.value(value) / most) : 0.0f;
    // While nothing has set the value (the editor, before the game runs) it is shown full, to be seen.
    if (!world.hasValue(value)) share = 1.0f;
    canvas.rect(corner, size, backColor);
    canvas.rect(corner + Vec2{2, 2}, Vec2{std::max(size.x - 4, 0.0f) * share, std::max(size.y - 4, 0.0f)}, color);
}

MOLTEN_COMPONENT(UIPanel, MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(border).range(0, 20),
                 MOLTEN_FIELD(borderColor).color())
MOLTEN_COMPONENT(UIImage, MOLTEN_FIELD(picture).help("A picture of Assets: Textures/logo.png"), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size),
                 MOLTEN_FIELD(tint).color())
MOLTEN_COMPONENT(UIButton, MOLTEN_FIELD(label), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(textColor).color(),
                 MOLTEN_FIELD(action).help("A button action it holds while pressed: jump, fire"), MOLTEN_FIELD(scene).help("A scene it loads when clicked: Scenes/Level1.scene"),
                 MOLTEN_FIELD(value).help("A shared value it adds Amount to when clicked"), MOLTEN_FIELD(amount))
MOLTEN_COMPONENT(UIBar, MOLTEN_FIELD(value).help("The shared value it shows: health"), MOLTEN_FIELD(most), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size),
                 MOLTEN_FIELD(color).color(), MOLTEN_FIELD(backColor).color())
MOLTEN_COMPONENT(TouchStick, MOLTEN_FIELD(horizontal).help("The axis action it pushes left and right"), MOLTEN_FIELD(vertical).help("The axis action it pushes up and down"),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(radius).range(20, 200), MOLTEN_FIELD(verticalIsReversed),
                 MOLTEN_FIELD(onlyOnTouchScreens).help("Hidden until the screen is touched: for a game that also has keys"), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(TouchButton, MOLTEN_FIELD(action).help("The button action it holds: jump, fire..."), MOLTEN_FIELD(label), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset),
                 MOLTEN_FIELD(radius).range(12, 150), MOLTEN_FIELD(onlyOnTouchScreens).help("Hidden until the screen is touched: for a game that also has keys"),
                 MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(HudText, MOLTEN_FIELD(text).help("{name} shows a shared value: Coins: {score}"), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset),
                 MOLTEN_FIELD(size).range(8, 120), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(Goal, MOLTEN_FIELD(counter), MOLTEN_FIELD(target), MOLTEN_FIELD(message), MOLTEN_FIELD(nextScene), MOLTEN_FIELD(delay))
MOLTEN_COMPONENT(RestartWhenBelow, MOLTEN_FIELD(height))
MOLTEN_COMPONENT(ConstantSpeed, MOLTEN_FIELD(speed), MOLTEN_FIELD(startDirection), MOLTEN_FIELD(minimumAlongZ).range(0, 0.9f))
MOLTEN_COMPONENT(Breakable, MOLTEN_FIELD(by), MOLTEN_FIELD(counter), MOLTEN_FIELD(points), MOLTEN_FIELD(effect), MOLTEN_FIELD(sound))
MOLTEN_COMPONENT(Explosion, MOLTEN_FIELD(radius), MOLTEN_FIELD(force), MOLTEN_FIELD(damage), MOLTEN_FIELD(flashSeconds))
MOLTEN_COMPONENT(AudioSource, MOLTEN_FIELD(clip).help("A sound in Assets, like Audio/music.wav"), MOLTEN_FIELD(volume).range(0, 2), MOLTEN_FIELD(pitch).range(0.1f, 4),
                 MOLTEN_FIELD(loops), MOLTEN_FIELD(playsOnStart))
MOLTEN_COMPONENT(Flicker, MOLTEN_FIELD(amount).range(0, 1), MOLTEN_FIELD(speed))

}  // namespace molten::kit
