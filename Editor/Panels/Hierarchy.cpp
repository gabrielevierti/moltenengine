// The hierarchy: every entity of the scene as a tree, parents above their children.
// Click to select (Cmd adds), drag one onto another to parent it, F2 to rename.
#include "Editor.hpp"
#include "Kit/Effects.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unordered_set>

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

        // ---- the top: add something, or find something
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0))) ImGui::OpenPopup("##create");
        ui::tooltip("Create an entity");
        if (ImGui::BeginPopup("##create")) {
            drawCreateMenu(editor, Entity{});
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search", &filter_);
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
            for (Entity entity : scene.entities()) {
                if (containsIgnoringCase(scene.nameOf(entity), filter_)) drawRow(editor, entity, 0, false);
            }
            if (visible_.empty()) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextDisabled("  Nothing is called that.");
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
                editor.selectedAsset.clear();
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
            // On the upper edge of a row: beside it, under the same parent. On the row itself: into it.
            bool isBetween = ImGui::GetIO().MousePos.y - min.y < kRowHeight * 0.3f;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kEntityPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (isBetween) {
                    float lineX = min.x + 8 + float(depth) * kIndent;
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(lineX, min.y), ImVec2(max.x - 4, min.y), IM_COL32(255, 122, 47, 255), 2.0f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(lineX, min.y), 3.0f, IM_COL32(255, 122, 47, 255));
                }
                if (payload->IsDelivery()) {
                    if (isBetween) {
                        dropEntities(editor, *static_cast<const Entity*>(payload->Data), own ? own->parent : Entity{});
                    } else {
                        dropEntities(editor, *static_cast<const Entity*>(payload->Data), entity);
                        open_.insert(entity.index);
                    }
                }
            }
            ImGui::EndDragDropTarget();
            isDropTarget = !isBetween;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
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
            ImGui::Separator();
            if (ImGui::MenuItem("Frame in View", "F")) editor.frameSelectionRequested = true;
            if (ImGui::MenuItem("Align with View", "Shift+Cmd+F")) editor.alignWithViewRequested = true;
            if (ImGui::MenuItem("Save as Prefab")) savePrefab(editor, entity);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) scene.deleteSelection();
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
            drawList->AddText(ImVec2(textX, middle - ImGui::GetFontSize() * 0.5f), isHidden ? IM_COL32(120, 128, 142, 255) : IM_COL32(223, 227, 234, 255),
                              name.empty() ? "(no name)" : name.c_str());
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
    bool isSceneClosed_ = false;
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
    addInterface("UI/Text", "Text", "HudText");
    addInterface("UI/Button", "Button", "UIButton");
    addInterface("UI/Panel", "Panel", "UIPanel");
    addInterface("UI/Image", "Image", "UIImage");
    addInterface("UI/Bar", "Bar", "UIBar");
    addInterface("UI/Touch Stick", "Touch Stick", "TouchStick");
    addInterface("UI/Touch Button", "Touch Button", "TouchButton");
    add("Effects/Particle Emitter", Primitive::ParticleEmitter);
    for (int i = 0; i < int(effects::Preset::Count); i++) {
        effects::Preset preset = effects::Preset(i);
        editor.registry.addCreateEntry(nullptr, std::string("Effects/") + effects::name(preset), [preset](EditorContext& context) {
            return effects::create(context.engine.scene, preset, Vec3{0, 0.1f, 0});
        });
    }
}

}  // namespace molten::editor
