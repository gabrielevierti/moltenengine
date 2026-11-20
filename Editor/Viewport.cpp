// The 3D view: the editor's camera, clicking to select, the gizmo that moves, turns and
// scales the selection (with snapping), and the lines and icons drawn over the scene.
#include "Editor.hpp"
#include "Rendering/Particles.hpp"
#include "Panels/Widgets.hpp"

#include <ImGuizmo.h>

#include <algorithm>
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
            hit = intersectSphere(ray, transform->worldPosition(), 0.35f);     // lights and cameras: their icon
        }
        if (hit && *hit < bestDistance) {
            bestDistance = *hit;
            best = entity;
        }
    }
    return best;
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
    // The grid follows the camera in whole steps, so it always reaches the horizon of interest.
    const int half = 40;
    Vec3 center{std::round(editor.scene.cameraPosition.x), 0, std::round(editor.scene.cameraPosition.z)};
    for (int i = -half; i <= half; i++) {
        int x = int(center.x) + i, z = int(center.z) + i;
        auto colorOf = [](int coordinate) {
            if (coordinate == 0) return Vec4{0.55f, 0.55f, 0.55f, 1};
            return coordinate % 10 == 0 ? Vec4{0.32f, 0.34f, 0.38f, 1} : Vec4{0.19f, 0.2f, 0.23f, 1};
        };
        lines.line(Vec3{float(x), 0.002f, center.z - half}, Vec3{float(x), 0.002f, center.z + half}, colorOf(x));
        lines.line(Vec3{center.x - half, 0.002f, float(z)}, Vec3{center.x + half, 0.002f, float(z)}, colorOf(z));
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
    if (const Collider* collider = scene.get<Collider>(entity)) {
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
        const Light* light = scene.get<Light>(entity);
        const char* letter = light ? "L" : scene.has<Camera>(entity) ? "C" : scene.has<ParticleEmitter>(entity) ? "P" : nullptr;
        ImVec2 at;
        if (!letter || !viewport.worldToScreen(scene.get<Transform>(entity)->worldPosition(), at)) continue;
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

        if (const Camera* camera = scene.get<Camera>(entity)) {
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
        if (const Light* light = scene.get<Light>(entity)) {
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
        ImGui::Checkbox("Grid", &gizmo.showGrid);
        ImGui::Checkbox("Icons", &gizmo.showIcons);
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

    Mat4 before = transform->world;
    Mat4 world = before;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(viewport.position.x, viewport.position.y, viewport.size.x, viewport.size.y);
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
            document.setWorldMatrix(entity, entity == primary ? world : change * other->world);
        }
        scene.updateTransforms();
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
    Vec2 min{0, 0}, max{0, 0};      // where it is, in points of the game's picture
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
        for (Entity entity : editor.scene.entities()) {
            void* component = type.get(scene, entity);
            if (!component) continue;
            InterfacePiece piece;
            piece.entity = entity;
            piece.anchor = static_cast<Vec2*>(anchor->address(component));
            piece.offset = static_cast<Vec2*>(offset->address(component));
            Vec2 middle{screen.x * piece.anchor->x + piece.offset->x, screen.y * piece.anchor->y + piece.offset->y};
            if (size && size->type == FieldType::Vec2) {
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
void arrangeInterface(EditorContext& editor, ImVec2 at, ImVec2 shown, Vec2 screen, bool isHovered) {
    static Entity dragged{};
    static bool isResizing = false, hasMoved = false;
    if (screen.x <= 0 || shown.x <= 0) return;
    float zoom = shown.x / screen.x;                    // window points for each point of the game
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    auto toWindow = [&](Vec2 p) { return ImVec2(at.x + p.x * zoom, at.y + p.y * zoom); };
    Vec2 pointer{(io.MousePos.x - at.x) / zoom, (io.MousePos.y - at.y) / zoom};
    std::vector<InterfacePiece> pieces = interfacePieces(editor, screen);
    const ImU32 orange = IM_COL32(255, 122, 47, 255);
    drawList->PushClipRect(at, ImVec2(at.x + shown.x, at.y + shown.y), true);

    // The one under the pointer: the last drawn is on top.
    const InterfacePiece* under = nullptr;
    for (const InterfacePiece& piece : pieces) {
        if (pointer.x >= piece.min.x && pointer.x <= piece.max.x && pointer.y >= piece.min.y && pointer.y <= piece.max.y) under = &piece;
    }
    const InterfacePiece* chosen = nullptr;
    for (const InterfacePiece& piece : pieces) {
        if (editor.scene.isSelected(piece.entity)) chosen = &piece;
    }
    bool onHandle = chosen && (chosen->size || chosen->radius || chosen->letters) && std::abs(pointer.x - chosen->max.x) * zoom < 9 && std::abs(pointer.y - chosen->max.y) * zoom < 9;

    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (onHandle) {
            dragged = chosen->entity;
            isResizing = true;
        } else if (under) {
            editor.scene.select(under->entity);
            editor.selectedAsset.clear();
            dragged = under->entity;
            isResizing = false;
        }
        hasMoved = false;
    }
    if (dragged.isValid()) {
        const InterfacePiece* piece = nullptr;
        for (const InterfacePiece& candidate : pieces) {
            if (candidate.entity == dragged) piece = &candidate;
        }
        if (!piece || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (hasMoved) editor.scene.markChanged();        // one step of undo for the whole drag
            dragged = Entity{};
        } else if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
            Vec2 delta{io.MouseDelta.x / zoom, io.MouseDelta.y / zoom};
            hasMoved = true;
            if (!isResizing) {
                *piece->offset += delta;
            } else if (piece->size) {
                // The corner follows the pointer and the opposite one stays: it grows from its middle by half.
                *piece->size = Vec2{std::max(piece->size->x + delta.x, 8.0f), std::max(piece->size->y + delta.y, 8.0f)};
                *piece->offset += delta * 0.5f;
            } else if (piece->radius) {
                *piece->radius = std::max(*piece->radius + (delta.x + delta.y) * 0.5f, 8.0f);
            } else if (piece->letters) {
                *piece->letters = std::max(*piece->letters + delta.y, 6.0f);
            }
        }
    }

    // What is drawn over the picture: a faint outline under the pointer, and the chosen one's frame, handle and anchor.
    if (under && under != chosen && isHovered && !dragged.isValid()) drawList->AddRect(toWindow(under->min), toWindow(under->max), IM_COL32(255, 255, 255, 110), 0, ImDrawFlags_None, 1.0f);
    if (chosen) {
        drawList->AddRect(toWindow(chosen->min), toWindow(chosen->max), orange, 0, ImDrawFlags_None, 1.5f);
        if (chosen->size || chosen->radius || chosen->letters) {
            ImVec2 corner = toWindow(chosen->max);
            drawList->AddRectFilled(ImVec2(corner.x - 5, corner.y - 5), ImVec2(corner.x + 5, corner.y + 5), orange);
            if (onHandle) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        }
        // Its anchor: the point of the screen it keeps its distance from, and the line to it.
        ImVec2 anchor = toWindow(Vec2{screen.x * chosen->anchor->x, screen.y * chosen->anchor->y});
        ImVec2 middle = toWindow((chosen->min + chosen->max) * 0.5f);
        drawList->AddLine(anchor, middle, IM_COL32(255, 122, 47, 120), 1.0f);
        drawList->AddLine(ImVec2(anchor.x - 7, anchor.y), ImVec2(anchor.x + 7, anchor.y), orange, 2.0f);
        drawList->AddLine(ImVec2(anchor.x, anchor.y - 7), ImVec2(anchor.x, anchor.y + 7), orange, 2.0f);
    }
    drawList->PopClipRect();
}

/// Nine places a piece of interface can hold on to: the corners, the sides, the middle.
/// Choosing one leaves the piece where it is and changes what it keeps its distance from.
void drawAnchorChoices(EditorContext& editor, Vec2 screen) {
    std::vector<InterfacePiece> pieces = interfacePieces(editor, screen);
    const InterfacePiece* chosen = nullptr;
    for (const InterfacePiece& piece : pieces) {
        if (editor.scene.isSelected(piece.entity)) chosen = &piece;
    }
    if (!chosen) return;
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
            bool isIt = std::abs(chosen->anchor->x - anchor.x) < 0.01f && std::abs(chosen->anchor->y - anchor.y) < 0.01f;
            bool isOver = ImGui::IsMouseHoveringRect(min, max);
            drawList->AddRectFilled(min, max, isIt ? IM_COL32(255, 122, 47, 255) : isOver ? IM_COL32(200, 205, 215, 255) : IM_COL32(110, 116, 128, 255), 1.5f);
            if (isOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // The same place on the screen, measured from the new anchor.
                Vec2 place{screen.x * chosen->anchor->x + chosen->offset->x, screen.y * chosen->anchor->y + chosen->offset->y};
                *chosen->anchor = anchor;
                *chosen->offset = place - Vec2{screen.x * anchor.x, screen.y * anchor.y};
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
    bool isVisible = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
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
    }
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
        drawAnchorChoices(editor, screen);
        arrangeInterface(editor, at, shown, screen, ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(at, ImVec2(at.x + shown.x, at.y + shown.y)));
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
void drawSceneView(EditorContext& editor) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    Engine& engine = editor.engine;
    ViewportInfo& viewport = editor.viewport;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool isVisible = ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
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
    for (Entity entity : document.selection()) drawOutline(editor, entity);
    if (editor.gizmo.showIcons) drawShapes(editor);
    if (editor.gizmo.showIcons) drawIcons(editor, viewport);
    for (auto& overlay : editor.registry.viewportOverlays) overlay.value(editor, viewport);

    bool isOnGizmo = updateGizmo(editor, viewport);

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
    if (viewport.isHovered && !isOnGizmo && !isOnCube && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
        if (hit.isValid()) editor.selectedAsset.clear();
    }

    // Things dragged in from the asset browser land where the pointer is on the ground.
    ImGui::SetCursorScreenPos(position);
    ImGui::Dummy(size);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kAssetPayload)) {
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
