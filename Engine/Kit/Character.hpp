// Character.hpp
// Someone who walks: a capsule that is moved, not pushed, and slides along what it meets,
// climbs steps, stands on slopes it can walk and slides off the ones it cannot.
//
//   CharacterController   the capsule and its move(): what any character uses, a player or not
//   CharacterMover        a player's character: walks and runs where the input says, seen from
//                         the camera, jumps, falls, turns to face where it goes
//   OrbitCamera           a camera behind it, turned with the mouse or the right stick, that
//                         comes closer rather than go through a wall
//
// The entity's position is the character's feet. It needs no Collider of its own: the capsule
// is the controller's. (Give it a trigger Collider to be seen by triggers.)
#pragma once

#include "Core/World.hpp"

#include <string>

namespace molten::kit {

struct CharacterController {
    float radius = 0.35f;
    float height = 1.8f;                // from the feet to the top of the head
    float stepHeight = 0.35f;           // stairs and curbs up to this are walked up
    float slopeLimit = 50.0f;           // degrees: steeper ground is slid down, not stood on
    float pushStrength = 1.0f;          // how hard it shoves bodies that move (crates, balls)

    /// Moves the character by `displacement` (meters, in the world), sliding along walls and
    /// up steps. Returns how far it really went.
    Vec3 move(World& world, Entity self, Vec3 displacement);

    // ---- state
    bool isGrounded = false;            // standing on something walkable, after the last move
    Vec3 groundNormal{0, 1, 0};
    Vec3 lastMove{0, 0, 0};             // what the last move achieved
    bool wasGrounded = false;
};

struct CharacterMover {
    float walkSpeed = 2.2f;             // meters per second
    float runSpeed = 5.5f;              // with the action "run" held (Shift), or always with `alwaysRuns`
    bool alwaysRuns = false;
    float acceleration = 18.0f;         // meters per second, per second: how quickly it gets going and stops
    float airControl = 0.35f;           // of that, while in the air
    float jumpHeight = 1.1f;            // meters; 0: cannot jump
    float gravity = 22.0f;              // meters per second, per second: a little more than the Earth's, as games feel right
    float turnSpeed = 12.0f;            // how quickly it turns to face where it goes
    std::string camera;                 // the camera whose view "forward" is; empty: the active one

    void update(World& world, Entity self, float dt);
    void fixedUpdate(World& world, Entity self, float dt);

    // ---- state, also for an Animator to follow
    Vec3 velocity{0, 0, 0};
    float speedNow = 0;                 // meters per second along the ground
    bool isGrounded = false;
    bool justJumped = false;
    bool wantsJump = false;
    float sinceGrounded = 0;            // a jump pressed just after walking off an edge still counts
    Entity animated;                    // its Animator, found once
};

struct OrbitCamera {
    std::string target = "Player";      // the entity it looks at
    float distance = 4.5f;
    float height = 1.5f;                // above the target's feet: where it looks
    float yaw = 0.0f, pitch = 15.0f;    // degrees: round the target, and down at it
    float minPitch = -25.0f, maxPitch = 70.0f;
    float mouseSensitivity = 0.25f;     // degrees per point the mouse moves (with a button held)
    float stickSpeed = 160.0f;          // degrees per second with the right stick pushed all the way
    bool avoidsWalls = true;            // closer to the target instead of behind a wall
    float smoothness = 14.0f;

    void lateUpdate(World& world, Entity self, float dt);

    float shownDistance = -1;           // what it is at now, after walls
};

/// Keeps this file's components in a program that links the kit statically.
void linkCharacters();

}  // namespace molten::kit
