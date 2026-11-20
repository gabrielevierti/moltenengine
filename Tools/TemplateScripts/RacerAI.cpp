// RacerAI.cpp
// A rival who drives the car it is on round the circuit: it follows the line through
// "Waypoint 1", "Waypoint 2"... (the middle of the road), as fast as each corner allows.
//
//   Speed       from each corner's radius, the most its tyres can hold there (times `skill`),
//               and braking for the next corner early enough: worked out once, all the way round.
//   Steering    toward a point ahead on the line, farther ahead the faster it goes.
//   Traffic     a slower car close ahead: it moves to the side with more room and passes, or
//               waits behind it if there is none.
//   Rain        a wet road (the shared value "wetness") holds less: it brakes earlier.
//   Trouble     stuck against something, it backs out; upside down, it is put back on its wheels.
//
// It drives once the lights go out (RaceDirector sets the shared value "raceStarted"), or at
// once with `isDriving` on.
#include "MoltenMetal.hpp"
#include "Motor.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace molten;

struct RacerAI {
    float skill = 0.9f;                 // the share of its grip it dares to use: 1 is on the limit
    float topSpeed = 160.0f;            // km/h it will not go past
    float lineOffset = 0.0f;            // meters to the right of the line it prefers
    bool isDriving = false;

    std::vector<Vec3> line;
    std::vector<float> fastest;         // m/s at each point of the line
    std::vector<float> baseGrip;
    int at = 0;                         // the point of the line it is nearest
    float stuckFor = 0, backingFor = 0, overFor = 0;
    float sideways = 0;                 // meters from its preferred line, eased: where it is passing
    float wetGrip = 1;
    std::vector<Entity> wheels;         // its own, found once
    Entity motorPart;
    bool hasParts = false;

    void prepare(World& world) {
        line.clear();
        for (int k = 1; k < 2000; k++) {
            const Transform* point = world.scene.get<Transform>(world.find("Waypoint " + std::to_string(k)));
            if (!point) break;
            line.push_back(point->worldPosition());
        }
        size_t count = line.size();
        fastest.assign(count, topSpeed / 3.6f);
        if (count < 6) return;
        // Each corner's radius, through the points either side of it.
        for (size_t i = 0; i < count; i++) {
            Vec3 a = line[(i + count - 2) % count], b = line[i], c = line[(i + 2) % count];
            float ab = simd::length(b - a), bc = simd::length(c - b), ca = simd::length(a - c);
            Vec3 across = simd::cross(b - a, c - a);
            float twiceArea = simd::length(across);
            float radius = twiceArea > 1e-4f ? ab * bc * ca / (2.0f * twiceArea) : 1e6f;
            float grip = 1.18f * skill * wetGrip;                           // what the tyres hold, in g
            fastest[i] = std::min(fastest[i], std::sqrt(grip * 9.81f * radius));
        }
        // Braking: no point is faster than what can be slowed from it in time for the next. Twice round, as it is a loop.
        float braking = 8.5f * skill * wetGrip;
        for (int round = 0; round < 2; round++) {
            for (size_t step = 0; step < count; step++) {
                size_t i = count - 1 - step, next = (i + 1) % count;
                float length = simd::length(line[next] - line[i]);
                fastest[i] = std::min(fastest[i], std::sqrt(fastest[next] * fastest[next] + 2.0f * braking * length));
            }
        }
    }

    void start(World& world, Entity) { prepare(world); }

    static float speedOf(const kit::VehicleBody& car) { return simd::dot(car.body.velocity, car.body.forward()); }

    void fixedUpdate(World& world, Entity self, float dt) {
        kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(self);
        if (!car || line.size() < 6 || dt <= 0) return;
        // The road's grip in the rain: on the tyres, and in how fast it dares to go.
        float wetness = saturate(world.value("wetness"));
        float wanted = 1.0f - 0.37f * wetness;
        if (!hasParts) {
            world.scene.each<kit::Wheel>([&](Entity entity, kit::Wheel& part) {
                if (kit::vehicleOf(world.scene, entity) != self) return;
                wheels.push_back(entity);
                baseGrip.push_back(part.grip);
            });
            world.scene.each<Motor>([&](Entity entity, Motor&) {
                if (!motorPart.isValid() && kit::vehicleOf(world.scene, entity) == self) motorPart = entity;
            });
            hasParts = true;
        }
        for (size_t k = 0; k < wheels.size(); k++) {
            if (kit::Wheel* part = world.scene.get<kit::Wheel>(wheels[k])) part->grip = baseGrip[k] * wanted;
        }
        // It plans on a little less than that: a slide on a wet road takes longer to catch.
        float planned = 1.0f - 0.45f * wetness;
        if (std::abs(planned - wetGrip) > 0.04f) {
            wetGrip = planned;
            prepare(world);
        }
        if (!isDriving && world.value("raceStarted") <= 0) return;

        Vec3 position = car->body.position, forward = car->body.forward();
        float speed = speedOf(*car);
        size_t count = line.size();
        // The nearest point, looked for around the last one: the line crosses itself nowhere near.
        float nearest = 1e30f;
        int found = at;
        for (int d = -6; d <= 12; d++) {
            int i = int((at + d + int(count)) % int(count));
            float distance = simd::length_squared(line[size_t(i)] - position);
            if (distance < nearest) nearest = distance, found = i;
        }
        if (nearest > 30.0f * 30.0f) {
            for (size_t i = 0; i < count; i++) {
                float distance = simd::length_squared(line[i] - position);
                if (distance < nearest) nearest = distance, found = int(i);
            }
        }
        at = found;

        // Over on its side or its roof: back on its wheels after a moment, as the player's car is.
        overFor = car->body.up().y < 0.35f && simd::length(car->body.velocity) < 3.0f ? overFor + dt : 0.0f;
        if (overFor > 1.5f) {
            Vec3 along = line[size_t((at + 1) % int(count))] - line[size_t(at)];
            float heading = std::atan2(-along.x, -along.z);
            Quat upright = axisAngle(Vec3{0, 1, 0}, heading);
            car->body.position = line[size_t(at)] + Vec3{0, 0.6f, 0};
            car->body.orientation = upright;
            car->body.velocity = Vec3{0, 0, 0};
            car->body.angularVelocity = Vec3{0, 0, 0};
            if (Transform* place = world.scene.get<Transform>(self)) {
                place->position = car->body.position;
                place->rotation = upright;
            }
            overFor = 0;
            return;
        }

        // Traffic: another car close ahead and slower, and which side has more room.
        float passing = 0, followSpeed = 1e9f;
        Vec3 right = simd::normalize(simd::cross(forward, Vec3{0, 1, 0}));
        world.scene.each<kit::VehicleBody, Transform>([&](Entity other, kit::VehicleBody& them, Transform& place) {
            if (other == self || !place.isActiveInHierarchy) return;
            Vec3 away = them.body.position - position;
            float ahead = simd::dot(away, forward), aside = simd::dot(away, right);
            if (ahead < 1.5f || ahead > 16.0f || std::abs(aside) > 3.2f) return;
            float theirs = speedOf(them);
            if (theirs > speed - 0.5f && ahead > 7.0f) return;        // pulling away: no matter
            // Move to the side it is not on, as far as the road allows.
            passing = aside > 0 ? -2.6f : 2.6f;
            if (ahead < 7.0f) followSpeed = std::min(followSpeed, theirs + 1.0f);
        });
        sideways += (lineOffset + passing - sideways) * std::min(1.0f, dt * 1.5f);

        // The point to steer at: ahead on the line, farther the faster it goes, moved to its side.
        float reach = 5.0f + std::abs(speed) * 0.42f;
        int target = at;
        float walked = 0;
        while (walked < reach) {
            int next = (target + 1) % int(count);
            walked += simd::length(line[size_t(next)] - line[size_t(target)]);
            target = next;
        }
        Vec3 along = simd::normalize(line[size_t((target + 1) % int(count))] - line[size_t(target)]);
        Vec3 aim = line[size_t(target)] + simd::normalize(simd::cross(along, Vec3{0, 1, 0})) * sideways;
        Vec3 local = rotate(simd::conjugate(car->body.orientation), aim - position);
        float angle = std::atan2(local.x, -local.z);                    // right of straight ahead is positive

        // How fast it may go: the slowest of the points just ahead, and the car in its way.
        float allowed = 1e9f;
        int look = at;
        float lookedAt = 0;
        while (lookedAt < reach + 6.0f) {
            allowed = std::min(allowed, fastest[size_t(look)]);
            int next = (look + 1) % int(count);
            lookedAt += simd::length(line[size_t(next)] - line[size_t(look)]);
            look = next;
        }
        allowed = std::min(allowed, followSpeed);

        kit::Controls controls;
        // Stuck: pushing and not moving, for a while. Back out with the wheels the other way.
        stuckFor = speed < 1.0f && backingFor <= 0 ? stuckFor + dt : 0.0f;
        if (stuckFor > 2.0f) {
            backingFor = 1.6f;
            stuckFor = 0;
        }
        Motor* motor = world.scene.get<Motor>(motorPart);
        if (backingFor > 0) {
            // In reverse, the wheel the other way, gently: out of the wall, pointing back at the road.
            backingFor -= dt;
            if (motor) motor->gear = -1;
            controls.throttle = 0.6f;
            controls.steer = angle > 0 ? -1.0f : 1.0f;
            car->setControls(controls);
            car->controlsAreGiven = true;
            return;
        }
        if (motor && motor->gear < 1) motor->gear = 1;
        // Toward the point ahead, and against the car's own turning: what keeps a sliding tail
        // from swinging it from one side to the other.
        float yawRate = car->body.angularVelocity.y;                    // positive is turning left
        controls.steer = std::clamp(angle * 2.2f + yawRate * 0.14f, -1.0f, 1.0f);
        // How far it is sliding: the angle between where it points and where it goes.
        float slip = degrees(std::atan2(simd::dot(car->body.velocity, right), std::max(std::abs(speed), 2.0f)));
        float short_ = allowed - speed;
        if (short_ > 0.5f) {
            controls.throttle = std::clamp(0.35f + short_ * 0.25f, 0.0f, 1.0f);
        } else if (short_ < -0.8f) {
            controls.brake = std::clamp(-short_ * 0.22f, 0.0f, 1.0f);
        } else {
            controls.throttle = 0.25f;
        }
        // Easing off in a slide: the throttle only swings the tail out more.
        if (std::abs(controls.steer) > 0.85f && speed > 15.0f) controls.throttle *= 0.5f;
        if (std::abs(slip) > 7.0f) controls.throttle = std::min(controls.throttle, std::abs(slip) > 12.0f ? 0.0f : 0.3f);
        car->setControls(controls);
        car->controlsAreGiven = true;
    }
};

MOLTEN_COMPONENT(RacerAI, MOLTEN_FIELD(skill).range(0.3f, 1.1f).help("The share of its grip it dares to use: 1 is on the limit"),
                 MOLTEN_FIELD(topSpeed).range(30, 400), MOLTEN_FIELD(lineOffset).range(-3, 3).help("Meters to the right of the line it prefers"),
                 MOLTEN_FIELD(isDriving).help("On: it drives at once. Off: when the race starts (the shared value raceStarted)"))
