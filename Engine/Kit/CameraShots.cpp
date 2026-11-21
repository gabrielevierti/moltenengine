// CameraShots.cpp
#include "Kit/CameraShots.hpp"

#include "Game/Component.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

void linkCameraShots() {}

Entity CameraShot::best(World& world) {
    Entity found;
    int highest = 0;
    world.scene.each<CameraShot, Transform>([&](Entity entity, CameraShot& shot, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!found.isValid() || shot.priority > highest) {
            found = entity;
            highest = shot.priority;
        }
    });
    return found;
}

void CameraDirector::lateUpdate(World& world, Entity self, float dt) {
    Transform* transform = world.scene.get<Transform>(self);
    Camera* camera = world.scene.get<Camera>(self);
    if (!transform) return;
    Entity next = CameraShot::best(world);
    const CameraShot* chosen = world.scene.get<CameraShot>(next);
    const Transform* place = world.scene.get<Transform>(next);
    if (!chosen || !place) return;
    if (next != shot) {
        // A new shot: from wherever the camera is now (even halfway through another blend).
        Vec3 scale;
        decompose(transform->world, fromPosition, fromRotation, scale);
        fromFieldOfView = camera ? camera->fovY * 57.29578f : chosen->fieldOfView;
        bool isFirst = !shot.isValid();
        shot = next;
        blendSeconds = chosen->blendSeconds;
        blended = isFirst || blendSeconds <= 0 ? 1.0f : 0.0f;
    } else if (blended < 1) {
        blended = std::min(1.0f, blended + dt / std::max(blendSeconds, 1e-4f));
    }
    Vec3 position, scale;
    Quat rotation;
    decompose(place->world, position, rotation, scale);
    float fieldOfView = chosen->fieldOfView;
    if (blended < 1) {
        float t = blended * blended * (3 - 2 * blended);        // slow out, slow in
        position = fromPosition + (position - fromPosition) * t;
        rotation = simd::slerp(fromRotation, rotation, t);
        fieldOfView = fromFieldOfView + (fieldOfView - fromFieldOfView) * t;
    }
    // Where the transform keeps it: relative to its parent, if it has one.
    Mat4 placed = compose(position, rotation, transform->scale);
    if (const Transform* parent = world.scene.get<Transform>(transform->parent)) placed = simd::inverse(parent->world) * placed;
    decompose(placed, transform->position, transform->rotation, scale);
    transform->world = compose(position, rotation, transform->scale);
    if (camera) camera->fovY = std::clamp(fieldOfView, 1.0f, 170.0f) * 0.017453293f;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(CameraShot, MOLTEN_FIELD(priority).range(0, 100).help("The highest of the shots switched on is filmed"),
                 MOLTEN_FIELD(blendSeconds).range(0, 10).help("How long the camera takes to come to it; 0 cuts"),
                 MOLTEN_FIELD(fieldOfView).range(5, 120).help("Degrees, top to bottom"))
MOLTEN_COMPONENT(CameraDirector)
