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
public:
    AssetBrowserPanel() { title = "Explorer"; }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("Open a project to see its assets.");
            return;
        }
        AssetDatabase& assets = editor.assets;
        // The folder may have been renamed or deleted, here or in the Finder.
        if (!editor.currentFolder.empty() && !assets.find(editor.currentFolder)) editor.currentFolder.clear();

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
        ImGui::BeginChild("##contents", ImVec2(0, 0));
        drawContents(editor);
        ImGui::EndChild();

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
        bool wasOpenScene = editor.scene.filePath == editor.assets.absolutePath(oldPath);
        std::string error;
        std::string newPath = editor.assets.rename(oldPath, newName, &error);
        fail(editor, error);
        if (newPath.empty()) return;
        editor.selectedAsset = newPath;
        if (editor.currentFolder == oldPath) editor.currentFolder = newPath;
        if (wasOpenScene) editor.scene.filePath = editor.assets.absolutePath(newPath);
    }

    void moveInto(EditorContext& editor, const std::string& path, const std::string& folder) {
        bool wasOpenScene = editor.scene.filePath == editor.assets.absolutePath(path);
        std::string error;
        std::string newPath = editor.assets.move(path, folder, &error);
        fail(editor, error);
        if (newPath.empty()) return;
        if (editor.selectedAsset == path) editor.selectedAsset = newPath;
        if (wasOpenScene) editor.scene.filePath = editor.assets.absolutePath(newPath);
    }

    void acceptMove(EditorContext& editor, const std::string& folder) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kAssetPayload)) {
            moveInto(editor, std::string(static_cast<const char*>(payload->Data)), folder);
        }
        ImGui::EndDragDropTarget();
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
        if (ImGui::MenuItem("Effect")) {
            // A particle effect to shape in the Viewer: it starts as a fire.
            std::string path = editor.assets.uniquePath(folder, "New Effect", ".effect");
            if (text::writeFile(editor.assets.absolutePath(path), writeEffect(effectPreset("Fire")))) {
                editor.assets.refresh();
                editor.currentFolder = folder;
                startRenaming(editor, path);
            }
        }
        if (ImGui::MenuItem("Animation Clip")) {
            // Keys to set in the Animation window, on whatever entity plays it.
            AnimationClip clip;
            std::string path = editor.assets.uniquePath(folder, "New Animation", ".anim");
            if (text::writeFile(editor.assets.absolutePath(path), clip.toText())) {
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
            ImGui::Separator();
            if (ImGui::MenuItem("Reveal in Finder")) platform::revealInFinder(editor.assets.absolutePath(folder));
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
        if (ImGui::IsItemClicked()) editor.selectedAsset.clear();
        if (ImGui::BeginPopupContextWindow("##rest-menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("New")) {
                drawNewItems(editor);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Import...")) importFiles(editor);
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
        bool isSelected = editor.selectedAsset == path;
        if (ImGui::Selectable("##tile", isSelected, ImGuiSelectableFlags_AllowDoubleClick, size)) {
            editor.selectedAsset = path;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
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
                    editor.openPanel("Animation");
                } else if (type == AssetType::Timeline) {
                    editor.openPanel("Timeline");
                } else {
                    platform::openWithDefaultApplication(editor.assets.absolutePath(path));
                }
            }
        }
        bool isHovered = ImGui::IsItemHovered();

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(ui::kAssetPayload, path.c_str(), path.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        if (isFolder) acceptMove(editor, path);

        if (ImGui::BeginPopupContextItem("##menu")) {
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
            if (ImGui::MenuItem("Rename")) startRenaming(editor, path);
            if (ImGui::MenuItem("Reveal in Finder")) platform::revealInFinder(editor.assets.absolutePath(path));
            if (!isFolder && ImGui::MenuItem("Open with Default Application")) platform::openWithDefaultApplication(editor.assets.absolutePath(path));
            ImGui::Separator();
            if (ImGui::MenuItem("Move to Trash")) pendingDelete_ = path;
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
        ImGui::EndGroup();
        ImGui::PopID();
    }

    void drawDeleteConfirmation(EditorContext& editor) {
        if (!pendingDelete_.empty() && !ImGui::IsPopupOpen("Move to Trash?")) ImGui::OpenPopup("Move to Trash?");
        if (!ImGui::BeginPopupModal("Move to Trash?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::Text("Move \"%s\" to the Trash?", pendingDelete_.c_str());
        ImGui::TextDisabled("Scenes that use it will no longer find it. It can be put back from the Trash.");
        ImGui::Spacing();
        if (ImGui::Button("Move to Trash", ImVec2(140, 0))) {
            std::string absolute = editor.assets.absolutePath(pendingDelete_);
            if (platform::moveToTrash(absolute)) {
                editor.notify("Moved " + pendingDelete_ + " to the Trash");
                if (editor.selectedAsset == pendingDelete_) editor.selectedAsset.clear();
                editor.assets.refresh();            // also removes the .meta that is now alone
            } else {
                editor.notify("Cannot move " + pendingDelete_ + " to the Trash", ConsoleMessage::Level::Error);
            }
            pendingDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    std::string pendingDelete_;
};

}  // namespace

void registerAssetBrowserPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<AssetBrowserPanel>());
}

}  // namespace molten::editor
