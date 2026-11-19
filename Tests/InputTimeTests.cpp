#include "Test.hpp"

#include "Core/Time.hpp"
#include "Input/Input.hpp"

using namespace molten;

TEST(key_press_and_release_last_one_frame) {
    Input input;
    input.handleKey(Key::Space, true);
    CHECK(input.isDown(Key::Space) && input.wasPressed(Key::Space));
    input.endFrame();
    CHECK(input.isDown(Key::Space) && !input.wasPressed(Key::Space));
    input.handleKey(Key::Space, true);                  // key repeat must not count as a new press
    CHECK(!input.wasPressed(Key::Space));
    input.handleKey(Key::Space, false);
    CHECK(!input.isDown(Key::Space) && input.wasReleased(Key::Space));
    input.endFrame();
    CHECK(!input.wasReleased(Key::Space));
}

TEST(a_tap_within_one_frame_is_not_lost) {
    Input input;
    input.handleKey(Key::E, true);
    input.handleKey(Key::E, false);
    CHECK(input.wasPressed(Key::E) && input.wasReleased(Key::E) && !input.isDown(Key::E));
}

TEST(actions_combine_keyboard_and_gamepad) {
    Input input;
    input.bindButton("jump", {Key::Space}, {GamepadButton::A});
    input.bindAxis("moveX", Key::A, Key::D, GamepadAxis::LeftX);
    CHECK(!input.isDown("jump"));
    CHECK_NEAR(input.axis("moveX"), 0, 1e-6);

    input.handleKey(Key::D, true);
    CHECK_NEAR(input.axis("moveX"), 1, 1e-6);
    input.handleKey(Key::A, true);
    CHECK_NEAR(input.axis("moveX"), 0, 1e-6);            // both directions cancel
    input.handleKey(Key::A, false);
    input.handleKey(Key::D, false);

    GamepadState pad;
    pad.connected = true;
    pad.buttons[size_t(GamepadButton::A)] = true;
    pad.axes[size_t(GamepadAxis::LeftX)] = -1.0f;
    input.handleGamepad(pad);
    CHECK(input.isDown("jump") && input.wasPressed("jump"));
    CHECK_NEAR(input.axis("moveX"), -1, 1e-6);

    pad.axes[size_t(GamepadAxis::LeftX)] = 0.1f;        // inside the dead zone
    input.handleGamepad(pad);
    CHECK_NEAR(input.axis("moveX"), 0, 1e-6);
    CHECK(!input.isDown("unbound action"));
}

TEST(mouse_delta_accumulates_until_end_of_frame) {
    Input input;
    input.handleMouseMove(Vec2{10, 10}, Vec2{3, 0});
    input.handleMouseMove(Vec2{12, 14}, Vec2{2, 4});
    CHECK_NEAR(input.mouseDelta().x, 5, 1e-6); CHECK_NEAR(input.mouseDelta().y, 4, 1e-6);
    CHECK_NEAR(input.mousePosition().y, 14, 1e-6);
    input.endFrame();
    CHECK_NEAR(input.mouseDelta().x, 0, 1e-6);
}

TEST(fixed_steps_follow_real_time) {
    Time time;
    time.fixedDelta = 1.0f / 60.0f;
    int steps = 0;
    for (int frame = 0; frame < 144; frame++) steps += time.beginFrame(1.0 / 144.0);    // one second at 144 Hz
    CHECK(steps >= 59 && steps <= 60);
    CHECK(time.alpha >= 0.0f && time.alpha < 1.0f);
    CHECK_NEAR(time.elapsed, 1.0, 1e-3);
}

TEST(a_long_hitch_does_not_cause_a_spiral) {
    Time time;
    int steps = time.beginFrame(3.0);                    // the app was suspended for three seconds
    CHECK(steps <= time.maxStepsPerFrame);
    CHECK(time.beginFrame(1.0 / 60.0) <= 2);             // and the backlog is gone
}

TEST(time_scale_pauses_the_simulation) {
    Time time;
    time.timeScale = 0;
    int steps = 0;
    for (int i = 0; i < 30; i++) steps += time.beginFrame(1.0 / 60.0);
    CHECK(steps == 0);
    CHECK_NEAR(time.elapsed, 0, 1e-9);
}
