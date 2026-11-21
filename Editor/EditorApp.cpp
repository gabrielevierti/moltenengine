#include "Theme.hpp"
#include "EditorApp.hpp"

#include "GameBuild.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include "Core/Paths.hpp"
#include "Core/TextFile.hpp"

#include <sstream>
#include <unordered_map>
#include <imgui_internal.h>         // the dock builder, for the default layout

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <mutex>

namespace molten::editor {

namespace fs = std::filesystem;

namespace {

const char* const kVersion = "1.0";
/// Where the engine's source is.
const char* const kRepository = "https://github.com/gabrielevierti/moltenengine";

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
                                   : level[0] == 'w' ? ConsoleMessage::Level::Warning
                                   : level[0] == 'd' ? ConsoleMessage::Level::Debug : ConsoleMessage::Level::Info;
        std::lock_guard<std::mutex> lock(logQueue().mutex);
        logQueue().waiting.push_back({kind, message});
    };

    std::string userData = userDataDirectory();
    // The editor's own settings: its look, and the shortcuts changed.
    settingsFile_ = userData + "/editor-settings.txt";
    if (std::optional<std::string> settings = text::readFile(settingsFile_)) {
        std::string shortcutLines;
        std::istringstream lines(*settings);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("theme ", 0) == 0) setTheme(line.substr(6));
            else if (line.rfind("shortcut ", 0) == 0) shortcutLines += line.substr(9) + "\n";
        }
        shortcuts_.fromText(shortcutLines);
        if (ImGui::GetCurrentContext()) applyTheme(1.0f);       // otherwise the interface starts with it
    }
    recentFile_ = userData + "/recent-projects.txt";
    recent_.load(recentFile_);
    const char* home = std::getenv("HOME");
    projectWindow_.location = std::string(home ? home : ".") + "/Documents";
    templates_ = projectTemplates(findRootDirectory() + "/Templates");
    makeMissingTemplatePreviews();

    std::string layoutFile = userData + "/layout-6.ini";     // a new name when the default changes: it is then laid out afresh
    std::error_code ignored;
    needsDefaultLayout_ = !fs::exists(layoutFile, ignored);
    if (!engine.window()) needsDefaultLayout_ = true;       // a capture shows the editor as it first opens
    imgui_.start(engine, layoutFile);

    registerCreateEntries(editor);
    registerBuiltinComponents(editor);
    registerHierarchyPanel(editor);
    registerInspectorPanel(editor);
    registerAssetBrowserPanel(editor);
    registerAssetViewerPanel(editor);
    registerConsolePanel(editor);
    registerProfilerPanel(editor);
    registerTimelinePanel(editor);
    registerAnimationPanel(editor);
    registerAnimatorPanel(editor);
    registerSourceControlPanel(editor);
    registerSceneWeightPanel(editor);
    registerRenderingPanel(editor);
    registerProjectPanel(editor);
    registerVisualScriptPanel(editor);
    // The editor opens on its project window, at once: no logo to wait for, and no empty
    // editor shown on the way. (Games made with it still have their splash.)
    splashSeconds_ = Splash::kDuration;
    projectWindow_.logoPath = findRootDirectory() + "/Assets/Branding/logo.png";

    // Plugins that come with the editor live next to its executable.
    if (!engine.config().executablePath.empty()) {
        fs::path beside = fs::absolute(fs::path(engine.config().executablePath), ignored).parent_path() / "Plugins";
        plugins_.loadDirectory(editor, beside.string(), false);
    }

    editor.scene.newScene();
    // What an empty project starts with, for its card in the project window.
    emptyScenePicture_ = makeScenePicture(editor, 640, 360);
    projectWindow_.emptyPicture = ImGuiLayer::textureId(emptyScenePicture_.get());
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
        projectWindow_.error = error;
        editor.notify(error, ConsoleMessage::Level::Error);
        recent_.remove(path);
        recent_.save(recentFile_);
        return false;
    }
    closeProject();
    editor.project = *project;
    // While the game is tried here, what it keeps for the player is kept with the project.
    editor.engine.preferencesFile = project->libraryDirectory() + "/Preferences.txt";
    editor.hasProject = true;
    editor.engine.assets().setProjectDirectory(editor.project.assetsDirectory());
    assetChanges_.forget();
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
    projectWindow_.error.clear();
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
    else saveProjectPreview();
}

/// The picture the project is shown with where projects are listed: what the game's camera
/// sees now (or the editor's, in a scene without one), in the project's folder.
void EditorApp::saveProjectPreview() {
    EditorContext& editor = *context_;
    if (!editor.hasProject) return;
    int width = 0, height = 0;
    std::vector<uint8_t> pixels = editor.engine.renderer().readView(1, width, height);
    if (pixels.empty()) pixels = editor.engine.renderer().readView(0, width, height);
    if (pixels.empty() || width < 16 || height < 16) return;
    // Half size or less is plenty for a card, and keeps the file small.
    int step = std::max(1, width / 640);
    int smallWidth = width / step, smallHeight = height / step;
    std::vector<uint8_t> small(size_t(smallWidth) * size_t(smallHeight) * 4);
    for (int y = 0; y < smallHeight; y++) {
        for (int x = 0; x < smallWidth; x++) {
            // The average of each block, so thin things do not flicker away.
            int sum[3] = {0, 0, 0};
            for (int dy = 0; dy < step; dy++) {
                for (int dx = 0; dx < step; dx++) {
                    const uint8_t* from = &pixels[(size_t(y * step + dy) * size_t(width) + size_t(x * step + dx)) * 4];
                    for (int c = 0; c < 3; c++) sum[c] += from[c];
                }
            }
            uint8_t* to = &small[(size_t(y) * size_t(smallWidth) + size_t(x)) * 4];
            for (int c = 0; c < 3; c++) to[c] = uint8_t(sum[c] / (step * step));
            to[3] = 255;
        }
    }
    editor.engine.writeImage(editor.project.directory + "/preview.png", small, smallWidth, smallHeight);
}

/// Templates are shown with a picture of their scene. One that has none yet gets it made:
/// the player runs the template for a moment, without a window, and saves what it sees.
void EditorApp::makeMissingTemplatePreviews() {
    if (!engine_->window()) return;         // not from a run that is itself without a window
    std::error_code code;
    fs::path player = fs::absolute(fs::path(engine_->config().executablePath), code).parent_path() / "molten-player";
    if (!fs::exists(player, code)) return;
    std::string commands;
    for (const ProjectTemplate& item : templates_) {
        fs::path picture = fs::path(item.directory) / "preview.png";
        if (fs::exists(picture, code)) continue;
        commands += "\"" + player.string() + "\" \"" + item.directory + "\" --capture \"" + picture.string() + "\" --frames 150 --width 640 --height 360 --scale 2 >/dev/null 2>&1; ";
    }
    if (commands.empty()) return;
    // In the background, one after the other: the editor does not wait, and shows each as it appears.
    std::thread([commands] { std::system(commands.c_str()); }).detach();
}

/// A picture file as something ImGui can draw, read again when the file changes. 0 if there is none.
ImTextureID EditorApp::pictureAt(const std::string& path) {
    std::error_code code;
    auto time = fs::last_write_time(path, code);
    if (code) return ImTextureID(0);
    auto found = pictures_.find(path);
    // A file still being written is tried again a little later.
    if (found != pictures_.end() && found->second.time == time) return ImGuiLayer::textureId(found->second.texture.get());
    auto age = fs::file_time_type::clock::now() - time;
    if (age < std::chrono::milliseconds(700)) return found != pictures_.end() ? ImGuiLayer::textureId(found->second.texture.get()) : ImTextureID(0);
    Picture picture;
    picture.time = time;
    picture.texture = Texture::load(engine_->renderer().device(), path, TextureFormat::RGBA8_sRGB, false);
    if (found != pictures_.end() && found->second.texture) engine_->renderer().waitUntilIdle();    // the one it replaces may be in a frame on its way
    pictures_[path] = picture;
    return ImGuiLayer::textureId(picture.texture.get());
}

/// Compiles the project's Source folder and puts the result under the open scene.
/// Returns false if the code did not compile; the code that was loaded before stays then.
bool EditorApp::buildGame() {
    EditorContext& editor = *context_;
    if (!editor.hasProject) return false;
    builtSignature_ = gameSourceSignature(editor.project);      // whatever happens, this state of the code has been tried
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
        lastBuildFailed_ = true;
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
    lastBuildFailed_ = false;
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
    std::string error = exportMacApplication(editor.project, destination, player, engineRoot, &application, exportsDevelopment_);
    if (!error.empty()) {
        editor.notify(error, ConsoleMessage::Level::Error);
        if (application.empty()) return;
    } else {
        editor.notify("Exported " + application);
    }
    // For other Macs: signed with the Developer ID and notarized, beside the editor (it takes minutes).
    if (exportsForOtherMacs_ && error.empty()) {
        deviceLines_.clear();
        if (!deviceExport_.startScript(DeviceExport::notarizeScript(application, "molten"))) {
            editor.notify("Something is already being built: notarize when it has finished", ConsoleMessage::Level::Warning);
        }
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

void EditorApp::draw(Engine& engine, Canvas& canvas) {
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
        // Written again (a picture saved in another program, a model exported again): read
        // into what already uses it, so the scene changes without being opened again.
        std::vector<std::pair<std::string, int64_t>> files;
        for (const AssetInfo& asset : editor.assets.assets()) {
            bool isReadAgain = asset.type == AssetType::Texture || asset.type == AssetType::Mesh || asset.type == AssetType::Material ||
                               asset.type == AssetType::Audio;
            if (isReadAgain) files.push_back({asset.path, asset.modified});
        }
        std::vector<std::string> changed = assetChanges_.look(files, engine.time.elapsed);
        if (!changed.empty()) {
            engine.renderer().waitUntilIdle();          // nothing the GPU is drawing is replaced under it
            for (const std::string& path : changed) {
                // Said for files written elsewhere; a material saved here is read back without a word.
                bool isMaterial = path.size() > 9 && path.compare(path.size() - 9, 9, ".material") == 0;
                if (engine.assets().refreshInPlace(path) > 0 && !isMaterial) editor.notify("Read again: " + path);
            }
        }
    }

    // Code saved in another program (Xcode, any text editor) is rebuilt by itself.
    if (editor.hasProject && !editor.scene.isPlaying() && engine.time.elapsed >= nextSourceCheck_) {
        nextSourceCheck_ = engine.time.elapsed + 1.0;
        if (gameSourceSignature(editor.project) != builtSignature_) buildGame();
    }

    handleWindowClose();
    runPendingAction();
    // What putting the game on a device says, as it says it: kept for its window, and in the Console.
    for (std::string& line : deviceExport_.takeLines()) {
        bool isError = line.find("error") != std::string::npos;
        editor.console.push_back(ConsoleMessage{isError ? ConsoleMessage::Level::Error : ConsoleMessage::Level::Info, line, engine.time.elapsed, 1});
        deviceLines_.push_back(std::move(line));
        if (deviceLines_.size() > 200) deviceLines_.erase(deviceLines_.begin());
    }

    // The splash has the window to itself: the engine's canvas draws it, and no panel is put over it.
    if (!Splash::isOver(splashSeconds_)) {
        engine.renderer().sceneViewport = Renderer::Rect{};
        Splash::draw(canvas, splashSeconds_, std::string("Editor  ") + kVersion);
        splashSeconds_ += std::min(engine.time.unscaledDelta, 0.1f);
        if (engine.input.wasPressed(MouseButton::Left) || engine.input.wasPressed(Key::Space) || engine.input.wasPressed(Key::Escape)) {
            splashSeconds_ = std::max(splashSeconds_, Splash::kDuration - 0.5f);
        }
        imgui_.beginFrame();
        imgui_.endFrame();
        return;
    }

    applyPendingLayout();
    imgui_.beginFrame();
    plugins_.update(editor, engine.time.unscaledDelta);
    handleShortcuts();
    drawMenuBar();
    if (editor.hasProject) drawPlayBar();
    if (editor.hasProject) drawStatusBar();       // it speaks of the scene: nothing to say without one
    if (editor.hasProject) {
        drawDockSpace();
        drawPanels();
    } else {
        hideSceneAndGameViews(editor);
        drawProjectList();
    }
    drawUnsavedChangesDialog();
    drawAboutDialog();
    drawCommandPalette();
    drawSettingsWindow();
    drawBuildWindow();
drawNewScriptDialog();
    drawSaveLayoutDialog();

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

/// ImGui's number for a key, by the name it gives it ("S", "F5", "Comma").
static ImGuiKey keyNamed(const std::string& name) {
    static std::unordered_map<std::string, ImGuiKey> keys;
    if (keys.empty()) {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) keys[ImGui::GetKeyName(ImGuiKey(key))] = ImGuiKey(key);
    }
    auto found = keys.find(name);
    return found == keys.end() ? ImGuiKey_None : found->second;
}

bool EditorApp::isPressed(const std::string& action) {
    if (!recording_.empty()) return false;              // the keys are being taken for a shortcut
    const KeyChord& chord = shortcuts_.chord(action);
    ImGuiKey key = keyNamed(chord.key);
    if (key == ImGuiKey_None) return false;
    ImGuiKeyChord mods = (chord.shift ? ImGuiMod_Shift : 0) | (chord.alt ? ImGuiMod_Alt : 0);
    // ImGui calls the Command key "Ctrl" on a Mac; the key that says Control works too: hands
    // that learned the shortcuts elsewhere find them here.
    if (!chord.command) return ImGui::Shortcut(mods | key, ImGuiInputFlags_RouteGlobal);
    return ImGui::Shortcut(mods | ImGuiMod_Ctrl | key, ImGuiInputFlags_RouteGlobal) || ImGui::Shortcut(mods | ImGuiMod_Super | key, ImGuiInputFlags_RouteGlobal);
}

void EditorApp::handleShortcuts() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    ImGuiIO& io = ImGui::GetIO();

    if (isPressed("quit")) editor.requestQuit();
    if (isPressed("findAnything") && !(scene.isPlaying() && editor.gameViewHasFocus)) palette_.open();
    if (isPressed("settings")) showsSettings_ = true;
    if (isPressed("openProject")) chooseAndOpenProject();
    else if (!editor.hasProject && isPressed("openScene")) chooseAndOpenScene();
    if (!editor.hasProject) return;

    if (isPressed("pause")) {
        if (scene.isPlaying()) scene.setPaused(!scene.isPaused());
    } else if (isPressed("step")) {
        scene.stepFrame();
    } else if (isPressed("play")) {
        scene.setPlaying(!scene.isPlaying());
    }
    // While the game has the keys, the arrows and Space are the game's: the editor's own keyboard
    // navigation (moving between buttons and lists, showing their tips) is off until it lets go.
    {
        ImGuiIO& io = ImGui::GetIO();
        if (scene.isPlaying() && editor.gameViewHasFocus) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    if (scene.isPlaying() && editor.gameViewHasFocus) {
        // The keyboard is the game's, but for the way out.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) scene.setPlaying(false);
        return;
    }
    const bool isEditing = !scene.isPlaying();
    if (isEditing) {
        if (isPressed("saveAs")) saveScene(true);
        else if (isPressed("save")) saveScene(false);
        if (isPressed("newScene")) editor.requestNewScene();
        if (isPressed("openScene")) chooseAndOpenScene();
        if (isPressed("export")) showsBuild_ = true;
        else if (isPressed("buildScripts")) buildGame();
    }
    if (isPressed("alignWithView")) editor.alignWithViewRequested = true;

    // These would fight with typing in a text field, or belong to a panel with a document of its own.
    bool panelHasThem = editor.panelOwnsEditKeys;
    editor.panelOwnsEditKeys = false;           // the panel says so again this frame if it still has the keyboard
    if (io.WantTextInput || panelHasThem) return;
    if (isEditing) {
        bool redoes = isPressed("redo") || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal);
        if (redoes) scene.redo();
        else if (isPressed("undo")) scene.undo();
    }
    if (isPressed("duplicate")) scene.duplicateSelection();
    if (isPressed("selectAll")) scene.selectAll();
    if (!ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        // Only when the scene is what the user is working on, not the asset browser.
        if (!scene.selection().empty() && editor.selectedAsset.empty()) scene.deleteSelection();
    }
}

// ---------------------------------------------------------------- Edit > Editor Settings

void EditorApp::saveEditorSettings() {
    std::string out = "theme " + currentTheme() + "\n";
    std::istringstream changed(shortcuts_.toText());
    std::string line;
    while (std::getline(changed, line)) out += "shortcut " + line + "\n";
    std::error_code code;
    fs::create_directories(fs::path(settingsFile_).parent_path(), code);
    text::writeFile(settingsFile_, out);
}

void EditorApp::drawSettingsWindow() {
    if (!showsSettings_) {
        recording_.clear();
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Editor Settings", &showsSettings_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##settings")) {
        if (ImGui::BeginTabItem("Shortcuts")) {
            // Taking the keys for one: the next key pressed, with what is held with it.
            if (!recording_.empty()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    recording_.clear();
                } else {
                    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && !recording_.empty(); key++) {
                        ImGuiKey k = ImGuiKey(key);
                        bool isModifier = k == ImGuiKey_LeftCtrl || k == ImGuiKey_RightCtrl || k == ImGuiKey_LeftShift || k == ImGuiKey_RightShift ||
                                          k == ImGuiKey_LeftAlt || k == ImGuiKey_RightAlt || k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper ||
                                          (k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY) || k >= ImGuiKey_ReservedForModCtrl;
                        if (isModifier || !ImGui::IsKeyPressed(k, false)) continue;
                        ImGuiIO& io = ImGui::GetIO();
                        KeyChord chord;
                        chord.command = io.KeyCtrl || io.KeySuper;
                        chord.shift = io.KeyShift;
                        chord.alt = io.KeyAlt;
                        chord.key = ImGui::GetKeyName(k);
                        // Backspace alone takes the shortcut away.
                        if (k == ImGuiKey_Backspace && !chord.command && !chord.shift && !chord.alt) chord = KeyChord{};
                        shortcuts_.set(recording_, chord);
                        recording_.clear();
                        saveEditorSettings();
                    }
                }
            }
            ImGui::TextDisabled("Click one, then press its new keys. Backspace: none. Esc: as it was.");
            if (ImGui::BeginTable("##shortcuts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, -34))) {
                ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 170);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
                std::string group;
                for (const Shortcuts::Action& action : shortcuts_.actions()) {
                    ImGui::PushID(action.id.c_str());
                    if (action.group != group) {
                        group = action.group;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", group.c_str());
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(action.title.c_str());
                    std::vector<std::string> sharing = shortcuts_.sharing(action.id);
                    if (!sharing.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1), "also %s", sharing.front().c_str());
                    }
                    ImGui::TableNextColumn();
                    bool isTaking = recording_ == action.id;
                    std::string shown = isTaking ? std::string("Press the keys...") : shortcuts_.text(action.id);
                    if (shown.empty()) shown = "none";
                    if (ImGui::Button(shown.c_str(), ImVec2(-1, 0))) recording_ = isTaking ? std::string() : action.id;
                    ImGui::TableNextColumn();
                    if (!(action.chord == action.original)) {
                        if (ImGui::SmallButton("Reset")) {
                            shortcuts_.reset(action.id);
                            saveEditorSettings();
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("All as They Were")) {
                shortcuts_.resetAll();
                recording_.clear();
                saveEditorSettings();
            }
            ImGui::EndTabItem();
        } else {
            recording_.clear();
        }
        if (ImGui::BeginTabItem("Look")) {
            ImGui::TextDisabled("The colors of the editor. The game's own are in its scenes.");
            for (const std::string& name : themeNames()) {
                if (ImGui::RadioButton(name.c_str(), currentTheme() == name)) {
                    setTheme(name);
                    applyTheme(1.0f);
                    saveEditorSettings();
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorApp::chooseAndOpenProject() {
    std::string folder = platform::chooseFolder("Choose the folder of a Molten Metal Engine project");
    if (!folder.empty()) context_->requestOpenProject(folder);
}

void EditorApp::chooseAndOpenScene() {
    EditorContext& editor = *context_;
    std::string start = editor.hasProject ? editor.assets.absolutePath(editor.assets.find("Scenes") ? "Scenes" : "") : std::string();
    // One window for both: a scene of this project, or another project by its .molten file.
    std::vector<std::string> chosen = platform::chooseFiles("Open a scene, or a project (its .molten file)", {"scene", "molten"}, false, start);
    if (chosen.empty()) return;
    if (fs::path(chosen.front()).extension() == ".molten") editor.requestOpenProject(fs::path(chosen.front()).parent_path().string());
    else if (editor.hasProject) editor.requestOpenScene(chosen.front());
    else editor.notify("Open a project first: a scene belongs to one", ConsoleMessage::Level::Warning);
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
        if (ImGui::MenuItem("New Scene", shortcuts_.text("newScene").c_str(), false, isEditing)) editor.requestNewScene();
        if (ImGui::MenuItem("Open...", shortcuts_.text("openScene").c_str())) chooseAndOpenScene();
        ui::tooltip("A scene of this project, or another project (its .molten file)");
        if (ImGui::BeginMenu("Open Scene of the Project", isEditing)) {
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
        if (ImGui::MenuItem("Save Scene", shortcuts_.text("save").c_str(), false, isEditing)) saveScene(false);
        if (ImGui::MenuItem("Save Scene As New File", shortcuts_.text("saveAs").c_str(), false, isEditing)) saveScene(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Open Project...", shortcuts_.text("openProject").c_str())) chooseAndOpenProject();
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
        if (ImGui::MenuItem("Quit", shortcuts_.text("quit").c_str())) editor.requestQuit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", shortcuts_.text("undo").c_str(), false, scene.canUndo())) scene.undo();
        if (ImGui::MenuItem("Redo", shortcuts_.text("redo").c_str(), false, scene.canRedo())) scene.redo();
        ImGui::Separator();
        bool hasSelection = hasProject && !scene.selection().empty();
        if (ImGui::MenuItem("Duplicate", shortcuts_.text("duplicate").c_str(), false, hasSelection)) scene.duplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) scene.deleteSelection();
        if (ImGui::MenuItem("Select All", shortcuts_.text("selectAll").c_str(), false, isEditing)) scene.selectAll();
        if (ImGui::MenuItem("Frame Selection", "F", false, hasSelection)) editor.frameSelectionRequested = true;
        if (ImGui::MenuItem("Align with View", shortcuts_.text("alignWithView").c_str(), false, hasSelection)) editor.alignWithViewRequested = true;
        if (ImGui::MenuItem("Align View with Selection", nullptr, false, hasSelection)) scene.alignViewWithSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Find Anything...", shortcuts_.text("findAnything").c_str())) palette_.open();
        if (ImGui::MenuItem("Editor Settings...", shortcuts_.text("settings").c_str())) showsSettings_ = true;
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
        if (ImGui::MenuItem(scene.isPlaying() ? "Stop" : "Play", shortcuts_.text("play").c_str())) scene.setPlaying(!scene.isPlaying());
        if (ImGui::MenuItem(scene.isPaused() ? "Resume" : "Pause", shortcuts_.text("pause").c_str(), false, scene.isPlaying())) scene.setPaused(!scene.isPaused());
        if (ImGui::MenuItem("Step One Frame", shortcuts_.text("step").c_str(), false, scene.isPlaying())) scene.stepFrame();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Game", hasProject)) {
        if (ImGui::MenuItem("New Script...")) {
            showsNewScript_ = true;
            newScriptError_.clear();
        }
        if (ImGui::MenuItem("New Visual Script")) {
            std::string folder = editor.assets.find("Scripts") ? "Scripts" : editor.assets.createFolder("", "Scripts");
            std::string path = editor.assets.uniquePath(folder, "New Script", ".graph");
            if (text::writeFile(editor.assets.absolutePath(path), script::Graph::starter().toText())) {
                editor.assets.refresh();
                editor.selectedAsset = path;
                editor.currentFolder = folder;
                editor.editGraph(path);
            }
        }
        // The project's scripts of both kinds: a click opens one where it is edited.
        if (ImGui::BeginMenu("Scripts")) {
            std::vector<std::string> sources = gameSourceFiles(editor.project);
            for (const std::string& source : sources) {
                if (ImGui::MenuItem(fs::path(source).filename().string().c_str(), "code")) platform::openWithDefaultApplication(source);
            }
            bool any = !sources.empty();
            for (const AssetInfo& asset : editor.assets.assets()) {
                if (asset.type != AssetType::Graph) continue;
                any = true;
                std::string path = asset.path;
                if (ImGui::MenuItem(asset.name.c_str(), "visual")) editor.editGraph(path);
            }
            if (!any) ImGui::TextDisabled("None yet.");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Build Scripts Now", shortcuts_.text("buildScripts").c_str())) buildGame();
        ui::tooltip("Scripts are built by themselves when one is saved, and before the game is exported: this is for doing it by hand");
        if (ImGui::MenuItem("Show Scripts Folder")) platform::revealInFinder(editor.project.sourceDirectory());
        if (ImGui::BeginMenu("Components", gameModule_.isLoaded())) {
            for (const std::string& name : gameModule_.componentNames()) ImGui::MenuItem(name.c_str(), nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Build and Export...", shortcuts_.text("export").c_str())) showsBuild_ = true;
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
        if (ImGui::BeginMenu("Layouts")) {
            drawLayoutItems();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset Layout")) pendingLayout_ = "Default";
        drawRegisteredMenuItems("Window");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Molten Metal Engine")) showsAbout_ = true;
        if (ImGui::MenuItem("Molten Engine on GitHub")) platform::openWebPage(kRepository);
        drawRegisteredMenuItems("Help");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------- Cmd+K

std::vector<PaletteCommand> EditorApp::paletteCommands() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    const bool hasProject = editor.hasProject, isEditing = hasProject && !scene.isPlaying(), hasSelection = !scene.selection().empty();
    std::vector<PaletteCommand> commands;
    auto add = [&](std::string title, std::string kind, std::string shortcut, bool isEnabled, std::function<void()> run) {
        commands.push_back(PaletteCommand{std::move(title), std::move(kind), std::move(shortcut), isEnabled, std::move(run)});
    };
    // What the menus do.
    add("New Scene", "File", shortcuts_.text("newScene"), isEditing, [&editor] { editor.requestNewScene(); });
    add("Open Scene...", "File", shortcuts_.text("openScene"), true, [this] { chooseAndOpenScene(); });
    add("Save Scene", "File", shortcuts_.text("save"), isEditing, [this] { saveScene(false); });
    add("Save Scene As New File", "File", shortcuts_.text("saveAs"), isEditing, [this] { saveScene(true); });
    add("Open Project...", "File", shortcuts_.text("openProject"), true, [this] { chooseAndOpenProject(); });
    add("Undo", "Edit", shortcuts_.text("undo"), isEditing && scene.canUndo(), [&scene] { scene.undo(); });
    add("Redo", "Edit", shortcuts_.text("redo"), isEditing && scene.canRedo(), [&scene] { scene.redo(); });
    add("Duplicate", "Edit", shortcuts_.text("duplicate"), hasSelection, [&scene] { scene.duplicateSelection(); });
    add("Delete", "Edit", "Del", hasSelection, [&scene] { scene.deleteSelection(); });
    add("Select All", "Edit", shortcuts_.text("selectAll"), isEditing, [&scene] { scene.selectAll(); });
    add("Frame Selection", "Edit", "F", hasSelection, [&editor] { editor.frameSelectionRequested = true; });
    add("Align with View", "Edit", shortcuts_.text("alignWithView"), hasSelection, [&editor] { editor.alignWithViewRequested = true; });
    add("Align View with Selection", "Edit", "", hasSelection, [&scene] { scene.alignViewWithSelection(); });
    add("Project Settings", "Edit", "", hasProject, [&editor] {
        if (Panel* panel = editor.registry.findPanel("Project Settings")) panel->isOpen = true;
    });
    add(scene.isPlaying() ? "Stop" : "Play", "Play", shortcuts_.text("play"), hasProject, [&scene] { scene.setPlaying(!scene.isPlaying()); });
    add(scene.isPaused() ? "Resume" : "Pause", "Play", shortcuts_.text("pause"), scene.isPlaying(), [&scene] { scene.setPaused(!scene.isPaused()); });
    add("Step One Frame", "Play", shortcuts_.text("step"), scene.isPlaying(), [&scene] { scene.stepFrame(); });
    add("New Script...", "Game", "", hasProject, [this] {
        showsNewScript_ = true;
        newScriptError_.clear();
    });
    add("Build Scripts Now", "Game", shortcuts_.text("buildScripts"), hasProject, [this] { buildGame(); });
    add("Build and Export...", "Game", shortcuts_.text("export"), hasProject, [this] { showsBuild_ = true; });
    add("Show Scripts Folder", "Game", "", hasProject, [&editor] { platform::revealInFinder(editor.project.sourceDirectory()); });
    add(editor.engine.showsColliders ? "Hide All Colliders" : "Show All Colliders", "Window", "", true,
        [&editor] { editor.engine.showsColliders = !editor.engine.showsColliders; });
    add("Reset Layout", "Window", "", true, [this] { pendingLayout_ = "Default"; });
    add("About Molten Metal Engine", "Help", "", true, [this] { showsAbout_ = true; });
    add("Editor Settings: Shortcuts and Look", "Edit", shortcuts_.text("settings"), true, [this] { showsSettings_ = true; });
    // The windows: opened and brought to the front.
    for (auto& item : editor.registry.panels) {
        Panel* panel = item.value.get();
        add(panel->title, "Window", "", true, [panel] {
            panel->isOpen = true;
            ImGui::SetWindowFocus(panel->title.c_str());
        });
    }
    // What plugins put in the menus.
    for (const auto& item : editor.registry.menuItems) {
        std::string path = item.value.path;
        size_t slash = path.find('/');
        std::string title = path.substr(path.rfind('/') + 1), kind = slash == std::string::npos ? std::string() : path.substr(0, slash);
        bool isEnabled = !item.value.isEnabled || item.value.isEnabled(editor);
        std::function<void(EditorContext&)> action = item.value.action;
        add(title, kind, item.value.shortcut, isEnabled && action != nullptr, [action, &editor] { action(editor); });
    }
    // The Create menu.
    for (const auto& item : editor.registry.createEntries) {
        CreateEntry entry = item.value;
        std::string title = entry.path.substr(entry.path.rfind('/') + 1);
        add("Create " + title, "Create", "", isEditing, [entry, &editor] { createFromEntry(editor, entry, Entity{}); });
    }
    // The scene's entities: chosen, and the view goes to them.
    Scene& world = editor.engine.scene;
    world.each<Name>([&](Entity entity, Name& name) {
        if (name.value.empty()) return;
        // Where it is: "Rival 2 / Model", so that the many "Wheels" can be told apart, and found.
        std::string path;
        const Transform* transform = world.get<Transform>(entity);
        for (int depth = 0; transform && world.isAlive(transform->parent) && depth < 16; depth++) {
            const Name* parent = world.get<Name>(transform->parent);
            path = (parent ? parent->value : std::string("?")) + (path.empty() ? "" : " / " + path);
            transform = world.get<Transform>(transform->parent);
        }
        add(name.value, path.empty() ? "Entity" : path, "", hasProject, [&editor, entity] {
            editor.selectedAsset.clear();
            editor.scene.select(entity);
            editor.frameSelectionRequested = true;
        });
        commands.back().context = path;
    });
    // The project's assets: a scene opens, a visual script is edited, anything else is shown in the Explorer.
    if (hasProject) {
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.isFolder()) continue;
            std::string path = asset.path, folder = asset.parent();
            AssetType type = asset.type;
            add(asset.path, type == AssetType::Scene ? "Scene" : "Asset", "", true, [&editor, path, folder, type] {
                if (type == AssetType::Scene) {
                    editor.requestOpenScene(editor.assets.absolutePath(path));
                } else if (type == AssetType::Graph) {
                    editor.editGraph(path);
                } else {
                    editor.scene.clearSelection();
                    editor.currentFolder = folder;
                    editor.selectedAsset = path;
                    if (Panel* panel = editor.registry.findPanel("Explorer")) {
                        panel->isOpen = true;
                        ImGui::SetWindowFocus("Explorer");
                    }
                }
            });
        }
    }
    return commands;
}

void EditorApp::drawCommandPalette() {
    if (!palette_.isOpen()) return;
    palette_.draw(paletteCommands());
}

void EditorApp::drawAboutDialog() {
    if (showsAbout_ && !ImGui::IsPopupOpen("About Molten Metal Engine")) ImGui::OpenPopup("About Molten Metal Engine");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("About Molten Metal Engine", &showsAbout_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Molten Metal Engine %s", kVersion);
    ImGui::TextDisabled("A game engine for Apple silicon, built on Metal.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Open source, by Gabriele Vierti.");
    // Its repository: the source, the issues, the releases.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.25f, 1.0f));
    ImGui::TextUnformatted(kRepository);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked()) platform::openWebPage(kRepository);
    ImGui::TextDisabled("Interface: Dear ImGui and ImGuizmo. Window: GLFW. Sound: miniaudio. Images: stb.");
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(100, 0))) {
        showsAbout_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/// From a project to a game that runs by itself, one step under the other: what the game
/// is called, where it starts, how it is played on a screen without keys, its scripts
/// (built here, by themselves), and the export.
void EditorApp::drawBuildWindow() {
    EditorContext& editor = *context_;
    if (!showsBuild_ || !editor.hasProject) {
        showsBuild_ = false;
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, std::min(720.0f, viewport->WorkSize.y - 60)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Build and Export", &showsBuild_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    Project& project = editor.project;
    bool changed = false;
    // A step's heading: its number in a disc, green with a tick's worth of meaning when it is in order.
    auto step = [](int number, const char* name, bool isInOrder) {
        ImGui::Dummy(ImVec2(0, 6));
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImU32 color = isInOrder ? IM_COL32(96, 190, 120, 255) : IM_COL32(255, 122, 47, 255);
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + 11, at.y + 11), 11, color, 24);
        char digit[4];
        std::snprintf(digit, sizeof(digit), "%d", number);
        ImVec2 size = ImGui::CalcTextSize(digit);
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 11 - size.x * 0.5f, at.y + 11 - size.y * 0.5f), IM_COL32(20, 20, 22, 255), digit);
        ImGui::SetCursorScreenPos(ImVec2(at.x + 32, at.y + 2));
        ImGui::PushFont(nullptr, 17.0f);
        ImGui::TextUnformatted(name);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));
    };

    // ---- 1
    step(1, "The game", !project.name.empty() && !project.identifier.empty());
    changed |= ui::text("Name", project.name);
    changed |= ui::text("Version", project.version);
    changed |= ui::text("Identifier", project.identifier);
    ui::tooltip("What the system knows the game by, like com.yourname.game. The App Store needs one of your own.");
    changed |= ui::checkbox("Splash Screen", project.showsSplash);
    ui::tooltip("The game opens with the engine's logo for a moment");
    {
        // Its icon: the engine's until the game has one of its own.
        std::string iconFile = project.iconFile(findRootDirectory());
        ImTextureID icon = pictureAt(iconFile);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Icon");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f + ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x - ImGui::CalcTextSize("Icon").x);
        ImVec2 at = ImGui::GetCursorScreenPos();
        if (icon) ImGui::Image(icon, ImVec2(56, 56));
        else ImGui::Dummy(ImVec2(56, 56));
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button("Choose a Picture...")) {
            std::vector<std::string> chosen = platform::chooseFiles("Choose a square picture for the game's icon (1024 x 1024 is best)", {"png", "jpg", "jpeg"}, false, {});
            if (!chosen.empty()) {
                // Kept in the project, so that it travels with it.
                std::string name = "Icon" + fs::path(chosen.front()).extension().string();
                std::error_code code;
                fs::copy_file(chosen.front(), fs::path(project.assetsDirectory()) / name, fs::copy_options::overwrite_existing, code);
                if (code) editor.notify("Cannot copy the picture: " + code.message(), ConsoleMessage::Level::Error);
                else {
                    project.icon = name;
                    changed = true;
                    editor.assets.refresh();
                }
            }
        }
        ImGui::BeginDisabled(project.icon.empty());
        if (ImGui::Button("Use the Engine's")) {
            project.icon.clear();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        (void)at;
    }

    // ---- 2
    std::vector<std::string> scenes;
    for (const AssetInfo& asset : editor.assets.assets()) {
        if (asset.type == AssetType::Scene) scenes.push_back(asset.path);
    }
    int first = -1;
    for (size_t i = 0; i < scenes.size(); i++) {
        if (scenes[i] == project.startScene) first = int(i);
    }
    step(2, "Where it starts", first >= 0);
    if (scenes.empty()) {
        ImGui::TextWrapped("The project has no scene saved yet. Save the one that is open (File > Save Scene) and it becomes the first.");
    } else if (ui::combo("First Scene", first, scenes)) {
        project.startScene = scenes[size_t(first)];
        changed = true;
    }
    ImGui::TextDisabled("%d %s in the project. The others are reached from the game (loadScene).", int(scenes.size()), scenes.size() == 1 ? "scene" : "scenes");

    // ---- 3
    int sticks = 0, buttons = 0;
    const ComponentType* stickType = ComponentRegistry::instance().find("TouchStick");
    const ComponentType* buttonType = ComponentRegistry::instance().find("TouchButton");
    for (Entity entity : editor.scene.entities()) {
        if (stickType && stickType->has(editor.engine.scene, entity)) sticks++;
        if (buttonType && buttonType->has(editor.engine.scene, entity)) buttons++;
        if (const kit::UIButton* button = editor.engine.scene.get<kit::UIButton>(entity); button && (button->isRound || !button->action.empty())) buttons++;
    }
    step(3, "How it is played", buildPlatform_ == 0 || sticks + buttons > 0);
    ImGui::TextWrapped("Keys, mouse and controllers are set in the Input window. On a phone or a tablet there are none: a Stick "
                       "and Round Buttons (Create > UI) push the same actions from the screen. Drag them into place in the Game view, and try them there with the mouse.");
    ImGui::TextDisabled("In the open scene: %d %s, %d %s with an action.", sticks, sticks == 1 ? "stick" : "sticks", buttons, buttons == 1 ? "button" : "buttons");
    ImGui::BeginDisabled(editor.scene.isPlaying() || !stickType);
    if (ImGui::Button("Add a Stick and a Button to the Scene")) {
        Scene& scene = editor.engine.scene;
        Entity stick = scene.create("Stick");
        scene.add(stick, Transform{});
        stickType->add(scene, stick);
        Entity button = scene.create("Round Button");
        scene.add(button, Transform{});
        kit::UIButton round;
        round.isRound = true;
        round.label = "A";
        round.action = "jump";
        round.anchor = Vec2{1, 1};
        round.offset = Vec2{-100, -110};
        round.color = Vec4{1, 1, 1, 0.6f};
        round.onlyOnTouchScreens = true;
        scene.add(button, round);
        editor.scene.markChanged();
        editor.notify("Added a Stick (moveX, moveZ) and a round Button (jump): change what they push in the Inspector");
    }
    ImGui::EndDisabled();
    if (ImGui::SameLine(); ImGui::Button("Open Input")) {
        if (Panel* panel = editor.registry.findPanel("Input")) panel->isOpen = true;
    }

    // ---- 4
    std::vector<std::string> sources = gameSourceFiles(project);
    step(4, "Its scripts", !lastBuildFailed_);
    if (sources.empty()) ImGui::TextDisabled("None: the game is made of the engine's own components.");
    else ImGui::TextDisabled("%d %s. Built when one is saved, and again when the game is exported: nothing to do here.", int(sources.size()), sources.size() == 1 ? "file" : "files");
    if (lastBuildFailed_) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "The last build failed: the compiler's words are in the Console.");
        if (ImGui::SameLine(); ImGui::SmallButton("Try Again")) buildGame();
    }

    // ---- 5
    step(5, "Export", true);
    ImGui::RadioButton("Mac", &buildPlatform_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("iPhone and iPad", &buildPlatform_, 1);
    if (buildPlatform_ == 0) {
        ImGui::TextWrapped("An application that runs by itself: the game, its assets and the engine in one. You choose the folder it is put in.");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.42f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.5f, 0.2f, 1.0f));
        ImGui::BeginDisabled(deviceExport_.isRunning());
        bool wantsExport = ImGui::Button("Export for Mac...", ImVec2(200, 34));
        ImGui::EndDisabled();
        ImGui::PopStyleColor(2);
        if (wantsExport) exportGame();          // saves the scene and builds the scripts first
        ImGui::SameLine();
        ImGui::Checkbox("Development", &exportsDevelopment_);
        ui::tooltip("A development build: F1 shows the frame rate, the draw calls and where each frame's time goes; F2 the colliders.\n"
                    "Leave it off for the build you give to players.");
        ImGui::SameLine();
        ImGui::Checkbox("For other Macs", &exportsForOtherMacs_);
        ui::tooltip("Signed with your Developer ID certificate and notarized by Apple, so that it opens on any Mac without warnings.\n"
                    "Needs a paid developer account, the certificate (Xcode > Settings > Accounts > Manage Certificates), and once, in Terminal:\n"
                    "xcrun notarytool store-credentials molten --apple-id <your Apple ID> --team-id <your team>\n"
                    "Without it the game runs on this Mac only.");
        if (deviceExport_.isRunning()) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Notarizing%s", &"..."[2 - int(ImGui::GetTime() * 2) % 3]);
        for (size_t i = deviceLines_.size() > 4 ? deviceLines_.size() - 4 : 0; i < deviceLines_.size(); i++) {
            bool isError = deviceLines_[i].find("error") != std::string::npos;
            ImGui::TextColored(isError ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", deviceLines_[i].c_str());
        }
    } else {
        // Three programs do this (CMake, Xcode, the device's own tools): the editor runs them,
        // beside itself, and says here and in the Console what they say.
        ImGui::TextWrapped("The game is made into an application for iPhone and iPad, signed with your Apple developer team, and put on the device that is plugged in. "
                           "It needs Xcode and CMake on this Mac.");
        std::string teamFile = userDataDirectory() + "/apple-team.txt";
        if (appleTeam_.empty() && !ImGui::IsAnyItemActive()) {
            if (std::optional<std::string> kept = text::readFile(teamFile)) {
                appleTeam_ = *kept;
                while (!appleTeam_.empty() && std::isspace(static_cast<unsigned char>(appleTeam_.back()))) appleTeam_.pop_back();
            }
        }
        if (ui::text("Team", appleTeam_)) text::writeFile(teamFile, appleTeam_ + "\n");
        ui::tooltip("Your Apple developer team identifier, ten letters and digits: developer.apple.com > Account > Membership details, or Xcode > Settings > Accounts.\n"
                    "Kept on this Mac, not in the project.");
        bool isBusy = deviceExport_.isRunning();
        auto begin = [&](DeviceExport::Goal goal) {
            if (editor.scene.isPlaying()) editor.scene.setPlaying(false);
            if (editor.scene.isDirty() || editor.scene.filePath.empty()) saveScene(false);
            if (project.startScene.empty()) {
                editor.notify("Save a scene first: the game starts with the project's first scene (step 2)", ConsoleMessage::Level::Warning);
                return;
            }
            deviceLines_.clear();
            deviceExport_.start(project, findRootDirectory(), appleTeam_, goal);
        };
        ImGui::BeginDisabled(isBusy);
        ImGui::BeginDisabled(appleTeam_.size() < 6);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.42f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.5f, 0.2f, 1.0f));
        if (ImGui::Button("Build and Put on the Device", ImVec2(250, 34))) begin(DeviceExport::Goal::Install);
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if (appleTeam_.size() < 6) ui::tooltip("Fill in Team first: an application for a device has to be signed");
        ImGui::SameLine();
        if (ImGui::Button("Open in Xcode", ImVec2(0, 34))) begin(DeviceExport::Goal::OpenInXcode);
        ui::tooltip("Makes the Xcode project with the game in it and opens it: for choosing a simulator, or seeing a signing problem");
        ImGui::SameLine();
        ImGui::BeginDisabled(appleTeam_.size() < 6);
        if (ImGui::Button("Send to App Store Connect", ImVec2(0, 34))) begin(DeviceExport::Goal::AppStore);
        ImGui::EndDisabled();
        ui::tooltip("Archives the game for release and sends it to App Store Connect, where it appears under TestFlight and can be sent for review.\n"
                    "Before the first time: create the app in App Store Connect with the project's identifier (Project Settings), and sign in to Xcode with your account.\n"
                    "The version is the project's; the build number is the date and time, so each one sent is new.");
        ImGui::EndDisabled();
        if (isBusy) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Working%s", &"..."[2 - int(ImGui::GetTime() * 2) % 3]);
        } else if (deviceExport_.hasEnded()) {
            ImGui::SameLine();
            if (deviceExport_.succeeded()) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), "Done");
            else ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "It did not work: see below and the Console");
        }
        // The last things the tools said.
        if (!deviceLines_.empty()) {
            ImGui::BeginChild("##device-lines", ImVec2(0, 120), ImGuiChildFlags_Borders);
            for (const std::string& line : deviceLines_) {
                bool isError = line.find("error") != std::string::npos;
                bool isStep = line.rfind("== ", 0) == 0;
                if (isError) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
                else if (!isStep) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", isStep ? line.c_str() + 3 : line.c_str());
                if (isError || !isStep) ImGui::PopStyleColor();
            }
            if (isBusy) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        if (sticks + buttons == 0) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), "The open scene has no touch controls (step 3): it cannot be played without keys.");
    }

    if (changed) {
        if (project.name.empty()) project.name = "Untitled";
        std::string error;
        if (!project.save(&error)) editor.notify(error, ConsoleMessage::Level::Error);
        updateTitle();
    }
    ImGui::End();
}

/// Asks for the name of a new script, makes its file, opens it and builds.
void EditorApp::drawNewScriptDialog() {
    EditorContext& editor = *context_;
    if (editor.wantsNewScript) {
        editor.wantsNewScript = false;
        showsNewScript_ = true;
        newScriptError_.clear();
    }
    if (showsNewScript_ && !ImGui::IsPopupOpen("New Script")) ImGui::OpenPopup("New Script");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Script", &showsNewScript_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("A script is a component written in C++.");
    ImGui::TextDisabled("Its name is the name it has in Add Component.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(320);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool entered = ImGui::InputText("##name", &newScriptName_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (!newScriptError_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1), "%s", newScriptError_.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Create", ImVec2(110, 0)) || entered) {
        std::string file = createScriptFile(editor.project, newScriptName_, &newScriptError_);
        if (!file.empty()) {
            showsNewScript_ = false;
            ImGui::CloseCurrentPopup();
            editor.assets.refresh();
            editor.notify("Created Assets/Scripts/" + newScriptName_ + ".cpp");
            platform::openWithDefaultApplication(file);     // in Xcode, or whatever opens .cpp files here
            buildGame();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
        showsNewScript_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/// The bar under the menus: play, pause and step in the middle, where they are in reach
/// whichever view is in front; the layout of the windows on the right.
void EditorApp::drawPlayBar() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    const float height = 38;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    bool isOpen = ImGui::BeginViewportSideBar("##playbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (isOpen) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool isPlaying = scene.isPlaying(), isPaused = scene.isPaused();
        // Molten's own controls: three labelled keys at the left, and what the scene is doing beside them.
        const float keyHeight = 26;
        enum class Picture { Run, Stop, Hold, Frame };
        auto control = [&](const char* id, const char* label, Picture picture, bool isLit, bool isEnabled, ImU32 litColor, const char* tooltip) {
            float width = 34 + ImGui::CalcTextSize(label).x + 12;
            ImGui::BeginDisabled(!isEnabled);
            ImVec2 min = ImGui::GetCursorScreenPos();
            bool pressed = ImGui::InvisibleButton(id, ImVec2(width, keyHeight));
            bool isHovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
            ImVec2 max(min.x + width, min.y + keyHeight), c(min.x + 18, (min.y + max.y) * 0.5f);
            ImU32 ink = !isEnabled ? IM_COL32(96, 102, 114, 255) : isLit ? litColor : IM_COL32(223, 227, 234, 255);
            drawList->AddRectFilled(min, max, isHovered ? IM_COL32(58, 64, 76, 255) : IM_COL32(40, 44, 52, 255), keyHeight * 0.5f);
            if (isLit) drawList->AddRect(min, max, litColor, keyHeight * 0.5f, ImDrawFlags_None, 1.5f);
            switch (picture) {
                case Picture::Run:      // a spark: the forge is lit
                    drawList->AddCircle(c, 7.0f, ink, 20, 1.6f);
                    drawList->AddTriangleFilled(ImVec2(c.x - 2.5f, c.y - 4), ImVec2(c.x - 2.5f, c.y + 4), ImVec2(c.x + 4.5f, c.y), ink);
                    break;
                case Picture::Stop:
                    drawList->AddCircle(c, 7.0f, ink, 20, 1.6f);
                    drawList->AddRectFilled(ImVec2(c.x - 3, c.y - 3), ImVec2(c.x + 3, c.y + 3), ink, 1.0f);
                    break;
                case Picture::Hold:     // a snowflake's worth: three bars of a held breath
                    drawList->AddCircle(c, 7.0f, ink, 20, 1.6f);
                    drawList->AddLine(ImVec2(c.x - 2.2f, c.y - 3.5f), ImVec2(c.x - 2.2f, c.y + 3.5f), ink, 1.8f);
                    drawList->AddLine(ImVec2(c.x + 2.2f, c.y - 3.5f), ImVec2(c.x + 2.2f, c.y + 3.5f), ink, 1.8f);
                    break;
                case Picture::Frame:    // one frame of film, and an arrow past it
                    drawList->AddRect(ImVec2(c.x - 7, c.y - 5), ImVec2(c.x + 1, c.y + 5), ink, 1.5f, ImDrawFlags_None, 1.5f);
                    drawList->AddLine(ImVec2(c.x + 3, c.y), ImVec2(c.x + 8, c.y), ink, 1.6f);
                    drawList->AddTriangleFilled(ImVec2(c.x + 6, c.y - 3), ImVec2(c.x + 6, c.y + 3), ImVec2(c.x + 9.5f, c.y), ink);
                    break;
            }
            drawList->AddText(ImVec2(min.x + 32, c.y - ImGui::GetTextLineHeight() * 0.5f), ink, label);
            return pressed;
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 0));
        // What the scene is doing, at the left.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (keyHeight - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", !isPlaying ? "Editing" : isPaused ? "Held: changes are kept until Stop" : "Running: what you change now is undone at Stop");
        // The keys at the right end, beside the layout: their widths first, to know where they start.
        const float layoutWidth = 170;
        auto keyWidth = [](const char* label) { return 34 + ImGui::CalcTextSize(label).x + 12; };
        float keysWidth = keyWidth(isPlaying ? "Stop" : "Run") + keyWidth(isPaused ? "Resume" : "Hold") + keyWidth("Frame") + 12;
        // Each key placed on its own, all at the same height in the middle of the bar: placed
        // after the text on the same line, the first was put on the text's line, lower than the others.
        const float barTop = ImGui::GetWindowPos().y, keysTop = barTop + (height - keyHeight) * 0.5f;
        float keyX = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - layoutWidth - keysWidth - 22;
        auto placeKey = [&](const char* label) {
            ImGui::SetCursorScreenPos(ImVec2(keyX, keysTop));
            keyX += keyWidth(label) + 6;
        };
        placeKey(isPlaying ? "Stop" : "Run");
        if (control("##play", isPlaying ? "Stop" : "Run", isPlaying ? Picture::Stop : Picture::Run, isPlaying, true, IM_COL32(255, 122, 47, 255),
                    isPlaying ? "Stop: back to editing, the scene returns to how it was (Cmd+P)" : "Run the scene (Cmd+P)")) {
            scene.setPlaying(!isPlaying);
        }
        placeKey(isPaused ? "Resume" : "Hold");
        if (control("##pause", isPaused ? "Resume" : "Hold", Picture::Hold, isPaused, isPlaying, IM_COL32(120, 190, 255, 255),
                    isPaused ? "Resume (Shift+Cmd+P)" : "Hold: freeze the game where it is; change things, then resume (Shift+Cmd+P)")) {
            scene.setPaused(!isPaused);
        }
        placeKey("Frame");
        if (control("##step", "Frame", Picture::Frame, false, isPlaying, 0, "Advance one frame and hold (Alt+Cmd+P)")) scene.stepFrame();
        ImGui::PopStyleVar();

        // The layout of the windows, at the right end.
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - layoutWidth - 10, barTop + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(layoutWidth);
        if (ImGui::BeginCombo("##layout", ("Layout: " + layoutName_).c_str(), ImGuiComboFlags_HeightLarge)) {
            drawLayoutItems();
            ImGui::EndCombo();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- layouts

namespace {
const char* const kBuiltInLayouts[] = {"Default", "2 by 3", "Tall", "Wide"};
}

std::string EditorApp::layoutsDirectory() const {
    return userDataDirectory() + "/Layouts";
}

std::vector<std::string> EditorApp::savedLayouts() const {
    std::vector<std::string> names;
    std::error_code code;
    for (const fs::directory_entry& entry : fs::directory_iterator(layoutsDirectory(), code)) {
        if (entry.path().extension() == ".ini") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// The entries of the layout menu: the ones that come with the editor, the ones saved, and saving.
void EditorApp::drawLayoutItems() {
    for (const char* name : kBuiltInLayouts) {
        if (ImGui::MenuItem(name, nullptr, layoutName_ == name)) pendingLayout_ = name;
    }
    std::vector<std::string> saved = savedLayouts();
    if (!saved.empty()) ImGui::Separator();
    for (const std::string& name : saved) {
        if (ImGui::MenuItem(name.c_str(), nullptr, layoutName_ == name)) pendingLayout_ = name;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save Layout...")) {
        showsSaveLayout_ = true;
        bool isBuiltIn = false;
        for (const char* name : kBuiltInLayouts) isBuiltIn = isBuiltIn || layoutName_ == name;
        newLayoutName_ = isBuiltIn ? "My Layout" : layoutName_;
    }
    if (ImGui::MenuItem("Export Layout...")) {
        // The windows as they are now, to a file of your own: to keep, or to give to someone.
        std::string file = platform::chooseSaveFile("Export the layout of the windows", layoutName_ + ".ini", "ini");
        if (!file.empty()) {
            size_t length = 0;
            const char* settings = ImGui::SaveIniSettingsToMemory(&length);
            if (text::writeFile(file, std::string(settings, length))) context_->notify("Exported the layout to " + file);
            else context_->notify("Cannot write " + file, ConsoleMessage::Level::Error);
        }
    }
    if (ImGui::MenuItem("Import Layout...")) {
        // A layout file from elsewhere becomes one of the saved ones, under its file's name, and is put on.
        std::vector<std::string> files = platform::chooseFiles("Choose a layout file (.ini)", {"ini"}, false);
        if (!files.empty()) {
            std::optional<std::string> settings = text::readFile(files[0]);
            std::string name = fs::path(files[0]).stem().string();
            std::error_code code;
            fs::create_directories(layoutsDirectory(), code);
            if (settings && settings->find("[Docking]") != std::string::npos &&
                text::writeFile((fs::path(layoutsDirectory()) / (name + ".ini")).string(), *settings)) {
                pendingLayout_ = name;
                context_->notify("Imported the layout " + name);
            } else {
                context_->notify(files[0] + " is not a layout of this editor", ConsoleMessage::Level::Error);
            }
        }
    }
    if (ImGui::BeginMenu("Delete Layout", !saved.empty())) {
        for (const std::string& name : saved) {
            if (ImGui::MenuItem(name.c_str())) {
                std::error_code code;
                fs::remove(fs::path(layoutsDirectory()) / (name + ".ini"), code);
                if (layoutName_ == name) layoutName_ = "Custom";
            }
        }
        ImGui::EndMenu();
    }
}

void EditorApp::drawSaveLayoutDialog() {
    if (showsSaveLayout_) {
        ImGui::OpenPopup("Save Layout");
        showsSaveLayout_ = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Save Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("The windows as they are now, under a name:");
    ImGui::SetNextItemWidth(300);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool entered = ImGui::InputText("##name", &newLayoutName_, ImGuiInputTextFlags_EnterReturnsTrue);
    // A name that is also a file name.
    std::string name;
    for (char c : newLayoutName_) {
        if (c != '/' && c != ':' && c != '\\') name += c;
    }
    bool isBuiltIn = false;
    for (const char* builtIn : kBuiltInLayouts) isBuiltIn = isBuiltIn || name == builtIn;
    if (isBuiltIn) ImGui::TextColored(ImVec4(0.94f, 0.69f, 0.38f, 1), "That name is one of the editor's own layouts.");
    ImGui::BeginDisabled(name.empty() || isBuiltIn);
    if (ImGui::Button("Save", ImVec2(120, 0)) || (entered && !name.empty() && !isBuiltIn)) {
        std::error_code code;
        fs::create_directories(layoutsDirectory(), code);
        size_t length = 0;
        const char* settings = ImGui::SaveIniSettingsToMemory(&length);
        if (text::writeFile((fs::path(layoutsDirectory()) / (name + ".ini")).string(), std::string(settings, length))) {
            layoutName_ = name;
            context_->notify("Saved the layout " + name);
        } else {
            context_->notify("Cannot write the layout " + name, ConsoleMessage::Level::Error);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

/// Between frames: ImGui's settings cannot change under a frame that is being built.
void EditorApp::applyPendingLayout() {
    if (pendingLayout_.empty()) return;
    std::string name = std::move(pendingLayout_);
    pendingLayout_.clear();
    for (const char* builtIn : kBuiltInLayouts) {
        if (name == builtIn) {
            layoutName_ = name;
            needsDefaultLayout_ = true;
            return;
        }
    }
    std::optional<std::string> settings = text::readFile((fs::path(layoutsDirectory()) / (name + ".ini")).string());
    if (!settings) {
        context_->notify("The layout " + name + " could not be read", ConsoleMessage::Level::Error);
        return;
    }
    ImGui::ClearIniSettings();
    ImGui::LoadIniSettingsFromMemory(settings->c_str(), settings->size());
    layoutName_ = name;
}

void EditorApp::drawStatusBar() {
    EditorContext& editor = *context_;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
        if (ImGui::BeginMenuBar()) {
            if (editor.engine.time.elapsed < editor.statusUntil) ImGui::TextUnformatted(editor.statusText.c_str());
            else if (editor.scene.isPlaying()) ImGui::TextDisabled("Playing. Click the Game window to give it the keyboard. What you change now is undone when you stop (Cmd+P).");
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
    ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockSpace, viewport->WorkSize);
    ImGuiID center = dockSpace;
    ImGuiID scene = 0, game = 0, hierarchy = 0, inspector = 0, bottom = 0, console = 0;
    if (layoutName_ == "2 by 3") {
        // Scene above Game on the left, then the hierarchy, the assets and the inspector as columns.
        inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.3f, nullptr, &center);
        hierarchy = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.3f, nullptr, &center);
        game = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.5f, nullptr, &scene);
    } else if (layoutName_ == "Tall") {
        // A tall scene on the left; hierarchy over assets, and the inspector, on the right.
        inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
        hierarchy = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.3f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(hierarchy, ImGuiDir_Down, 0.5f, nullptr, &hierarchy);
        scene = game = center;
    } else if (layoutName_ == "Wide") {
        // Scene and Game side by side across the top; everything else in a strip below.
        ImGuiID strip = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.36f, nullptr, &center);
        game = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.5f, nullptr, &scene);
        hierarchy = ImGui::DockBuilderSplitNode(strip, ImGuiDir_Left, 0.2f, nullptr, &strip);
        inspector = ImGui::DockBuilderSplitNode(strip, ImGuiDir_Right, 0.36f, nullptr, &bottom);
    } else {
        // Default, as few places as there can be: the inspector down the right (Lighting and
        // Rendering as its tabs; the viewer of a chosen asset is its lower part); the explorer
        // and the console as tabs along the bottom; the hierarchy on the left; the scene and the
        // game as tabs of one big view in the middle. Input and Visual Script open when wanted.
        inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
        hierarchy = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        scene = game = center;
    }
    ImGui::DockBuilderDockWindow("Scene", scene);
    ImGui::DockBuilderDockWindow("Game", game);
    ImGui::DockBuilderDockWindow("Hierarchy", hierarchy);
    ImGui::DockBuilderDockWindow("Inspector", inspector);
    ImGui::DockBuilderDockWindow("Lighting", inspector);
    ImGui::DockBuilderDockWindow("Rendering", inspector);
    ImGui::DockBuilderDockWindow("Explorer", bottom);
    ImGui::DockBuilderDockWindow("Console", console ? console : bottom);
    ImGui::DockBuilderDockWindow("Input", bottom);
    ImGui::DockBuilderDockWindow("Visual Script", bottom);
    ImGui::DockBuilderDockWindow("Timeline", bottom);
    ImGui::DockBuilderDockWindow("Animation", bottom);
    ImGui::DockBuilderDockWindow("Animator", bottom);
    ImGui::DockBuilderFinish(dockSpace);
}

void EditorApp::drawDockSpace() {
    EditorContext& editor = *context_;
    ImGuiID dockSpace = ImGui::GetID("MoltenDockSpace");
    if (needsDefaultLayout_) {
        buildDefaultLayout(dockSpace);
        needsDefaultLayout_ = false;
        frontTabsIn_ = 2;                   // once the windows exist
    }
    // Of the tabs that share a place, the ones worked in most start in front.
    if (frontTabsIn_ > 0 && --frontTabsIn_ == 0) {
        ImGui::SetWindowFocus("Explorer");
        ImGui::SetWindowFocus("Inspector");
        ImGui::SetWindowFocus("Hierarchy");
        showsSceneTab_ = true;
    }
    ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport());

    // The two views of the scene are windows like the panels: side by side if wanted.
    // Play brings the game to the front, Stop the scene.
    bool isPlaying = editor.scene.isPlaying();
    if (isPlaying != wasPlaying_) {
        ImGui::SetWindowFocus(isPlaying ? "Game" : "Scene");
        wasPlaying_ = isPlaying;
    }
    drawSceneView(editor);
    drawGameView(editor, splashSeconds_ >= Splash::kDuration);
    if (showsSceneTab_) {
        // Asked for after both exist, so that it is the scene that ends up in front.
        ImGui::SetWindowFocus("Scene");
        showsSceneTab_ = false;
    }
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
    // A panel asked for (Open Animator...): in front, once it has been drawn.
    if (!editor.panelToFocus.empty()) {
        if (ImGui::FindWindowByName(editor.panelToFocus.c_str())) ImGui::SetWindowFocus(editor.panelToFocus.c_str());
        editor.panelToFocus.clear();
    }
    // Clicking an asset or selecting something in the scene brings the Inspector (where both are
    // shown) to the front of its tabs, without taking the keyboard from where it is.
    static std::string shownAsset;
    static size_t shownSelection = 0;
    auto bringForward = [](const char* name) {
        ImGuiWindow* window = ImGui::FindWindowByName(name);
        if (window && window->DockNode && window->DockNode->TabBar) window->DockNode->TabBar->NextSelectedTabId = window->TabId;
    };
    const AssetInfo* asset = editor.selectedAsset.empty() ? nullptr : editor.assets.find(editor.selectedAsset);
    if (editor.selectedAsset != shownAsset) {
        shownAsset = editor.selectedAsset;
        if (asset) bringForward("Inspector");
    }
    size_t selection = editor.scene.selection().size();
    if (selection != shownSelection) {
        shownSelection = selection;
        if (selection > 0) bringForward("Inspector");
    }
}

// ---------------------------------------------------------------- the project list

/// What the editor shows before a project is open: the recent ones, and a form for a new one.
void EditorApp::drawProjectList() {
    // A project whose folder is gone (moved, deleted, on a disk that is not there) is not offered.
    size_t before = recent_.paths.size();
    recent_.paths.erase(std::remove_if(recent_.paths.begin(), recent_.paths.end(), [](const std::string& path) {
        std::error_code code;
        return !fs::exists(path, code);
    }), recent_.paths.end());
    if (recent_.paths.size() != before) recent_.save(recentFile_);
    projectWindow_.picture = [this](const std::string& path) { return pictureAt(path); };
    ProjectWindow::Result asked = projectWindow_.draw(recent_.paths, templates_, kVersion);
    switch (asked.action) {
        case ProjectWindow::Action::None: break;
        case ProjectWindow::Action::Create: {
            std::string error;
            int chosen = projectWindow_.chosenTemplate;
            std::string from = chosen >= 0 && chosen < int(templates_.size()) ? templates_[size_t(chosen)].directory : std::string();
            std::optional<Project> created = Project::create(projectWindow_.location, projectWindow_.name, &error, from);
            if (created) openProject(created->directory);
            else projectWindow_.error = error;
            break;
        }
        case ProjectWindow::Action::Open: openProject(asked.path); break;
        case ProjectWindow::Action::OpenOther: {
            std::string folder = platform::chooseFolder("Choose the folder of a Molten Metal Engine project");
            if (!folder.empty()) openProject(folder);
            break;
        }
        case ProjectWindow::Action::ChooseLocation: {
            std::string folder = platform::chooseFolder("Choose where the project's folder is created", projectWindow_.location);
            if (!folder.empty()) projectWindow_.location = folder;
            break;
        }
        case ProjectWindow::Action::Forget:
            recent_.remove(asked.path);
            recent_.save(recentFile_);
            break;
        case ProjectWindow::Action::Reveal: platform::revealInFinder(asked.path); break;
    }
}

}  // namespace molten::editor
