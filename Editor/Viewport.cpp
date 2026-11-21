// The 3D view: the editor's camera, clicking to select, the gizmo that moves, turns and
// scales the selection (with snapping), and the lines and icons drawn over the scene.
#include "Snapping.hpp"
#include "Editor.hpp"
#include "Rendering/Particles.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Vehicles.hpp"
#include "Kit/Spline.hpp"
#include "Kit/Terrain.hpp"
#include "Kit/Foliage.hpp"
#include "Audio/Audio.hpp"
#include "Panels/Widgets.hpp"

#include <ImGuizmo.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>

namespace molten::editor {

namespace {

const Vec4 kSelectionColor{1.0f, 0.48f, 0.18f, 1};

float* floats(Mat4& matrix) { return reinterpret_cast<float*>(&matrix); }

/// The entity under a point of the view: the nearest one the ray through it hits.
Entity pick(EditorContext& editor, const Ray& ray) {
    Scene& scene = editor.engine.scene;
    Entity best;
    float bestDistance = std::numeric_limits<float>::max();
    for (Entity entity : editor.scene.entities()) {
        const Transform* transform = scene.get<Transform>(entity);
        const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        if (!transform || !transform->isActiveInHierarchy) continue;
        std::optional<float> hit;
        if (renderer && renderer->mesh && renderer->isVisible) {
            // Test against the mesh's box in the mesh's own space: bring the ray there.
            // The direction is left unnormalized, so the distance keeps its world scale.
            Mat4 toLocal = simd::inverse(transform->world);
            Ray local{transformPoint(toLocal, ray.origin), transformDirection(toLocal, ray.direction)};
            // Against its triangles, not its box: a road's box covers the whole circuit, a forest's the whole map.
            hit = renderer->mesh->raycast(local);
        } else if (!renderer) {
            // Lights, cameras, effects and sounds: their icon, when that kind is shown.
            const GizmoSettings& shown = editor.gizmo;
            bool isHidden = (scene.has<Light>(entity) && !shown.showLights) || (scene.has<Camera>(entity) && !shown.showCameras) ||
                            (scene.has<ParticleEmitter>(entity) && !shown.showEffects) || (scene.has<kit::AudioSource>(entity) && !shown.showSounds);
            if (!isHidden) hit = intersectSphere(ray, transform->worldPosition(), 0.35f);
        }
        if (hit && *hit < bestDistance) {
            bestDistance = *hit;
            best = entity;
        }
    }
    return best;
}

/// A view of the scene must not be squeezed to nothing: below this it is held at this size,
/// docked (its place in the layout grows back) or floating.
const ImVec2 kLeastViewSize(360, 240);

void keepViewAtLeast() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window ? window->DockNode : nullptr;
    if (!node) return;
    ImVec2 size = node->Size;
    if (size.x >= kLeastViewSize.x && size.y >= kLeastViewSize.y) return;
    // Only where the layout has room for it: a window smaller than this as a whole is left as it is.
    ImVec2 whole = ImGui::GetMainViewport()->WorkSize;
    if (whole.x < kLeastViewSize.x * 2 || whole.y < kLeastViewSize.y * 2) return;
    ImGui::DockBuilderSetNodeSize(node->ID, ImVec2(std::max(size.x, kLeastViewSize.x), std::max(size.y, kLeastViewSize.y)));
}

/// Where a ray meets the ground (y = 0), or a point a few meters along it if it never does.
Vec3 groundPoint(const Ray& ray) {
    if (ray.direction.y < -1e-4f) {
        float t = -ray.origin.y / ray.direction.y;
        if (t > 0 && t < 500) return ray.origin + ray.direction * t;
    }
    return ray.origin + ray.direction * 8.0f;
}

void drawGrid(EditorContext& editor) {
    DebugDraw& lines = editor.engine.debug();
    // The grid grows with the camera's height: 1 m squares close to the ground, 10 m from higher
    // up, 100 m from higher still, always reaching well past what is in view. It follows the
    // camera in whole steps, so it does not swim.
    Vec3 eye = editor.scene.cameraPosition;
    float height = std::max(std::abs(eye.y), 1.0f);
    float step = 1.0f;
    while (height / step > 25.0f && step < 1000.0f) step *= 10.0f;
    const int half = 60;                                       // lines each side of the middle
    float reach = step * float(half);
    Vec3 center{std::round(eye.x / step) * step, 0, std::round(eye.z / step) * step};
    for (int i = -half; i <= half; i++) {
        float x = center.x + float(i) * step, z = center.z + float(i) * step;
        auto colorOf = [&](float coordinate) {
            long whole = std::lround(coordinate / step);
            if (std::abs(coordinate) < step * 0.5f) return Vec4{0.6f, 0.6f, 0.62f, 1};      // the axes
            return whole % 10 == 0 ? Vec4{0.36f, 0.38f, 0.42f, 1} : Vec4{0.22f, 0.23f, 0.26f, 1};
        };
        lines.line(Vec3{x, 0.002f, center.z - reach}, Vec3{x, 0.002f, center.z + reach}, colorOf(x));
        lines.line(Vec3{center.x - reach, 0.002f, z}, Vec3{center.x + reach, 0.002f, z}, colorOf(z));
    }
}

/// Lines around a selected entity: the box of its mesh, and what its other components mean in space.
bool gShowsAllColliders = false;

/// The outline of an entity's collider, in green, and with `depth` below 8 of its children's too.
/// Drawn a little larger than it is: a collider the size of its mesh would otherwise be hidden in the mesh's surface.
void drawColliderOf(EditorContext& editor, Entity entity, Vec4 color, int depth) {
    Scene& scene = editor.engine.scene;
    DebugDraw& lines = editor.engine.debug();
    const Transform* transform = scene.get<Transform>(entity);
    if (!transform) return;
    if (Collider* collider = scene.get<Collider>(entity)) {
        Quat rotation;
        Vec3 worldPosition, worldScale;
        decompose(transform->world, worldPosition, rotation, worldScale);
        Vec3 center = worldPosition + rotate(rotation, collider->offset);
        const float larger = 1.012f;
        if (collider->isTrigger) color = Vec4{1.0f, 0.85f, 0.2f, color.w};         // triggers in yellow: they let things through
        if (collider->shape == ColliderShape::Sphere) {
            lines.sphere(center, collider->radius * larger, color, 32);
        } else if (collider->shape == ColliderShape::Box) {
            lines.box(center, collider->halfExtents * larger + Vec3{0.004f, 0.004f, 0.004f}, rotation, color);
            // And its diagonals on top, so that a flat floor's box is seen from above too.
            Vec3 h = collider->halfExtents * larger;
            lines.line(center + rotate(rotation, Vec3{-h.x, h.y, -h.z}), center + rotate(rotation, Vec3{h.x, h.y, h.z}), color * Vec4{1, 1, 1, 0.5f});
            lines.line(center + rotate(rotation, Vec3{h.x, h.y, -h.z}), center + rotate(rotation, Vec3{-h.x, h.y, h.z}), color * Vec4{1, 1, 1, 0.5f});
        } else if (collider->shape == ColliderShape::Capsule) {
            float r = collider->radius * larger;
            Vec3 axis = rotate(rotation, Vec3{0, collider->halfHeight, 0});
            lines.sphere(center + axis, r, color, 20);
            lines.sphere(center - axis, r, color, 20);
            for (Vec3 side : {Vec3{r, 0, 0}, Vec3{-r, 0, 0}, Vec3{0, 0, r}, Vec3{0, 0, -r}}) {
                lines.line(center + axis + rotate(rotation, side), center - axis + rotate(rotation, side), color);
            }
        } else if (collider->shape == ColliderShape::Mesh && collider->isConvex) {
            // Wrapped here, the first time it is drawn: the same shape the game will use.
            if (!collider->convex && !collider->meshName.empty() && simd::length_squared(collider->meshScale - transform->scale) > 1e-10f) {
                collider->meshScale = transform->scale;
                if (std::optional<MeshData> data = editor.engine.readMeshData(collider->meshName)) {
                    collider->convex = ConvexShape::build(data->positions, transform->scale);
                }
            }
            if (const ConvexShape* hull = collider->convex.get()) {
                // Each face's edges: its corners, in order around its middle.
                float size = simd::length(hull->bounds.max - hull->bounds.min);
                for (Vec4 plane : hull->planes) {
                    Vec3 normal = xyz(plane);
                    std::vector<Vec3> onFace;
                    for (Vec3 p : hull->points) {
                        if (std::abs(simd::dot(normal, p) - plane.w) < size * 2e-3f) onFace.push_back(p);
                    }
                    if (onFace.size() < 3) continue;
                    Vec3 middle{0, 0, 0};
                    for (Vec3 p : onFace) middle += p;
                    middle /= float(onFace.size());
                    Vec3 across = safeNormalize(onFace[0] - middle, Vec3{1, 0, 0});
                    Vec3 other = simd::cross(normal, across);
                    std::sort(onFace.begin(), onFace.end(), [&](Vec3 a, Vec3 b) {
                        return std::atan2(simd::dot(a - middle, other), simd::dot(a - middle, across)) <
                               std::atan2(simd::dot(b - middle, other), simd::dot(b - middle, across));
                    });
                    for (size_t k = 0; k < onFace.size(); k++) {
                        Vec3 a = onFace[k] * larger, b = onFace[(k + 1) % onFace.size()] * larger;
                        lines.line(center + rotate(rotation, a), center + rotate(rotation, b), color);
                    }
                }
            } else if (const MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && renderer->mesh) {
                const AABB& box = renderer->mesh->bounds();
                lines.box(worldPosition + rotate(rotation, box.center() * worldScale), box.halfExtents() * worldScale * larger, rotation, color);
            }
        } else if (collider->shape == ColliderShape::Mesh) {
            if (collider->mesh && collider->mesh->triangleCount() <= 6000) {
                // Its triangles themselves.
                const MeshShape& shape = *collider->mesh;
                for (size_t t = 0; t < shape.triangleCount(); t++) {
                    Vec3 corner[3];
                    for (int k = 0; k < 3; k++) corner[k] = center + rotate(rotation, shape.points[shape.indices[t * 3 + size_t(k)]]) + Vec3{0, 0.01f, 0};
                    for (int k = 0; k < 3; k++) lines.line(corner[k], corner[(k + 1) % 3], color * Vec4{1, 1, 1, 0.6f});
                }
            } else if (collider->mesh) {
                lines.box(center + rotate(rotation, collider->mesh->bounds.center()), collider->mesh->bounds.halfExtents(), rotation, color);
            } else if (const MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && renderer->mesh) {
                // Not read yet (that happens when the game runs): the box of the model it will be.
                const AABB& box = renderer->mesh->bounds();
                lines.box(worldPosition + rotate(rotation, box.center() * worldScale), box.halfExtents() * worldScale * larger, rotation, color);
            }
        }
    }
    // A vehicle hits walls with the corners of a box of its size, and with its Bumpers: drawn in orange.
    if (const kit::VehicleBody* vehicle = scene.get<kit::VehicleBody>(entity)) {
        Quat rotation;
        Vec3 worldPosition, worldScale;
        decompose(transform->world, worldPosition, rotation, worldScale);
        const Vec4 orange{1.0f, 0.55f, 0.15f, color.w};
        // With colliders of its own it hits with those (drawn as colliders): the box is not used.
        bool hasColliders = false;
        scene.each<Collider, Transform>([&](Entity part, Collider& collider, Transform&) {
            if (!collider.isTrigger && kit::vehicleOf(scene, part) == entity) hasColliders = true;
        });
        if (vehicle->hitsWithItsBox && !hasColliders) {
            Vec3 low{-0.5f * vehicle->size.x, -0.18f * vehicle->size.y, -0.5f * vehicle->size.z};
            Vec3 high{0.5f * vehicle->size.x, 0.62f * vehicle->size.y, 0.5f * vehicle->size.z};
            lines.box(worldPosition + rotate(rotation, (low + high) * 0.5f), (high - low) * 0.5f, rotation, orange);
            for (int corner = 0; corner < 8; corner++) {
                Vec3 local{corner & 1 ? high.x : low.x, corner & 2 ? high.y : low.y, corner & 4 ? high.z : low.z};
                lines.sphere(worldPosition + rotate(rotation, local), 0.05f, orange, 8);
            }
        }
        // Its middle of mass, and where its front is.
        lines.line(worldPosition, worldPosition + rotate(rotation, Vec3{0, 0, -0.6f * vehicle->size.z}), orange * Vec4{1, 1, 1, 0.7f});
    }
    if (scene.get<kit::Bumper>(entity)) lines.sphere(transform->worldPosition(), 0.08f, Vec4{1.0f, 0.55f, 0.15f, color.w}, 10);
    if (depth >= 8) return;
    for (Entity child : editor.scene.children(entity)) drawColliderOf(editor, child, color, depth + 1);
}

void drawOutline(EditorContext& editor, Entity entity) {
    Scene& scene = editor.engine.scene;
    DebugDraw& lines = editor.engine.debug();
    const Transform* transform = scene.get<Transform>(entity);
    if (!transform) return;
    Vec3 position = transform->worldPosition();
    const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
    if (renderer && renderer->mesh) {
        // The eight corners of the mesh's box, moved to the world, joined by twelve edges.
        const AABB& box = renderer->mesh->bounds();
        Vec3 corners[8];
        for (int i = 0; i < 8; i++) {
            Vec3 corner{i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y, i & 4 ? box.max.z : box.min.z};
            corners[i] = transformPoint(transform->world, corner);
        }
        for (int i = 0; i < 8; i++) {
            for (int bit = 1; bit <= 4; bit <<= 1) {
                if (!(i & bit)) lines.line(corners[i], corners[i | bit], kSelectionColor);
            }
        }
    }
    if (const Light* light = scene.get<Light>(entity)) {
        Vec4 color = vec4(light->color, 1);
        Vec3 forward = safeNormalize(transformDirection(transform->world, Vec3{0, 0, -1}));
        if (light->type == LightType::Point) lines.sphere(position, light->range, color, 32);
        else lines.line(position, position + forward * (light->type == LightType::Spot ? light->range : 3.0f), color);
    }
    if (scene.has<Camera>(entity)) lines.axes(transform->world, 1.0f);
    // What it is solid as, and what everything under it is: a car's colliders are on its parts.
    drawColliderOf(editor, entity, Vec4{0.25f, 1.0f, 0.35f, 1}, 0);
}

/// Lights, cameras and emitters have no mesh to see or click: each gets a small disc with a letter.
void drawIcons(EditorContext& editor, const ViewportInfo& viewport) {
    Scene& scene = editor.engine.scene;
    for (Entity entity : editor.scene.entities()) {
        if (scene.has<MeshRenderer>(entity)) continue;
        const GizmoSettings& shown = editor.gizmo;
        const Light* light = shown.showLights ? scene.get<Light>(entity) : nullptr;
        const char* letter = light                                                   ? "L"
                             : shown.showCameras && scene.has<Camera>(entity)          ? "C"
                             : shown.showEffects && scene.has<ParticleEmitter>(entity) ? "P"
                             : shown.showSounds && scene.has<kit::AudioSource>(entity) ? "S"
                                                                                       : nullptr;
        ImVec2 at;
        if (!letter || !viewport.worldToScreen(scene.get<Transform>(entity)->worldPosition(), at)) continue;
        // Inside the view, and not on its tool bar: an icon over the buttons hides them and takes their clicks.
        const float kToolBar = 44;
        if (at.x < viewport.position.x + 10 || at.x > viewport.position.x + viewport.size.x - 10 || at.y < viewport.position.y + kToolBar ||
            at.y > viewport.position.y + viewport.size.y - 10) {
            continue;
        }
        bool isSelected = editor.scene.isSelected(entity);
        ImU32 fill = light ? IM_COL32(int(saturate(light->color.x) * 255), int(saturate(light->color.y) * 255), int(saturate(light->color.z) * 255), 255)
                           : IM_COL32(141, 150, 163, 255);
        viewport.drawList->AddCircleFilled(at, 11.5f, isSelected ? IM_COL32(255, 122, 47, 255) : IM_COL32(20, 24, 31, 220), 24);
        viewport.drawList->AddCircleFilled(at, 9.0f, fill, 24);
        ImVec2 size = ImGui::CalcTextSize(letter);
        viewport.drawList->AddText(ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), IM_COL32(20, 24, 31, 255), letter);
    }
}

/// What a camera sees, as the wire pyramid cameras are drawn as; which way a light shines.
/// Without them, turning either changes nothing one can see in the scene view.
void drawShapes(EditorContext& editor) {
    Scene& scene = editor.engine.scene;
    DebugDraw& lines = editor.engine.debug();
    // The shape of the game's picture, so the pyramid has the proportions of what will be seen.
    float aspect = 16.0f / 9.0f;
    const auto& views = editor.engine.renderer().views;
    if (views.size() > 1 && views[1].width > 0 && views[1].height > 0) aspect = float(views[1].width) / float(views[1].height);

    // While the scene is being built, only the particle systems that are selected (or are
    // under something selected) run: a scene full of smoke should not cost anything to edit.
    // While the game runs, all of them do.
    bool isPlaying = editor.scene.isPlaying();
    scene.each<ParticleEmitter, Transform>([&](Entity entity, ParticleEmitter& emitter, Transform&) {
        bool isShown = isPlaying;
        for (Entity up = entity; !isShown && up.isValid();) {
            isShown = editor.scene.isSelected(up);
            const Transform* above = scene.get<Transform>(up);
            up = above ? above->parent : Entity{};
        }
        emitter.isSimulated = isShown;
    });
    // Terrains: read and made while the scene is edited, as they are in the game.
    if (!isPlaying) {
        scene.each<kit::Terrain, Transform>([&](Entity entity, kit::Terrain& terrain, Transform&) { terrain.update(editor.engine, entity, 0); });
        scene.each<kit::Foliage, Transform>([&](Entity entity, kit::Foliage& foliage, Transform&) { foliage.update(editor.engine, entity, 0); });
    }
    // Curves: the line they make, a dot at each of their points; the road along one, made
    // again as soon as a point is moved, so that it is seen while it is laid out.
    scene.each<kit::Spline, Transform>([&](Entity entity, kit::Spline& spline, Transform&) {
        spline.refresh(editor.engine, entity);
        if (kit::SplineRoad* road = scene.get<kit::SplineRoad>(entity); road && !isPlaying && road->builtFrom != spline.signature) {
            road->rebuild(editor.engine, entity);
        }
        if (!editor.gizmo.showSplines) return;
        bool isChosen = editor.scene.isSelected(entity);
        for (Entity point : kit::Spline::pointsOf(editor.engine, entity)) isChosen = isChosen || editor.scene.isSelected(point);
        Vec4 color = isChosen ? Vec4{1.0f, 0.48f, 0.18f, 1} : Vec4{0.45f, 0.85f, 1.0f, 0.75f};
        const std::vector<Vec3>& points = spline.path.points;
        for (size_t i = 1; i < points.size(); i++) lines.line(points[i - 1], points[i], color);
        for (Entity point : kit::Spline::pointsOf(editor.engine, entity)) {
            if (const Transform* at = scene.get<Transform>(point)) lines.sphere(at->worldPosition(), isChosen ? 0.25f : 0.15f, color, 12);
        }
        // Which way it goes: an arrow a little way from the start.
        if (spline.path.length() > 1) {
            float along = std::min(2.0f, spline.path.length() * 0.25f);
            Vec3 tip = spline.path.pointAt(along), forward = spline.path.directionAt(along);
            Vec3 side = simd::normalize(simd::cross(forward, Vec3{0, 1, 0}) + Vec3{1e-6f, 0, 0}) * 0.4f;
            lines.line(tip, tip - forward * 0.7f + side, color);
            lines.line(tip, tip - forward * 0.7f - side, color);
        }
    });
    for (Entity entity : editor.scene.entities()) {
        const Transform* transform = scene.get<Transform>(entity);
        if (!transform) continue;
        bool isSelected = editor.scene.isSelected(entity);
        // Every collider of the scene, faintly, when that is asked for: to see what is solid where.
        if (gShowsAllColliders && !isSelected) drawColliderOf(editor, entity, Vec4{0.25f, 0.9f, 0.4f, 0.55f}, 8);
        Vec3 position, scale;
        Quat rotation;
        decompose(transform->world, position, rotation, scale);
        Vec3 forward = rotate(rotation, Vec3{0, 0, -1}), up = rotate(rotation, Vec3{0, 1, 0}), right = rotate(rotation, Vec3{1, 0, 0});

        if (const Camera* camera = editor.gizmo.showCameras ? scene.get<Camera>(entity) : nullptr) {
            Vec4 color = isSelected ? Vec4{1.0f, 0.48f, 0.18f, 1} : Vec4{0.75f, 0.8f, 0.88f, 0.7f};
            // Drawn to a few meters, not to the far plane: it says where the camera looks, not how far.
            float length = isSelected ? 3.0f : 1.6f;
            float halfHeight = camera->isOrthographic ? camera->orthographicHeight * 0.5f : std::tan(camera->fovY * 0.5f) * length;
            float halfWidth = halfHeight * aspect;
            float nearScale = camera->isOrthographic ? 1.0f : 0.12f;
            Vec3 farCenter = position + forward * length, nearCenter = position + forward * (length * 0.12f);
            Vec3 farCorners[4], nearCorners[4];
            const float signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i = 0; i < 4; i++) {
                Vec3 offset = right * (signs[i][0] * halfWidth) + up * (signs[i][1] * halfHeight);
                farCorners[i] = farCenter + offset;
                nearCorners[i] = nearCenter + offset * nearScale;
            }
            for (int i = 0; i < 4; i++) {
                lines.line(farCorners[i], farCorners[(i + 1) % 4], color);
                lines.line(nearCorners[i], nearCorners[(i + 1) % 4], color);
                lines.line(nearCorners[i], farCorners[i], color);
            }
            // A little roof on the far frame says which side is up.
            Vec3 roof = farCenter + up * (halfHeight * 1.35f);
            lines.line(farCorners[3] + right * (halfWidth * 0.5f), roof, color);
            lines.line(farCorners[2] - right * (halfWidth * 0.5f), roof, color);
        }
        if (const Light* light = editor.gizmo.showLights ? scene.get<Light>(entity) : nullptr) {
            Vec4 color = isSelected ? Vec4{1.0f, 0.48f, 0.18f, 1} : Vec4{1.0f, 0.86f, 0.4f, 0.75f};
            if (light->type == LightType::Directional) {
                // Parallel rays: the sun shines the same way everywhere.
                for (int i = 0; i < 5; i++) {
                    float angle = float(i) * 1.2566f;
                    Vec3 start = position + (i == 0 ? Vec3{0, 0, 0} : (right * std::cos(angle) + up * std::sin(angle)) * 0.35f);
                    lines.line(start, start + forward * 1.4f, color);
                }
                lines.line(position + forward * 1.4f, position + forward * 1.15f + right * 0.1f, color);
                lines.line(position + forward * 1.4f, position + forward * 1.15f - right * 0.1f, color);
            } else if (light->type == LightType::Spot) {
                float reach = std::min(light->range, isSelected ? light->range : 2.0f);
                float radius = std::tan(light->outerConeAngle) * reach;
                Vec3 center = position + forward * reach, previous = center + right * radius;
                for (int i = 1; i <= 24; i++) {
                    float angle = float(i) * (2.0f * kPi / 24.0f);
                    Vec3 next = center + (right * std::cos(angle) + up * std::sin(angle)) * radius;
                    lines.line(previous, next, color);
                    if (i % 6 == 0) lines.line(position, next, color);
                    previous = next;
                }
            } else if (isSelected) {
                lines.sphere(position, light->range, Vec4{color.x, color.y, color.z, 0.5f});
            }
        }
    }
}

/// A button that stays lit while its tool is the current one.
bool toolButton(const char* label, bool isCurrent, const char* tooltip) {
    if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.48f, 0.18f, 0.85f));
    bool pressed = ImGui::Button(label);
    if (isCurrent) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

void arrangeSelection(EditorContext& editor, int axis, int how);

void drawToolbar(EditorContext& editor) {
    GizmoSettings& gizmo = editor.gizmo;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    if (toolButton("Move", gizmo.operation == GizmoOperation::Move, "Move (W)")) gizmo.operation = GizmoOperation::Move;
    ImGui::SameLine();
    if (toolButton("Rotate", gizmo.operation == GizmoOperation::Rotate, "Rotate (E)")) gizmo.operation = GizmoOperation::Rotate;
    ImGui::SameLine();
    if (toolButton("Scale", gizmo.operation == GizmoOperation::Scale, "Scale (R)")) gizmo.operation = GizmoOperation::Scale;
    ImGui::SameLine();
    if (toolButton("All", gizmo.operation == GizmoOperation::Universal, "Move, rotate and scale at once (T)")) gizmo.operation = GizmoOperation::Universal;

    ImGui::SameLine(0, 14);
    if (ImGui::Button(gizmo.space == GizmoSpace::World ? "World" : "Local")) {
        gizmo.space = gizmo.space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Handles follow the world's axes or the entity's own (X)");
    ImGui::SameLine();
    if (ImGui::Button(gizmo.aroundCenter ? "Center" : "Pivot")) gizmo.aroundCenter = !gizmo.aroundCenter;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("With several selected: the handles at the middle of them all, which they turn and scale around (Center),\nor on the last one chosen (Pivot)");
    }

    ImGui::SameLine(0, 14);
    if (toolButton("Snap", gizmo.snapEnabled, "Move, turn and scale in steps. Holding Cmd while dragging does the opposite.")) {
        gizmo.snapEnabled = !gizmo.snapEnabled;
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##snap-steps", ImGuiDir_Down)) ImGui::OpenPopup("##snap");
    if (ImGui::BeginPopup("##snap")) {
        ImGui::TextDisabled("Steps");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Move", &gizmo.moveSnap, 0.01f, 0.001f, 100.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Rotate", &gizmo.rotateSnap, 0.5f, 0.1f, 180.0f, "%.1f\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        for (float preset : {5.0f, 15.0f, 45.0f, 90.0f}) {
            char text[16];
            std::snprintf(text, sizeof(text), "%.0f", double(preset));
            if (ImGui::SmallButton(text)) gizmo.rotateSnap = preset;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Scale", &gizmo.scaleSnap, 0.005f, 0.001f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Separator();
        ImGui::TextDisabled("Moving snaps to");
        int snapTo = int(gizmo.snapTo);
        ImGui::RadioButton("Steps", &snapTo, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Surfaces", &snapTo, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Corners", &snapTo, 2);
        gizmo.snapTo = GizmoSettings::SnapTo(snapTo);
        ui::tooltip("Corners: the nearest corner of another model. Holding V while moving does it too.");
        if (gizmo.snapTo == GizmoSettings::SnapTo::Surfaces) ImGui::Checkbox("Stands on the surface", &gizmo.standsOnSurface);
        if (editor.scene.selection().size() > 1 || !editor.scene.selection().empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Arrange the selection");
            const char* axes[] = {"X", "Y", "Z"};
            for (int axis = 0; axis < 3; axis++) {
                ImGui::PushID(axis);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(axes[axis]);
                ImGui::SameLine(30);
                if (ImGui::SmallButton("Least")) arrangeSelection(editor, axis, 0);
                ImGui::SameLine();
                if (ImGui::SmallButton("Middle")) arrangeSelection(editor, axis, 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("Most")) arrangeSelection(editor, axis, 2);
                ImGui::SameLine();
                if (ImGui::SmallButton("Spread")) arrangeSelection(editor, axis, 3);
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Drop to the Ground")) arrangeSelection(editor, 1, 4);
            ui::tooltip("Each one down onto the first thing under it");
        }
        ImGui::Separator();
        ImGui::TextDisabled("Shown in the view");
        ImGui::Checkbox("Grid", &gizmo.showGrid);
        ImGui::Checkbox("Selection outline", &gizmo.showSelection);
        ImGui::Checkbox("Colliders of everything", &gShowsAllColliders);
        ImGui::Checkbox("Lights", &gizmo.showLights);
        ImGui::Checkbox("Cameras", &gizmo.showCameras);
        ImGui::Checkbox("Particle effects", &gizmo.showEffects);
        ImGui::Checkbox("Sounds", &gizmo.showSounds);
        ImGui::Checkbox("Splines", &gizmo.showSplines);
        if (!editor.registry.viewportOverlays.empty()) ImGui::Checkbox("From plugins", &gizmo.showPluginOverlays);
        if (ImGui::SmallButton("All")) {
            gizmo.showGrid = gizmo.showSelection = gizmo.showLights = gizmo.showCameras = gizmo.showEffects = gizmo.showSounds = gizmo.showSplines = gizmo.showPluginOverlays = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None")) {
            gizmo.showGrid = gizmo.showSelection = gizmo.showLights = gizmo.showCameras = gizmo.showEffects = gizmo.showSounds = gizmo.showSplines = gizmo.showPluginOverlays = false;
            gShowsAllColliders = false;
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Fly Speed", &gizmo.flySpeed, 0.1f, 0.1f, 500.0f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 14);
    if (toolButton("Colliders", gShowsAllColliders, "Outlines every collider of the scene in green (triggers in yellow). The selected object's, and its children's, are always shown.")) {
        gShowsAllColliders = !gShowsAllColliders;
    }

    ImGui::PopStyleVar();
}

/// Right button: look around and fly. Alt + left: orbit. Middle: pan. Scroll: forward and back.
void updateCamera(EditorContext& editor, bool isHovered, float dt) {
    SceneDocument& scene = editor.scene;
    ImGuiIO& io = ImGui::GetIO();
    static bool isFlying = false, isOrbiting = false, isPanning = false;
    // A drag that began in the view keeps the camera until the button is released, even
    // when the pointer leaves the view.
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) isFlying = true;
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt) isOrbiting = true;
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) isPanning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) isFlying = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) isOrbiting = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) isPanning = false;

    Vec2 mouse{io.MouseDelta.x, io.MouseDelta.y};
    if (isFlying) {
        scene.cameraYaw -= mouse.x * 0.005f;
        scene.cameraPitch = std::clamp(scene.cameraPitch - mouse.y * 0.005f, -1.55f, 1.55f);
        Quat rotation = scene.cameraRotation();
        Vec3 move{0, 0, 0};
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += rotate(rotation, Vec3{0, 0, -1});
        if (ImGui::IsKeyDown(ImGuiKey_S)) move += rotate(rotation, Vec3{0, 0, 1});
        if (ImGui::IsKeyDown(ImGuiKey_A)) move += rotate(rotation, Vec3{-1, 0, 0});
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += rotate(rotation, Vec3{1, 0, 0});
        if (ImGui::IsKeyDown(ImGuiKey_E)) move += Vec3{0, 1, 0};
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move += Vec3{0, -1, 0};
        // The wheel changes the speed while flying, as in other editors.
        if (io.MouseWheel != 0) editor.gizmo.flySpeed = std::clamp(editor.gizmo.flySpeed * (1.0f + io.MouseWheel * 0.1f), 0.1f, 500.0f);
        float speed = editor.gizmo.flySpeed * (io.KeyShift ? 3.0f : 1.0f);
        scene.cameraPosition += move * (speed * dt);
    } else if (isOrbiting) {
        // Turn around the point the camera looks at, keeping the distance.
        Vec3 pivot = scene.cameraPosition + scene.cameraForward() * scene.orbitDistance;
        scene.cameraYaw -= mouse.x * 0.006f;
        scene.cameraPitch = std::clamp(scene.cameraPitch - mouse.y * 0.006f, -1.55f, 1.55f);
        scene.cameraPosition = pivot - scene.cameraForward() * scene.orbitDistance;
    } else if (isPanning) {
        Quat rotation = scene.cameraRotation();
        float scale = scene.orbitDistance * 0.0016f;
        scene.cameraPosition += rotate(rotation, Vec3{-mouse.x * scale, mouse.y * scale, 0});
    } else if (isHovered && io.MouseWheel != 0) {
        float step = std::max(scene.orbitDistance * 0.12f, 0.2f) * io.MouseWheel;
        scene.cameraPosition += scene.cameraForward() * step;
        scene.orbitDistance = std::max(scene.orbitDistance - step, 0.5f);
    }
}

/// The selected entities that no other selected one carries: those that are moved themselves.
std::vector<Entity> topSelected(EditorContext& editor) {
    std::vector<Entity> out;
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    for (Entity entity : document.selection()) {
        const Transform* transform = scene.get<Transform>(entity);
        if (!transform) continue;
        bool carried = false;
        for (Entity other : document.selection()) {
            if (other != entity && document.isDescendant(transform->parent, other)) carried = true;
        }
        if (!carried) out.push_back(entity);
    }
    return out;
}

/// The models of the scene that are not selected (nor under what is): what the selection snaps onto.
std::vector<SnapMesh> snapMeshes(EditorContext& editor, const Ray* along) {
    std::vector<SnapMesh> meshes;
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    for (Entity entity : document.entities()) {
        const Transform* transform = scene.get<Transform>(entity);
        const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        if (!transform || !transform->isActiveInHierarchy || !renderer || !renderer->mesh || !renderer->isVisible) continue;
        bool isMoving = false;
        for (Entity selected : document.selection()) isMoving = isMoving || entity == selected || document.isDescendant(entity, selected);
        if (isMoving) continue;
        if (along) {
            // Only those the ray meets at all, found the quick way first.
            Mat4 toLocal = simd::inverse(transform->world);
            Ray local{transformPoint(toLocal, along->origin), transformDirection(toLocal, along->direction)};
            if (!renderer->mesh->raycast(local)) continue;
        }
        meshes.push_back(SnapMesh{entity, transform->world, &renderer->mesh->positions(), &renderer->mesh->indices()});
    }
    return meshes;
}

/// After the handles moved the selection: the handles' point put onto the surface under the
/// mouse, or onto the nearest corner, and the selection with it.
void snapSelection(EditorContext& editor, ViewportInfo& viewport, GizmoSettings::SnapTo snapTo, const Mat4& handles) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    Vec3 at = xyz(handles.columns[3]);
    std::optional<Vec3> target;
    std::optional<Vec3> normal;
    if (snapTo == GizmoSettings::SnapTo::Surfaces) {
        Ray ray = viewport.screenRay(mouse);
        if (std::optional<SurfaceHit> hit = hitSurface(snapMeshes(editor, &ray), ray)) {
            target = hit->point;
            normal = hit->normal;
        }
    } else {
        Vec2 inView{mouse.x - viewport.position.x, mouse.y - viewport.position.y};
        target = nearestVertex(snapMeshes(editor, nullptr), viewport.projection * viewport.view, Vec2{viewport.size.x, viewport.size.y}, inView);
        if (target && viewport.drawList) {
            Vec4 clip = viewport.projection * viewport.view * Vec4{target->x, target->y, target->z, 1};
            if (clip.w > 0) {
                ImVec2 dot(viewport.position.x + (clip.x / clip.w * 0.5f + 0.5f) * viewport.size.x,
                           viewport.position.y + (0.5f - clip.y / clip.w * 0.5f) * viewport.size.y);
                viewport.drawList->AddCircle(dot, 7, IM_COL32(255, 200, 60, 255), 0, 2);
            }
        }
    }
    if (!target) return;
    Vec3 step = *target - at;
    std::vector<Entity> moved = topSelected(editor);
    for (Entity entity : moved) {
        const Transform* transform = scene.get<Transform>(entity);
        Mat4 world = transform->world;
        world.columns[3] += Vec4{step.x, step.y, step.z, 0};
        // One thing on a surface stands on it.
        if (normal && editor.gizmo.standsOnSurface && moved.size() == 1) {
            Vec3 position, size;
            Quat turned;
            decompose(world, position, turned, size);
            world = compose(position, standOn(*normal, turned), size);
        }
        document.setWorldMatrix(entity, world);
        document.keepPartPlace(entity);
    }
    scene.updateTransforms();
}

/// Lines the selection up, spaces it out, or drops it onto what is under it.
void arrangeSelection(EditorContext& editor, int axis, int how) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    std::vector<Entity> moved = topSelected(editor);
    if (moved.empty()) return;
    std::vector<Vec3> points;
    for (Entity entity : moved) points.push_back(scene.get<Transform>(entity)->worldPosition());
    if (how == 3) distributePoints(points, axis);
    else if (how == 4) {
        // Down onto the first surface below each.
        std::vector<SnapMesh> meshes = snapMeshes(editor, nullptr);
        for (Vec3& point : points) {
            if (std::optional<SurfaceHit> hit = hitSurface(meshes, Ray{point + Vec3{0, 0.01f, 0}, Vec3{0, -1, 0}})) point = hit->point;
        }
    } else {
        alignPoints(points, axis, how == 0 ? AlignTo::Least : how == 1 ? AlignTo::Middle : AlignTo::Most);
    }
    for (size_t i = 0; i < moved.size(); i++) {
        Mat4 world = scene.get<Transform>(moved[i])->world;
        world.columns[3] = Vec4{points[i].x, points[i].y, points[i].z, 1};
        document.setWorldMatrix(moved[i], world);
        document.keepPartPlace(moved[i]);
    }
    scene.updateTransforms();
    document.markChanged();
}

/// The handles on the selection. Returns true while the mouse is on them or dragging them.
bool updateGizmo(EditorContext& editor, ViewportInfo& viewport) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    GizmoSettings& settings = editor.gizmo;
    Entity primary = document.primary();
    Transform* transform = scene.get<Transform>(primary);
    if (!transform) return false;

    ImGuizmo::OPERATION operation = settings.operation == GizmoOperation::Move ? ImGuizmo::TRANSLATE
                                  : settings.operation == GizmoOperation::Rotate ? ImGuizmo::ROTATE
                                  : settings.operation == GizmoOperation::Scale ? ImGuizmo::SCALE : ImGuizmo::UNIVERSAL;
    // Scaling always follows the entity's own axes: that is what Transform::scale means.
    ImGuizmo::MODE mode = settings.space == GizmoSpace::Local || settings.operation == GizmoOperation::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    // Cmd inverts the Snap button for the length of a drag. (ImGui calls the Mac's Command key "Ctrl".)
    bool snap = settings.snapEnabled != ImGui::GetIO().KeyCtrl;
    float step = settings.operation == GizmoOperation::Rotate ? settings.rotateSnap
               : settings.operation == GizmoOperation::Scale ? settings.scaleSnap : settings.moveSnap;
    float snapValues[3] = {step, step, step};

    // With several selected, the handles may stand at the middle of them all: what they turn
    // and scale around. The entities that move with a selected parent are not counted.
    Mat4 before = transform->world;
    if (settings.aroundCenter && document.selection().size() > 1) {
        Vec3 sum{0, 0, 0};
        int count = 0;
        for (Entity entity : document.selection()) {
            if (const Transform* other = scene.get<Transform>(entity)) {
                sum += Vec3{other->world.columns[3].x, other->world.columns[3].y, other->world.columns[3].z};
                count++;
            }
        }
        if (count > 0) {
            Vec3 middle = sum / float(count);
            before.columns[3] = Vec4{middle.x, middle.y, middle.z, 1};
        }
    }
    Mat4 world = before;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(viewport.position.x, viewport.position.y, viewport.size.x, viewport.size.y);
    // Onto surfaces or corners while moving: from the Snap menu, or corners while V is held.
    GizmoSettings::SnapTo snapTo = ImGui::IsKeyDown(ImGuiKey_V) ? GizmoSettings::SnapTo::Corners : settings.snapTo;
    bool snapsToThings = snapTo != GizmoSettings::SnapTo::Steps && settings.operation == GizmoOperation::Move;
    if (snapsToThings) snap = false;
    // "All" shows three tools at once; snapping there would need three kinds of step, so it is left free.
    const float* snapping = snap && settings.operation != GizmoOperation::Universal ? snapValues : nullptr;
    bool moved = ImGuizmo::Manipulate(floats(viewport.view), floats(viewport.projection), operation, mode, floats(world), nullptr, snapping);
    if (moved) {
        // The rest of the selection follows: the same change, applied in the world.
        Mat4 change = world * simd::inverse(before);
        for (Entity entity : document.selection()) {
            const Transform* other = scene.get<Transform>(entity);
            if (!other) continue;
            // An entity under another selected one moves with its parent already.
            bool movesWithParent = false;
            for (Entity candidate : document.selection()) {
                if (candidate != entity && document.isDescendant(other->parent, candidate)) movesWithParent = true;
            }
            if (movesWithParent) continue;
            document.setWorldMatrix(entity, change * other->world);
            document.keepPartPlace(entity);         // a part of a prefab's copy keeps where it was put
        }
        scene.updateTransforms();
        if (snapsToThings) snapSelection(editor, viewport, snapTo, world);
        document.markChanged();
    }
    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

}  // namespace

namespace {

float pixelsPerPoint(Engine& engine) {
    return engine.window() ? engine.window()->contentScale() : float(std::max(engine.config().captureScale, 1));
}

/// Asks the renderer for a picture of the scene the size of a rectangle of the interface,
/// and puts the picture it made last frame there.
/// `pixels`: the size the view is drawn at; zero for as many as it takes on screen.
void showView(Engine& engine, size_t index, ImVec2 position, ImVec2 size, Entity camera, bool showsDebugLines, ImVec2 pixels = ImVec2(0, 0)) {
    Renderer& renderer = engine.renderer();
    if (renderer.views.size() <= index) renderer.views.resize(index + 1);
    float scale = pixelsPerPoint(engine);
    Renderer::SceneView& view = renderer.views[index];
    bool isShown = size.x >= 8 && size.y >= 8;
    view.width = !isShown ? 0 : pixels.x > 0 ? int(pixels.x) : int(std::round(size.x * scale));
    view.height = !isShown ? 0 : pixels.y > 0 ? int(pixels.y) : int(std::round(size.y * scale));
    view.camera = camera;
    view.showsDebugLines = showsDebugLines;
    if (MTL::Texture* picture = renderer.viewTexture(index); picture && view.width > 0) {
        ImGui::GetWindowDrawList()->AddImage(ImTextureID(reinterpret_cast<uintptr_t>(picture)), position, ImVec2(position.x + size.x, position.y + size.y));
    }
}

void hideView(Engine& engine, size_t index) {
    Renderer& renderer = engine.renderer();
    if (renderer.views.size() <= index) renderer.views.resize(index + 1);
    renderer.views[index].width = renderer.views[index].height = 0;
}

constexpr size_t kSceneView = 0, kGameView = 1;

}  // namespace

void hideSceneAndGameViews(EditorContext& editor) {
    hideView(editor.engine, kSceneView);
    hideView(editor.engine, kGameView);
    editor.engine.renderer().canvasView = -1;
    editor.gameViewHasFocus = false;
    editor.engine.input.isListening = true;
    editor.engine.input.setPicture(Vec2{0, 0}, 1.0f);
}

// ---------------------------------------------------------------- the interface, laid out in the Game view

namespace {

/// A piece of the game's interface as the editor handles it: where its component keeps its
/// anchor, its offset and its size, whatever kind of piece it is.
struct InterfacePiece {
    Entity entity;
    Vec2* anchor = nullptr;
    Vec2* offset = nullptr;
    Vec2* size = nullptr;           // a rectangle's
    float* radius = nullptr;        // a round one's
    float* letters = nullptr;       // a text's: the height of its letters
    std::vector<float*> lengths;    // what else grows with it: a dial's thickness, a panel's border
    Vec2 min{0, 0}, max{0, 0};      // where it is, in points of the game's picture
    /// The point it is placed by: its anchor's place on the screen and its offset from there.
    Vec2 place(Vec2 screen) const { return Vec2{screen.x * anchor->x, screen.y * anchor->y} + *offset; }
};

std::vector<InterfacePiece> interfacePieces(EditorContext& editor, Vec2 screen) {
    std::vector<InterfacePiece> pieces;
    Scene& scene = editor.engine.scene;
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (type.category != "UI" || !type.has || !type.get) continue;
        const Field* anchor = type.findField("anchor");
        const Field* offset = type.findField("offset");
        if (!anchor || !offset || anchor->type != FieldType::Vec2 || offset->type != FieldType::Vec2) continue;
        const Field* size = type.findField("size");
        const Field* radius = type.findField("radius");
        const Field* text = type.findField("text");
        const Field* round = type.findField("isRound");             // a Button that is a circle
        std::vector<const Field*> lengths;
        for (const char* name : {"thickness", "border"}) {
            if (const Field* field = type.findField(name); field && field->type == FieldType::Float) lengths.push_back(field);
        }
        for (Entity entity : editor.scene.entities()) {
            void* component = type.get(scene, entity);
            if (!component) continue;
            // Switched off (a menu the game opens later, or something under one): not drawn,
            // so not chosen or moved by a click where it would be.
            const Transform* transform = scene.get<Transform>(entity);
            if (transform && !transform->isActiveInHierarchy) continue;
            InterfacePiece piece;
            piece.entity = entity;
            piece.anchor = static_cast<Vec2*>(anchor->address(component));
            piece.offset = static_cast<Vec2*>(offset->address(component));
            for (const Field* length : lengths) piece.lengths.push_back(static_cast<float*>(length->address(component)));
            Vec2 middle{screen.x * piece.anchor->x + piece.offset->x, screen.y * piece.anchor->y + piece.offset->y};
            bool isRound = round && round->type == FieldType::Bool && *static_cast<bool*>(round->address(component));
            if (size && size->type == FieldType::Vec2 && !isRound) {
                piece.size = static_cast<Vec2*>(size->address(component));
                piece.min = middle - *piece.size * 0.5f;
                piece.max = middle + *piece.size * 0.5f;
            } else if (radius && radius->type == FieldType::Float) {
                piece.radius = static_cast<float*>(radius->address(component));
                piece.min = middle - Vec2{*piece.radius, *piece.radius};
                piece.max = middle + Vec2{*piece.radius, *piece.radius};
            } else if (size && size->type == FieldType::Float) {
                // A text: its place is the top of its first line, at the left, the middle or the right as its anchor says.
                piece.letters = static_cast<float*>(size->address(component));
                float height = *piece.letters * 1.25f;
                float width = 60.0f;
                if (text && text->type == FieldType::String) width = std::max(float(static_cast<std::string*>(text->address(component))->size()) * *piece.letters * 0.52f, 24.0f);
                float left = piece.anchor->x < 0.34f ? middle.x : piece.anchor->x > 0.66f ? middle.x - width : middle.x - width * 0.5f;
                piece.min = Vec2{left, middle.y};
                piece.max = Vec2{left + width, middle.y + height};
            } else {
                piece.min = middle - Vec2{20, 20};
                piece.max = middle + Vec2{20, 20};
            }
            pieces.push_back(piece);
        }
    }
    return pieces;
}

/// Lets the pieces of the interface be chosen, dragged into place and resized where the
/// game is shown: what is seen here is where they are in the game. `at` and `shown` are
/// where the picture is in the window; `screen` is its size in the game's own points.
///
/// Several can be chosen (Shift or Cmd and a click, or a box dragged over them; choosing a
/// parent in the Hierarchy chooses every piece under it): they move together, and the
/// corners of the box around them scale them together, from the opposite corner (from the
/// middle with Alt), each keeping its place among the others. A single rectangle's lower
/// right corner resizes it freely, as before; with Shift, in proportion.
void arrangeInterface(EditorContext& editor, ImVec2 at, ImVec2 shown, Vec2 screen, bool isHovered) {
    // What a piece was when the drag began: scaling starts from it every frame, so nothing drifts.
    struct Before {
        Entity entity;
        Vec2 offset, place, size;
        float radius = 0, letters = 0;
        std::vector<float> lengths;
    };
    enum class Drag { None, Move, Scale, Resize, Box };
    static Drag drag = Drag::None;
    static bool hasMoved = false;
    static std::vector<Before> before;
    static Vec2 pivot{0, 0}, grabbed{0, 0}, boxStart{0, 0};
    static Entity resized{};

    if (screen.x <= 0 || shown.x <= 0) return;
    float zoom = shown.x / screen.x;                    // window points for each point of the game
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    auto toWindow = [&](Vec2 p) { return ImVec2(at.x + p.x * zoom, at.y + p.y * zoom); };
    Vec2 pointer{(io.MousePos.x - at.x) / zoom, (io.MousePos.y - at.y) / zoom};
    std::vector<InterfacePiece> pieces = interfacePieces(editor, screen);
    SceneDocument& document = editor.scene;
    const ImU32 orange = IM_COL32(255, 122, 47, 255);
    drawList->PushClipRect(at, ImVec2(at.x + shown.x, at.y + shown.y), true);

    // Chosen: selected, or under something selected (the group a dial is made of).
    auto isChosen = [&](Entity entity) {
        for (Entity selected : document.selection()) {
            if (selected == entity || document.isDescendant(entity, selected)) return true;
        }
        return false;
    };
    std::vector<const InterfacePiece*> chosen;
    for (const InterfacePiece& piece : pieces) {
        if (isChosen(piece.entity)) chosen.push_back(&piece);
    }
    Vec2 low{0, 0}, high{0, 0};
    if (!chosen.empty()) {
        low = chosen[0]->min, high = chosen[0]->max;
        for (const InterfacePiece* piece : chosen) {
            low = simd::min(low, piece->min);
            high = simd::max(high, piece->max);
        }
    }
    bool isGroup = chosen.size() > 1;
    const InterfacePiece* single = chosen.size() == 1 ? chosen[0] : nullptr;
    bool canScale = !chosen.empty() && std::any_of(chosen.begin(), chosen.end(), [](const InterfacePiece* piece) {
        return piece->size || piece->radius || piece->letters;
    });

    // The handles: the four corners of a group, the lower right one of a single piece.
    Vec2 corners[4] = {low, Vec2{high.x, low.y}, high, Vec2{low.x, high.y}};
    int handle = -1;
    if (canScale) {
        for (int i = 0; i < 4; i++) {
            if (!isGroup && i != 2) continue;
            if (std::abs(pointer.x - corners[i].x) * zoom < 9 && std::abs(pointer.y - corners[i].y) * zoom < 9) handle = i;
        }
    }

    // The one under the pointer: the last drawn is on top.
    const InterfacePiece* under = nullptr;
    for (const InterfacePiece& piece : pieces) {
        if (pointer.x >= piece.min.x && pointer.x <= piece.max.x && pointer.y >= piece.min.y && pointer.y <= piece.max.y) under = &piece;
    }

    auto remember = [&]() {
        before.clear();
        for (const InterfacePiece* piece : chosen) {
            Before was;
            was.entity = piece->entity;
            was.offset = *piece->offset;
            was.place = piece->place(screen);
            if (piece->size) was.size = *piece->size;
            if (piece->radius) was.radius = *piece->radius;
            if (piece->letters) was.letters = *piece->letters;
            for (float* length : piece->lengths) was.lengths.push_back(*length);
            before.push_back(was);
        }
    };

    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        bool adds = io.KeyShift || io.KeySuper || io.KeyCtrl;
        hasMoved = false;
        grabbed = pointer;
        if (handle >= 0 && !(adds && !isGroup && handle != 2)) {
            remember();
            if (single && single->size && !io.KeyShift) {
                drag = Drag::Resize;                    // one rectangle, freely, as it always did
                resized = single->entity;
            } else {
                drag = Drag::Scale;
                pivot = io.KeyAlt ? (low + high) * 0.5f : corners[(handle + 2) % 4];
            }
        } else if (under) {
            if (adds) {
                document.toggleSelection(under->entity);
                drag = Drag::None;
            } else {
                // A piece already among the chosen keeps the others with it.
                if (!isChosen(under->entity)) document.select(under->entity);
                chosen.clear();
                for (const InterfacePiece& piece : pieces) {
                    if (isChosen(piece.entity)) chosen.push_back(&piece);
                }
                remember();
                drag = Drag::Move;
            }
        } else {
            drag = Drag::Box;
            boxStart = pointer;
            if (!adds) document.select(Entity{});
        }
    }

    if (drag != Drag::None) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (drag == Drag::Box) {
                Vec2 a = simd::min(boxStart, pointer), b = simd::max(boxStart, pointer);
                if ((b.x - a.x) * zoom > 3 || (b.y - a.y) * zoom > 3) {
                    for (const InterfacePiece& piece : pieces) {
                        bool touches = piece.max.x >= a.x && piece.min.x <= b.x && piece.max.y >= a.y && piece.min.y <= b.y;
                        if (touches && !document.isSelected(piece.entity)) document.addToSelection(piece.entity);
                    }
                }
            }
            if (hasMoved) document.markChanged();       // one step of undo for the whole drag
            drag = Drag::None;
        } else if (drag != Drag::Box && (pointer.x != grabbed.x || pointer.y != grabbed.y)) {
            hasMoved = true;
            auto pieceOf = [&](Entity entity) -> const InterfacePiece* {
                for (const InterfacePiece& piece : pieces) {
                    if (piece.entity == entity) return &piece;
                }
                return nullptr;
            };
            Vec2 delta = pointer - grabbed;
            if (drag == Drag::Move) {
                for (const Before& was : before) {
                    if (const InterfacePiece* piece = pieceOf(was.entity)) *piece->offset = was.offset + delta;
                }
            } else if (drag == Drag::Resize) {
                const Before* was = before.empty() ? nullptr : &before[0];
                const InterfacePiece* piece = pieceOf(resized);
                if (was && piece && piece->size) {
                    // The corner follows the pointer and the opposite one stays: it grows from its middle by half.
                    Vec2 grown = simd::max(was->size + delta, Vec2{8, 8});
                    *piece->size = grown;
                    *piece->offset = was->offset + (grown - was->size) * 0.5f;
                }
            } else if (drag == Drag::Scale) {
                // One factor for everything: how far the pointer is from the pivot, against where it started.
                Vec2 from = grabbed - pivot;
                float factor = simd::dot(pointer - pivot, from) / std::max(simd::dot(from, from), 1e-3f);
                factor = std::clamp(factor, 0.05f, 50.0f);
                for (const Before& was : before) {
                    const InterfacePiece* piece = pieceOf(was.entity);
                    if (!piece) continue;
                    // Its place moves away from the pivot as everything grows: they keep their places among each other.
                    Vec2 place = pivot + (was.place - pivot) * factor;
                    *piece->offset = was.offset + (place - was.place);
                    if (piece->size) *piece->size = simd::max(was.size * factor, Vec2{4, 4});
                    if (piece->radius) *piece->radius = std::max(was.radius * factor, 2.0f);
                    if (piece->letters) *piece->letters = std::max(was.letters * factor, 4.0f);
                    for (size_t i = 0; i < piece->lengths.size() && i < was.lengths.size(); i++) *piece->lengths[i] = was.lengths[i] * factor;
                }
            }
        }
    }

    // What is drawn over the picture: a faint outline under the pointer; the chosen with their
    // frames, the box around them with its handles, and each one's anchor.
    if (under && !isChosen(under->entity) && isHovered && drag == Drag::None) {
        drawList->AddRect(toWindow(under->min), toWindow(under->max), IM_COL32(255, 255, 255, 110), 0, ImDrawFlags_None, 1.0f);
    }
    for (const InterfacePiece* piece : chosen) {
        drawList->AddRect(toWindow(piece->min), toWindow(piece->max), isGroup ? IM_COL32(255, 122, 47, 150) : orange, 0, ImDrawFlags_None, isGroup ? 1.0f : 1.5f);
        ImVec2 anchor = toWindow(Vec2{screen.x * piece->anchor->x, screen.y * piece->anchor->y});
        if (!isGroup) drawList->AddLine(anchor, toWindow((piece->min + piece->max) * 0.5f), IM_COL32(255, 122, 47, 120), 1.0f);
        drawList->AddLine(ImVec2(anchor.x - 7, anchor.y), ImVec2(anchor.x + 7, anchor.y), orange, 2.0f);
        drawList->AddLine(ImVec2(anchor.x, anchor.y - 7), ImVec2(anchor.x, anchor.y + 7), orange, 2.0f);
    }
    if (isGroup) {
        ImVec2 a = toWindow(low), b = toWindow(high);
        drawList->AddRect(ImVec2(a.x - 4, a.y - 4), ImVec2(b.x + 4, b.y + 4), orange, 0, ImDrawFlags_None, 1.5f);
    }
    if (canScale) {
        for (int i = 0; i < 4; i++) {
            if (!isGroup && i != 2) continue;
            ImVec2 corner = toWindow(corners[i]);
            if (isGroup) corner = ImVec2(corner.x + (i == 1 || i == 2 ? 4.0f : -4.0f), corner.y + (i >= 2 ? 4.0f : -4.0f));
            drawList->AddRectFilled(ImVec2(corner.x - 5, corner.y - 5), ImVec2(corner.x + 5, corner.y + 5), handle == i ? IM_COL32(255, 200, 150, 255) : orange);
        }
        if (handle >= 0 || drag == Drag::Scale || drag == Drag::Resize) {
            ImGui::SetMouseCursor(handle == 0 || handle == 2 || drag == Drag::Resize ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
        }
    }
    if (drag == Drag::Box) {
        ImVec2 a = toWindow(simd::min(boxStart, pointer)), b = toWindow(simd::max(boxStart, pointer));
        drawList->AddRectFilled(a, b, IM_COL32(255, 122, 47, 30));
        drawList->AddRect(a, b, IM_COL32(255, 122, 47, 200));
    }
    if (drag == Drag::Scale && !before.empty()) {
        // How much, while it is being done.
        Vec2 from = grabbed - pivot;
        float factor = std::clamp(simd::dot(pointer - pivot, from) / std::max(simd::dot(from, from), 1e-3f), 0.05f, 50.0f);
        char text[24];
        std::snprintf(text, sizeof(text), "%d%%", int(factor * 100 + 0.5f));
        drawList->AddText(ImVec2(io.MousePos.x + 14, io.MousePos.y + 10), IM_COL32(255, 255, 255, 230), text);
    }
    drawList->PopClipRect();
}

/// Nine places a piece of interface can hold on to: the corners, the sides, the middle.
/// Choosing one leaves the piece where it is and changes what it keeps its distance from.
void drawAnchorChoices(EditorContext& editor, Vec2 screen) {
    std::vector<InterfacePiece> pieces = interfacePieces(editor, screen);
    std::vector<InterfacePiece*> chosen;
    for (InterfacePiece& piece : pieces) {
        bool isChosen = false;
        for (Entity selected : editor.scene.selection()) isChosen = isChosen || selected == piece.entity || editor.scene.isDescendant(piece.entity, selected);
        if (isChosen) chosen.push_back(&piece);
    }
    if (chosen.empty()) return;
    const InterfacePiece* primary = chosen.back();
    ImGui::SameLine(0, 14);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Anchor");
    ImGui::SameLine();
    ImVec2 at = ImGui::GetCursorScreenPos();
    const float cell = 7, gap = 2;
    float side = cell * 3 + gap * 2;
    ImGui::InvisibleButton("##anchors", ImVec2(side, side));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            ImVec2 min(at.x + float(column) * (cell + gap), at.y + float(row) * (cell + gap) + 1), max(min.x + cell, min.y + cell);
            Vec2 anchor{float(column) * 0.5f, float(row) * 0.5f};
            bool isIt = std::abs(primary->anchor->x - anchor.x) < 0.01f && std::abs(primary->anchor->y - anchor.y) < 0.01f;
            bool isOver = ImGui::IsMouseHoveringRect(min, max);
            drawList->AddRectFilled(min, max, isIt ? IM_COL32(255, 122, 47, 255) : isOver ? IM_COL32(200, 205, 215, 255) : IM_COL32(110, 116, 128, 255), 1.5f);
            if (isOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // The same place on the screen, measured from the new anchor: for every chosen piece.
                for (InterfacePiece* piece : chosen) {
                    Vec2 place = piece->place(screen);
                    *piece->anchor = anchor;
                    *piece->offset = place - Vec2{screen.x * anchor.x, screen.y * anchor.y};
                }
                editor.scene.markChanged();
            }
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Which part of the screen the piece holds on to: it keeps its distance from there on every screen");
}

}  // namespace

/// The Game window: the scene through its own camera, as the player will see it, with the
/// game's interface. It shows the scene when editing too; while playing, it has the keyboard
/// when it has the focus.
void drawGameView(EditorContext& editor, bool takesCanvas) {
    Engine& engine = editor.engine;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(kLeastViewSize, ImVec2(FLT_MAX, FLT_MAX));
    // Playing, nothing in it is reached by the keyboard's navigation: the keys steer the game.
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (editor.scene.isPlaying()) flags |= ImGuiWindowFlags_NoNav;
    bool isVisible = ImGui::Begin("Game", nullptr, flags);
    ImGui::PopStyleVar();
    keepViewAtLeast();
    editor.gameViewHasFocus = isVisible && ImGui::IsWindowFocused();
    ImVec2 position = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    Entity camera = editor.scene.gameCamera();
    if (!isVisible || !camera.isValid()) {
        hideView(engine, kGameView);
        engine.renderer().canvasView = -1;
        if (isVisible) {
            ImGui::SetCursorScreenPos(ImVec2(position.x + 14, position.y + 12));
            ImGui::TextDisabled("The scene has no camera to see the game through.");
            ImGui::SetCursorScreenPos(ImVec2(position.x + 14, position.y + 12 + ImGui::GetTextLineHeightWithSpacing()));
            ImGui::TextDisabled("Create > Camera makes one.");
        }
        ImGui::End();
        return;
    }
    // ---- the shape of the screen the game is made for
    struct Shape {
        const char* name;
        float width, height;            // a ratio, or pixels when `isExact`
        bool isExact;
    };
    static const Shape shapes[] = {
        {"Free (fills the window)", 0, 0, false},
        {"16:9  Landscape", 16, 9, false}, {"16:10  Mac", 16, 10, false}, {"4:3  iPad", 4, 3, false}, {"3:2", 3, 2, false}, {"21:9  Ultrawide", 21, 9, false},
        {"1:1  Square", 1, 1, false}, {"9:16  Portrait", 9, 16, false}, {"9:19.5  iPhone portrait", 9, 19.5f, false}, {"19.5:9  iPhone landscape", 19.5f, 9, false},
        {"1280 x 720  HD", 1280, 720, true}, {"1920 x 1080  Full HD", 1920, 1080, true}, {"2560 x 1440  QHD", 2560, 1440, true}, {"3840 x 2160  4K", 3840, 2160, true},
        {"2556 x 1179  iPhone", 2556, 1179, true}, {"2360 x 1640  iPad", 2360, 1640, true},
    };
    static int chosen = 0;
    const float barHeight = ImGui::GetFrameHeight() + 6;
    ImGui::SetCursorScreenPos(ImVec2(position.x + 6, position.y + 3));
    ImGui::SetNextItemWidth(210);
    if (ImGui::BeginCombo("##shape", shapes[chosen].name, ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < int(sizeof(shapes) / sizeof(shapes[0])); i++) {
            if (i == 1) ImGui::SeparatorText("Aspect");
            if (i == 10) ImGui::SeparatorText("Resolution");
            if (ImGui::Selectable(shapes[i].name, i == chosen)) chosen = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("The shape of the screen the game is shown in: the camera and the interface follow it");
    const Shape& shape = shapes[chosen];
    ImVec2 area(position.x, position.y + barHeight), room(size.x, size.y - barHeight);
    ImVec2 shown = room, at = area, pixels(0, 0);
    if (shape.width > 0 && room.x >= 8 && room.y >= 8) {
        float ratio = shape.width / shape.height;
        shown = room.x / room.y > ratio ? ImVec2(std::floor(room.y * ratio), room.y) : ImVec2(room.x, std::floor(room.x / ratio));
        at = ImVec2(area.x + std::floor((room.x - shown.x) * 0.5f), area.y + std::floor((room.y - shown.y) * 0.5f));
        if (shape.isExact) pixels = ImVec2(shape.width, shape.height);
        ImGui::GetWindowDrawList()->AddRectFilled(area, ImVec2(area.x + room.x, area.y + room.y), IM_COL32(12, 13, 16, 255));     // the bars beside it
    }
    showView(engine, kGameView, at, shown, camera, false, pixels);
    const Renderer::SceneView& view = engine.renderer().views[kGameView];
    if (view.width > 0) {
        ImGui::SameLine(0, 10);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d x %d", view.width, view.height);
        float scale = World::interfaceScaleFor(Vec2{float(view.width), float(view.height)} / pixelsPerPoint(engine), engine.interfaceReference, engine.interfaceMatch);
        ImGui::SameLine(0, 10);
        ImGui::TextDisabled("interface at %.0f%%", double(scale * 100));
        ui::tooltip("The game's interface is laid out for 1280 x 720 and drawn larger or smaller on other screens, so it looks the same on all of them");
    }
    // The game's sound, on or off: at the right end of the bar. Only here, in the editor: the
    // game itself is not changed, and its own volume settings go on working under it.
    static bool isMuted = false;
    {
        const char* label = isMuted ? "Muted" : "Sound";
        float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 + 34;
        ImGui::SetCursorScreenPos(ImVec2(position.x + size.x - width - 8, position.y + 3));
        const bool wasMuted = isMuted;         // a click changes it in between: push and pop by what it was
        if (wasMuted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.48f, 0.18f, 0.85f));
        ImVec2 corner = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1, 0.5f));      // the words at the right, the speaker at the left
        if (ImGui::Button((std::string(label) + "##mute").c_str(), ImVec2(width, 0))) isMuted = !isMuted;
        ImGui::PopStyleVar();
        if (wasMuted) ImGui::PopStyleColor();
        ui::tooltip(wasMuted ? "The game is silent while you work: click to hear it again" : "Silence the game while you work (its own settings are not changed)");
        // A little speaker, with its waves or with a cross.
        ImDrawList* ink = ImGui::GetWindowDrawList();
        ImU32 color = IM_COL32(230, 233, 240, 255);
        float cy = corner.y + ImGui::GetFrameHeight() * 0.5f, x = corner.x + 9;
        ink->AddRectFilled(ImVec2(x, cy - 3), ImVec2(x + 4, cy + 3), color);
        ink->AddTriangleFilled(ImVec2(x + 3, cy - 3), ImVec2(x + 9, cy - 7), ImVec2(x + 9, cy + 7), color);
        ink->AddTriangleFilled(ImVec2(x + 3, cy - 3), ImVec2(x + 9, cy + 7), ImVec2(x + 3, cy + 3), color);
        if (wasMuted) {
            ink->AddLine(ImVec2(x + 12, cy - 4), ImVec2(x + 18, cy + 4), color, 1.6f);
            ink->AddLine(ImVec2(x + 18, cy - 4), ImVec2(x + 12, cy + 4), color, 1.6f);
        } else {
            ink->PathArcTo(ImVec2(x + 9, cy), 5.5f, -0.9f, 0.9f, 8);
            ink->PathStroke(color, 0, 1.5f);
            ink->PathArcTo(ImVec2(x + 9, cy), 9.0f, -0.9f, 0.9f, 10);
            ink->PathStroke(color, 0, 1.5f);
        }
    }
    if (engine.hasAudio()) engine.audio().mixer().setMasterVolume(isMuted ? 0.0f : 1.0f);
    engine.renderer().canvasView = takesCanvas ? int(kGameView) : -1;
    // The game's pointer is where the mouse is in this picture, in the picture's own points:
    // a click on a button drawn here is a click on that button.
    Vec2 screen{0, 0};
    if (view.width > 0 && shown.x > 0) {
        screen = Vec2{float(view.width), float(view.height)} / pixelsPerPoint(engine);
        engine.input.setPicture(Vec2{at.x, at.y}, screen.x / shown.x);
    }
    // Not playing, the interface is laid out here: click a piece, drag it, pull its corner.
    if (!editor.scene.isPlaying() && isVisible && screen.x > 0) {
        // In the interface's own points: it is laid out for its reference screen and scaled to this one.
        Vec2 laidOut = screen / World::interfaceScaleFor(screen, engine.interfaceReference, engine.interfaceMatch);
        drawAnchorChoices(editor, laidOut);
        arrangeInterface(editor, at, shown, laidOut, ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(at, ImVec2(at.x + shown.x, at.y + shown.y)));
    }
    // While the game runs, it hears the keyboard, the mouse and the controllers only when this
    // window is the one being used: click in it to play, click anywhere else to work, and
    // what is typed there is not also steering. Not playing, the editor's own tools listen.
    static bool wasPlaying = false;
    bool isPlaying = editor.scene.isPlaying();
    if (isPlaying && !wasPlaying) ImGui::SetWindowFocus();          // Run: the game has the keys from the start
    wasPlaying = isPlaying;
    engine.input.isListening = !isPlaying || editor.gameViewHasFocus;
    if (isPlaying && !editor.gameViewHasFocus && isVisible) {
        const char* note = "Click here to play";
        ImVec2 size = ImGui::CalcTextSize(note);
        ImVec2 corner(at.x + (shown.x - size.x) * 0.5f, at.y + 10);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(corner.x - 10, corner.y - 5), ImVec2(corner.x + size.x + 10, corner.y + size.y + 5), IM_COL32(20, 22, 27, 210), 6.0f);
        ImGui::GetWindowDrawList()->AddText(corner, IM_COL32(235, 238, 244, 255), note);
    }
    // A line around it while the keys go to the game.
    if (editor.scene.isPlaying() && editor.gameViewHasFocus) {
        ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + shown.x, at.y + shown.y), IM_COL32(255, 122, 47, 255), 0, ImDrawFlags_None, 2.0f);
    }
    ImGui::End();
}

/// The Scene window: the scene through the editor's own camera, where things are selected,
/// moved and looked at from anywhere, whether the game is running or not.
// ---------------------------------------------------------------- shaping a terrain

namespace {

struct TerrainTool {
    int brush = -1;                     // a kit::TerrainBrush, or -1: the brushes are put away
    float radius = 8.0f;
    float strength = 4.0f;              // meters a second (raise, lower); for smooth and flatten, how fast
    bool isStroking = false;
    float level = 0;                    // where Flatten brings the ground: the height where the stroke began
    Entity terrain{};
    std::vector<std::vector<float>> undo;       // the heights before each stroke
};

TerrainTool gTerrainTool;

/// The one Terrain chosen, if exactly one is.
Entity chosenTerrain(EditorContext& editor) {
    const auto& chosen = editor.scene.selection();
    if (chosen.size() != 1 || !editor.engine.scene.has<kit::Terrain>(chosen[0])) return Entity{};
    return chosen[0];
}

/// The brushes, in the lower left of the view, while a Terrain is chosen. True if the pointer is on them.
bool drawTerrainBrushes(EditorContext& editor, ViewportInfo& viewport) {
    Entity entity = chosenTerrain(editor);
    if (entity != gTerrainTool.terrain) {
        gTerrainTool.terrain = entity;
        gTerrainTool.undo.clear();
        gTerrainTool.isStroking = false;
    }
    if (!entity.isValid() || editor.scene.isPlaying()) return false;
    ImGui::SetCursorScreenPos(ImVec2(viewport.position.x + 8, viewport.position.y + viewport.size.y - 112));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
    ImGui::BeginChild("##terrain brushes", ImVec2(420, 104), ImGuiChildFlags_Borders);
    const char* names[] = {"Raise", "Lower", "Smooth", "Flatten"};
    if (ImGui::RadioButton("Move it", gTerrainTool.brush < 0)) gTerrainTool.brush = -1;
    for (int k = 0; k < 4; k++) {
        ImGui::SameLine();
        if (ImGui::RadioButton(names[k], gTerrainTool.brush == k)) gTerrainTool.brush = k;
    }
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Size", &gTerrainTool.radius, 0.5f, 100.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Strength", &gTerrainTool.strength, 0.1f, 40.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    if (gTerrainTool.undo.empty()) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Undo Stroke") && !gTerrainTool.undo.empty()) {
        if (kit::Terrain* terrain = editor.engine.scene.get<kit::Terrain>(entity)) {
            terrain->samples = gTerrainTool.undo.back();
            terrain->needsRebuild = true;
            terrain->save(editor.engine);
        }
        gTerrainTool.undo.pop_back();
    }
    if (gTerrainTool.undo.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(gTerrainTool.brush < 0 ? "Choose a brush, then paint on the ground" : "Shift: the other way round. Alt: turn the view");
    bool isOn = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsAnyItemActive();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return isOn;
}

/// Paints with the brush chosen where the pointer is on the ground. True while it takes the mouse.
bool shapeTerrain(EditorContext& editor, ViewportInfo& viewport, bool isOnBrushes) {
    Entity entity = gTerrainTool.terrain;
    kit::Terrain* terrain = editor.engine.scene.get<kit::Terrain>(entity);
    if (!terrain || gTerrainTool.brush < 0 || editor.scene.isPlaying()) return false;
    ImGuiIO& io = ImGui::GetIO();
    if (gTerrainTool.isStroking && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // The stroke is over: the heights go to their file (made the first time), so that the
        // scene and the game read them.
        gTerrainTool.isStroking = false;
        if (terrain->heights.empty()) {
            const Name* name = editor.engine.scene.get<Name>(entity);
            terrain->heights = editor.assets.uniquePath("Terrains", name && !name->value.empty() ? name->value : std::string("Terrain"), ".terrain");
            terrain->loadedFrom = terrain->heights;
            editor.scene.markChanged();
        }
        if (terrain->save(editor.engine)) editor.assets.refresh();
        return true;
    }
    if (!viewport.isHovered || isOnBrushes || io.KeyAlt || ImGui::IsMouseDown(ImGuiMouseButton_Right)) return gTerrainTool.isStroking;
    Ray ray = viewport.screenRay(io.MousePos);
    std::optional<Vec3> hit = terrain->raycast(editor.engine, entity, ray.origin, ray.direction);
    if (!hit) return gTerrainTool.isStroking;
    const Transform* transform = editor.engine.scene.get<Transform>(entity);
    Mat4 toLocal = simd::inverse(transform->world);
    Vec3 local = xyz(toLocal * Vec4{hit->x, hit->y, hit->z, 1});
    // The brush, as a ring lying on the ground.
    DebugDraw& lines = editor.engine.debug();
    Vec4 color{1.0f, 0.55f, 0.2f, 1};
    Vec3 previous{};
    for (int k = 0; k <= 48; k++) {
        float angle = float(k) / 48.0f * 6.2831853f;
        Vec2 around{local.x + std::cos(angle) * gTerrainTool.radius, local.z + std::sin(angle) * gTerrainTool.radius};
        Vec3 point{around.x, terrain->heightAt(around) + 0.08f, around.y};
        point = xyz(transform->world * Vec4{point.x, point.y, point.z, 1});
        if (k > 0) lines.line(previous, point, color);
        previous = point;
    }
    lines.line(*hit, *hit + Vec3{0, gTerrainTool.radius * 0.3f, 0}, color);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        gTerrainTool.isStroking = true;
        gTerrainTool.level = local.y;
        gTerrainTool.undo.push_back(terrain->samples);
        if (gTerrainTool.undo.size() > 30) gTerrainTool.undo.erase(gTerrainTool.undo.begin());
    }
    if (gTerrainTool.isStroking) {
        auto brush = kit::TerrainBrush(gTerrainTool.brush);
        if (io.KeyShift && brush == kit::TerrainBrush::Raise) brush = kit::TerrainBrush::Lower;
        else if (io.KeyShift && brush == kit::TerrainBrush::Lower) brush = kit::TerrainBrush::Raise;
        float dt = std::min(io.DeltaTime, 0.05f);
        bool isShare = brush == kit::TerrainBrush::Smooth || brush == kit::TerrainBrush::Flatten;
        float amount = isShare ? std::min(gTerrainTool.strength * 0.25f * dt * 4, 1.0f) : gTerrainTool.strength * dt;
        terrain->sculpt(Vec2{local.x, local.z}, gTerrainTool.radius, amount, brush, gTerrainTool.level);
        terrain->rebuild(editor.engine, entity);
    }
    return true;
}

// ---------------------------------------------------------------- painting foliage

struct FoliageTool {
    int brush = -1;                     // 0 paint, 1 erase; -1: put away
    float radius = 4.0f;
    bool isStroking = false;
    Entity foliage{};
    std::vector<std::vector<kit::FoliageCopy>> undo;
};

FoliageTool gFoliageTool;

/// The first ground a ray meets: a Terrain's, or anything solid.
std::optional<Vec3> groundAlong(EditorContext& editor, const Ray& ray, Entity ignored) {
    std::optional<Vec3> best;
    float nearest = 1e30f;
    editor.engine.scene.each<kit::Terrain>([&](Entity entity, kit::Terrain& terrain) {
        if (std::optional<Vec3> hit = terrain.raycast(editor.engine, entity, ray.origin, ray.direction, 5000)) {
            float distance = simd::length(*hit - ray.origin);
            if (distance < nearest) {
                nearest = distance;
                best = hit;
            }
        }
    });
    if (auto hit = editor.engine.physics.raycast(editor.engine.scene, ray, std::min(nearest, 5000.0f), 0xFFFFFFFFu, ignored)) {
        if (hit->distance < nearest) best = hit->point;
    }
    return best;
}

bool drawFoliageBrushes(EditorContext& editor, ViewportInfo& viewport) {
    const auto& chosen = editor.scene.selection();
    Entity entity = chosen.size() == 1 && editor.engine.scene.has<kit::Foliage>(chosen[0]) ? chosen[0] : Entity{};
    if (entity != gFoliageTool.foliage) {
        gFoliageTool.foliage = entity;
        gFoliageTool.undo.clear();
        gFoliageTool.isStroking = false;
    }
    if (!entity.isValid() || editor.scene.isPlaying()) return false;
    kit::Foliage* foliage = editor.engine.scene.get<kit::Foliage>(entity);
    ImGui::SetCursorScreenPos(ImVec2(viewport.position.x + 8, viewport.position.y + viewport.size.y - 112));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
    ImGui::BeginChild("##foliage brushes", ImVec2(420, 104), ImGuiChildFlags_Borders);
    if (ImGui::RadioButton("Move it", gFoliageTool.brush < 0)) gFoliageTool.brush = -1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Paint", gFoliageTool.brush == 0)) gFoliageTool.brush = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Erase", gFoliageTool.brush == 1)) gFoliageTool.brush = 1;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu copies", foliage->copies.size());
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Size", &gFoliageTool.radius, 0.5f, 60.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Density", &foliage->density, 0.01f, 20.0f, "%.2f / m2", ImGuiSliderFlags_Logarithmic);
    if (gFoliageTool.undo.empty()) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Undo Stroke") && !gFoliageTool.undo.empty()) {
        foliage->copies = gFoliageTool.undo.back();
        foliage->needsShowing = true;
        foliage->save(editor.engine);
        gFoliageTool.undo.pop_back();
    }
    if (gFoliageTool.undo.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(gFoliageTool.brush < 0 ? "Choose Paint, then paint on the ground" : "Shift: erase. Alt: turn the view");
    bool isOn = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsAnyItemActive();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return isOn;
}

bool paintFoliage(EditorContext& editor, ViewportInfo& viewport, bool isOnBrushes) {
    Entity entity = gFoliageTool.foliage;
    kit::Foliage* foliage = editor.engine.scene.get<kit::Foliage>(entity);
    const Transform* transform = editor.engine.scene.get<Transform>(entity);
    if (!foliage || !transform || gFoliageTool.brush < 0 || editor.scene.isPlaying()) return false;
    ImGuiIO& io = ImGui::GetIO();
    if (gFoliageTool.isStroking && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        gFoliageTool.isStroking = false;
        if (foliage->places.empty()) {
            const Name* name = editor.engine.scene.get<Name>(entity);
            foliage->places = editor.assets.uniquePath("Foliage", name && !name->value.empty() ? name->value : std::string("Foliage"), ".foliage");
            foliage->loadedFrom = foliage->places;
            editor.scene.markChanged();
        }
        if (foliage->save(editor.engine)) editor.assets.refresh();
        return true;
    }
    if (!viewport.isHovered || isOnBrushes || io.KeyAlt || ImGui::IsMouseDown(ImGuiMouseButton_Right)) return gFoliageTool.isStroking;
    std::optional<Vec3> hit = groundAlong(editor, viewport.screenRay(io.MousePos), entity);
    if (!hit) return gFoliageTool.isStroking;
    Mat4 toLocal = simd::inverse(transform->world);
    Vec3 local = xyz(toLocal * Vec4{hit->x, hit->y, hit->z, 1});
    // The brush: a ring on the ground.
    DebugDraw& lines = editor.engine.debug();
    bool erases = gFoliageTool.brush == 1 || io.KeyShift;
    Vec4 color = erases ? Vec4{1.0f, 0.35f, 0.3f, 1} : Vec4{0.45f, 0.9f, 0.45f, 1};
    Vec3 previous{};
    for (int k = 0; k <= 40; k++) {
        float angle = float(k) / 40.0f * 6.2831853f;
        Vec3 point = *hit + Vec3{std::cos(angle) * gFoliageTool.radius, 0.1f, std::sin(angle) * gFoliageTool.radius};
        if (k > 0) lines.line(previous, point, color);
        previous = point;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        gFoliageTool.isStroking = true;
        gFoliageTool.undo.push_back(foliage->copies);
        if (gFoliageTool.undo.size() > 30) gFoliageTool.undo.erase(gFoliageTool.undo.begin());
    }
    if (!gFoliageTool.isStroking) return true;
    if (erases) {
        foliage->erase(Vec2{local.x, local.z}, gFoliageTool.radius);
    } else {
        // The ground under each place, straight down from above it.
        Mat4 world = transform->world;
        auto ground = [&](Vec2 at) -> std::optional<Vec3> {
            Vec3 above = xyz(world * Vec4{at.x, local.y + 50, at.y, 1});
            std::optional<Vec3> under = groundAlong(editor, Ray{above, Vec3{0, -1, 0}}, entity);
            if (!under) return std::nullopt;
            return xyz(toLocal * Vec4{under->x, under->y, under->z, 1});
        };
        // A share of the density each frame: a second's painting fills it.
        foliage->paint(Vec2{local.x, local.z}, gFoliageTool.radius, ground, std::min(io.DeltaTime * 2.0f, 1.0f));
    }
    if (foliage->needsShowing) foliage->show(editor.engine, entity);
    return true;
}

}  // namespace

void drawSceneView(EditorContext& editor) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    Engine& engine = editor.engine;
    ViewportInfo& viewport = editor.viewport;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(kLeastViewSize, ImVec2(FLT_MAX, FLT_MAX));
    bool isVisible = ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    keepViewAtLeast();
    ImVec2 position = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    viewport.position = position;
    viewport.size = size;
    if (!isVisible || size.x < 8 || size.y < 8) {
        hideView(engine, kSceneView);
        ImGui::End();
        return;
    }
    showView(engine, kSceneView, position, size, document.cameraEntity(), true);
    // The tools start a little in from the corner.
    ImGui::SetCursorScreenPos(ImVec2(position.x + 8, position.y + 8));
    viewport.drawList = ImGui::GetWindowDrawList();

    drawToolbar(editor);
    bool isOnToolbar = ImGui::IsAnyItemHovered();
    // Hovered also while one of its own popups (the snap steps) is not in the way.
    viewport.isHovered = ImGui::IsWindowHovered() && !isOnToolbar;

    if (editor.alignWithViewRequested) {
        document.alignSelectionWithView();
        editor.alignWithViewRequested = false;
    }
    if (editor.frameSelectionRequested) {
        document.frameSelection();
        editor.frameSelectionRequested = false;
    }
    updateCamera(editor, viewport.isHovered, engine.time.unscaledDelta);

    // The camera as matrices: what ImGuizmo, picking and the overlays all work from.
    const Camera* camera = scene.get<Camera>(document.cameraEntity());
    viewport.cameraPosition = document.cameraPosition;
    viewport.view = simd::inverse(compose(document.cameraPosition, document.cameraRotation(), Vec3{1, 1, 1}));
    viewport.projection = camera ? camera->projection(size.x / size.y) : perspective(radians(60.0f), size.x / size.y, 0.1f, 2000.0f);

    if (editor.gizmo.showGrid) drawGrid(editor);
    if (editor.gizmo.showSelection) {
        for (Entity entity : document.selection()) drawOutline(editor, entity);
    }
    drawShapes(editor);                 // each kind as the Snap menu says
    drawIcons(editor, viewport);
    if (editor.gizmo.showPluginOverlays) {
        for (auto& overlay : editor.registry.viewportOverlays) overlay.value(editor, viewport);
    }

    // A Terrain chosen with a brush out: the mouse shapes the ground instead of moving things.
    bool isOnBrushes = drawTerrainBrushes(editor, viewport) || drawFoliageBrushes(editor, viewport);
    bool isShaping = shapeTerrain(editor, viewport, isOnBrushes) || paintFoliage(editor, viewport, isOnBrushes);
    bool brushIsOut = (gTerrainTool.brush >= 0 && gTerrainTool.terrain.isValid()) || (gFoliageTool.brush >= 0 && gFoliageTool.foliage.isValid());
    bool isOnGizmo = isShaping || isOnBrushes || brushIsOut ? false : updateGizmo(editor, viewport);

    // The cube in the corner: shows which way the camera faces; drag it to turn, click a face to look along an axis.
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(position.x, position.y, size.x, size.y);
    Mat4 view = viewport.view;
    ImVec2 cubeSize(96, 96);
    ImVec2 cubePosition(position.x + size.x - cubeSize.x - 8, position.y + 8);
    Mat4 unused = identity();           // this form also tells ImGuizmo the projection, which it needs even with nothing selected
    ImGuizmo::ViewManipulate(floats(view), floats(viewport.projection), ImGuizmo::TRANSLATE, ImGuizmo::WORLD, floats(unused),
                             document.orbitDistance, cubePosition, cubeSize, IM_COL32(0, 0, 0, 0));
    bool isOnCube = ImGuizmo::IsViewManipulateHovered() || ImGuizmo::IsUsingViewManipulate();
    if (!simd::almost_equal_elements(view, viewport.view, 1e-5f)) {
        // Read the new direction back into the yaw and pitch the camera is kept as.
        Mat4 cameraWorld = simd::inverse(view);
        Vec3 forward = safeNormalize(-xyz(cameraWorld.columns[2]), Vec3{0, 0, -1});
        document.cameraPitch = std::clamp(std::asin(std::clamp(forward.y, -1.0f, 1.0f)), -1.55f, 1.55f);
        if (std::abs(forward.x) > 1e-5f || std::abs(forward.z) > 1e-5f) document.cameraYaw = std::atan2(-forward.x, -forward.z);
        document.cameraPosition = xyz(cameraWorld.columns[3]);
    }

    // Tools on the keys Unity and Blender users expect, unless something is being typed or the camera is flying.
    ImGuiIO& io = ImGui::GetIO();
    bool keysFree = !io.WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.KeyCtrl && !io.KeySuper;
    if (keysFree && (viewport.isHovered || ImGui::IsWindowFocused())) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) editor.gizmo.operation = GizmoOperation::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) editor.gizmo.operation = GizmoOperation::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) editor.gizmo.operation = GizmoOperation::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_T, false)) editor.gizmo.operation = GizmoOperation::Universal;
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) editor.gizmo.space = editor.gizmo.space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) editor.frameSelectionRequested = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) document.clearSelection();
    }

    // A click that is not on a handle selects what is under it.
    if (viewport.isHovered && !isOnGizmo && !isOnCube && !isShaping && !isOnBrushes && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Entity hit = pick(editor, viewport.screenRay(io.MousePos));
        // The first click on a thing made of parts takes the whole thing; a click on a part
        // of what is already selected takes the part.
        if (hit.isValid()) {
            Entity top = hit;
            for (int depth = 0; depth < 32; depth++) {
                const Transform* transform = scene.get<Transform>(top);
                if (!transform || !transform->parent.isValid() || !scene.get<Transform>(transform->parent)) break;
                top = transform->parent;
            }
            bool isInsideSelection = false;
            for (Entity selected : document.selection()) {
                for (Entity up = hit; up.isValid() && !isInsideSelection;) {
                    isInsideSelection = up == selected;
                    const Transform* transform = scene.get<Transform>(up);
                    up = transform ? transform->parent : Entity{};
                }
            }
            if (!isInsideSelection) hit = top;
        }
        if (io.KeyCtrl || io.KeyShift) {
            if (hit.isValid()) document.toggleSelection(hit);
        } else {
            document.select(hit);
        }
    }

    // Things dragged in from the asset browser land where the pointer is on the ground.
    ImGui::SetCursorScreenPos(position);
    ImGui::Dummy(size);
    // A material shows on whatever it is held over, before it is let go: what it was wearing comes back when the pointer moves on.
    static Entity triedOn;
    static std::shared_ptr<Material> wornBefore;
    if (triedOn.isValid()) {
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(triedOn)) renderer->material = wornBefore;
        triedOn = Entity{};
        wornBefore.reset();
    }
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* held = ImGui::GetDragDropPayload();
        if (held && held->IsDataType(ui::kAssetPayload)) {
            std::string path(static_cast<const char*>(held->Data));
            const AssetInfo* asset = editor.assets.find(path);
            if (asset && asset->type == AssetType::Material) {
                Entity hit = pick(editor, viewport.screenRay(io.MousePos));
                MeshRenderer* renderer = scene.get<MeshRenderer>(hit);
                std::shared_ptr<Material> material = renderer ? engine.assets().material(path) : nullptr;
                if (renderer && material) {
                    triedOn = hit;
                    wornBefore = renderer->material;
                    renderer->material = material;
                    ImGui::SetTooltip("%s", document.nameOf(hit).c_str());
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kAssetPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            std::string path(static_cast<const char*>(payload->Data));
            const AssetInfo* asset = editor.assets.find(path);
            if (asset && asset->type == AssetType::Mesh && std::filesystem::path(path).extension() == ".obj") {
                Vec3 point = groundPoint(viewport.screenRay(io.MousePos));
                if (!document.createFromMesh(path, point).isValid()) editor.notify("Cannot load " + path, ConsoleMessage::Level::Error);
            } else if (asset && asset->type == AssetType::Material) {
                // Onto whatever is under the pointer.
                Entity hit = pick(editor, viewport.screenRay(io.MousePos));
                MeshRenderer* renderer = scene.get<MeshRenderer>(hit);
                std::shared_ptr<Material> material = engine.assets().material(path);
                if (renderer && material) {
                    renderer->material = material;
                    triedOn = Entity{};                 // it keeps it
                    wornBefore.reset();
                    document.select(hit);
                    document.markChanged();
                } else if (!renderer) {
                    editor.notify("Drop a material onto something with a mesh", ConsoleMessage::Level::Warning);
                }
            } else if (asset && asset->type == AssetType::Prefab) {
                Vec3 point = groundPoint(viewport.screenRay(io.MousePos));
                if (!document.instantiatePrefab(path, point).isValid()) editor.notify("Cannot read " + path, ConsoleMessage::Level::Error);
            } else if (asset && asset->type == AssetType::Scene) {
                editor.requestOpenScene(editor.assets.absolutePath(path));
            } else if (asset && asset->type == AssetType::Mesh) {
                editor.notify("Only Wavefront OBJ models can be imported for now", ConsoleMessage::Level::Warning);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

}  // namespace molten::editor
