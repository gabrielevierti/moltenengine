// Vehicles.hpp
// The parts machines are made of, each a component of its own: a body that moves and
// turns, wheels, suspensions, tires that flatten, an engine behind a gearbox, wings,
// hinges for the surfaces that move, a propeller. A car is a body with four wheels and a
// motor; an aeroplane is a body with wings, a tail, three wheels and a propeller; a
// trike, a hovercraft with fins, a glider or a truck with six wheels are other ways of
// putting the same parts together.
//
// How they fit:
//   - `VehicleBody` goes on the root entity. It is the thing that has mass, and it runs
//     the simulation of every part below it in the hierarchy.
//   - A part goes on a child entity. Where the child is, and how it is turned, is where
//     the part is and how it is set: a wing tilted up two degrees has two degrees of
//     incidence; a wheel placed at a corner is that corner's wheel.
//   - Controls (`DriveControls`, `FlightControls`, or your own code) write what the driver
//     wants into `VehicleBody::controls`; parts read it from there.
//
// The engine's own physics has no rotating bodies yet, so a vehicle moves its own. The
// ground is found with rays against the scene's colliders: anything with a collider can
// be driven on, landed on, or hit.
//
// Like the rest of the kit these take a World, and are driven in tests without a screen.
#pragma once

#include "Core/World.hpp"

#include <string>

namespace molten::kit {

/// A rigid body with rotation: where it is, how it moves, and what is pushing it this step.
struct Body6 {
    Vec3 position{0, 0, 0}, velocity{0, 0, 0};
    Quat orientation{};
    Vec3 angularVelocity{0, 0, 0};      // world axes, radians per second
    float mass = 1000;
    Vec3 inertia{1500, 2500, 800};      // about the body's own x (right), y (up), z (back)
    Vec3 force{0, 0, 0}, torque{0, 0, 0};

    Vec3 right() const { return rotate(orientation, Vec3{1, 0, 0}); }
    Vec3 up() const { return rotate(orientation, Vec3{0, 1, 0}); }
    Vec3 forward() const { return rotate(orientation, Vec3{0, 0, -1}); }
    Vec3 worldPoint(Vec3 local) const { return position + rotate(orientation, local); }
    Vec3 velocityAt(Vec3 worldPoint) const { return velocity + simd::cross(angularVelocity, worldPoint - position); }
    Vec3 toLocal(Vec3 worldDirection) const;
    void addForceAt(Vec3 worldForce, Vec3 worldPoint);
    void integrate(float dt);
};

/// What whoever is at the controls wants, for the parts to read.
struct Controls {
    float throttle = 0;                 // 0 to 1
    float brake = 0;                    // 0 to 1
    float handbrake = 0;                // 0 to 1
    float steer = 0;                    // -1 left to 1 right
    float pitch = 0;                    // 1 is the stick pulled back: nose up
    float roll = 0;                     // 1 is the stick to the right
    float yaw = 0;                      // 1 is the right pedal
    float flaps = 0;                    // 0 up to 1 fully down
    float byName(const std::string& name) const;
};

/// The thing that has mass. Everything below it in the hierarchy that is a part
/// (Wheel, Wing, Motor, Propeller, Bumper...) pushes on it.
struct VehicleBody {
    float mass = 1350;                  // kilograms
    Vec3 size{1.8f, 1.3f, 4.3f};        // width, height, length: for how hard it is to turn, and for what it hits
    Vec3 inertia{0, 0, 0};              // per kilogram about x, y, z; zero works it out from the size
    float dragArea = 0.68f;             // drag coefficient times frontal area, square meters
    bool hitsWithItsBox = true;         // the corners of a box of its size stop at walls and the ground. Off: only its Bumpers do
    Vec3 startVelocity{0, 0, 0};        // in its own axes: -z is forward. For starting on the move
    /// Stability control, 0 (off) to 1: once the car slides further than `driftAngle` off where
    /// it is going, the engine is eased and the spin is caught. Up to that angle it lets you
    /// drift; with the handbrake held it lets you drift further.
    float stability = 0.0f;
    float driftAngle = 15.0f;           // degrees
    /// What a tire keeps of its grip once it slides, as a share of its best: low is ice, high
    /// is a car that slides and can be held there. 0 leaves the tire's own curve.
    float slideGrip = 0.0f;

    void start(World& world, Entity self);
    void fixedUpdate(World& world, Entity self, float dt);
    void update(World& world, Entity self, float dt);

    // ---- state, to read
    Body6 body;
    Controls controls;
    bool isOnGround = false;            // a wheel touches
    float angleOfAttack = 0;            // of its main wings, radians
    float stallAngle = 0.27f;           // of its main wings, radians
    bool isStalled = false;             // flying, with its main wings past that angle
    float speed() const;                // forward, meters per second
    float airspeed() const { return simd::length(body.velocity); }
    /// Sets the controls directly, for a vehicle nobody is driving (and for tests): the
    /// controls components then leave them alone.
    void setControls(const Controls& wanted);
    bool controlsAreGiven = false;

    bool hasStarted = false;
    Vec3 previousPosition{0, 0, 0};
    Quat previousOrientation{};
    Vec3 writtenPosition{0, 0, 0};      // what was last put in the Transform: if it differs, someone moved the entity
    Quat writtenRotation{};
};

/// A point of a vehicle that must not go through things: a nose, a wingtip, a roll bar.
/// On a child entity of a VehicleBody.
struct Bumper {
    float friction = 0.6f;
    Vec3 mount{0, 0, 0};
    bool hasMount = false;
};

/// One wheel as the simulation sees it. Read it for sounds, skid marks, dials.
struct WheelState {
    float compression = 0;              // of the spring, meters
    float load = 0;                     // on the tire, newtons
    float deflection = 0;               // how much the tire is flattened, meters
    float spin = 0;                     // radians per second
    float angle = 0;                    // how far it has turned, radians
    float slipRatio = 0, slipAngle = 0;
    float steer = 0;                    // radians
    bool isOnGround = false;
};

/// A wheel. Put it on a child of a VehicleBody, where the wheel is when the vehicle
/// stands. The entity is moved with the suspension and steered; a child called "Spin"
/// is turned as the wheel turns: put the wheel's model there, with its axle along x.
/// Alone it is a rigid wheel on a rigid leg; add Suspension and DeformableTire to it.
struct Wheel {
    float radius = 0.33f;
    float grip = 1.05f;                 // friction of the rubber on a dry road
    float steerAngle = 0;               // degrees at full lock; 0 does not steer
    std::string steersWith = "steer";   // "steer" (less lock at speed, as a car) or "yaw" (the pedals, as an aeroplane's nose wheel)
    bool isDriven = false;              // a Motor on the vehicle turns it
    float brakeTorque = 2000;           // newton meters with the brake fully on
    float handbrakeTorque = 0;
    bool antiLock = true;               // eases the brake when the wheel stops turning: a locked wheel slides and cannot steer

    void update(World& world, Entity self, float dt);

    WheelState state;
    float driveTorque = 0;              // from the Motor, this step
    bool easesWhenSpinning = false;     // traction control, set by the Motor
    Vec3 mount{0, 0, 0};                // where it stands, in the body's axes
    Vec3 restPosition{0, 0, 0};         // and in its parent's
    Quat restRotation{};
    bool hasMount = false;
    Entity spinPart;                    // its child "Spin", found once
};

/// A spring and a damper between a Wheel and the body. On the same entity as the Wheel.
struct Suspension {
    float travel = 0.2f;                // meters, from fully out to fully in. The wheel stands in the middle
    float springRate = 36000;           // newtons per meter
    float damping = 3600;               // newton seconds per meter
    float antiRoll = 9000;              // how strongly it is tied to the wheel on the other side
};

/// Air in the tire of a Wheel: it flattens under load, more when soft; a soft tire grips
/// a little more and rolls harder. On the same entity as the Wheel.
struct DeformableTire {
    float pressure = 2.2f;              // bar
    float stiffness = 250000;           // newtons per meter of flattening at 2.2 bar
};

/// An engine behind an automatic gearbox, turning every driven Wheel of the vehicle.
/// On the VehicleBody's entity or any child.
struct Motor {
    std::string engine = "i4";          // "i4", "v8", "diesel" or "electric"
    bool tractionControl = true;        // eases off a wheel that spins: without it, a strong engine needs a careful foot
    std::string soundFolder = "Audio";  // engine_i4_2600.wav and so on are looked for here; empty for silence

    void update(World& world, Entity self, float dt);

    int gear = 1;                       // -1 reverse, 1 and up forward
    float rpm = 900;
    float shiftPause = 0;               // seconds until the gearbox takes torque again
    float throttle = 0;                 // as it was this step
    // The engine is several recordings at different speeds, blended by how fast it turns:
    // one recording played faster or slower sounds like a toy.
    uint32_t voices[4] = {0, 0, 0, 0};
    std::string soundingEngine;
};

/// A flat piece that makes lift and drag from the air meeting it: a wing, a tailplane, a
/// fin, a spoiler on a car. On a child of a VehicleBody, placed where its lift acts. Its
/// upper side is the entity's +y and its leading edge is toward -z: tilt the entity to
/// set it at an angle, stand it on its side (turn it about z) to make a fin.
/// Children with a Hinge are its moving parts.
struct Wing {
    float area = 8.0f;                  // square meters
    float aspect = 7.4f;                // span squared over area: long thin wings lift better
    float stallAngle = 15.5f;           // degrees
    float drag = 0;                     // added to its own

    float angle = 0;                    // of attack, radians, as it is now
    Vec3 mount{0, 0, 0};
    Quat mountRotation{};
    bool hasMount = false;
};

/// A part that turns about an axis by what a control asks for: an aileron, an elevator, a
/// rudder, a flap, a steering wheel. The entity is turned. Under a Wing it also changes
/// what the wing does.
struct Hinge {
    std::string input = "pitch";        // which of the controls moves it
    float maxAngle = 20;                // degrees at full control
    bool isInverted = false;
    Vec3 axis{1, 0, 0};                 // in the entity's own axes
    float effect = 0.5f;                // under a Wing: how much of its angle counts as the whole wing's
    float drag = 0;                     // under a Wing: added at full angle

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);

    float angle = 0;                    // degrees, now. Positive puts a trailing edge down
    Quat restRotation{};
    bool hasStarted = false;
};

/// An engine with a propeller: pulls the vehicle along the entity's -z. A child called
/// "Blades" (or the entity itself if there is none) is spun.
struct Propeller {
    float power = 135;                  // of the engine, kilowatts
    float staticThrust = 3300;          // newtons at standstill and full throttle
    std::string sound = "Audio/propeller_2400.wav";

    void update(World& world, Entity self, float dt);

    Vec3 direction{0, 0, -1};           // in the body's axes
    bool hasMount = false;
    float angle = 0;
    float throttle = 0;
    uint32_t voice = 0;
};

/// A driver for a vehicle with wheels, reading the player's keys: W and S (the action
/// moveZ) accelerate and brake, and reverse from a standstill; A and D steer; Space is the
/// handbrake; 1 to 4 change the engine. On the VehicleBody's entity.
/// Writes the values "speed" (km/h), "rpm" and "gear".
struct DriveControls {
    bool switchesEngines = true;        // keys 1 to 4
    std::string skidSound = "Audio/skid.wav";
    std::string windSound = "Audio/wind.wav";

    void update(World& world, Entity self, float dt);
    uint32_t skidVoice = 0, windVoice = 0;
    bool hasSounds = false;
};

/// A pilot's hands and feet, reading the player's keys: S and W (moveZ) pull and push the
/// stick, A and D roll, Q and E are the rudder, Shift and Control the throttle, F the
/// flaps, B the brakes. On the VehicleBody's entity.
/// Writes the values "airspeed" (km/h), "altitude", "climb", "throttle", "flaps", "stall".
struct FlightControls {
    /// Keys are all or nothing, and a real stick held hard over throws an aeroplane about.
    /// With this on, the keys ask for a rate of roll and of pitch and the controls are moved
    /// to give it; the wings come level when the keys are let go, the nose is not allowed
    /// past the stall, and in a turn it is held up. Off: the keys are the stick itself.
    bool assist = true;
    float startThrottle = 0;            // 0 to 1: what the lever is set to when the scene starts
    std::string windSound = "Audio/wind.wav";
    std::string stallSound = "Audio/stall.wav";

    void update(World& world, Entity self, float dt);

    float flapsWanted = 0;
    float trim = 0;                     // what the assist has learned it takes to fly level
    float angleOfAttack = 0;            // of the main wings, radians
    float stallAngle = 0.27f;           // of the main wings, radians
    bool isStalled = false;
    bool hasStarted = false;
    uint32_t windVoice = 0, stallVoice = 0;
    bool hasSounds = false;
};

/// The torque an engine of a kind gives at a speed, with the pedal down. For dials and tests.
float engineTorque(const std::string& engine, float rpm);

/// The VehicleBody an entity is part of: its own, or the nearest above it.
Entity vehicleOf(const Scene& scene, Entity part);

/// Builds a car out of parts, without models: a body with a Motor and DriveControls, and
/// four wheels called "Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR", each with a
/// Suspension and a DeformableTire. `drive` is "front", "rear" or "all".
Entity makeCar(World& world, Vec3 position, const std::string& engine = "i4", const std::string& drive = "rear");
/// Builds a light aeroplane out of parts, without models: wings with ailerons and flaps,
/// a tailplane with an elevator, a fin with a rudder, three wheels, a propeller, bumpers.
Entity makeAirplane(World& world, Vec3 position);

}  // namespace molten::kit
