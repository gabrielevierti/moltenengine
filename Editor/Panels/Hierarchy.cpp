// The hierarchy: every entity of the scene as a tree, parents above their children.
// Click to select (Cmd adds), drag one onto another to parent it, F2 to rename.
#include "Editor.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>

namespace molten::editor {

namespace {

bool containsIgnoringCase(const std::string& text, const std::string& part) {
    auto found = std::search(text.begin(), text.end(), part.begin(), part.end(),
                             [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return found != text.end();
}

class HierarchyPanel final : public Panel {
public:
    HierarchyPanel() { title = "Hierarchy"; }

    void draw(EditorContext& editor) override {
        SceneDocument& scene = editor.scene;

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search", &filter_);
        ImGui::Spacing();

        ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
        if (filter_.empty()) {
            for (Entity root : scene.roots()) drawNode(editor, root);
        } else {
            // A search shows what matches as a flat list: the tree would hide it in closed branches.
            for (Entity entity : scene.entities()) {
                if (containsIgnoringCase(scene.nameOf(entity), filter_)) drawRow(editor, entity, true);
            }
        }

        // The empty space under the last row: dropping there removes the parent, and a
        // click there deselects.
        ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##rest", ImVec2(std::max(rest.x, 1.0f), std::max(rest.y, 40.0f)));
        if (ImGui::IsItemClicked()) scene.clearSelection();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kEntityPayload)) {
                dropEntities(editor, *static_cast<const Entity*>(payload->Data), Entity{});
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("##rest-menu")) {
            drawCreateMenu(editor, Entity{});
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            startRenaming(editor, scene.primary());
        }
    }

private:
    void startRenaming(EditorContext& editor, Entity entity) {
        if (!editor.engine.scene.isAlive(entity)) return;
        renaming_ = entity;
        renameText_ = editor.scene.nameOf(entity);
        renameNeedsFocus_ = true;
    }

    /// Parents the dragged entity, and the rest of the selection if it was part of it.
    void dropEntities(EditorContext& editor, Entity dragged, Entity newParent) {
        SceneDocument& scene = editor.scene;
        std::vector<Entity> moved = scene.isSelected(dragged) ? scene.selection() : std::vector<Entity>{dragged};
        for (Entity entity : moved) {
            if (!scene.reparent(entity, newParent)) editor.notify("An entity cannot be put under itself", ConsoleMessage::Level::Warning);
        }
    }

    void drawNode(EditorContext& editor, Entity entity) {
        std::vector<Entity> children = editor.scene.children(entity);
        bool isOpen = drawRow(editor, entity, children.empty());
        if (!isOpen) return;
        for (Entity child : children) drawNode(editor, child);
        ImGui::TreePop();
    }

    /// One row. Returns true if it is an open branch: the caller draws the children and pops.
    bool drawRow(EditorContext& editor, Entity entity, bool isLeaf) {
        SceneDocument& scene = editor.scene;
        Scene& world = editor.engine.scene;
        ImGui::PushID(int(entity.index));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding;
        if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (scene.isSelected(entity)) flags |= ImGuiTreeNodeFlags_Selected;

        bool isRenaming = renaming_ == entity;
        // What kind of thing it is, in one dim word after the name.
        const char* kind = world.has<Camera>(entity) ? "camera" : world.has<Light>(entity) ? "light"
                         : world.has<ParticleEmitter>(entity) ? "particles" : nullptr;
        std::string name = scene.nameOf(entity);
        bool isOpen = ImGui::TreeNodeEx("##row", flags, "%s", isRenaming ? "" : name.c_str());

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) scene.toggleSelection(entity);
            else if (ImGui::GetIO().KeyShift) scene.addToSelection(entity);
            else scene.select(entity);
            editor.selectedAsset.clear();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) editor.frameSelectionRequested = true;

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(ui::kEntityPayload, &entity, sizeof(Entity));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kEntityPayload)) {
                dropEntities(editor, *static_cast<const Entity*>(payload->Data), entity);
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##menu")) {
            if (!scene.isSelected(entity)) scene.select(entity);
            if (ImGui::BeginMenu("Create Child")) {
                drawCreateMenu(editor, entity);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename", "F2")) startRenaming(editor, entity);
            if (ImGui::MenuItem("Duplicate", "Cmd+D")) scene.duplicateSelection();
            if (ImGui::MenuItem("Remove Parent", nullptr, false, world.isAlive(world.get<Transform>(entity)->parent))) {
                for (Entity selected : std::vector<Entity>(scene.selection())) scene.reparent(selected, Entity{});
            }
            if (ImGui::MenuItem("Frame in View", "F")) editor.frameSelectionRequested = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) scene.deleteSelection();
            ImGui::EndPopup();
        }

        if (isRenaming) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (renameNeedsFocus_) {
                ImGui::SetKeyboardFocusHere();
                renameNeedsFocus_ = false;
            }
            bool done = ImGui::InputText("##rename", &renameText_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivated()) {
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !renameText_.empty() && renameText_ != name) {
                    world.add(entity, Name{renameText_});
                    scene.markChanged();
                }
                renaming_ = Entity{};
            }
        } else if (kind) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", kind);
        }

        ImGui::PopID();
        return isOpen && !isLeaf;
    }

    std::string filter_;
    Entity renaming_;
    std::string renameText_;
    bool renameNeedsFocus_ = false;
};

}  // namespace

void registerHierarchyPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<HierarchyPanel>());
}

void drawCreateMenu(EditorContext& editor, Entity parent) {
    // "Light/Point Light" becomes the item "Point Light" in the submenu "Light". Entries
    // are grouped in the order their groups first appear.
    std::vector<std::string> groups;
    for (const auto& item : editor.registry.createEntries) {
        size_t slash = item.value.path.find('/');
        std::string group = slash == std::string::npos ? std::string() : item.value.path.substr(0, slash);
        if (std::find(groups.begin(), groups.end(), group) == groups.end()) groups.push_back(group);
    }
    for (const std::string& group : groups) {
        bool isOpen = group.empty() || ImGui::BeginMenu(group.c_str());
        if (!isOpen) continue;
        for (const auto& item : editor.registry.createEntries) {
            size_t slash = item.value.path.find('/');
            std::string itemGroup = slash == std::string::npos ? std::string() : item.value.path.substr(0, slash);
            if (itemGroup != group) continue;
            std::string label = slash == std::string::npos ? item.value.path : item.value.path.substr(slash + 1);
            if (ImGui::MenuItem(label.c_str())) {
                Entity created = item.value.create(editor);
                if (editor.engine.scene.isAlive(created)) {
                    if (editor.engine.scene.isAlive(parent)) {
                        editor.engine.scene.updateTransforms();
                        editor.scene.reparent(created, parent);
                        // Under a parent, a new entity starts where the parent is.
                        if (Transform* transform = editor.engine.scene.get<Transform>(created)) transform->position = Vec3{0, 0, 0};
                    }
                    editor.scene.select(created);
                    editor.selectedAsset.clear();
                    editor.scene.markChanged();
                }
            }
        }
        if (!group.empty()) ImGui::EndMenu();
    }
}

void registerCreateEntries(EditorContext& editor) {
    auto add = [&editor](const char* path, Primitive kind) {
        editor.registry.addCreateEntry(nullptr, path, [kind](EditorContext& context) { return context.scene.create(kind); });
    };
    add("Empty", Primitive::Empty);
    add("3D Object/Cube", Primitive::Cube);
    add("3D Object/Sphere", Primitive::Sphere);
    add("3D Object/Cylinder", Primitive::Cylinder);
    add("3D Object/Plane", Primitive::Plane);
    add("Light/Directional Light", Primitive::DirectionalLight);
    add("Light/Point Light", Primitive::PointLight);
    add("Light/Spot Light", Primitive::SpotLight);
    add("Camera", Primitive::Camera);
    add("Effects/Particle Emitter", Primitive::ParticleEmitter);
}

}  // namespace molten::editor
