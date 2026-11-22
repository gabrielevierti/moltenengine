// The hierarchy: every entity of the scene as a tree, parents above their children.
// Click to select (Cmd adds), drag one onto another to parent it, F2 to rename.
#include "HierarchySearch.hpp"
#include "Editor.hpp"
#include "Kit/Character.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Network.hpp"
#include "Kit/WhenThen.hpp"
#include "Water/Water.hpp"
#include "Kit/Spline.hpp"
#include "Kit/Layout.hpp"
#include "Kit/Terrain.hpp"
#include "Kit/Foliage.hpp"
#include "Kit/CameraShots.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unordered_set>

namespace molten::editor {

namespace {

class HierarchyPanel final : public Panel {
public:
    HierarchyPanel() { title = "Hierarchy"; }

    void draw(EditorContext& editor) override {
        SceneDocument& scene = editor.scene;

        // Something else chose an entity (a click in the Scene view, a paste, the search): the tree
        // shows where it is, unfolding what it is under. A click in the tree itself needs no such help.
        if (scene.primary() != shownPrimary_) {
            shownPrimary_ = scene.primary();
            bool isOwnClick = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (!isOwnClick && editor.engine.scene.isAlive(shownPrimary_)) {
                const Transform* transform = editor.engine.scene.get<Transform>(shownPrimary_);
                for (int depth = 0; depth < 64 && transform && editor.engine.scene.isAlive(transform->parent); depth++) {
                    open_.insert(transform->parent.index);
                    transform = editor.engine.scene.get<Transform>(transform->parent);
                }
                scrollTo_ = shownPrimary_;
            }
        }

        // ---- the top: add something, or find something
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0))) ImGui::OpenPopup("##create");
        ui::tooltip("Create an entity");
        if (ImGui::BeginPopup("##create")) {
            drawCreateMenu(editor, Entity{});
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + 6));
        if (editor.hierarchySearchRequested) {
            editor.hierarchySearchRequested = false;
            ImGui::SetKeyboardFocusHere();          // Edit > Find in Hierarchy (Cmd+F)
        }
        ImGui::InputTextWithHint("##search", "Search: a name, c:Light, tag:Enemy", &filter_);
        ImGui::SameLine(0, 6);
        // Only one kind of thing: the lights, the cameras... (the same as typing c:Light).
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            float side = ImGui::GetFrameHeight();
            if (ImGui::Button("##kind", ImVec2(side, side))) ImGui::OpenPopup("##kinds");
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            // A funnel.
            ImVec2 funnel[3] = {ImVec2(at.x + side * 0.22f, at.y + side * 0.28f), ImVec2(at.x + side * 0.78f, at.y + side * 0.28f), ImVec2(at.x + side * 0.5f, at.y + side * 0.6f)};
            draw->AddConvexPolyFilled(funnel, 3, color);
            draw->AddLine(ImVec2(at.x + side * 0.5f, at.y + side * 0.55f), ImVec2(at.x + side * 0.5f, at.y + side * 0.76f), color, 2.0f);
            ui::tooltip("Only one kind of thing");
            if (ImGui::BeginPopup("##kinds")) {
                const char* kinds[][3] = {{"Everything", "", "axes"}, {"Lights", "c:Light", "light"}, {"Cameras", "c:Camera", "camera"}, {"Meshes", "c:MeshRenderer", "cube"},
                                          {"Colliders", "c:Collider", "collider"}, {"Bodies", "c:RigidBody", "physics"}, {"Sounds", "c:AudioSource", "audio"},
                                          {"Animated", "c:Animator", "anim"}, {"Particles", "c:ParticleEmitter", "spark"}, {"Interface", "c:UI", "ui"},
                                          {"Scripts", "c:VisualScript", "graph"}, {"Switched Off", "is:off", "eye"}};
                for (auto& kind : kinds) {
                    ImVec2 row = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable((std::string("      ") + kind[0]).c_str(), filter_ == kind[1])) filter_ = kind[1];
                    ui::drawIcon(ImGui::GetWindowDrawList(), ImVec2(row.x + 9, row.y + ImGui::GetTextLineHeight() * 0.5f), 13, kind[2], ImGui::GetColorU32(ImGuiCol_Text));
                }
                ImGui::EndPopup();
            }
        }
        ui::tooltip("A name; c: a component; tag: a tag; layer: a layer; prefab: a prefab's copies; is:off. "
                    "Every word must hold; a word with - before it must not.");
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 2));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
        lastVisible_ = visible_;
        visible_.clear();
        if (filter_.empty()) {
            drawSceneRow(editor);
            if (!isSceneClosed_) {
                for (Entity root : scene.roots()) drawNode(editor, root, 1);
            }
        } else {
            // A search shows what matches as a flat list: the tree would hide it in closed branches.
            std::vector<SearchWord> words = parseSearch(filter_);
            std::vector<Entity> found;
            for (Entity entity : scene.entities()) {
                if (matchesSearch(editor.engine.scene, entity, scene.nameOf(entity), words)) found.push_back(entity);
            }
            if (found.size() > 1) {
                // All of them at once: then the Inspector changes them all together.
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
                if (ImGui::SmallButton(("Select all " + std::to_string(found.size())).c_str())) {
                    scene.clearSelection();
                    for (Entity entity : found) scene.addToSelection(entity);
                }
                ImGui::PopStyleVar();
                ImGui::Dummy(ImVec2(0, 2));
            }
            for (Entity entity : found) drawRow(editor, entity, 0, false);
            if (visible_.empty()) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextDisabled("  Nothing found.");
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
            acceptPrefab(editor);
            ImGui::EndDragDropTarget();
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        if (ImGui::BeginPopupContextItem("##rest-menu")) {
            editor.menuItem("paste");
            editor.menuItem("selectAll");
            ImGui::Separator();
            drawCreateMenu(editor, Entity{});
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        // ---- the keyboard, when the tree has it: arrows walk the rows and fold branches.
        if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && !renaming_.isValid() && !visible_.empty()) {
            Entity primary = scene.primary();
            auto at = std::find(visible_.begin(), visible_.end(), primary);
            int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
            if (step != 0) {
                size_t next = at == visible_.end() ? 0 : size_t(std::clamp(int(at - visible_.begin()) + step, 0, int(visible_.size()) - 1));
                if (ImGui::GetIO().KeyShift) scene.addToSelection(visible_[next]);
                else scene.select(visible_[next]);
                scrollTo_ = visible_[next];
            }
            if (at != visible_.end()) {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) open_.erase(primary.index);
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) open_.insert(primary.index);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        // Edit > Rename (F2), asked for from the menu or the keyboard.
        if (editor.renameRequested) {
            editor.renameRequested = false;
            if (editor.engine.scene.isAlive(scene.primary())) {
                scrollTo_ = scene.primary();
                startRenaming(editor, scene.primary());
            }
        }
    }

private:
    void startRenaming(EditorContext& editor, Entity entity) {
        if (!editor.engine.scene.isAlive(entity)) return;
        renaming_ = entity;
        renameText_ = editor.scene.nameOf(entity);
        renameNeedsFocus_ = true;
    }

    /// Writes the entity to Assets/Prefabs under its own name (with a number if taken).
    void savePrefab(EditorContext& editor, Entity entity) {
        namespace fs = std::filesystem;
        std::string name = editor.scene.nameOf(entity);
        for (char& c : name) {
            if (c == '/' || c == ':' || c == '\\') c = '-';
        }
        if (name.empty()) name = "Prefab";
        std::error_code code;
        fs::create_directories(editor.assets.absolutePath("Prefabs"), code);
        std::string path = "Prefabs/" + name + ".prefab";
        for (int number = 2; fs::exists(editor.assets.absolutePath(path), code); number++) {
            path = "Prefabs/" + name + " " + std::to_string(number) + ".prefab";
        }
        if (editor.scene.savePrefab(entity, editor.assets.absolutePath(path))) {
            editor.assets.refresh();
            editor.currentFolder = "Prefabs";
            editor.selectedAsset = path;
            editor.notify("Saved " + path + ". Drag it into the scene to use it, or spawn it from the game.");
        } else {
            editor.notify("Cannot write " + path, ConsoleMessage::Level::Error);
        }
    }

    /// What a drag carries: the dragged entity, or the whole selection if it was part of it, in
    /// the order of the rows, without the ones that go along inside another.
    std::vector<Entity> carried(EditorContext& editor, Entity dragged) {
        SceneDocument& scene = editor.scene;
        std::vector<Entity> chosen = scene.isSelected(dragged) ? scene.selection() : std::vector<Entity>{dragged};
        std::vector<Entity> ordered;
        for (Entity entity : scene.entities()) {
            if (std::find(chosen.begin(), chosen.end(), entity) == chosen.end()) continue;
            bool isInside = false;
            for (Entity other : chosen) isInside |= other != entity && scene.isDescendant(entity, other);
            if (!isInside) ordered.push_back(entity);
        }
        return ordered;
    }

    /// Dropped on the line between two rows: beside `target`, under its parent, in their order.
    void dropBeside(EditorContext& editor, Entity dragged, Entity target, bool after) {
        SceneDocument& scene = editor.scene;
        std::vector<Entity> moved = carried(editor, dragged);
        if (std::find(moved.begin(), moved.end(), target) != moved.end()) return;
        Entity anchor = target;
        for (Entity entity : moved) {
            if (!scene.moveBeside(entity, anchor, after)) {
                editor.notify("An entity cannot be put under itself", ConsoleMessage::Level::Warning);
                continue;
            }
            anchor = entity;
            after = true;
        }
    }

    /// Parents the dragged entity, and the rest of the selection if it was part of it.
    void dropEntities(EditorContext& editor, Entity dragged, Entity newParent) {
        SceneDocument& scene = editor.scene;
        std::vector<Entity> moved = scene.isSelected(dragged) ? scene.selection() : std::vector<Entity>{dragged};
        for (Entity entity : moved) {
            if (!scene.reparent(entity, newParent)) editor.notify("An entity cannot be put under itself", ConsoleMessage::Level::Warning);
        }
    }

    static constexpr float kRowHeight = 24.0f;
    static constexpr float kIndent = 15.0f;

    enum class Kind { Empty, Mesh, Light, Sun, Camera, Particles, Code };

    static Kind kindOf(Scene& world, Entity entity) {
        if (world.has<Camera>(entity)) return Kind::Camera;
        if (const Light* light = world.get<Light>(entity)) {
            if (!world.has<ParticleEmitter>(entity)) return light->type == LightType::Directional ? Kind::Sun : Kind::Light;
        }
        if (world.has<ParticleEmitter>(entity)) return Kind::Particles;
        if (world.has<MeshRenderer>(entity)) return Kind::Mesh;
        for (const ComponentType& type : ComponentRegistry::instance().types()) {
            if (type.has(world, entity)) return Kind::Code;
        }
        return Kind::Empty;
    }

    /// A small drawing that says what an entity is, centered on `c`.
    static void drawIcon(ImDrawList* drawList, ImVec2 c, Kind kind, float alpha) {
        auto color = [alpha](int r, int g, int b) { return IM_COL32(r, g, b, int(255 * alpha)); };
        switch (kind) {
            case Kind::Mesh: {
                ImU32 line = color(130, 170, 235);
                const ImVec2 top(c.x, c.y - 6.5f), middle(c.x, c.y - 0.5f), bottom(c.x, c.y + 6.5f);
                const ImVec2 leftTop(c.x - 5.5f, c.y - 3.5f), rightTop(c.x + 5.5f, c.y - 3.5f), leftBottom(c.x - 5.5f, c.y + 3.5f), rightBottom(c.x + 5.5f, c.y + 3.5f);
                drawList->AddQuadFilled(top, rightTop, middle, leftTop, color(130, 170, 235));
                drawList->AddQuadFilled(leftTop, middle, bottom, leftBottom, color(88, 122, 184));
                drawList->AddQuadFilled(middle, rightTop, rightBottom, bottom, color(62, 90, 142));
                (void)line;
                break;
            }
            case Kind::Sun: {
                ImU32 yellow = color(250, 205, 90);
                drawList->AddCircleFilled(c, 3.2f, yellow, 16);
                for (int i = 0; i < 8; i++) {
                    float angle = float(i) * 0.7854f;
                    ImVec2 direction(std::cos(angle), std::sin(angle));
                    drawList->AddLine(ImVec2(c.x + direction.x * 5.0f, c.y + direction.y * 5.0f), ImVec2(c.x + direction.x * 7.0f, c.y + direction.y * 7.0f), yellow, 1.4f);
                }
                break;
            }
            case Kind::Light: {
                ImU32 yellow = color(250, 205, 90);
                drawList->AddCircleFilled(ImVec2(c.x, c.y - 1.5f), 4.6f, yellow, 20);
                drawList->AddRectFilled(ImVec2(c.x - 2.2f, c.y + 3.2f), ImVec2(c.x + 2.2f, c.y + 6.5f), color(170, 150, 110), 1.0f);
                break;
            }
            case Kind::Camera: {
                ImU32 green = color(140, 210, 160);
                drawList->AddRectFilled(ImVec2(c.x - 7, c.y - 4), ImVec2(c.x + 2.5f, c.y + 4), green, 1.5f);
                drawList->AddTriangleFilled(ImVec2(c.x + 2.5f, c.y), ImVec2(c.x + 7, c.y - 4), ImVec2(c.x + 7, c.y + 4), green);
                break;
            }
            case Kind::Particles: {
                ImU32 orange = color(255, 150, 80);
                const float dots[4][3] = {{-3.5f, 3.0f, 2.6f}, {2.5f, 0.5f, 2.1f}, {-1.0f, -3.5f, 1.6f}, {4.5f, -4.5f, 1.1f}};
                for (const auto& dot : dots) drawList->AddCircleFilled(ImVec2(c.x + dot[0], c.y + dot[1]), dot[2], orange, 12);
                break;
            }
            case Kind::Code: {
                ImU32 teal = color(110, 200, 210);
                const ImVec2 left[3] = {ImVec2(c.x - 2.5f, c.y - 4.5f), ImVec2(c.x - 6.5f, c.y), ImVec2(c.x - 2.5f, c.y + 4.5f)};
                const ImVec2 right[3] = {ImVec2(c.x + 2.5f, c.y - 4.5f), ImVec2(c.x + 6.5f, c.y), ImVec2(c.x + 2.5f, c.y + 4.5f)};
                drawList->AddPolyline(left, 3, teal, ImDrawFlags_None, 1.6f);
                drawList->AddPolyline(right, 3, teal, ImDrawFlags_None, 1.6f);
                break;
            }
            case Kind::Empty:
                drawList->AddCircle(c, 4.5f, color(128, 136, 150), 16, 1.4f);
                break;
        }
    }

    static void drawArrow(ImDrawList* drawList, ImVec2 c, bool isOpen, ImU32 color) {
        if (isOpen) drawList->AddTriangleFilled(ImVec2(c.x - 4, c.y - 2.5f), ImVec2(c.x + 4, c.y - 2.5f), ImVec2(c.x, c.y + 3), color);
        else drawList->AddTriangleFilled(ImVec2(c.x - 2.5f, c.y - 4), ImVec2(c.x + 3, c.y), ImVec2(c.x - 2.5f, c.y + 4), color);
    }

    /// The scene itself, above everything in it: its name, and a dot if it has unsaved changes.
    void drawSceneRow(EditorContext& editor) {
        SceneDocument& scene = editor.scene;
        ImVec2 min = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##scene", ImVec2(std::max(width, 1.0f), kRowHeight))) isSceneClosed_ = !isSceneClosed_;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 max(min.x + width, min.y + kRowHeight);
        drawList->AddRectFilled(min, max, ImGui::IsItemHovered() ? IM_COL32(48, 53, 62, 255) : IM_COL32(38, 42, 50, 255), 4.0f);
        float middle = min.y + kRowHeight * 0.5f;
        drawArrow(drawList, ImVec2(min.x + 11, middle), !isSceneClosed_, IM_COL32(160, 168, 182, 255));
        std::string name = scene.filePath.empty() ? std::string("Untitled") : std::filesystem::path(scene.filePath).stem().string();
        float textY = middle - ImGui::GetFontSize() * 0.5f;
        drawList->AddText(ImVec2(min.x + 24, textY), IM_COL32(223, 227, 234, 255), name.c_str());
        float nameWidth = ImGui::CalcTextSize(name.c_str()).x;
        if (scene.isDirty()) drawList->AddCircleFilled(ImVec2(min.x + 24 + nameWidth + 9, middle), 2.6f, IM_COL32(255, 122, 47, 255), 12);
        char count[32];
        std::snprintf(count, sizeof(count), "%zu", scene.entities().size());
        drawList->AddText(ImVec2(max.x - ImGui::CalcTextSize(count).x - 9, textY), IM_COL32(120, 128, 142, 255), count);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kEntityPayload)) {
                dropEntities(editor, *static_cast<const Entity*>(payload->Data), Entity{});
            }
            acceptPrefab(editor);
            ImGui::EndDragDropTarget();
        }
        ImGui::Dummy(ImVec2(0, 2));
    }

    /// A prefab dragged from the assets lands at the origin.
    void acceptPrefab(EditorContext& editor) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kAssetPayload);
        if (!payload) return;
        std::string path(static_cast<const char*>(payload->Data));
        const AssetInfo* asset = editor.assets.find(path);
        if (asset && asset->type == AssetType::Prefab) editor.scene.instantiatePrefab(path, Vec3{0, 0, 0});
    }

    void drawNode(EditorContext& editor, Entity entity, int depth) {
        std::vector<Entity> children = editor.scene.children(entity);
        bool isOpen = drawRow(editor, entity, depth, !children.empty());
        if (!isOpen) return;
        for (Entity child : children) drawNode(editor, child, depth + 1);
    }

    /// One row. Returns true if it is a branch and open: the caller draws the children.
    bool drawRow(EditorContext& editor, Entity entity, int depth, bool hasChildren) {
        SceneDocument& scene = editor.scene;
        Scene& world = editor.engine.scene;
        ImGui::PushID(int(entity.index));
        visible_.push_back(entity);

        const bool isSelected = scene.isSelected(entity);
        const bool isRenaming = renaming_ == entity;
        bool isOpen = hasChildren && open_.count(entity.index);
        const std::string name = scene.nameOf(entity);
        const Kind kind = kindOf(world, entity);
        Transform* own = world.get<Transform>(entity);
        const bool isHidden = own && !own->isActiveInHierarchy;

        ImVec2 min = ImGui::GetCursorScreenPos();
        float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
        ImVec2 max(min.x + width, min.y + kRowHeight);
        float middle = min.y + kRowHeight * 0.5f;
        float arrowX = min.x + 11 + float(depth) * kIndent;
        float iconX = arrowX + 17;
        float textX = iconX + 15;

        ImGui::SetNextItemAllowOverlap();
        bool isClicked = ImGui::InvisibleButton("##row", ImVec2(width, kRowHeight));
        bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (scrollTo_ == entity) {
            ImGui::SetScrollHereY(0.5f);
            scrollTo_ = Entity{};
        }
        bool onArrow = hasChildren && std::abs(ImGui::GetIO().MousePos.x - arrowX) < 9;
        if (isClicked) {
            if (onArrow) {
                if (isOpen) open_.erase(entity.index);
                else open_.insert(entity.index);
            } else {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl || io.KeySuper) scene.toggleSelection(entity);
                else if (io.KeyShift) selectRangeTo(editor, entity);
                else scene.select(entity);
            }
        }
        if (isHovered && !onArrow && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) editor.frameSelectionRequested = true;

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(ui::kEntityPayload, &entity, sizeof(Entity));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        bool isDropTarget = false;
        if (ImGui::BeginDragDropTarget()) {
            // On the upper edge of a row: before it, under the same parent. On the lower edge: after
            // it (into an open branch: its first child). On the row itself: into it.
            float into = ImGui::GetIO().MousePos.y - min.y;
            bool isBefore = into < kRowHeight * 0.3f, isAfter = into > kRowHeight * 0.7f;
            bool isBetween = isBefore || isAfter;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kEntityPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                std::vector<Entity> kids = isAfter && isOpen ? scene.children(entity) : std::vector<Entity>{};
                if (isBetween) {
                    float lineY = isBefore ? min.y : max.y;
                    float lineX = min.x + 8 + float(depth + (kids.empty() ? 0 : 1)) * kIndent;
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(lineX, lineY), ImVec2(max.x - 4, lineY), IM_COL32(255, 122, 47, 255), 2.0f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(lineX, lineY), 3.0f, IM_COL32(255, 122, 47, 255));
                }
                if (payload->IsDelivery()) {
                    Entity dragged = *static_cast<const Entity*>(payload->Data);
                    if (!kids.empty()) {
                        dropBeside(editor, dragged, kids.front(), false);
                    } else if (isBetween) {
                        dropBeside(editor, dragged, entity, isAfter);
                    } else {
                        dropEntities(editor, *static_cast<const Entity*>(payload->Data), entity);
                        open_.insert(entity.index);
                    }
                }
            }
            // A material from the Explorer, onto the thing this row is.
            if (const ImGuiPayload* held = ImGui::GetDragDropPayload(); held && held->IsDataType(ui::kAssetPayload)) {
                std::string path(static_cast<const char*>(held->Data));
                const AssetInfo* asset = editor.assets.find(path);
                MeshRenderer* renderer = world.get<MeshRenderer>(entity);
                if (asset && asset->type == AssetType::Material && renderer) {
                    isBetween = false;
                    if (ImGui::AcceptDragDropPayload(ui::kAssetPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                        if (std::shared_ptr<Material> material = editor.engine.assets().material(path)) {
                            renderer->material = material;
                            scene.select(entity);
                            scene.markChanged();
                        }
                    }
                } else {
                    isBetween = true;                   // nothing to do with it here: no highlight
                }
            }
            ImGui::EndDragDropTarget();
            isDropTarget = !isBetween;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        if (ImGui::BeginPopupContextItem("##menu")) {
            if (!scene.isSelected(entity)) scene.select(entity);
            // The same commands as the Edit and Entity menus, with the same keys: see EditorCommands.cpp.
            editor.menuItem("cut");
            editor.menuItem("copy");
            editor.menuItem("paste");
            ImGui::Separator();
            editor.menuItem("rename");
            editor.menuItem("duplicate");
            editor.menuItem("delete");
            ImGui::Separator();
            if (ImGui::BeginMenu("Create Child")) {
                drawCreateMenu(editor, entity);
                ImGui::EndMenu();
            }
            editor.menuItem("group");
            editor.menuItem("clearParent");
            editor.menuItem("firstSibling");
            editor.menuItem("lastSibling");
            editor.menuItem("selectChildren");
            ImGui::Separator();
            editor.menuItem("frame");
            editor.menuItem("moveToView");
            editor.menuItem("alignWithView");
            editor.menuItem("toggleActive", world.get<Transform>(entity) && world.get<Transform>(entity)->isActive ? "Switch Off" : "Switch On");
            ImGui::Separator();
            if (ImGui::MenuItem("Save as Prefab...")) savePrefab(editor, entity);
            // A copy of a prefab follows the prefab; unpacked, it is the scene's own, to change freely.
            if (world.has<PrefabInstance>(entity) && ImGui::MenuItem("Unpack Prefab")) {
                world.remove<PrefabInstance>(entity);
                std::vector<Entity> under{entity};
                for (size_t i = 0; i < under.size(); i++) {
                    for (Entity child : scene.children(under[i])) {
                        world.remove<FromPrefab>(child);
                        if (!world.has<PrefabInstance>(child)) under.push_back(child);       // a prefab inside stays a copy
                    }
                }
                scene.markChanged();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        // ---- the row itself, drawn by hand
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (isSelected) {
            drawList->AddRectFilled(min, max, IM_COL32(255, 122, 47, 46), 4.0f);
            drawList->AddRectFilled(min, ImVec2(min.x + 3, max.y), IM_COL32(255, 122, 47, 255), 2.0f);
        } else if (isHovered) {
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 14), 4.0f);
        }
        if (isDropTarget) drawList->AddRect(min, max, IM_COL32(255, 122, 47, 255), 4.0f, ImDrawFlags_None, 1.5f);
        // A faint line per level shows what is under what.
        for (int level = 1; level < depth; level++) {
            float x = min.x + 11 + float(level) * kIndent;
            drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 20), 1.0f);
        }
        if (hasChildren) drawArrow(drawList, ImVec2(arrowX, middle), isOpen, isHovered && onArrow ? IM_COL32(230, 234, 240, 255) : IM_COL32(140, 148, 162, 255));
        drawIcon(drawList, ImVec2(iconX, middle), kind, isHidden ? 0.4f : 1.0f);

        // The eye, at the right: shown when the pointer is on the row, or always when it is closed.
        float right = max.x - 6;
        if (own && (isHovered || !own->isActive)) {
            ImVec2 eye(right - 9, middle);
            ImGui::SetCursorScreenPos(ImVec2(eye.x - 10, min.y));
            if (ImGui::InvisibleButton("##eye", ImVec2(20, kRowHeight))) {
                own->isActive = !own->isActive;
                scene.markChanged();
            }
            ui::tooltip(!own->isActive ? "Switched off. Click to switch it on." : "Click to switch it off: not drawn, no collisions, no code.");
            const bool isOff = !own->isActive;
            ImU32 color = isOff ? IM_COL32(120, 128, 142, 255) : IM_COL32(190, 198, 210, 255);
            const ImVec2 lid[5] = {ImVec2(eye.x - 7, eye.y), ImVec2(eye.x - 3.5f, eye.y - 3.5f), ImVec2(eye.x, eye.y - 4.5f), ImVec2(eye.x + 3.5f, eye.y - 3.5f), ImVec2(eye.x + 7, eye.y)};
            const ImVec2 under[5] = {ImVec2(eye.x - 7, eye.y), ImVec2(eye.x - 3.5f, eye.y + 3.5f), ImVec2(eye.x, eye.y + 4.5f), ImVec2(eye.x + 3.5f, eye.y + 3.5f), ImVec2(eye.x + 7, eye.y)};
            drawList->AddPolyline(lid, 5, color, ImDrawFlags_None, 1.3f);
            drawList->AddPolyline(under, 5, color, ImDrawFlags_None, 1.3f);
            if (isOff) drawList->AddLine(ImVec2(eye.x - 6, eye.y + 5), ImVec2(eye.x + 6, eye.y - 5), color, 1.5f);
            else drawList->AddCircleFilled(eye, 2.0f, color, 10);
            right -= 24;
            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
            ImGui::Dummy(ImVec2(0, 0));
        }

        if (isRenaming) {
            ImGui::SetCursorScreenPos(ImVec2(textX - 4, min.y + 1));
            ImGui::SetNextItemWidth(std::max(right - textX, 60.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, (kRowHeight - 2 - ImGui::GetFontSize()) * 0.5f));
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
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
            ImGui::Dummy(ImVec2(0, 0));
        } else {
            drawList->PushClipRect(ImVec2(textX, min.y), ImVec2(right, max.y), true);
            // A prefab's copy in blue, as Unity has it; what it holds (from the prefab, not saved here) greyer.
            const bool isCopy = world.has<PrefabInstance>(entity), isFromPrefab = world.has<FromPrefab>(entity);
            ImU32 ink = isHidden ? IM_COL32(120, 128, 142, 255) : isCopy ? IM_COL32(120, 175, 255, 255) : isFromPrefab ? IM_COL32(150, 175, 215, 255) : IM_COL32(223, 227, 234, 255);
            drawList->AddText(ImVec2(textX, middle - ImGui::GetFontSize() * 0.5f), ink, name.empty() ? "(no name)" : name.c_str());
            drawList->PopClipRect();
        }

        ImGui::PopID();
        return isOpen;
    }

    /// Shift-click: everything between the last selected row and this one, as shown.
    void selectRangeTo(EditorContext& editor, Entity entity) {
        SceneDocument& scene = editor.scene;
        // The rows above this one are already in visible_; the ones below are not yet, so
        // the range is found in last frame's list when this frame's does not have both ends.
        const std::vector<Entity>& rows = lastVisible_.empty() ? visible_ : lastVisible_;
        auto from = std::find(rows.begin(), rows.end(), scene.primary());
        auto to = std::find(rows.begin(), rows.end(), entity);
        if (from == rows.end() || to == rows.end()) {
            scene.addToSelection(entity);
            return;
        }
        if (to < from) std::swap(from, to);
        for (auto row = from; row <= to; ++row) scene.addToSelection(*row);
        scene.addToSelection(entity);       // the one clicked becomes the primary
    }

    std::string filter_;
    std::unordered_set<uint32_t> open_;         // branches unfolded, by entity index: they start folded, or a scene of any size is unreadable
    std::vector<Entity> visible_, lastVisible_; // the rows as drawn, top to bottom: for arrows and ranges
    Entity scrollTo_;
    Entity shownPrimary_;                       // the last selected as of the last frame: when it changes from outside, the tree goes to it
    bool isSceneClosed_ = false;
    Entity renaming_;
    std::string renameText_;
    bool renameNeedsFocus_ = false;
};

}  // namespace

void registerHierarchyPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<HierarchyPanel>());
}

Entity createFromEntry(EditorContext& editor, const CreateEntry& entry, Entity parent) {
    Entity created = entry.create(editor);
    if (!editor.engine.scene.isAlive(created)) return created;
    if (editor.engine.scene.isAlive(parent)) {
        editor.engine.scene.updateTransforms();
        editor.scene.reparent(created, parent);
        // Under a parent, a new entity starts where the parent is.
        if (Transform* transform = editor.engine.scene.get<Transform>(created)) transform->position = Vec3{0, 0, 0};
    }
    editor.scene.select(created);
    editor.scene.markChanged();
    return created;
}

void drawCreateMenu(EditorContext& editor, Entity parent, bool withEmpty) {
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
            if (!withEmpty && item.value.path == "Empty") continue;         // the menu has its own Create Empty, with keys
            if (ImGui::MenuItem(label.c_str())) createFromEntry(editor, item.value, parent);
        }
        if (!group.empty()) ImGui::EndMenu();
    }
}

/// A point more on the Spline that is chosen (after its last point), or after the point that is
/// chosen, halfway to the next. Its points are numbered again, in order.
void addSplinePoint(EditorContext& editor) {
    Scene& scene = editor.engine.scene;
    if (editor.scene.selection().empty()) return;
    Entity chosen = editor.scene.selection().front(), line = chosen, after{};
    if (!scene.has<kit::Spline>(chosen)) {
        const Transform* transform = scene.get<Transform>(chosen);
        line = transform ? transform->parent : Entity{};
        after = chosen;
    }
    kit::Spline* spline = scene.get<kit::Spline>(line);
    if (!spline) {
        editor.notify("Choose a Spline, or one of its points, to add a point to it", ConsoleMessage::Level::Warning);
        return;
    }
    std::vector<Entity> points = kit::Spline::pointsOf(editor.engine, line);
    size_t place = points.size();                       // where the new one goes
    for (size_t i = 0; i < points.size(); i++) {
        if (points[i] == after) place = i + 1;
    }
    auto where = [&](size_t i) { return scene.get<Transform>(points[i])->position; };
    Vec3 position{0, 0, 0};
    if (points.empty()) {
        position = Vec3{0, 0, 0};
    } else if (place < points.size()) {
        position = (where(place - 1) + where(place)) * 0.5f;
    } else if (points.size() == 1) {
        position = where(0) + Vec3{4, 0, 0};
    } else {
        position = where(points.size() - 1) * 2.0f - where(points.size() - 2);     // on, the way it was going
    }
    Entity added = scene.create("Point");
    Transform transform;
    transform.position = position;
    transform.parent = line;
    scene.add(added, transform);
    points.insert(points.begin() + long(place), added);
    for (size_t i = 0; i < points.size(); i++) {
        if (Name* name = scene.get<Name>(points[i])) name->value = "Point " + std::to_string(i + 1);
    }
    scene.updateTransforms();
    editor.scene.select(added);
    editor.scene.markChanged();
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
    // Ground to shape with brushes: flat at first, its heights a file once it is shaped.
    editor.registry.addCreateEntry(nullptr, "3D Object/Terrain", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        Entity entity = scene.create("Terrain");
        scene.add(entity, Transform{});
        scene.add(entity, kit::Terrain{});
        scene.add(entity, MeshRenderer{});
        scene.updateTransforms();
        context.scene.markChanged();
        context.scene.select(entity);
        return entity;
    });
    add("Light/Directional Light", Primitive::DirectionalLight);
    add("Light/Point Light", Primitive::PointLight);
    add("Light/Spot Light", Primitive::SpotLight);
    // A glowing rectangle: a window's light, a ceiling panel, a screen.
    editor.registry.addCreateEntry(nullptr, "Light/Area Light", [](EditorContext& context) {
        Entity made = context.scene.create(Primitive::SpotLight);
        Scene& scene = context.engine.scene;
        if (Light* light = scene.get<Light>(made)) {
            light->type = LightType::Area;
            light->intensity = 6.0f;
            light->range = 15.0f;
        }
        if (Name* name = scene.get<Name>(made)) name->value = "Area Light";
        context.scene.markChanged();
        return made;
    });
    add("Camera", Primitive::Camera);
    // Copies of a model painted on the ground: put the model in its Mesh Renderer, then paint.
    editor.registry.addCreateEntry(nullptr, "3D Object/Foliage", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        Entity entity = scene.create("Foliage");
        scene.add(entity, Transform{});
        kit::Foliage foliage;
        foliage.smallest = 0.25f;               // little cubes until it is given its own model
        foliage.largest = 0.45f;
        scene.add(entity, foliage);
        MeshRenderer renderer;
        renderer.mesh = context.engine.assets().cube();
        scene.add(entity, renderer);
        scene.updateTransforms();
        context.scene.markChanged();
        context.scene.select(entity);
        return entity;
    });
    // A shot, where the Scene view looks from now: the Camera Director glides to it.
    editor.registry.addCreateEntry(nullptr, "Camera Shot", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        Entity entity = scene.create("Camera Shot");
        Transform transform;
        transform.position = context.scene.cameraPosition;
        transform.rotation = context.scene.cameraRotation();
        scene.add(entity, transform);
        scene.add(entity, kit::CameraShot{});
        // The first shot gives the scene's camera a director, so that it films the shots.
        bool hasDirector = false;
        scene.each<kit::CameraDirector>([&](Entity, kit::CameraDirector&) { hasDirector = true; });
        if (!hasDirector) {
            scene.each<Camera>([&](Entity camera, Camera&) {
                if (!hasDirector) {
                    scene.add(camera, kit::CameraDirector{});
                    hasDirector = true;
                }
            });
        }
        scene.updateTransforms();
        context.scene.markChanged();
        context.scene.select(entity);
        return entity;
    });
    // The game's interface: each piece an entity with one component, placed in the Game view.
    auto addInterface = [&editor](const char* path, const char* name, const char* component) {
        editor.registry.addCreateEntry(nullptr, path, [name, component](EditorContext& context) {
            const ComponentType* type = ComponentRegistry::instance().find(component);
            if (!type) return Entity{};
            Scene& scene = context.engine.scene;
            Entity entity = scene.create(name);
            scene.add(entity, Transform{});
            type->add(scene, entity);
            context.scene.markChanged();
            context.scene.select(entity);
            // It is in the Game view that it is seen and put in its place.
            ImGui::SetWindowFocus("Game");
            return entity;
        });
    };
    // The basic pieces, and nothing made of them: a gauge, a menu, a health bar with a frame are put
    // together from these, as the game needs them.
    addInterface("UI/Panel", "Panel", "UIPanel");
    addInterface("UI/Image", "Image", "UIImage");
    addInterface("UI/Text", "Text", "HudText");
    addInterface("UI/Button", "Button", "UIButton");
    addInterface("UI/Toggle", "Toggle", "UIToggle");
    addInterface("UI/Slider", "Slider", "UISlider");
    addInterface("UI/Text Field", "Text Field", "UITextField");
    addInterface("UI/Bar", "Bar", "UIBar");
    addInterface("UI/Arc", "Arc", "UIArc");
    addInterface("UI/Layout", "Layout", "UILayout");
    addInterface("UI/Touch Stick", "Touch Stick", "TouchStick");
    // Curves: an entity with a Spline and its points as children, "Point 1" to "Point 4".
    auto addPath = [&editor](const char* path, const char* name, bool isRoad) {
        editor.registry.addCreateEntry(nullptr, path, [name, isRoad](EditorContext& context) {
            Entity line = context.scene.create(Primitive::Empty);
            Scene& scene = context.engine.scene;
            if (!scene.isAlive(line)) return line;
            if (Name* named = scene.get<Name>(line)) named->value = name;
            kit::Spline spline;
            spline.closed = isRoad;
            scene.add(line, spline);
            const Vec3 corners[4] = {{-6, 0, -6}, {6, 0, -6}, {6, 0, 6}, {-6, 0, 6}};
            for (int k = 0; k < 4; k++) {
                Entity point = scene.create("Point " + std::to_string(k + 1));
                Transform transform;
                transform.position = isRoad ? corners[k] * 2.5f : Vec3{float(k) * 4 - 6, 0, k % 2 == 0 ? 0.0f : -3.0f};
                transform.parent = line;
                scene.add(point, transform);
            }
            if (isRoad) {
                scene.add(line, kit::SplineRoad{});
                scene.add(line, MeshRenderer{});
            }
            scene.updateTransforms();
            context.scene.select(line);
            context.scene.markChanged();
            return line;
        });
    };
    addPath("Paths/Spline", "Spline", false);
    // One particle system, to make into whatever it should be: fire, smoke, rain are made, not picked.
    add("Effects/Particle System", Primitive::ParticleEmitter);

    // A sheet of water with waves: a sea or a lake. Its material's shader is Water, and the Water
    // component moves its surface; things with a Buoyancy float on it.
    editor.registry.addCreateEntry(nullptr, "3D Object/Water", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity water = document.create(Primitive::Plane);
        if (Name* name = scene.get<Name>(water)) name->value = "Water";
        scene.remove<Collider>(water);                  // water stops nothing: things float by their Buoyancy
        if (Transform* transform = scene.get<Transform>(water)) transform->scale = Vec3{400, 1, 400};
        std::shared_ptr<Material> look;
        for (const NamedMaterial& named : document.materials) {
            if (named.name == "Water" && named.material && named.material->isWater) look = named.material;
        }
        if (!look) {
            look = std::make_shared<Material>();
            look->isWater = true;
            // The color it ends up where it is deep, and how murky it is: a clear sea, that shows its bottom near the shore.
            look->baseColor = Vec4{0.02f, 0.13f, 0.2f, 0.3f};
            look->alphaMode = AlphaMode::Blend;
            look->roughness = 0.05f;
            document.materials.push_back(NamedMaterial{"Water", look});
        }
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(water)) {
            renderer->material = look;
            renderer->castsShadows = false;
        }
        scene.add(water, Water{});
        scene.updateTransforms();
        document.select(water);
        document.markChanged();
        return water;
    });

    // ---- starting points: something that already works, to take apart and make one's own

    // Someone to walk with: a character moved by W A S D (or a stick), a camera that follows and turns with the mouse.
    editor.registry.addCreateEntry(nullptr, "Characters/Third Person Player", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity player = document.create(Primitive::Empty);
        if (Name* name = scene.get<Name>(player)) name->value = "Player";
        scene.add(player, Tag{"Player", 0});
        scene.add(player, kit::CharacterController{});
        scene.add(player, kit::CharacterMover{});
        // What triggers see of it (a coin, a zone): a capsule its size, that blocks nothing itself.
        Collider seen = Collider::capsule(0.35f, 0.55f);
        seen.offset = Vec3{0, 0.9f, 0};
        seen.isTrigger = true;
        scene.add(player, seen);
        // Something to look at, until it has a model of its own: put one under the Player and delete this.
        Entity body = document.create(Primitive::Cylinder, player);
        if (Name* name = scene.get<Name>(body)) name->value = "Body";
        scene.remove<Collider>(body);
        if (Transform* transform = scene.get<Transform>(body)) {
            transform->position = Vec3{0, 0.9f, 0};
            transform->scale = Vec3{0.7f, 1.8f, 0.7f};
        }
        // The scene's camera follows it; a scene without one gets one.
        Entity camera = document.gameCamera();
        if (!scene.isAlive(camera)) camera = document.create(Primitive::Camera);
        if (!scene.get<kit::OrbitCamera>(camera)) scene.add(camera, kit::OrbitCamera{});
        scene.updateTransforms();
        document.select(player);
        document.markChanged();
        context.notify("Made a Player: W A S D walks, Shift runs, Space jumps; hold a mouse button and move the mouse to turn the camera. Press Run to try it.");
        return player;
    });

    // The same seen from its own eyes: the scene's camera goes under the player, at the height of its eyes, and the mouse looks about.
    editor.registry.addCreateEntry(nullptr, "Characters/First Person Player", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity player = document.create(Primitive::Empty);
        if (Name* name = scene.get<Name>(player)) name->value = "Player";
        scene.add(player, Tag{"Player", 0});
        scene.add(player, kit::CharacterController{});
        kit::CharacterMover mover;
        mover.walkSpeed = 3.6f;
        mover.runSpeed = 6.4f;
        mover.turnsToFace = false;                  // its view turns it
        scene.add(player, mover);
        Collider seen = Collider::capsule(0.35f, 0.55f);
        seen.offset = Vec3{0, 0.9f, 0};
        seen.isTrigger = true;
        scene.add(player, seen);
        Entity camera = document.gameCamera();
        if (!scene.isAlive(camera)) camera = document.create(Primitive::Camera);
        if (Transform* eye = scene.get<Transform>(camera)) {
            eye->parent = player;
            eye->position = Vec3{0, 1.62f, 0};
            eye->rotation = Quat{};
        }
        if (Camera* lens = scene.get<Camera>(camera)) lens->nearPlane = std::min(lens->nearPlane, 0.05f);
        scene.remove<kit::OrbitCamera>(camera);
        if (!scene.get<kit::FirstPersonLook>(camera)) scene.add(camera, kit::FirstPersonLook{});
        scene.updateTransforms();
        document.select(player);
        document.markChanged();
        context.notify("Made a Player seen from its own eyes: W A S D walks, Shift runs, Space jumps, the mouse looks about (Esc gives the pointer back). Press Run to try it.");
        return player;
    });

    // A game for several machines: who is in it. With a prefab for its players, Run asks whether to host or to join.
    editor.registry.addCreateEntry(nullptr, "Gameplay/Network Session", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity session = document.create(Primitive::Empty);
        if (Name* name = scene.get<Name>(session)) name->value = "Session";
        scene.add(session, kit::NetworkSession{});
        document.select(session);
        document.markChanged();
        context.notify("Made a Session. Give it a Player Prefab (a prefab with a Networked on it) and put Networked on what everyone must see the same; then Run, and host or join.");
        return session;
    });

    // Something to pick up: it turns, and adds one to the score when the Player touches it.
    editor.registry.addCreateEntry(nullptr, "Gameplay/Collectible", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity coin = document.create(Primitive::Sphere);
        if (Name* name = scene.get<Name>(coin)) name->value = "Coin";
        if (Transform* transform = scene.get<Transform>(coin)) {
            transform->position.y += 0.4f;
            transform->scale = Vec3{0.5f, 0.5f, 0.5f};
        }
        if (Collider* collider = scene.get<Collider>(coin)) {
            collider->radius = 0.4f;
            collider->isTrigger = true;
        }
        std::shared_ptr<Material> gold;
        for (const NamedMaterial& named : document.materials) {
            if (named.name == "Gold") gold = named.material;
        }
        if (!gold) {
            gold = std::make_shared<Material>();
            gold->baseColor = Vec4{1.0f, 0.76f, 0.2f, 1.0f};
            gold->metallic = 1.0f;
            gold->roughness = 0.3f;
            document.materials.push_back(NamedMaterial{"Gold", gold});
        }
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(coin)) renderer->material = gold;
        scene.add(coin, kit::Collectible{});
        scene.add(coin, kit::Spin{});
        scene.updateTransforms();
        document.select(coin);
        document.markChanged();
        context.notify("Made a Coin: it adds 1 to the shared value \"score\" when the entity called Player touches it. Its Collectible says who and how much.");
        return coin;
    });

    // An invisible box that does something when the Player walks into it: what, is chosen in its When Then.
    editor.registry.addCreateEntry(nullptr, "Gameplay/Trigger Zone", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity zone = document.create(Primitive::Empty);
        if (Name* name = scene.get<Name>(zone)) name->value = "Trigger Zone";
        Collider box = Collider::box(Vec3{1.5f, 1.0f, 1.5f});
        box.offset = Vec3{0, 1.0f, 0};
        box.isTrigger = true;
        scene.add(zone, box);
        kit::WhenThen rule;
        rule.when = "Something enters";
        rule.who = "Player";
        scene.add(zone, rule);
        scene.updateTransforms();
        document.select(zone);
        document.markChanged();
        context.notify("Made a Trigger Zone: its When Then (in the Inspector) says what happens when the Player enters. Its box shows in green in the Scene view.");
        return zone;
    });

    // The end of the level: a message when the score is reached, and the next scene if there is one.
    editor.registry.addCreateEntry(nullptr, "Gameplay/Goal", [](EditorContext& context) {
        Scene& scene = context.engine.scene;
        SceneDocument& document = context.scene;
        Entity goal = document.create(Primitive::Empty);
        if (Name* name = scene.get<Name>(goal)) name->value = "Goal";
        kit::Goal rule;
        rule.target = 3;
        scene.add(goal, rule);
        document.select(goal);
        document.markChanged();
        context.notify("Made a Goal: when the shared value \"score\" reaches 3 it says \"You win!\". Its Goal says which value, how much, and what comes next.");
        return goal;
    });
}

}  // namespace molten::editor
