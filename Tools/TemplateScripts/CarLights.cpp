// CarLights.cpp
// The car's lamps: headlamps that are on, rear lamps that glow, and glow hard under braking.
// It looks under the car for lights by name ("Headlight L", "Headlight R", "Brake Light L",
// "Brake Light R") and for the model's part called "Rear Lights", whose material it lights.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

struct CarLights {
    bool headlights = true;             // L turns them on and off
    float headlightBrightness = 16.0f;
    float tailBrightness = 0.5f;        // of the rear lamps when the brakes are off
    float brakeBrightness = 5.0f;       // and when they are on
    Vec3 brakeColor{1.0f, 0.06f, 0.03f};
    bool answersKeys = true;            // L switches the headlights: the player's car only

    float braking = 0;                  // eased: a filament takes a moment to glow and to fade
    bool ownsLampMaterial = false;

    /// A part of this car by its name: several cars have parts of the same names.
    static Entity partOf(World& world, Entity self, const char* name) {
        Entity found;
        world.scene.each<Name>([&](Entity entity, Name& named) {
            if (!found.isValid() && named.value == name && kit::vehicleOf(world.scene, entity) == self) found = entity;
        });
        return found;
    }
    Entity parts[5];                    // the lights and the lamps, found once
    bool hasParts = false;

    void update(World& world, Entity self, float dt) {
        const kit::VehicleBody* car = world.scene.get<kit::VehicleBody>(self);
        if (!car) return;
        if (answersKeys && world.input.wasPressed(Key::L)) headlights = !headlights;
        if (!hasParts) {
            const char* names[5] = {"Headlight L", "Headlight R", "Brake Light L", "Brake Light R", "Rear Lights"};
            for (int k = 0; k < 5; k++) parts[k] = partOf(world, self, names[k]);
            hasParts = true;
        }
        float wanted = car->controls.brake > 0.05f ? 1.0f : 0.0f;
        braking += (wanted - braking) * std::min(dt * (wanted > braking ? 30.0f : 12.0f), 1.0f);

        for (int k = 0; k < 2; k++) {
            if (Light* light = world.scene.get<Light>(parts[k])) {
                light->isEnabled = headlights;
                light->intensity = headlightBrightness;
            }
        }
        float glow = tailBrightness + (brakeBrightness - tailBrightness) * braking;
        for (int k = 2; k < 4; k++) {
            if (Light* light = world.scene.get<Light>(parts[k])) {
                light->color = brakeColor;
                light->intensity = glow * 0.7f;
            }
        }
        // The lamps themselves: their material gives off the same light, so that they bloom.
        if (MeshRenderer* lamps = world.scene.get<MeshRenderer>(parts[4])) {
            if (lamps->material && !ownsLampMaterial) {
                // A copy of its own to light: the material is an asset, shared and kept, and playing must not change it.
                lamps->material = std::make_shared<Material>(*lamps->material);
                lamps->material->assetName.clear();
                ownsLampMaterial = true;
            }
            if (lamps->material) lamps->material->emissive = brakeColor * (0.6f + 5.0f * braking);
        }
    }
};

MOLTEN_COMPONENT(CarLights, MOLTEN_FIELD(headlights).help("L turns them on and off"), MOLTEN_FIELD(headlightBrightness).range(0, 60),
                 MOLTEN_FIELD(tailBrightness).range(0, 5), MOLTEN_FIELD(brakeBrightness).range(0, 20), MOLTEN_FIELD(brakeColor).color(),
                 MOLTEN_FIELD(answersKeys).help("L switches the headlights: on for the player's car, off for the others"))
