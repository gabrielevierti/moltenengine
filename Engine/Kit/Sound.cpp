#include "Kit/Sound.hpp"

#include "Game/Component.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

bool roomPreset(const std::string& room, float& size, float& damping, float& wet) {
    struct Preset {
        const char* name;
        float size, damping, wet;
    };
    static const Preset presets[] = {
        {"Small Room", 0.25f, 0.55f, 0.2f}, {"Hall", 0.65f, 0.4f, 0.35f},  {"Cave", 0.85f, 0.2f, 0.5f},
        {"Bathroom", 0.35f, 0.05f, 0.45f},  {"Cathedral", 1.0f, 0.3f, 0.5f}, {"Forest", 0.45f, 0.9f, 0.12f},
    };
    for (const Preset& preset : presets) {
        if (room == preset.name) {
            size = preset.size, damping = preset.damping, wet = preset.wet;
            return true;
        }
    }
    return false;
}

float ReverbZone::inside(const World& world, Entity self, Vec3 point) const {
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return 0;
    float fade = std::max(edge, 0.001f);
    float beyond;                                // how far past the zone's surface; negative: inside
    Vec3 scale{simd::length(xyz(transform->world.columns[0])), simd::length(xyz(transform->world.columns[1])),
               simd::length(xyz(transform->world.columns[2]))};
    if (isBox) {
        Vec3 local = xyz(simd::inverse(transform->world) * Vec4{point.x, point.y, point.z, 1});
        Vec3 out = simd::abs(local) * scale - halfSize * scale;
        beyond = std::max({out.x, out.y, out.z});
    } else {
        beyond = simd::length(point - transform->worldPosition()) - radius * std::max({scale.x, scale.y, scale.z});
    }
    // Full a little inside the surface, nothing at it: the edge lies within the zone.
    return std::clamp(-beyond / fade, 0.0f, 1.0f);
}

void ReverbZone::lateUpdate(World& world, Entity self, float) {
    Mat4 ears;
    if (!world.ears(ears)) return;
    float weight = inside(world, self, xyz(ears.columns[3]));
    if (weight <= 0) return;
    float s = size, d = damping, w = wet;
    roomPreset(room, s, d, w);
    world.askForRoom(weight, s, d, w);
}

void linkSound() {
    ComponentRegistry::instance().setCategory("ReverbZone", "Audio");
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(ReverbZone,
                 MOLTEN_FIELD(room).choices({"Small Room", "Hall", "Cave", "Bathroom", "Cathedral", "Forest", "Custom"})
                     .help("How it rings. Custom: as size, damping and wet say"),
                 MOLTEN_FIELD(radius).range(0.5f, 200).help("Meters from the entity"), MOLTEN_FIELD(isBox).help("A box instead of a sphere"),
                 MOLTEN_FIELD(halfSize).help("With Is Box: half the box's size in each direction"),
                 MOLTEN_FIELD(edge).range(0, 20).help("Meters over which it fades in at its edge"),
                 MOLTEN_FIELD(size).range(0, 1).help("Custom: 0 a small room, 1 a cathedral"),
                 MOLTEN_FIELD(damping).range(0, 1).help("Custom: 0 bright, as stone; 1 dull, as curtains"),
                 MOLTEN_FIELD(wet).range(0, 1).help("Custom: how much of the ringing is heard"))
