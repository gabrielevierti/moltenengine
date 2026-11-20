// The car and the aeroplane, driven and flown without a screen. What is checked is what
// someone at the controls would notice: it sits on its springs, it gets up to speed, it
// stops, it turns, soft tires flatten, engines differ, it takes off, it stalls.
#include "Test.hpp"

#include "Kit/Kit.hpp"
#include "Kit/Vehicles.hpp"

#include <cmath>

using namespace molten;

namespace {

/// A car made of parts, with what the tests look at close to hand.
struct TestCar {
    kit::VehicleBody* vehicle = nullptr;
    kit::Motor* motor = nullptr;
    kit::WheelState* wheels[4] = {};
    kit::Body6& body() { return vehicle->body; }
    float speed() const { return vehicle->speed(); }
    void setControls(float throttle, float brake, float steer) {
        kit::Controls controls;
        controls.throttle = throttle;
        controls.brake = brake;
        controls.steer = steer;
        vehicle->setControls(controls);
    }
};

struct Aeroplane {
    kit::VehicleBody* vehicle = nullptr;
    kit::FlightControls* flight = nullptr;
    kit::Body6& body() { return vehicle->body; }
    float airspeed() const { return vehicle->airspeed(); }
    void setControls(float pitch, float roll, float yaw, float throttle) {
        kit::Controls controls;
        controls.pitch = pitch;
        controls.roll = roll;
        controls.yaw = yaw;
        controls.throttle = throttle;
        vehicle->setControls(controls);
    }
};

struct Ground {
    World world;
    Ground() {
        kit::registerKit();
        Entity ground = world.scene.create("Ground");
        Transform transform;
        transform.position = Vec3{0, -0.5f, 0};
        world.scene.add(ground, transform);
        Collider collider;
        collider.shape = ColliderShape::Box;
        collider.halfExtents = Vec3{3000, 0.5f, 3000};
        world.scene.add(ground, collider);
    }
    void seconds(float time) {
        for (int i = 0; i < int(time * 60.0f + 0.5f); i++) world.advance(1.0 / 60.0);
    }
    TestCar car(const char* engine = "i4", const char* drive = "rear", float pressure = 2.2f);
    Aeroplane plane();
};

TestCar Ground::car(const char* engine, const char* drive, float pressure) {
    Entity entity = kit::makeCar(world, Vec3{0, 0.5f, 0}, engine, drive);
    TestCar car;
    const char* const names[4] = {"Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR"};
    for (int i = 0; i < 4; i++) {
        Entity wheel = world.find(names[i]);
        world.scene.get<kit::DeformableTire>(wheel)->pressure = pressure;
        car.wheels[i] = &world.scene.get<kit::Wheel>(wheel)->state;
    }
    car.vehicle = world.scene.get<kit::VehicleBody>(entity);
    car.motor = world.scene.get<kit::Motor>(entity);
    car.motor->soundFolder.clear();
    world.scene.get<kit::DriveControls>(entity)->skidSound.clear();
    world.scene.get<kit::DriveControls>(entity)->windSound.clear();
    return car;
}

Aeroplane Ground::plane() {
    Entity entity = kit::makeAirplane(world, Vec3{0, 1.3f, 0});
    Aeroplane plane;
    plane.vehicle = world.scene.get<kit::VehicleBody>(entity);
    plane.flight = world.scene.get<kit::FlightControls>(entity);
    plane.flight->windSound.clear();
    plane.flight->stallSound.clear();
    world.scene.get<kit::Propeller>(world.find("Propeller"))->sound.clear();
    return plane;
}

/// Seconds from standstill to 100 km/h with the pedal down; 99 if it does not get there in 40.
float zeroToHundred(const char* engine, const char* drive = "rear") {
    Ground ground;
    TestCar car = ground.car(engine, drive);
    car.setControls(0, 0, 0);
    ground.seconds(1.5f);
    for (int frame = 0; frame < 60 * 40; frame++) {
        car.setControls(1, 0, 0);
        ground.world.advance(1.0 / 60.0);
        if (car.speed() * 3.6f >= 100) return float(frame) / 60.0f;
    }
    return 99;
}

}  // namespace

TEST(car_rests_on_its_springs_and_tires) {
    Ground ground;
    TestCar car = ground.car();
    car.setControls(0, 0, 0);
    ground.seconds(3);
    CHECK(simd::length(car.body().velocity) < 0.05f);                     // it has come to rest
    CHECK(std::abs(car.body().up().y - 1) < 1e-3f);                       // level
    float weight = car.vehicle->mass * 9.81f, carried = 0;
    for (const kit::WheelState* each : car.wheels) {
        const kit::WheelState& wheel = *each;
        CHECK(wheel.isOnGround);
        carried += wheel.load;
        // Each spring is pushed in by what its share of the weight asks for.
        CHECK_NEAR(wheel.compression, weight / 4 / 36000.0f, 0.01);
        CHECK(wheel.deflection > 0.008f && wheel.deflection < 0.03f);   // and each tire flattened a centimeter or two
    }
    CHECK_NEAR(carried, weight, weight * 0.02);

    // Softer tires flatten more.
    Ground other;
    TestCar soft = other.car("i4", "rear", 1.0f);
    soft.setControls(0, 0, 0);
    other.seconds(3);
    CHECK(soft.wheels[0]->deflection > car.wheels[0]->deflection * 1.8f);
    CHECK(soft.body().position.y < car.body().position.y - 0.008f);         // so the whole car sits lower
}

TEST(car_accelerates_shifts_and_brakes) {
    Ground ground;
    TestCar car = ground.car("i4");
    car.setControls(0, 0, 0);
    ground.seconds(1);
    int highestGear = 1;
    float highestRpm = 0;
    for (int frame = 0; frame < 60 * 14; frame++) {
        car.setControls(1, 0, 0);
        ground.world.advance(1.0 / 60.0);
        highestGear = std::max(highestGear, car.motor->gear);
        highestRpm = std::max(highestRpm, car.motor->rpm);
    }
    float fast = car.speed() * 3.6f;
    CHECK(fast > 110 && fast < 230);
    CHECK(highestGear >= 3);
    CHECK(highestRpm > 5500 && highestRpm <= 6900);
    CHECK(std::abs(car.body().position.x) < 1.0f);                        // straight
    CHECK(ground.world.value("speed") > 110);

    // Full braking: stopped in a believable distance, without spinning round.
    Vec3 from = car.body().position;
    float speed = car.speed();
    int frames = 0;
    while (car.speed() > 0.5f && frames < 60 * 12) {
        car.setControls(0, 1, 0);
        ground.world.advance(1.0 / 60.0);
        frames++;
    }
    float distance = simd::length(car.body().position - from);
    float ideal = speed * speed / (2 * 9.81f * 1.05f);                  // what perfect grip would allow
    CHECK(frames < 60 * 12);
    CHECK(distance > ideal * 0.95f && distance < ideal * 1.9f);
    CHECK(car.body().forward().z < -0.97f);

    // Then reverse, from the driver's seat: S held at a standstill.
    car.vehicle->controlsAreGiven = false;
    ground.world.input.handleKey(Key::S, true);
    ground.seconds(3);
    CHECK(car.motor->gear == -1);
    CHECK(car.speed() < -2);
}

TEST(car_turns_and_leans) {
    Ground ground;
    TestCar car = ground.car("i4");
    car.setControls(0, 0, 0);
    ground.seconds(1);
    for (int frame = 0; frame < 60 * 5; frame++) {
        car.setControls(0.6f, 0, 0);
        ground.world.advance(1.0 / 60.0);
    }
    float headingBefore = std::atan2(car.body().forward().x, -car.body().forward().z);
    float mostLean = 0;
    for (int frame = 0; frame < 60 * 2; frame++) {
        car.setControls(0.4f, 0, 0.35f);                                // to the right
        ground.world.advance(1.0 / 60.0);
        // In a right turn the body leans left: its right side comes up.
        mostLean = std::max(mostLean, car.body().right().y);
    }
    float heading = std::atan2(car.body().forward().x, -car.body().forward().z);
    CHECK(heading - headingBefore > 0.4f);                              // it went right
    CHECK(mostLean > 0.01f && mostLean < 0.2f);
    CHECK(car.wheels[0]->load > car.wheels[1]->load * 1.15f);             // the outside front wheel carries more
    CHECK(car.body().up().y > 0.9f);                                      // and it stayed on its wheels
}

TEST(engines_are_different_machines) {
    CHECK(kit::engineTorque("v8", 3000) > kit::engineTorque("i4", 3000) * 2);
    CHECK(kit::engineTorque("diesel", 1800) > kit::engineTorque("diesel", 4500) * 1.5f);    // all of it early
    CHECK(kit::engineTorque("electric", 200) == kit::engineTorque("electric", 4000));       // flat from nothing
    CHECK(kit::engineTorque("i4", 7500) == 0);                                              // the limiter
    float four = zeroToHundred("i4"), eight = zeroToHundred("v8"), electric = zeroToHundred("electric", "all");
    CHECK(four > 6 && four < 16);
    CHECK(eight < four - 1.5f);
    CHECK(electric < four - 2);
    CHECK(eight > 2.5f && electric > 2.0f);                             // grip still limits them
}

TEST(car_lands_on_its_wheels_and_hits_walls) {
    Ground ground;
    // A wall across the road, 60 meters ahead.
    Entity wall = ground.world.scene.create("Wall");
    Transform transform;
    transform.position = Vec3{0, 1, -60};
    ground.world.scene.add(wall, transform);
    Collider collider;
    collider.halfExtents = Vec3{10, 1, 0.5f};
    ground.world.scene.add(wall, collider);

    TestCar car = ground.car("v8");
    car.setControls(0, 0, 0);
    ground.world.advance(1.0 / 60.0);
    car.body().position.y = 2.5f;                                          // dropped from two meters
    float deepest = 0;
    for (int frame = 0; frame < 120; frame++) {
        ground.world.advance(1.0 / 60.0);
        deepest = std::max(deepest, car.wheels[0]->compression);
    }
    CHECK(deepest > car.vehicle->mass * 9.81f / 4 / 36000.0f + 0.03f);      // the springs took the landing
    CHECK(car.body().position.y < 1.0f && car.body().position.y > 0.4f);
    for (int frame = 0; frame < 60 * 8; frame++) {
        car.setControls(1, 0, 0);
        ground.world.advance(1.0 / 60.0);
    }
    CHECK(car.body().position.z > -60.0f);                                 // it did not go through
    CHECK(car.body().position.z < -50.0f);                                 // but it got there
    CHECK(std::isfinite(car.body().position.x) && std::isfinite(car.body().orientation.vector.w));
}

TEST(aeroplane_takes_off_climbs_and_stalls) {
    Ground ground;
    Aeroplane plane = ground.plane();

    // Sitting on its wheels with the engine idling, it stays where it is.
    plane.setControls(0, 0, 0, 0);
    ground.seconds(3);
    CHECK(plane.vehicle->isOnGround);
    CHECK(simd::length(plane.body().velocity) < 0.3f);
    CHECK(plane.body().up().y > 0.98f);

    // Full throttle, and a little back pressure once it is fast enough.
    float liftOffSpeed = 0, runLength = 0;
    for (int frame = 0; frame < 60 * 70 && plane.body().position.y < 300; frame++) {
        float kmh = plane.airspeed() * 3.6f;
        plane.setControls(kmh > 105 ? 0.6f : 0.0f, 0, 0, 1);
        ground.world.advance(1.0 / 60.0);
        if (liftOffSpeed == 0 && !plane.vehicle->isOnGround && plane.body().position.y > 2.0f) {
            liftOffSpeed = kmh;
            runLength = -plane.body().position.z;
        }
    }
    CHECK(liftOffSpeed > 80 && liftOffSpeed < 160);
    CHECK(runLength > 120 && runLength < 900);
    CHECK(plane.body().position.y >= 300);                                 // it climbed away
    CHECK(std::abs(plane.body().position.x) < 150);                        // more or less straight
    CHECK(std::abs(plane.body().right().y) < 0.2f);                        // wings more or less level

    // Hands off, with cruise power: it flies on by itself, neither diving nor rearing up.
    float lowest = 1e9f, highest = 0;
    for (int frame = 0; frame < 60 * 60; frame++) {
        plane.setControls(0, 0, 0, 0.7f);
        ground.world.advance(1.0 / 60.0);
        if (frame > 60 * 30) {
            lowest = std::min(lowest, plane.body().position.y);
            highest = std::max(highest, plane.body().position.y);
        }
    }
    float cruise = plane.airspeed() * 3.6f;
    CHECK(cruise > 110 && cruise < 290);
    CHECK(lowest > 150);                                                 // it did not fly itself into the ground
    CHECK(highest - lowest < 120);                                       // its ups and downs die away
    CHECK(std::abs(plane.body().forward().y) < 0.35f);
    CHECK(!plane.flight->isStalled);

    // Rolling: stick to the right, the right wing goes down.
    for (int frame = 0; frame < 45; frame++) {
        plane.setControls(0, 0.6f, 0, 0.65f);
        ground.world.advance(1.0 / 60.0);
    }
    CHECK(plane.body().right().y < -0.15f);
    for (int frame = 0; frame < 60 * 6; frame++) {                       // level it again
        plane.setControls(0, std::clamp(plane.body().right().y * 3.0f, -1.0f, 1.0f), 0, 0.65f);
        ground.world.advance(1.0 / 60.0);
    }

    // Engine to idle and the stick held back: it slows, the wing lets go, and it sinks.
    bool stalled = false;
    float slowest = 999;
    for (int frame = 0; frame < 60 * 30 && !stalled; frame++) {
        // Back slowly, as one does to find the stall, not snatched.
        plane.setControls(std::min(1.0f, float(frame) / (60.0f * 12.0f)), std::clamp(plane.body().right().y * 3.0f, -1.0f, 1.0f), 0, 0);
        ground.world.advance(1.0 / 60.0);
        slowest = std::min(slowest, plane.airspeed() * 3.6f);
        stalled = plane.flight->isStalled;
    }
    CHECK(stalled);
    CHECK(slowest > 50 && slowest < 115);
    CHECK(ground.world.value("stall") == 1);
    CHECK(std::isfinite(plane.body().position.y));
}

TEST(a_car_hits_with_its_own_colliders_and_its_wheels_see_through_them) {
    // A box over the whole car, from its middle up, around its wheels' mounts: the wheels'
    // rays start inside it and still find the road; the box stops it at the wall.
    Ground ground;
    Entity wall = ground.world.scene.create("Wall");
    Transform transform;
    transform.position = Vec3{0, 1, -40};
    ground.world.scene.add(wall, transform);
    Collider collider;
    collider.halfExtents = Vec3{10, 1, 0.5f};
    ground.world.scene.add(wall, collider);

    TestCar car = ground.car("v8");
    Entity root = ground.world.find("Car");
    if (!root.isValid()) root = kit::vehicleOf(ground.world.scene, ground.world.find("Wheel FL"));
    Entity shell = ground.world.scene.create("Shell");
    Transform under;
    under.parent = root;
    ground.world.scene.add(shell, under);
    Collider box;
    box.halfExtents = Vec3{0.95f, 0.45f, 2.4f};             // longer than the car: it hits first
    box.offset = Vec3{0, 0.4f, 0};
    ground.world.scene.add(shell, box);
    ground.world.scene.updateTransforms();
    car.vehicle->hitsWithItsBox = false;

    car.setControls(0, 0, 0);
    for (int frame = 0; frame < 120; frame++) ground.world.advance(1.0 / 60.0);
    CHECK(car.wheels[0]->isOnGround && car.wheels[3]->isOnGround);  // through its own box to the road
    CHECK(car.body().position.y > 0.3f && car.body().position.y < 1.0f);
    for (int frame = 0; frame < 60 * 6; frame++) {
        car.setControls(1, 0, 0);
        ground.world.advance(1.0 / 60.0);
    }
    // Its box's front is 2.4 m ahead of its middle: stopped with that at the wall, not the car's own nose.
    CHECK(car.body().position.z > -40.0f + 0.5f + 2.4f - 0.3f);
    CHECK(car.body().position.z < -30.0f);
    CHECK(std::isfinite(car.body().position.x));
}
