#include "EditorApp.hpp"

#include "GameBuild.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include "Core/Paths.hpp"

#include <imgui_internal.h>         // the dock builder, for the default layout

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace molten::editor {

namespace fs = std::filesystem;

namespace {

const char* const kVersion = "0.1.0";

/// Everything the engine logs also goes to the console panel. The engine may log from
/// another thread (audio), so messages wait here until the frame collects them.
struct LogQueue {
    std::mutex mutex;
    std::vector<std::pair<ConsoleMessage::Level, std::string>> waiting;
};

LogQueue& logQueue() {
    static LogQueue queue;
    return queue;
}

}  // namespace

// ---------------------------------------------------------------- start and stop

void EditorApp::start(Engine& engine) {
    engine_ = &engine;
    context_ = std::make_unique<EditorContext>(engine);
    EditorContext& editor = *context_;
    engine.isSimulationPaused = true;       // nothing falls while it is being placed
    engine.runsComponents = false;          // and the game's code waits for Play

    logSink() = [](const char* level, const char* message) {
        ConsoleMessage::Level kind = level[0] == 'e' ? ConsoleMessage::Level::Error
                                   : level[0] == 'w' ? ConsoleMessage::Level::Warning : ConsoleMessage::Level::Info;
        std::lock_guard<std::mutex> lock(logQueue().mutex);
        logQueue().waiting.push_back({kind, message});
    };

    std::string userData = userDataDirectory();
    recentFile_ = userData + "/recent-projects.txt";
    recent_.load(recentFile_);
    const char* home = std::getenv("HOME");
    newProjectLocation_ = std::string(home ? home : ".") + "/Documents";

    std::string layoutFile = userData + "/layout.ini";
    std::error_code ignored;
    needsDefaultLayout_ = !fs::exists(layoutFile, ignored);
    imgui_.start(engine, layoutFile);       // false without a window: the editor then only loads and shows the scene

    registerCreateEntries(editor);
    registerBuiltinComponents(editor);
    registerHierarchyPanel(editor);
    registerInspectorPanel(editor);
    registerAssetBrowserPanel(editor);
    registerConsolePanel(editor);
    registerMaterialsPanel(editor);
    registerRenderingPanel(editor);
    registerProjectPanel(editor);

    // Plugins that come with the editor live next to its executable.
    if (!engine.config().executablePath.empty()) {
        fs::path beside = fs::absolute(fs::path(engine.config().executablePath), ignored).parent_path() / "Plugins";
        plugins_.loadDirectory(editor, beside.string(), false);
    }

    editor.scene.newScene();
    if (!projectToOpen.empty() && !openProject(projectToOpen)) projectToOpen.clear();
    if (editor.hasProject && !sceneToOpen.empty()) openSceneNow(editor.assets.absolutePath(sceneToOpen));
    updateTitle();
}

void EditorApp::shutdown(Engine& engine) {
    if (!context_) return;
    plugins_.unloadAll(*context_);
    logSink() = nullptr;
    imgui_.shutdown(engine);
    context_.reset();
}

// ---------------------------------------------------------------- projects and scenes

bool EditorApp::openProject(const std::string& path) {
    EditorContext& editor = *context_;
    std::string error;
    std::optional<Project> project = Project::load(path, &error);
    if (!project) {
        projectError_ = error;
        editor.notify(error, ConsoleMessage::Level::Error);
        recent_.remove(path);
        recent_.save(recentFile_);
        return false;
    }
    closeProject();
    editor.project = *project;
    editor.hasProject = true;
    editor.engine.assets().setProjectDirectory(editor.project.assetsDirectory());
    editor.assets.open(editor.project.assetsDirectory());
    editor.currentFolder.clear();
    editor.selectedAsset.clear();
    recent_.add(editor.project.directory);
    recent_.save(recentFile_);
    plugins_.loadDirectory(editor, editor.project.pluginsDirectory(), true);
    buildGame();                            // before the scene is read: its components are the game's

    // The scene the game starts with, if it has one; otherwise a new scene to save later.
    std::string first = editor.project.startScene.empty() ? std::string() : editor.assets.absolutePath(editor.project.startScene);
    if (first.empty() || !editor.scene.open(first)) editor.scene.newScene();
    editor.notify("Opened project " + editor.project.name);
    projectError_.clear();
    return true;
}

void EditorApp::closeProject() {
    EditorContext& editor = *context_;
    if (!editor.hasProject) return;
    plugins_.unloadProjectPlugins(editor);
    // The project's components leave with it; the scene is emptied around the change.
    if (gameModule_.isLoaded()) editor.scene.reloadGameCode([this] { gameModule_.retire(); });
    editor.hasProject = false;
    editor.project = Project{};
    editor.assets.open("");
    editor.engine.assets().setProjectDirectory("");
}

void EditorApp::openSceneNow(const std::string& absolutePath) {
    EditorContext& editor = *context_;
    std::string error;
    if (editor.scene.open(absolutePath, &error)) editor.notify("Opened " + editor.scene.displayName());
    else editor.notify(error, ConsoleMessage::Level::Error);
}

void EditorApp::saveScene(bool asNewFile) {
    EditorContext& editor = *context_;
    if (!editor.hasProject) {
        editor.notify("Open or create a project first: scenes are saved in its Assets folder", ConsoleMessage::Level::Warning);
        return;
    }
    if (editor.scene.isPlaying()) {
        editor.notify("Stop the game before saving", ConsoleMessage::Level::Warning);
        return;
    }
    std::string error;
    if (asNewFile || editor.scene.filePath.empty()) {
        // A new file in the Scenes folder, under a name nothing has yet. It can be renamed in the asset browser.
        std::string stem = editor.scene.filePath.empty() ? std::string("New Scene") : editor.scene.displayName() + " Copy";
        std::string path = editor.assets.uniquePath("Scenes", stem, ".scene");
        if (editor.scene.saveAs(editor.assets.absolutePath(path), &error)) {
            editor.assets.refresh();
            editor.selectedAsset = path;
            editor.currentFolder = "Scenes";
            // The first scene saved in a project is the one the game starts with.
            if (editor.project.startScene.empty()) {
                editor.project.startScene = path;
                editor.project.save();
            }
            editor.notify("Saved as Assets/" + path);
        }
    } else if (editor.scene.save(&error)) {
        editor.notify("Saved " + editor.scene.displayName());
    }
    if (!error.empty()) editor.notify(error, ConsoleMessage::Level::Error);
}

/// Compiles the project's Source folder and puts the result under the open scene.
/// Returns false if the code did not compile; the code that was loaded before stays then.
bool EditorApp::buildGame() {
    EditorContext& editor = *context_;
    if (!editor.hasProject) return false;
    BuildResult result = buildGameCode(editor.project, currentExecutablePath());
    if (!result.hadSources) {
        if (gameModule_.isLoaded()) editor.scene.reloadGameCode([this] { gameModule_.retire(); });
        return true;                        // a project without code is a project all the same
    }
    if (!result.succeeded) {
        // The compiler's words, line by line, where they can be read and searched.
        size_t start = 0;
        int lines = 0;
        while (start < result.log.size() && lines < 300) {
            size_t stop = result.log.find('\n', start);
            if (stop == std::string::npos) stop = result.log.size();
            if (stop > start) editor.console.push_back(ConsoleMessage{ConsoleMessage::Level::Error, result.log.substr(start, stop - start), editor.engine.time.elapsed, 1});
            start = stop + 1;
            lines++;
        }
        editor.notify("The game's code did not build. See the console.", ConsoleMessage::Level::Error);
        if (Panel* console = editor.registry.findPanel("Console")) console->isOpen = true;
        return false;
    }
    std::string error;
    editor.scene.reloadGameCode([&] {
        if (!gameModule_.load(result.libraryPath, &error)) error = "The game's code was built but could not be loaded: " + error;
    });
    if (!error.empty()) {
        editor.notify(error, ConsoleMessage::Level::Error);
        return false;
    }
    char seconds[32];
    std::snprintf(seconds, sizeof(seconds), "%.1f", result.seconds);
    size_t count = gameModule_.componentNames().size();
    editor.notify("Built the game's code in " + std::string(seconds) + " s: " + std::to_string(count) + (count == 1 ? " component" : " components"));
    return true;
}

/// Makes a Mac application of the project: asks where, builds the code, packs everything.
void EditorApp::exportGame() {
    EditorContext& editor = *context_;
    if (!editor.hasProject) return;
    if (editor.scene.isPlaying()) editor.scene.setPlaying(false);
    if (editor.scene.isDirty() || editor.scene.filePath.empty()) saveScene(false);
    if (editor.project.startScene.empty()) {
        editor.notify("Save a scene first: the game starts with the project's first scene (Edit > Project Settings)", ConsoleMessage::Level::Warning);
        return;
    }
    if (!buildGame()) return;
    std::string destination = platform::chooseFolder("Choose where to put the application");
    if (destination.empty()) return;
    std::string executable = currentExecutablePath();
    std::string player = (fs::path(executable).parent_path() / "molten-player").string();
    std::string engineRoot = findRootDirectory(executable);
    std::string application;
    std::string error = exportMacApplication(editor.project, destination, player, engineRoot, &application);
    if (!error.empty()) {
        editor.notify(error, ConsoleMessage::Level::Error);
        if (application.empty()) return;
    } else {
        editor.notify("Exported " + application);
    }
    platform::revealInFinder(application);
}

/// Carries out what a panel or a menu asked for, after asking about unsaved changes
/// when carrying it out would lose them.
void EditorApp::runPendingAction() {
    EditorContext& editor = *context_;
    if (editor.pendingAction.empty() || confirmIsOpen_) return;
    std::string action = std::move(editor.pendingAction), argument = std::move(editor.pendingArgument);
    editor.pendingAction.clear();
    editor.pendingArgument.clear();

    if (editor.scene.isPlaying()) editor.scene.setPlaying(false);
    if (editor.scene.isDirty()) {
        confirmAction_ = action;
        confirmArgument_ = argument;
        confirmIsOpen_ = true;
        return;
    }
    if (action == "open-scene") {
        openSceneNow(argument);
    } else if (action == "new-scene") {
        editor.scene.newScene();
    } else if (action == "open-project") {
        openProject(argument);
    } else if (action == "close-project") {
        closeProject();
        editor.scene.newScene();
    } else if (action == "quit") {
        editor.engine.quit();
    }
}

void EditorApp::drawUnsavedChangesDialog() {
    EditorContext& editor = *context_;
    if (confirmIsOpen_ && !ImGui::IsPopupOpen("Unsaved Changes")) ImGui::OpenPopup("Unsaved Changes");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("\"%s\" has changes that are not saved.", editor.scene.displayName().c_str());
    ImGui::Spacing();
    bool proceed = false;
    if (ImGui::Button("Save", ImVec2(110, 0))) {
        saveScene(false);
        proceed = !editor.scene.isDirty();          // if saving failed, stay here: nothing is lost
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
        editor.scene.newScene();                    // drops the changes; the action then replaces the scene anyway
        proceed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        confirmIsOpen_ = false;
        confirmAction_.clear();
        ImGui::CloseCurrentPopup();
    }
    if (proceed) {
        confirmIsOpen_ = false;
        editor.pendingAction = confirmAction_;
        editor.pendingArgument = confirmArgument_;
        confirmAction_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/// Closing the window is a request like any other: it must not lose changes without asking.
void EditorApp::handleWindowClose() {
    Window* window = engine_->window();
    if (!window || !window->shouldClose()) return;
    if (context_->scene.isDirty() && !context_->scene.isPlaying()) {
        glfwSetWindowShouldClose(window->nativeHandle(), GLFW_FALSE);
        if (!confirmIsOpen_) context_->requestQuit();
    }
}

// ---------------------------------------------------------------- the frame

void EditorApp::draw(Engine& engine, Canvas&) {
    if (!context_) return;
    EditorContext& editor = *context_;
    editor.scene.beginFrame();
    if (!imgui_.isActive()) {
        engine.scene.updateTransforms();
        return;                             // no window: nothing to show an interface in
    }

    {
        std::lock_guard<std::mutex> lock(logQueue().mutex);
        for (auto& [level, message] : logQueue().waiting) {
            if (!editor.console.empty() && editor.console.back().text == message) editor.console.back().count++;
            else editor.console.push_back(ConsoleMessage{level, message, engine.time.elapsed, 1});
        }
        logQueue().waiting.clear();
    }
    if (editor.console.size() > 2000) editor.console.erase(editor.console.begin(), editor.console.begin() + 500);

    // Files change behind the editor's back (the Finder, git, other programs): look once a second.
    if (editor.hasProject && engine.time.elapsed >= nextAssetScan_) {
        nextAssetScan_ = engine.time.elapsed + 1.0;
        editor.assets.refresh();
    }

    handleWindowClose();
    runPendingAction();

    imgui_.beginFrame();
    plugins_.update(editor, engine.time.unscaledDelta);
    handleShortcuts();
    drawMenuBar();
    drawStatusBar();
    if (editor.hasProject) {
        drawDockSpace();
        drawPanels();
    } else {
        engine.renderer().sceneViewport = Renderer::Rect{};
        drawProjectList();
    }
    drawUnsavedChangesDialog();
    drawAboutDialog();
    if (showsDemo_) ImGui::ShowDemoWindow(&showsDemo_);

    // An edit becomes a step for Undo once the hand is off: no drag in progress, no text field open.
    bool interacting = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    editor.scene.endFrame(interacting);
    imgui_.endFrame();
    updateTitle();
}

void EditorApp::updateTitle() {
    Window* window = engine_->window();
    if (!window) return;
    EditorContext& editor = *context_;
    std::string title = "Molten Metal Engine";
    if (editor.hasProject) {
        title = editor.scene.displayName() + (editor.scene.isDirty() ? " \xE2\x80\xA2" : "") + " \xE2\x80\x94 " + editor.project.name + " \xE2\x80\x94 Molten Metal Engine";
    }
    if (title != shownTitle_) {
        window->setTitle(title);
        shownTitle_ = title;
    }
}

void EditorApp::handleShortcuts() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    ImGuiIO& io = ImGui::GetIO();
    // ImGui calls the Command key "Ctrl" on a Mac, so these are Cmd+S, Cmd+Z and so on.
    auto pressed = [](ImGuiKeyChord chord) { return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal); };

    if (pressed(ImGuiMod_Ctrl | ImGuiKey_P)) scene.setPlaying(!scene.isPlaying());
    if (scene.isPlaying()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) scene.setPlaying(false);
        return;
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) saveScene(true);
    else if (pressed(ImGuiMod_Ctrl | ImGuiKey_S)) saveScene(false);
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_N)) editor.requestNewScene();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_B)) buildGame();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Q)) editor.requestQuit();

    // These would fight with typing in a text field.
    if (io.WantTextInput) return;
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || pressed(ImGuiMod_Ctrl | ImGuiKey_Y)) scene.redo();
    else if (pressed(ImGuiMod_Ctrl | ImGuiKey_Z)) scene.undo();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_D)) scene.duplicateSelection();
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_A)) scene.selectAll();
    if (!ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        // Only when the scene is what the user is working on, not the asset browser.
        if (!scene.selection().empty() && editor.selectedAsset.empty()) scene.deleteSelection();
    }
}

// ---------------------------------------------------------------- menus

/// Draws the items plugins (and the editor) registered under one top-level menu.
/// "Tools/Level/Align" with menu "Tools" appears as Level > Align.
void EditorApp::drawRegisteredMenuItems(const std::string& menu) {
    EditorContext& editor = *context_;
    // Indices, not references: an action may register or remove items.
    for (size_t i = 0; i < editor.registry.menuItems.size(); i++) {
        const MenuItem& item = editor.registry.menuItems[i].value;
        if (item.path.compare(0, menu.size() + 1, menu + "/") != 0) continue;
        std::string rest = item.path.substr(menu.size() + 1);
        // Open the submenus along the path, then the item itself.
        int depth = 0;
        bool isVisible = true;
        size_t start = 0;
        for (size_t slash = rest.find('/'); slash != std::string::npos; slash = rest.find('/', start)) {
            if (!ImGui::BeginMenu(rest.substr(start, slash - start).c_str())) {
                isVisible = false;
                break;
            }
            depth++;
            start = slash + 1;
        }
        if (isVisible) {
            bool isEnabled = !item.isEnabled || item.isEnabled(editor);
            std::function<void(EditorContext&)> action = item.action;
            if (ImGui::MenuItem(rest.substr(start).c_str(), item.shortcut.empty() ? nullptr : item.shortcut.c_str(), false, isEnabled) && action) {
                action(editor);
            }
        }
        for (; depth > 0; depth--) ImGui::EndMenu();
    }
}

void EditorApp::drawMenuBar() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    if (!ImGui::BeginMainMenuBar()) return;
    bool hasProject = editor.hasProject, isEditing = hasProject && !scene.isPlaying();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene", "Cmd+N", false, isEditing)) editor.requestNewScene();
        if (ImGui::BeginMenu("Open Scene", isEditing)) {
            bool any = false;
            for (const AssetInfo& asset : editor.assets.assets()) {
                if (asset.type != AssetType::Scene) continue;
                any = true;
                if (ImGui::MenuItem(asset.path.c_str())) editor.requestOpenScene(editor.assets.absolutePath(asset.path));
            }
            if (!any) ImGui::TextDisabled("The project has no scenes yet.");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", "Cmd+S", false, isEditing)) saveScene(false);
        if (ImGui::MenuItem("Save Scene As New File", "Shift+Cmd+S", false, isEditing)) saveScene(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Open Project...")) {
            std::string folder = platform::chooseFolder("Choose the folder of a Molten Metal Engine project");
            if (!folder.empty()) editor.requestOpenProject(folder);
        }
        if (ImGui::BeginMenu("Recent Projects", !recent_.paths.empty())) {
            for (const std::string& path : std::vector<std::string>(recent_.paths)) {
                if (ImGui::MenuItem(path.c_str())) editor.requestOpenProject(path);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Close Project", nullptr, false, hasProject)) {
            editor.pendingAction = "close-project";
            editor.pendingArgument.clear();
        }
        drawRegisteredMenuItems("File");
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Cmd+Q")) editor.requestQuit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Cmd+Z", false, scene.canUndo())) scene.undo();
        if (ImGui::MenuItem("Redo", "Shift+Cmd+Z", false, scene.canRedo())) scene.redo();
        ImGui::Separator();
        bool hasSelection = isEditing && !scene.selection().empty();
        if (ImGui::MenuItem("Duplicate", "Cmd+D", false, hasSelection)) scene.duplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) scene.deleteSelection();
        if (ImGui::MenuItem("Select All", "Cmd+A", false, isEditing)) scene.selectAll();
        if (ImGui::MenuItem("Frame Selection", "F", false, hasSelection)) editor.frameSelectionRequested = true;
        drawRegisteredMenuItems("Edit");
        ImGui::Separator();
        if (ImGui::MenuItem("Project Settings...", nullptr, false, hasProject)) {
            if (Panel* panel = editor.registry.findPanel("Project Settings")) panel->isOpen = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Create", isEditing)) {
        drawCreateMenu(editor, Entity{});
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play", hasProject)) {
        if (ImGui::MenuItem(scene.isPlaying() ? "Stop" : "Play", "Cmd+P")) scene.setPlaying(!scene.isPlaying());
        if (ImGui::MenuItem(scene.isPaused() ? "Resume" : "Pause", nullptr, false, scene.isPlaying())) scene.setPaused(!scene.isPaused());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Game", hasProject)) {
        if (ImGui::MenuItem("Build Game Code", "Cmd+B")) buildGame();
        if (ImGui::MenuItem("Show Source Folder")) platform::revealInFinder(editor.project.sourceDirectory());
        if (ImGui::BeginMenu("Components", gameModule_.isLoaded())) {
            for (const std::string& name : gameModule_.componentNames()) ImGui::MenuItem(name.c_str(), nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export for macOS...")) exportGame();
        drawRegisteredMenuItems("Game");
        ImGui::EndMenu();
    }

    // Menus that exist only because something was registered under them: "Tools", or a plugin's own.
    std::vector<std::string> extraMenus;
    for (const auto& item : editor.registry.menuItems) {
        std::string menu = item.value.path.substr(0, item.value.path.find('/'));
        bool isBuiltIn = menu == "File" || menu == "Edit" || menu == "Game" || menu == "Window" || menu == "Help";
        if (!isBuiltIn && std::find(extraMenus.begin(), extraMenus.end(), menu) == extraMenus.end()) extraMenus.push_back(menu);
    }
    for (const std::string& menu : extraMenus) {
        if (ImGui::BeginMenu(menu.c_str())) {
            drawRegisteredMenuItems(menu);
            ImGui::EndMenu();
        }
    }

    if (ImGui::BeginMenu("Window")) {
        for (auto& item : editor.registry.panels) {
            ImGui::MenuItem(item.value->title.c_str(), nullptr, &item.value->isOpen);
        }
        ImGui::Separator();
        ImGui::MenuItem("Show All Colliders", nullptr, &editor.engine.showsColliders);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) needsDefaultLayout_ = true;
        drawRegisteredMenuItems("Window");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Molten Metal Engine")) showsAbout_ = true;
        if (ImGui::BeginMenu("Plugins")) {
            if (plugins_.plugins().empty()) ImGui::TextDisabled("None loaded.");
            for (const PluginHost::Loaded& loaded : plugins_.plugins()) {
                ImGui::MenuItem(loaded.name.c_str(), loaded.isFromProject ? "project" : "editor", false, false);
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Dear ImGui Demo", nullptr, &showsDemo_);
        drawRegisteredMenuItems("Help");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorApp::drawAboutDialog() {
    if (showsAbout_ && !ImGui::IsPopupOpen("About Molten Metal Engine")) ImGui::OpenPopup("About Molten Metal Engine");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("About Molten Metal Engine", &showsAbout_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Molten Metal Engine %s", kVersion);
    ImGui::TextDisabled("A game engine for Apple silicon, built on Metal.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Open source, by Gabriele Vierti.");
    ImGui::TextDisabled("Interface: Dear ImGui and ImGuizmo. Window: GLFW. Sound: miniaudio. Images: stb.");
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(100, 0))) {
        showsAbout_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::drawStatusBar() {
    EditorContext& editor = *context_;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
        if (ImGui::BeginMenuBar()) {
            if (editor.engine.time.elapsed < editor.statusUntil) ImGui::TextUnformatted(editor.statusText.c_str());
            else if (editor.scene.isPlaying()) ImGui::TextDisabled("Playing. Cmd+P or Esc goes back to editing.");
            char right[160];
            std::snprintf(right, sizeof(right), "%zu entities   %d draw calls   %.0f fps", editor.scene.entities().size(),
                          editor.engine.renderer().stats.drawCalls, double(editor.engine.time.framesPerSecond));
            float width = ImGui::CalcTextSize(right).x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - width - 14));
            ImGui::TextDisabled("%s", right);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- the dock space and the panels

void EditorApp::buildDefaultLayout(ImGuiID dockSpace) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockSpace);
    ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockSpace, viewport->WorkSize);
    // Hierarchy on the left, inspector on the right, assets and console along the bottom;
    // what is left in the middle is the 3D view.
    ImGuiID center = dockSpace;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Materials", right);
    ImGui::DockBuilderDockWindow("Rendering", right);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockSpace);
}

void EditorApp::drawDockSpace() {
    EditorContext& editor = *context_;
    ImGuiID dockSpace = ImGui::GetID("MoltenDockSpace");
    if (needsDefaultLayout_) {
        buildDefaultLayout(dockSpace);
        needsDefaultLayout_ = false;
    }
    ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // The part of the dock space no panel covers is where the scene shows through.
    ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockSpace);
    if (central) drawViewport(editor, central->Pos, central->Size);
}

void EditorApp::drawPanels() {
    EditorContext& editor = *context_;
    // By index: a panel may register another panel while it draws.
    for (size_t i = 0; i < editor.registry.panels.size(); i++) {
        Panel* panel = editor.registry.panels[i].value.get();
        if (!panel->isOpen) continue;
        ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(panel->title.c_str(), &panel->isOpen, panel->windowFlags())) panel->draw(editor);
        ImGui::End();
    }
}

// ---------------------------------------------------------------- the project list

/// What the editor shows before a project is open: the recent ones, and a form for a new one.
void EditorApp::drawProjectList() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("##projects", nullptr, flags)) {
        ImGui::End();
        return;
    }
    float width = std::min(720.0f, viewport->WorkSize.x - 60);
    ImGui::SetCursorPos(ImVec2((viewport->WorkSize.x - width) * 0.5f, std::max(40.0f, viewport->WorkSize.y * 0.12f)));
    ImGui::BeginChild("##column", ImVec2(width, 0));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.48f, 0.18f, 1));
    ImGui::PushFont(nullptr, 34.0f);
    ImGui::TextUnformatted("Molten Metal Engine");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::TextDisabled("A game engine for Apple silicon, built on Metal.   %s", kVersion);
    ImGui::Dummy(ImVec2(0, 18));

    ImGui::SeparatorText("New Project");
    ui::text("Name", newProjectName_);
    ui::label("Location");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
    ImGui::InputText("##location", &newProjectLocation_);
    ImGui::SameLine();
    if (ImGui::Button("Choose...", ImVec2(-FLT_MIN, 0))) {
        std::string folder = platform::chooseFolder("Choose where the project's folder is created", newProjectLocation_);
        if (!folder.empty()) newProjectLocation_ = folder;
    }
    ImGui::TextDisabled("Will be created at %s/%s", newProjectLocation_.c_str(), Project::fileNameFor(newProjectName_).c_str());
    if (ImGui::Button("Create Project", ImVec2(160, 0))) {
        std::string error;
        std::optional<Project> created = Project::create(newProjectLocation_, newProjectName_, &error);
        if (created) openProject(created->directory);
        else projectError_ = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Other...", ImVec2(160, 0))) {
        std::string folder = platform::chooseFolder("Choose the folder of a Molten Metal Engine project");
        if (!folder.empty()) openProject(folder);
    }
    if (!projectError_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1), "%s", projectError_.c_str());
    ImGui::Dummy(ImVec2(0, 18));

    ImGui::SeparatorText("Recent Projects");
    if (recent_.paths.empty()) ImGui::TextDisabled("Projects you open will be listed here.");
    for (const std::string& path : std::vector<std::string>(recent_.paths)) {
        ImGui::PushID(path.c_str());
        std::string name = fs::path(path).filename().string();
        if (ImGui::Selectable("##recent", false, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetTextLineHeight() * 2 + 8))) openProject(path);
        ImVec2 min = ImGui::GetItemRectMin();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(ImVec2(min.x + 8, min.y + 3), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        drawList->AddText(ImVec2(min.x + 8, min.y + 5 + ImGui::GetTextLineHeight()), ImGui::GetColorU32(ImGuiCol_TextDisabled), path.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace molten::editor
