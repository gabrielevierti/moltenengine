#include "Kit/Kit.hpp"
#include "Kit/Character.hpp"
#include "Kit/Network.hpp"
#include "Kit/Shooting.hpp"
#include "Kit/Navigation.hpp"
#include "Kit/Spline.hpp"
#include "Kit/Layout.hpp"
#include "Kit/Terrain.hpp"
#include "Kit/Foliage.hpp"
#include "Kit/WhenThen.hpp"
#include "Kit/Saveable.hpp"
#include "Kit/Sound.hpp"
#include "Kit/Ragdoll.hpp"
#include "Kit/Rope.hpp"
#include "Kit/Cloth.hpp"
#include "Animation/Timeline.hpp"

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
/// left stick, unless the project's Input settings say otherwise). x to the right, z toward the viewer.
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
    float lifting = 9.81f * body->gravityScale * lift * dt;
    // Slowed by moving through the water, and carried along by its flow.
    auto slow = [&](float under) {
        Vec3 relative = body->velocity - current;
        Vec3 length = rotate(body->rotation, Vec3{0, 0, -1});
        float along = simd::dot(relative, length);
        Vec3 across = relative - length * along;
        body->velocity = current + across * std::max(0.0f, 1.0f - drag * under * dt) +
                         length * (along * std::max(0.0f, 1.0f - drag * lengthDrag * under * dt));
    };
    if (hull.x <= 0 || hull.y <= 0 || hull.z <= 0) {
        // Over a Water, the surface is where its waves are now: what floats rides them.
        float level = waterLevel;
        waterHeightAt(world, body->position, level);
        float under = saturate((level - body->position.y) / std::max(depth, 0.01f) * 0.5f + 0.5f);     // 0 out, 1 fully under
        underNow = under;
        isInWater = under > 0;
        if (!isInWater) return;
        // Up by the weight of the water it pushes aside, and slowed by moving through it.
        body->velocity.y += lifting * under;
        slow(under);
        return;
    }
    // A hull: held up at its four lower corners, each by how deep it is there.
    const Quat turn = body->rotation;
    const Vec3 corners[4] = {{-hull.x, -hull.y, -hull.z}, {hull.x, -hull.y, -hull.z}, {-hull.x, -hull.y, hull.z}, {hull.x, -hull.y, hull.z}};
    float total = 0;
    Vec3 torque{0, 0, 0};
    for (const Vec3& corner : corners) {
        Vec3 arm = rotate(turn, corner);
        Vec3 point = body->position + arm;
        float level = waterLevel;
        waterHeightAt(world, point, level);
        // Fully under when the water is the hull's height above its bottom there.
        float under = saturate((level - point.y) / (2 * hull.y));
        total += under * 0.25f;
        Vec3 up{0, lifting * under * 0.25f, 0};             // a change of speed, for the whole body's mass
        torque += simd::cross(arm, up);
    }
    underNow = total;
    isInWater = total > 0;
    if (!isInWater) return;
    // Each corner's push lifts the whole body, and turns it about its middle as a box of its size turns.
    body->velocity.y += lifting * total;
    if (body->rotates) {
        Vec3 local = rotate(simd::conjugate(turn), torque);
        Vec3 inertia{(hull.y * hull.y + hull.z * hull.z) / 3, (hull.x * hull.x + hull.z * hull.z) / 3, (hull.x * hull.x + hull.y * hull.y) / 3};
        body->angularVelocity += rotate(turn, local / simd::max(inertia, Vec3{1e-4f, 1e-4f, 1e-4f}));
        body->angularVelocity *= std::max(0.0f, 1.0f - turnDrag * total * dt);
    }
    slow(total);
}

void Boat::start(World& world, Entity self) {
    if (Buoyancy* floats = world.scene.get<Buoyancy>(self); floats && floats->lengthDrag == 1) floats->lengthDrag = 0.12f;
}

void Boat::fixedUpdate(World& world, Entity self, float dt) {
    RigidBody* body = world.scene.get<RigidBody>(self);
    const Buoyancy* floats = world.scene.get<Buoyancy>(self);
    if (!body) return;
    if (usesInput) {
        throttle = std::clamp(-world.input.axis("moveZ"), -1.0f, 1.0f);       // W is -1 along moveZ
        steer = std::clamp(world.input.axis("moveX"), -1.0f, 1.0f);
    }
    Vec3 forward = rotate(body->rotation, Vec3{0, 0, -1});
    forward.y = 0;
    forward = safeNormalize(forward, Vec3{0, 0, -1});
    speed = simd::dot(body->velocity, forward);
    if (!floats || !floats->isInWater) return;
    // Pushed by its propeller where it sits in the water, up to its top speed.
    float push = throttle >= 0 ? throttle : throttle * reverse;
    if ((push > 0 && speed < topSpeed) || (push < 0 && speed > -topSpeed * reverse)) body->velocity += forward * (push * acceleration * dt);
    // The rudder turns it the more the faster it goes (and the other way going astern), and the
    // water keeps it going the way it points instead of sliding sideways.
    float bite = std::clamp(speed / std::max(topSpeed * 0.4f, 0.1f), -1.0f, 1.0f);
    body->angularVelocity.y += (-steer * radians(turnSpeed) * bite - body->angularVelocity.y) * std::min(1.0f, 3.0f * dt);
    Vec3 side = simd::cross(forward, Vec3{0, 1, 0});
    float sliding = simd::dot(body->velocity, side);
    body->velocity -= side * sliding * std::min(1.0f, 2.0f * floats->underNow * dt);
    if (!body->rotates) {
        // A body that does not rotate is turned here.
        Transform* transform = world.scene.get<Transform>(self);
        if (transform) transform->rotation = simd::normalize(Quat(body->angularVelocity.y * dt, Vec3{0, 1, 0}) * transform->rotation);
    }
}

void Boat::update(World& world, Entity, float) {
    // For a dial, and for whoever asks: how fast it goes, as a car's speed and as a sailor's.
    world.setValue("speed", std::abs(speed) * 3.6f);
    world.setValue("knots", std::abs(speed) * 1.943844f);
    if (sound.empty()) return;
    if (voice == 0) {
        voice = world.startSound(sound, 0.0f, 1.0f, true);
        if (voice == 0) voice = ~0u;                            // no sound here: do not ask again every frame
    }
    // Idling it mutters; with the lever forward it is loud, and higher the faster the boat goes.
    float effort = std::abs(throttle), pace = std::clamp(std::abs(speed) / std::max(topSpeed, 0.1f), 0.0f, 1.0f);
    if (voice != ~0u) world.changeSound(voice, 0.22f + 0.5f * effort, 0.6f + 0.45f * effort + 0.35f * pace);
}

void Spin::update(World& world, Entity self, float dt) {
    if (Transform* transform = world.scene.get<Transform>(self)) {
        Quat turn = axisAngle(safeNormalize(axis), radians(degreesPerSecond) * dt);
        transform->rotation = simd::normalize(turn * transform->rotation);
    }
}

void Shuttle::update(World& world, Entity self, float dt) {
    Transform* place = world.scene.get<Transform>(self);
    if (!place) return;
    if (!hasFrom) {
        from = place->position;
        hasFrom = true;
    }
    time += dt;
    float leg = std::max(seconds, 0.01f) + std::max(waits, 0.0f);
    float through = std::fmod(time, 2 * leg);
    bool comesBack = through >= leg;
    float along = saturate((comesBack ? through - leg : through) / std::max(seconds, 0.01f));
    if (eases) along = along * along * (3 - 2 * along);
    place->position = from + offset * (comesBack ? 1 - along : along);
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
    if (age >= seconds) world.release(self);        // back to its pool, if it came from one
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

void Health::onMessage(World& world, Entity self, const Message& message) {
    if (message.name == "damage" && message.number > 0) damage(world, self, message.number);
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

void LoadingScreen::update(World& world, Entity self, float) {
    bool isLoading = world.isLoadingScene();
    world.scene.each<Transform>([&](Entity, Transform& transform) {
        if (transform.parent == self) transform.isActive = isLoading;
    });
}

int Spawner::countAlive(World& world) {
    alive.erase(std::remove_if(alive.begin(), alive.end(),
                               [&](Entity entity) {
                                   if (!world.scene.isAlive(entity)) return true;
                                   const Pooled* pooled = world.scene.get<Pooled>(entity);
                                   const Transform* transform = world.scene.get<Transform>(entity);
                                   return (pooled && pooled->isWaiting) || !transform || !transform->isActive;
                               }),
                alive.end());
    return int(alive.size());
}

void Spawner::start(World&, Entity) {
    waited = 0;
    spawned = 0;
    hasBegun = false;
    wave = inWave = 0;
    isPausing = isDone = toldDone = false;
    alive.clear();
}

void Spawner::update(World& world, Entity self, float dt) {
    int about = countAlive(world);
    if (!aliveValue.empty()) world.setValue(aliveValue, float(about));
    if (isDone || (limit > 0 && spawned >= limit)) {
        // All made and all gone: said once, for whoever waits for it.
        if (about == 0 && !toldDone && (isDone || limit > 0)) {
            toldDone = true;
            world.send("wavesDone", Entity{}, float(wave), prefab, self);
        }
        return;
    }
    waited += dt;
    if (isPausing) {
        if (waited < waveSeconds) return;
        isPausing = false;
        waited = interval;              // the wave's first at once
    }
    float due = hasBegun ? interval : firstDelay;
    if (waited < due || (maxAlive > 0 && about >= maxAlive)) return;
    waited = std::min(waited - due, due);
    hasBegun = true;
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    Vec3 position = transform->worldPosition();
    if (radius > 0) {
        float angle = random.range(0.0f, 2.0f * kPi), distance = radius * std::sqrt(random.next());
        position += Vec3{std::cos(angle) * distance, 0, std::sin(angle) * distance};
    }
    spawned++;
    Entity made = usesPool ? world.take(prefab, position) : world.spawn(prefab, position);
    if (made.isValid()) alive.push_back(made);
    if (count > 0 && ++inWave >= count) {
        inWave = 0;
        wave++;
        world.send("waveDone", Entity{}, float(wave), prefab, self);
        if (waves > 0 && wave >= waves) isDone = true;
        else isPausing = true;
        waited = 0;
    }
}

// ---------------------------------------------------------------- HudText, Goal, RestartWhenBelow

std::string HudText::filled(const World& world, const std::string& written) {
    // In the game's language first, as a whole: "BEST LAP   {bestLap:time}" is one line of a table.
    const std::string text = world.translate(written);
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
            // "{loading:percent}": 0 to 1 as 0% to 100%.
            if (name.compare(colon + 1, std::string::npos, "percent") == 0) {
                char percent[16];
                std::snprintf(percent, sizeof(percent), "%d%%", int(std::lround(world.value(name.substr(0, colon)) * 100)));
                out += percent;
                i = close + 1;
                continue;
            }
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
            out += world.translate(word);            // "{weather:SUN|RAIN}": each word in the game's language
            i = close + 1;
            continue;
        }
        if (world.hasText(name)) {                  // shared words: a name typed in a text field
            out += world.text(name);
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

void HudText::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    InterfaceFont typeface(world, canvas, font, loadedFont, loadedFontName);
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
    sound = world.startSound(clip, volume, pitch, loops, group);
    shownVolume = volume;
    shownPitch = pitch;
}

void AudioSource::stop(World& world) {
    world.stopSound(sound);
    sound = 0;
}

void AudioSource::update(World& world, Entity self, float) {
    // From where it is, as it moves and as the listener does.
    if (sound != 0 && comesFromHere) {
        if (const Transform* transform = world.scene.get<Transform>(self)) {
            world.placeSound(sound, transform->worldPosition(), fullWithin, silentBeyond);
            if (volume != shownVolume || pitch != shownPitch) world.changeSound(sound, volume, pitch);
            shownVolume = volume;
            shownPitch = pitch;
            return;
        }
    }
    // Volume and pitch can be changed while it plays, by the inspector or by code.
    if (sound != 0 && (volume != shownVolume || pitch != shownPitch)) {
        world.changeSound(sound, volume, pitch);
        shownVolume = volume;
        shownPitch = pitch;
    }
}

void registerKit() {
    (void)engineTorque("i4", 1000);
    linkCharacters();
    linkShooting();
    linkNetwork();
    linkNavigation();
    linkSplines();
    linkTerrain();
    linkFoliage();
    linkWhenThen();
    linkSaveable();
    linkSound();
    linkRagdoll();
    linkRope();
    linkCloth();
    linkTimeline();
    (void)script::typeNameFor("kit");
    (void)&sharedGltfModel;                 // and the Animator       // and what scripts made from visual scripts are written with         // names something in Vehicles.cpp, so that a program that links the kit gets the vehicles too
    // Where each is found under Add Component. What is offered is what a game is built of, as in
    // Unity: what is drawn, what collides, what sounds, the interface, animation, navigation,
    // paths, vehicles' parts, the network. What a game does with them (a player that moves, a
    // pickup, a gun, a goal) is its own code: the templates have theirs as scripts to copy and
    // change. The components that did it are kept, for the scenes that have them, but not offered.
    ComponentRegistry& registry = ComponentRegistry::instance();
    for (const char* name : {"PlayerMover", "Follow", "Spin", "Mover", "Lifetime", "Collectible", "Health", "Damage", "Shooter", "Spawner", "Goal",
                             "RestartWhenBelow", "ConstantSpeed", "Breakable", "Shuttle", "Explosion", "Flicker", "Guard", "Weapon", "ShootingTarget",
                             "Hunter", "Supply", "DamageFlash", "Boat", "DriveControls", "FlightControls", "CharacterMover", "OrbitCamera",
                             "FirstPersonLook", "WhenThen"}) {
        registry.setCategory(name, "Hidden");
    }
    registry.setCategory("Buoyancy", "Physics");
    registry.setCategory("CharacterController", "Physics");
    ComponentRegistry::setDisplayName("CharacterController", "Character Controller");
    ComponentRegistry::setDisplayName("CharacterMover", "Character Mover");
    ComponentRegistry::setDisplayName("OrbitCamera", "Orbit Camera");
    for (const char* name : {"NavigationArea", "NavAgent"}) registry.setCategory(name, "Navigation");
    ComponentRegistry::setDisplayName("NavigationArea", "Navigation Area");
    ComponentRegistry::setDisplayName("NavAgent", "Nav Agent");
    for (const char* name : {"Spline", "SplineFollower", "SplineRoad"}) registry.setCategory(name, "Paths");
    ComponentRegistry::setDisplayName("SplineFollower", "Spline Follower");
    ComponentRegistry::setDisplayName("SplineRoad", "Spline Road");
    registry.setCategory("Joint", "Physics");
    registry.setCategory("Animator", "Animation");
    registry.setCategory("TwoBoneIK", "Animation");
    registry.setCategory("TimelinePlayer", "Hidden");
    ComponentRegistry::setDisplayName("TimelinePlayer", "Timeline Player");
    ComponentRegistry::setDisplayName("TwoBoneIK", "Two Bone IK");
    registry.setCategory("Water", "Hidden");        // what scenes from before water was all in its material had: read, and dropped (Water/Water.hpp)
    registry.setCategory("Terrain", "Rendering");
    registry.setCategory("Foliage", "Rendering");
    registry.setCategory("Saveable", "Miscellaneous");
    registry.setCategory("LoadingScreen", "UI");
    ComponentRegistry::setDisplayName("LoadingScreen", "Loading Screen");
    ComponentRegistry::setDisplayName("WhenThen", "When / Then");
    registry.setCategory("Liquid", "Effects");
    registry.setCategory("Decal", "Rendering");
    (void)sizeof(Liquid);
    registry.setCategory("HudText", "UI");
    registry.setCategory("TouchStick", "UI");
    for (const char* name : {"NetworkSession", "Networked"}) registry.setCategory(name, "Network");
    ComponentRegistry::setDisplayName("NetworkSession", "Network Session");
    // The interface's basic pieces, from which anything is made: a panel, a picture, words, a button,
    // a toggle, a slider, a box to type in, a bar, an arc, a layout that places the others.
    for (const char* name : {"UILayout", "UIPanel", "UIImage", "UIButton", "UIToggle", "UIBar", "UISlider", "UITextField", "UIArc"}) registry.setCategory(name, "UI");
    ComponentRegistry::setDisplayName("UIArc", "Arc");
    ComponentRegistry::setDisplayName("UIToggle", "Toggle");
    // Pieces made of pieces: kept for the scenes that have them, not offered any more. A gauge is made of arcs and texts.
    // (Still pieces of the interface: laid out for the reference screen and scaled with the rest of it.
    // So is what other components draw over the game in points: a weapon's sights, the session's menu.)
    for (const char* name : {"UIGauge", "UIMarks", "UINeedle", "TouchButton", "Weapon", "NetworkSession"}) registry.setInterfacePiece(name);
    for (const char* name : {"UIGauge", "UIMarks", "UINeedle"}) registry.setCategory(name, "Hidden");
    ComponentRegistry::setDisplayName("UIMarks", "Dial Marks");
    ComponentRegistry::setDisplayName("UINeedle", "Needle");
    // A touch button is a round Button: kept for scenes that have one, but not offered any more.
    registry.setCategory("TouchButton", "Hidden");
    for (auto [type, shown] : {std::pair<const char*, const char*>{"HudText", "Text"}, {"UIPanel", "Panel"}, {"UIImage", "Image"}, {"UIButton", "Button"},
                               {"UIBar", "Bar"}, {"UIGauge", "Gauge"}, {"TouchStick", "Stick"}, {"UILayout", "Layout"}, {"UISlider", "Slider"}, {"UITextField", "Text Field"}}) {
        ComponentRegistry::setDisplayName(type, shown);
    }
    registry.setCategory("AudioSource", "Audio");
    // The parts of a vehicle (what drives them is the game's code: the templates' scripts).
    for (const char* name : {"VehicleBody", "Bumper", "Wheel", "Suspension", "DeformableTire", "Motor", "Wing", "Hinge", "Propeller"}) {
        registry.setCategory(name, "Vehicles");
    }
    registry.setCategory("VisualScript", "Scripts");
    registry.setCategory("LodGroup", "Rendering");
    ComponentRegistry::setDisplayName("LodGroup", "LOD Group");
}


}  // namespace molten::kit

namespace molten::kit {

MOLTEN_COMPONENT(PlayerMover, MOLTEN_FIELD(speed).range(0, 30), MOLTEN_FIELD(jumpSpeed).range(0, 20), MOLTEN_FIELD(rolls),
                 MOLTEN_FIELD(acceleration).range(0, 80), MOLTEN_FIELD(movesAlongX), MOLTEN_FIELD(movesAlongZ), MOLTEN_FIELD(limitX),
                 MOLTEN_FIELD(facesMovement))
MOLTEN_COMPONENT(Buoyancy, MOLTEN_FIELD(waterLevel), MOLTEN_FIELD(lift).range(0, 6).help("Times its weight when fully under: above 1 it floats"), MOLTEN_FIELD(depth).range(0.05f, 10),
                 MOLTEN_FIELD(drag).range(0, 10),
                 MOLTEN_FIELD(hull).help("Half its size each way: then it rocks on the waves and rights itself. Zero: held up at its middle"),
                 MOLTEN_FIELD(turnDrag).range(0, 10), MOLTEN_FIELD(current).help("The water's flow, meters per second"),
                 MOLTEN_FIELD(lengthDrag).range(0, 1).help("Of the drag, along its length: a hull cuts through the water ahead"))
MOLTEN_COMPONENT(Follow, MOLTEN_FIELD(target).picks("entity").help("The name of the entity to follow"), MOLTEN_FIELD(offset), MOLTEN_FIELD(smoothness).range(0, 20),
                 MOLTEN_FIELD(looksAtTarget), MOLTEN_FIELD(turnsWithTarget))
MOLTEN_COMPONENT(Spin, MOLTEN_FIELD(axis), MOLTEN_FIELD(degreesPerSecond))
MOLTEN_COMPONENT(LodGroup, MOLTEN_FIELD(distance).range(1, 2000).help("Meters from the camera: nearer, the model in full; farther, the child named Far"),
                 MOLTEN_FIELD(far).picks("child").help("The child that is the simpler version, drawn only from far away"))
MOLTEN_COMPONENT(Mover, MOLTEN_FIELD(velocity), MOLTEN_FIELD(isLocal), MOLTEN_FIELD(chases).picks("entity").help("If set: move toward the entity with this name"),
                 MOLTEN_FIELD(chaseSpeed))
MOLTEN_COMPONENT(Lifetime, MOLTEN_FIELD(seconds))
MOLTEN_COMPONENT(LoadingScreen)
MOLTEN_COMPONENT(Collectible, MOLTEN_FIELD(collector).picks("entityOrTag").help("Who can pick it up, by name; empty for anyone"),
                 MOLTEN_FIELD(counter).picks("value").help("The shared value that goes up"), MOLTEN_FIELD(amount), MOLTEN_FIELD(sound).picks("sound"), MOLTEN_FIELD(effect).picks("prefab"))
MOLTEN_COMPONENT(Shuttle, MOLTEN_FIELD(offset).help("Where it goes to, from where it starts"), MOLTEN_FIELD(seconds).range(0.05f, 120).help("Each way"),
                 MOLTEN_FIELD(waits).range(0, 60).help("Seconds at each end"), MOLTEN_FIELD(eases).help("Slows into each end"))
MOLTEN_COMPONENT(Health, MOLTEN_FIELD(amount), MOLTEN_FIELD(counter).picks("value").help("A shared value that shows it, for the screen"), MOLTEN_FIELD(scoreCounter).picks("value"),
                 MOLTEN_FIELD(points), MOLTEN_FIELD(effect).picks("prefab").help("A prefab spawned where it died"), MOLTEN_FIELD(restartsSceneOnDeath))
MOLTEN_COMPONENT(Damage, MOLTEN_FIELD(amount), MOLTEN_FIELD(onlyTo).picks("entityOrTag").help("If set: only entities whose name starts with this"), MOLTEN_FIELD(destroysSelf))
MOLTEN_COMPONENT(Shooter, MOLTEN_FIELD(prefab).picks("prefab"), MOLTEN_FIELD(key).choices({"Space", "Enter", "F", "E", "Q", "X", "Z", "LeftShift", "LeftControl"}), MOLTEN_FIELD(pointerToo), MOLTEN_FIELD(interval).range(0.02f, 3), MOLTEN_FIELD(muzzle),
                 MOLTEN_FIELD(sound).picks("sound"))
MOLTEN_COMPONENT(Spawner, MOLTEN_FIELD(prefab).picks("prefab"), MOLTEN_FIELD(interval).help("Seconds between two"), MOLTEN_FIELD(radius), MOLTEN_FIELD(limit).help("Stop after this many; 0 for no limit"),
                 MOLTEN_FIELD(firstDelay), MOLTEN_FIELD(count).range(0, 1000).help("In a wave; 0: no waves"),
                 MOLTEN_FIELD(waves).range(0, 1000).help("With waves: how many; 0 forever"), MOLTEN_FIELD(waveSeconds).range(0, 600).help("The pause between two waves"),
                 MOLTEN_FIELD(maxAlive).range(0, 10000).help("No more than this about at once; 0: no limit"),
                 MOLTEN_FIELD(usesPool).help("Reuses those put back instead of making new ones"),
                 MOLTEN_FIELD(aliveValue).picks("value").help("A shared value kept at how many are about"))
// ---------------------------------------------------------------- touch controls

namespace {

/// The fingers on the screen. A mouse with its button down is one: that is how they are tried on a Mac.
struct Finger {
    uint64_t id;
    Vec2 position;
    bool hasBegun;
};
/// In the interface's own points: the screen's, divided by the scale it is drawn at.
std::vector<Finger> fingersOn(const World& world) {
    const Input& input = world.input;
    float scale = world.interfaceScale > 1e-3f ? world.interfaceScale : 1.0f;
    std::vector<Finger> fingers;
    for (const Input::Touch& touch : input.touches()) {
        if (!touch.hasEnded) fingers.push_back(Finger{touch.id, touch.position / scale, touch.hasBegun});
    }
    if (fingers.empty() && input.isDown(MouseButton::Left)) {
        fingers.push_back(Finger{~0ull, input.mousePosition() / scale, input.wasPressed(MouseButton::Left)});
    }
    return fingers;
}

Vec2 pointerOn(const World& world) {
    return world.input.mousePosition() / (world.interfaceScale > 1e-3f ? world.interfaceScale : 1.0f);
}

Vec2 gScreen{1280, 720};        // the canvas's size the last time something was drawn on it

}  // namespace

void TouchStick::update(World& world, Entity, float) {
    Input& input = world.input;
    std::vector<Finger> fingers = fingersOn(world);
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
    // Hidden until a finger is seen, but shown while the scene is edited: it has to be seen to be laid out.
    if (onlyOnTouchScreens && world.runsComponents && !world.input.hasTouched()) return;
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
    for (const Finger& finger : fingersOn(world)) {
        if (simd::length(finger.position - middle) <= radius * 1.25f) pressed = true;      // a little generous: thumbs are not pointers
    }
    isHeld = pressed;
    if (!action.empty()) world.input.setVirtualButton(action, pressed);
}

void TouchButton::draw(World& world, Entity, Canvas& canvas) {
    gScreen = canvas.size();
    // Hidden until a finger is seen, but shown while the scene is edited: it has to be seen to be laid out.
    if (onlyOnTouchScreens && world.runsComponents && !world.input.hasTouched()) return;
    Vec2 middle = place(gScreen);
    canvas.circle(middle, radius, color * Vec4{1, 1, 1, isHeld ? 0.9f : 0.3f});
    canvas.ring(middle, radius, 2.0f, color);
    if (!label.empty()) {
        // Smaller for a longer word, so that it stays inside the circle.
        float size = radius * 0.7f * std::min(1.0f, 3.2f / float(std::max<size_t>(label.size(), 1)));
        canvas.text(label, Vec2{middle.x, middle.y - size * 0.6f}, size, Vec4{color.x, color.y, color.z, 0.95f}, TextAlign::Center);
    }
}

// ---------------------------------------------------------------- panels, pictures, buttons, bars

void UIPanel::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    gScreen = canvas.size();
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    canvas.rect(corner, size, color);
    if (border > 0) canvas.rectOutline(corner, size, border, borderColor);
}

void UIImage::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
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

void UIButton::update(World& world, Entity self, float) {
    Vec2 middle = placeOnScreen(gScreen, anchor, offset);
    auto isOn = [&](Vec2 point) {
        if (!isTouchableAt(world, self, point)) return false;       // scrolled out of its list
        // A round one is a little generous: thumbs are not pointers.
        if (isRound) return simd::length(point - middle) <= radius * 1.25f;
        return std::abs(point.x - middle.x) <= size.x * 0.5f && std::abs(point.y - middle.y) <= size.y * 0.5f;
    };
    bool pressed = false;
    for (const Finger& finger : fingersOn(world)) pressed = pressed || isOn(finger.position);
    isHovered = isOn(pointerOn(world));
    // Clicked: let go while still on it, as buttons are everywhere.
    bool letGoOnIt = false;
    for (const Input::Touch& touch : world.input.touches()) letGoOnIt = letGoOnIt || (touch.hasEnded && isOn(touch.position / std::max(world.interfaceScale, 1e-3f)));
    if (world.input.touches().empty() && world.input.wasReleased(MouseButton::Left) && isHovered) letGoOnIt = true;
    wasClicked = isHeld && !pressed && letGoOnIt;
    isHeld = pressed;
    if (!action.empty()) world.input.setVirtualButton(action, pressed);
    if (wasClicked) {
        if (!value.empty()) world.setValue(value, world.value(value) + amount);
        if (!scene.empty()) world.loadScene(scene);
    }
}

void UIButton::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    InterfaceFont typeface(world, canvas, font, loadedFont, loadedFontName);
    gScreen = canvas.size();
    // Hidden until a finger is seen, but shown while the scene is edited: it has to be seen to be laid out.
    if (onlyOnTouchScreens && world.runsComponents && !world.input.hasTouched()) return;
    if (isRound) {
        Vec2 middle = placeOnScreen(gScreen, anchor, offset);
        canvas.circle(middle, radius, color * Vec4{1, 1, 1, isHeld ? 0.9f : 0.3f});
        canvas.ring(middle, radius, 2.0f, color);
        if (!label.empty()) {
            std::string shown = world.translate(label);
            float height = radius * 0.7f * std::min(1.0f, 3.2f / float(std::max<size_t>(shown.size(), 1)));
            canvas.text(shown, Vec2{middle.x, middle.y - height * 0.6f}, height, Vec4{color.x, color.y, color.z, 0.95f}, TextAlign::Center);
        }
        return;
    }
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    Vec4 shade = isHeld ? Vec4{0.75f, 0.75f, 0.75f, 1} : isHovered ? Vec4{1.12f, 1.12f, 1.12f, 1} : Vec4{1, 1, 1, 1};
    canvas.rect(corner, size, color * shade);
    if (!label.empty()) {
        float height = std::min(size.y * 0.5f, 28.0f);
        canvas.text(world.translate(label), Vec2{corner.x + size.x * 0.5f, corner.y + (size.y - height * 1.2f) * 0.5f}, height, textColor, TextAlign::Center);
    }
}

void UIGauge::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    gScreen = canvas.size();
    Vec2 middle = placeOnScreen(gScreen, anchor, offset);
    // While nothing has set the value (the editor, before the game runs) the needle rests at
    // two thirds, to be seen.
    float target = world.hasValue(value) ? world.value(value) : most * 0.66f;
    shown += (target - shown) * std::min(1.0f, world.time.unscaledDelta * 12.0f);
    float share = most > 0 ? saturate(shown / most) : 0.0f;
    // The arc runs clockwise from the lower left to the lower right, three quarters of a turn.
    const float from = kPi * 0.75f, sweep = kPi * 1.5f;
    canvas.circle(middle, radius + thickness * 0.6f, backColor);
    canvas.arc(middle, radius - thickness * 0.5f, from, from + sweep, thickness, Vec4{1, 1, 1, 0.1f});
    if (redFrom > 0 && redFrom < 1) {
        canvas.arc(middle, radius - thickness * 0.5f, from + sweep * redFrom, from + sweep, thickness, Vec4{0.9f, 0.12f, 0.08f, 0.55f});
    }
    if (share > 0.002f) canvas.arc(middle, radius - thickness * 0.5f, from, from + sweep * share, thickness, color);
    // The marks: long and numbered at each multiple of numbersEvery, short between.
    if (numbersEvery > 0 && most > 0) {
        int count = int(most / numbersEvery + 0.5f);
        float numberSize = std::max(radius * 0.15f, 8.0f);
        for (int i = 0; i <= count * 2; i++) {
            float at = float(i) * 0.5f * numbersEvery / most;
            if (at > 1.0001f) break;
            float angle = from + sweep * at;
            Vec2 direction{std::cos(angle), std::sin(angle)};
            bool isLong = i % 2 == 0;
            float inner = radius - thickness - (isLong ? radius * 0.13f : radius * 0.07f);
            canvas.line(middle + direction * inner, middle + direction * (radius - thickness - 1), isLong ? 2.0f : 1.2f, Vec4{1, 1, 1, isLong ? 0.85f : 0.45f});
            if (isLong) {
                char number[16];
                std::snprintf(number, sizeof(number), "%g", double(std::round(float(i / 2) * numbersEvery * numbersScale * 10) / 10));
                Vec2 place = middle + direction * (inner - numberSize * 0.9f);
                canvas.text(number, Vec2{place.x, place.y - numberSize * 0.6f}, numberSize, Vec4{1, 1, 1, 0.75f}, TextAlign::Center);
            }
        }
    }
    // The needle.
    float angle = from + sweep * share;
    Vec2 direction{std::cos(angle), std::sin(angle)};
    canvas.line(middle - direction * radius * 0.12f, middle + direction * (radius - thickness * 1.3f), 2.5f, Vec4{1.0f, 0.3f, 0.15f, 1});
    canvas.circle(middle, radius * 0.07f + 2, Vec4{0.9f, 0.9f, 0.92f, 1});
    // The value, and what it is.
    if (showsValue) {
        char number[24];
        std::snprintf(number, sizeof(number), "%d", int(std::round(world.hasValue(value) ? target : 0)));
        float size = radius * 0.36f;
        canvas.text(number, Vec2{middle.x, middle.y + radius * 0.22f}, size, Vec4{1, 1, 1, 1}, TextAlign::Center);
    }
    if (!label.empty()) canvas.text(label, Vec2{middle.x, middle.y + radius * (showsValue ? 0.62f : 0.32f)}, std::max(radius * 0.14f, 8.0f), Vec4{1, 1, 1, 0.55f}, TextAlign::Center);
}

namespace {
/// How far along a value is, eased: a needle and a dial have weight. Before the game sets it (the
/// editor), two thirds, to be seen.
float easedShare(World& world, const std::string& value, float most, float& shown) {
    if (value.empty()) return 1.0f;
    float target = world.hasValue(value) ? world.value(value) : most * 0.66f;
    shown += (target - shown) * std::min(1.0f, world.time.unscaledDelta * 12.0f);
    return most > 0 ? saturate(shown / most) : 0.0f;
}
}  // namespace

void UIArc::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    gScreen = canvas.size();
    Vec2 middle = placeOnScreen(gScreen, anchor, offset);
    float share = easedShare(world, value, most, shown);
    float from = radians(startAngle), to = from + radians(sweep) * share;
    if (share <= 0.002f || thickness <= 0) return;
    if (thickness >= radius * 0.98f && sweep >= 359.9f) {
        canvas.circle(middle, radius, color);
        return;
    }
    canvas.arc(middle, radius - thickness * 0.5f, from, to, thickness, color);
}

void UIMarks::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    gScreen = canvas.size();
    Vec2 middle = placeOnScreen(gScreen, anchor, offset);
    if (every <= 0 || most <= 0) return;
    int count = int(most / every + 0.5f);
    float numberSize = std::max(radius * 0.17f, 8.0f);
    float from = radians(startAngle), span = radians(sweep);
    for (int i = 0; i <= count * 2; i++) {
        float at = float(i) * 0.5f * every / most;
        if (at > 1.0001f) break;
        float angle = from + span * at;
        Vec2 direction{std::cos(angle), std::sin(angle)};
        bool isLong = i % 2 == 0;
        float inner = radius - (isLong ? radius * 0.15f : radius * 0.08f);
        canvas.line(middle + direction * inner, middle + direction * radius, isLong ? 2.0f : 1.2f, Vec4{color.x, color.y, color.z, color.w * (isLong ? 1.0f : 0.55f)});
        if (isLong && showsNumbers) {
            char number[16];
            std::snprintf(number, sizeof(number), "%g", double(std::round(float(i / 2) * every * numbersScale * 10) / 10));
            Vec2 place = middle + direction * (inner - numberSize * 0.9f);
            canvas.text(number, Vec2{place.x, place.y - numberSize * 0.6f}, numberSize, Vec4{color.x, color.y, color.z, color.w * 0.9f}, TextAlign::Center);
        }
    }
}

void UINeedle::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    gScreen = canvas.size();
    Vec2 middle = placeOnScreen(gScreen, anchor, offset);
    float share = easedShare(world, value, most, shown);
    float angle = radians(startAngle) + radians(sweep) * share;
    Vec2 direction{std::cos(angle), std::sin(angle)};
    canvas.line(middle - direction * radius * 0.15f, middle + direction * radius, thickness, color);
    canvas.circle(middle, thickness * 1.6f + 1.5f, Vec4{0.9f, 0.9f, 0.92f, 1});
}

void UIBar::draw(World& world, Entity self, Canvas& canvas) {
    InterfaceClip clip(world, self, canvas);
    gScreen = canvas.size();
    Vec2 corner = placeOnScreen(gScreen, anchor, offset) - size * 0.5f;
    float share = most > 0 ? saturate(world.value(value) / most) : 0.0f;
    // While nothing has set the value (the editor, before the game runs) it is shown full, to be seen.
    if (!world.hasValue(value)) share = 1.0f;
    canvas.rect(corner, size, backColor);
    canvas.rect(corner + Vec2{2, 2}, Vec2{std::max(size.x - 4, 0.0f) * share, std::max(size.y - 4, 0.0f)}, color);
}

// Layouts first: each places what it holds before any of it is drawn, and draws behind it.
MOLTEN_COMPONENT(UILayout, MOLTEN_FIELD(arrangement).range(0, 2).help("0 a row, 1 a column, 2 a grid"), MOLTEN_FIELD(columns).range(1, 12).help("Of a grid"),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(padding).range(0, 100), MOLTEN_FIELD(spacing).range(0, 100),
                 MOLTEN_FIELD(alignment).range(0, 2).help("Across: 0 the start, 1 the middle, 2 the end"),
                 MOLTEN_FIELD(stretches).help("What it holds is as wide as a column, as tall as a row"),
                 MOLTEN_FIELD(fitsContent).help("As long as what it holds"), MOLTEN_FIELD(scrolls).help("What does not fit is dragged into view"),
                 MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(UIPanel, MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(border).range(0, 20),
                 MOLTEN_FIELD(borderColor).color())
MOLTEN_COMPONENT(UIImage, MOLTEN_FIELD(picture).picks("picture").help("A picture of Assets: Textures/logo.png"), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size),
                 MOLTEN_FIELD(tint).color())
MOLTEN_COMPONENT(UIButton, MOLTEN_FIELD(label), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(textColor).color(),
                 MOLTEN_FIELD(action).picks("action").help("A button action it holds while pressed: jump, fire"),
                 MOLTEN_FIELD(isRound).help("A circle for a thumb, of Radius, instead of a box of Size"), MOLTEN_FIELD(radius).range(12, 150),
                 MOLTEN_FIELD(onlyOnTouchScreens).help("Hidden until the screen is touched: for a game that also has keys"), MOLTEN_FIELD(scene).picks("scene").help("A scene it loads when clicked: Scenes/Level1.scene"),
                 MOLTEN_FIELD(value).picks("value").help("A shared value it adds Amount to when clicked"), MOLTEN_FIELD(amount),
                 MOLTEN_FIELD(font).picks("font").help("Of its label: a font of Assets, Fonts/Title. Empty: the game's own"))
MOLTEN_COMPONENT(UIGauge, MOLTEN_FIELD(value).picks("value").help("The shared value it shows: speed, rpm, boost"), MOLTEN_FIELD(most),
                 MOLTEN_FIELD(numbersEvery).help("A numbered mark at each multiple of this; 0 for none"),
                 MOLTEN_FIELD(numbersScale).help("The numbers as written: 0.001 writes rpm in thousands"),
                 MOLTEN_FIELD(redFrom).range(0, 1).help("Where the red part of the arc starts, as a share; 0 for none"), MOLTEN_FIELD(label), MOLTEN_FIELD(showsValue),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(radius).range(16, 300), MOLTEN_FIELD(thickness).range(1, 40),
                 MOLTEN_FIELD(color).color(), MOLTEN_FIELD(backColor).color())
MOLTEN_COMPONENT(UIArc, MOLTEN_FIELD(value).picks("value").help("Filled as far as this shared value is of Most; empty: all of it"), MOLTEN_FIELD(most),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(radius).range(2, 600), MOLTEN_FIELD(thickness).range(0, 600),
                 MOLTEN_FIELD(startAngle).range(-360, 360).help("Degrees, clockwise from the right: 135 is the lower left"),
                 MOLTEN_FIELD(sweep).range(0, 360).help("How far round it goes, in degrees"), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(UIMarks, MOLTEN_FIELD(most), MOLTEN_FIELD(every).help("A numbered mark at each multiple of this"),
                 MOLTEN_FIELD(numbersScale).help("The numbers as written: 0.001 writes rpm in thousands"), MOLTEN_FIELD(showsNumbers),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(radius).range(2, 600), MOLTEN_FIELD(startAngle).range(-360, 360),
                 MOLTEN_FIELD(sweep).range(0, 360), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(UINeedle, MOLTEN_FIELD(value).picks("value").help("The shared value it points at"), MOLTEN_FIELD(most), MOLTEN_FIELD(anchor),
                 MOLTEN_FIELD(offset), MOLTEN_FIELD(radius).range(2, 600), MOLTEN_FIELD(thickness).range(0.5f, 20), MOLTEN_FIELD(startAngle).range(-360, 360),
                 MOLTEN_FIELD(sweep).range(0, 360), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(UIBar, MOLTEN_FIELD(value).picks("value").help("The shared value it shows: health"), MOLTEN_FIELD(most), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size),
                 MOLTEN_FIELD(color).color(), MOLTEN_FIELD(backColor).color())
MOLTEN_COMPONENT(UISlider, MOLTEN_FIELD(label), MOLTEN_FIELD(value).picks("value").help("The shared value it shows and sets: volume"),
                 MOLTEN_FIELD(isSaved).help("Kept between plays, as a preference of that name"), MOLTEN_FIELD(least), MOLTEN_FIELD(most),
                 MOLTEN_FIELD(step).help("It moves by this much; 0 smoothly"), MOLTEN_FIELD(showsNumber), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset),
                 MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(backColor).color(), MOLTEN_FIELD(textColor).color())
MOLTEN_COMPONENT(UIToggle, MOLTEN_FIELD(label), MOLTEN_FIELD(value).picks("value").help("The shared value it shows and sets, 1 ticked or 0: sound"),
                 MOLTEN_FIELD(isOn).help("Ticked at the start, when the value has not been set yet"), MOLTEN_FIELD(isSaved).help("Kept between plays, as a preference of that name"),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(backColor).color(),
                 MOLTEN_FIELD(textColor).color())
MOLTEN_COMPONENT(UITextField, MOLTEN_FIELD(value).picks("value").help("The shared text it shows and sets: playerName. Shown by a text as {playerName}"),
                 MOLTEN_FIELD(placeholder).help("Shown faintly while it is empty"), MOLTEN_FIELD(maxLength).range(0, 200),
                 MOLTEN_FIELD(isSaved).help("Kept between plays"), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(size), MOLTEN_FIELD(color).color(),
                 MOLTEN_FIELD(textColor).color(), MOLTEN_FIELD(focusColor).color(), MOLTEN_FIELD(font).picks("font").help("A font of Assets; empty for the game's own"))
MOLTEN_COMPONENT(TouchStick, MOLTEN_FIELD(horizontal).picks("axis").help("The axis action it pushes left and right"), MOLTEN_FIELD(vertical).picks("axis").help("The axis action it pushes up and down"),
                 MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset), MOLTEN_FIELD(radius).range(20, 200), MOLTEN_FIELD(verticalIsReversed),
                 MOLTEN_FIELD(onlyOnTouchScreens).help("Hidden until the screen is touched: for a game that also has keys"), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(TouchButton, MOLTEN_FIELD(action).picks("action").help("The button action it holds: jump, fire..."), MOLTEN_FIELD(label), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset),
                 MOLTEN_FIELD(radius).range(12, 150), MOLTEN_FIELD(onlyOnTouchScreens).help("Hidden until the screen is touched: for a game that also has keys"),
                 MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(HudText, MOLTEN_FIELD(text).help("{name} shows a shared value: Coins: {score}"), MOLTEN_FIELD(anchor), MOLTEN_FIELD(offset),
                 MOLTEN_FIELD(font).picks("font").help("A font of Assets made by Tools/bake_font.py: Fonts/Title. Empty: the game's own"),
                 MOLTEN_FIELD(size).range(8, 120), MOLTEN_FIELD(color).color())
MOLTEN_COMPONENT(Goal, MOLTEN_FIELD(counter).picks("value"), MOLTEN_FIELD(target), MOLTEN_FIELD(message), MOLTEN_FIELD(nextScene).picks("scene"), MOLTEN_FIELD(delay))
MOLTEN_COMPONENT(RestartWhenBelow, MOLTEN_FIELD(height))
MOLTEN_COMPONENT(ConstantSpeed, MOLTEN_FIELD(speed), MOLTEN_FIELD(startDirection), MOLTEN_FIELD(minimumAlongZ).range(0, 0.9f))
MOLTEN_COMPONENT(Breakable, MOLTEN_FIELD(by).picks("entityOrTag"), MOLTEN_FIELD(counter).picks("value"), MOLTEN_FIELD(points), MOLTEN_FIELD(effect).picks("prefab"), MOLTEN_FIELD(sound).picks("sound"))
MOLTEN_COMPONENT(Explosion, MOLTEN_FIELD(radius), MOLTEN_FIELD(force), MOLTEN_FIELD(damage), MOLTEN_FIELD(flashSeconds))
MOLTEN_COMPONENT(Decal, MOLTEN_FIELD(picture).picks("picture").help("A picture in Assets, see-through where nothing is drawn. The entity's scale is the decal's size; its y how deep it reaches"),
                 MOLTEN_FIELD(color).color(), MOLTEN_FIELD(opacity).range(0, 1),
                 MOLTEN_FIELD(darkens).help("Multiplies what is under it, as dirt, cracks and tire marks do. Off: painted over it"))
MOLTEN_COMPONENT(Boat, MOLTEN_FIELD(acceleration).range(0, 40).help("Meters per second, per second, at full throttle"),
                 MOLTEN_FIELD(topSpeed).range(0, 80), MOLTEN_FIELD(turnSpeed).range(0, 180).help("Degrees per second at full rudder"),
                 MOLTEN_FIELD(reverse).range(0, 1).help("Of the acceleration, going backwards"),
                 MOLTEN_FIELD(usesInput).help("Driven by the move actions. Off: by throttle and steer, from code"),
                 MOLTEN_FIELD(throttle).range(-1, 1), MOLTEN_FIELD(steer).range(-1, 1),
                 MOLTEN_FIELD(sound).picks("sound").help("Its engine: a sound in Assets that loops, louder and higher with the throttle. Empty: silent"))
MOLTEN_COMPONENT(AudioSource, MOLTEN_FIELD(clip).picks("sound").help("A sound in Assets, like Audio/music.wav"), MOLTEN_FIELD(volume).range(0, 2), MOLTEN_FIELD(pitch).range(0.1f, 4),
                 MOLTEN_FIELD(loops), MOLTEN_FIELD(playsOnStart),
                 MOLTEN_FIELD(group).choices({"Effects", "Music", "Voices", "Interface"}).help("Which volume it follows"),
                 MOLTEN_FIELD(comesFromHere).help("Heard from where it is: louder near it, from its side, muffled behind walls"),
                 MOLTEN_FIELD(fullWithin).range(0, 100).help("Meters within which it is at full volume"),
                 MOLTEN_FIELD(silentBeyond).range(1, 500).help("Meters beyond which it is not heard"))
MOLTEN_COMPONENT(Flicker, MOLTEN_FIELD(amount).range(0, 1), MOLTEN_FIELD(speed))

}  // namespace molten::kit
