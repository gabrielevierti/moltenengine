// EditorCommands.cpp
// The editor's commands: every thing its menus, its keys and its search (Cmd+K) can do, in
// the order the menus show them. See Commands.hpp.
//
// The menus are Unity's, because most people who open this have opened that: File, Edit,
// Assets, Entity (Unity's GameObject), Component, Window, Help. To add a command, add it
// here; if it should have keys, give it a line with the same id in Shortcuts.cpp.
#include "EditorApp.hpp"

#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include "Core/Paths.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace molten::editor {

namespace fs = std::filesystem;

std::string EditorContext::keysOf(const std::string& commandId) const {
    const Command* command = commands.find(commandId);
    if (command && !command->keys.empty()) return command->keys;
    return shortcuts.text(commandId);
}

bool EditorContext::menuItem(const std::string& commandId, const char* label) {
    const Command* command = commands.find(commandId);
    if (!command) return false;
    std::string name = label ? std::string(label) : command->name(), keys = keysOf(commandId);
    if (!ImGui::MenuItem(name.c_str(), keys.empty() ? nullptr : keys.c_str(), command->isChecked(), command->isEnabled())) return false;
    if (command->run) command->run();
    return true;
}

void EditorApp::registerCommands() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    Commands& commands = editor.commands;

    auto hasProject = [&editor] { return editor.hasProject; };
    auto isEditing = [&editor] { return editor.hasProject && !editor.scene.isPlaying(); };
    auto isPlaying = [&editor] { return editor.hasProject && editor.scene.isPlaying(); };
    auto hasSelection = [&editor] { return editor.hasProject && !editor.scene.selection().empty(); };
    // Edit's commands are about what was chosen last: something of the scene, or files of the project.
    auto onAssets = [&editor] { return editor.hasProject && editor.projectIsActive && !editor.selectedAsset.empty(); };
    auto asset = [&editor](const char* request) { editor.assetRequest = request; };
    auto show = [&editor](const char* window) {
        return [&editor, window] {
            if (Panel* panel = editor.registry.findPanel(window)) panel->isOpen = true;
            editor.panelToFocus = window;
        };
    };

    // ---------------------------------------------------------------- File
    commands.add("newScene", "File/New Scene", [&editor] { editor.requestNewScene(); }).when(isEditing);
    commands.add("openScene", "File/Open Scene...", [this] { chooseAndOpenScene(); });
    commands.add("", "File/Open Scene in Project").when(isEditing).lists([&editor] {
        bool any = false;
        for (const AssetInfo& item : editor.assets.assets()) {
            if (item.type != AssetType::Scene) continue;
            any = true;
            if (ImGui::MenuItem(item.path.c_str())) editor.requestOpenScene(editor.assets.absolutePath(item.path));
        }
        if (!any) ImGui::MenuItem("No scenes yet", nullptr, false, false);
    });
    commands.line("File");
    commands.add("save", "File/Save", [this] { saveScene(false); }).when(isEditing);
    commands.add("saveAs", "File/Save As...", [this] { saveScene(true); }).when(isEditing);
    commands.line("File");
    commands.add("newProject", "File/New Project...", [&editor] {
        editor.pendingAction = "new-project";
        editor.pendingArgument.clear();
    });
    commands.add("openProject", "File/Open Project...", [this] { chooseAndOpenProject(); });
    commands.add("", "File/Recent Projects").when([this] { return !recent_.paths.empty(); }).lists([this, &editor] {
        for (const std::string& path : std::vector<std::string>(recent_.paths)) {
            if (ImGui::MenuItem(path.c_str())) editor.requestOpenProject(path);
        }
    });
    commands.add("closeProject", "File/Close Project", [&editor] {
        editor.pendingAction = "close-project";
        editor.pendingArgument.clear();
    }).when(hasProject);
    commands.line("File");
    commands.add("export", "File/Build Settings...", [this] { showsBuild_ = true; }).when(hasProject);
    commands.line("File");
    commands.add("quit", "File/Quit", [&editor] { editor.requestQuit(); }).inGame();

    // ---------------------------------------------------------------- Edit
    commands.add("undo", "Edit/Undo", [&scene] { scene.undo(); }).when([&scene] { return scene.canUndo(); }).editKeys();
    commands.add("redo", "Edit/Redo", [&scene] { scene.redo(); }).when([&scene] { return scene.canRedo(); }).editKeys();
    commands.line("Edit");
    commands.add("cut", "Edit/Cut", [this, onAssets, asset] {
        if (onAssets()) asset("cut");
        else copySelection(true);
    }).when([hasSelection, onAssets] { return hasSelection() || onAssets(); }).onSelection();
    commands.add("copy", "Edit/Copy", [this] { copySelection(false); }).when(hasSelection).onSelection();
    commands.add("paste", "Edit/Paste", [this, onAssets, asset] {
        if (onAssets()) asset("paste");
        else pasteEntities();
    }).when([this, hasProject, onAssets] { return hasProject() && (onAssets() || canPasteEntities()); }).onSelection();
    commands.line("Edit");
    commands.add("duplicate", "Edit/Duplicate", [&scene] { scene.duplicateSelection(); }).when(hasSelection).onSelection();
    commands.add("rename", "Edit/Rename", [&editor, onAssets, asset] {
        if (onAssets()) asset("rename");
        else editor.renameRequested = true;
    }).when([&editor, hasSelection, onAssets] { return onAssets() || (hasSelection() && editor.scene.selection().size() == 1); }).onSelection();
    commands.add("delete", "Edit/Delete", [&scene, onAssets, asset] {
        if (onAssets()) asset("delete");
        else scene.deleteSelection();
    }).when([hasSelection, onAssets] { return hasSelection() || onAssets(); }).onSelection();
    commands.line("Edit");
    commands.add("selectAll", "Edit/Select All", [&scene, onAssets, asset] {
        if (onAssets()) asset("selectAll");
        else scene.selectAll();
    }).when(hasProject).onSelection();
    commands.add("deselectAll", "Edit/Deselect All", [&editor] {
        editor.scene.clearSelection();
        editor.selectedAsset.clear();
    }).when([&editor, hasSelection] { return hasSelection() || !editor.selectedAsset.empty(); }).onSelection();
    commands.add("selectChildren", "Edit/Select Children", [&scene] { scene.selectChildren(); }).when(hasSelection).onSelection();
    commands.add("invertSelection", "Edit/Invert Selection", [&scene] { scene.invertSelection(); }).when(hasProject).onSelection();
    commands.line("Edit");
    commands.add("frame", "Edit/Frame Selected", [&editor] { editor.frameSelectionRequested = true; }).when(hasSelection).shows("F");
    commands.add("find", "Edit/Find in Hierarchy", [&editor] {
        if (Panel* panel = editor.registry.findPanel("Hierarchy")) panel->isOpen = true;
        editor.hierarchySearchRequested = true;
    }).when(hasProject);
    commands.add("findAnything", "Edit/Search Everything...", [this] { palette_.open(); });
    commands.line("Edit");
    commands.add("play", "Edit/Play", [&scene] { scene.setPlaying(!scene.isPlaying()); })
        .named([&scene] { return std::string(scene.isPlaying() ? "Stop" : "Play"); }).when(hasProject).inGame();
    commands.add("pause", "Edit/Pause", [&scene] { scene.setPaused(!scene.isPaused()); })
        .named([&scene] { return std::string(scene.isPaused() ? "Resume" : "Pause"); }).when(isPlaying).inGame();
    commands.add("step", "Edit/Step", [&scene] { scene.stepFrame(); }).when(isPlaying).inGame();
    commands.line("Edit");
    commands.add("projectSettings", "Edit/Project Settings...", [this] { showProjectSettings(); }).when(hasProject);
    commands.add("settings", "Edit/Preferences...", [this] { showPreferences(); });
    commands.add("shortcuts", "Edit/Keyboard Shortcuts...", [this] { showPreferences("Shortcuts"); });

    // ---------------------------------------------------------------- Assets
    commands.add("", "Assets/Create").when(hasProject).lists([&editor] { drawAssetCreateMenu(editor); });
    commands.add("importAsset", "Assets/Import New Asset...", [asset] { asset("import"); }).when(hasProject);
    commands.line("Assets");
    commands.add("revealAsset", "Assets/Show in Finder", [asset] { asset("reveal"); }).when(hasProject);
    commands.add("openAsset", "Assets/Open", [asset] { asset("open"); }).when([&editor] { return editor.hasProject && !editor.selectedAsset.empty(); });
    commands.add("copyAssetPath", "Assets/Copy Path", [&editor] {
        ImGui::SetClipboardText(editor.selectedAsset.c_str());
        editor.notify("Copied " + editor.selectedAsset);
    }).when([&editor] { return editor.hasProject && !editor.selectedAsset.empty(); });
    commands.line("Assets");
    commands.add("refresh", "Assets/Refresh", [this] { refreshAssets(); }).when(hasProject);
    commands.line("Assets");
    commands.add("newScript", "Assets/New C++ Script...", [this] {
        showsNewScript_ = true;
        newScriptError_.clear();
    }).when(hasProject);
    commands.add("buildScripts", "Assets/Compile Scripts", [this] { buildGameWithFeedback(); }).when(isEditing);
    commands.add("openScripts", "Assets/Show Scripts Folder", [&editor] { platform::revealInFinder(editor.project.sourceDirectory()); }).when(hasProject);

    // ---------------------------------------------------------------- Entity
    commands.add("createEmpty", "Entity/Create Empty", [&scene] { scene.select(scene.create(Primitive::Empty)); }).when(isEditing);
    commands.add("createEmptyChild", "Entity/Create Empty Child", [&editor] {
        SceneDocument& scene = editor.scene;
        Entity parent = scene.primary();
        Entity child = scene.create(Primitive::Empty, parent);
        if (Transform* transform = editor.engine.scene.get<Transform>(child)) transform->position = Vec3{0, 0, 0};
        scene.select(child);
    }).when([isEditing, hasSelection] { return isEditing() && hasSelection(); });
    commands.add("group", "Entity/Group Selection", [&scene] { scene.groupSelection(); }).when([isEditing, hasSelection] { return isEditing() && hasSelection(); });
    commands.line("Entity");
    // 3D Object, Light, UI, Effects...: what was registered to be made (Editor::registerCreateEntries, and plugins).
    commands.add("", "Entity/Create").when(isEditing).lists([&editor] { drawCreateMenu(editor, Entity{}, false); }, true);
    commands.line("Entity");
    commands.add("clearParent", "Entity/Clear Parent", [&scene] { scene.clearParentOfSelection(); }).when(hasSelection);
    commands.add("firstSibling", "Entity/Move to Top", [&scene] { scene.moveSelectionInOrder(true); }).when(hasSelection);
    commands.add("lastSibling", "Entity/Move to Bottom", [&scene] { scene.moveSelectionInOrder(false); }).when(hasSelection);
    commands.line("Entity");
    commands.add("moveToView", "Entity/Move to View", [&scene] { scene.moveSelectionToView(); }).when(hasSelection);
    commands.add("alignWithView", "Entity/Align with View", [&editor] { editor.alignWithViewRequested = true; }).when(hasSelection);
    commands.add("alignView", "Entity/Align View to Selected", [&scene] { scene.alignViewWithSelection(); }).when(hasSelection);
    commands.line("Entity");
    commands.add("toggleActive", "Entity/Toggle Active", [&scene] { scene.toggleSelectionActive(); }).when(hasSelection);

    // ---------------------------------------------------------------- Component
    commands.add("addComponent", "Component/Add...", [&editor] {
        if (Panel* panel = editor.registry.findPanel("Inspector")) panel->isOpen = true;
        editor.panelToFocus = "Inspector";
        editor.addComponentRequested = true;
    }).when(hasSelection);
    commands.line("Component");
    commands.add("", "Component/All").when(hasSelection).lists([&editor] { drawAddComponentMenu(editor); }, true);

    // ---------------------------------------------------------------- Window
    auto focus = [](const char* window) {
        return [window] { ImGui::SetWindowFocus(window); };
    };
    commands.add("windowScene", "Window/General/Scene", focus("Scene")).when(hasProject);
    commands.add("windowGame", "Window/General/Game", focus("Game")).when(hasProject);
    commands.add("windowInspector", "Window/General/Inspector", show("Inspector")).when(hasProject);
    commands.add("windowHierarchy", "Window/General/Hierarchy", show("Hierarchy")).when(hasProject);
    commands.add("windowProject", "Window/General/Explorer", show("Explorer")).when(hasProject);
    commands.add("windowConsole", "Window/General/Console", show("Console")).when(hasProject);
    commands.add("windowLighting", "Window/Rendering/Lighting", show("Lighting")).when(hasProject);
    commands.add("windowRendering", "Window/Rendering/Post-processing", show("Rendering")).when(hasProject);
    commands.add("windowTimeline", "Window/Animation/Timeline", show("Timeline")).when(hasProject);
    commands.add("windowAnimator", "Window/Animation/Animator", show("Animator")).when(hasProject);
    commands.add("windowScript", "Window/Visual Scripting", show("Visual Script")).when(hasProject);
    commands.add("windowProfiler", "Window/Analysis/Profiler", show("Profiler")).when(hasProject);
    commands.add("windowSource", "Window/Source Control", show("Source Control")).when(hasProject);
    // Windows that plugins added.
    commands.add("", "Window/Others").when(hasProject).lists([&editor] {
        static const char* const listed[] = {"Hierarchy", "Inspector", "Explorer", "Console", "Lighting", "Rendering", "Timeline", "Animator",
                                             "Visual Script", "Profiler", "Source Control", "Manual", "Project Settings"};
        for (auto& item : editor.registry.panels) {
            Panel* panel = item.value.get();
            bool isListed = std::any_of(std::begin(listed), std::end(listed), [&](const char* title) { return panel->title == title; });
            if (isListed) continue;
            if (ImGui::MenuItem(panel->title.c_str(), nullptr, panel->isOpen)) {
                panel->isOpen = true;
                editor.panelToFocus = panel->title;
            }
        }
    }, true);
    commands.line("Window");
    commands.add("", "Window/Layouts").lists([this] { drawLayoutItems(); });
    commands.add("resetLayout", "Window/Reset Layout", [this] { pendingLayout_ = "Default"; }).when(hasProject);

    // ---------------------------------------------------------------- Help
    commands.add("gettingStarted", "Help/Getting Started", [this] { showManual("GettingStarted"); });
    commands.add("manual", "Help/Molten Manual", [this] { showManual("MakingGames"); });
    commands.add("manualGraphs", "Help/Visual Scripting", [this] { showManual("VisualScripting"); });
    commands.add("manualScripts", "Help/Writing Game Code", [this] { showManual("GameCode"); });
    commands.add("helpShortcuts", "Help/Keyboard Shortcuts", [this] { showPreferences("Shortcuts"); });
    commands.line("Help");
    commands.add("reportProblem", "Help/Report a Problem", [] { platform::openWebPage(std::string(kRepository) + "/issues"); });
    commands.add("github", "Help/Molten on GitHub", [] { platform::openWebPage(kRepository); });
    commands.line("Help");
    commands.add("about", "Help/About Molten Metal Engine", [this] { showsAbout_ = true; });
}

// ---------------------------------------------------------------- what some of them do

void EditorApp::copySelection(bool cuts) {
    EditorContext& editor = *context_;
    std::string text = editor.scene.copySelection();
    if (text.empty()) return;
    editor.entityClipboard = text;
    ImGui::SetClipboardText(text.c_str());          // another Molten window can paste it
    size_t count = editor.scene.selectionTops().size();
    if (cuts) editor.scene.deleteSelection();
    editor.notify(std::string(cuts ? "Cut " : "Copied ") + (count == 1 ? std::string("1 entity") : std::to_string(count) + " entities"));
}

bool EditorApp::canPasteEntities() const {
    if (!context_->entityClipboard.empty()) return true;
    const char* system = ImGui::GetClipboardText();
    return system && SceneDocument::isEntityText(system);
}

void EditorApp::pasteEntities() {
    EditorContext& editor = *context_;
    // The system's clipboard first: it may hold what another window of the editor copied since.
    const char* system = ImGui::GetClipboardText();
    std::string text = system && SceneDocument::isEntityText(system) ? std::string(system) : editor.entityClipboard;
    std::vector<Entity> pasted = editor.scene.paste(text, editor.scene.primary());
    if (pasted.empty()) editor.notify("Nothing to paste: copy something of the scene first", ConsoleMessage::Level::Warning);
}

void EditorApp::refreshAssets() {
    EditorContext& editor = *context_;
    editor.assets.refresh();
    nextAssetScan_ = 0;                             // and what changed on the disk is read again this frame
    editor.notify("Assets refreshed: " + std::to_string(editor.assets.assets().size()) + " files and folders");
}

}  // namespace molten::editor
