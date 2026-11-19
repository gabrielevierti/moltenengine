#include "Track.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace molten;

namespace fpv {

bool Gate::isPassed(Vec3 start, Vec3 end) const {
    float before = simd::dot(start - center, forward());
    float after = simd::dot(end - center, forward());
    if (!(before < 0 && after >= 0)) return false;              // not crossing, or crossing backwards
    float t = before / (before - after);
    Vec3 crossing = start + (end - start) * t - center;
    return std::abs(simd::dot(crossing, right())) <= width * 0.5f && std::abs(crossing.y) <= height * 0.5f;
}

void Race::update(float dt) {
    switch (phase) {
        case Phase::Countdown:
            countdown -= dt;
            if (countdown <= 0) phase = Phase::Running;
            break;
        case Phase::Running: time += dt; break;
        case Phase::Finished: break;
    }
}

bool Race::pass(int index) {
    if (phase != Phase::Running || index != nextGate) return false;
    nextGate++;
    if (nextGate == gateCount) {
        nextGate = 0;
        lap++;
        float earlier = 0;
        for (float lapTime : lapTimes) earlier += lapTime;
        lapTimes.push_back(time - earlier);
        if (lap == laps) phase = Phase::Finished;
    }
    return true;
}

const std::vector<Gate>& courseGates() {
    static const std::vector<Gate> gates = {
        Gate::at(Vec3{0, 1.8f, -18}, 0),         Gate::at(Vec3{-6, 2.6f, -42}, 25),
        Gate::at(Vec3{-26, 5.0f, -58}, 75),      Gate::at(Vec3{-50, 2.6f, -46}, 140),
        Gate::at(Vec3{-56, 1.8f, -16}, 185),     Gate::at(Vec3{-44, 6.0f, 12}, 225, 4.2f, 3.2f),
        Gate::at(Vec3{-18, 1.8f, 22}, 270),      Gate::at(Vec3{4, 2.2f, 8}, 330),
    };
    return gates;
}

float distanceToCourse(Vec2 point) {
    const std::vector<Gate>& gates = courseGates();
    float nearest = 1e9f;
    for (size_t i = 0; i < gates.size(); i++) {
        const Gate& gate = gates[i];
        const Gate& next = gates[(i + 1) % gates.size()];
        Vec2 a{gate.center.x, gate.center.z}, b{next.center.x, next.center.z};
        Vec2 ab = b - a;
        float t = std::clamp(simd::dot(point - a, ab) / simd::dot(ab, ab), 0.0f, 1.0f);
        nearest = std::min(nearest, simd::length(point - (a + ab * t)));
    }
    return nearest;
}

std::string formatTime(float seconds) {
    int minutes = int(seconds) / 60;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%05.2f", minutes, double(seconds - float(minutes * 60)));
    return buffer;
}

}  // namespace fpv
