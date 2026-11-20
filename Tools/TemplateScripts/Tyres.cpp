// Tyres.cpp
// The car's tyres: what they are made of, how worn they are, and the pit stop that
// changes them. Put it on the car, beside its VehicleBody.
//
//   Compounds   Soft grips most and wears fastest, Hard the other way round, Medium between.
//               Wet is for rain: slower than any of them on a dry road, and it wears there
//               fast, but in the wet the others slide where it holds. How wet the road is
//               comes from the shared value "wetness" (0 dry, 1 soaked), which Weather sets.
//   Wear        Every wheel wears as it rolls, and much faster as it slides. A worn tyre
//               grips less: a quarter less at the end.
//   Pit lane    Inside "Pit Lane" (an entity whose position and scale are a box on the
//               ground) the car is held to `pitSpeedLimit`. Stopped in "Pit Box", the car
//               is held for `pitStopSeconds` while the four tyres are changed: for the
//               compound chosen with the buttons "Tyre Soft", "Tyre Medium", "Tyre Hard" and
//               "Tyre Wet", or the same again if none was.
//
// Shared values: {tyreCompound} 0 to 3, {nextCompound}, {tyreWear} in percent, {inPitLane},
// {pitStop} the seconds left of a stop (0 when none), {speedLimit} meters per second for
// whoever drives (0: none).
#include "MoltenMetal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using namespace molten;

struct Tyres {
    int compound = 1;                   // 0 soft, 1 medium, 2 hard, 3 wet
    float wearRate = 1.0f;              // 1 as made; more for a shorter race
    float pitSpeedLimit = 60.0f;        // km/h in the pit lane
    float pitStopSeconds = 4.0f;

    std::array<float, 4> wear{0, 0, 0, 0};
    std::array<float, 4> baseGrip{0, 0, 0, 0};
    int nextCompound = -1;              // chosen for the next stop; -1: the same
    float stopLeft = 0;                 // seconds of the stop still to go
    bool hasLeftBox = true;             // one stop each time in: the car must leave the box before the next
    bool hasGrips = false;

    // How much each compound grips, dry and wet, and how fast it wears.
    static constexpr float kDry[4] = {1.06f, 1.0f, 0.95f, 0.84f};
    static constexpr float kWet[4] = {0.6f, 0.63f, 0.65f, 0.95f};
    static constexpr float kWear[4] = {1.7f, 1.0f, 0.6f, 1.1f};

    static bool isInBox(World& world, const char* name, Vec3 at, float margin) {
        const Transform* box = world.scene.get<Transform>(world.find(name));
        if (!box) return false;
        Vec3 middle = box->worldPosition();
        // Its scale is its size; it is laid along the world's axes.
        return std::abs(at.x - middle.x) <= box->scale.x * 0.5f + margin && std::abs(at.z - middle.z) <= box->scale.z * 0.5f + margin;
    }

    void start(World& world, Entity) {
        compound = std::clamp(int(world.preference("startCompound", float(compound))), 0, 3);
        world.setValue("tyreCompound", float(compound));
        world.setValue("nextCompound", float(compound));
        world.setValue("tyreWear", 0);
        world.setValue("pitStop", 0);
        world.setValue("speedLimit", 0);
    }

    void fixedUpdate(World& world, Entity self, float dt) {
        kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(self);
        const Transform* place = world.scene.get<Transform>(self);
        if (!car || !place || dt <= 0) return;
        float wetness = saturate(world.value("wetness"));

        // The wheels: what they were made to grip, then this compound, this wear, this road.
        int index = 0;
        float worn = 0;
        world.scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
            if (kit::vehicleOf(world.scene, entity) != self || index >= 4) return;
            int k = index++;
            if (!hasGrips) baseGrip[k] = wheel.grip;
            float sliding = 0;
            if (wheel.state.isOnGround) {
                float excess = std::max(std::abs(wheel.state.slipRatio) / 0.12f, std::abs(wheel.state.slipAngle) / 0.15f);
                sliding = saturate(excess - 0.6f) * saturate(wheel.state.load / 2500.0f);
            }
            float rolling = std::min(std::abs(wheel.state.spin) * wheel.radius, 60.0f);
            // A wet tyre on a dry road wears as if it were sliding.
            float rate = kWear[compound] * (compound == 3 ? 1.0f + 3.0f * (1.0f - wetness) : 1.0f);
            wear[size_t(k)] = std::min(1.0f, wear[size_t(k)] + dt * wearRate * rate * (rolling * 0.000022f + sliding * 0.0035f));
            float grip = kDry[compound] + (kWet[compound] - kDry[compound]) * wetness;
            wheel.grip = baseGrip[size_t(k)] * grip * (1.0f - 0.25f * std::pow(wear[size_t(k)], 1.3f));
            worn += wear[size_t(k)];
        });
        if (index > 0) hasGrips = true;
        world.setValue("tyreWear", std::round(worn / float(std::max(index, 1)) * 100.0f));

        // The pit lane, and the stop in the box.
        Vec3 at = place->worldPosition();
        bool inLane = isInBox(world, "Pit Lane", at, 0.0f);
        world.setValue("inPitLane", inLane ? 1.0f : 0.0f);
        world.setValue("speedLimit", inLane ? pitSpeedLimit / 3.6f : 0.0f);
        float speed = std::abs(car->speed());
        bool inBox = isInBox(world, "Pit Box", at, 0.4f);
        if (!inBox) hasLeftBox = true;
        if (stopLeft <= 0 && hasLeftBox && inLane && speed < 0.8f && inBox) {
            stopLeft = pitStopSeconds;
            hasLeftBox = false;
        }
        if (stopLeft > 0) {
            // Held where it is, brakes on, while the crew works.
            kit::Controls held;
            held.brake = 1;
            held.handbrake = 1;
            car->setControls(held);
            stopLeft -= dt;
            if (stopLeft <= 0) {
                stopLeft = 0;
                if (nextCompound >= 0) compound = nextCompound;
                wear = {0, 0, 0, 0};
                car->controlsAreGiven = false;              // the driver has it again
                world.setValue("tyreCompound", float(compound));
            }
        }
        world.setValue("pitStop", stopLeft > 0 ? std::ceil(stopLeft) : 0.0f);
    }

    void update(World& world, Entity, float) {
        // The buttons that choose the next tyres, shown in the pit lane.
        const char* names[4] = {"Tyre Soft", "Tyre Medium", "Tyre Hard", "Tyre Wet"};
        for (int k = 0; k < 4; k++) {
            if (const kit::UIButton* button = world.scene.get<kit::UIButton>(world.find(names[k])); button && button->wasClicked) nextCompound = k;
            if (world.input.wasPressed(Key(int(Key::Num1) + k)) && world.value("inPitLane") > 0) nextCompound = k;
        }
        world.setValue("nextCompound", float(nextCompound >= 0 ? nextCompound : compound));
        // Its panel shows in the pit lane only.
        if (Transform* panel = world.scene.get<Transform>(world.find("Pit Panel"))) {
            bool shown = world.value("inPitLane") > 0;
            if (panel->isActive != shown) {
                panel->isActive = shown;
                world.scene.updateTransforms();
            }
        }
        // The chosen one stands out.
        for (int k = 0; k < 4; k++) {
            if (kit::UIButton* button = world.scene.get<kit::UIButton>(world.find(names[k]))) {
                bool chosen = k == (nextCompound >= 0 ? nextCompound : compound);
                button->color.w = chosen ? 0.95f : 0.35f;
            }
        }
    }
};

MOLTEN_COMPONENT(Tyres, MOLTEN_FIELD(compound).range(0, 3).help("0 soft, 1 medium, 2 hard, 3 wet"), MOLTEN_FIELD(wearRate).range(0, 20),
                 MOLTEN_FIELD(pitSpeedLimit).range(10, 200).help("km/h, inside the entity called Pit Lane"),
                 MOLTEN_FIELD(pitStopSeconds).range(0, 30))
