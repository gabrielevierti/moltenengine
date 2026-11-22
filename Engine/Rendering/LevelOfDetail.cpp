// LevelOfDetail.cpp
#include "Rendering/Quality.hpp"
#include "Rendering/LevelOfDetail.hpp"

#include "Rendering/Material.hpp"
#include "Scene/Scene.hpp"

namespace molten {

/// Which version of each model under a LOD Group this view draws: by how far the group is from its eye.
void chooseLevelsOfDetail(Scene& scene, Vec3 eye) {
    if (scene.count<LodGroup>() == 0) return;
    scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform&) {
        renderer.isBeyondItsDetail = false;
        Entity path[16];
        int depth = 0;
        for (Entity at = entity; at.isValid() && depth < 16;) {
            if (const LodGroup* group = scene.get<LodGroup>(at)) {
                // Is it under the group's far version?
                bool isFar = false;
                for (int k = 0; k < depth && !isFar; k++) {
                    const Name* name = scene.get<Name>(path[k]);
                    isFar = name && name->value == group->far;
                }
                const Transform* place = scene.get<Transform>(at);
                float distance = place ? simd::length(place->worldPosition() - eye) : 0.0f;
                bool beyond = distance > group->distance * detailDistanceScale();      // the project's quality: how far detail lasts
                renderer.isBeyondItsDetail = isFar ? !beyond : beyond;
                return;
            }
            path[depth++] = at;
            const Transform* transform = scene.get<Transform>(at);
            at = transform ? transform->parent : Entity{};
        }
    });
}

}  // namespace molten
