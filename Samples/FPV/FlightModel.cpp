#include "FlightModel.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

namespace fpv {

namespace {

const float kGravity = 9.81f;
const float kAirDensity = 1.225f;
const float kNominalCell = 3.85f;           // the voltage at which a motor reaches "full speed"

// Where each motor is and what it does. Order: front right, front left, rear right, rear left.
const float kSideX[4] = {1, -1, 1, -1};
const float kSideZ[4] = {-1, -1, 1, 1};
// Thrust in front lifts the nose (a turn about +X); thrust on the right lifts the right
// side (a turn about +Z). Propellers turn in alternate directions, and each twists the
// body the opposite way: speeding up front-left and rear-right turns the drone about +Y.
const float kMixPitch[4] = {1, 1, -1, -1};
const float kMixRoll[4] = {1, -1, 1, -1};
const float kMixYaw[4] = {-1, 1, 1, -1};

/// How much of the way a filter with this time constant goes in one step.
float blend(float dt, float timeConstant) {
    return 1.0f - std::exp(-dt / std::max(timeConstant, 1e-5f));
}

Vec3 clampEach(Vec3 v, float limit) {
    return Vec3{std::clamp(v.x, -limit, limit), std::clamp(v.y, -limit, limit), std::clamp(v.z, -limit, limit)};
}

}  // namespace

Airframe Airframe::fiveInch() {
    Airframe a;
    a.name = "5-inch, 6S";
    return a;                               // the defaults are this drone
}

Airframe Airframe::whoop() {
    Airframe a;
    a.name = "Whoop 95 mm, 2S";
    a.mass = 0.082f;
    a.inertia = Vec3{6.4e-5f, 1.18e-4f, 6.4e-5f};
    a.armX = a.armZ = 0.0336f;
    a.radius = 0.075f;
    a.maxThrust = 0.72f;
    a.idle = 0.06f;
    a.motorTimeUp = 0.020f;
    a.motorTimeDown = 0.028f;
    a.maxRevs = 950.0f;
    a.propRadius = 0.0255f;
    a.pitchSpeed = 38.0f;
    a.torqueRatio = 0.006f;
    a.rotorInertia = 1.0e-7f;
    a.dragArea = Vec3{0.009f, 0.011f, 0.008f};
    a.rotorDrag = 0.06f;
    a.cells = 2;
    a.capacity = 0.45f;
    a.resistance = 0.075f;
    a.maxCurrent = 5.5f;
    a.idleCurrent = 0.55f;
    a.cameraTilt = 25.0f;
    a.range = 180.0f;
    a.crashSpeed = 5.5f;                    // light, and its propellers are guarded
    return a;
}

float actualRate(float stick, float center, float maximum, float expo) {
    stick = std::clamp(stick, -1.0f, 1.0f);
    float curve = std::abs(stick) * (std::pow(stick, 5.0f) * expo + stick * (1.0f - expo));
    return stick * center + std::max(0.0f, maximum - center) * curve;
}

float restingCellVoltage(float charge) {
    // Measured curves all look like this: a quick drop from 4.2, a long shallow slope,
    // and a cliff at the end. Straight lines between a few points are close enough.
    static const float points[8][2] = {{0.0f, 3.20f}, {0.07f, 3.52f}, {0.15f, 3.66f}, {0.3f, 3.74f},
                                       {0.5f, 3.82f}, {0.7f, 3.92f},  {0.9f, 4.06f},  {1.0f, 4.20f}};
    charge = saturate(charge);
    for (int i = 1; i < 8; i++) {
        if (charge <= points[i][0]) {
            float t = (charge - points[i - 1][0]) / (points[i][0] - points[i - 1][0]);
            return molten::lerp(points[i - 1][1], points[i][1], t);
        }
    }
    return 4.2f;
}

float groundEffect(float height, float propRadius) {
    // Cheeseman and Bennett's formula for a rotor near the ground. Four rotors side by
    // side trap more air than one, so the effect reaches about twice as high.
    float reach = propRadius * 2.0f;
    float ratio = reach / (4.0f * std::max(height, reach * 0.3f));
    return std::min(1.0f / (1.0f - ratio * ratio), 1.5f);
}

float propwashStrength(float sinkSpeed, float sideSpeed, float inducedSpeed) {
    // Worst when sinking at about the speed of the downwash, so that the drone stays
    // inside it. Sinking much faster leaves it behind; moving sideways steps out of it.
    float ratio = sinkSpeed / std::max(inducedSpeed, 0.1f);
    float sinking = smoothstep(0.25f, 0.7f, ratio) * (1.0f - smoothstep(1.6f, 2.4f, ratio));
    float clear = saturate(sideSpeed / (1.5f * std::max(inducedSpeed, 0.1f)));
    return sinking * (1.0f - clear);
}

// ---------------------------------------------------------------- the drone

Drone::Drone(const Airframe& frame) : airframe(frame) {
    rotation = axisAngle(Vec3{0, 1, 0}, 0);
    tune();
    chargeBattery();
}

void Drone::place(Vec3 where, float heading) {
    position = where;
    velocity = Vec3{0, 0, 0};
    rotation = axisAngle(Vec3{0, 1, 0}, heading);
    angularVelocity = Vec3{0, 0, 0};
    for (float& motor : motors) motor = 0;
    for (float& gust : gust_) gust = 0;
    gyro_ = integral_ = derivative_ = Vec3{0, 0, 0};
    heading_ = heading;
    propwash = 0;
    thrust = 0;
}

void Drone::chargeBattery() {
    battery.used = 0;
    battery.current = 0;
    battery.voltage = restingCellVoltage(1) * float(airframe.cells);
}

float Drone::hoverThrottle() const {
    float scale = std::max(battery.voltage / (kNominalCell * float(airframe.cells)), 0.1f);
    return saturate((hoverSpeed_ / scale - airframe.idle) / (1.0f - airframe.idle));
}

/// Chooses the gains of the flight controller. A pilot does this by flying, listening
/// and changing numbers; here they are worked out from the airframe, so that a heavy
/// 5-inch and a tiny whoop both answer the sticks crisply.
///
/// Around a hover, one unit of "more roll" from the controller changes each motor's
/// thrust by a known amount, which gives a known angular acceleration: call it K. With
/// motors that take a time T to respond, the loop behaves like a mass on a spring:
///   stiffness K * kp / T,  damping (1 + K * kd) / T.
/// Asking for a natural frequency and a damping ratio gives kp and kd.
void Drone::tune() {
    const Airframe& a = airframe;
    hoverSpeed_ = std::sqrt(a.mass * kGravity / (4.0f * a.maxThrust));
    float hoverThrust = a.mass * kGravity / 4.0f;
    inducedSpeed_ = std::sqrt(hoverThrust / (2.0f * kAirDensity * kPi * a.propRadius * a.propRadius));

    float thrustPerUnit = 2.0f * a.maxThrust * hoverSpeed_ * (1.0f - a.idle);       // of one motor, per unit of command
    Vec3 lever{a.armZ, a.torqueRatio, a.armX};                                      // pitch, yaw, roll
    Vec3 frequency{42.0f, 22.0f, 42.0f};                                            // radians per second
    Vec3 damping{0.75f, 0.9f, 0.75f};
    for (int axis = 0; axis < 3; axis++) {
        float gain = 4.0f * thrustPerUnit * lever[axis] / a.inertia[axis];
        kp_[axis] = frequency[axis] * frequency[axis] * a.motorTimeUp / gain;
        kd_[axis] = std::max(0.0f, 2.0f * damping[axis] * frequency[axis] * a.motorTimeUp - 1.0f) / gain;
        ki_[axis] = kp_[axis] * frequency[axis] * 0.25f;
    }
}

/// The rates of rotation the pilot is asking for, about the drone's own axes.
Vec3 Drone::wantedRates(const StickInput& sticks, FlightMode mode) {
    const Airframe& a = airframe;
    float yawRate = radians(actualRate(sticks.yaw, a.centerRate, a.maxRate, a.rateExpo));
    if (mode == FlightMode::Acro) {
        // Nose down is a turn about -X, a turn to the right about -Y, a roll to the right about -Z.
        return Vec3{-radians(actualRate(sticks.pitch, a.centerRate, a.maxRate, a.rateExpo)), -yawRate,
                    -radians(actualRate(sticks.roll, a.centerRate, a.maxRate, a.rateExpo))};
    }

    // Angle mode. Where is the nose pointing, seen from above? Straight up or down there
    // is no answer, so the last one is kept.
    Vec3 nose = forward();
    if (nose.x * nose.x + nose.z * nose.z > 0.09f) heading_ = std::atan2(-nose.x, -nose.z);
    Vec3 ahead{-std::sin(heading_), 0, -std::cos(heading_)};
    Vec3 side{std::cos(heading_), 0, -std::sin(heading_)};

    // The sticks say how far "up" should lean forward and to the right.
    float limit = radians(a.maxTilt);
    Vec3 wantedUp = simd::normalize(Vec3{0, 1, 0} + ahead * std::tan(sticks.pitch * limit) + side * std::tan(sticks.roll * limit));
    // The turn that brings the present "up" onto the wanted one, by the shortest way.
    Vec3 currentUp = up();
    Vec3 axis = simd::cross(currentUp, wantedUp);
    float sine = simd::length(axis);
    float angle = std::atan2(sine, simd::dot(currentUp, wantedUp));
    axis = sine > 1e-4f ? axis / sine : nose;                           // upside down exactly: roll over
    Vec3 error = rotate(simd::conjugate(rotation), axis * angle);       // in the drone's own axes

    const float levelStrength = 7.0f;       // radians per second of correction per radian of error
    float fastest = radians(a.maxRate);
    return Vec3{std::clamp(error.x * levelStrength, -fastest, fastest), -yawRate * 0.6f,
                std::clamp(error.z * levelStrength, -fastest, fastest)};
}

void Drone::step(const StickInput& sticks, FlightMode mode, float dt) {
    const Airframe& a = airframe;
    float throttle = saturate(sticks.throttle);

    // ---- 1. the flight controller: what the pilot wants, compared with what the gyro says
    Vec3 wanted = wantedRates(sticks, mode);
    Vec3 previousGyro = gyro_;
    gyro_ += (angularVelocity - gyro_) * blend(dt, 1.0f / (2.0f * kPi * 150.0f));      // a 150 Hz low-pass, as on the real thing
    Vec3 error = wanted - gyro_;
    Vec3 slope = (gyro_ - previousGyro) / dt;
    derivative_ += (slope - derivative_) * blend(dt, 1.0f / (2.0f * kPi * 90.0f));

    // At high throttle the same command moves more thrust: back off, as Betaflight's
    // "TPA" does, or the drone would shake.
    float attenuation = 1.0f - 0.45f * saturate((throttle - 0.4f) / 0.6f);
    Vec3 command{0, 0, 0};
    for (int axis = 0; axis < 3; axis++) {
        // The integral removes a steady error (a bent propeller, wind). It is left alone
        // during fast moves, when it would only wind up and cause a bounce at the end.
        if (std::abs(error[axis]) < 2.0f) integral_[axis] = std::clamp(integral_[axis] + ki_[axis] * error[axis] * dt, -0.12f, 0.12f);
        command[axis] = (kp_[axis] * error[axis] - kd_[axis] * derivative_[axis]) * attenuation + integral_[axis];
    }
    command = Vec3{std::clamp(command.x, -0.6f, 0.6f), std::clamp(command.y, -0.4f, 0.4f), std::clamp(command.z, -0.6f, 0.6f)};
    if (!isArmed) integral_ = Vec3{0, 0, 0};

    // ---- 2. the mixer: four motor commands from one throttle and three corrections
    float mix[4], lowest = 0, highest = 0;
    for (int i = 0; i < 4; i++) {
        mix[i] = command.x * kMixPitch[i] + command.z * kMixRoll[i] + command.y * kMixYaw[i];
        lowest = std::min(lowest, mix[i]);
        highest = std::max(highest, mix[i]);
    }
    // No motor can go below zero or above full. Rather than clip one motor (which would
    // turn a roll into a roll and a yaw), shrink the corrections to fit and move the
    // throttle out of their way: "air mode". Control survives at zero and at full throttle.
    float span = highest - lowest;
    if (span > 1.0f) {
        for (float& m : mix) m /= span;
        lowest /= span;
        highest /= span;
    }
    float base = std::clamp(throttle, -lowest, 1.0f - highest);

    // ---- 3. motors: they chase their command, faster up than down, and no faster than the battery allows
    float voltageScale = std::clamp(battery.voltage / (kNominalCell * float(a.cells)), 0.0f, 1.2f);
    float acceleration[4];
    for (int i = 0; i < 4; i++) {
        float target = isArmed ? (a.idle + (1.0f - a.idle) * saturate(base + mix[i])) * voltageScale : 0.0f;
        float before = motors[i];
        motors[i] += (target - before) * blend(dt, target > before ? a.motorTimeUp : a.motorTimeDown);
        acceleration[i] = (motors[i] - before) / dt;
    }

    // ---- 4. the air
    Quat toBody = simd::conjugate(rotation);
    Vec3 airVelocity = rotate(toBody, velocity - wind);                 // how the drone moves through the air, in its own axes
    float sideSpeed = std::sqrt(airVelocity.x * airVelocity.x + airVelocity.z * airVelocity.z);
    propwash = propwashStrength(-airVelocity.y, sideSpeed, inducedSpeed_ * std::max(motors[0] + motors[1] + motors[2] + motors[3], 0.4f) /
                                                               (4.0f * hoverSpeed_));
    float cushion = molten::lerp(1.0f, groundEffect(heightAboveGround, a.propRadius), saturate(up().y));

    Vec3 torque{0, 0, 0};
    thrust = 0;
    float speedSum = 0;
    for (int i = 0; i < 4; i++) {
        float x = kSideX[i] * a.armX, z = kSideZ[i] * a.armZ;
        // Air already moving down through a propeller leaves it less to push against. Each
        // propeller sees the drone's motion plus its own, from the drone turning: that
        // is where most of a quad's resistance to spinning comes from.
        float inflow = airVelocity.y + angularVelocity.z * x - angularVelocity.x * z;
        float bite = std::clamp(1.0f - inflow / (a.pitchSpeed * std::max(motors[i], 0.05f)), 0.0f, 1.25f);
        // In its own wake a propeller meets air that is already swirling: less lift, and uneven.
        gust_[i] += (random_.range(-1.0f, 1.0f) * 2.2f - gust_[i]) * blend(dt, 0.02f);
        float wake = 1.0f - 0.15f * propwash + 0.5f * propwash * std::clamp(gust_[i], -1.0f, 1.0f);

        float force = a.maxThrust * motors[i] * motors[i] * bite * cushion * wake;
        thrust += force;
        speedSum += motors[i];
        torque.x += -z * force;
        torque.z += x * force;
        // Yaw: the drag of the spinning propeller, and the kick of changing its speed.
        torque.y += kMixYaw[i] * (a.torqueRatio * force + a.rotorInertia * (2.0f * kPi * a.maxRevs) * acceleration[i]);
    }

    // Drag. The body's grows with the square of the speed and depends on which side
    // faces the wind; the propellers' grows in proportion and only acts sideways.
    Vec3 drag = -0.5f * kAirDensity * a.dragArea * simd::abs(airVelocity) * airVelocity;
    float rotorDrag = a.rotorDrag * speedSum / (4.0f * hoverSpeed_);
    drag.x -= rotorDrag * airVelocity.x;
    drag.z -= rotorDrag * airVelocity.z;

    // ---- 5. the body: Newton for the motion, Euler's equation for the rotation
    Vec3 force = rotate(rotation, Vec3{0, thrust, 0} + drag);
    velocity += (force / a.mass + Vec3{0, -kGravity, 0}) * dt;
    position += velocity * dt;

    Vec3 momentum = a.inertia * angularVelocity;
    angularVelocity += (torque - simd::cross(angularVelocity, momentum)) / a.inertia * dt;
    angularVelocity = clampEach(angularVelocity, 60.0f);                // nothing real spins faster; keeps a crash from blowing up
    float turned = simd::length(angularVelocity) * dt;
    if (turned > 1e-9f) rotation = simd::normalize(rotation * axisAngle(angularVelocity / simd::length(angularVelocity), turned));

    // ---- 6. the battery: current rises steeply with motor speed (a little less than its
    // cube: motors waste more at low speed), and the voltage sags with the current
    float current = a.idleCurrent;
    for (float motor : motors) current += a.maxCurrent * std::pow(std::max(motor, 0.0f), 2.6f);
    battery.current = current;
    battery.used = std::min(battery.used + current * dt / 3600.0f, a.capacity);
    float loaded = restingCellVoltage(charge()) * float(a.cells) - current * a.resistance;
    battery.voltage += (std::max(loaded, 0.0f) - battery.voltage) * blend(dt, 0.03f);
}

}  // namespace fpv
