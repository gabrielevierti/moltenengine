// FPVGame.hpp
// FPV: fly a racing drone through a course of gates, seen from its own camera.
//
// The game is a few small parts:
//   FlightModel   how the drone moves (pure math)
//   Track         gates, laps and the clock (pure logic)
//   World         the field, the trees, the gates, the drone to look at
//   Controls      keyboard or controller -> stick positions
//   Sounds        motors, wind, beeps
//   FPVGame.cpp   the flight: collisions, crashes, the radio link, the camera
//   FPVHud.cpp    what is drawn over the picture: the OSD, the menu, the results
#pragma once

#include "Controls.hpp"
#include "FlightModel.hpp"
#include "Sounds.hpp"
#include "Track.hpp"
#include "World.hpp"

class FPVGame : public molten::Game {
public:
    enum class Screen { Menu, Flying, Finished };
    enum class Mode { Race, Free };
    enum class View { Onboard, Chase };

    void start(molten::Engine& engine) override;
    void fixedUpdate(molten::Engine& engine, float dt) override;
    void update(molten::Engine& engine, float dt) override;
    void draw(molten::Engine& engine, molten::Canvas& canvas) override;

    void begin(molten::Engine& engine, Mode mode);
    void chooseAirframe(molten::Engine& engine, int index);

    // ---- the state of the game. Public so that a test can look at it and set it up.
    Screen screen = Screen::Menu;
    Mode mode = Mode::Race;
    View view = View::Onboard;
    fpv::FlightMode flightMode = fpv::FlightMode::Angle;
    fpv::Drone drone;
    fpv::StickInput sticks;
    fpv::Race race{int(fpv::courseGates().size()), kLaps};
    fpv::Controls controls;
    int windSetting = 1;                    // 0 calm, 1 a breeze, 2 gusty
    float cameraTilt = 30;                  // degrees
    float link = 1;                         // the radio link: 1 perfect, 0 lost
    bool isCrashed = false;                 // tumbling or lying on the ground, motors off
    bool isWaitingToArm = false;            // back on its feet: the throttle must come down before the motors start
    float bestTime = 0;                     // 0: no time yet

    static constexpr int kLaps = 2;

private:
    void placeAtStart();
    float restHeight() const;
    void collide(molten::Engine& engine, molten::Vec3 previous, float dt, bool testSolids);
    void crash(molten::Engine& engine, float impact);
    void recover(molten::Engine& engine);
    void finish(molten::Engine& engine);
    void toggleFlightMode();
    molten::Vec3 windAt(double time, float height) const;
    void updateCamera(molten::Engine& engine, float dt);
    void updateSound(molten::Engine& engine, float dt);

    // FPVHud.cpp
    bool button(molten::Engine& engine, molten::Canvas& canvas, const std::string& label, molten::Vec2 position, molten::Vec2 size, bool primary = false);
    void drawMenu(molten::Engine& engine, molten::Canvas& canvas);
    void drawOSD(molten::Engine& engine, molten::Canvas& canvas);
    void drawGateMarker(molten::Engine& engine, molten::Canvas& canvas, const fpv::Gate& gate);
    void drawFinished(molten::Engine& engine, molten::Canvas& canvas);

    fpv::World world_;
    fpv::DroneModel model_;
    fpv::Sounds sounds_;
    int airframe_ = 0;
    molten::Entity camera_;
    molten::Vec3 chaseEye_{0, 3, 6};
    molten::SoundHandle motorSounds_[4] = {0, 0, 0, 0};
    molten::SoundHandle windSound_ = 0;
    molten::Random random_{99};

    bool isOnGround_ = true;
    bool isNewBest_ = false;
    bool lostSignal_ = false;               // the crash in progress began with the radio link dropping
    bool isBlocked_ = false;                // something stands between the pilot and the drone
    float crashTimer_ = 0;                  // seconds since the crash
    float restTimer_ = 0;                   // seconds the wreck has been lying still
    float linkLostTimer_ = 0;
    float flightTime_ = 0;
    float flashTimer_ = 0;                  // a gate was just passed
    float glitch_ = 0;                      // a burst of video noise, after a knock
    float alarmTimer_ = 0;
    int step_ = 0;
    std::string notice_;                    // a line shown for a few seconds
    float noticeTimer_ = 0;
};
