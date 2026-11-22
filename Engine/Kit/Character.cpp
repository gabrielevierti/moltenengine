#include "Kit/Character.hpp"

#include "Animation/Animator.hpp"
#include "Game/Component.hpp"
#include "Physics/Collision.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

void linkCharacters() {}

namespace {

ShapeInstance capsuleAt(const CharacterController& controller, Vec3 feet) {
    ShapeInstance shape;
    shape.shape = ColliderShape::Capsule;
    shape.radius = std::max(controller.radius, 0.05f);
    shape.halfHeight = std::max(controller.height * 0.5f - shape.radius, 0.01f);
    shape.center = feet + Vec3{0, std::max(controller.height * 0.5f, shape.radius), 0};
    return shape;
}

struct Resolved {
    bool isGrounded = false;
    Vec3 groundNormal{0, 1, 0};
};

/// Pushes the capsule at `feet` out of everything solid near it. Walkable ground pushes it
/// straight up (so that it stands on a slope instead of sliding down it); everything else
/// pushes it out along the contact. Bodies that move are shoved aside.
/// With `edgesHold`, the edge of a step under the rounded bottom holds it up too (when it is put
/// down onto a step): it rests on the edge, and rises onto the step as it goes on.
Resolved pushOut(World& world, Entity self, const CharacterController& controller, Vec3& feet, float walkable, bool edgesHold = false) {
    Resolved resolved;
    for (int pass = 0; pass < 4; pass++) {
        ShapeInstance me = capsuleAt(controller, feet);
        AABB near = me.bounds();
        near.min -= Vec3{0.02f, 0.02f, 0.02f};
        near.max += Vec3{0.02f, 0.02f, 0.02f};
        bool moved = false;
        world.physics.forEachShapeNear(world.scene, near, self, [&](Entity other, const ShapeInstance& shape) {
            ContactInfo contact;
            if (!collide(me, shape, contact) || contact.depth <= 1e-5f) return;
            Vec3 out = -contact.normal;                         // from it toward the character
            // The edge of a step under the rounded bottom holds it up as walkable ground does,
            // when what is just past the edge is walkable and no higher than a step: it then
            // rises onto the step as it goes on. (A steep slope there is not ground.)
            bool isStepEdge = false;
            if (out.y > 0.05f && out.y <= walkable && contact.point.y < feet.y + controller.stepHeight + 0.01f) {
                Vec3 inward = Vec3{-out.x, 0, -out.z};
                if (simd::length_squared(inward) > 1e-6f) inward = simd::normalize(inward);
                Vec3 from = Vec3{contact.point.x, feet.y + controller.stepHeight + 0.05f, contact.point.z} + inward * 0.03f;
                if (auto below = world.physics.raycast(world.scene, Ray{from, Vec3{0, -1, 0}}, controller.stepHeight + 0.15f, 0xFFFFFFFFu, self)) {
                    isStepEdge = below->normal.y > walkable && below->distance > 0.01f;
                }
            }
            (void)edgesHold;
            if (out.y > walkable || isStepEdge) {
                resolved.isGrounded = true;
                resolved.groundNormal = out.y > walkable ? out : Vec3{0, 1, 0};
                feet.y += std::min(contact.depth / out.y, std::max(contact.depth * 3.0f, controller.stepHeight));
            } else {
                feet += out * contact.depth;
                // A body that can move is shoved, as much as the character presses into it.
                if (RigidBody* body = world.scene.get<RigidBody>(other); body && body->type == BodyType::Dynamic) {
                    Vec3 shove{-out.x, 0, -out.z};
                    body->velocity += shove * (contact.depth * 30.0f * controller.pushStrength / std::max(body->mass, 0.1f));
                }
            }
            me = capsuleAt(controller, feet);
            moved = true;
        });
        if (!moved) break;
    }
    return resolved;
}

/// Moves along `displacement` in short steps (never further than half the radius at once, so
/// that nothing thin is gone through), pushing out after each.
Resolved slide(World& world, Entity self, const CharacterController& controller, Vec3& feet, Vec3 displacement, float walkable, bool edgesHold = false) {
    Resolved total;
    float length = simd::length(displacement);
    int steps = std::clamp(int(std::ceil(length / std::max(controller.radius * 0.5f, 0.02f))), 1, 32);
    for (int i = 0; i < steps; i++) {
        feet += displacement / float(steps);
        Resolved step = pushOut(world, self, controller, feet, walkable, edgesHold);
        if (step.isGrounded) total = step;
    }
    if (length < 1e-6f) total = pushOut(world, self, controller, feet, walkable, edgesHold);
    return total;
}

}  // namespace

Vec3 CharacterController::move(World& world, Entity self, Vec3 displacement) {
    Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return Vec3{0, 0, 0};
    const float walkable = std::cos(radians(std::clamp(slopeLimit, 0.0f, 89.0f)));
    Vec3 start = transform->worldPosition(), feet = start;
    Vec3 flat{displacement.x, 0, displacement.z};
    float wanted = simd::length(flat);

    Resolved where = slide(world, self, *this, feet, displacement, walkable);

    // Held by something low (a step, a curb) while walking on the ground: tried again from a
    // step higher, and kept if it got further and came down on something to stand on.
    if (wasGrounded && wanted > 1e-4f && stepHeight > 0) {
        Vec3 along = flat / wanted;
        float got = simd::dot(feet - start, along);
        if (got < wanted * 0.6f) {
            Vec3 high = start;
            slide(world, self, *this, high, Vec3{0, stepHeight, 0}, walkable);
            slide(world, self, *this, high, flat, walkable);
            Resolved landed = slide(world, self, *this, high, Vec3{0, -(stepHeight + 0.05f), 0}, walkable, true);
            if (landed.isGrounded && simd::dot(high - start, along) > got + 0.01f && high.y <= start.y + stepHeight + 0.01f) {
                feet = high;
                where = landed;
            }
        }
    }
    // Walking down a slope or off a step: kept on the ground instead of flying off it, when the
    // ground is within a step below and it is not going up (a jump).
    if (wasGrounded && !where.isGrounded && displacement.y <= 0 && stepHeight > 0) {
        Vec3 low = feet;
        Resolved below = slide(world, self, *this, low, Vec3{0, -stepHeight, 0}, walkable);
        if (below.isGrounded) {
            feet = low;
            where = below;
        }
    }

    isGrounded = where.isGrounded;
    groundNormal = where.groundNormal;
    wasGrounded = isGrounded;
    lastMove = feet - start;
    // The entity's own position, in its parent's space where it has one.
    if (transform->parent.isValid()) {
        if (const Transform* parent = world.scene.get<Transform>(transform->parent)) {
            Vec3 where, size;
            Quat turned;
            decompose(parent->world, where, turned, size);
            transform->position += rotate(simd::conjugate(turned), lastMove) / size;
        }
    } else {
        transform->position = feet;
    }
    world.scene.updateTransforms();
    return lastMove;
}

// ---------------------------------------------------------------- CharacterMover

void CharacterMover::update(World& world, Entity, float) {
    if (jumpHeight > 0 && world.input.wasPressed("jump")) wantsJump = true;
}

void CharacterMover::fixedUpdate(World& world, Entity self, float dt) {
    CharacterController* controller = world.scene.get<CharacterController>(self);
    Transform* transform = world.scene.get<Transform>(self);
    if (!controller || !transform || dt <= 0) return;

    // "Forward" is where the camera looks, along the ground.
    Entity view = camera.empty() ? Entity{} : world.find(camera);
    if (!view.isValid()) {
        world.scene.each<Camera, Transform>([&](Entity entity, Camera& candidate, Transform& place) {
            if (!view.isValid() && candidate.isActive && place.isActiveInHierarchy) view = entity;
        });
    }
    Vec3 forward{0, 0, -1};
    if (const Transform* eye = world.scene.get<Transform>(view)) {
        Vec3 looking = transformDirection(eye->world, Vec3{0, 0, -1});
        looking.y = 0;
        if (simd::length_squared(looking) > 1e-4f) forward = simd::normalize(looking);
    }
    Vec3 right = simd::normalize(simd::cross(forward, Vec3{0, 1, 0}));
    float x = world.input.axis("moveX"), z = world.input.axis("moveZ");     // W is -1 along moveZ
    Vec3 wish = right * x - forward * z;
    if (simd::length(wish) > 1) wish = simd::normalize(wish);
    // Shift (the action "run") is the other pace: running for one that walks, walking for one that always runs.
    bool runs = alwaysRuns != (world.input.isDown("run") || world.input.isDown(Key::LeftShift));
    Vec3 target = wish * (runs ? runSpeed : walkSpeed);

    // Along the ground: toward what is wanted, at the rate it can change.
    float rate = acceleration * (isGrounded ? 1.0f : airControl) * dt;
    Vec3 flat{velocity.x, 0, velocity.z};
    Vec3 change = target - flat;
    float changeLength = simd::length(change);
    if (changeLength > rate) change *= rate / changeLength;
    velocity.x += change.x;
    velocity.z += change.z;

    // Up and down: pressed to the ground while on it; a jump; gravity.
    sinceGrounded = isGrounded ? 0.0f : sinceGrounded + dt;
    justJumped = false;
    if (wantsJump && (isGrounded || sinceGrounded < 0.12f) && jumpHeight > 0) {
        velocity.y = std::sqrt(2.0f * gravity * jumpHeight);
        justJumped = true;
        sinceGrounded = 1.0f;
    } else if (isGrounded && velocity.y <= 0) {
        velocity.y = -2.0f;
    } else {
        velocity.y -= gravity * dt;
    }
    wantsJump = false;

    Vec3 moved = controller->move(world, self, velocity * dt);
    isGrounded = controller->isGrounded && !justJumped;
    if (velocity.y > 0 && moved.y < velocity.y * dt * 0.5f) velocity.y = 0;   // a ceiling
    if (isGrounded && velocity.y < 0) velocity.y = 0;
    speedNow = std::sqrt(moved.x * moved.x + moved.z * moved.z) / dt;

    // Its Animator (on it or on its model, under it) is told how it moves: the parameters
    // "speed", "grounded", "verticalSpeed" and the trigger "jump", for its controller's transitions.
    if (!world.scene.isAlive(animated) || !world.scene.get<Animator>(animated)) {
        animated = Entity{};
        world.scene.each<Animator>([&](Entity entity, Animator&) {
            if (!animated.isValid() && (entity == self || Physics::isUnder(world.scene, entity, self))) animated = entity;
        });
    }
    if (Animator* animator = world.scene.get<Animator>(animated); animator && !animator->controller.empty()) {
        animator->setFloat("speed", speedNow);
        animator->setBool("grounded", isGrounded);
        animator->setFloat("verticalSpeed", velocity.y);
        if (justJumped) animator->setTrigger("jump");
    }

    // Facing where it goes.
    if (turnsToFace && simd::length_squared(wish) > 0.01f) {
        Quat wanted = lookRotation(wish);
        transform->rotation = simd::normalize(simd::slerp(transform->rotation, wanted, std::min(turnSpeed * dt, 1.0f)));
    }
}

// ---------------------------------------------------------------- OrbitCamera

void OrbitCamera::lateUpdate(World& world, Entity self, float dt) {
    Transform* transform = world.scene.get<Transform>(self);
    Entity followed = world.find(target);
    const Transform* subject = world.scene.get<Transform>(followed);
    if (!transform || !subject) return;
    // Turned by the mouse (dragging, or all the time once the game has the pointer), or with the right stick.
    bool turns = capturesMouse && mouseTurnsView(world, askedForPointer);
    const Input& input = world.input;
    if (turns || input.isDown(MouseButton::Left) || input.isDown(MouseButton::Right)) {
        Vec2 delta = input.mouseDelta();
        yaw -= delta.x * mouseSensitivity;
        pitch += delta.y * mouseSensitivity;
    }
    yaw -= input.axis(GamepadAxis::RightX) * stickSpeed * dt;
    pitch -= input.axis(GamepadAxis::RightY) * stickSpeed * dt;
    pitch = std::clamp(pitch, minPitch, maxPitch);

    Vec3 focus = subject->worldPosition() + Vec3{0, height, 0};
    float y = radians(yaw), p = radians(pitch);
    Vec3 back{std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
    float wanted = distance;
    if (avoidsWalls) {
        if (auto hit = world.physics.raycast(world.scene, Ray{focus, back}, distance, 0xFFFFFFFFu, followed)) {
            if (!Physics::isUnder(world.scene, hit->entity, followed)) wanted = std::max(hit->distance - 0.25f, 0.3f);
        }
    }
    // Closer at once (never through the wall), farther again gently.
    if (shownDistance < 0 || wanted < shownDistance) shownDistance = wanted;
    else shownDistance += (wanted - shownDistance) * (1.0f - std::exp(-smoothness * 0.3f * dt));
    Vec3 eye = focus + back * shownDistance;
    if (transform->parent.isValid()) return;            // placed in the world, not under something
    transform->position = eye;
    transform->rotation = lookRotation(focus - eye);
}

// ---------------------------------------------------------------- FirstPersonLook

bool mouseTurnsView(World& world, bool& asked) {
    Input& input = world.input;
    if (!asked) {
        asked = true;
        input.setPointerCaptured(true);
    } else if (input.wasPressed("pause")) {
        input.setPointerCaptured(false);
    } else if (!input.wantsPointerCaptured() && input.wasPressed(MouseButton::Left)) {
        input.setPointerCaptured(true);
    }
    return input.wantsPointerCaptured();
}

void FirstPersonLook::lateUpdate(World& world, Entity self, float dt) {
    Transform* eye = world.scene.get<Transform>(self);
    if (!eye) return;
    Transform* body = world.scene.get<Transform>(eye->parent);
    if (!hasStarted) {
        hasStarted = true;
        yaw = degrees(toEulerAngles(body ? body->rotation : eye->rotation).y);
    }
    const Input& input = world.input;
    bool turns = takesPointer ? mouseTurnsView(world, askedForPointer) : input.isDown(MouseButton::Left) || input.isDown(MouseButton::Right);
    if (turns) {
        Vec2 delta = input.mouseDelta();
        yaw -= delta.x * mouseSensitivity;
        pitch -= delta.y * mouseSensitivity;
    }
    yaw -= input.axis(GamepadAxis::RightX) * stickSpeed * dt;
    pitch += input.axis(GamepadAxis::RightY) * stickSpeed * dt;
    pitch = std::clamp(pitch, minPitch, maxPitch);
    yaw = std::fmod(yaw, 360.0f);
    // A kick is taken back quickly at first, then gently.
    float back = std::exp(-9.0f * dt);
    kickPitch *= back, kickYaw *= back;
    Quat aside(radians(yaw + kickYaw), Vec3{0, 1, 0}), up(radians(std::clamp(pitch + kickPitch, -89.0f, 89.0f)), Vec3{1, 0, 0});
    if (body) {
        body->rotation = aside;
        eye->rotation = up;
    } else {
        eye->rotation = aside * up;
    }
}

}  // namespace molten::kit

namespace molten::kit {

MOLTEN_COMPONENT(FirstPersonLook, MOLTEN_FIELD(mouseSensitivity).range(0, 2).help("Degrees for each point the mouse moves"),
                 MOLTEN_FIELD(stickSpeed).range(0, 720).help("Degrees a second with the right stick pushed all the way"), MOLTEN_FIELD(minPitch).range(-89, 0),
                 MOLTEN_FIELD(maxPitch).range(0, 89),
                 MOLTEN_FIELD(takesPointer).help("The mouse turns it without a button held: Esc gives the pointer back, a click takes it again"))
MOLTEN_COMPONENT(CharacterController, MOLTEN_FIELD(radius).range(0.05f, 3), MOLTEN_FIELD(height).range(0.2f, 6),
                 MOLTEN_FIELD(stepHeight).range(0, 1).help("Steps and curbs up to this are walked up"),
                 MOLTEN_FIELD(slopeLimit).range(0, 89).help("Degrees: steeper ground is slid down, not stood on"),
                 MOLTEN_FIELD(pushStrength).range(0, 10).help("How hard it shoves bodies that move"))
MOLTEN_COMPONENT(CharacterMover, MOLTEN_FIELD(walkSpeed).range(0, 20), MOLTEN_FIELD(runSpeed).range(0, 30).help("With the action \"run\" or Shift held"),
                 MOLTEN_FIELD(alwaysRuns).help("It runs, and Shift walks"), MOLTEN_FIELD(acceleration).range(0, 100), MOLTEN_FIELD(airControl).range(0, 1),
                 MOLTEN_FIELD(jumpHeight).range(0, 10), MOLTEN_FIELD(gravity).range(0, 100), MOLTEN_FIELD(turnSpeed).range(0, 40),
                 MOLTEN_FIELD(turnsToFace).help("Off for a body seen from its own eyes, which its view turns"),
                 MOLTEN_FIELD(camera).picks("entity").help("The camera whose view sets forward; empty for the active one"))
MOLTEN_COMPONENT(OrbitCamera, MOLTEN_FIELD(target).picks("entity"), MOLTEN_FIELD(distance).range(0.5f, 50), MOLTEN_FIELD(height).range(-5, 10),
                 MOLTEN_FIELD(yaw).range(-360, 360), MOLTEN_FIELD(pitch).range(-89, 89), MOLTEN_FIELD(minPitch).range(-89, 89),
                 MOLTEN_FIELD(maxPitch).range(-89, 89), MOLTEN_FIELD(mouseSensitivity).range(0, 2), MOLTEN_FIELD(stickSpeed).range(0, 720),
                 MOLTEN_FIELD(avoidsWalls), MOLTEN_FIELD(smoothness).range(0, 40),
                 MOLTEN_FIELD(capturesMouse).help("The mouse turns it without a button held: Esc gives the pointer back, a click takes it again"))

}  // namespace molten::kit
