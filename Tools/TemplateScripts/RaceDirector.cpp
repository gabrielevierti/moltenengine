// RaceDirector.cpp
// The race: the grid, the start, the order, the finish. Put it on any one entity.
//
//   Mode        the preference "mode": 0 a race against the cars with a RacerAI, 1 a time trial
//               alone (they are switched off). "laps" is how long a race is (3 if not chosen).
//   Grid        "Grid 1", "Grid 2"... are the places, from the front: the rivals take the
//               first ones in their order, the player's car ("Car") the last.
//   Start       the five "Start Light 1" to "Start Light 5" on the gantry, and "Light 1" to
//               "Light 5" on the screen, come on one a second; a moment later all go out: go.
//               Until then every car is held on its brakes.
//   Order       every car's laps and how far round the line through the Waypoints it is.
//   Finish      after the last lap, the group "Results" shows the order: "Result 1" (a name) and
//               "Result 1 Time" (the time, or the gap to the winner)...
//
// Shared values: {position}, {cars}, {raceLap}, {raceLaps}, {isRacing} (0 in a time trial),
// {startLights} 0 to 5 while they are lit.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace molten;

struct RaceDirector {
    float lightsEvery = 1.0f;           // seconds between one light and the next
    std::string drivers;                // the rivals' names on the results, in order, by commas: "ROSSI,BIANCHI"

    struct Runner {
        Entity car;
        std::string name;
        bool isPlayer = false;
        int at = 0;                     // its nearest point of the line
        int laps = 0;                   // finished; -1 behind the line, before the start
        bool hasLeftLine = false;       // some way round since it last crossed the line
        float finishedAt = -1;
    };
    std::vector<Runner> runners;
    std::vector<Vec3> line;
    int laps = 3;
    bool isRace = true;
    enum class Phase { Grid, Lights, Racing, Finished } phase = Phase::Grid;
    float clock = 0, goAt = 0, raceTime = 0;
    int lit = 0;
    bool hasPlaced = false;
    uint32_t seed = 2026;

    float random01() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    }

    static void hold(kit::VehicleBody* car) {
        kit::Controls held;
        held.brake = 1;
        car->setControls(held);
        car->controlsAreGiven = true;
    }

    /// The light on the gantry and the one on the screen, on or off.
    void showLight(World& world, int k, bool on) {
        if (MeshRenderer* lamp = world.scene.get<MeshRenderer>(world.find("Start Light " + std::to_string(k)))) {
            if (lamp->material) lamp->material->emissive = on ? Vec3{9, 0.4f, 0.2f} : Vec3{0.05f, 0, 0};
        }
        if (kit::UIPanel* dot = world.scene.get<kit::UIPanel>(world.find("Light " + std::to_string(k)))) {
            dot->color = on ? Vec4{1, 0.12f, 0.08f, 1} : Vec4{0.12f, 0.12f, 0.14f, 0.85f};
        }
    }

    void start(World& world, Entity) {
        isRace = world.preference("mode", 0) < 0.5f;
        laps = std::clamp(int(world.preference("laps", 3)), 1, 50);
        for (int k = 1; k < 2000; k++) {
            const Transform* point = world.scene.get<Transform>(world.find("Waypoint " + std::to_string(k)));
            if (!point) break;
            line.push_back(point->worldPosition());
        }
        // Who races: the player's car, and in a race every car with a RacerAI.
        Entity player = world.find("Car");
        if (world.scene.get<kit::VehicleBody>(player)) runners.push_back(Runner{player, "YOU", true});
        // The rivals, by name: "Rival 1", "Rival 2"...
        for (int k = 1; k < 64; k++) {
            Entity rival = world.find("Rival " + std::to_string(k));
            if (!rival.isValid()) break;
            Transform* place = world.scene.get<Transform>(rival);
            if (!isRace) {
                if (place) place->isActive = false;                         // a time trial is alone on the track
                continue;
            }
            std::string shown = "RIVAL " + std::to_string(k);
            size_t from = 0;
            for (int skip = 1; skip < k && from != std::string::npos; skip++) {
                from = drivers.find(',', from);
                if (from != std::string::npos) from++;
            }
            if (from != std::string::npos && from < drivers.size()) shown = drivers.substr(from, drivers.find(',', from) - from);
            runners.push_back(Runner{rival, shown, false});
        }
        world.scene.updateTransforms();
        world.setValue("isRacing", isRace ? 1.0f : 0.0f);
        world.setValue("raceLaps", float(laps));
        world.setValue("cars", float(runners.size()));
        if (Transform* panel = world.scene.get<Transform>(world.find("Race Panel"))) panel->isActive = isRace;
        if (Transform* lights = world.scene.get<Transform>(world.find("Start Lights"))) lights->isActive = isRace;
        if (Transform* results = world.scene.get<Transform>(world.find("Results"))) results->isActive = false;
        // Each lamp its own copy of the material they share, to light them one by one.
        for (int k = 1; k <= 5; k++) {
            MeshRenderer* lamp = world.scene.get<MeshRenderer>(world.find("Start Light " + std::to_string(k)));
            if (lamp && lamp->material) lamp->material = std::make_shared<Material>(*lamp->material);
            showLight(world, k, false);
        }
        phase = isRace ? Phase::Grid : Phase::Racing;
    }

    /// Every car to its place on the grid, still: the rivals first, the player last.
    void placeOnGrid(World& world) {
        int slot = 1;
        auto put = [&](Runner& runner) {
            const Transform* grid = world.scene.get<Transform>(world.find("Grid " + std::to_string(slot++)));
            kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(runner.car);
            Transform* place = world.scene.get<Transform>(runner.car);
            if (!grid || !car || !place) return;
            Vec3 at = grid->worldPosition() + Vec3{0, 0.4f, 0};
            car->body.position = at;
            car->body.orientation = grid->rotation;
            car->body.velocity = Vec3{0, 0, 0};
            car->body.angularVelocity = Vec3{0, 0, 0};
            place->position = at;
            place->rotation = grid->rotation;
        };
        for (Runner& runner : runners) if (!runner.isPlayer) put(runner);
        for (Runner& runner : runners) if (runner.isPlayer) put(runner);
        world.scene.updateTransforms();
    }

    int nearestPoint(World& world, Entity car) const {
        const kit::VehicleBody* body = world.scene.get<kit::VehicleBody>(car);
        int best = 0;
        float nearest = 1e30f;
        for (size_t i = 0; body && i < line.size(); i++) {
            float distance = simd::length_squared(line[i] - body->body.position);
            if (distance < nearest) nearest = distance, best = int(i);
        }
        return best;
    }

    void follow(Runner& runner, World& world) {
        const kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(runner.car);
        if (!car || line.empty()) return;
        int count = int(line.size());
        float nearest = 1e30f;
        int found = runner.at;
        for (int d = -8; d <= 12; d++) {
            int i = (runner.at + d + count) % count;
            float distance = simd::length_squared(line[size_t(i)] - car->body.position);
            if (distance < nearest) nearest = distance, found = i;
        }
        // Over the line, forward, having been round: a lap. (The line starts at the finish.)
        if (runner.at > count * 3 / 4 && found < count / 4 && runner.hasLeftLine) {
            runner.laps++;
            runner.hasLeftLine = false;
            if (runner.laps >= laps && runner.finishedAt < 0) runner.finishedAt = raceTime;
        }
        // Back over it the wrong way: that lap is not done after all.
        if (runner.at < count / 4 && found > count * 3 / 4 && !runner.hasLeftLine) {
            runner.laps--;
            runner.hasLeftLine = true;
        }
        if (found > count / 4 && found < count * 3 / 4) runner.hasLeftLine = true;
        runner.at = found;
    }

    void update(World& world, Entity, float dt) {
        if (dt <= 0 || !isRace) return;
        clock += dt;
        if (!hasPlaced) {
            placeOnGrid(world);
            hasPlaced = true;
            // Where each one is on the line; on the grid, behind it: not yet on its first lap.
            for (Runner& runner : runners) {
                runner.at = nearestPoint(world, runner.car);
                if (runner.at > int(line.size()) / 2) {
                    runner.laps = -1;
                    runner.hasLeftLine = true;
                }
            }
        }
        if (phase == Phase::Grid || phase == Phase::Lights) {
            for (Runner& runner : runners) {
                if (kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(runner.car)) hold(car);
            }
            phase = Phase::Lights;
            int wanted = std::min(5, int(clock / lightsEvery));
            while (lit < wanted) showLight(world, ++lit, true);
            if (lit == 5 && goAt <= 0) goAt = clock + 0.4f + 1.6f * random01();     // a moment no one can guess
            world.setValue("startLights", float(lit));
            if (goAt > 0 && clock >= goAt) {
                // Lights out.
                for (int k = 1; k <= 5; k++) showLight(world, k, false);
                world.setValue("startLights", 0);
                for (Runner& runner : runners) {
                    kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(runner.car);
                    if (!car) continue;
                    car->setControls(kit::Controls{});
                    car->controlsAreGiven = false;                          // the driver has it: keys, or the AI
                }
                world.setValue("raceStarted", 1);                           // what RacerAI waits for
                phase = Phase::Racing;
            }
            return;
        }

        raceTime += dt;
        for (Runner& runner : runners) follow(runner, world);
        // The order: finished first (by when), then by laps and how far round.
        std::vector<size_t> order(runners.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const Runner &x = runners[a], &y = runners[b];
            bool xDone = x.finishedAt >= 0, yDone = y.finishedAt >= 0;
            if (xDone != yDone) return xDone;
            if (xDone) return x.finishedAt < y.finishedAt;
            if (x.laps != y.laps) return x.laps > y.laps;
            if (x.at != y.at) return x.at > y.at;
            return a < b;
        });
        const Runner* player = nullptr;
        for (size_t place = 0; place < order.size(); place++) {
            const Runner& runner = runners[order[place]];
            if (runner.isPlayer) {
                player = &runner;
                world.setValue("position", float(place + 1));
            }
        }
        if (player) world.setValue("raceLap", float(std::clamp(player->laps + 1, 1, laps)));

        // The player across the line after the last lap: the results.
        if (player && player->finishedAt >= 0 && phase != Phase::Finished) {
            phase = Phase::Finished;
            if (Transform* results = world.scene.get<Transform>(world.find("Results"))) results->isActive = true;
            world.scene.updateTransforms();
        }
        if (phase == Phase::Finished) {
            float winner = -1;
            for (size_t place = 0; place < order.size(); place++) {
                const Runner& runner = runners[order[place]];
                if (place == 0) winner = runner.finishedAt;
                // Each row: the name ("Result 1"), and the time or the gap ("Result 1 Time").
                char time[48];
                if (runner.finishedAt >= 0 && place == 0) {
                    int minutes = int(runner.finishedAt) / 60;
                    std::snprintf(time, sizeof(time), "%d:%04.1f", minutes, runner.finishedAt - float(minutes) * 60);
                } else if (runner.finishedAt >= 0) {
                    std::snprintf(time, sizeof(time), "+%.1f s", runner.finishedAt - winner);
                } else {
                    std::snprintf(time, sizeof(time), "LAP %d", std::clamp(runner.laps + 1, 1, laps));
                }
                Vec4 colour = runner.isPlayer ? Vec4{1.0f, 0.62f, 0.3f, 1} : Vec4{1, 1, 1, 0.9f};
                std::string row = "Result " + std::to_string(place + 1);
                if (kit::HudText* name = world.scene.get<kit::HudText>(world.find(row))) name->text = runner.name, name->color = colour;
                if (kit::HudText* gap = world.scene.get<kit::HudText>(world.find(row + " Time"))) gap->text = time, gap->color = colour;
            }
            // The player's car drives itself home: the race is over for it.
            if (kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(player->car)) {
                kit::Controls coast;
                coast.brake = 0.3f;
                car->setControls(coast);
                car->controlsAreGiven = true;
            }
        }
    }
};

MOLTEN_COMPONENT(RaceDirector, MOLTEN_FIELD(lightsEvery).range(0.2f, 3).help("Seconds between one start light and the next"),
                 MOLTEN_FIELD(drivers).help("The rivals' names on the results, in order, by commas"))
