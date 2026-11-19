// The drone game's flight model and race rules. No graphics involved: the model is
// stepped in a loop, exactly as the game steps it, and asked where the drone ended up.
#include "Test.hpp"

#include "FPV/FlightModel.hpp"
#include "FPV/Track.hpp"

using namespace molten;
using namespace fpv;

namespace {

const float kStep = 1.0f / 480.0f;

void fly(Drone& drone, const StickInput& sticks, FlightMode mode, float seconds) {
    for (int i = 0; i < int(seconds / kStep); i++) drone.step(sticks, mode, kStep);
}

/// A drone high in the air, hovering, with its motors already up to speed.
Drone hovering(const Airframe& airframe, StickInput& sticks) {
    Drone drone(airframe);
    drone.place(Vec3{0, 100, 0}, 0);
    sticks = StickInput{};
    sticks.throttle = drone.hoverThrottle();
    fly(drone, sticks, FlightMode::Angle, 0.5f);
    sticks.throttle = drone.hoverThrottle();
    return drone;
}

}  // namespace

TEST(fpv_hover_throttle_holds_the_drone_and_is_far_below_half) {
    for (const Airframe& airframe : {Airframe::fiveInch(), Airframe::whoop()}) {
        StickInput sticks;
        Drone drone = hovering(airframe, sticks);
        float before = drone.position.y;
        fly(drone, sticks, FlightMode::Angle, 2);
        CHECK(std::abs(drone.position.y - before) < 1.5f);
        CHECK(drone.speed() < 1.5f);
        CHECK_NEAR(drone.up().y, 1, 1e-3);
    }
    // Thrust grows with the square of motor speed: a strong quad hovers on a quarter of its throttle.
    Drone strong(Airframe::fiveInch());
    CHECK(strong.hoverThrottle() > 0.15f && strong.hoverThrottle() < 0.32f);
    Drone weak(Airframe::whoop());
    CHECK(weak.hoverThrottle() > 0.35f && weak.hoverThrottle() < 0.55f);
}

TEST(fpv_sticks_move_the_drone_the_way_a_pilot_expects) {
    StickInput sticks;
    Drone forward = hovering(Airframe::fiveInch(), sticks);
    sticks.pitch = 0.5f;
    fly(forward, sticks, FlightMode::Angle, 1.5f);
    CHECK(forward.position.z < -3);                         // stick forward: toward -Z, where the nose points
    CHECK(std::abs(forward.position.x) < 0.5f);

    Drone sideways = hovering(Airframe::fiveInch(), sticks);
    sticks.roll = 0.5f;
    fly(sideways, sticks, FlightMode::Angle, 1.5f);
    CHECK(sideways.position.x > 3);                         // stick right: toward +X

    Drone turning = hovering(Airframe::fiveInch(), sticks);
    sticks.yaw = 0.3f;
    fly(turning, sticks, FlightMode::Angle, 0.5f);
    CHECK(turning.forward().x > 0.2f);                      // yaw right: the nose swings toward +X
    CHECK_NEAR(turning.up().y, 1, 0.01);

    Drone climbing = hovering(Airframe::fiveInch(), sticks);
    sticks.throttle = 1;
    fly(climbing, sticks, FlightMode::Angle, 0.5f);
    CHECK(climbing.velocity.y > 15);                        // more than three g: a real 5-inch leaps
}

TEST(fpv_acro_keeps_its_attitude_and_angle_mode_recovers_from_upside_down) {
    StickInput sticks;
    Drone acro = hovering(Airframe::fiveInch(), sticks);
    sticks.roll = 0.6f;
    fly(acro, sticks, FlightMode::Acro, 0.3f);
    sticks.roll = 0;
    fly(acro, sticks, FlightMode::Acro, 0.2f);              // the roll stops
    float tilt = acro.up().y;
    CHECK(tilt < 0.75f);
    fly(acro, sticks, FlightMode::Acro, 0.5f);
    CHECK_NEAR(acro.up().y, tilt, 0.03f);                   // hands off, it stays where it was left

    Drone angle = hovering(Airframe::fiveInch(), sticks);
    angle.rotation = axisAngle(Vec3{0, 0, 1}, radians(178));
    fly(angle, sticks, FlightMode::Angle, 1.2f);
    CHECK(angle.up().y > 0.99f);
}

TEST(fpv_rates_follow_betaflight_actual_rates) {
    CHECK_NEAR(actualRate(0, 70, 670, 0), 0, 1e-4);
    CHECK_NEAR(actualRate(1, 70, 670, 0), 670, 1e-3);
    CHECK_NEAR(actualRate(-1, 70, 670, 0.5f), -670, 1e-3);
    CHECK_NEAR(actualRate(0.001f, 70, 670, 0) / 0.001f, 70, 1.0f);          // the slope in the middle is the center rate
    CHECK_NEAR(actualRate(0.5f, 70, 670, 0), 35 + 600 * 0.25f, 1e-3);
    CHECK(actualRate(0.5f, 70, 670, 0.8f) < actualRate(0.5f, 70, 670, 0));  // expo flattens the middle
}

TEST(fpv_a_roll_reaches_its_rate_quickly_with_little_overshoot) {
    for (const Airframe& airframe : {Airframe::fiveInch(), Airframe::whoop()}) {
        StickInput sticks;
        Drone drone = hovering(airframe, sticks);
        sticks.roll = 0.75f;
        float wanted = actualRate(0.75f, airframe.centerRate, airframe.maxRate, airframe.rateExpo);
        float peak = 0, reached = -1;
        for (int i = 0; i < 480; i++) {
            drone.step(sticks, FlightMode::Acro, kStep);
            float rate = -degrees(drone.angularVelocity.z);
            peak = std::max(peak, rate);
            if (reached < 0 && rate > 0.63f * wanted) reached = float(i) * kStep;
        }
        CHECK(reached > 0.015f && reached < 0.08f);          // motors need time to change speed; a good tune hides most of it
        CHECK(peak < wanted * 1.2f);
        CHECK_NEAR(-degrees(drone.angularVelocity.z), wanted, wanted * 0.08f);
    }
}

TEST(fpv_stays_under_control_at_every_throttle) {
    for (const Airframe& airframe : {Airframe::fiveInch(), Airframe::whoop()}) {
        for (float throttle = 0; throttle <= 1.001f; throttle += 0.125f) {
            Drone drone(airframe);
            drone.place(Vec3{0, 2000, 0}, 0);
            Random random(5);
            StickInput sticks;
            sticks.throttle = throttle;
            for (int move = 0; move < 30; move++) {
                sticks.roll = random.range(-1, 1);
                sticks.pitch = random.range(-1, 1);
                sticks.yaw = random.range(-1, 1);
                fly(drone, sticks, FlightMode::Acro, 0.2f);
                CHECK(std::isfinite(drone.position.x) && std::isfinite(drone.rotation.vector.w));
                // 0.2 seconds after any change of mind, the drone turns about as fast as asked.
                float wanted = -radians(actualRate(sticks.roll, airframe.centerRate, airframe.maxRate, airframe.rateExpo));
                CHECK(std::abs(drone.angularVelocity.z - wanted) < radians(360));
            }
        }
    }
}

TEST(fpv_top_speed_and_battery_are_those_of_real_drones) {
    // Nose far down, full throttle: drag and the propellers running out of bite set the limit.
    Drone fast(Airframe::fiveInch());
    fast.place(Vec3{0, 500, 0}, 0);
    fast.rotation = axisAngle(Vec3{1, 0, 0}, radians(-72));
    StickInput sticks;
    sticks.throttle = 1;
    fly(fast, sticks, FlightMode::Acro, 8);
    CHECK(fast.speed() > 28 && fast.speed() < 48);          // 100 to 170 km/h
    CHECK(fast.cellVoltage() > 3.1f && fast.cellVoltage() < 3.7f);          // the pack sags under a hundred amperes
    CHECK(fast.battery.current > 60 && fast.battery.current < 150);

    Drone small(Airframe::whoop());
    small.place(Vec3{0, 500, 0}, 0);
    small.rotation = axisAngle(Vec3{1, 0, 0}, radians(-65));
    fly(small, sticks, FlightMode::Acro, 8);
    CHECK(small.speed() > 9 && small.speed() < 22);         // 30 to 80 km/h

    // Hovering, the packs last what their makers say: minutes, not hours.
    Drone hover = hovering(Airframe::whoop(), sticks);
    float minutes = hover.airframe.capacity * 0.8f / hover.battery.current * 60;
    CHECK(minutes > 3 && minutes < 7);
    StickInput big;
    Drone five = hovering(Airframe::fiveInch(), big);
    minutes = five.airframe.capacity * 0.8f / five.battery.current * 60;
    CHECK(minutes > 5 && minutes < 14);

    CHECK_NEAR(restingCellVoltage(1), 4.2f, 1e-4);
    CHECK(restingCellVoltage(0.5f) > 3.7f && restingCellVoltage(0.5f) < 3.9f);
    CHECK(restingCellVoltage(0.05f) < 3.5f);
    for (float charge = 0.05f; charge <= 1; charge += 0.05f) CHECK(restingCellVoltage(charge) > restingCellVoltage(charge - 0.05f));
}

TEST(fpv_the_air_matters_ground_wind_and_wake) {
    // Close to the ground the same throttle lifts more.
    CHECK(groundEffect(0.05f, 0.065f) > 1.1f);
    CHECK_NEAR(groundEffect(5.0f, 0.065f), 1, 0.001);
    CHECK(groundEffect(0.1f, 0.065f) > groundEffect(0.3f, 0.065f));

    // Sinking straight down at about the speed of the downwash is the worst place to be.
    CHECK_NEAR(propwashStrength(0, 0, 7), 0, 1e-4);
    CHECK(propwashStrength(7, 0, 7) > 0.9f);
    CHECK(propwashStrength(7, 12, 7) < 0.05f);              // moving sideways steps out of it
    CHECK(propwashStrength(25, 0, 7) < 0.05f);              // falling fast leaves it behind

    // A wind from the left carries a hovering drone to the right.
    StickInput sticks;
    Drone drone = hovering(Airframe::whoop(), sticks);
    drone.wind = Vec3{6, 0, 0};
    fly(drone, sticks, FlightMode::Angle, 3);
    CHECK(drone.position.x > 2);

    // Disarmed, it falls.
    Drone falling = hovering(Airframe::fiveInch(), sticks);
    falling.isArmed = false;
    fly(falling, sticks, FlightMode::Angle, 1);
    CHECK(falling.velocity.y < -7);
    CHECK(falling.motors[0] < 0.01f);
}

// ---------------------------------------------------------------- the race

TEST(fpv_a_gate_counts_only_when_flown_through_the_right_way) {
    Gate gate = Gate::at(Vec3{0, 2, -10}, 0, 4, 3);
    CHECK(gate.isPassed(Vec3{0, 2, -9}, Vec3{0, 2, -11}));
    CHECK(!gate.isPassed(Vec3{0, 2, -11}, Vec3{0, 2, -9}));         // backwards
    CHECK(!gate.isPassed(Vec3{3, 2, -9}, Vec3{3, 2, -11}));         // beside it
    CHECK(!gate.isPassed(Vec3{0, 4, -9}, Vec3{0, 4, -11}));         // over it
    CHECK(!gate.isPassed(Vec3{0, 2, -8}, Vec3{0, 2, -9}));          // not there yet
    Gate turned = Gate::at(Vec3{5, 2, 0}, 90, 4, 3);                // 90 degrees to the left: fly toward -X
    CHECK(turned.isPassed(Vec3{6, 2, 0.5f}, Vec3{4, 2, 0.5f}));
    CHECK(!turned.isPassed(Vec3{4, 2, 0}, Vec3{6, 2, 0}));
}

TEST(fpv_a_race_counts_gates_in_order_and_laps) {
    Race race(3, 2);
    CHECK(!race.pass(0));                               // not during the countdown
    race.update(3.1f);
    CHECK(race.phase == Race::Phase::Running);
    race.update(5);
    CHECK(!race.pass(1));                               // out of order
    CHECK(race.pass(0) && race.pass(1));
    CHECK(race.lastGate() == 1);
    race.update(5);
    CHECK(race.pass(2));
    CHECK(race.lap == 1 && race.nextGate == 0);
    CHECK_NEAR(race.lapTimes[0], 10, 1e-4);
    race.update(8);
    CHECK(race.pass(0) && race.pass(1) && race.pass(2));
    CHECK(race.phase == Race::Phase::Finished);
    CHECK_NEAR(race.lapTimes[1], 8, 1e-4);
    race.update(5);
    CHECK_NEAR(race.time, 18, 1e-4);                    // the clock stops at the finish
    CHECK(formatTime(83.5f) == "1:23.50");
    CHECK(formatTime(7.04f) == "0:07.04");
}

TEST(fpv_the_course_is_a_loop_of_gates_that_face_each_other) {
    const std::vector<Gate>& gates = courseGates();
    CHECK(gates.size() >= 6);
    for (size_t i = 0; i < gates.size(); i++) {
        const Gate& gate = gates[i];
        const Gate& next = gates[(i + 1) % gates.size()];
        Vec3 toNext = next.center - gate.center;
        CHECK(simd::dot(simd::normalize(toNext), gate.forward()) > 0.3f);       // the next gate is ahead, not behind
        CHECK(simd::dot(simd::normalize(toNext), next.forward()) > 0.3f);       // and is entered from its front
        CHECK(simd::length(toNext) > 15);
        CHECK(gate.center.y - gate.height * 0.5f > 0.4f);                       // room to pass above the grass
    }
    CHECK(distanceToCourse(Vec2{gates[0].center.x, gates[0].center.z}) < 0.01f);
    CHECK(distanceToCourse(Vec2{200, 200}) > 100);
}
