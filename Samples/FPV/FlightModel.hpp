// FlightModel.hpp
// How the drone flies. Pure math: no graphics, no input devices, so it can be tested
// by calling step() in a loop.
//
// Nothing here is scripted. The model is a rigid body with four motors, and what a
// pilot feels comes out of the physics:
//
//   sticks -> rates wanted       the "Actual" rates curve of Betaflight, or self-leveling
//          -> flight controller  a PID loop per axis compares them with the gyro
//          -> mixer              turns "more roll, less yaw" into four motor commands
//          -> motors             that take time to change speed, and sag with the battery
//          -> propellers         thrust grows with the square of their speed and fades
//                                as the air already moves through them
//          -> the body           thrust, torque, drag, gravity, wind
//
// From this come the things FPV pilots know: a throttle that is anything but linear,
// the small overshoot at the end of a roll, the wobble when dropping through your own
// propwash, yaw that is weaker than roll, a pack that pulls hard for a minute and then
// less and less, the cushion of air just above the ground.
//
// Axes, as everywhere in the engine: +X is the drone's right, +Y its up, -Z its nose.
// Units are SI: meters, seconds, kilograms, newtons, radians, volts, amperes.
#pragma once

#include "Math/Math.hpp"

namespace fpv {

/// What the pilot asks for. Mode 2, like most radios: the left stick is throttle and yaw,
/// the right stick is pitch and roll.
struct StickInput {
    float throttle = 0;         // 0 closed, 1 wide open
    float yaw = 0;              // -1 turn left, 1 turn right
    float pitch = 0;            // 1 nose down (stick pushed forward), -1 nose up
    float roll = 0;             // -1 lean left, 1 lean right
};

enum class FlightMode {
    Angle,      // the sticks set an angle and the drone levels itself when they are released
    Acro,       // the sticks set a rate of rotation and the drone keeps whatever attitude it has
};

/// Everything that makes one drone different from another. The numbers are the kind
/// found on a product page or measured on a thrust stand.
struct Airframe {
    const char* name = "";

    // The body.
    float mass = 0.65f;                     // with its battery
    molten::Vec3 inertia{0.003f, 0.0052f, 0.0024f};     // resistance to turning about x (pitch), y (yaw), z (roll)
    float armX = 0.0795f, armZ = 0.0795f;   // the motors are at (+-armX, 0, +-armZ) from the center
    float radius = 0.17f;                   // of the sphere used for collisions

    // Motors and propellers.
    float maxThrust = 17.0f;                // of one motor, standing still, at the pack's nominal voltage
    float idle = 0.055f;                    // armed motors never stop: their speed at zero throttle
    float motorTimeUp = 0.032f;             // seconds to cover most of a change of speed, speeding up
    float motorTimeDown = 0.045f;           // and slowing down
    float maxRevs = 520.0f;                 // turns per second at full speed
    float propRadius = 0.0647f;
    float pitchSpeed = 56.0f;               // how fast a propeller at full speed moves the air: thrust fades toward it
    float torqueRatio = 0.0125f;            // newton-meters of twist on the body per newton of thrust
    float rotorInertia = 4.5e-6f;           // of one propeller and motor bell

    // Air.
    molten::Vec3 dragArea{0.012f, 0.018f, 0.009f};      // drag coefficient times area, seen from the side, from above, from the front
    float rotorDrag = 0.28f;                // newtons per meter per second of sideways motion, in a hover

    // Battery.
    int cells = 6;
    float capacity = 1.3f;                  // amp-hours
    float resistance = 0.040f;              // of the whole pack and its wiring, in ohms
    float maxCurrent = 35.0f;               // of one motor at full speed
    float idleCurrent = 0.9f;               // flight controller, receiver, video transmitter

    // Rates, as entered in Betaflight ("Actual" rates), in degrees per second.
    float centerRate = 70.0f;               // per unit of stick, around the center
    float maxRate = 670.0f;                 // at full stick
    float rateExpo = 0.0f;
    float maxTilt = 55.0f;                  // angle mode: degrees at full stick

    float cameraTilt = 30.0f;               // degrees the camera looks up
    float range = 320.0f;                   // meters at which the radio link is lost
    float crashSpeed = 3.5f;                // meters per second: hitting something faster than this ends the flight

    /// A 5-inch freestyle quad on six cells: about 650 grams and 5.5 kilograms of thrust.
    static Airframe fiveInch();
    /// A ducted 2-inch "whoop" on two cells, 95 mm across: the size of the BETAFPV Cetus X.
    static Airframe whoop();
};

struct Battery {
    float used = 0;                         // amp-hours taken out since it was charged
    float voltage = 0;                      // of the pack, under the present load
    float current = 0;                      // amperes being drawn
};

class Drone {
public:
    explicit Drone(const Airframe& airframe = Airframe::fiveInch());

    Airframe airframe;
    molten::Vec3 position{0, 0, 0};
    molten::Vec3 velocity{0, 0, 0};
    molten::Quat rotation{};                    // no rotation: level, nose toward -Z
    molten::Vec3 angularVelocity{0, 0, 0};      // radians per second about the drone's own axes
    /// The speed of each motor, 0 to about 1: front right, front left, rear right, rear left.
    float motors[4] = {0, 0, 0, 0};
    Battery battery;
    /// Disarmed, the motors stop and the drone is a falling object.
    bool isArmed = true;

    // ---- the world around it, set by the game before each step
    molten::Vec3 wind{0, 0, 0};
    float heightAboveGround = 100.0f;

    // ---- what the last step did, for instruments and tests
    float propwash = 0;                     // 0 clean air ... 1 sinking straight into its own wake
    float thrust = 0;                       // newtons, all four motors

    /// Advances the simulation by `dt` seconds. Meant to be called several hundred times
    /// per second: the flight controller inside runs once per call, as a real one does.
    void step(const StickInput& sticks, FlightMode mode, float dt);

    /// Level, still, motors stopped, facing `heading` (radians, 0 toward -Z, positive to the left).
    void place(molten::Vec3 where, float heading);
    void chargeBattery();

    molten::Vec3 forward() const { return molten::rotate(rotation, molten::Vec3{0, 0, -1}); }
    molten::Vec3 up() const { return molten::rotate(rotation, molten::Vec3{0, 1, 0}); }
    molten::Vec3 right() const { return molten::rotate(rotation, molten::Vec3{1, 0, 0}); }
    float speed() const { return simd::length(velocity); }
    /// The part of the battery that is left, 1 full to 0 empty.
    float charge() const { return molten::saturate(1.0f - battery.used / airframe.capacity); }
    float cellVoltage() const { return battery.voltage / float(airframe.cells); }
    /// The throttle that holds the drone in the air right now, with this battery.
    float hoverThrottle() const;

private:
    void tune();
    molten::Vec3 wantedRates(const StickInput& sticks, FlightMode mode);

    // Flight controller.
    molten::Vec3 kp_{0, 0, 0}, ki_{0, 0, 0}, kd_{0, 0, 0};
    molten::Vec3 gyro_{0, 0, 0};                // the filtered reading
    molten::Vec3 integral_{0, 0, 0};
    molten::Vec3 derivative_{0, 0, 0};
    float heading_ = 0;                     // remembered while the nose points straight up or down
    float hoverSpeed_ = 0.3f;               // motor speed that carries the weight at nominal voltage
    float inducedSpeed_ = 7.0f;             // how fast the air leaves the propellers in a hover
    molten::Random random_{2024};
    float gust_[4] = {0, 0, 0, 0};          // the turbulence each propeller is in
};

/// Betaflight's "Actual" rates: degrees per second for a stick position from -1 to 1.
/// `center` is the slope around the middle, `maximum` the rate at full stick; `expo`
/// (0 to 1) flattens the middle further without lowering the maximum.
float actualRate(float stick, float center, float maximum, float expo);

/// The voltage of one lithium-polymer cell at rest: 4.2 full, about 3.8 at half, falling away below 3.5.
float restingCellVoltage(float charge);

/// How much more thrust a propeller makes this close to the ground: 1 far away, more near it.
float groundEffect(float height, float propRadius);

/// How badly a drone that sinks at `sinkSpeed` and moves sideways at `sideSpeed` is caught
/// in the air its own propellers pushed down, from 0 to 1. `inducedSpeed` is how fast
/// that air moves in a hover.
float propwashStrength(float sinkSpeed, float sideSpeed, float inducedSpeed);

}  // namespace fpv
