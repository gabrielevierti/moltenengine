#include "Water/Water.hpp"

#include "Game/Component.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

void retireOldWater(Scene& scene, RenderSettings& settings) {
    // What was water: a plane with a material that said so, or with a Water component.
    std::vector<Entity> components, waters;
    std::vector<float> sheets;                  // how wide each component's sheet was
    scene.each<Water, Transform>([&](Entity entity, Water& water, Transform&) {
        components.push_back(entity);
        waters.push_back(entity);
        sheets.push_back(water.size);
        // Its wind was its own; there is one wind now, the scene's. A scene that has none takes the water's.
        if (simd::length(settings.wind) < 0.01f) {
            Vec2 along = water.windDirection;
            float length = simd::length(along);
            along = length > 1e-4f ? along / length : Vec2{1, 0};
            settings.wind = Vec3{along.x, 0, along.y} * std::clamp(water.windSpeed, 0.0f, 40.0f);
        }
    });
    scene.each<MeshRenderer>([&](Entity entity, MeshRenderer& renderer) {
        if (renderer.material && renderer.material->wasWater && std::find(waters.begin(), waters.end(), entity) == waters.end()) waters.push_back(entity);
    });
    for (Entity entity : components) scene.remove<Water>(entity);
    if (waters.empty()) return;
    scene.updateTransforms();
    for (size_t i = 0; i < waters.size(); i++) {
        Entity entity = waters[i];
        MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        const Transform* transform = scene.get<Transform>(entity);
        // How wide it is: its mesh (the built-in plane is a meter square, when it is not loaded) times
        // its scale; a component's sheet was as wide as it said.
        float width = i < sheets.size() ? sheets[i] : 0.0f;
        if (renderer && transform) {
            Vec3 lowest{-0.5f, 0, -0.5f}, highest{0.5f, 0, 0.5f};
            bool known = renderer->meshName == "plane";
            if (renderer->mesh) lowest = renderer->mesh->bounds().min, highest = renderer->mesh->bounds().max, known = true;
            if (known) {
                float across = (highest.x - lowest.x) * simd::length(xyz(transform->world.columns[0]));
                float along = (highest.z - lowest.z) * simd::length(xyz(transform->world.columns[2]));
                width = std::max(width, std::max(across, along));
            } else {
                width = std::max(width, 1e9f);  // a mesh not known here: taken for a sea, as it was most likely
            }
        }
        // A pool, a basin, a pond: too small to be a sea. It stays a plane, of clear blue, seen through.
        if (renderer && width < 300.0f) {
            if (renderer->material) {
                renderer->material->alphaMode = AlphaMode::Blend;
                renderer->material->roughness = std::min(renderer->material->roughness, 0.08f);
            }
            continue;
        }
        // The first large one is the sea, unless the scene has one already.
        if (!settings.sea.enabled && transform) {
            SeaSettings& sea = settings.sea;
            sea.enabled = true;
            float top = renderer && renderer->mesh ? renderer->mesh->bounds().max.y : 0.0f;
            sea.level = transformPoint(transform->world, Vec3{0, top, 0}).y;
            if (renderer && renderer->material) {
                const Material& look = *renderer->material;
                sea.color = Vec3{look.baseColor.x, look.baseColor.y, look.baseColor.z};
                sea.clarity = SeaSettings::clarityOfMurk(look.baseColor.w);
                sea.foam = std::max(look.foam, 0.0f);
            }
        }
        if (renderer) renderer->isVisible = false;
    }
}

MOLTEN_COMPONENT(Water, MOLTEN_FIELD(size), MOLTEN_FIELD(resolution), MOLTEN_FIELD(windSpeed), MOLTEN_FIELD(windDirection), MOLTEN_FIELD(patchSize),
                 MOLTEN_FIELD(choppiness), MOLTEN_FIELD(waveHeight), MOLTEN_FIELD(foam))

}  // namespace molten
