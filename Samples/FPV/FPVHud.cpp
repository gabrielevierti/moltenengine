// What is drawn over the picture: the menu, the on-screen display while flying, the results.
#include "FPVGame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace molten;
using namespace fpv;

namespace {

const Vec4 kOrange = rgb(0xfe8b3c);
const Vec4 kGreen = rgb(0x7dff9a);
const Vec4 kRed = rgb(0xff5a4d);
const Vec4 kWhite{1, 1, 1, 0.96f};

std::string format(const char* pattern, double value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), pattern, value);
    return buffer;
}

/// Text that stays readable over sky and over grass: white on its own dark shadow.
void osd(Canvas& canvas, const std::string& text, Vec2 position, float size, TextAlign align = TextAlign::Left, Vec4 color = kWhite) {
    canvas.text(text, position + Vec2{1, 1.5f}, size, Vec4{0, 0, 0, 0.65f * color.w}, align);
    canvas.text(text, position, size, color, align);
}

/// Where a point of the undistorted picture ends up on screen, after the lens has bent
/// it. `point` runs from -1 to 1 in both directions. The shader knows the other way
/// round (from the screen to the picture), so this looks for the answer in a few steps.
Vec2 throughLens(Vec2 point, float aspect, float distortion) {
    Vec2 guess = point;
    Vec2 weight = Vec2{aspect, 1.0f} / std::sqrt(aspect * aspect + 1.0f);
    for (int i = 0; i < 6; i++) {
        Vec2 q = guess * weight;
        float bend = (1.0f + distortion * simd::dot(q, q)) / (1.0f + distortion);
        guess = point / bend;
    }
    return guess;
}

}  // namespace

/// A button for the mouse. Returns true in the frame it is pressed.
bool FPVGame::button(Engine& engine, Canvas& canvas, const std::string& label, Vec2 position, Vec2 size, bool primary) {
    Vec2 mouse = engine.input.mousePosition();
    bool inside = mouse.x >= position.x && mouse.y >= position.y && mouse.x < position.x + size.x && mouse.y < position.y + size.y;
    float radius = std::min(size.y * 0.28f, 10.0f);
    canvas.roundedRect(position, size, radius, primary ? Vec4{kOrange.x, kOrange.y, kOrange.z, 0.95f} : Vec4{0.05f, 0.07f, 0.1f, inside ? 0.8f : 0.62f});
    if (!primary) canvas.roundedRectOutline(position, size, radius, 1, Vec4{1, 1, 1, 0.22f});
    float textSize = std::min(size.y * 0.4f, 18.0f);
    canvas.text(label, position + Vec2{size.x * 0.5f, (size.y - textSize) * 0.5f - 1}, textSize, primary ? rgb(0x1b1d22) : kWhite, TextAlign::Center);
    return inside && engine.input.wasPressed(MouseButton::Left);
}

void FPVGame::draw(Engine& engine, Canvas& canvas) {
    switch (screen) {
        case Screen::Menu: drawMenu(engine, canvas); break;
        case Screen::Flying: drawOSD(engine, canvas); break;
        case Screen::Finished: drawFinished(engine, canvas); break;
    }
}

// ---------------------------------------------------------------- the menu

void FPVGame::drawMenu(Engine& engine, Canvas& canvas) {
    Vec2 size = canvas.size();
    canvas.rect(Vec2{0, 0}, size, Vec4{0.02f, 0.03f, 0.06f, 0.5f});

    // Two columns: what to fly on the left, how on the right.
    float gap = 10, rowHeight = std::clamp((size.y - 150) / 5.0f - gap, 34.0f, 46.0f);
    float columnWidth = std::min(320.0f, (size.x - 72) * 0.5f);
    float left = size.x * 0.5f - columnWidth - 12, right = size.x * 0.5f + 12;
    float top = std::max(size.y * 0.5f - (rowHeight + gap) * 2.5f + 26, 96.0f);

    canvas.text("FPV", Vec2{left, top - 82}, 46, kOrange);
    std::string best = bestTime > 0 ? "Best race  " + formatTime(bestTime) : "No race time yet";
    canvas.text(best, Vec2{left + 2, top - 30}, 14, Vec4{1, 1, 1, 0.75f});

    Vec2 wide{columnWidth, rowHeight * 2 + gap};
    if (button(engine, canvas, "Race, " + std::to_string(kLaps) + " laps", Vec2{left, top}, wide, true)) begin(engine, Mode::Race);
    if (button(engine, canvas, "Free flight", Vec2{left, top + wide.y + gap}, Vec2{columnWidth, rowHeight})) begin(engine, Mode::Free);
    canvas.text(engine.input.isGamepadConnected() ? "Controller connected.\nLeft stick: throttle and yaw. Right stick: pitch and roll."
                                                  : "W S throttle, A D yaw, arrows pitch and roll.\nA controller or an RC radio works too.",
                Vec2{left + 2, top + wide.y + rowHeight + gap * 2 + 2}, 12.5f, Vec4{1, 1, 1, 0.7f});

    Vec2 row{columnWidth, rowHeight};
    float y = top;
    if (button(engine, canvas, std::string("Drone: ") + drone.airframe.name, Vec2{right, y}, row)) chooseAirframe(engine, 1 - airframe_);
    y += rowHeight + gap;
    if (button(engine, canvas, flightMode == FlightMode::Angle ? "Mode: Angle (levels itself)" : "Mode: Acro (rates)", Vec2{right, y}, row)) {
        toggleFlightMode();
    }
    y += rowHeight + gap;
    static const char* const winds[3] = {"Wind: calm", "Wind: a breeze", "Wind: gusty"};
    if (button(engine, canvas, winds[windSetting], Vec2{right, y}, row)) windSetting = (windSetting + 1) % 3;
    y += rowHeight + gap;
    if (button(engine, canvas, format("Camera tilt: %.0f degrees", cameraTilt), Vec2{right, y}, row)) {
        cameraTilt = cameraTilt >= 45 ? 10 : cameraTilt + 5;
    }
    y += rowHeight + gap;
    if (button(engine, canvas, controls.throttleUsesFullStick ? "Controller throttle: whole stick" : "Controller throttle: upper half",
               Vec2{right, y}, row)) {
        controls.throttleUsesFullStick = !controls.throttleUsesFullStick;
    }
}

// ---------------------------------------------------------------- while flying

/// The on-screen display: what a flight controller writes over the video for the pilot.
void FPVGame::drawOSD(Engine& engine, Canvas& canvas) {
    Vec2 size = canvas.size();
    Vec2 center = size * 0.5f;
    float top = 10, left = 14, right = size.x - 14;
    bool blink = std::fmod(engine.time.elapsed, 0.6) < 0.36;

    // The strip of buttons at the top.
    if (button(engine, canvas, "Menu", Vec2{left, top}, Vec2{70, 34})) screen = Screen::Menu;
    if (button(engine, canvas, "Reset", Vec2{left + 78, top}, Vec2{70, 34})) begin(engine, mode);
    if (button(engine, canvas, view == View::Onboard ? "Onboard" : "Chase", Vec2{right - 164, top}, Vec2{82, 34})) {
        view = view == View::Onboard ? View::Chase : View::Onboard;
    }
    if (button(engine, canvas, flightMode == FlightMode::Angle ? "Angle" : "Acro", Vec2{right - 74, top}, Vec2{74, 34})) toggleFlightMode();

    // ---- battery, top left: volts per cell (what pilots watch), the pack, and what was used
    float cell = drone.cellVoltage();
    Vec4 batteryColor = cell < 3.3f ? kRed : cell < 3.5f ? kOrange : kWhite;
    Vec2 at{left + 2, top + 46};
    canvas.roundedRectOutline(at + Vec2{0, 2}, Vec2{26, 13}, 2.5f, 1.5f, batteryColor);
    canvas.rect(at + Vec2{26, 6}, Vec2{2.5f, 5}, batteryColor);
    canvas.rect(at + Vec2{3, 5}, Vec2{20 * drone.charge(), 7}, batteryColor);
    osd(canvas, format("%.2f V", cell), at + Vec2{36, 0}, 15, TextAlign::Left, batteryColor);
    osd(canvas, format("%.1f V", drone.battery.voltage) + "   " + format("%.0f MAH", drone.battery.used * 1000), at + Vec2{0, 21}, 12);
    osd(canvas, format("%.1f A", drone.battery.current), at + Vec2{0, 38}, 12);

    // ---- radio link, top right
    Vec4 linkColor = link < 0.25f ? kRed : link < 0.5f ? kOrange : kWhite;
    for (int bar = 0; bar < 4; bar++) {
        float barHeight = 4.0f + float(bar) * 3.0f;
        canvas.rect(Vec2{right - 86 + float(bar) * 5, top + 61 - barHeight}, Vec2{3.5f, barHeight},
                    link * 4 > float(bar) + 0.2f ? linkColor : Vec4{1, 1, 1, 0.25f});
    }
    osd(canvas, format("LQ %.0f", link * 100), Vec2{right - 2, top + 46}, 15, TextAlign::Right, linkColor);
    Vec3 home = world_.pilot - drone.position;
    osd(canvas, format("HOME %.0f M", std::sqrt(home.x * home.x + home.z * home.z)), Vec2{right - 2, top + 67}, 12, TextAlign::Right);

    // ---- the clock, top center
    if (mode == Mode::Race) {
        osd(canvas, formatTime(race.time), Vec2{center.x, top + 2}, 26, TextAlign::Center);
        osd(canvas, "LAP " + std::to_string(std::min(race.lap + 1, race.laps)) + "/" + std::to_string(race.laps) + "    GATE " +
                        std::to_string(race.nextGate + 1) + "/" + std::to_string(race.gateCount),
            Vec2{center.x, top + 34}, 13, TextAlign::Center);
        if (race.phase == Race::Phase::Countdown) {
            osd(canvas, std::to_string(int(std::ceil(race.countdown))), Vec2{center.x, size.y * 0.24f}, 84, TextAlign::Center, kOrange);
        }
        drawGateMarker(engine, canvas, courseGates()[size_t(race.nextGate)]);
    } else {
        int seconds = int(flightTime_);
        osd(canvas, format("FLY %02.0f", double(seconds / 60)) + format(":%02.0f", double(seconds % 60)), Vec2{center.x, top + 6}, 18, TextAlign::Center);
    }

    // ---- the artificial horizon, as the flight controller sees it: level is the middle
    // of the screen, whatever the camera's tilt.
    if (view == View::Onboard) {
        Vec3 nose = drone.forward(), wing = drone.right(), up = drone.up();
        float pitch = std::asin(std::clamp(nose.y, -1.0f, 1.0f));               // nose up is positive
        float roll = std::atan2(-wing.y, up.y);                                 // right wing down is positive
        Vec2 along{std::cos(roll), -std::sin(roll)};
        Vec2 middle = center + Vec2{0, std::clamp(degrees(pitch) * 2.2f, -70.0f, 70.0f)};
        Vec4 faint{1, 1, 1, 0.7f};
        canvas.line(middle - along * 96, middle - along * 26, 1.5f, faint);
        canvas.line(middle + along * 26, middle + along * 96, 1.5f, faint);
        canvas.ring(center, 4, 1.5f, faint);
        canvas.line(center - Vec2{16, 0}, center - Vec2{7, 0}, 1.5f, faint);
        canvas.line(center + Vec2{7, 0}, center + Vec2{16, 0}, 1.5f, faint);
    }

    // ---- throttle, speed, height: bottom center, between the thumbs
    float bottom = size.y - 34;
    float height = drone.position.y - groundHeight(drone.position.x, drone.position.z);
    osd(canvas, format("THR %.0f", sticks.throttle * 100), Vec2{center.x - 118, bottom}, 14, TextAlign::Center);
    osd(canvas, format("%.0f KM/H", drone.speed() * 3.6f), Vec2{center.x, bottom - 2}, 17, TextAlign::Center);
    osd(canvas, format("ALT %.1f M", std::max(height, 0.0f)), Vec2{center.x + 118, bottom}, 14, TextAlign::Center);

    // ---- warnings, one at a time, the most urgent first
    std::string warning;
    Vec4 warningColor = kRed;
    if (isCrashed) warning = lostSignal_ ? "RX LOSS" : "CRASHED";
    else if (isWaitingToArm) warning = "LOWER THE THROTTLE TO ARM";
    else if (drone.charge() <= 0) warning = "BATTERY EMPTY";
    else if (cell < 3.3f) warning = "LAND NOW";
    else if (link < 0.25f) warning = "RX LOSS SOON: TURN BACK";
    else if (cell < 3.5f) { warning = "LOW BATTERY"; warningColor = kOrange; }
    if (!warning.empty() && (blink || isCrashed || isWaitingToArm)) osd(canvas, warning, Vec2{center.x, size.y * 0.3f}, 22, TextAlign::Center, warningColor);
    if (noticeTimer_ > 0) osd(canvas, notice_, Vec2{center.x, size.y * 0.3f + 30}, 15, TextAlign::Center);

    if (flashTimer_ > 0) canvas.rect(Vec2{0, 0}, size, Vec4{0.3f, 1, 0.5f, 0.22f * flashTimer_ / 0.25f});
    controls.draw(engine, canvas);
}

/// Shows where the next gate is: a ring on it when it is in view, an arrow at the edge
/// of the screen when it is not.
void FPVGame::drawGateMarker(Engine& engine, Canvas& canvas, const Gate& gate) {
    const Transform* transform = engine.scene.get<Transform>(camera_);
    const Camera* lens = engine.scene.get<Camera>(camera_);
    if (!transform || !lens) return;
    Vec2 size = canvas.size();
    float aspect = size.x / size.y;
    // The gate as the camera sees it: x right, y up, -z ahead.
    Vec3 local = rotate(simd::conjugate(transform->rotation), gate.center - transform->position);
    float tangent = std::tan(lens->fovY * 0.5f);
    if (local.z < -0.5f) {
        Vec2 point{local.x / (-local.z * tangent * aspect), local.y / (-local.z * tangent)};
        point = throughLens(point, aspect, engine.renderer().settings.lensDistortion);
        if (std::abs(point.x) < 0.94f && std::abs(point.y) < 0.9f) {
            Vec2 at{(point.x * 0.5f + 0.5f) * size.x, (0.5f - point.y * 0.5f) * size.y};
            canvas.ring(at, 11, 2, kGreen);
            osd(canvas, format("%.0f M", simd::length(local)), at + Vec2{0, 14}, 12, TextAlign::Center, kGreen);
            return;
        }
    }
    // Out of view: point toward it from the middle of the nearer side.
    bool isLeft = local.x < 0;
    Vec2 tip{isLeft ? 22.0f : size.x - 22.0f, size.y * 0.5f};
    float back = isLeft ? 16.0f : -16.0f;
    canvas.polyline({tip + Vec2{back, -13}, tip, tip + Vec2{back, 13}}, 3.5f, kGreen);
}

// ---------------------------------------------------------------- the results

void FPVGame::drawFinished(Engine& engine, Canvas& canvas) {
    Vec2 size = canvas.size();
    float center = size.x * 0.5f;
    canvas.rect(Vec2{0, 0}, size, Vec4{0.02f, 0.03f, 0.06f, 0.6f});
    float panelHeight = 214 + 22 * float(race.lapTimes.size());
    float y = std::max((size.y - panelHeight) * 0.5f, 8.0f);
    canvas.roundedRect(Vec2{center - 190, y}, Vec2{380, panelHeight}, 16, Vec4{0.05f, 0.07f, 0.1f, 0.85f});
    y += 22;
    canvas.text("Finished", Vec2{center, y}, 20, Vec4{1, 1, 1, 0.8f}, TextAlign::Center);
    y += 32;
    canvas.text(formatTime(race.time), Vec2{center, y}, 44, kOrange, TextAlign::Center);
    y += 58;
    canvas.text(isNewBest_ ? "New best time" : "Best  " + formatTime(bestTime), Vec2{center, y}, 16, isNewBest_ ? kGreen : Vec4{1, 1, 1, 0.8f},
                TextAlign::Center);
    y += 30;
    for (size_t i = 0; i < race.lapTimes.size(); i++) {
        canvas.text("Lap " + std::to_string(i + 1) + "   " + formatTime(race.lapTimes[i]), Vec2{center, y}, 15, Vec4{1, 1, 1, 0.8f}, TextAlign::Center);
        y += 22;
    }
    y += 12;
    if (button(engine, canvas, "Race again", Vec2{center - 166, y}, Vec2{160, 42}, true)) begin(engine, Mode::Race);
    if (button(engine, canvas, "Menu", Vec2{center + 6, y}, Vec2{160, 42})) screen = Screen::Menu;
}
