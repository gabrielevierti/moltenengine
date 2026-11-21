// Navigation.hpp
// Finding the way: where the ground can be walked on, and a path across it round what is in
// the way.
//
//   NavigationArea   a rectangle of the world, looked at once when the game starts: a grid of
//                    cells, each walkable or not (ground under it, not too steep, room above it
//                    for someone of the agents' size), and the height of its ground
//   NavAgent         walks to where it is sent (an entity, or a point) along the shortest path,
//                    with its CharacterController if it has one
//
// One area serves every agent. Walls, crates and buildings are whatever has a Collider; a body
// that moves (a Rigidbody) is not counted as an obstacle, since it will not stay where it was.
#pragma once

#include "Core/World.hpp"

#include <string>
#include <vector>

namespace molten::kit {

struct NavigationArea {
    Vec2 size{60, 60};                  // meters along x and z, centered on the entity
    float cellSize = 0.5f;
    float agentRadius = 0.35f;
    float agentHeight = 1.8f;
    float stepHeight = 0.4f;            // ground that rises less than this from one cell to the next is walked up
    float maxSlope = 45.0f;             // degrees
    float above = 30.0f, below = 30.0f; // how far above and below the entity ground is looked for

    void update(World& world, Entity self, float dt);

    /// Looks at the world again: after walls have moved, or doors have opened.
    void bake(World& world, Entity self);
    /// The way from one point to another, as points on the ground (the first is `from` brought
    /// to the ground, the last `to`'s nearest walkable point). Empty when there is none.
    std::vector<Vec3> findPath(Vec3 from, Vec3 to) const;
    /// The walkable point nearest `point`, if there is one within a few cells.
    bool nearestWalkable(Vec3 point, Vec3& found) const;
    bool isWalkable(Vec3 point) const;

    // ---- what the bake found
    bool isBaked = false;
    int columns = 0, rows = 0;
    Vec3 origin{0, 0, 0};               // the corner of cell (0, 0)
    std::vector<uint8_t> walkable;      // row by row
    std::vector<float> heights;         // of the ground in each cell

    int cellOf(Vec3 point) const;       // -1 outside
    Vec3 centerOf(int cell) const;
    bool canStep(int from, int to) const;
    bool isLineWalkable(Vec3 from, Vec3 to) const;
};

struct NavAgent {
    std::string target;                 // an entity to go to (and to follow as it moves); empty: the point set by goTo
    float speed = 3.5f;
    float turnSpeed = 10.0f;
    float stoppingDistance = 0.6f;      // near enough
    float repathEvery = 0.5f;           // seconds between paths to a target that moves

    void fixedUpdate(World& world, Entity self, float dt);

    /// Sends it to a point.
    /// Sends it to a point. A point near the one it is already going to does not make a new path
    /// at once (it is made again every `repathEvery`): calling this each frame costs nothing.
    void goTo(Vec3 point) {
        bool isNew = !hasDestination || simd::length(point - destination) > 0.5f;
        destination = point;
        hasDestination = true;
        if (isNew) sincePath = 1e9f;
    }
    void stop() { hasDestination = false, path.clear(); }
    bool hasArrived() const { return arrived; }

    // ---- state
    Vec3 destination{0, 0, 0};
    bool hasDestination = false;
    bool arrived = false;
    std::vector<Vec3> path;
    size_t next = 0;
    float sincePath = 1e9f;
    float fall = 0;
    float speedNow = 0;
};

/// A guard, as a small state machine on a NavAgent: it walks its round, chases the player when
/// it sees them (near enough, in front of it, nothing in between), looks where it last saw
/// them when they get away, and goes back to its round when it gives up. Something to start
/// enemies from; its state is there for an Animator or a game's code to follow.
struct Guard {
    std::string round = "Patrol Point";  // its round: "Patrol Point 1", "Patrol Point 2"... in order
    std::string player = "Player";
    float sightDistance = 12.0f;
    float sightAngle = 120.0f;          // degrees, the whole width of what it sees ahead
    float hearingDistance = 2.5f;       // this near it notices the player wherever it faces
    float walkSpeed = 1.8f, chaseSpeed = 4.2f;
    float giveUpAfter = 4.0f;           // seconds of looking where the player was last seen
    float waitAtPoints = 1.0f;          // seconds at each point of its round

    void fixedUpdate(World& world, Entity self, float dt);

    enum class State { Patrol, Chase, Search, Return };
    State state = State::Patrol;
    int point = 1;                      // the point of its round it is going to
    float timer = 0;
    Vec3 lastSeen{0, 0, 0};
    bool seesPlayer = false;
    /// The state's name: "Patrol", "Chase", "Search", "Return".
    const char* stateName() const;
};

/// The scene's NavigationArea, if it has one, and the entity it is on.
NavigationArea* navigationAreaOf(World& world, Entity* on = nullptr);

void linkNavigation();

}  // namespace molten::kit
