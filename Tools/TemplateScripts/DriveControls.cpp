// DriveControls.cpp
// The driver: reads the keys and tells the car what is wanted. This is the game's own
// script; the car itself is made of the engine's parts (VehicleBody, Wheel, Suspension,
// DeformableTire, Motor), which read `VehicleBody::controls`.
//
//   W S       accelerate and brake; S at a standstill is reverse
//   A D       steer
//   Space     handbrake
//   1 to 7    another engine: four, five, six, V8, V12, diesel, electric
//   B         anti-lock brakes on and off, to feel what they do
//   T         the touch controls on the screen, on and off (they show by themselves on a phone)
//
// On a phone there are no keys: the stick on the left steers and the buttons on the right
// hold the actions "gas", "brake" and "jump" (the handbrake), which are read here too.
#include "MoltenMetal.hpp"
#include "Motor.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

struct DriveControls {
    bool switchesEngines = true;        // keys 1 to 7
    bool antiLockBrakes = true;         // ABS: B turns it on and off
    bool showsTouchControls = false;    // the stick and the pedals on the screen: T, or a touch
    std::string skidSound = "Audio/skid.wav";
    std::string windSound = "Audio/wind.wav";

    uint32_t skidVoice = 0, windVoice = 0;
    bool hasSounds = false;

    static float toward(float value, float target, float step) {
        return value < target ? std::min(value + step, target) : std::max(value - step, target);
    }

    /// The Motor of the car this is on: on the same object or one below it.
    static Motor* motorOf(World& world, Entity self) {
        Motor* found = nullptr;
        world.scene.each<Motor, Transform>([&](Entity entity, Motor& motor, Transform&) {
            if (!found && kit::vehicleOf(world.scene, entity) == self) found = &motor;
        });
        return found;
    }

    void fixedUpdate(World& world, Entity self, float dt) {
        kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(self);
        if (!car || car->controlsAreGiven) return;
        Motor* motor = motorOf(world, self);
        kit::Controls& controls = car->controls;
        const Input& input = world.input;

        float ahead = -input.axis("moveZ");                 // W is +1
        // The pedals on the screen: buttons that hold "gas" and "brake".
        if (input.isDown("gas")) ahead = 1.0f;
        if (input.isDown("brake")) ahead = -1.0f;
        float along = car->speed();
        int gear = motor ? motor->gear : 1;
        // Pulling back at a standstill is reverse; pushing forward again, first gear.
        if (gear >= 1 && along < 0.6f && ahead < -0.1f) gear = -1;
        else if (gear == -1 && along > -0.6f && ahead > 0.1f) gear = 1;
        float wantThrottle = gear >= 1 ? std::max(ahead, 0.0f) : std::max(-ahead, 0.0f);
        float wantBrake = gear >= 1 ? std::max(-ahead, 0.0f) : std::max(ahead, 0.0f);
        // Feet and hands move at their own speed, not at the speed keys change.
        controls.throttle = toward(controls.throttle, wantThrottle, dt * 6);
        controls.brake = toward(controls.brake, wantBrake, dt * 8);
        float wheel = input.axis("moveX");
        controls.steer = toward(controls.steer, wheel, dt * (std::abs(wheel) > 0.01f ? 3.2f : 5.0f));
        controls.handbrake = input.isDown("jump") ? 1.0f : 0.0f;

        // Anti-lock brakes are the wheels' own (Wheel::antiLock): this is the switch on the dashboard.
        if (input.wasPressed(Key::B)) antiLockBrakes = !antiLockBrakes;
        world.scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
            if (kit::vehicleOf(world.scene, entity) == self) wheel.antiLock = antiLockBrakes;
        });
        world.setValue("abs", antiLockBrakes ? 1.0f : 0.0f);
        world.setValue("speed", std::round(std::abs(along) * 3.6f));
        if (!motor) return;
        motor->gear = gear;
        if (switchesEngines) {
            const char* const engines[] = {"i4", "i5", "i6", "v8", "v12", "diesel", "electric"};
            for (int i = 0; i < 7; i++) {
                if (input.wasPressed(Key(int(Key::Num1) + i))) {
                    motor->engine = engines[i];
                    if (motor->gear > 1) motor->gear = 1;   // the new gearbox starts from the bottom
                }
            }
        }
        world.setValue("rpm", std::round(motor->rpm / 10.0f) * 10.0f);
        world.setValue("gear", float(motor->gear));
        // For the read-outs: the gear as a place in "R|1|2|3...", the engine as a place in its
        // list, and how far up its revs it is, from 0 to 1.
        world.setValue("gearIndex", motor->gear < 0 ? 0.0f : float(motor->gear));
        const EngineKind& kind = engineKind(motor->engine);
        world.setValue("revs", saturate((motor->rpm - kind.idle * 0.6f) / std::max(kind.redline - kind.idle * 0.6f, 1.0f)));
        const char* const names[] = {"i4", "i5", "i6", "v8", "v12", "diesel", "electric"};
        for (int i = 0; i < 7; i++) {
            if (motor->engine == names[i]) world.setValue("engine", float(i));
        }
    }

    void update(World& world, Entity self, float) {
        const kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(self);
        if (!car) return;
        // The controls on the screen show by themselves once the screen is touched; T shows
        // and hides them where there is a keyboard, to try them with the mouse.
        if (world.input.wasPressed(Key::T)) showsTouchControls = !showsTouchControls;
        bool onScreen = showsTouchControls || world.input.hasTouched();
        world.scene.each<kit::TouchStick, Transform>([&](Entity, kit::TouchStick& stick, Transform&) { stick.onlyOnTouchScreens = !onScreen; });
        world.scene.each<kit::TouchButton, Transform>([&](Entity, kit::TouchButton& button, Transform&) { button.onlyOnTouchScreens = !onScreen; });
        // And what the keys do is of no use to someone holding a phone.
        // On a touch screen the keys' help and the engine's name go: the thumbs need the room.
        for (const char* name : {"Help", "Help 2", "Engine"}) {
            if (kit::HudText* help = world.scene.get<kit::HudText>(world.find(name))) help->color.w = world.input.hasTouched() ? 0.0f : 0.55f;
        }
        if (!hasSounds) {
            if (!skidSound.empty()) skidVoice = world.startSound(skidSound, 0.0f, 1.0f, true);
            if (!windSound.empty()) windVoice = world.startSound(windSound, 0.0f, 1.0f, true);
            hasSounds = true;
        }
        // Tires howl when they are asked for more than they can give.
        float slide = 0, fastest = 0;
        world.scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
            if (kit::vehicleOf(world.scene, entity) != self) return;
            fastest = std::max(fastest, std::abs(wheel.state.spin) * wheel.radius);
            if (!wheel.state.isOnGround) return;
            float excess = std::max(std::abs(wheel.state.slipRatio) / 0.1f, std::abs(wheel.state.slipAngle) / 0.14f) - 1.1f;
            slide = std::max(slide, saturate(excess * 0.6f) * saturate(wheel.state.load / 2500.0f));
        });
        float air = car->airspeed();
        float moving = saturate((air + fastest) / 6.0f);
        world.changeSound(skidVoice, slide * moving * 0.7f, 0.85f + 0.3f * moving);
        world.changeSound(windVoice, saturate(air * air / 2500.0f) * 0.6f, 0.7f + air / 80.0f);
    }
};

MOLTEN_COMPONENT(DriveControls, MOLTEN_FIELD(switchesEngines).help("Keys 1 to 7"), MOLTEN_FIELD(antiLockBrakes).help("ABS: eases the brakes when a wheel locks. B turns it on and off."),
                 MOLTEN_FIELD(showsTouchControls).help("The stick and the pedals on the screen. T turns them on and off; a touch shows them."), MOLTEN_FIELD(skidSound), MOLTEN_FIELD(windSound))
