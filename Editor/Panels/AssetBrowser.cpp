// The asset browser: the project's Assets folder. Folders on the left, what is in the
// open one on the right. Drag things into the scene, onto a material, or into a folder.
#include "Editor.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/SurfaceShader.hpp"
#include "Panels/Widgets.hpp"
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Platform.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <filesystem>

namespace molten::editor {

namespace {

bool containsIgnoringCase(const std::string& text, const std::string& part) {
    auto found = std::search(text.begin(), text.end(), part.begin(), part.end(),
                             [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return found != text.end();
}

bool isLoadableImage(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
}

/// The color and the two letters of the tile that stands for a kind of asset.
struct TileLook {
    ImU32 color;
    const char* letters;
};

TileLook lookOf(AssetType type) {
    switch (type) {
        case AssetType::Folder: return {IM_COL32(214, 168, 84, 255), ""};
        case AssetType::Scene: return {IM_COL32(255, 122, 47, 255), "SC"};
        case AssetType::Mesh: return {IM_COL32(76, 154, 255, 255), "3D"};
        case AssetType::Texture: return {IM_COL32(120, 190, 120, 255), "TX"};
        case AssetType::Audio: return {IM_COL32(255, 122, 47, 255), "AU"};
        case AssetType::Font: return {IM_COL32(150, 160, 175, 255), "Aa"};
        case AssetType::Shader: return {IM_COL32(240, 200, 80, 255), "SH"};
        case AssetType::Script: return {IM_COL32(110, 200, 210, 255), "C+"};
        case AssetType::Text: return {IM_COL32(150, 160, 175, 255), "TXT"};
        case AssetType::Other: return {IM_COL32(110, 118, 130, 255), "?"};
        case AssetType::Graph: return {IM_COL32(255, 122, 47, 255), "VS"};
        case AssetType::Prefab: return {IM_COL32(86, 156, 255, 255), "PRE"};
        case AssetType::Effect: return {IM_COL32(255, 170, 60, 255), "FX"};
        case AssetType::Material: return {IM_COL32(230, 120, 160, 255), "MAT"};
        case AssetType::Animation: return {IM_COL32(255, 175, 90, 255), "ANI"};
        case AssetType::Controller: return {IM_COL32(120, 170, 255, 255), "CTL"};
        case AssetType::Timeline: return {IM_COL32(200, 160, 255, 255), "TL"};
    }
    return {IM_COL32(110, 118, 130, 255), "?"};
}

class AssetBrowserPanel final : public Panel {
    float viewerWidth_ = 360;

public:
    AssetBrowserPanel() { title = "Explorer"; }

    /// What "+ New" makes, for the menu bar's Assets > Create.
    void drawCreateItems(EditorContext& editor) { drawNewItems(editor); }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("Open a project to see its assets.");
            return;
        }
        AssetDatabase& assets = editor.assets;
        // The folder may have been renamed or deleted, here or in the Finder.
        if (!editor.currentFolder.empty() && !assets.find(editor.currentFolder)) editor.currentFolder.clear();

        keepChoiceInStep(editor);
        handleKeys(editor);
        handleRequest(editor);
        drawToolbar(editor);
        ImGui::Separator();

        float treeWidth = std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.3f);
        ImGui::BeginChild("##folders", ImVec2(treeWidth, 0), ImGuiChildFlags_ResizeX);
        drawFolderNode(editor, "");
        // Below the tree too: a right click makes something in the folder that is shown.
        if (ImGui::BeginPopupContextWindow("##tree-menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("New")) {
                drawNewItems(editor);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import...")) importFiles(editor);
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        // What is chosen is shown at the right, in the viewer; nothing chosen, no viewer.
        const AssetInfo* chosen = editor.selectedAsset.empty() ? nullptr : assets.find(editor.selectedAsset);
        float room = ImGui::GetContentRegionAvail().x;
        viewerWidth_ = std::clamp(viewerWidth_, 220.0f, std::max(220.0f, room - 200.0f));
        ImGui::BeginChild("##contents", ImVec2(chosen ? room - viewerWidth_ - 8 : 0, 0));
        drawContents(editor);
        ImGui::EndChild();
        if (chosen) {
            ImGui::SameLine(0, 0);
            ImVec2 at = ImGui::GetCursorScreenPos();
            float height = ImGui::GetContentRegionAvail().y;
            ImGui::InvisibleButton("##viewer-bar", ImVec2(8, height));
            bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
            if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (active) viewerWidth_ -= ImGui::GetIO().MouseDelta.x;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + 4, at.y), ImVec2(at.x + 4, at.y + height),
                                                ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator), active || hovered ? 2.0f : 1.0f);
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (ImGui::BeginChild("##viewer", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding)) drawAssetViewer(editor);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        drawDeleteConfirmation(editor);
    }

private:
    void fail(EditorContext& editor, const std::string& error) {
        if (!error.empty()) editor.notify(error, ConsoleMessage::Level::Error);
    }

    void importFiles(EditorContext& editor) {
        std::vector<std::string> files = platform::chooseFiles("Choose files to copy into the project", {}, true);
        int count = 0;
        for (const std::string& file : files) {
            std::string error;
            std::string imported = editor.assets.importFile(file, editor.currentFolder, &error);
            fail(editor, error);
            if (!imported.empty()) {
                count++;
                editor.selectedAsset = imported;
            }
        }
        if (count > 0) editor.notify("Imported " + std::to_string(count) + (count == 1 ? " file" : " files"));
    }

    void createFolder(EditorContext& editor) {
        std::string error;
        std::string created = editor.assets.createFolder(editor.currentFolder, "New Folder", &error);
        fail(editor, error);
        if (!created.empty()) startRenaming(editor, created);
    }

    void startRenaming(EditorContext& editor, const std::string& path) {
        const AssetInfo* info = editor.assets.find(path);
        if (!info) return;
        renaming_ = path;
        renameText_ = info->isFolder() ? info->name : info->stem();     // the extension is not for renaming by accident
        renameNeedsFocus_ = true;
        editor.selectedAsset = path;
    }

    void finishRenaming(EditorContext& editor) {
        const AssetInfo* info = editor.assets.find(renaming_);
        std::string oldPath = renaming_;
        renaming_.clear();
        if (!info || renameText_.empty()) return;
        std::string extension = info->isFolder() ? std::string() : std::filesystem::path(info->name).extension().string();
        std::string newName = renameText_ + extension;
        if (newName == info->name) return;
        std::string error;
        std::string newPath = editor.assets.rename(oldPath, newName, &error);
        fail(editor, error);
        if (newPath.empty()) return;
        follow(editor, oldPath, newPath);
        editor.selectedAsset = newPath;
        chosen_ = {newPath};
    }

    // ---------------------------------------------------------------- moving, and what follows a move

    /// `path` with `from` at its start replaced by `to`: the path of a thing inside a folder that moved.
    static bool replaceStart(std::string& path, const std::string& from, const std::string& to) {
        if (path == from) {
            path = to;
            return true;
        }
        if (path.size() > from.size() && path.compare(0, from.size(), from) == 0 && path[from.size()] == '/') {
            path = to + path.substr(from.size());
            return true;
        }
        return false;
    }

    /// Everything that named `from` names `to` now: the files that use it, the open scene, the
    /// project's first scene and icon, and what the editor itself has open or chosen.
    void follow(EditorContext& editor, const std::string& from, const std::string& to) {
        int files = editor.assets.retargetReferences(from, to);
        editor.scene.retargetAsset(from, to);
        bool projectChanged = replaceStart(editor.project.startScene, from, to);
        projectChanged |= replaceStart(editor.project.icon, from, to);
        if (projectChanged) editor.project.save();
        std::string sceneFile = editor.scene.filePath, assetsFolder = editor.project.assetsDirectory();
        if (sceneFile.size() > assetsFolder.size() + 1 && sceneFile.compare(0, assetsFolder.size() + 1, assetsFolder + "/") == 0) {
            std::string relative = sceneFile.substr(assetsFolder.size() + 1);
            if (replaceStart(relative, from, to)) editor.scene.filePath = editor.assets.absolutePath(relative);
        }
        replaceStart(editor.selectedAsset, from, to);
        replaceStart(editor.currentFolder, from, to);
        for (std::string& path : chosen_) replaceStart(path, from, to);
        for (std::string& path : cut_) replaceStart(path, from, to);
        if (files > 0) editor.notify("Updated " + std::to_string(files) + (files == 1 ? " file that uses " : " files that use ") + to);
    }

    std::string moveInto(EditorContext& editor, const std::string& path, const std::string& folder) {
        std::string error;
        std::string newPath = editor.assets.move(path, folder, &error);
        fail(editor, error);
        if (!newPath.empty() && newPath != path) follow(editor, path, newPath);
        return newPath;
    }

    /// Moves them all into `folder`. A thing inside a folder that moves too goes with its folder.
    void moveAll(EditorContext& editor, std::vector<std::string> paths, const std::string& folder) {
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        std::vector<std::string> outermost;
        for (const std::string& path : paths) {
            bool isInside = false;
            for (const std::string& other : outermost) isInside |= path.size() > other.size() && path.compare(0, other.size() + 1, other + "/") == 0;
            if (!isInside) outermost.push_back(path);
        }
        int moved = 0;
        for (const std::string& path : outermost) {
            if (!editor.assets.find(path)) continue;
            std::string parent = std::filesystem::path(path).parent_path().string();
            if (parent == folder) continue;
            if (!moveInto(editor, path, folder).empty()) moved++;
        }
        if (moved > 1) editor.notify("Moved " + std::to_string(moved) + " items to " + (folder.empty() ? std::string("Assets") : folder));
    }

    void acceptMove(EditorContext& editor, const std::string& folder) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kAssetPayload)) {
            std::string path(static_cast<const char*>(payload->Data));
            // The tile dragged, or all the chosen ones if it was one of them.
            bool isGroup = std::find(dragged_.begin(), dragged_.end(), path) != dragged_.end();
            moveAll(editor, isGroup ? dragged_ : std::vector<std::string>{path}, folder);
        }
        ImGui::EndDragDropTarget();
    }

    /// Every folder of the project, for Move To.
    void drawMoveToItems(EditorContext& editor) {
        std::vector<std::string> folders = {""};
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.isFolder()) folders.push_back(asset.path);
        }
        std::sort(folders.begin(), folders.end());
        for (const std::string& folder : folders) {
            // Not into itself or into a folder inside it.
            bool isOwn = false;
            for (const std::string& path : chosen_) isOwn |= folder == path || folder.compare(0, path.size() + 1, path + "/") == 0;
            int depth = int(std::count(folder.begin(), folder.end(), '/')) + (folder.empty() ? 0 : 1);
            std::string label = std::string(size_t(depth) * 3, ' ') + (folder.empty() ? std::string("Assets") : std::filesystem::path(folder).filename().string());
            ImGui::PushID(folder.c_str());
            if (ImGui::MenuItem(label.c_str(), nullptr, false, !isOwn)) moveAll(editor, chosen_, folder);
            ImGui::PopID();
        }
    }

    // ---------------------------------------------------------------- choosing several, and the keys

    bool isChosen(const std::string& path) const { return std::find(chosen_.begin(), chosen_.end(), path) != chosen_.end(); }

    /// The Viewer shows one thing (editor.selectedAsset); the list may have more chosen. When
    /// something else chose an asset (a field's "show in Explorer"), the list follows it.
    void keepChoiceInStep(EditorContext& editor) {
        chosen_.erase(std::remove_if(chosen_.begin(), chosen_.end(), [&](const std::string& path) { return !editor.assets.find(path); }), chosen_.end());
        cut_.erase(std::remove_if(cut_.begin(), cut_.end(), [&](const std::string& path) { return !editor.assets.find(path); }), cut_.end());
        if (editor.selectedAsset.empty()) chosen_.clear();
        else if (!isChosen(editor.selectedAsset)) chosen_ = {editor.selectedAsset};
    }

    void choose(EditorContext& editor, const std::string& path) {
        ImGuiIO& io = ImGui::GetIO();
        bool adds = io.KeySuper || io.KeyCtrl;
        if (io.KeyShift && !anchor_.empty()) {
            // Everything between the last one clicked and this one, as they are shown.
            auto from = std::find(shownPaths_.begin(), shownPaths_.end(), anchor_);
            auto to = std::find(shownPaths_.begin(), shownPaths_.end(), path);
            if (from != shownPaths_.end() && to != shownPaths_.end()) {
                if (from > to) std::swap(from, to);
                if (!adds) chosen_.clear();
                for (auto it = from; it <= to; ++it) {
                    if (!isChosen(*it)) chosen_.push_back(*it);
                }
                editor.selectedAsset = path;
                return;
            }
        }
        if (adds) {
            if (isChosen(path)) chosen_.erase(std::find(chosen_.begin(), chosen_.end(), path));
            else chosen_.push_back(path);
            editor.selectedAsset = chosen_.empty() ? std::string() : (isChosen(path) ? path : chosen_.back());
        } else {
            chosen_ = {path};
            editor.selectedAsset = path;
        }
        anchor_ = path;
    }

    void cutChosen(EditorContext& editor) {
        if (chosen_.empty()) return;
        cut_ = chosen_;
        editor.notify(std::string(cut_.size() == 1 ? "Cut " + cut_.front() : "Cut " + std::to_string(cut_.size()) + " items") +
                      ". Paste in another folder moves them there.");
    }

    void pasteInto(EditorContext& editor, const std::string& folder) {
        if (cut_.empty()) return;
        std::vector<std::string> paths = std::move(cut_);
        cut_.clear();
        moveAll(editor, paths, folder);
    }

    /// What the Assets and Edit menus asked for (EditorCommands.cpp), about what is chosen here.
    void handleRequest(EditorContext& editor) {
        if (editor.assetRequest.empty()) return;
        std::string request = std::move(editor.assetRequest);
        editor.assetRequest.clear();
        AssetDatabase& assets = editor.assets;
        if (request == "import") importFiles(editor);
        else if (request == "rename" && chosen_.size() == 1) startRenaming(editor, chosen_.front());
        else if (request == "delete" && !chosen_.empty()) pendingDelete_ = chosen_;
        else if (request == "cut") cutChosen(editor);
        else if (request == "paste") pasteInto(editor, editor.currentFolder);
        else if (request == "selectAll") chosen_ = shownPaths_;
        else if (request == "reveal") platform::revealInFinder(assets.absolutePath(chosen_.empty() ? editor.currentFolder : chosen_.front()));
        else if (request == "open" && !chosen_.empty()) {
            const AssetInfo* asset = assets.find(chosen_.front());
            if (!asset) return;
            if (asset->isFolder()) editor.currentFolder = asset->path;
            else if (asset->type == AssetType::Scene) editor.requestOpenScene(assets.absolutePath(asset->path));
            else if (asset->type == AssetType::Graph) editor.editGraph(asset->path);
            else platform::openWithDefaultApplication(assets.absolutePath(asset->path));
        }
    }

    void handleKeys(EditorContext& editor) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
        editor.explorerHasKeys = true;
        if (ImGui::GetIO().WantTextInput || !renaming_.empty()) return;
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        // Focused shortcuts: here they come before the editor's own, which are for the scene.
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) chosen_ = shownPaths_;
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_X)) cutChosen(editor);
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) pasteInto(editor, editor.currentFolder);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cut_.clear();
        if (chosen_.empty()) return;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) pendingDelete_ = chosen_;
        if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_F2, false)) && chosen_.size() == 1) startRenaming(editor, chosen_.front());
    }

    /// What can be made: the same list under + New and under a right click anywhere, made in
    /// `folder` (the one shown, or the one that was right-clicked), which is then shown.
    void drawNewItems(EditorContext& editor) { drawNewItems(editor, editor.currentFolder); }
    void drawNewItems(EditorContext& editor, const std::string& folder) {
        if (ImGui::MenuItem("Folder")) {
            editor.currentFolder = folder;
            createFolder(editor);
        }
        if (ImGui::MenuItem("Material")) {
            if (std::shared_ptr<Material> material = createMaterialAsset(editor, folder, "New Material", nullptr)) {
                editor.selectedAsset = material->assetName;
                editor.currentFolder = folder;
                startRenaming(editor, material->assetName);
            }
        }
        if (ImGui::MenuItem("Script")) editor.wantsNewScript = true;        // C++: asks for its name, makes it in Scripts and opens it
        if (ImGui::MenuItem("Visual Script")) {
            std::string path = editor.assets.uniquePath(folder, "New Script", ".graph");
            if (text::writeFile(editor.assets.absolutePath(path), script::Graph::starter().toText())) {
                editor.assets.refresh();
                editor.currentFolder = folder;
                startRenaming(editor, path);
            }
        }
        if (ImGui::MenuItem("Shader")) {
            // A surface shader to write by hand: it starts as one that works, to change.
            std::string path = editor.assets.uniquePath(folder, "New Shader", ".shader");
            if (text::writeFile(editor.assets.absolutePath(path), newSurfaceShaderText())) {
                editor.assets.refresh();
                editor.currentFolder = folder;
                startRenaming(editor, path);
            }
        }
        if (ImGui::MenuItem("Animator Controller")) {
            // States and transitions to draw in the Animator window; put it in an Animator's Controller.
            AnimatorController controller;
            std::string path = editor.assets.uniquePath(folder, "New Controller", ".animator");
            if (text::writeFile(editor.assets.absolutePath(path), controller.toText())) {
                editor.assets.refresh();
                editor.currentFolder = folder;
                startRenaming(editor, path);
            }
        }
        if (ImGui::MenuItem("Scene")) {
            // A new file with what every new scene starts with. The scene that is open is left alone.
            std::string path = editor.assets.uniquePath(folder, "New Scene", ".scene");
            if (text::writeFile(editor.assets.absolutePath(path), editor.scene.defaultSceneText())) {
                editor.assets.refresh();
                editor.currentFolder = folder;
                startRenaming(editor, path);
            }
        }
    }

    void drawToolbar(EditorContext& editor) {
        if (ImGui::Button("+ New")) ImGui::OpenPopup("##new");
        if (ImGui::BeginPopup("##new")) {
            drawNewItems(editor);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import...")) importFiles(editor);
        ImGui::SameLine();

        // The path to the open folder; each part is a button and a place to drop things.
        if (ImGui::SmallButton("Assets")) editor.currentFolder.clear();
        acceptMove(editor, "");
        size_t start = 0;
        while (start < editor.currentFolder.size()) {
            size_t slash = editor.currentFolder.find('/', start);
            if (slash == std::string::npos) slash = editor.currentFolder.size();
            std::string part = editor.currentFolder.substr(start, slash - start);
            std::string upTo = editor.currentFolder.substr(0, slash);
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0, 2);
            ImGui::PushID(int(slash));
            if (ImGui::SmallButton(part.c_str())) editor.currentFolder = upTo;
            acceptMove(editor, upTo);
            ImGui::PopID();
            start = slash + 1;
        }

        ImGui::SameLine();
        float searchWidth = 180, sizeWidth = 90;
        float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - searchWidth - sizeWidth - ImGui::GetStyle().ItemSpacing.x));
        ImGui::SetNextItemWidth(sizeWidth);
        ImGui::SliderFloat("##size", &tileSize_, 48.0f, 160.0f, "", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size of the pictures: all the way down, a list");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##search", "Search all assets", &filter_);
    }

    void drawFolderNode(EditorContext& editor, const std::string& folder) {
        std::vector<const AssetInfo*> subfolders;
        for (const AssetInfo* child : editor.assets.children(folder)) {
            if (child->isFolder()) subfolders.push_back(child);
        }
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (subfolders.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (folder.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (editor.currentFolder == folder) flags |= ImGuiTreeNodeFlags_Selected;
        std::string name = folder.empty() ? std::string("Assets") : std::filesystem::path(folder).filename().string();
        ImGui::PushID(folder.c_str());
        bool isOpen = ImGui::TreeNodeEx("##folder", flags, "%s", name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) editor.currentFolder = folder;
        acceptMove(editor, folder);
        if (ImGui::BeginPopupContextItem("##folder-menu")) {
            if (ImGui::BeginMenu("New")) {
                drawNewItems(editor, folder);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import...")) {
                editor.currentFolder = folder;
                importFiles(editor);
            }
            if (!cut_.empty() && ImGui::MenuItem("Paste Here", "Cmd+V")) pasteInto(editor, folder);
            ImGui::Separator();
            if (ImGui::MenuItem("Reveal in Finder")) platform::revealInFinder(editor.assets.absolutePath(folder));
            if (!folder.empty()) {
                if (ImGui::MenuItem("Rename")) startRenaming(editor, folder);
                ImGui::Separator();
                if (ImGui::MenuItem("Move to Trash")) pendingDelete_ = {folder};
            }
            ImGui::EndPopup();
        }
        if (isOpen) {
            // Copies of the paths: moving something into a folder rescans, and the pointers go stale.
            std::vector<std::string> paths;
            for (const AssetInfo* child : subfolders) paths.push_back(child->path);
            for (const std::string& path : paths) drawFolderNode(editor, path);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void drawContents(EditorContext& editor) {
        AssetDatabase& assets = editor.assets;
        // Paths, not pointers: an action on one tile may rescan while the others are still to draw.
        std::vector<std::string> paths;
        if (filter_.empty()) {
            for (const AssetInfo* child : assets.children(editor.currentFolder)) paths.push_back(child->path);
        } else {
            for (const AssetInfo& asset : assets.assets()) {
                if (containsIgnoringCase(asset.name, filter_)) paths.push_back(asset.path);
            }
        }

        shownPaths_ = paths;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float tileWidth = isList() ? ImGui::GetContentRegionAvail().x : tileSize_ + 16;
        int columns = isList() ? 1 : std::max(1, int((ImGui::GetContentRegionAvail().x + spacing) / (tileWidth + spacing)));
        if (isList()) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
        int column = 0;
        for (const std::string& path : paths) {
            const AssetInfo* asset = assets.find(path);
            if (!asset) continue;
            if (column > 0) ImGui::SameLine();
            drawTile(editor, *asset, tileWidth);
            column = (column + 1) % columns;
        }
        if (isList()) ImGui::PopStyleVar();
        if (paths.empty()) ImGui::TextDisabled(filter_.empty() ? "This folder is empty. Use Import, or drop files into it in the Finder." : "Nothing matches.");

        // The space that is left: a click deselects, a right click offers what can be made here.
        ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##rest", ImVec2(std::max(rest.x, 1.0f), std::max(rest.y, 30.0f)));
        if (ImGui::IsItemClicked()) {
            editor.selectedAsset.clear();
            chosen_.clear();
        }
        acceptMove(editor, editor.currentFolder);
        // A right click anywhere that is not a tile: what can be made here.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
            (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            ImGui::OpenPopup("##rest-menu");
        }
        if (ImGui::BeginPopup("##rest-menu")) {
            if (ImGui::BeginMenu("New")) {
                drawNewItems(editor);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import...")) importFiles(editor);
            if (!cut_.empty() && ImGui::MenuItem("Paste", "Cmd+V")) pasteInto(editor, editor.currentFolder);
            ImGui::Separator();
            if (ImGui::MenuItem("Reveal in Finder")) platform::revealInFinder(assets.absolutePath(editor.currentFolder));
            ImGui::EndPopup();
        }
    }

    void drawTile(EditorContext& editor, const AssetInfo& asset, float tileWidth) {
        // Everything needed after the tile's actions is copied now: an action may rescan.
        const std::string path = asset.path, name = asset.name;
        const AssetType type = asset.type;
        const uintmax_t assetSize = asset.size;
        const bool isFolder = asset.isFolder();

        ImGui::PushID(path.c_str());
        ImGui::BeginGroup();
        // At the smallest size, as in Unity, a list: a small picture and the name on one row.
        const bool asList = isList();
        const float t = asList ? 18.0f : tileSize_;
        float textHeight = asList ? ImGui::GetTextLineHeight() : ImGui::GetTextLineHeight() * 2 + 4;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 size = asList ? ImVec2(tileWidth, 24) : ImVec2(tileWidth, tileSize_ + textHeight + 12);
        bool isSelected = isChosen(path);
        if (ImGui::Selectable("##tile", isSelected, ImGuiSelectableFlags_AllowDoubleClick, size)) {
            choose(editor, path);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && chosen_.size() == 1) {
                if (isFolder) {
                    editor.currentFolder = path;
                    filter_.clear();
                } else if (type == AssetType::Scene) {
                    editor.requestOpenScene(editor.assets.absolutePath(path));
                } else if (type == AssetType::Graph) {
                    editor.editGraph(path);
                } else if (type == AssetType::Controller) {
                    editor.openPanel("Animator");
                } else if (type == AssetType::Animation) {
                    editor.openPanel("Timeline");
                } else if (type == AssetType::Timeline) {
                    editor.openPanel("Timeline");
                } else {
                    platform::openWithDefaultApplication(editor.assets.absolutePath(path));
                }
            }
        }
        bool isHovered = ImGui::IsItemHovered();

        if (ImGui::BeginDragDropSource()) {
            // One of the chosen ones carries them all (into a folder); another carries itself.
            dragged_ = isChosen(path) ? chosen_ : std::vector<std::string>{path};
            ImGui::SetDragDropPayload(ui::kAssetPayload, path.c_str(), path.size() + 1);
            if (dragged_.size() > 1) ImGui::Text("%s and %d more", name.c_str(), int(dragged_.size()) - 1);
            else ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        if (isFolder) acceptMove(editor, path);

        if (ImGui::BeginPopupContextItem("##menu")) {
            // A right click on one that is not chosen chooses it alone; on a chosen one, it acts on them all.
            if (!isChosen(path)) chosen_ = {path};
            editor.selectedAsset = path;
            if (ImGui::BeginMenu(isFolder ? "New Inside" : "New")) {
                drawNewItems(editor, isFolder ? path : editor.currentFolder);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (type == AssetType::Scene && ImGui::MenuItem("Open Scene")) editor.requestOpenScene(editor.assets.absolutePath(path));
            // A variant: the prefab with some values changed, written as the changes (see SceneFile.hpp).
            if (type == AssetType::Prefab && ImGui::MenuItem("Create Variant")) {
                std::filesystem::path original(path);
                std::string stem = original.stem().string();
                std::string made = (original.parent_path() / (stem + " Variant.prefab")).string();
                for (int number = 2; std::filesystem::exists(editor.assets.absolutePath(made)); number++) {
                    made = (original.parent_path() / (stem + " Variant " + std::to_string(number) + ".prefab")).string();
                }
                std::string variant = "molten-variant 1\nbase \"" + path + "\"\nname \"" + stem + " Variant\"\n"
                                      "# What it changes, one per line (the entity by its path of names below the root, \"\" for the root):\n"
                                      "# set \"\" NavAgent speed \"6\"\n# set \"Body\" MeshRenderer material \"Materials/Red.material\"\n"
                                      "# set \"Body\" Transform scale \"1.2 1.2 1.2\"\n";
                if (text::writeFile(editor.assets.absolutePath(made), variant)) {
                    editor.assets.refresh();
                    editor.selectedAsset = made;
                    editor.notify("Made " + made + ": its changes are lines in the file (Open with Default Application)");
                }
            }
            if (type == AssetType::Graph && ImGui::MenuItem("Edit Script")) editor.editGraph(path);
            if (type == AssetType::Graph && ImGui::MenuItem("Convert to C++ Script")) convertGraphToCpp(editor, path);
            if (type == AssetType::Script && std::filesystem::path(path).extension() == ".cpp" && ImGui::MenuItem("Convert to Visual Script")) {
                std::string made = convertCppToGraph(editor, path);
                if (!made.empty()) editor.editGraph(made);
            }
            if (ImGui::MenuItem("Rename", "Return", false, chosen_.size() == 1)) startRenaming(editor, path);
            if (ImGui::MenuItem("Reveal in Finder")) platform::revealInFinder(editor.assets.absolutePath(path));
            if (!isFolder && ImGui::MenuItem("Open with Default Application")) platform::openWithDefaultApplication(editor.assets.absolutePath(path));
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Cmd+X")) cutChosen(editor);
            if (isFolder && !cut_.empty() && ImGui::MenuItem("Paste Into", "Cmd+V")) pasteInto(editor, path);
            if (ImGui::BeginMenu("Move To")) {
                drawMoveToItems(editor);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            std::string trashLabel = chosen_.size() > 1 ? "Move " + std::to_string(chosen_.size()) + " Items to Trash" : std::string("Move to Trash");
            if (ImGui::MenuItem(trashLabel.c_str(), "Delete")) pendingDelete_ = chosen_;
            ImGui::EndPopup();
        }

        // The picture: the texture itself, or a tile in the color of the asset's kind.
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pictureMin = asList ? ImVec2(origin.x + 6, origin.y + 3) : ImVec2(origin.x + 8, origin.y + 6);
        ImVec2 pictureMax(pictureMin.x + t, pictureMin.y + t);
        std::shared_ptr<Texture> texture;
        if (type == AssetType::Texture && isLoadableImage(path)) texture = editor.engine.assets().texture(path);
        TileLook look = lookOf(type);
        if (texture) {
            drawList->AddImageRounded(ImGuiLayer::textureId(texture.get()), pictureMin, pictureMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
        } else if (type == AssetType::Material && editor.engine.assets().material(path)) {
            drawMaterialPreview(drawList, ImVec2((pictureMin.x + pictureMax.x) * 0.5f, (pictureMin.y + pictureMax.y) * 0.5f), t * 0.42f,
                                *editor.engine.assets().material(path));
        } else if (!isFolder && drawAssetPreview(editor, drawList, pictureMin, pictureMax, path, type, assetSize, look.color)) {
            // drawn
        } else if (isFolder) {
            // Everything a share of the tile's size, so that it is the same folder at any size.
            float left = pictureMin.x + t * 0.08f, right = pictureMax.x - t * 0.08f;
            float top = pictureMin.y + t * 0.26f, bottom = pictureMax.y - t * 0.14f;
            float round = t * 0.06f;
            ImU32 back = IM_COL32(184, 142, 66, 255);
            drawList->AddRectFilled(ImVec2(left, top - t * 0.1f), ImVec2(left + t * 0.36f, top + round), back, round, ImDrawFlags_RoundCornersTop);       // the tab
            drawList->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), back, round);
            drawList->AddRectFilled(ImVec2(left, top + t * 0.07f), ImVec2(right, bottom), look.color, round);                                           // the front
        } else {
            ImVec2 min(pictureMin.x + t * 0.14f, pictureMin.y + (asList ? 0 : 2)), max(pictureMax.x - t * 0.14f, pictureMax.y - (asList ? 0 : 2));
            drawList->AddRectFilled(min, max, IM_COL32(40, 44, 51, 255), 5.0f);
            drawList->AddRectFilled(min, ImVec2(max.x, min.y + 5), look.color, 5.0f, ImDrawFlags_RoundCornersTop);
            ImVec2 letters = ImGui::CalcTextSize(look.letters);
            if (!asList) drawList->AddText(ImVec2((min.x + max.x - letters.x) * 0.5f, (min.y + max.y - letters.y) * 0.5f), look.color, look.letters);
        }

        // The name: an input while renaming, otherwise up to two lines, cut with "...".
        ImVec2 textMin = asList ? ImVec2(pictureMax.x + 10, origin.y + (size.y - textHeight) * 0.5f) : ImVec2(origin.x + 4, pictureMax.y + 6);
        if (renaming_ == path) {
            ImGui::SetCursorScreenPos(textMin);
            ImGui::SetNextItemWidth(asList ? std::min(tileWidth - 40, 320.0f) : tileWidth - 8);
            if (renameNeedsFocus_) {
                ImGui::SetKeyboardFocusHere();
                renameNeedsFocus_ = false;
            }
            bool done = ImGui::InputText("##rename", &renameText_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done) {
                finishRenaming(editor);
            } else if (ImGui::IsItemDeactivated()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) renaming_.clear();
                else finishRenaming(editor);
            }
        } else {
            ImVec4 clip(textMin.x, textMin.y, origin.x + tileWidth - 4, textMin.y + textHeight);
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textMin, ImGui::GetColorU32(ImGuiCol_Text), name.c_str(), nullptr,
                              asList ? 0.0f : tileWidth - 8, &clip);
            if (asList && !isFolder) {
                // What it is, in the column after the names.
                const char* kind = AssetDatabase::typeName(type);
                float x = std::max(textMin.x + ImGui::CalcTextSize(name.c_str()).x + 24, origin.x + 300);
                if (x + 80 < origin.x + tileWidth) drawList->AddText(ImVec2(x, textMin.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), kind);
            }
            if (isHovered) ImGui::SetItemTooltip("%s", name.c_str());
        }
        // Cut, waiting for Paste: faded, as in the Finder.
        if (std::find(cut_.begin(), cut_.end(), path) != cut_.end()) {
            drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_WindowBg, 0.55f), 4.0f);
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }

    void drawDeleteConfirmation(EditorContext& editor) {
        if (!pendingDelete_.empty() && !ImGui::IsPopupOpen("Move to Trash?")) ImGui::OpenPopup("Move to Trash?");
        if (!ImGui::BeginPopupModal("Move to Trash?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        if (pendingDelete_.size() == 1) {
            ImGui::Text("Move \"%s\" to the Trash?", pendingDelete_.front().c_str());
        } else {
            ImGui::Text("Move these %d items to the Trash?", int(pendingDelete_.size()));
            for (size_t i = 0; i < pendingDelete_.size() && i < 6; i++) ImGui::BulletText("%s", pendingDelete_[i].c_str());
            if (pendingDelete_.size() > 6) ImGui::TextDisabled("and %d more", int(pendingDelete_.size()) - 6);
        }
        ImGui::TextDisabled("Scenes that use them will no longer find them. They can be put back from the Trash.");
        ImGui::Spacing();
        bool confirms = ImGui::Button("Move to Trash", ImVec2(140, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        bool cancels = ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (confirms) {
            int moved = 0;
            for (const std::string& path : pendingDelete_) {
                if (!editor.assets.find(path)) continue;        // inside a folder that went first
                if (platform::moveToTrash(editor.assets.absolutePath(path))) {
                    moved++;
                    // Its .meta goes with it, or the refresh would find it alone.
                    platform::moveToTrash(editor.assets.absolutePath(path) + ".meta");
                } else {
                    editor.notify("Cannot move " + path + " to the Trash", ConsoleMessage::Level::Error);
                }
            }
            if (moved > 0) editor.notify(moved == 1 ? "Moved " + pendingDelete_.front() + " to the Trash" : "Moved " + std::to_string(moved) + " items to the Trash");
            editor.assets.refresh();
            if (!editor.assets.find(editor.selectedAsset)) editor.selectedAsset.clear();
            chosen_.clear();
            pendingDelete_.clear();
            ImGui::CloseCurrentPopup();
        } else if (cancels) {
            pendingDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    std::string filter_;
    float tileSize_ = 48.0f;                // the smallest: a list, the most to see at once
    bool isList() const { return tileSize_ <= 50.0f; }
    std::string renaming_, renameText_;
    bool renameNeedsFocus_ = false;
    std::vector<std::string> pendingDelete_;    // waiting for "Move to Trash?"
    std::vector<std::string> chosen_;           // everything chosen; editor.selectedAsset is the one the Viewer shows
    std::string anchor_;                        // where a Shift-click range starts
    std::vector<std::string> shownPaths_;       // the tiles in the order shown
    std::vector<std::string> dragged_;          // what the drag under way carries
    std::vector<std::string> cut_;              // waiting for Paste
};

}  // namespace

namespace {
AssetBrowserPanel* theBrowser = nullptr;        // the one the editor has, for the menu bar
}

void registerAssetBrowserPanel(EditorContext& editor) {
    auto panel = std::make_unique<AssetBrowserPanel>();
    theBrowser = panel.get();
    editor.registry.addPanel(nullptr, std::move(panel));
}

void drawAssetCreateMenu(EditorContext& editor) {
    if (theBrowser && editor.hasProject) theBrowser->drawCreateItems(editor);
}

}  // namespace molten::editor
