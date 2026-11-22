// Shooting.hpp
// What a game with a gun in it needs: the gun, things to shoot at, and things that come for
// whoever holds it.
//
//   Weapon           a gun held before a first-person view (under its camera): it shoots where
//                    the view looks, from a magazine that empties and is loaded again
//   ShootingTarget   a plate that falls when it is shot, and stands up again if asked to
//   Hunter           a machine that hovers, looks for the player, flies at them and hurts them
//   Supply           rounds, or health, to pick up
//   DamageFlash      the edges of the screen go red when a shared value (health) drops
//
// A shot is a ray, not a thing that flies: it lands at once on the first solid Collider in its
// way. What is struck is told so with the message "shot" (heard in onMessage, or by a visual
// script's On Message: the number it carries is the damage), and what it is, or is part of,
// loses Health if it has any.
#pragma once

#include "Core/World.hpp"

#include <string>

namespace molten {
class Canvas;
}

namespace molten::kit {

struct Weapon {
    // ---- what it is
    float damage = 1.0f;                // what a hit takes from the Health of what is hit
    float range = 150.0f;               // meters
    float fireRate = 10.0f;             // shots a second
    bool isAutomatic = true;            // goes on firing while the action "fire" is held; off: one shot each press
    int magazineSize = 30;
    int reserve = 90;                   // rounds carried besides those in it; below zero: no end of them
    float reloadTime = 1.45f;           // seconds
    float spread = 0.25f;               // degrees a shot can stray, standing still
    float movingSpread = 1.5f;          // and at a run
    float kick = 0.9f;                  // degrees the view jumps at each shot (the camera's FirstPersonLook)
    float push = 3.0f;                  // how hard a hit shoves a body that can move
    // ---- what shows and sounds
    std::string muzzle = "Muzzle";      // an entity under it at the end of its barrel: its Light flashes, what is under it shows for a moment
    std::string hitEffect;              // a prefab left where a shot lands
    std::string shotSound, emptySound, reloadSound;
    /// Shared values for the screen: this one is what is in the magazine, this + "Reserve" the
    /// rounds besides, this + "Loading" how far loading is (0 to 1; 0 when not loading).
    std::string counter = "ammo";
    bool showsSights = true;            // draws its sights in the middle of the screen, wider as it strays
    /// It listens only while the game holds the pointer (see Input::setPointerCaptured): after
    /// Esc, the click that takes the pointer back is not also a shot. Off for a game played
    /// with the pointer free.
    bool needsPointer = true;

    void update(World& world, Entity self, float dt);
    void lateUpdate(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);

    /// One shot, if it can: not with an empty magazine, while loading, or too soon after the last.
    bool fire(World& world, Entity self);
    /// Starts loading, if there is room in the magazine and there are rounds to put in it.
    bool reload(World& world, Entity self);
    void addRounds(int rounds) {
        if (reserve >= 0) reserve += rounds;
    }
    bool isLoading() const { return loading > 0; }
    /// How far a shot can stray now, in degrees.
    float strayNow() const { return spread + (movingSpread - spread) * moving + bloom; }

    // ---- state
    int inMagazine = -1;                // -1: full, when the game starts
    float untilNext = 0;                // seconds before it can fire again
    float loading = 0;                  // seconds of loading left
    float sinceShot = 10, sinceHit = 10;
    float bloom = 0;                    // degrees more it strays for having just fired
    float moving = 0;                   // 0 standing to 1 at a run
    int shots = 0, hits = 0;
    Vec3 restPosition{0, 0, 0};         // where it is held, under the camera
    Quat restRotation{};
    bool hasRest = false;
    float bob = 0, recoil = 0, lowered = 0, drawnBack = 0;
    uint32_t chance = 12345;            // for where shots stray: the same every game
};

/// A plate to shoot down: struck by a shot, it falls back about its foot (its entity's own
/// place: put the plate and its post under it) and a shared value goes up; `standsAfter`
/// seconds later it stands again (0: it stays down). Its front looks along its -z.
struct ShootingTarget {
    std::string counter = "score";
    float points = 1;
    float standsAfter = 0;
    float fallAngle = 86;               // degrees
    std::string sound;                  // when it is struck

    void onMessage(World& world, Entity self, const Message& message);
    void update(World& world, Entity self, float dt);

    bool isDown = false;
    float fallen = 0;                   // 0 standing to 1 down
    float downFor = 0;
    Quat upright{};
    bool hasUpright = false;
};

/// A machine that hovers and hunts. It stays about where it was put, turning, until it sees
/// its prey (near enough, nothing solid between); then it flies at them, over what is in its
/// way, and hurts them while it is within reach. Give it a solid Collider and Health, and it
/// can be shot down.
struct Hunter {
    std::string prey = "Player";
    float sightDistance = 24.0f;
    float speed = 3.6f;                 // meters per second
    float hoverHeight = 1.5f;           // above the ground
    float reach = 1.7f;                 // this near, it hurts
    float damage = 8.0f;                // each time
    float every = 0.8f;                 // seconds between two
    float forgetsAfter = 6.0f;          // seconds without seeing its prey before it goes home
    std::string attackSound;

    void fixedUpdate(World& world, Entity self, float dt);

    bool isHunting = false;
    float untilNext = 0, unseenFor = 0, time = 0;
    Vec3 home{0, 0, 0};
    bool hasHome = false;
};

/// Something to pick up, for a trigger collider: who touches it (by name) gets rounds for the
/// Weapon they carry, or Health, and it is gone.
struct Supply {
    std::string collector = "Player";
    int rounds = 30;
    float health = 0;
    float mostHealth = 100;             // health is not filled past this
    std::string sound;

    void onTriggerEnter(World& world, Entity self, Entity other);

    bool isTaken = false;
};

/// The edges of the screen flash when a shared value drops: being hurt.
struct DamageFlash {
    std::string value = "health";
    Vec3 color{0.9f, 0.05f, 0.03f};
    float seconds = 0.6f;
    std::string sound;

    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);

    float last = -1, shown = 0;
};

/// Keeps this file's components in a program that links the kit statically.
void linkShooting();

}  // namespace molten::kit
