// Physics.hpp
// Arcade rigid-body physics: the kind most games want.
//
// What it does: spheres, oriented boxes and capsules; gravity, bouncing and friction;
// static, kinematic (moved by the game) and dynamic (moved by forces) bodies; triggers,
// collision layers, raycasts and overlap queries; a fixed time step with interpolation.
//
// What it leaves out, on purpose: bodies do not spin from impacts (rotation stays under
// the game's control), there are no joints, and fast objects can pass through thin ones.
// Those are the hard parts of a physics engine; when a game needs them, a dedicated
// library is the right tool.
//
// Entities that take part in physics must not have a parent: their Transform is
// read and written as a world-space position.
#pragma once

#include <string>

#include "Physics/Collision.hpp"
#include "Scene/Scene.hpp"

#include <array>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace molten {

enum class BodyType : uint8_t {
    Static,         // never moves
    Kinematic,      // moves with its velocity, is not pushed by anything: platforms, doors
    Dynamic,        // moved by gravity and collisions
};

struct RigidBody {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float restitution = 0.1f;       // 0 does not bounce, 1 bounces back to the same height
    float friction = 0.6f;
    float linearDamping = 0.05f;    // air resistance
    float gravityScale = 1.0f;
    Vec3 velocity{0, 0, 0};
    /// Turns when it is hit off-center, rolls, tumbles. Off, it keeps the way it faces
    /// whatever happens to it: what a character that walks wants, and what bodies did
    /// before they could turn.
    bool rotates = false;
    float angularDamping = 0.08f;   // how quickly it stops turning by itself
    Vec3 angularVelocity{0, 0, 0};  // world axes, radians per second

    // Maintained by Physics. Read them freely; change position only through Physics::teleport.
    Vec3 position{0, 0, 0};
    Vec3 previousPosition{0, 0, 0};
    bool isGrounded = false;        // resting on something walkable this step
    Vec3 groundNormal{0, 1, 0};
    bool isInitialized = false;
    Vec3 writtenPosition{0, 0, 0};  // what physics last put in the Transform: anything else there was put by someone else
    Quat rotation{}, previousRotation{}, writtenRotation{};

    /// A push at a point of the body, in world space: it moves it and, off-center, turns it.
    void applyImpulse(Vec3 impulse, Vec3 worldPoint);
};

/// Ties a body to another, or to the world: a door on its hinges, a lamp on its chain, a
/// trailer on its hook, the links of a rope. On an entity with a RigidBody.
struct Joint {
    std::string connectedTo;            // the name of the other entity; empty ties it to where it is in the world
    std::string kind = "ball";          // "ball": joined at a point, free to turn. "hinge": turns about one axis only.
                                        // "fixed": welded. "distance": held at a length, as by a rope or a rod
    Vec3 anchor{0, 0, 0};               // where on this body, in its own axes
    Vec3 axis{0, 1, 0};                 // hinge: the axis, in this body's axes
    float length = 0;                   // distance: the length; 0 keeps what there is when the game starts
    bool isRope = false;                // distance: only pulls, as a rope; a rod also pushes
    float breakForce = 0;               // above zero: it gives way when pulled harder than this, newtons
    float swingLimit = 0;               // ball: the most it bends from how it started, degrees; 0 as far as it likes (a ragdoll's knees, a chain)

    // Worked out when the game starts.
    bool isSet = false, isBroken = false;
    Entity other;
    Vec3 otherAnchor{0, 0, 0};          // on the other body, in its axes; or the point of the world
    Quat restRelative{};                // how the two were turned to each other
    Vec3 otherAxis{0, 1, 0};
};

/// A shape for collision detection. Without a RigidBody on the same entity it is a
/// static obstacle. Sizes are in world units and ignore Transform::scale.
struct Collider {
    ColliderShape shape = ColliderShape::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float halfHeight = 0.5f;
    Vec3 offset{0, 0, 0};           // from the entity's position, in its local space
    /// Mesh: the model whose triangles it is ("Models/hills.obj", "cube"...). The entity's
    /// scale is its size. As it is, for things that stand still: ground, tracks, buildings.
    std::string meshName;
    /// Mesh: wrapped as a convex shape instead (no hollows, no holes), which can move with a
    /// Rigidbody, fall, roll and be hit: a car's body, a rock, a crate of an odd shape.
    bool isConvex = false;
    std::shared_ptr<const MeshShape> mesh;      // read from that file when the game runs
    std::shared_ptr<const ConvexShape> convex;  // the same, wrapped, when it is convex
    Vec3 meshScale{0, 0, 0};                    // the scale it was read at
    bool isTrigger = false;         // detects overlaps but does not block
    uint32_t layer = 1;             // the group this collider belongs to, as a bit
    uint32_t mask = 0xFFFFFFFFu;    // the groups it interacts with

    static Collider meshOf(std::string name) {
        Collider c;
        c.shape = ColliderShape::Mesh;
        c.meshName = std::move(name);
        return c;
    }
    static Collider sphere(float radius) {
        Collider c;
        c.shape = ColliderShape::Sphere;
        c.radius = radius;
        return c;
    }
    static Collider box(Vec3 halfExtents) {
        Collider c;
        c.shape = ColliderShape::Box;
        c.halfExtents = halfExtents;
        return c;
    }
    static Collider capsule(float radius, float halfHeight) {
        Collider c;
        c.shape = ColliderShape::Capsule;
        c.radius = radius;
        c.halfHeight = halfHeight;
        return c;
    }
};

struct Contact {
    Entity a, b;
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};           // from a to b
    float depth = 0;
    float impactSpeed = 0;          // how fast the two were approaching
    float share = 1;                // of the pair's contact this point is: boxes touch at several
};

struct RaycastHit {
    Entity entity;
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};
    float distance = 0;
};

/// Where a collider without a body of its own stands in the world (under whatever it is under):
/// its place and its turn, without the scale. For code that tests against colliders itself.
void worldPlace(const Transform& transform, Vec3& position, Quat& rotation);

class Physics {
public:
    Vec3 gravity{0, -9.81f, 0};
    int solverIterations = 6;
    /// Which layers collide with which: bit b of layerMeets[a] says things on layer a meet things on
    /// layer b. Everything meets everything until the project says otherwise (GameSettings). A
    /// collider's own mask still counts: both must agree.
    std::array<uint32_t, 32> layerMeets = allLayersMeet();
    static std::array<uint32_t, 32> allLayersMeet() {
        std::array<uint32_t, 32> all;
        all.fill(0xFFFFFFFFu);
        return all;
    }

    /// A collider as a shape placed in the world: for code that tests against colliders itself.
    static ShapeInstance makeShape(const Collider& collider, Vec3 position, Quat rotation);

    /// Two solid colliders started touching.
    std::function<void(const Contact&)> onContactBegin;
    /// Something entered or left a trigger. The trigger is the first argument.
    std::function<void(Entity trigger, Entity other)> onTriggerEnter;
    std::function<void(Entity trigger, Entity other)> onTriggerExit;

    /// Advances the simulation by `dt` seconds. Call with a fixed dt.
    void step(Scene& scene, float dt);

    /// Writes body positions into Transforms, blended between the last two steps.
    /// `alpha` is Time::alpha.
    void writeTransforms(Scene& scene, float alpha);

    /// Moves a body instantly, without sweeping through what is in between.
    void teleport(Scene& scene, Entity entity, Vec3 position);

    /// The closest collider hit by the ray. Triggers are ignored.
    std::optional<RaycastHit> raycast(Scene& scene, const Ray& ray, float maxDistance = 1e9f,
                                      uint32_t mask = 0xFFFFFFFFu, Entity ignore = Entity{});

    /// Every entity with a collider that touches the sphere.
    std::vector<Entity> overlapSphere(Scene& scene, Vec3 center, float radius, uint32_t mask = 0xFFFFFFFFu);

    /// The solid contacts found by the last step.
    const std::vector<Contact>& contacts() const { return contacts_; }

    /// The contacts that began and the trigger overlaps (trigger first) that started since
    /// the lists were last taken. The same events the callbacks above announce, for code
    /// that would rather ask than be called.
    std::vector<Contact> takeBegunContacts() { return std::exchange(begunContacts_, {}); }
    std::vector<std::pair<Entity, Entity>> takeEnteredTriggers() { return std::exchange(enteredTriggers_, {}); }

    /// Forgets every contact and trigger overlap. Call it after Scene::clear(), so that
    /// no "exit" event is sent for entities that no longer exist.
    void reset() {
        contacts_.clear();
        begunContacts_.clear();
        enteredTriggers_.clear();
        contactProxies_.clear();
        touching_.clear();
        triggering_.clear();
        lastPlaces_.clear();
    }

    /// Every solid shape whose box overlaps `box`, except the one ignored and everything under
    /// it: `visit(entity, shape)`. What a body that moves by itself (a vehicle) collides with.
    template <typename Visit>
    void forEachShapeNear(Scene& scene, const AABB& box, Entity ignore, Visit&& visit) {
        prepareRayShapes(scene);
        for (const RayShape& cached : rayShapes_) {
            if (cached.entity == ignore || !scene.isAlive(cached.entity)) continue;
            if (cached.high.x < box.min.x || cached.low.x > box.max.x || cached.high.y < box.min.y || cached.low.y > box.max.y ||
                cached.high.z < box.min.z || cached.low.z > box.max.z) continue;
            if (ignore.isValid() && isUnder(scene, cached.entity, ignore)) continue;
            visit(cached.entity, cached.shape);
        }
    }
    static bool isUnder(Scene& scene, Entity entity, Entity ancestor);

    /// Forgets the shapes raycasts are tested against: after moving colliders by hand without
    /// a step or an update of the transforms in between.
    void forgetShapes() { rayShapesReady_ = false; }

private:
    // What raycasts test against, worked out once and kept until something may have moved
    // (a step, an update of the transforms, a collider more or less): a ray first meets each
    // shape's box, which is cheap, and only then the shape itself.
    struct RayShape {
        Entity entity;
        ShapeInstance shape;
        Vec3 low, high;
        uint32_t layer = 0;
    };
    std::vector<RayShape> rayShapes_;
    bool rayShapesReady_ = false;
    uint32_t rayShapesStamp_ = 0;
    size_t rayShapesColliders_ = 0;
    void prepareRayShapes(Scene& scene);
    struct Proxy {
        Entity entity;
        Collider* collider = nullptr;
        RigidBody* body = nullptr;
        ShapeInstance shape;
        AABB bounds;
        float inverseMass = 0;
        Vec3 inverseInertia{0, 0, 0};   // about its own axes; zero does not turn
        bool movedItself = false;       // no body, yet not where it was at the last step: a character, a thing a script or an animation moves
    };
    void solveJoints(Scene& scene, float dt);

    std::vector<Contact> begunContacts_;
    std::vector<std::pair<Entity, Entity>> enteredTriggers_;

    static uint64_t pairKey(Entity a, Entity b);

    std::vector<Proxy> proxies_;
    std::vector<uint32_t> order_;
    std::vector<Contact> contacts_;
    std::vector<std::pair<uint32_t, uint32_t>> contactProxies_;
    std::unordered_map<uint64_t, std::pair<Entity, Entity>> touching_, triggering_;
    /// Where each collider that has no body was at the last step, by entity: one that is somewhere
    /// else now moved by other means (a CharacterController, a script, an animation), and triggers
    /// must hear of it as they hear of bodies.
    std::unordered_map<uint64_t, Vec3> lastPlaces_;
};

}  // namespace molten
