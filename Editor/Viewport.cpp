// The 3D view: the editor's camera, clicking to select, the gizmo that moves, turns and
// scales the selection (with snapping), and the lines and icons drawn over the scene.
#include "Editor.hpp"
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
        if (!transform) continue;
        std::optional<float> hit;
        if (renderer && renderer->mesh && renderer->isVisible) {
            // Test against the mesh's box in the mesh's own space: bring the ray there.
            // The direction is left unnormalized, so the distance keeps its world scale.
            Mat4 toLocal = simd::inverse(transform->world);
            Ray local{transformPoint(toLocal, ray.origin), transformDirection(toLocal, ray.direction)};
            hit = intersectAABB(local, renderer->mesh->bounds());
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
    if (const Collider* collider = scene.get<Collider>(entity)) {
        const Vec4 green{0.3f, 1.0f, 0.4f, 1};
        Quat rotation;
        Vec3 worldPosition, worldScale;
        decompose(transform->world, worldPosition, rotation, worldScale);
        Vec3 center = worldPosition + rotate(rotation, collider->offset);
        if (collider->shape == ColliderShape::Sphere) {
            lines.sphere(center, collider->radius, green);
        } else if (collider->shape == ColliderShape::Box) {
            lines.box(center, collider->halfExtents, rotation, green);
        } else {
            Vec3 axis = rotate(rotation, Vec3{0, collider->halfHeight, 0});
            lines.sphere(center + axis, collider->radius, green, 16);
            lines.sphere(center - axis, collider->radius, green, 16);
        }
    }
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
    SceneDocument& scene = editor.scene;
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

    // Play, in the middle of the view.
    const char* playLabel = scene.isPlaying() ? "Stop" : "Play";
    float playWidth = 64, pauseWidth = 64;
    float total = playWidth + (scene.isPlaying() ? pauseWidth + 4 : 0);
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 14, (ImGui::GetWindowWidth() - total) * 0.5f));
    if (scene.isPlaying()) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.9f));
    if (ImGui::Button(playLabel, ImVec2(playWidth, 0))) scene.setPlaying(!scene.isPlaying());
    if (scene.isPlaying()) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Run the scene, or go back to editing (Cmd+P)");
    if (scene.isPlaying()) {
        ImGui::SameLine();
        if (ImGui::Button(scene.isPaused() ? "Resume" : "Pause", ImVec2(pauseWidth, 0))) scene.setPaused(!scene.isPaused());
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

void drawViewport(EditorContext& editor, ImVec2 position, ImVec2 size) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    Engine& engine = editor.engine;
    ViewportInfo& viewport = editor.viewport;
    viewport.position = position;
    viewport.size = size;

    // The renderer draws the scene into exactly this rectangle of the window.
    float pixelsPerPoint = engine.window() ? engine.window()->contentScale() : 1.0f;
    Renderer::Rect area;
    area.x = int(std::round(position.x * pixelsPerPoint));
    area.y = int(std::round(position.y * pixelsPerPoint));
    area.width = int(std::round(size.x * pixelsPerPoint));
    area.height = int(std::round(size.y * pixelsPerPoint));
    engine.renderer().sceneViewport = area;
    if (size.x < 8 || size.y < 8) return;

    // A window without a background, exactly over the view: it takes the mouse and holds the toolbar.
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    bool isVisible = ImGui::Begin("##Viewport", nullptr, flags);
    ImGui::PopStyleVar(2);
    if (!isVisible) {
        ImGui::End();
        return;
    }
    viewport.drawList = ImGui::GetWindowDrawList();

    drawToolbar(editor);
    bool isOnToolbar = ImGui::IsAnyItemHovered();
    // Hovered also while one of its own popups (the snap steps) is not in the way.
    viewport.isHovered = ImGui::IsWindowHovered() && !isOnToolbar;

    if (document.isPlaying()) {
        // While playing, the view belongs to the game.
        ImGui::End();
        return;
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
