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

    // ---------------------------------------------------------------- the program's own menu
    // Where every Mac program has these: in the menu named after it, at the left of the menu bar.
    commands.add("about", "Molten Engine/About Molten Engine", [this] { showsAbout_ = true; });
    commands.line("Molten Engine");
    commands.add("settings", "Molten Engine/Preferences...", [this] { showPreferences(); });
    commands.line("Molten Engine");
    commands.add("quit", "Molten Engine/Quit Molten Engine", [&editor] { editor.requestQuit(); }).inGame();

    // ---------------------------------------------------------------- File
    // Scenes, then projects, then the game as it is shipped. (The scenes of the project are also in the Explorer.)
    commands.add("newScene", "File/New Scene", [&editor] { editor.requestNewScene(); }).when(isEditing);
    commands.add("openScene", "File/Open Scene...", [this] { chooseAndOpenScene(); }).when(isEditing);
    commands.line("File");
    commands.add("save", "File/Save", [this] { saveScene(false); }).when(isEditing);
    commands.add("saveAs", "File/Save As...", [this] { saveScene(true); }).when(isEditing);
    commands.line("File");
    commands.add("newProject", "File/New Project...", [&editor] {
        editor.pendingAction = "new-project";
        editor.pendingArgument.clear();
    });
    commands.add("openProject", "File/Open Project...", [this] { chooseAndOpenProject(); });
    commands.add("", "File/Open Recent").when([this] { return !recent_.paths.empty(); }).lists([this, &editor] {
        std::vector<MenuEntry> entries;
        for (const std::string& path : recent_.paths) {
            MenuEntry entry;
            entry.label = path;
            entry.run = [&editor, path] { editor.requestOpenProject(path); };
            entries.push_back(std::move(entry));
        }
        return entries;
    });
    commands.add("closeProject", "File/Close Project", [&editor] {
        editor.pendingAction = "close-project";
        editor.pendingArgument.clear();
    }).when(hasProject);
    commands.line("File");
    // The project as it is now, as a template new projects can start from: in your templates folder, beside the engine's.
    commands.add("saveAsTemplate", "File/Save Project as Template", [this, &editor] {
        std::string error;
        std::error_code code;
        fs::create_directories(yourTemplatesDirectory(), code);
        saveProjectPreview();
        std::string name = editor.project.name;
        for (int copy = 2; fs::exists(fs::path(yourTemplatesDirectory()) / Project::fileNameFor(name), code) && copy < 100; copy++) {
            name = editor.project.name + " " + std::to_string(copy);
        }
        std::string made = copyAsTemplate(editor.project.directory, yourTemplatesDirectory(), name, &error);
        if (made.empty()) {
            editor.notify("Could not make a template of it: " + error, ConsoleMessage::Level::Error);
            return;
        }
        templates_ = projectTemplates(findRootDirectory() + "/Templates", yourTemplatesDirectory());
        editor.notify("Saved as the template \"" + name + "\" (in ~/Documents/Molten Templates): File > New Project starts from it");
    }).when(hasProject);
    commands.line("File");
    commands.add("export", "File/Build Settings...", [this] { showsBuild_ = true; }).when(hasProject);

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

    // ---------------------------------------------------------------- Assets
    commands.add("", "Assets/Create").when(hasProject).lists([&editor] { return assetCreateEntries(editor); });
    commands.add("importAsset", "Assets/Import New Asset...", [asset] { asset("import"); }).when(hasProject);
    commands.line("Assets");
    commands.add("openAsset", "Assets/Open", [asset] { asset("open"); }).when([&editor] { return editor.hasProject && !editor.selectedAsset.empty(); });
    commands.add("revealAsset", "Assets/Show in Finder", [asset] { asset("reveal"); }).when(hasProject);
    commands.add("copyAssetPath", "Assets/Copy Path", [&editor] {
        ImGui::SetClipboardText(editor.selectedAsset.c_str());
        editor.notify("Copied " + editor.selectedAsset);
    }).when([&editor] { return editor.hasProject && !editor.selectedAsset.empty(); });
    commands.line("Assets");
    commands.add("refresh", "Assets/Refresh", [this] { refreshAssets(); }).when(hasProject);
    commands.add("buildScripts", "Assets/Compile Scripts", [this] { buildGameWithFeedback(); }).when(isEditing);

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
    commands.add("", "Entity/Create").when(isEditing).lists([&editor] { return createMenuEntries(editor, Entity{}, false); }, true);
    commands.line("Entity");
    commands.add("clearParent", "Entity/Clear Parent", [&scene] { scene.clearParentOfSelection(); }).when(hasSelection);
    commands.add("firstSibling", "Entity/Move to Top", [&scene] { scene.moveSelectionInOrder(true); }).when(hasSelection);
    commands.add("lastSibling", "Entity/Move to Bottom", [&scene] { scene.moveSelectionInOrder(false); }).when(hasSelection);
    commands.line("Entity");
    commands.add("frame", "Entity/Frame Selected", [&editor] { editor.frameSelectionRequested = true; }).when(hasSelection).shows("F");
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
    commands.add("", "Component/All").when(hasSelection).lists([&editor] { return addComponentEntries(editor); }, true);

    // ---------------------------------------------------------------- Window
    // The windows, in groups. (Layouts are in the bar at the top right.)
    auto focus = [](const char* window) {
        return [window] { ImGui::SetWindowFocus(ui::windowId(window).c_str()); };
    };
    commands.add("windowScene", "Window/Scene", focus("Scene")).when(hasProject);
    commands.add("windowGame", "Window/Game", focus("Game")).when(hasProject);
    commands.add("windowInspector", "Window/Inspector", show("Inspector")).when(hasProject);
    commands.add("windowHierarchy", "Window/Hierarchy", show("Hierarchy")).when(hasProject);
    commands.add("windowProject", "Window/Explorer", show("Explorer")).when(hasProject);
    commands.add("windowConsole", "Window/Console", show("Console")).when(hasProject);
    commands.line("Window");
    commands.add("windowLighting", "Window/Lighting", show("Lighting")).when(hasProject);
    commands.add("windowEnvironment", "Window/Environment", show("Environment")).when(hasProject);
    commands.add("windowRendering", "Window/Rendering", show("Rendering")).when(hasProject);
    commands.add("windowPostProcessing", "Window/Post Processing", show("Post Processing")).when(hasProject);
    commands.line("Window");
    commands.add("windowAnimation", "Window/Animation", show("Animation")).when(hasProject);
    commands.add("windowTimeline", "Window/Timeline", show("Timeline")).when(hasProject);
    commands.add("windowAnimator", "Window/Animator", show("Animator")).when(hasProject);
    commands.add("windowScript", "Window/Visual Script", show("Visual Script")).when(hasProject);
    commands.add("windowShaderGraph", "Window/Shader Graph", show("Shader Graph")).when(hasProject);
    commands.line("Window");
    commands.add("windowProfiler", "Window/Profiler", show("Profiler")).when(hasProject);
    commands.add("windowSource", "Window/Source Control", show("Source Control")).when(hasProject);
    commands.line("Window");
    // The whole screen but the Mac's menu bar, where the menus are (the window's green button does the same).
    commands.add("fullScreen", "Window/Full Screen", [&editor] {
        if (Window* window = editor.engine.window()) platform::toggleFullScreen(window->nativeHandle());
    }).ticked([&editor] { return editor.engine.window() && platform::isFullScreen(editor.engine.window()->nativeHandle()); });
    // Windows that plugins added.
    commands.add("", "Window/Others").when(hasProject).lists([&editor] {
        static const char* const listed[] = {"Hierarchy", "Inspector", "Explorer", "Console", "Lighting", "Environment", "Rendering", "Post Processing", "Animation", "Timeline", "Animator",
                                             "Visual Script", "Shader Graph", "Profiler", "Source Control", "Manual", "Project Settings"};
        std::vector<MenuEntry> entries;
        for (auto& item : editor.registry.panels) {
            Panel* panel = item.value.get();
            bool isListed = std::any_of(std::begin(listed), std::end(listed), [&](const char* title) { return panel->title == title; });
            if (isListed) continue;
            MenuEntry entry;
            entry.label = panel->title;
            entry.isChecked = panel->isOpen;
            std::string title = panel->title;
            entry.run = [&editor, title] {
                if (Panel* found = editor.registry.findPanel(title)) found->isOpen = true;
                editor.panelToFocus = title;
            };
            entries.push_back(std::move(entry));
        }
        if (!entries.empty()) entries.insert(entries.begin(), MenuEntry::line());
        return entries;
    }, true);

    // ---------------------------------------------------------------- Help
    commands.add("gettingStarted", "Help/Getting Started", [this] { showManual("GettingStarted"); });
    commands.add("manual", "Help/Molten Manual", [this] { showManual("MakingGames"); });
    commands.add("manualGraphs", "Help/Visual Scripting", [this] { showManual("VisualScripting"); });
    commands.add("manualScripts", "Help/Writing Game Code", [this] { showManual("GameCode"); });
    commands.line("Help");
    commands.add("reportProblem", "Help/Report a Problem", [] { platform::openWebPage(std::string(kRepository) + "/issues"); });
    commands.add("github", "Help/Molten on GitHub", [] { platform::openWebPage(kRepository); });
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
