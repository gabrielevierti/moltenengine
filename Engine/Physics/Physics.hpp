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
    /// scale is its size. For things that stand still: ground, tracks, buildings.
    std::string meshName;
    std::shared_ptr<const MeshShape> mesh;      // read from that file when the game runs
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

class Physics {
public:
    Vec3 gravity{0, -9.81f, 0};
    int solverIterations = 6;

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
    }

private:
    struct Proxy {
        Entity entity;
        Collider* collider = nullptr;
        RigidBody* body = nullptr;
        ShapeInstance shape;
        AABB bounds;
        float inverseMass = 0;
        Vec3 inverseInertia{0, 0, 0};   // about its own axes; zero does not turn
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
};

}  // namespace molten
