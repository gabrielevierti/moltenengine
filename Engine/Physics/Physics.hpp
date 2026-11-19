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

#include "Physics/Collision.hpp"
#include "Scene/Scene.hpp"

#include <functional>
#include <optional>
#include <unordered_map>
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

    // Maintained by Physics. Read them freely; change position only through Physics::teleport.
    Vec3 position{0, 0, 0};
    Vec3 previousPosition{0, 0, 0};
    bool isGrounded = false;        // resting on something walkable this step
    Vec3 groundNormal{0, 1, 0};
    bool isInitialized = false;
};

/// A shape for collision detection. Without a RigidBody on the same entity it is a
/// static obstacle. Sizes are in world units and ignore Transform::scale.
struct Collider {
    ColliderShape shape = ColliderShape::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float halfHeight = 0.5f;
    Vec3 offset{0, 0, 0};           // from the entity's position, in its local space
    bool isTrigger = false;         // detects overlaps but does not block
    uint32_t layer = 1;             // the group this collider belongs to, as a bit
    uint32_t mask = 0xFFFFFFFFu;    // the groups it interacts with

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

    /// Forgets every contact and trigger overlap. Call it after Scene::clear(), so that
    /// no "exit" event is sent for entities that no longer exist.
    void reset() {
        contacts_.clear();
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
    };

    static ShapeInstance makeShape(const Collider& collider, Vec3 position, Quat rotation);
    static uint64_t pairKey(Entity a, Entity b);

    std::vector<Proxy> proxies_;
    std::vector<uint32_t> order_;
    std::vector<Contact> contacts_;
    std::vector<std::pair<uint32_t, uint32_t>> contactProxies_;
    std::unordered_map<uint64_t, std::pair<Entity, Entity>> touching_, triggering_;
};

}  // namespace molten
