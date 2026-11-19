// SceneTools: the example editor plugin.
//
// It shows the four things a plugin can add: a panel, menu items, a drawing over the
// 3D view, and work done every frame. Copy this folder to start a plugin of your own;
// Docs/Plugins.md explains each part.
#include "API/EditorAPI.hpp"

#include <algorithm>
#include <string>

using namespace molten;
using namespace molten::editor;

namespace {

/// A window that counts what is in the scene.
class StatisticsPanel final : public Panel {
public:
    StatisticsPanel() {
        title = "Scene Statistics";
        isOpen = false;                 // opened from the Window menu
    }

    void draw(EditorContext& editor) override {
        Scene& scene = editor.engine.scene;
        size_t triangles = 0;
        scene.each<MeshRenderer>([&](Entity, MeshRenderer& renderer) {
            if (renderer.mesh && renderer.isVisible) triangles += renderer.mesh->indexCount() / 3;
        });
        ImGui::Text("Entities     %zu", editor.scene.entities().size());
        ImGui::Text("Meshes       %zu", scene.count<MeshRenderer>());
        ImGui::Text("Triangles    %zu", triangles);
        ImGui::Text("Lights       %zu", scene.count<Light>());
        ImGui::Text("Colliders    %zu", scene.count<Collider>());
        ImGui::Text("Bodies       %zu", scene.count<RigidBody>());
        ImGui::Text("Materials    %zu", editor.scene.materials.size());
        ImGui::Separator();
        const RenderStats& stats = editor.engine.renderer().stats;
        ImGui::Text("Draw calls   %d", stats.drawCalls);
        ImGui::Text("Drawn        %d", stats.visibleObjects);
        ImGui::Text("Culled       %d", stats.culledObjects);
    }
};

/// Moves every selected entity straight down (or up) until the bottom of its mesh rests on y = 0.
void dropToGround(EditorContext& editor) {
    Scene& scene = editor.engine.scene;
    int moved = 0;
    for (Entity entity : editor.scene.selection()) {
        const Transform* transform = scene.get<Transform>(entity);
        const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        if (!transform) continue;
        float bottom = transform->worldPosition().y;
        if (renderer && renderer->mesh) bottom = renderer->mesh->bounds().transformed(transform->world).min.y;
        Mat4 world = transform->world;
        world.columns[3].y -= bottom;
        editor.scene.setWorldMatrix(entity, world);
        moved++;
    }
    if (moved > 0) {
        editor.scene.markChanged();     // one step for Undo
        editor.notify("Dropped " + std::to_string(moved) + (moved == 1 ? " entity" : " entities") + " to the ground");
    }
}

/// Turns every selected entity around the vertical axis by a random angle: quick variety for props.
void randomizeYaw(EditorContext& editor) {
    Scene& scene = editor.engine.scene;
    static Random random{12345};
    for (Entity entity : editor.scene.selection()) {
        if (Transform* transform = scene.get<Transform>(entity)) {
            transform->rotation = axisAngle(Vec3{0, 1, 0}, random.range(0.0f, 2.0f * kPi)) * transform->rotation;
        }
    }
    editor.scene.markChanged();
}

class SceneTools final : public Plugin {
public:
    const char* name() const override { return "Scene Tools"; }

    void onLoad(EditorContext& editor) override {
        // `this` is the owner of everything registered: the editor removes it all when the plugin is unloaded.
        editor.registry.addPanel(this, std::make_unique<StatisticsPanel>());

        auto hasSelection = [](EditorContext& context) { return !context.scene.selection().empty() && !context.scene.isPlaying(); };
        editor.registry.addMenuItem(this, "Tools/Drop Selection to Ground", dropToGround, "", hasSelection);
        editor.registry.addMenuItem(this, "Tools/Randomize Rotation Around Y", randomizeYaw, "", hasSelection);
        editor.registry.addMenuItem(this, "Tools/Show Names in View", [this](EditorContext&) { showsNames_ = !showsNames_; });

        // Drawn over the 3D view every frame, after the editor's own lines and icons.
        editor.registry.addViewportOverlay(this, [this](EditorContext& context, const ViewportInfo& viewport) {
            if (!showsNames_) return;
            for (Entity entity : context.scene.selection()) {
                const Transform* transform = context.engine.scene.get<Transform>(entity);
                ImVec2 at;
                if (!transform || !viewport.worldToScreen(transform->worldPosition(), at)) continue;
                std::string label = context.scene.nameOf(entity);
                ImVec2 size = ImGui::CalcTextSize(label.c_str());
                ImVec2 min(at.x - size.x * 0.5f - 6, at.y - 44), max(at.x + size.x * 0.5f + 6, at.y - 44 + size.y + 6);
                viewport.drawList->AddRectFilled(min, max, IM_COL32(20, 24, 31, 220), 4.0f);
                viewport.drawList->AddText(ImVec2(min.x + 6, min.y + 3), IM_COL32(255, 255, 255, 255), label.c_str());
            }
        });
    }

private:
    bool showsNames_ = false;
};

}  // namespace

MOLTEN_EDITOR_PLUGIN(SceneTools)
