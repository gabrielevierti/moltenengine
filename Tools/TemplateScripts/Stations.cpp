// Stations.cpp
// Which part of the showcase the car is at: the nearest entity called "Station 1",
// "Station 2"... within reach. It sets the shared value "station" (0 for none), which
// the interface turns into the station's name and what it shows.
#include "MoltenMetal.hpp"

#include <string>

using namespace molten;

struct Stations {
    float reach = 22.0f;                // meters from a station's middle

    void update(World& world, Entity self, float) {
        const Transform* car = world.scene.get<Transform>(self);
        if (!car) return;
        Vec3 at = car->worldPosition();
        int nearest = 0;
        float best = reach;
        for (int k = 1; k <= 16; k++) {
            const Transform* station = world.scene.get<Transform>(world.find("Station " + std::to_string(k)));
            if (!station) break;
            Vec3 middle = station->worldPosition();
            float away = simd::length(Vec3{middle.x - at.x, 0, middle.z - at.z});
            if (away < best) {
                best = away;
                nearest = k;
            }
        }
        world.setValue("station", float(nearest));
    }
};

MOLTEN_COMPONENT(Stations, MOLTEN_FIELD(reach).range(5, 60).help("How near a station's middle the car has to be"))
