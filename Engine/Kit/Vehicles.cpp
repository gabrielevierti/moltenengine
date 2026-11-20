#include "Kit/Vehicles.hpp"

#include "Game/Component.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

namespace {

const float kGravity = 9.81f;
const float kAirDensity = 1.225f;       // kilograms per cubic meter at sea level

float smoothstep01(float t) {
    t = saturate(t);
    return t * t * (3 - 2 * t);
}

/// Moves `value` toward `target` by at most `step`.
float approach(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

/// The child of `parent` with a name, or an invalid entity.
Entity childCalled(Scene& scene, Entity parent, const char* name) {
    Entity found;
    scene.each<Name, Transform>([&](Entity entity, Name& entityName, Transform& transform) {
        if (!found.isValid() && transform.parent == parent && entityName.value == name) found = entity;
    });
    return found;
}

/// Something solid pressing against a point of a body that is not a wheel: a wall, the
/// ground under a car on its roof. A stiff spring out of the surface, and friction along it.
void pressAgainstScene(World& world, Entity self, Body6& body, Vec3 localPoint, float stiffness, float damping, float dt) {
    Vec3 point = body.worldPoint(localPoint);
    Vec3 toPoint = point - body.position;
    float reach = simd::length(toPoint);
    if (reach < 1e-3f) return;
    Ray ray{body.position, toPoint / reach};
    std::optional<RaycastHit> hit = world.physics.raycast(world.scene, ray, reach, 0xFFFFFFFFu, self);
    if (!hit) return;
    float depth = reach - hit->distance;
    Vec3 velocity = body.velocityAt(hit->point);
    float approachSpeed = -simd::dot(velocity, hit->normal);
    float push = std::max(0.0f, depth * stiffness + approachSpeed * damping);
    Vec3 along = velocity + hit->normal * approachSpeed;                 // what is left of the velocity along the surface
    float slide = simd::length(along);
    Vec3 friction = slide > 0.05f ? along * (-std::min(0.6f * push, slide * body.mass * 4.0f) / slide) : Vec3{0, 0, 0};
    body.addForceAt(hit->normal * push + friction, hit->point);
    // What it presses on is pressed back, if it can move: a crate is shoved, a ball rolls away.
    if (RigidBody* other = world.scene.get<RigidBody>(hit->entity); other && other->type == BodyType::Dynamic) {
        other->applyImpulse((hit->normal * push + friction) * -dt, hit->point);
    }
}

// ---------------------------------------------------------------- engines

struct EngineSpec {
    const char* name;
    float idle, redline;                // revolutions per minute
    float peakTorque, peakRpm;          // newton meters, and where
    float lowShare, highShare;          // torque at idle and at the redline, as shares of the peak
    float gears[7];
    int gearCount;
    float finalDrive;
    float reverse;
    float soundRpm;                     // unused: kept for files made before the sound had layers
    bool isElectric;
    float layers[4];                    // the speeds its sound was recorded at, low to high
};

const EngineSpec kEngines[] = {
    // A small four: has to be revved, and likes it.
    {"i4", 850, 6800, 190, 4300, 0.62f, 0.74f, {3.58f, 2.05f, 1.42f, 1.08f, 0.87f, 0.72f}, 6, 4.1f, 3.4f, 3000, false, {1000, 2600, 4400, 6200}},
    // A big eight: torque everywhere, long gears.
    {"v8", 700, 6500, 560, 4400, 0.72f, 0.76f, {2.97f, 2.07f, 1.43f, 1.0f, 0.84f, 0.56f}, 6, 3.42f, 3.0f, 2400, false, {800, 2000, 3700, 5600}},
    // A turbo diesel: all of it early, nothing at the top.
    {"diesel", 800, 4800, 420, 2000, 0.5f, 0.48f, {3.8f, 2.1f, 1.3f, 0.95f, 0.75f, 0.62f}, 6, 3.2f, 3.5f, 2000, false, {900, 1700, 2800, 4200}},
    // A five: an odd number of cylinders and a warble nobody mistakes.
    {"i5", 800, 7000, 320, 4200, 0.66f, 0.74f, {3.6f, 2.1f, 1.45f, 1.1f, 0.88f, 0.72f}, 6, 3.9f, 3.4f, 3000, false, {900, 2500, 4300, 6200}},
    // A straight six: smooth all the way up.
    {"i6", 750, 7200, 400, 4500, 0.68f, 0.78f, {3.5f, 2.05f, 1.4f, 1.05f, 0.85f, 0.7f}, 6, 3.6f, 3.3f, 3000, false, {800, 2400, 4400, 6500}},
    // A twelve: revs forever, and makes its power at the top.
    {"v12", 900, 8500, 690, 6000, 0.6f, 0.82f, {3.1f, 2.1f, 1.55f, 1.2f, 0.97f, 0.8f, 0.67f}, 7, 3.7f, 2.9f, 3500, false, {900, 2800, 5200, 8000}},
    // An electric motor: full torque from nothing, one gear, no idle.
    {"electric", 0, 15000, 430, 5200, 1.0f, 0.35f, {1.0f}, 1, 9.0f, 1.0f, 6000, true, {1500, 4500, 9000, 14000}},
};

const EngineSpec& specOf(const std::string& name) {
    for (const EngineSpec& spec : kEngines) {
        if (name == spec.name) return spec;
    }
    return kEngines[0];
}

float torqueOf(const EngineSpec& spec, float rpm) {
    if (rpm > spec.redline) return 0;                               // the limiter
    if (spec.isElectric) return rpm <= spec.peakRpm ? spec.peakTorque : spec.peakTorque * spec.peakRpm / rpm;   // then constant power
    if (rpm <= spec.peakRpm) {
        float t = smoothstep01((rpm - spec.idle) / std::max(spec.peakRpm - spec.idle, 1.0f));
        return spec.peakTorque * (spec.lowShare + (1 - spec.lowShare) * t);
    }
    float t = (rpm - spec.peakRpm) / std::max(spec.redline - spec.peakRpm, 1.0f);
    return spec.peakTorque * (1 - (1 - spec.highShare) * t * t);
}

// ---------------------------------------------------------------- tires

/// How much of the available friction a tire gives at a slip, where 1 is the slip it grips
/// best at: it rises quickly, peaks, and falls a little as the tire slides (the "magic
/// formula" of tire models, in its simplest form).
float gripAt(float slip) {
    const float c = 1.65f;
    const float b = std::tan(kPi / (2 * c));                        // puts the peak at slip 1
    return std::sin(c * std::atan(b * slip));
}

const float kPeakSlipRatio = 0.1f;      // a tire pulls hardest turning a tenth faster than it rolls
const float kPeakSlipAngle = 0.14f;     // and corners hardest pointed 8 degrees off where it goes

}  // namespace

float engineTorque(const std::string& engine, float rpm) {
    return torqueOf(specOf(engine), rpm);
}

// ---------------------------------------------------------------- Body6

Vec3 Body6::toLocal(Vec3 worldDirection) const {
    return rotate(simd::conjugate(orientation), worldDirection);
}

void Body6::addForceAt(Vec3 worldForce, Vec3 worldPoint) {
    force += worldForce;
    torque += simd::cross(worldPoint - position, worldForce);
}

void Body6::integrate(float dt) {
    velocity += force * (dt / mass);
    // Rotation is worked out in the body's own axes, where its inertia is three numbers.
    Vec3 spin = toLocal(angularVelocity), twist = toLocal(torque);
    Vec3 momentum = spin * inertia;
    spin += (twist - simd::cross(spin, momentum)) / inertia * dt;
    angularVelocity = rotate(orientation, spin);
    position += velocity * dt;
    float angle = simd::length(angularVelocity) * dt;
    if (angle > 1e-7f) orientation = simd::normalize(Quat(angle, angularVelocity / (angle / dt)) * orientation);
    force = torque = Vec3{0, 0, 0};
}

// ---------------------------------------------------------------- finding the parts

float Controls::byName(const std::string& name) const {
    if (name == "throttle") return throttle;
    if (name == "brake") return brake;
    if (name == "handbrake") return handbrake;
    if (name == "steer") return steer;
    if (name == "pitch") return pitch;
    if (name == "roll") return roll;
    if (name == "yaw") return yaw;
    if (name == "flaps") return flaps;
    return 0;
}

Entity vehicleOf(const Scene& scene, Entity part) {
    for (int depth = 0; depth < 16 && part.isValid(); depth++) {
        if (scene.has<VehicleBody>(part)) return part;
        const Transform* transform = scene.get<Transform>(part);
        if (!transform) break;
        part = transform->parent;
    }
    return Entity{};
}

namespace {

/// Where an entity is and how it is turned, in the axes of an entity above it.
void placeIn(const Scene& scene, Entity entity, Entity above, Vec3& position, Quat& rotation) {
    position = Vec3{0, 0, 0};
    rotation = Quat{};
    for (int depth = 0; depth < 16 && entity.isValid() && entity != above; depth++) {
        const Transform* transform = scene.get<Transform>(entity);
        if (!transform) break;
        position = transform->position + rotate(transform->rotation, position * transform->scale);
        rotation = simd::normalize(transform->rotation * rotation);
        entity = transform->parent;
    }
}

struct WheelPart {
    Entity entity;
    Wheel* wheel;
    const Suspension* suspension;
    const DeformableTire* tire;
    // Worked out once a step:
    float travel, springRate, damping, antiRoll, stiffness, grip, rolling;
    Vec3 contactPoint, contactNormal;
};

struct WingPart {
    Wing* wing;
    float control;                      // what its hinges add to its angle, radians
    float drag;
};

/// Every part of a vehicle, found afresh each step: parts can be added and removed while it runs.
struct Parts {
    std::vector<WheelPart> wheels;
    std::vector<WingPart> wings;
    std::vector<Propeller*> propellers;
    std::vector<Bumper*> bumpers;
    std::vector<Hinge*> hinges;
    // Its body's own colliders (on it or under it): what it hits the world with.
    struct BodyShape {
        Entity entity;
        const Collider* collider;
        Vec3 position;                  // where it sits on the vehicle, in its axes
        Quat rotation;
    };
    std::vector<BodyShape> shapes;
    Motor* motor = nullptr;
    DriveControls* drive = nullptr;
    FlightControls* flight = nullptr;
};

Parts gather(Scene& scene, Entity self, const Controls& controls) {
    Parts parts;
    auto isOurs = [&](Entity entity, const Transform& transform) { return transform.isActiveInHierarchy && vehicleOf(scene, entity) == self; };
    scene.each<Wheel, Transform>([&](Entity entity, Wheel& wheel, Transform& transform) {
        if (!isOurs(entity, transform)) return;
        if (!wheel.hasMount) {
            placeIn(scene, entity, self, wheel.mount, wheel.restRotation);
            wheel.restPosition = transform.position;
            wheel.restRotation = transform.rotation;
            wheel.hasMount = true;
        }
        WheelPart part{};
        part.entity = entity;
        part.wheel = &wheel;
        part.suspension = scene.get<Suspension>(entity);
        part.tire = scene.get<DeformableTire>(entity);
        // Without a suspension: a leg that hardly gives.
        part.travel = part.suspension ? std::max(part.suspension->travel, 0.01f) : 0.03f;
        part.springRate = part.suspension ? part.suspension->springRate : 400000.0f;
        part.damping = part.suspension ? part.suspension->damping : 9000.0f;
        part.antiRoll = part.suspension ? part.suspension->antiRoll : 0.0f;
        float pressure = part.tire ? std::max(part.tire->pressure, 0.3f) : 2.2f;
        part.stiffness = part.tire ? part.tire->stiffness * pressure / 2.2f : 0.0f;
        // Soft tires put more rubber on the road, up to a point, and take more work to roll.
        part.grip = wheel.grip * std::clamp(1.0f + 0.06f * (2.2f - pressure), 0.85f, 1.12f);
        part.rolling = 0.013f * std::pow(2.2f / pressure, 0.7f);
        parts.wheels.push_back(part);
    });
    scene.each<Hinge, Transform>([&](Entity entity, Hinge& hinge, Transform& transform) {
        if (!isOurs(entity, transform)) return;
        hinge.angle = std::clamp(controls.byName(hinge.input), -1.0f, 1.0f) * hinge.maxAngle * (hinge.isInverted ? -1.0f : 1.0f);
        parts.hinges.push_back(&hinge);
    });
    scene.each<Wing, Transform>([&](Entity entity, Wing& wing, Transform& transform) {
        if (!isOurs(entity, transform)) return;
        if (!wing.hasMount) {
            placeIn(scene, entity, self, wing.mount, wing.mountRotation);
            wing.hasMount = true;
        }
        WingPart part{&wing, 0, wing.drag};
        scene.each<Hinge, Transform>([&](Entity, Hinge& hinge, Transform& hingeTransform) {
            if (hingeTransform.parent != entity || !hingeTransform.isActiveInHierarchy) return;
            part.control += radians(hinge.angle) * hinge.effect;
            part.drag += hinge.drag * std::abs(hinge.angle) / std::max(hinge.maxAngle, 1.0f);
        });
        parts.wings.push_back(part);
    });
    scene.each<Propeller, Transform>([&](Entity entity, Propeller& propeller, Transform& transform) {
        if (!isOurs(entity, transform)) return;
        if (!propeller.hasMount) {
            Vec3 position;
            Quat rotation;
            placeIn(scene, entity, self, position, rotation);
            propeller.direction = rotate(rotation, Vec3{0, 0, -1});
            propeller.hasMount = true;
        }
        parts.propellers.push_back(&propeller);
    });
    scene.each<Bumper, Transform>([&](Entity entity, Bumper& bumper, Transform& transform) {
        if (!isOurs(entity, transform)) return;
        if (!bumper.hasMount) {
            Quat rotation;
            placeIn(scene, entity, self, bumper.mount, rotation);
            bumper.hasMount = true;
        }
        parts.bumpers.push_back(&bumper);
    });
    scene.each<Motor, Transform>([&](Entity entity, Motor& motor, Transform& transform) {
        if (!parts.motor && isOurs(entity, transform)) parts.motor = &motor;
    });
    scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& transform) {
        if (collider.isTrigger || !isOurs(entity, transform) || scene.get<Wheel>(entity)) return;
        if (collider.shape == ColliderShape::Mesh && !collider.isConvex) return;      // only what can move can be a moving body
        Parts::BodyShape shape{entity, &collider, Vec3{0, 0, 0}, Quat{}};
        if (entity != self) placeIn(scene, entity, self, shape.position, shape.rotation);
        parts.shapes.push_back(shape);
    });
    parts.drive = scene.get<DriveControls>(self);
    parts.flight = scene.get<FlightControls>(self);
    return parts;
}

/// The body's own colliders against everything solid around it: each point where one is in
/// something is a stiff spring out of it, with friction along it, as the box's corners are.
/// What it hits is pushed back, if it can move.
void collideBody(World& world, Entity self, Body6& body, const std::vector<Parts::BodyShape>& shapes, float dt) {
    for (const Parts::BodyShape& part : shapes) {
        ShapeInstance shape = Physics::makeShape(*part.collider, body.worldPoint(part.position), simd::normalize(body.orientation * part.rotation));
        if ((shape.shape == ColliderShape::Convex && !shape.convex)) continue;
        AABB box = shape.bounds();
        world.physics.forEachShapeNear(world.scene, box, self, [&](Entity other, const ShapeInstance& against) {
            ContactInfo contact;
            if (!collide(shape, against, contact)) return;
            Vec3 outward = -contact.normal;                         // out of what it hit, toward the body
            int count = std::max(contact.pointCount, 1);
            RigidBody* moved = world.scene.get<RigidBody>(other);
            bool canMove = moved && moved->type == BodyType::Dynamic;
            Vec3 otherVelocity = moved && moved->type != BodyType::Static ? moved->velocity : Vec3{0, 0, 0};
            for (int k = 0; k < count; k++) {
                Vec3 point = contact.pointCount > 0 ? contact.points[k] : contact.point;
                float depth = contact.pointCount > 0 ? contact.depths[k] : contact.depth;
                Vec3 velocity = body.velocityAt(point) - otherVelocity;
                float approach = -simd::dot(velocity, outward);
                float push = std::max(0.0f, depth * body.mass * 160.0f + approach * body.mass * 9.0f);
                Vec3 along = velocity + outward * approach;
                float slide = simd::length(along);
                Vec3 friction = slide > 0.05f ? along * (-std::min(0.6f * push, slide * body.mass * 4.0f) / slide) : Vec3{0, 0, 0};
                Vec3 force = outward * push + friction;
                body.addForceAt(force, point);
                if (canMove) moved->applyImpulse(force * -dt, point);
            }
        });
    }
}

/// The force on a wing, in world axes; its angle of attack is left in the wing.
Vec3 wingForce(const Body6& body, WingPart& part, float density) {
    Wing& wing = *part.wing;
    Vec3 normal = rotate(wing.mountRotation, Vec3{0, 1, 0}), chord = rotate(wing.mountRotation, Vec3{0, 0, -1});
    Vec3 air = body.toLocal(body.velocityAt(body.worldPoint(wing.mount)));      // how the wing moves through the air, in the body's axes
    float speed = simd::length(air);
    wing.angle = 0;
    if (speed < 0.5f) return Vec3{0, 0, 0};
    Vec3 direction = air / speed;
    float angle = std::atan2(-simd::dot(air, normal), simd::dot(air, chord)) + part.control;
    if (angle > kPi) angle -= 2 * kPi;
    if (angle < -kPi) angle += 2 * kPi;
    wing.angle = angle;

    // Attached flow: lift grows with the angle. Past the stall the wing is a board in the wind.
    float aspect = std::max(wing.aspect, 0.2f), stall = radians(wing.stallAngle);
    float slope = 2.0f * kPi * aspect / (aspect + 2.0f);
    float attachedLift = slope * angle;
    float attachedDrag = 0.011f + attachedLift * attachedLift / (kPi * 0.8f * aspect);
    float boardLift = 1.05f * std::sin(2.0f * angle);
    float boardDrag = 0.02f + 1.9f * std::sin(angle) * std::sin(angle);
    float separated = smoothstep01((std::abs(angle) - (stall - radians(2.0f))) / radians(8.0f));
    float liftCoefficient = attachedLift + (boardLift - attachedLift) * separated;
    float dragCoefficient = attachedDrag + (boardDrag - attachedDrag) * separated + part.drag;

    float pressure = 0.5f * density * speed * speed;
    // Lift is across the airflow, toward the wing's upper side; drag is along it.
    Vec3 across = safeNormalize(normal - direction * simd::dot(normal, direction), normal);
    Vec3 force = across * (pressure * wing.area * liftCoefficient) - direction * (pressure * wing.area * dragCoefficient);
    return rotate(body.orientation, force);
}

/// The driver's keys, into the controls. Also the gearbox's forward and reverse, and the
/// keys that change the engine.
void readDriver(World& world, DriveControls& drive, VehicleBody& vehicle, Motor* motor, float dt) {
    const Input& input = world.input;
    Controls& controls = vehicle.controls;
    float ahead = -input.axis("moveZ");                 // W is +1
    float along = vehicle.speed();
    int gear = motor ? motor->gear : 1;
    // Pulling back at a standstill is reverse; pushing forward again, first gear.
    if (gear >= 1 && along < 0.6f && ahead < -0.1f) gear = -1;
    else if (gear == -1 && along > -0.6f && ahead > 0.1f) gear = 1;
    float wantThrottle = gear >= 1 ? std::max(ahead, 0.0f) : std::max(-ahead, 0.0f);
    float wantBrake = gear >= 1 ? std::max(-ahead, 0.0f) : std::max(ahead, 0.0f);
    controls.throttle = approach(controls.throttle, wantThrottle, dt * 6);
    controls.brake = approach(controls.brake, wantBrake, dt * 8);
    controls.steer = approach(controls.steer, input.axis("moveX"), dt * (std::abs(input.axis("moveX")) > 0.01f ? 3.2f : 5.0f));
    controls.handbrake = input.isDown("jump") ? 1.0f : 0.0f;
    if (!motor) return;
    motor->gear = gear;
    // Another engine at the touch of a key: the quickest way to feel what an engine is.
    if (drive.switchesEngines) {
        const char* const choices[] = {"i4", "i5", "i6", "v8", "v12", "diesel", "electric"};
        for (int i = 0; i < 7; i++) {
            if (input.wasPressed(Key(int(Key::Num1) + i)) && motor->engine != choices[i]) {
                motor->engine = choices[i];
                motor->gear = std::clamp(motor->gear, -1, specOf(motor->engine).gearCount);
            }
        }
    }
}

/// The pilot's keys, into the controls.
void readPilot(World& world, FlightControls& flight, VehicleBody& vehicle, float dt) {
    const Input& input = world.input;
    Controls& controls = vehicle.controls;
    const Body6& body = vehicle.body;
    if (!flight.hasStarted) {
        controls.throttle = saturate(flight.startThrottle);
        flight.hasStarted = true;
    }
    float wantPitch = input.axis("moveZ"), wantRoll = input.axis("moveX"), wantYaw = input.axis("yaw");     // S pulls the stick back: nose up
    float speed = vehicle.airspeed();
    if (flight.assist && speed > 14.0f) {
        // How it is turning now, about its own axes: nose up, rolling right, nose right.
        Vec3 spin = body.toLocal(body.angularVelocity);
        float pitchRate = spin.x, rollRate = -spin.z, yawRate = -spin.y;
        float bank = -std::asin(std::clamp(body.right().y, -1.0f, 1.0f));       // right wing down is positive
        // Roll: the keys ask for a rate; with none pressed, for the wings to come level.
        float rollWanted = std::abs(wantRoll) > 0.05f ? wantRoll * 0.9f : std::clamp(-bank * 1.6f, -0.7f, 0.7f);
        // Not past forty-five degrees of bank by keys alone.
        if (bank * wantRoll > 0 && std::abs(bank) > radians(45.0f)) rollWanted = std::clamp((radians(45.0f) - std::abs(bank)) * 2.0f, -0.5f, 0.0f) * (bank > 0 ? 1.0f : -1.0f);
        // Pitch: a rate too; with none pressed, hold the nose where it is. Never past the stall.
        float pitchWanted = wantPitch * 0.4f;
        // With the keys let go it levels off: the path it flies along, not only its nose, is brought
        // back to the horizon, so that it neither sinks away nor sails up and down as it trades speed for height.
        if (std::abs(wantPitch) < 0.05f) {
            float path = std::asin(std::clamp(body.velocity.y / std::max(speed, 1.0f), -1.0f, 1.0f));
            pitchWanted = std::clamp(-path * 1.1f, -0.22f, 0.22f);
            // And what it takes to hold it there is learned, as a pilot trims.
            if (std::abs(path) < radians(5.0f)) flight.trim = std::clamp(flight.trim - path * 0.3f * dt, -0.5f, 0.5f);
        }
        // Nor into a loop: the nose is not taken past thirty degrees up or down by keys alone.
        float nose = std::asin(std::clamp(body.forward().y, -1.0f, 1.0f));
        if (nose * pitchWanted > 0 && std::abs(nose) > radians(22.0f)) pitchWanted *= saturate((radians(32.0f) - std::abs(nose)) / radians(10.0f));
        float margin = flight.stallAngle - radians(3.5f) - flight.angleOfAttack;
        pitchWanted = std::min(pitchWanted, margin * 5.0f);
        // In a bank the nose would fall: a little back pressure, as a pilot gives without thinking.
        pitchWanted += (1.0f / std::max(std::cos(bank), 0.5f) - 1.0f) * 9.81f / std::max(speed, 20.0f);
        // The faster it flies the less the surfaces need to move for the same effect.
        float effect = std::clamp(900.0f / (speed * speed), 0.25f, 1.6f);
        wantRoll = std::clamp((rollWanted - rollRate) * 1.1f * effect, -1.0f, 1.0f);
        wantPitch = std::clamp((pitchWanted - pitchRate) * 3.2f * effect + flight.trim, -1.0f, 1.0f);
        // The rudder: what the pedals ask for, less whatever the nose is already swinging by, and
            // a push back toward flying straight through the air instead of sideways through it.
            Vec3 through = body.toLocal(body.velocity);
            float slip = std::atan2(through.x, std::max(-through.z, 1.0f));     // the air coming from the right is positive
            wantYaw = std::clamp(wantYaw * 0.5f - yawRate * 2.2f * effect + (std::abs(wantYaw) < 0.05f ? slip * 1.5f : 0.0f), -1.0f, 1.0f);       // and the rudder damps the tail's wagging
        controls.pitch = approach(controls.pitch, wantPitch, dt * 9.0f);
        controls.roll = approach(controls.roll, wantRoll, dt * 9.0f);
        controls.yaw = approach(controls.yaw, wantYaw, dt * 9.0f);
    } else {
        // The controls move at the speed hands move them, not at the speed keys change.
        controls.pitch = approach(controls.pitch, wantPitch, dt * 3.5f);
        controls.roll = approach(controls.roll, wantRoll, dt * 4.5f);
        controls.yaw = approach(controls.yaw, wantYaw, dt * 4.0f);
    }
    // A real lever says where it is; keys can only push it up and down.
    if (input.hasJoystick("throttleLever")) controls.throttle = saturate(input.axis("throttleLever") * 0.5f + 0.5f);
    else controls.throttle = saturate(controls.throttle + input.axis("throttle") * dt * 0.6f);
    if (input.wasPressed("flaps")) flight.flapsWanted = flight.flapsWanted > 0.75f ? 0.0f : flight.flapsWanted + 0.5f;
    controls.brake = input.isDown("brake") ? 1.0f : 0.0f;
    controls.steer = controls.yaw;
}

}  // namespace

// ---------------------------------------------------------------- VehicleBody

float VehicleBody::speed() const {
    return simd::dot(body.velocity, body.forward());
}

void VehicleBody::setControls(const Controls& wanted) {
    controlsAreGiven = true;
    controls = wanted;
}

void VehicleBody::start(World& world, Entity self) {
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    body.position = transform->worldPosition();
    body.orientation = transform->rotation;
    body.velocity = rotate(body.orientation, startVelocity);
    previousPosition = writtenPosition = body.position;
    previousOrientation = writtenRotation = body.orientation;
    hasStarted = true;
}

void VehicleBody::fixedUpdate(World& world, Entity self, float dt) {
    if (!hasStarted) return;
    Scene& scene = world.scene;
    // Moved or turned by hand (an editor's tools, the game's code): it goes on from where it was put.
    if (const Transform* placed = scene.get<Transform>(self)) {
        if (simd::length_squared(placed->position - writtenPosition) > 1e-10f) body.position = placed->position;
        if (std::abs(simd::dot(placed->rotation.vector, writtenRotation.vector)) < 0.9999995f) {
            body.orientation = placed->rotation;
            body.angularVelocity = Vec3{0, 0, 0};
        }
    }
    previousPosition = body.position;
    previousOrientation = body.orientation;
    body.mass = std::max(mass, 1.0f);
    // Left at zero: a box of its size, with the mass a little more toward the middle than a box has.
    Vec3 perKilogram = simd::length_squared(inertia) > 0 ? inertia : Vec3{size.y * size.y + size.z * size.z, size.x * size.x + size.z * size.z, size.x * size.x + size.y * size.y} / 14.0f;
    body.inertia = simd::max(perKilogram, Vec3{0.01f, 0.01f, 0.01f}) * body.mass;

    // ---- what whoever is at the controls wants
    Motor* motorNow = nullptr;
    scene.each<Motor, Transform>([&](Entity entity, Motor& motor, Transform&) {
        if (!motorNow && vehicleOf(scene, entity) == self) motorNow = &motor;
    });
    DriveControls* drive = scene.get<DriveControls>(self);
    FlightControls* flight = scene.get<FlightControls>(self);
    bool runsDrive = drive && world.runs("DriveControls", self), runsFlight = flight && world.runs("FlightControls", self);
    if (!controlsAreGiven) {
        if (runsDrive) readDriver(world, *drive, *this, motorNow, dt);
        if (runsFlight) readPilot(world, *flight, *this, dt);
    }
    if (flight) controls.flaps = approach(controls.flaps, flight->flapsWanted, dt * 0.5f);

    Parts parts = gather(scene, self, controls);
    Motor* motor = parts.motor;
    const EngineSpec& engine = specOf(motor ? motor->engine : "i4");
    int drivenWheels = 0;
    float drivenRadius = 0.33f;
    for (WheelPart& part : parts.wheels) {
        Wheel& wheel = *part.wheel;
        // With no Motor of the engine's own, whatever turns the wheels is the game's code: its torque is left as it set it.
        if (motor) {
            wheel.driveTorque = 0;
            wheel.easesWhenSpinning = motor->tractionControl;
        }
        if (wheel.isDriven) {
            drivenWheels++;
            drivenRadius = wheel.radius;
        }
        // Less lock at speed: what turns a car around in a car park would spin it on a motorway.
        float along = std::abs(speed());
        float lock = radians(wheel.steerAngle);
        if (wheel.steersWith == "yaw") wheel.state.steer = controls.yaw * lock;
        else wheel.state.steer = controls.steer * lock / (1.0f + along * along / 260.0f);
    }
    if (motor) motor->throttle = controls.throttle;
    for (Propeller* propeller : parts.propellers) propeller->throttle = controls.throttle;

    const float wheelInertia = 1.3f;
    const int substeps = 4;
    const float h = dt / float(substeps);
    bool touches = false;
    for (int substep = 0; substep < substeps; substep++) {
        Vec3 up = body.up();
        float density = kAirDensity * std::exp(-std::max(body.position.y, 0.0f) / 8500.0f);     // thinner higher up
        body.force += Vec3{0, -kGravity * body.mass, 0};

        // ---- the engine, as fast as the driven wheels turn it
        if (motor && drivenWheels > 0) {
            float drivenSpin = 0;
            for (const WheelPart& part : parts.wheels) {
                if (part.wheel->isDriven) drivenSpin += part.wheel->state.spin / float(drivenWheels);
            }
            int& gear = motor->gear;
            float throttle = controls.throttle;
            float ratio = (gear == -1 ? -engine.reverse : engine.gears[std::clamp(gear, 1, engine.gearCount) - 1]) * engine.finalDrive;
            float wheelRpm = std::abs(drivenSpin * ratio) * 60.0f / (2.0f * kPi);
            // Below idle the clutch slips (or the converter does): the engine keeps turning.
            float slipRpm = engine.idle + throttle * (engine.isElectric ? 0.0f : 1100.0f);
            float engineRpm = std::max(wheelRpm, slipRpm);
            motor->rpm += (engineRpm - motor->rpm) * std::min(1.0f, h * 14.0f);
            motor->shiftPause = std::max(0.0f, motor->shiftPause - h);
            float torque = motor->shiftPause > 0 ? 0.0f : torqueOf(engine, engineRpm) * throttle;
            // With the pedal up the engine holds the car back, more the faster it turns.
            if (!engine.isElectric) torque -= (1 - throttle) * engine.peakTorque * 0.12f * (engineRpm / engine.redline) * (wheelRpm > engine.idle ? 1.0f : 0.0f);
            else torque -= (1 - throttle) * 40.0f * saturate(wheelRpm / 800.0f);       // a little regeneration
            float driveTorque = torque * ratio * 0.9f / float(drivenWheels);
            for (WheelPart& part : parts.wheels) part.wheel->driveTorque = part.wheel->isDriven ? driveTorque : 0.0f;
            // The gearbox: up near the top, down when the engine labors.
            // It goes by the speed of the road, not of the wheels: spinning them is no reason to change up.
            float roadRpm = std::abs(speed()) / std::max(drivenRadius, 0.05f) * std::abs(ratio) * 60.0f / (2.0f * kPi);
            if (gear >= 1 && motor->shiftPause <= 0 && !engine.isElectric) {
                if (roadRpm > engine.redline * 0.9f && gear < engine.gearCount) {
                    gear++;
                    motor->shiftPause = 0.22f;
                } else if (gear > 1 && roadRpm < engine.redline * (0.3f + 0.12f * throttle)) {
                    float lower = engine.gears[gear - 2] / engine.gears[gear - 1] * roadRpm;
                    if (lower < engine.redline * 0.82f) {
                        gear--;
                        motor->shiftPause = 0.18f;
                    }
                }
            }
        }

        // ---- stability control: a slide past the angle it allows, getting worse, is caught by
        // turning the car back, and the engine is eased so as not to push it further round.
        float driveShare = 1.0f;
        if (stability > 0 && isOnGround) {
            float along = simd::dot(body.velocity, body.forward()), across = simd::dot(body.velocity, body.right());
            if (along > 3.0f) {
                float slip = std::atan2(across, along);                      // above zero: going to the right of where the nose points
                float yawRate = simd::dot(body.angularVelocity, up);         // above zero: the nose turning left
                float allowed = radians(driftAngle) * (controls.handbrake > 0.5f ? 1.7f : 1.0f);
                float past = saturate((std::abs(slip) - allowed) / 0.12f);
                bool isWorsening = slip * yawRate > 0;
                if (past > 0 && isWorsening) body.torque += up * (-yawRate * body.inertia.y * stability * 12.0f * past);
                driveShare = 1.0f - 0.85f * stability * saturate((std::abs(slip) - allowed * 0.6f) / (allowed * 0.6f + 0.02f));
            }
        }

        // ---- each wheel: spring, then tire
        for (WheelPart& part : parts.wheels) {
            Wheel& wheel = *part.wheel;
            WheelState& state = wheel.state;
            // The spring is bolted on half its travel above where the wheel stands.
            Vec3 top = body.worldPoint(wheel.mount + Vec3{0, part.travel * 0.5f, 0});
            float radius = wheel.radius - state.deflection;
            float fullLength = part.travel + radius;
            std::optional<RaycastHit> hit = world.physics.raycast(scene, Ray{top, -up}, fullLength, 0xFFFFFFFFu, self);
            state.isOnGround = hit.has_value();
            float before = state.compression;
            state.compression = hit ? std::clamp(fullLength - hit->distance, 0.0f, part.travel) : 0.0f;
            state.load = 0;
            if (!hit) continue;
            touches = true;
            part.contactPoint = hit->point;
            part.contactNormal = hit->normal;
            float springForce = state.compression * part.springRate + (state.compression - before) / h * part.damping;
            // At the end of its travel the suspension is solid: the rest of the weight comes through at once.
            if (state.compression >= part.travel - 1e-4f) springForce += (fullLength - hit->distance - part.travel) * part.springRate * 12.0f;
            state.load = std::max(springForce, 0.0f);
        }
        // The anti-roll bar: the wheel that is pushed in more lends some of its push to the one across from it.
        std::vector<float> lent(parts.wheels.size(), 0.0f);
        for (size_t i = 0; i < parts.wheels.size(); i++) {
            const WheelPart& one = parts.wheels[i];
            if (one.antiRoll <= 0 || !one.wheel->state.isOnGround) continue;
            for (size_t j = 0; j < parts.wheels.size(); j++) {
                const WheelPart& other = parts.wheels[j];
                bool isAcross = i != j && std::abs(one.wheel->mount.z - other.wheel->mount.z) < 0.15f && one.wheel->mount.x * other.wheel->mount.x < 0;
                if (isAcross && other.wheel->state.isOnGround) lent[i] += (one.wheel->state.compression - other.wheel->state.compression) * one.antiRoll;
            }
        }
        // What each tire would stand on if they all stood on the same: for sharing out the brakes.
        float sharedLoad = 0;
        for (const WheelPart& part : parts.wheels) sharedLoad += part.wheel->state.load;
        if (!parts.wheels.empty()) sharedLoad /= float(parts.wheels.size());
        for (size_t i = 0; i < parts.wheels.size(); i++) {
            WheelPart& part = parts.wheels[i];
            Wheel& wheel = *part.wheel;
            WheelState& state = wheel.state;
            state.load = std::max(state.load + lent[i], 0.0f);
            // The tire flattens under its load; softer, more.
            float wanted = part.stiffness > 0 ? std::min(state.load / part.stiffness, wheel.radius * 0.45f) : 0.0f;
            state.deflection += (wanted - state.deflection) * std::min(1.0f, h * 30.0f);
            float radius = wheel.radius - state.deflection;

            float torque = wheel.driveTorque * driveShare;
            // Traction control: a driven wheel turning much faster than the road passes gets less.
            if (wheel.easesWhenSpinning && wheel.isDriven && state.isOnGround && torque * state.slipRatio > 0) {
                torque *= 1.0f - saturate((std::abs(state.slipRatio) - kPeakSlipRatio) / (kPeakSlipRatio * 0.8f));
            }
            float footBrake = controls.brake * wheel.brakeTorque;
            // Brakes shared out by what each tire stands on: braking throws the car's weight onto
            // its front wheels, and a rear wheel braked as if it still carried its share locks,
            // slides, and comes round. A wheel carrying less than its share is braked less.
            if (wheel.antiLock && sharedLoad > 1.0f) footBrake *= std::clamp(state.load / sharedLoad, 0.2f, 1.0f);
            // Anti-lock: a wheel turning much slower than the road passes is let go a little,
            // so that it keeps gripping, and steering, instead of sliding.
            if (wheel.antiLock && state.isOnGround) {
                // A tire has one budget of grip: what braking takes is not there for holding the
                // car's line. So the brake is eased as the wheel nears locking, and also as it
                // is asked to hold more sideways.
                float locking = saturate((-state.slipRatio - kPeakSlipRatio * 0.6f) / (kPeakSlipRatio * 0.6f));
                float sideways = saturate((std::abs(state.slipAngle) - kPeakSlipAngle * 0.5f) / (kPeakSlipAngle * 0.7f));
                footBrake *= 1.0f - 0.95f * std::max(locking, sideways * 0.8f);
            }
            float brakeTorque = footBrake + controls.handbrake * wheel.handbrakeTorque;
            float longitudinal = 0, stiffness = 0;
            if (state.isOnGround && state.load > 0) {
                Vec3 normal = part.contactNormal;
                body.addForceAt(normal * state.load, part.contactPoint);

                // Which way the wheel points, laid on the ground.
                Vec3 heading = rotate(body.orientation, Vec3{std::sin(state.steer), 0, -std::cos(state.steer)});
                heading = safeNormalize(heading - normal * simd::dot(heading, normal), body.forward());
                Vec3 side = simd::cross(heading, normal);
                Vec3 velocity = body.velocityAt(part.contactPoint);
                float forwardSpeed = simd::dot(velocity, heading), sideSpeed = simd::dot(velocity, side);

                // Slip: how much faster the tread goes than the road, and how far off its heading it travels.
                float reference = std::max(std::abs(forwardSpeed), 2.5f);
                auto forcesAt = [&](float spin, float& outLongitudinal, float& outLateral) {
                    float ratioSlip = (spin * radius - forwardSpeed) / reference;
                    float angleSlip = std::atan2(sideSpeed, reference);
                    float x = ratioSlip / kPeakSlipRatio, y = angleSlip / kPeakSlipAngle;
                    float combined = std::sqrt(x * x + y * y);
                    state.slipRatio = ratioSlip;
                    state.slipAngle = angleSlip;
                    if (combined < 1e-5f) {
                        outLongitudinal = outLateral = 0;
                        return;
                    }
                    // One budget of friction for both directions: braking hard leaves little to corner with.
                    float available = state.load * part.grip * std::max(gripAt(combined), combined > 1.0f ? slideGrip : 0.0f);
                    outLongitudinal = available * x / combined;
                    outLateral = -available * y / combined;
                };
                float lateral = 0, nudged = 0, ignored = 0;
                forcesAt(state.spin + 0.5f, nudged, ignored);
                forcesAt(state.spin, longitudinal, lateral);
                stiffness = std::max((nudged - longitudinal) / 0.5f, 0.0f);     // newtons per radian per second

                // The wheel's own turning, solved so that a stiff tire cannot make it shake:
                // the change in spin is found with the tire's answer to it taken into account.
                float resisting = longitudinal * radius + part.rolling * state.load * radius * (state.spin > 0 ? 1.0f : state.spin < 0 ? -1.0f : 0.0f);
                float change = h * (torque - resisting) / (wheelInertia + h * stiffness * radius);
                float spin = state.spin + change;
                float held = h * brakeTorque / (wheelInertia + h * stiffness * radius);
                spin = std::abs(spin) <= held ? 0.0f : spin - (spin > 0 ? held : -held);
                longitudinal += stiffness * (spin - state.spin);
                state.spin = spin;
                // Never more than the road can give.
                float limit = state.load * part.grip;
                longitudinal = std::clamp(longitudinal, -limit, limit);
                body.addForceAt(heading * longitudinal + side * lateral, part.contactPoint);
            } else {
                // In the air: the engine and the brakes have the wheel to themselves.
                float spin = state.spin + h * torque / wheelInertia;
                float held = h * brakeTorque / wheelInertia;
                state.spin = std::abs(spin) <= held ? 0.0f : spin - (spin > 0 ? held : -held);
                state.slipRatio = state.slipAngle = 0;
            }
            state.angle += state.spin * h;
        }

        // ---- the air: on the wings, and on everything that is not a wing
        float mainArea = 0, mainAngle = 0, largest = 0;
        for (const WingPart& part : parts.wings) largest = std::max(largest, part.wing->area);
        for (WingPart& part : parts.wings) {
            body.addForceAt(wingForce(body, part, density), body.worldPoint(part.wing->mount));
            if (part.wing->area >= largest * 0.6f) {
                mainArea += part.wing->area;
                mainAngle += part.wing->angle * part.wing->area;
            }
        }
        if (mainArea > 0) {
            angleOfAttack = mainAngle / mainArea;
            for (const WingPart& part : parts.wings) {
                if (part.wing->area >= largest * 0.6f) stallAngle = radians(part.wing->stallAngle);
            }
        }
        float airSpeed = simd::length(body.velocity);
        body.force += body.velocity * (-0.5f * density * dragArea * airSpeed);

        // ---- propellers: all the engine's power at speed, and no more than they can throw back at rest
        for (const Propeller* propeller : parts.propellers) {
            Vec3 direction = rotate(body.orientation, propeller->direction);
            float along = std::max(simd::dot(body.velocity, direction), 0.0f);
            float thrust = controls.throttle * std::min(propeller->staticThrust, 0.8f * propeller->power * 1000.0f / std::max(along, 1.0f)) * (density / kAirDensity);
            body.force += direction * thrust;
        }

        // ---- what the body itself runs into
        if (!parts.shapes.empty()) {
            collideBody(world, self, body, parts.shapes, h);
        } else if (hitsWithItsBox) {
            for (int corner = 0; corner < 8; corner++) {
                Vec3 local{(corner & 1 ? 0.5f : -0.5f) * size.x, (corner & 2 ? 0.62f : -0.18f) * size.y, (corner & 4 ? 0.5f : -0.5f) * size.z};
                pressAgainstScene(world, self, body, local, body.mass * 160.0f, body.mass * 9.0f, h);
            }
        }
        for (const Bumper* bumper : parts.bumpers) pressAgainstScene(world, self, body, bumper->mount, body.mass * 140.0f, body.mass * 10.0f, h);
        body.integrate(h);
    }
    isOnGround = touches;

    // ---- the dials
    if (runsDrive) {
        world.setValue("speed", std::round(std::abs(speed()) * 3.6f));
        if (motor) {
            world.setValue("rpm", std::round(motor->rpm / 10.0f) * 10.0f);
            world.setValue("gear", float(motor->gear));
        }
    }
    isStalled = !isOnGround && !parts.wings.empty() && std::abs(angleOfAttack) > stallAngle - radians(1.0f) && airspeed() > 5.0f;
    if (flight) {
        flight->angleOfAttack = angleOfAttack;
        flight->stallAngle = stallAngle;
        flight->isStalled = isStalled;
        if (runsFlight) {
            world.setValue("airspeed", std::round(airspeed() * 3.6f));
            world.setValue("altitude", std::round(body.position.y));
            world.setValue("throttle", std::round(controls.throttle * 100.0f));
            world.setValue("climb", std::round(body.velocity.y * 10.0f) / 10.0f);
            world.setValue("flaps", std::round(controls.flaps * 100.0f));
            world.setValue("stall", isStalled ? 1.0f : 0.0f);
        }
    }
}

void VehicleBody::update(World& world, Entity self, float) {
    if (!hasStarted) return;
    Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    // Between two steps of the simulation, so that the picture moves as smoothly as the screen refreshes.
    float alpha = saturate(world.time.alpha);
    transform->position = previousPosition + (body.position - previousPosition) * alpha;
    transform->rotation = simd::slerp(previousOrientation, body.orientation, alpha);
    writtenPosition = transform->position;
    writtenRotation = transform->rotation;
}

// ---------------------------------------------------------------- the parts, as they look and sound

void Wheel::update(World& world, Entity self, float) {
    if (!hasMount) return;
    Scene& scene = world.scene;
    Transform* transform = scene.get<Transform>(self);
    if (!transform) return;
    const Suspension* suspension = scene.get<Suspension>(self);
    float travel = suspension ? std::max(suspension->travel, 0.01f) : 0.03f;
    // Hanging at the end of its spring: on the ground that puts the tire's lowest point
    // exactly on the road, because the ray that found the road is the same length.
    transform->position = restPosition + rotate(restRotation, Vec3{0, state.compression - travel * 0.5f, 0});
    transform->rotation = simd::normalize(restRotation * Quat(-state.steer, Vec3{0, 1, 0}));
    // Flattened: shorter from road to hub, a little fatter. The wheel's size is the model's own.
    float squash = (radius - state.deflection) / std::max(radius, 0.01f);
    transform->scale = Vec3{2.0f - squash, squash, 1.0f};
    // The child that turns with the wheel: looked for once (a search of the whole scene), then kept.
    const Transform* kept = scene.isAlive(spinPart) ? scene.get<Transform>(spinPart) : nullptr;
    if (!kept || kept->parent != self) spinPart = childCalled(scene, self, "Spin");
    if (Transform* spin = scene.get<Transform>(spinPart)) spin->rotation = Quat(-state.angle, Vec3{1, 0, 0});
}

void Motor::update(World& world, Entity, float) {
    const EngineSpec& spec = specOf(engine);
    if (soundingEngine != engine) {
        for (uint32_t& voice : voices) {
            world.stopSound(voice);
            voice = 0;
        }
        if (!soundFolder.empty()) {
            for (int layer = 0; layer < 4; layer++) {
                voices[layer] = world.startSound(soundFolder + "/engine_" + engine + "_" + std::to_string(int(spec.layers[layer])) + ".wav", 0.0f, 1.0f, true);
            }
        }
        soundingEngine = engine;
    }
    // Each recording is loudest at its own speed and fades toward its neighbors'; between
    // two, both play, each bent a little toward the speed the engine really has.
    float loudness = 0.32f + 0.6f * throttle;
    for (int layer = 0; layer < 4; layer++) {
        float own = spec.layers[layer];
        float below = layer > 0 ? spec.layers[layer - 1] : own, above = layer < 3 ? spec.layers[layer + 1] : own;
        float weight = 1.0f;
        if (rpm < own && layer > 0) weight = saturate((rpm - below) / (own - below));
        else if (rpm > own && layer < 3) weight = saturate((above - rpm) / (above - own));
        world.changeSound(voices[layer], loudness * std::sqrt(weight), std::clamp(rpm / own, 0.5f, 2.0f));
    }
}

void DriveControls::update(World& world, Entity self, float) {
    const VehicleBody* vehicle = world.scene.get<VehicleBody>(self);
    if (!vehicle) return;
    if (!hasSounds) {
        if (!skidSound.empty()) skidVoice = world.startSound(skidSound, 0.0f, 1.0f, true);
        if (!windSound.empty()) windVoice = world.startSound(windSound, 0.0f, 1.0f, true);
        hasSounds = true;
    }
    Scene& scene = world.scene;
    float slide = 0, fastest = 0;
    scene.each<Wheel, Transform>([&](Entity entity, Wheel& wheel, Transform&) {
        if (vehicleOf(scene, entity) != self) return;
        fastest = std::max(fastest, std::abs(wheel.state.spin) * wheel.radius);
        if (!wheel.state.isOnGround) return;
        float excess = std::max(std::abs(wheel.state.slipRatio) / kPeakSlipRatio, std::abs(wheel.state.slipAngle) / kPeakSlipAngle) - 1.1f;
        slide = std::max(slide, saturate(excess * 0.6f) * saturate(wheel.state.load / 2500.0f));
    });
    float air = vehicle->airspeed();
    float moving = saturate((air + fastest) / 6.0f);
    world.changeSound(skidVoice, slide * moving * 0.7f, 0.85f + 0.3f * moving);
    world.changeSound(windVoice, saturate(air * air / 2500.0f) * 0.6f, 0.7f + air / 80.0f);
}

void FlightControls::update(World& world, Entity self, float) {
    const VehicleBody* vehicle = world.scene.get<VehicleBody>(self);
    if (!vehicle) return;
    if (!hasSounds) {
        if (!windSound.empty()) windVoice = world.startSound(windSound, 0.0f, 1.0f, true);
        if (!stallSound.empty()) stallVoice = world.startSound(stallSound, 0.0f, 1.0f, true);
        hasSounds = true;
    }
    float speed = vehicle->airspeed();
    world.changeSound(windVoice, saturate(speed * speed / 3600.0f) * 0.6f, 0.7f + speed / 90.0f);
    world.changeSound(stallVoice, isStalled ? 0.5f : 0.0f, 1.0f);
}

void Hinge::start(World& world, Entity self) {
    if (const Transform* transform = world.scene.get<Transform>(self)) restRotation = transform->rotation;
    hasStarted = true;
}

void Hinge::update(World& world, Entity self, float) {
    if (!hasStarted) return;
    Scene& scene = world.scene;
    // Not part of a vehicle: it still follows the controls of... nothing. It keeps its angle, for code to set.
    if (Transform* transform = scene.get<Transform>(self)) transform->rotation = simd::normalize(restRotation * Quat(radians(angle), safeNormalize(axis, Vec3{1, 0, 0})));
}

void Propeller::update(World& world, Entity self, float dt) {
    Scene& scene = world.scene;
    const VehicleBody* vehicle = scene.get<VehicleBody>(vehicleOf(scene, self));
    float speed = vehicle ? vehicle->airspeed() : 0.0f;
    angle += (6.0f + throttle * 60.0f + speed * 0.4f) * dt;
    Entity blades = childCalled(scene, self, "Blades");
    if (Transform* part = scene.get<Transform>(blades.isValid() ? blades : self)) {
        if (blades.isValid()) part->rotation = Quat(angle, Vec3{0, 0, 1});
    }
    if (voice == 0 && !sound.empty()) {
        voice = world.startSound(sound, 0.0f, 1.0f, true);
        if (voice == 0) voice = ~0u;                        // no sound here: do not ask again every frame
    }
    if (voice != ~0u) world.changeSound(voice, 0.25f + 0.5f * throttle, 0.55f + 0.75f * throttle + speed / 300.0f);
}

// ---------------------------------------------------------------- putting parts together

namespace {

Entity addChild(World& world, Entity parent, const char* name, Vec3 position, Quat rotation = Quat{}) {
    Entity entity = world.scene.create(name);
    Transform transform;
    transform.position = position;
    transform.rotation = rotation;
    transform.parent = parent;
    world.scene.add(entity, transform);
    return entity;
}

}  // namespace

Entity makeCar(World& world, Vec3 position, const std::string& engine, const std::string& drive) {
    Scene& scene = world.scene;
    Entity car = addChild(world, Entity{}, "Car", position);
    scene.add(car, VehicleBody{});
    Motor motor;
    motor.engine = engine;
    scene.add(car, motor);
    scene.add(car, DriveControls{});
    const char* const names[4] = {"Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR"};
    for (int i = 0; i < 4; i++) {
        bool isFront = i < 2;
        Entity entity = addChild(world, car, names[i], Vec3{i % 2 == 0 ? -0.8f : 0.8f, -0.22f, isFront ? -1.3f : 1.3f});
        Wheel wheel;
        wheel.steerAngle = isFront ? 34.0f : 0.0f;
        wheel.isDriven = drive == "all" || (isFront ? drive == "front" : drive != "front");
        wheel.brakeTorque = isFront ? 3300.0f : 1250.0f;         // most of the braking in front, where the weight goes: the rear must not lock first
        wheel.handbrakeTorque = isFront ? 0.0f : 5400.0f;
        scene.add(entity, wheel);
        scene.add(entity, Suspension{});
        scene.add(entity, DeformableTire{});
        addChild(world, entity, "Spin", Vec3{0, 0, 0});
    }
    return car;
}

Entity makeAirplane(World& world, Vec3 position) {
    Scene& scene = world.scene;
    Entity plane = addChild(world, Entity{}, "Plane", position);
    VehicleBody body;
    body.mass = 1050;
    body.size = Vec3{10.9f, 2.4f, 7.4f};
    body.inertia = Vec3{1.75f, 2.7f, 1.3f};         // pitch, yaw, roll: an aeroplane is long and wide and not tall
    body.dragArea = 0.42f;
    body.hitsWithItsBox = false;
    scene.add(plane, body);
    scene.add(plane, FlightControls{});

    auto hinge = [&](Entity parent, const char* name, Vec3 at, const char* input, float maxAngle, bool isInverted, float effect, float drag = 0) {
        Entity entity = addChild(world, parent, name, at);
        Hinge made;
        made.input = input;
        made.maxAngle = maxAngle;
        made.isInverted = isInverted;
        made.effect = effect;
        made.drag = drag;
        scene.add(entity, made);
    };
    // The wings sit a little behind the center of mass and the tail pushes down: nose-heavy
    // and held up by its tail, which is what makes it want to fly straight. Each wing is
    // set 2.5 degrees nose up and tipped up 4 at the tip: left alone, it levels itself.
    for (int side = -1; side <= 1; side += 2) {
        Quat set = simd::normalize(Quat(radians(4.0f) * float(side), Vec3{0, 0, 1}) * Quat(radians(2.5f), Vec3{1, 0, 0}));
        Entity wing = addChild(world, plane, side < 0 ? "Wing L" : "Wing R", Vec3{2.725f * float(side), 0.15f, 0.14f}, set);
        scene.add(wing, Wing{});
        hinge(wing, side < 0 ? "Aileron L" : "Aileron R", Vec3{1.6f * float(side), 0, 0.6f}, "roll", 16, side > 0, 0.45f);
        hinge(wing, side < 0 ? "Flap L" : "Flap R", Vec3{-1.0f * float(side), 0, 0.6f}, "flaps", 30, false, 0.3f, 0.03f);
    }
    Entity tail = addChild(world, plane, "Tailplane", Vec3{0, 0.25f, 4.4f}, Quat(radians(-2.6f), Vec3{1, 0, 0}));
    Wing tailWing;
    tailWing.area = 3.2f;
    tailWing.aspect = 4.0f;
    tailWing.stallAngle = 17;
    scene.add(tail, tailWing);
    hinge(tail, "Elevator", Vec3{0, 0, 0.4f}, "pitch", 22, true, 0.6f);
    Entity fin = addChild(world, plane, "Fin", Vec3{0, 0.9f, 4.6f}, Quat(radians(-90.0f), Vec3{0, 0, 1}));
    Wing finWing;
    finWing.area = 1.7f;
    finWing.aspect = 1.8f;
    finWing.stallAngle = 18;
    scene.add(fin, finWing);
    hinge(fin, "Rudder", Vec3{0, 0, 0.4f}, "yaw", 22, true, 0.6f);        // the right pedal swings the rudder right, which pushes the tail left

    // Three wheels: two under the wings with the brakes, one under the nose that the pedals steer.
    const Vec3 legs[3] = {{-1.25f, -0.9f, 0.35f}, {1.25f, -0.9f, 0.35f}, {0, -0.85f, -1.7f}};
    const char* const names[3] = {"Wheel L", "Wheel R", "Wheel Nose"};
    for (int i = 0; i < 3; i++) {
        Entity entity = addChild(world, plane, names[i], legs[i]);
        Wheel wheel;
        wheel.radius = 0.2f;
        wheel.grip = 0.8f;
        wheel.brakeTorque = i < 2 ? 420.0f : 0.0f;
        wheel.steerAngle = i == 2 ? 24.0f : 0.0f;
        wheel.steersWith = "yaw";
        scene.add(entity, wheel);
        Suspension leg;
        leg.travel = 0.3f;
        leg.springRate = 57750;
        leg.damping = 5775;
        leg.antiRoll = 0;
        scene.add(entity, leg);
        addChild(world, entity, "Spin", Vec3{0, 0, 0});
    }
    Entity propeller = addChild(world, plane, "Propeller", Vec3{0, -0.05f, -2.5f});
    scene.add(propeller, Propeller{});
    addChild(world, propeller, "Blades", Vec3{0, 0, 0});
    // What must not touch the ground and sometimes does.
    const Vec3 hard[7] = {{0, 0, -2.4f}, {0, 0.3f, 5.0f}, {-5.45f, 0.3f, 0.3f}, {5.45f, 0.3f, 0.3f}, {0, -0.75f, 0.5f}, {0, 1.2f, 0.4f}, {0, 1.7f, 4.7f}};
    const char* const hardNames[7] = {"Bumper Nose", "Bumper Tail", "Bumper Tip L", "Bumper Tip R", "Bumper Belly", "Bumper Roof", "Bumper Fin"};
    for (int i = 0; i < 7; i++) scene.add(addChild(world, plane, hardNames[i], hard[i]), Bumper{});
    return plane;
}

}  // namespace molten::kit

namespace molten::kit {

MOLTEN_COMPONENT(VehicleBody, MOLTEN_FIELD(mass).range(50, 20000), MOLTEN_FIELD(size).help("Width, height, length: how hard it is to turn, and the box that hits walls (orange in the Scene view when it is selected)"),
                 MOLTEN_FIELD(inertia).help("Per kilogram about x, y, z. Zero works it out from the size"), MOLTEN_FIELD(dragArea),
                 MOLTEN_FIELD(hitsWithItsBox).help("With no colliders of its own: the corners of a box of its size stop at walls (off: only its Bumpers do). "
                                                       "Give it colliders (a Box Collider, a convex Mesh Collider of its body, on it or under it) and it hits with those"),
                 MOLTEN_FIELD(stability).range(0, 1).help("Stability control: catches a slide past Drift Angle, and eases the engine. 0 is off"),
                 MOLTEN_FIELD(driftAngle).range(2, 45).help("Degrees the car may slide off its line before stability control steps in: a drift"),
                 MOLTEN_FIELD(slideGrip).range(0, 1).help("Grip a sliding tire keeps, as a share of its best: low is ice, 0.8 a car that can be held in a slide"),
                 MOLTEN_FIELD(startVelocity).help("In its own axes: -z is forward. For starting on the move"))
MOLTEN_COMPONENT(Bumper, MOLTEN_FIELD(friction).range(0, 1.5f))
MOLTEN_COMPONENT(Wheel, MOLTEN_FIELD(radius).range(0.05f, 1.5f), MOLTEN_FIELD(grip).range(0.2f, 2), MOLTEN_FIELD(steerAngle).range(-50, 50).help("Degrees at full lock. 0 does not steer"),
                 MOLTEN_FIELD(steersWith).choices({"steer", "yaw"}).help("steer: less lock at speed, as a car. yaw: the pedals, as an aeroplane's nose wheel"),
                 MOLTEN_FIELD(isDriven).help("A Motor on the vehicle turns it"), MOLTEN_FIELD(brakeTorque), MOLTEN_FIELD(handbrakeTorque),
                 MOLTEN_FIELD(antiLock).help("Eases the brake when the wheel locks, so that it keeps gripping and steering"))
MOLTEN_COMPONENT(Suspension, MOLTEN_FIELD(travel).range(0.01f, 1), MOLTEN_FIELD(springRate), MOLTEN_FIELD(damping),
                 MOLTEN_FIELD(antiRoll).help("How strongly it is tied to the wheel on the other side"))
MOLTEN_COMPONENT(DeformableTire, MOLTEN_FIELD(pressure).range(0.5f, 3.5f).help("In bar. Softer tires flatten and grip more, and roll harder"), MOLTEN_FIELD(stiffness))
MOLTEN_COMPONENT(Motor, MOLTEN_FIELD(engine).choices({"i4", "i5", "i6", "v8", "v12", "diesel", "electric"}), MOLTEN_FIELD(tractionControl), MOLTEN_FIELD(soundFolder))
MOLTEN_COMPONENT(Wing, MOLTEN_FIELD(area).range(0.05f, 200).help("Square meters"), MOLTEN_FIELD(aspect).range(0.3f, 30).help("Span squared over area: long thin wings lift better"),
                 MOLTEN_FIELD(stallAngle).range(6, 30), MOLTEN_FIELD(drag))
MOLTEN_COMPONENT(Hinge, MOLTEN_FIELD(input).choices({"pitch", "roll", "yaw", "flaps", "steer", "throttle", "brake", "none"}), MOLTEN_FIELD(maxAngle).range(0, 90),
                 MOLTEN_FIELD(isInverted), MOLTEN_FIELD(axis), MOLTEN_FIELD(effect).range(0, 1).help("Under a Wing: how much of its angle counts as the whole wing's"),
                 MOLTEN_FIELD(drag).help("Under a Wing: added at full angle"))
MOLTEN_COMPONENT(Propeller, MOLTEN_FIELD(power).help("Kilowatts"), MOLTEN_FIELD(staticThrust), MOLTEN_FIELD(sound))
MOLTEN_COMPONENT(DriveControls, MOLTEN_FIELD(switchesEngines).help("Keys 1 to 4"), MOLTEN_FIELD(skidSound), MOLTEN_FIELD(windSound))
MOLTEN_COMPONENT(FlightControls, MOLTEN_FIELD(assist).help("Keys ask for a rate of roll and pitch; wings level themselves; no stall. Off: the keys are the stick"),
                 MOLTEN_FIELD(startThrottle).range(0, 1), MOLTEN_FIELD(windSound), MOLTEN_FIELD(stallSound))

}  // namespace molten::kit
