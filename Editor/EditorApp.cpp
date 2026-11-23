#include "Theme.hpp"
#include "EditorApp.hpp"

#include "GameBuild.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include "Core/Paths.hpp"
#include "Core/TextFile.hpp"
#include "Scene/SceneAhead.hpp"

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
#include <cmath>
#include <ctime>
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <thread>
#include <mutex>

namespace molten::editor {

namespace fs = std::filesystem;

namespace {

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

    openLogFile();
    logRendererState();

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
            else if (line.rfind("autosave ", 0) == 0) autosaves_ = line.substr(9) != "0";
            else if (line.rfind("autosaveMinutes ", 0) == 0) autosaveMinutes_ = std::clamp(std::atoi(line.substr(16).c_str()), 1, 60);
            else if (line.rfind("reopenLastProject ", 0) == 0) reopensLastProject_ = line.substr(18) != "0";
            else if (line.rfind("maximizeOnPlay ", 0) == 0) context_->maximizesOnPlay = line.substr(15) != "0";
        }
        savedMaximizesOnPlay_ = context_->maximizesOnPlay;
        context_->shortcuts.fromText(shortcutLines);
        if (ImGui::GetCurrentContext()) applyTheme(1.0f);       // otherwise the interface starts with it
    }
    recentFile_ = userData + "/recent-projects.txt";
    recent_.load(recentFile_);
    const char* home = std::getenv("HOME");
    projectWindow_.location = std::string(home ? home : ".") + "/Documents";
    templates_ = projectTemplates(findRootDirectory() + "/Templates", yourTemplatesDirectory());
    makeMissingTemplatePreviews();
    context_->pictureAt = [this](const std::string& path) { return pictureAt(path); };

    std::string layoutFile = userData + "/layout-7.ini";     // a new name when the default changes: it is then laid out afresh
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
    registerAnimationPanel(editor);
    registerSequencePanel(editor);
    registerShaderGraphPanel(editor);
    registerAnimatorPanel(editor);
    registerSourceControlPanel(editor);
    registerSceneWeightPanel(editor);
    registerRenderingPanel(editor);
    registerProjectPanel(editor);
    registerVisualScriptPanel(editor);
    registerManualPanel(editor);
    registerCommands();
    // The editor opens on its project window, at once: no logo to wait for, and no empty
    // editor shown on the way. (Games made with it still have their splash.)
    splashSeconds_ = Splash::kDuration;
    projectWindow_.logoPath = findRootDirectory() + "/Assets/Branding/logo.png";

    // Plugins that come with the editor live next to its executable.
    if (!engine.config().executablePath.empty()) {
        // The real program, through the link the editor's application in the build folder is.
        std::string program = currentExecutablePath();
        if (program.empty()) program = fs::absolute(fs::path(engine.config().executablePath), ignored).string();
        fs::path beside = fs::path(program).parent_path() / "Plugins";
        plugins_.loadDirectory(editor, beside.string(), false);
    }

    editor.scene.newScene();
    // What an empty project starts with, for its card in the project window.
    emptyScenePicture_ = makeScenePicture(editor, 640, 360);
    projectWindow_.emptyPicture = ImGuiLayer::textureId(emptyScenePicture_.get());
    // The project the editor had, if Preferences say to open on it and it is still there.
    if (projectToOpen.empty() && reopensLastProject_ && !recent_.paths.empty() && engine.window() && fs::exists(recent_.paths.front(), ignored)) {
        projectToOpen = recent_.paths.front();
    }
    if (!projectToOpen.empty()) {
        // Asked for from the Terminal: straight to it, with the loading screen and without the logo.
        openProjectWithFeedback(projectToOpen, sceneToOpen);
    }
    updateTitle();
}

void EditorApp::openLogFile() {
    // Beside the real program (in the build folder, when it is run from there): a new file each run.
    std::string program = currentExecutablePath();
    if (program.empty() && engine_) program = engine_->config().executablePath;
    if (program.empty()) return;
    std::error_code code;
    fs::path folder = fs::weakly_canonical(fs::path(program), code).parent_path();
    if (code || folder.empty()) folder = fs::path(program).parent_path();
    logFile_ = std::fopen((folder / "editor.log").string().c_str(), "w");
    if (!logFile_) return;
    std::time_t now = std::time(nullptr);
    char when[64] = "";
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(logFile_, "Molten Metal Engine %s, editor started %s\n", kVersion, when);
    std::fflush(logFile_);
}

void EditorApp::writeLog(const char* level, const std::string& message) {
    if (!logFile_) return;
    std::fprintf(logFile_, "[%7.2f] %-7s %s\n", engine_ ? engine_->time.elapsed : 0.0, level, message.c_str());
    std::fflush(logFile_);              // a crash right after must not lose what led to it
}

void EditorApp::logRendererState() {
    if (!engine_) return;
    // What the engine said while it started was said before anyone listened: the state it ended in.
    Renderer& renderer = engine_->renderer();
    writeLog("info", std::string("ray tracing: ") + (renderer.device().supportsRayTracing() ? "the GPU can" : "the GPU cannot") +
                         (renderer.canTraceRays() ? ", and the shaders with it are in use" : ", and it is not in use"));
    writeLog("info", std::string("shaders: ") + (renderer.hasExtras() ? "with the extras (water depth, clouds with depth, low fog, traced light)" : "WITHOUT the extras"));
    if (!renderer.shaderProblem().empty()) writeLog("error", "the full shaders did not compile; what the compiler said:\n" + renderer.shaderProblem());
}

void EditorApp::shutdown(Engine& engine) {
    if (logFile_) {
        std::fclose(logFile_);
        logFile_ = nullptr;
    }
    if (!context_) return;
    plugins_.unloadAll(*context_);
    logSink() = nullptr;
    imgui_.shutdown(engine);
    context_.reset();
}

// ---------------------------------------------------------------- projects and scenes

/// Opening a project, step by step: each is a thing the editor can say it is doing.
std::vector<EditorApp::WorkStep> EditorApp::projectSteps(const std::string& path, const std::string& sceneAfter) {
    auto loaded = std::make_shared<std::optional<Project>>();
    std::vector<WorkStep> steps;
    steps.push_back({"Reading the project", [this, path, loaded] {
        EditorContext& editor = *context_;
        std::string error;
        *loaded = Project::load(path, &error);
        if (!*loaded) {
            projectWindow_.error = error;
            editor.notify(error, ConsoleMessage::Level::Error);
            recent_.remove(path);
            recent_.save(recentFile_);
            return false;
        }
        return true;
    }});
    steps.push_back({"Looking through its assets", [this, loaded] {
        EditorContext& editor = *context_;
        closeProject();
        editor.project = **loaded;
        // While the game is tried here, what it keeps for the player is kept with the project.
        editor.engine.preferencesFile = editor.project.libraryDirectory() + "/Preferences.txt";
        editor.hasProject = true;
        editor.engine.assets().setProjectDirectory(editor.project.assetsDirectory());
        editor.engine.loadQuality(editor.project.settingsDirectory());
        editor.engine.loadSettings(editor.project.settingsDirectory());
        editor.engine.loadInputMap();       // the project's own actions (Assets/Input.actions): Play here uses the keys the game will
        assetChanges_.forget();
        editor.assets.open(editor.project.assetsDirectory());
        editor.currentFolder.clear();
        editor.selectedAsset.clear();
        recent_.add(editor.project.directory);
        recent_.save(recentFile_);
        return true;
    }});
    steps.push_back({"Loading its plugins", [this] {
        plugins_.loadDirectory(*context_, context_->project.pluginsDirectory(), true);
        return true;
    }});
    // Before the scene is read: its components are the game's.
    steps.push_back(buildStep("Building the game's code"));
    steps.push_back(sceneStep("Opening the scene", [this, sceneAfter] {
        EditorContext& editor = *context_;
        // The scene asked for; else the one the game starts with; else a new scene to save later.
        return !sceneAfter.empty() ? editor.assets.absolutePath(sceneAfter)
             : editor.project.startScene.empty() ? std::string() : editor.assets.absolutePath(editor.project.startScene);
    }, [this](bool opened) {
        EditorContext& editor = *context_;
        if (!opened) editor.scene.newScene();
        else lookForAutosave();
        editor.notify("Opened project " + editor.project.name);
        projectWindow_.error.clear();
    }));
    steps[0].seconds = 0.1f;
    steps[1].seconds = 0.35f;
    steps[2].seconds = 0.1f;
    return steps;
}

EditorApp::WorkStep EditorApp::buildStep(const std::string& label) {
    auto result = std::make_shared<BuildResult>();
    auto project = std::make_shared<Project>();
    auto host = std::make_shared<std::string>();
    WorkStep step;
    step.label = label;
    step.seconds = 1.6f;
    step.begin = [this, project, host] {
        *project = context_->project;
        *host = currentExecutablePath();
        builtSignature_ = gameSourceSignature(*project);      // whatever happens, this state of the code has been tried
    };
    // The compiler is another program, and what it reads are files: the editor goes on drawing meanwhile.
    step.prepare = [result, project, host] { *result = buildGameCode(*project, *host); };
    step.run = [this, result] {
        if (context_->hasProject) finishBuild(*result);
        return true;                    // a game whose code does not build can still be opened, and mended
    };
    return step;
}

EditorApp::WorkStep EditorApp::sceneStep(const std::string& label, std::function<std::string()> pathNow, std::function<void(bool opened)> then) {
    auto ahead = std::make_shared<SceneAhead>();
    auto progress = std::make_shared<SceneAheadProgress>();
    auto path = std::make_shared<std::string>();
    auto pathOf = std::make_shared<std::function<std::string(const std::string&)>>();
    WorkStep step;
    step.label = label;
    step.seconds = 1.2f;
    step.begin = [this, path, pathOf, pathNow] {
        *path = pathNow();
        *pathOf = context_->engine.assetPathFinder();
    };
    // Its models made into triangles and its pictures into pixels, several files at a time
    // (Scene/SceneAhead.hpp): the slow part of opening a scene, and none of it needs the main thread.
    step.prepare = [ahead, progress, path, pathOf] {
        if (!path->empty()) *ahead = readSceneAhead(*path, *path, *pathOf, progress.get());
    };
    step.progress = [progress] { return progress->fraction(); };
    step.detail = [progress] {
        int total = progress->total.load();
        return total > 1 ? std::to_string(std::min(progress->done.load(), total)) + " of " + std::to_string(total) + " files" : std::string();
    };
    step.run = [this, ahead, path, then] {
        EditorContext& editor = *context_;
        Assets& assets = editor.engine.assets();
        for (SceneAhead::Model& model : ahead->models) assets.preload(model.name, std::move(model.data));
        for (SceneAhead::Picture& picture : ahead->pictures) assets.preloadPicture(picture.name, std::move(picture.pixels), picture.width, picture.height);
        std::string error;
        bool opened = !path->empty() && editor.scene.open(*path, &error);
        assets.forgetPreloaded();
        if (!opened && !path->empty()) editor.notify(error.empty() ? "Cannot open " + *path : error, ConsoleMessage::Level::Error);
        if (then) then(opened);
        return true;
    };
    return step;
}

/// Opens a project at once, without the screen: when there is no window to show it in.
bool EditorApp::openProject(const std::string& path) {
    for (WorkStep& step : projectSteps(path, {})) {
        if (step.begin) step.begin();
        if (step.prepare) step.prepare();
        if (step.run && !step.run()) return false;
    }
    return true;
}

void EditorApp::openProjectWithFeedback(const std::string& path, const std::string& sceneAfter) {
    if (!engine_->window()) {
        if (openProject(path) && !sceneAfter.empty()) openSceneNow(context_->assets.absolutePath(sceneAfter));
        return;
    }
    Work work;
    std::string name = fs::path(path).filename().string();
    if (name == "" || fs::path(path).extension() == ".molten") name = fs::path(path).stem().string();
    work.title = "Opening " + name;
    work.coversAll = true;
    work.steps = projectSteps(path, sceneAfter);
    startWork(std::move(work));
}

void EditorApp::openSceneWithFeedback(const std::string& absolutePath) {
    Work work;
    work.title = "Opening " + fs::path(absolutePath).stem().string();
    work.steps.push_back(sceneStep("Reading the scene and what it uses", [absolutePath] { return absolutePath; }, [this](bool opened) {
        if (!opened) return;
        context_->notify("Opened " + context_->scene.displayName());
        lookForAutosave();
    }));
    startWork(std::move(work));
}

void EditorApp::buildGameWithFeedback() {
    if (!context_->hasProject) return;
    Work work;
    work.title = "Building the game's code";
    work.steps.push_back(buildStep("Compiling the scripts"));
    startWork(std::move(work));
}

void EditorApp::exportGameWithFeedback() {
    Work work;
    work.title = "Exporting the game";
    work.steps.push_back({"Building, then packing the application", [this] {
        exportGame();
        return true;
    }});
    startWork(std::move(work));
}

// ---------------------------------------------------------------- the work screen

void EditorApp::startWork(Work work) {
    if (work_ || work.steps.empty()) return;
    work.started = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    work.lastDone = work.started;
    work.consoleFrom = context_->console.size();
    work_ = std::move(work);
}

namespace {

const double kWorkFadeIn = 0.16, kWorkFadeOut = 0.32;

float smoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

/// A step at a time. What is slow in a step and can be done elsewhere (the compiler, reading
/// models and pictures) runs on another thread, and the screen goes on moving; what must be
/// done here waits for a frame that has shown what it is.
void EditorApp::runWork() {
    if (!work_) return;
    Work& work = *work_;
    const double now = ImGui::GetTime();
    if (work.finishedAt >= 0) {
        // Gone once the bar has reached its end and the screen has faded.
        if (work.fullAt >= 0 && now - work.fullAt > kWorkFadeOut) work_.reset();
        return;
    }
    // Nothing that makes the editor wait while the screen is still coming in.
    if (now - work.started < kWorkFadeIn) return;
    if (work.waitFrames > 0) {
        work.waitFrames--;
        return;
    }
    WorkStep& step = work.steps[work.next];
    if (step.prepare && !work.isPrepared) {
        if (!work.isPreparing) {
            if (step.begin) step.begin();
            work.preparing = std::async(std::launch::async, step.prepare);
            work.isPreparing = true;
            return;
        }
        if (work.preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        work.preparing.get();
        work.isPreparing = false;
        work.isPrepared = true;
    } else if (step.begin && !step.prepare) {
        step.begin();
    }
    bool carriesOn = !step.run || step.run();
    const double ended = ImGui::GetTime();
    work.took.push_back(ended - work.lastDone);
    work.lastDone = ended;
    work.consoleAt.push_back(context_->console.size());
    work.next++;
    work.isPrepared = false;
    work.stepStarted = -1;              // the next one starts with the next frame
    work.waitFrames = 1;
    if (!carriesOn) {
        work_.reset();                  // what went wrong is said where it went wrong
        return;
    }
    if (work.next >= work.steps.size()) work.finishedAt = ImGui::GetTime();
}

void EditorApp::drawWork() {
    if (!work_) return;
    Work& work = *work_;
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    if (work.stepStarted < 0) work.stepStarted = now;

    // ---- where the bar should be: each step has its share of it, by how long such a step takes
    float total = 0, done = 0;
    for (size_t i = 0; i < work.steps.size(); i++) {
        total += work.steps[i].seconds;
        if (i < work.next) done += work.steps[i].seconds;
    }
    float target = 1.0f;
    if (work.finishedAt < 0) {
        const WorkStep& step = work.steps[work.next];
        // Inside a step: as far as the step says it is; or, when it cannot say, toward its end
        // at the pace such a step goes, slowing as it nears it and never arriving before it.
        float byTime = 1.0f - std::exp(-float(now - work.stepStarted) / std::max(step.seconds, 0.05f));
        float within = step.progress && work.isPreparing ? std::max(std::clamp(step.progress(), 0.0f, 1.0f) * 0.96f, byTime * 0.3f) : byTime * 0.92f;
        target = (done + step.seconds * within) / std::max(total, 0.001f);
    } else {
        // The work is done. What is under the screen draws its first frames now (shaders made
        // ready, pictures sent to the GPU), and those are the slow ones: it is shown once they
        // have passed, not in the middle of them.
        work.calmFrames = io.DeltaTime < 0.05f ? work.calmFrames + 1 : 0;
        bool hasSettled = !work.coversAll || work.calmFrames >= 4 || now - work.finishedAt > 2.5;
        target = hasSettled ? 1.0f : 0.97f;
    }
    // The bar follows: only forward, and at a pace, however long the last frame took.
    const float dt = std::min(io.DeltaTime, 1.0f / 30.0f);
    const float gap = target - work.shown;
    if (gap > 0) work.shown += std::min(gap, dt * std::max(work.finishedAt >= 0 ? 0.9f : 0.2f, gap * 6.0f));
    if (work.finishedAt >= 0 && work.fullAt < 0 && work.shown >= 0.999f) work.fullAt = now;

    const float in = smoothStep(float((now - work.started) / kWorkFadeIn));
    const float out = work.fullAt >= 0 ? 1.0f - smoothStep(float((now - work.fullAt) / kWorkFadeOut)) : 1.0f;
    const float alpha = in * out;
    auto faded = [alpha](int r, int g, int b, float a = 1.0f) { return IM_COL32(r, g, b, int(255.0f * a * alpha)); };

    // A window over everything: it takes the clicks and keys while the work goes on.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##work", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar(2);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = viewport->Pos, max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    ImFont* font = ImGui::GetFont();
    const std::string label = (work.next < work.steps.size() ? work.steps[work.next].label : work.steps.back().label) + "...";
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", int(std::floor(work.shown * 100.0f + 0.5f)));

    // The bar: what is done, from ember to flame, and a light that runs along it, which says
    // the editor is at work even when a step takes its time.
    auto drawBar = [&](ImVec2 at, float width) {
        const float height = 8, radius = 4;
        drawList->AddRectFilled(at, ImVec2(at.x + width, at.y + height), faded(255, 255, 255, 0.075f), radius);
        const float filled = std::max(height, width * work.shown);
        const ImVec2 end(at.x + filled, at.y + height);
        const ImU32 from = faded(224, 90, 26), to = faded(255, 160, 74);
        drawList->AddRectFilled(at, end, from, radius);
        if (filled > height * 1.5f) {
            drawList->AddRectFilledMultiColor(ImVec2(at.x + radius, at.y), ImVec2(end.x - radius, end.y), from, to, to, from);
            drawList->AddCircleFilled(ImVec2(end.x - radius, at.y + radius), radius, to, 16);
        }
        drawList->PushClipRect(at, end, true);
        const float span = 140.0f;
        const float x = at.x - span + float(std::fmod(now * 0.6, 1.0)) * (filled + span * 2.0f);
        const ImU32 light = faded(255, 240, 210, 0.38f), none = faded(255, 240, 210, 0.0f);
        drawList->AddRectFilledMultiColor(ImVec2(x - span * 0.5f, at.y), ImVec2(x, end.y), none, light, light, none);
        drawList->AddRectFilledMultiColor(ImVec2(x, at.y), ImVec2(x + span * 0.5f, end.y), light, none, none, light);
        drawList->PopClipRect();
    };
    // What is being done under the bar's left end, how far it is under its right end.
    auto drawWords = [&](ImVec2 at, float width) {
        drawList->AddText(font, 13.0f, at, faded(139, 147, 161), label.c_str());
        ImVec2 extent = font->CalcTextSizeA(13.0f, FLT_MAX, 0, percent);
        drawList->AddText(font, 13.0f, ImVec2(at.x + width - extent.x, at.y), faded(190, 196, 206), percent);
    };

    // The log under the bar: each step done and how long it took, the one under way, and what the
    // console was told meanwhile (a project's warnings are seen as it opens, not found later).
    auto drawLog = [&](ImVec2 at, float width, int most) {
        struct Line {
            const char* glyph;
            ImU32 color;
            std::string text, right;
            bool spins = false, isDone = false;
        };
        std::vector<Line> lines;
        const std::vector<ConsoleMessage>& console = context_->console;
        auto told = [&](size_t from, size_t to) {
            for (size_t i = std::min(from, console.size()); i < std::min(to, console.size()); i++) {
                const ConsoleMessage& message = console[i];
                if (message.level == ConsoleMessage::Level::Debug) continue;
                bool isError = message.level == ConsoleMessage::Level::Error, isWarning = message.level == ConsoleMessage::Level::Warning;
                lines.push_back({isError ? icon::CircleX : isWarning ? icon::TriangleAlert : icon::Info,
                                 isError ? faded(240, 96, 88) : isWarning ? faded(236, 178, 72) : faded(118, 126, 140), message.text, ""});
            }
        };
        size_t from = work.consoleFrom;
        for (size_t i = 0; i < work.next && i < work.steps.size(); i++) {
            char seconds[24];
            std::snprintf(seconds, sizeof(seconds), "%.2f s", i < work.took.size() ? work.took[i] : 0.0);
            lines.push_back({icon::Check, faded(255, 140, 60), work.steps[i].label, seconds, false, true});
            size_t to = i < work.consoleAt.size() ? work.consoleAt[i] : console.size();
            told(from, to);
            from = to;
        }
        if (work.next < work.steps.size()) {
            const WorkStep& step = work.steps[work.next];
            lines.push_back({nullptr, faded(223, 227, 234), step.label, work.isPreparing && step.detail ? step.detail() : std::string(), true});
            told(from, console.size());
        }
        // The latest lines, the oldest of them fading.
        const size_t first = lines.size() > size_t(most) ? lines.size() - size_t(most) : 0;
        ImFont* icons = ImGuiLayer::iconFont();
        const float lineHeight = 21.0f, textSize = 13.0f;
        for (size_t i = first; i < lines.size(); i++) {
            const Line& line = lines[i];
            const float y = at.y + float(i - first) * lineHeight;
            const float fade = (first > 0 && i == first) ? 0.35f : (first > 0 && i == first + 1) ? 0.7f : 1.0f;
            auto dim = [&](ImU32 color) { return (color & 0x00FFFFFFu) | (ImU32(float(color >> 24) * fade) << 24); };
            const ImVec2 mark(at.x + 7, y + lineHeight * 0.5f - 1);
            if (line.spins) {
                // Under way: an arc that goes round.
                const float a = float(std::fmod(now * 5.0, 6.2831853));
                drawList->PathArcTo(mark, 5.5f, a, a + 4.4f, 18);
                drawList->PathStroke(dim(faded(255, 150, 70)), 0, 1.7f);
            } else if (icons && line.glyph) {
                ImVec2 extent = icons->CalcTextSizeA(14.0f, FLT_MAX, 0, line.glyph);
                drawList->AddText(icons, 14.0f, ImVec2(std::round(mark.x - extent.x * 0.5f), std::round(mark.y - extent.y * 0.5f)), dim(line.color), line.glyph);
            }
            const float rightWidth = line.right.empty() ? 0.0f : font->CalcTextSizeA(textSize, FLT_MAX, 0, line.right.c_str()).x;
            const float textX = at.x + 24, room = width - 24 - (rightWidth > 0 ? rightWidth + 12 : 0);
            const ImU32 ink = line.spins ? faded(223, 227, 234) : line.isDone ? faded(170, 177, 189) : line.color;
            drawList->PushClipRect(ImVec2(textX, y), ImVec2(textX + room, y + lineHeight), true);
            drawList->AddText(font, textSize, ImVec2(textX, y + (lineHeight - textSize) * 0.5f - 1), dim(ink), line.text.c_str());
            drawList->PopClipRect();
            if (rightWidth > 0) {
                drawList->AddText(font, textSize, ImVec2(at.x + width - rightWidth, y + (lineHeight - textSize) * 0.5f - 1), dim(faded(110, 118, 132)), line.right.c_str());
            }
        }
    };

    if (work.coversAll) {
        // Opening a project: the window is the editor's own until the project is there.
        drawList->AddRectFilled(min, max, faded(17, 18, 22));
        const float width = std::min(460.0f, viewport->Size.x - 80.0f);
        float top = center.y - 190;
        ImTextureID mark = pictureAt(findRootDirectory() + "/Assets/Branding/mark-hd.png");
        if (mark) {
            const float markWidth = 150, markHeight = markWidth * 1040.0f / 2252.0f;     // the picture is 2252 x 1040
            drawList->AddImage(mark, ImVec2(center.x - markWidth * 0.5f, top), ImVec2(center.x + markWidth * 0.5f, top + markHeight), ImVec2(0, 0), ImVec2(1, 1),
                               faded(255, 255, 255));
            top += markHeight + 22;
        }
        ImVec2 extent = font->CalcTextSizeA(23.0f, FLT_MAX, 0, work.title.c_str());
        drawList->AddText(font, 23.0f, ImVec2(center.x - extent.x * 0.5f, top), faded(223, 227, 234), work.title.c_str());
        const ImVec2 bar(center.x - width * 0.5f, top + 46);
        drawBar(bar, width);
        drawWords(ImVec2(bar.x, bar.y + 18), width);
        drawList->AddLine(ImVec2(bar.x, bar.y + 48), ImVec2(bar.x + width, bar.y + 48), faded(255, 255, 255, 0.06f));
        drawLog(ImVec2(bar.x, bar.y + 58), width, 9);
    } else {
        // Anything else: a card over the editor, which stays in view behind it.
        drawList->AddRectFilled(min, max, faded(8, 9, 12, 0.45f));
        const float width = 420, height = 108;
        const ImVec2 cardMin(center.x - width * 0.5f, center.y - height * 0.5f), cardMax(center.x + width * 0.5f, center.y + height * 0.5f);
        drawList->AddRectFilled(ImVec2(cardMin.x, cardMin.y + 6), ImVec2(cardMax.x, cardMax.y + 10), faded(0, 0, 0, 0.35f), 14);
        drawList->AddRectFilled(cardMin, cardMax, faded(36, 40, 47), 12);
        drawList->AddRect(cardMin, cardMax, faded(58, 64, 75), 12);
        drawList->AddText(font, 16.0f, ImVec2(cardMin.x + 24, cardMin.y + 20), faded(223, 227, 234), work.title.c_str());
        drawBar(ImVec2(cardMin.x + 24, cardMin.y + 54), width - 48);
        drawWords(ImVec2(cardMin.x + 24, cardMin.y + 72), width - 48);
    }
    ImGui::End();
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
    if (editor.scene.open(absolutePath, &error)) {
        editor.notify("Opened " + editor.scene.displayName());
        lookForAutosave();
    } else {
        editor.notify(error, ConsoleMessage::Level::Error);
    }
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
    if (!error.empty()) {
        editor.notify(error, ConsoleMessage::Level::Error);
    } else {
        forgetAutosave();                   // what it kept is in the file now
        saveProjectPreview();
    }
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
        // One that is there stays, unless the template has changed since (its scenes are newer): a
        // template is a project like any other, and can be opened and worked on.
        if (fs::exists(picture, code)) {
            fs::file_time_type made = fs::last_write_time(picture, code), changed = made;
            for (fs::recursive_directory_iterator file(fs::path(item.directory) / "Assets" / "Scenes", code), end; !code && file != end; file.increment(code)) {
                std::error_code ignored;
                if (file->is_regular_file(ignored)) changed = std::max(changed, file->last_write_time(ignored));
            }
            code.clear();
            if (changed <= made) continue;
        }
        // (A template for several players is shown as it is played alone: no menu over it, and no network asked for.)
        commands += "MOLTEN_NET=alone \"" + player.string() + "\" \"" + item.directory + "\" --capture \"" + picture.string() + "\" --frames 150 --width 1280 --height 720 --scale 1 >/dev/null 2>&1; ";
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
    return finishBuild(buildGameCode(editor.project, currentExecutablePath()));
}

bool EditorApp::finishBuild(const BuildResult& built) {
    EditorContext& editor = *context_;
    BuildResult result = built;
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
    // The build of last time, used again because nothing changed: a library loaded once
    // cannot be loaded a second time (its components would not be declared again), so
    // a project opened twice gets a copy of it under another name.
    if (result.reused && loadedLibraries_.count(result.libraryPath)) {
        std::string copy = copyOfBuild(result.libraryPath);
        if (!copy.empty()) result.libraryPath = copy;
    }
    std::string error;
    editor.scene.reloadGameCode([&] {
        if (!gameModule_.load(result.libraryPath, &error)) error = "The game's code was built but could not be loaded: " + error;
    });
    if (!error.empty()) {
        editor.notify(error, ConsoleMessage::Level::Error);
        return false;
    }
    loadedLibraries_.insert(result.libraryPath);
    lastBuildFailed_ = false;
    size_t count = gameModule_.componentNames().size();
    std::string components = std::to_string(count) + (count == 1 ? " component" : " components");
    if (result.reused) {
        editor.notify("The game's code is as it was last built: " + components);
    } else {
        char seconds[32];
        std::snprintf(seconds, sizeof(seconds), "%.1f", result.seconds);
        editor.notify("Built the game's code in " + std::string(seconds) + " s: " + components);
    }
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
        openSceneWithFeedback(argument);
    } else if (action == "new-scene") {
        editor.scene.newScene();
    } else if (action == "open-project") {
        openProjectWithFeedback(argument);
    } else if (action == "close-project" || action == "new-project") {
        closeProject();
        if (action == "new-project") projectWindow_.showNewProject();
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
        forgetAutosave();                           // they were let go on purpose: nothing to recover
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
    // What the console has heard since the last frame, from the engine or from the editor itself, goes to the log file.
    static size_t written = 0;
    if (written > editor.console.size()) written = editor.console.size();
    for (; written < editor.console.size(); written++) {
        const ConsoleMessage& said = editor.console[written];
        writeLog(said.level == ConsoleMessage::Level::Error ? "error" : said.level == ConsoleMessage::Level::Warning ? "warning" : "info", said.text);
    }
    if (editor.console.size() > 2000) {
        editor.console.erase(editor.console.begin(), editor.console.begin() + 500);
        written -= std::min<size_t>(written, 500);
    }

    // Files change behind the editor's back (the Finder, git, other programs): look once a second.
    runWork();
    // A project with no picture where projects are listed (never saved here, or made before the
    // editor kept them) gets one once its scene has been on the screen for a moment.
    if (editor.hasProject && !isWorking()) {
        if (++framesWithProject_ == 90 && !editor.scene.isPlaying()) {
            std::error_code code;
            if (!fs::exists(fs::path(editor.project.directory) / "preview.png", code)) saveProjectPreview();
        }
    } else {
        framesWithProject_ = 0;
    }
    if (editor.hasProject && !isWorking() && engine.time.elapsed >= nextAssetScan_) {
        nextAssetScan_ = engine.time.elapsed + 1.0;
        editor.assets.refresh();
        // A material file deleted in the Finder: what wore it no longer does.
        if (int count = editor.scene.takeOffMissingMaterials([&](const std::string& name) { return editor.assets.find(name) != nullptr; })) {
            editor.notify(std::to_string(count) + (count == 1 ? " object's material is gone: it wears the plain one" : " objects' material is gone: they wear the plain one"),
                          ConsoleMessage::Level::Warning);
        }
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
    if (editor.hasProject && !isWorking() && !editor.scene.isPlaying() && engine.time.elapsed >= nextSourceCheck_) {
        nextSourceCheck_ = engine.time.elapsed + 1.0;
        if (gameSourceSignature(editor.project) != builtSignature_) buildGameWithFeedback();
    }

    handleWindowClose();
    if (!isWorking()) runPendingAction();
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
    if (!isWorking()) handleShortcuts();            // nothing is started while a project is half open
    drawMenuBar();
    if (editor.hasProject) drawPlayBar();
    if (editor.hasProject) drawStatusBar();       // it speaks of the scene: nothing to say without one
    if (editor.hasProject) {
        // Files dropped from the Finder: the panel under the pointer takes them as it draws.
        if (Window* window = engine.window()) editor.droppedFiles = window->takeDroppedFiles();
        // Run maximizes the Game view if asked to, and Stop puts back what Run did.
        const bool isPlaying = editor.scene.isPlaying();
        if (isPlaying != wasPlayingForMaximize_) {
            if (isPlaying && editor.maximizesOnPlay && !editor.gameViewMaximized) editor.gameViewMaximized = maximizedByPlay_ = true;
            if (!isPlaying && maximizedByPlay_) editor.gameViewMaximized = maximizedByPlay_ = false;
            wasPlayingForMaximize_ = isPlaying;
        }
        if (editor.maximizesOnPlay != savedMaximizesOnPlay_) saveEditorSettings();
        if (editor.gameViewMaximized) {
            // Only the game, over the whole editor; the layout of the rest kept as it is, for its return.
            ImGui::DockSpaceOverViewport(ImGui::GetID("MoltenDockSpace"), ImGui::GetMainViewport(), ImGuiDockNodeFlags_KeepAliveOnly);
            hideSceneView(editor);
            drawGameView(editor, splashSeconds_ >= Splash::kDuration);
            if (!editor.gameViewMaximized) maximizedByPlay_ = false;      // restored by hand
        } else {
            drawDockSpace();
            drawPanels();
        }
        // Dropped where no panel takes them: into the folder the Explorer shows, as Unity does.
        if (!editor.droppedFiles.empty()) {
            importDropped(editor, editor.droppedFiles, editor.currentFolder);
            editor.droppedFiles.clear();
        }
    } else {
        hideSceneAndGameViews(editor);
        drawProjectList();
        // The manual can be read before any project is open.
        if (Panel* manual = editor.registry.findPanel("Manual"); manual && manual->isOpen) {
            ImGui::SetNextWindowSize(manual->firstSize(), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin(ui::windowName(manual->title).c_str(), &manual->isOpen, manual->windowFlags() | ImGuiWindowFlags_NoDocking)) manual->draw(editor);
            ImGui::End();
            if (!editor.panelToFocus.empty()) {
                ImGui::SetWindowFocus(ui::windowId(editor.panelToFocus).c_str());
                editor.panelToFocus.clear();
            }
        }
    }
    autosave();
    drawUnsavedChangesDialog();
    drawRecoverDialog();
    drawAboutDialog();
    drawCommandPalette();
    drawSettingsWindow();
    drawBuildWindow();
drawNewScriptDialog();
    drawSaveLayoutDialog();
    drawWork();                             // over everything: what the editor is busy with

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
    // "Molten Engine v1.2 - Racer": the engine and the project. That the scene has changes not saved is said the
    // way a Mac says it, by the dot in the window's close button.
    std::string title = std::string("Molten Engine v") + kVersion;
    if (editor.hasProject) title += " - " + editor.project.name;
    if (title != shownTitle_) {
        window->setTitle(title);
        shownTitle_ = title;
    }
    bool isEdited = editor.hasProject && editor.scene.isDirty();
    if (isEdited != shownEdited_) {
        platform::setWindowEdited(window->nativeHandle(), isEdited);
        shownEdited_ = isEdited;
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
    const KeyChord& chord = context_->shortcuts.chord(action);
    ImGuiKey key = keyNamed(chord.key);
    if (key == ImGuiKey_None) return false;
    ImGuiKeyChord mods = (chord.shift ? ImGuiMod_Shift : 0) | (chord.alt ? ImGuiMod_Alt : 0);
    // ImGui calls the Command key "Ctrl" on a Mac; the key that says Control works too: hands
    // that learned the shortcuts elsewhere find them here.
    if (!chord.command) return ImGui::Shortcut(mods | key, ImGuiInputFlags_RouteGlobal);
    return ImGui::Shortcut(mods | ImGuiMod_Ctrl | key, ImGuiInputFlags_RouteGlobal) || ImGui::Shortcut(mods | ImGuiMod_Super | key, ImGuiInputFlags_RouteGlobal);
}

namespace {
/// Whether what is selected moves by itself, and so should move while it is looked at: a
/// particle system, water, a material that sways in the wind or has a shader of its own (an
/// entity's, or the material open in the inspector).
bool selectionMoves(EditorContext& editor) {
    auto moves = [](const Material* material) { return material && (material->isWater || material->sway > 0.0f || !material->shader.empty()); };
    Scene& scene = editor.engine.scene;
    for (Entity entity : editor.scene.selection()) {
        if (scene.has<ParticleEmitter>(entity)) return true;
        if (const MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && moves(renderer->material.get())) return true;
    }
    const std::string& asset = editor.selectedAsset;
    if (asset.size() > 9 && asset.compare(asset.size() - 9, 9, ".material") == 0) {
        if (std::shared_ptr<Material> material = editor.engine.assets().material(asset)) return moves(material.get());
    }
    return false;
}
}  // namespace

void EditorApp::handleShortcuts() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    // What moves by itself moves while the game runs, while asked to (Simulate), or while it is selected.
    editor.engine.simulatesEffects = scene.isPlaying() || editor.simulatesInViews || selectionMoves(editor);
    ImGuiIO& io = ImGui::GetIO();
    // While the game has the keys, the arrows and Space are the game's: the editor's own keyboard
    // navigation (moving between buttons and lists, showing their tips) is off until it lets go.
    const bool gameHasKeys = editor.hasProject && scene.isPlaying() && editor.gameViewHasFocus;
    if (gameHasKeys) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // A game that took the pointer to look about with has it only while it runs: stopped or held, the pointer is the editor's again.
    if (!scene.isPlaying() || scene.isPaused()) editor.engine.input.setPointerCaptured(false);
    // While the game has it, the editor's own panels must not feel a pointer that is not shown going over them.
    if (editor.engine.input.isPointerCaptured()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    // Keys that would fight with typing in a text field, or belong to a panel with a document of
    // its own, or to the Explorer while it has the keyboard: each command says which it is.
    const bool panelHasThem = editor.panelOwnsEditKeys, explorerHasThem = editor.explorerHasKeys;
    editor.panelOwnsEditKeys = false;           // the panel says so again this frame if it still has the keyboard
    editor.explorerHasKeys = false;
    const bool isTyping = io.WantTextInput;
    // Flying through the scene holds a mouse button and letters: Shift+D is then "right, fast", not a command.
    const bool handsAreBusy = ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    // By index: what a command does may be anything, but the list itself does not change.
    const std::deque<Command>& all = editor.commands.all();
    for (size_t i = 0; i < all.size(); i++) {
        const Command& command = all[i];
        if (!command.run || command.id.empty()) continue;
        const KeyChord& chord = editor.shortcuts.chord(command.id);
        if (chord.isEmpty()) continue;
        if (gameHasKeys && !command.worksInGame) continue;
        if (command.usesEditKeys && (isTyping || panelHasThem)) continue;
        if (command.actsOnSelection && explorerHasThem) continue;
        if (!chord.command && (isTyping || handsAreBusy)) continue;
        if (isPressed(command.id) && command.isEnabled()) command.run();
    }

    if (gameHasKeys) {
        // The keyboard is the game's, but for the way out: Esc is the editor's. The game keeps
        // running and hears nothing more (the pointer goes back with it) until its picture is
        // clicked; Esc again, with the Game window still the one in use, stops it.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            editor.gameViewHasFocus = false;
            editor.engine.input.isListening = false;
        }
        return;
    }
    if (scene.isPlaying() && editor.gameWindowIsFocused && !isTyping && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        scene.setPlaying(false);
        return;
    }
    if (!editor.hasProject || isTyping || panelHasThem || !recording_.empty()) return;
    // Two that hands expect besides the ones in the settings: Cmd+Y redoes, and Delete or Backspace alone deletes.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) editor.commands.run("redo");
    bool deletes = ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
    if (deletes && !io.KeySuper && !io.KeyCtrl && !ImGui::IsAnyItemActive() && !explorerHasThem) editor.commands.run("delete");
}

// ---------------------------------------------------------------- Molten Engine > Preferences

void EditorApp::saveEditorSettings() {
    std::string out = "theme " + currentTheme() + "\n";
    out += std::string("autosave ") + (autosaves_ ? "1" : "0") + "\n";
    out += "autosaveMinutes " + std::to_string(autosaveMinutes_) + "\n";
    out += std::string("reopenLastProject ") + (reopensLastProject_ ? "1" : "0") + "\n";
    out += std::string("maximizeOnPlay ") + (context_->maximizesOnPlay ? "1" : "0") + "\n";
    savedMaximizesOnPlay_ = context_->maximizesOnPlay;
    std::istringstream changed(context_->shortcuts.toText());
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
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(780, 580), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui::windowName("Preferences").c_str(), &showsSettings_, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    // A list of pages at the left, the page at the right: as the Mac's own settings are, and Unity's Preferences.
    static int page = 0;
    const char* pages[] = {"General", "Appearance", "Shortcuts"};
    const char* icons[] = {"settings", "palette", "keyboard"};
    const char* notes[] = {"How the editor itself behaves. It is the same for every project.", "The colors of the editor. The game's own are in its scenes.",
                           "Every command of the menus, and the keys that do it."};
    const int pageCount = 3;
    if (!settingsPage_.empty()) {
        for (int i = 0; i < pageCount; i++) {
            if (settingsPage_ == pages[i]) page = i;
        }
        settingsPage_.clear();
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    ImGui::BeginChild("##pages", ImVec2(176, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    for (int i = 0; i < pageCount; i++) {
        if (ui::iconSelectable(pages[i], icons[i], page == i)) {
            page = i;
            recording_.clear();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ui::title(pages[page]);
    ImGui::TextDisabled("%s", notes[page]);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (page == 0) {
        bool changed = false;
        ui::subheading("Saving");
        changed |= ui::checkbox("Autosave", autosaves_);
        ui::tooltip("While a scene has changes that are not saved, a copy is kept aside every few minutes (in the project's Library).\n"
                    "The scene's own file is never touched: if the editor closes badly, the next time the scene is opened it asks whether to recover the copy");
        ImGui::BeginDisabled(!autosaves_);
        ui::label("Every");
        changed |= ImGui::DragInt("##autosave-minutes", &autosaveMinutes_, 0.05f, 1, 60, autosaveMinutes_ == 1 ? "%d minute" : "%d minutes", ImGuiSliderFlags_AlwaysClamp);
        ui::tooltip("How long between two copies");
        ImGui::EndDisabled();
        ui::subheading("Starting");
        changed |= ui::checkbox("Reopen Last Project", reopensLastProject_);
        ui::tooltip("The editor opens on the project it had last, without stopping at the project window");
        if (changed) saveEditorSettings();
    } else if (page == 1) {
        // Each look as a card: a click wears it.
        float card = 150;
        int column = 0, perRow = std::max(1, int(ImGui::GetContentRegionAvail().x / (card + 10)));
        for (const std::string& name : themeNames()) {
            if (column++ % perRow) ImGui::SameLine(0, 10);
            ImGui::PushID(name.c_str());
            bool isCurrent = currentTheme() == name;
            ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##theme", ImVec2(card, 92))) {
                setTheme(name);
                applyTheme(1.0f);
                saveEditorSettings();
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            bool hovered = ImGui::IsItemHovered();
            draw->AddRectFilled(at, ImVec2(at.x + card, at.y + 92), ImGui::GetColorU32(ImGuiCol_FrameBg), 8.0f);
            // A small editor in the card: a bar, a panel, an accent.
            draw->AddRectFilled(ImVec2(at.x + 10, at.y + 10), ImVec2(at.x + card - 10, at.y + 22), ImGui::GetColorU32(ImGuiCol_MenuBarBg), 3.0f);
            draw->AddRectFilled(ImVec2(at.x + 10, at.y + 26), ImVec2(at.x + 50, at.y + 64), ImGui::GetColorU32(ImGuiCol_WindowBg), 3.0f);
            draw->AddRectFilled(ImVec2(at.x + 54, at.y + 26), ImVec2(at.x + card - 10, at.y + 64), ImGui::GetColorU32(ImGuiCol_ChildBg, 1.0f), 3.0f);
            draw->AddRectFilled(ImVec2(at.x + 60, at.y + 32), ImVec2(at.x + 100, at.y + 40), ImGui::GetColorU32(ImGuiCol_ButtonActive), 2.0f);
            draw->AddText(ImVec2(at.x + 10, at.y + 70), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (isCurrent || hovered) draw->AddRect(at, ImVec2(at.x + card, at.y + 92), ImGui::GetColorU32(isCurrent ? ImGuiCol_ButtonActive : ImGuiCol_Border), 8.0f, 0, isCurrent ? 2.0f : 1.0f);
            ImGui::PopID();
        }
    } else {
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
                    context_->shortcuts.set(recording_, chord);
                    recording_.clear();
                    saveEditorSettings();
                }
            }
        }
        static std::string filter;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 130);
        ImGui::InputTextWithHint("##filter", "Search shortcuts", &filter);
        ImGui::SameLine();
        if (ImGui::Button("Reset All", ImVec2(-1, 0))) {
            context_->shortcuts.resetAll();
            recording_.clear();
            saveEditorSettings();
        }
        ImGui::TextDisabled("Click the keys of one, then press the new ones. Backspace: none. Esc: as it was.");
        auto lower = [](std::string text) {
            for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        std::string wanted = lower(filter);
        ImGui::BeginChild("##list", ImVec2(0, 0));
        std::string group;
        for (const Shortcuts::Action& action : context_->shortcuts.actions()) {
            if (!wanted.empty() && lower(action.title).find(wanted) == std::string::npos && lower(action.group).find(wanted) == std::string::npos) continue;
            ImGui::PushID(action.id.c_str());
            if (action.group != group) {
                group = action.group;
                ui::subheading(group.c_str());
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(action.title.c_str());
            std::vector<std::string> sharing = context_->shortcuts.sharing(action.id);
            if (!sharing.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1), "(also %s)", sharing.front().c_str());
            }
            bool isTaking = recording_ == action.id;
            std::string shown = isTaking ? std::string("Press the keys...") : context_->shortcuts.text(action.id);
            if (shown.empty()) shown = "none";
            float right = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(right - 190);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            if (isTaking) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(shown.c_str(), ImVec2(150, 0))) recording_ = isTaking ? std::string() : action.id;
            if (isTaking) ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (!(action.chord == action.original)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset")) {
                    context_->shortcuts.reset(action.id);
                    saveEditorSettings();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
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
void drawMenuEntries(const std::vector<MenuEntry>& entries) {
    for (size_t i = 0; i < entries.size(); i++) {
        const MenuEntry& entry = entries[i];
        if (entry.isLine()) {
            if (i > 0 && i + 1 < entries.size()) ImGui::Separator();
            continue;
        }
        ImGui::PushID(int(i));
        if (!entry.children.empty()) {
            if (ImGui::BeginMenu(entry.label.c_str(), entry.isEnabled)) {
                drawMenuEntries(entry.children);
                ImGui::EndMenu();
            }
        } else if (ImGui::MenuItem(entry.label.c_str(), nullptr, entry.isChecked, entry.isEnabled) && entry.run) {
            entry.run();
        }
        ImGui::PopID();
    }
}

void EditorApp::drawMenuRows(const Commands& commands, const std::string& menuPath) {
    EditorContext& editor = *context_;
    for (const Commands::Row& row : commands.rowsOf(menuPath)) {
        if (!row.command) {
            if (ImGui::BeginMenu(row.submenu.c_str())) {
                drawMenuRows(commands, menuPath + "/" + row.submenu);
                ImGui::EndMenu();
            }
            continue;
        }
        const Command& command = *row.command;
        if (command.isLine()) {
            ImGui::Separator();
            continue;
        }
        std::string name = command.name();
        if (command.entries) {
            // A list that is made as it is shown: in the menu itself, or in a submenu of its own.
            if (command.entriesAreInline) {
                ImGui::BeginDisabled(!command.isEnabled());
                drawMenuEntries(command.entries());
                ImGui::EndDisabled();
            } else if (ImGui::BeginMenu(name.c_str(), command.isEnabled())) {
                drawMenuEntries(command.entries());
                ImGui::EndMenu();
            }
            continue;
        }
        std::string keys = command.keys.empty() ? editor.shortcuts.text(command.id) : command.keys;
        if (ImGui::MenuItem(name.c_str(), keys.empty() ? nullptr : keys.c_str(), command.isChecked(), command.isEnabled()) && command.run) command.run();
    }
}

void EditorApp::drawRegisteredMenuItems(const std::string& menu) {
    EditorContext& editor = *context_;
    // What plugins put in this menu, as commands of a list of their own: submenus are then
    // drawn once each, however many items share them. Actions are copied: one may remove items.
    Commands added;
    for (size_t i = 0; i < editor.registry.menuItems.size(); i++) {
        const MenuItem& item = editor.registry.menuItems[i].value;
        if (item.path.compare(0, menu.size() + 1, menu + "/") != 0 || !item.action) continue;
        std::function<void(EditorContext&)> action = item.action;
        std::function<bool(EditorContext&)> isEnabled = item.isEnabled;
        Command& command = added.add({}, item.path, [action, &editor] { action(editor); });
        if (isEnabled) command.when([isEnabled, &editor] { return isEnabled(editor); });
        command.shows(item.shortcut);
    }
    if (added.all().empty()) return;
    ImGui::Separator();
    drawMenuRows(added, menu);
}

/// The menus of the bar, in order: the program's own, the editor's, those plugins added (before Window), Window and Help.
std::vector<std::string> EditorApp::menuBarMenus() const {
    EditorContext& editor = *context_;
    static const char* const first[] = {"Molten Engine", "File", "Edit", "Assets", "Entity", "Component"};
    static const char* const last[] = {"Window", "Help"};
    std::vector<std::string> menus(std::begin(first), std::end(first));
    for (const auto& item : editor.registry.menuItems) {
        std::string menu = item.value.path.substr(0, item.value.path.find('/'));
        bool isLast = std::any_of(std::begin(last), std::end(last), [&](const char* name) { return menu == name; });
        if (!isLast && std::find(menus.begin(), menus.end(), menu) == menus.end()) menus.push_back(menu);
    }
    menus.insert(menus.end(), std::begin(last), std::end(last));
    return menus;
}

namespace {
platform::NativeMenuRow nativeRowOf(const MenuEntry& entry) {
    platform::NativeMenuRow row;
    row.title = entry.label;
    row.run = entry.run;
    row.isEnabled = entry.isEnabled;
    row.isChecked = entry.isChecked;
    for (const MenuEntry& child : entry.children) row.children.push_back(nativeRowOf(child));
    return row;
}
}  // namespace

/// A menu of the bar as the Mac's menu bar shows it: the same rows as the menu drawn in the window.
std::vector<platform::NativeMenuRow> EditorApp::nativeMenuRows(const Commands& commands, const std::string& menuPath) const {
    EditorContext& editor = *context_;
    std::vector<platform::NativeMenuRow> rows;
    for (const Commands::Row& item : commands.rowsOf(menuPath)) {
        if (!item.command) {
            platform::NativeMenuRow submenu;
            submenu.title = item.submenu;
            submenu.children = nativeMenuRows(commands, menuPath + "/" + item.submenu);
            rows.push_back(std::move(submenu));
            continue;
        }
        const Command& command = *item.command;
        if (command.isLine()) {
            rows.push_back({});
            continue;
        }
        if (command.entries) {
            std::vector<MenuEntry> entries = command.isEnabled() ? command.entries() : std::vector<MenuEntry>{};
            if (command.entriesAreInline) {
                for (const MenuEntry& entry : entries) rows.push_back(nativeRowOf(entry));
            } else {
                platform::NativeMenuRow submenu;
                submenu.title = command.name();
                for (const MenuEntry& entry : entries) submenu.children.push_back(nativeRowOf(entry));
                submenu.isEnabled = command.isEnabled() && !submenu.children.empty();
                if (submenu.children.empty()) submenu.children.push_back(platform::NativeMenuRow{"(none)", {}, false, false, {}, {}});
                rows.push_back(std::move(submenu));
            }
            continue;
        }
        platform::NativeMenuRow row;
        row.title = command.name();
        row.keys = command.keys.empty() ? editor.shortcuts.text(command.id) : command.keys;
        row.isEnabled = command.isEnabled();
        row.isChecked = command.isChecked();
        row.run = command.run;
        rows.push_back(std::move(row));
    }
    return rows;
}

/// The Mac's menu bar, made (again) when the menus change: once at the start, and when a plugin adds a menu.
void EditorApp::updateNativeMenuBar() {
    if (!engine_->window() || !platform::usesNativeMenuBar()) return;
    // Full screen keeps the menu bar: the menus are in it (the window's green button, Window > Full Screen).
    platform::takeOverFullScreen(engine_->window()->nativeHandle());
    std::vector<std::string> menus = menuBarMenus();
    if (menus == nativeMenus_) return;
    nativeMenus_ = menus;
    platform::setNativeMenuBar(menus, [this](const std::string& menu) {
        EditorContext& editor = *context_;
        std::vector<platform::NativeMenuRow> rows = nativeMenuRows(editor.commands, menu);
        // What plugins put in this menu, after a line.
        Commands added;
        for (size_t i = 0; i < editor.registry.menuItems.size(); i++) {
            const MenuItem& item = editor.registry.menuItems[i].value;
            if (item.path.compare(0, menu.size() + 1, menu + "/") != 0 || !item.action) continue;
            std::function<void(EditorContext&)> action = item.action;
            std::function<bool(EditorContext&)> isEnabled = item.isEnabled;
            EditorContext* context = &editor;
            Command& command = added.add({}, item.path, [action, context] { action(*context); });
            if (isEnabled) command.when([isEnabled, context] { return isEnabled(*context); });
            command.shows(item.shortcut);
        }
        if (!added.all().empty()) {
            rows.push_back({});
            std::vector<platform::NativeMenuRow> more = nativeMenuRows(added, menu);
            rows.insert(rows.end(), more.begin(), more.end());
        }
        return rows;
    });
}

void EditorApp::drawMenuBar() {
    EditorContext& editor = *context_;
    // On a Mac the menus are the Mac's own, at the top of the screen: what was chosen there is done now.
    if (engine_->window() && platform::usesNativeMenuBar()) {
        updateNativeMenuBar();
        for (const std::function<void()>& action : platform::takeChosenMenuActions()) {
            if (action) action();
        }
        return;
    }
    if (!ImGui::BeginMainMenuBar()) return;
    for (const std::string& menu : menuBarMenus()) {
        if (!ImGui::BeginMenu(menu.c_str())) continue;
        drawMenuRows(editor.commands, menu);
        drawRegisteredMenuItems(menu);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorApp::showPreferences(const std::string& page) {
    showsSettings_ = true;
    settingsPage_ = page;
}

void EditorApp::showProjectSettings() { context_->showProjectSettings(); }

void EditorApp::showManual(const std::string& page) { molten::editor::showManual(*context_, page); }

// ---------------------------------------------------------------- Cmd+K

std::vector<PaletteCommand> EditorApp::paletteCommands() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    const bool hasProject = editor.hasProject, isEditing = hasProject && !scene.isPlaying();
    std::vector<PaletteCommand> commands;
    auto add = [&](std::string title, std::string kind, std::string shortcut, bool isEnabled, std::function<void()> run) {
        commands.push_back(PaletteCommand{std::move(title), std::move(kind), std::move(shortcut), isEnabled, std::move(run)});
    };
    // What the menus do: the same commands, by the same names.
    for (const Command& command : editor.commands.all()) {
        if (!command.run || command.isLine()) continue;
        std::function<void()> run = command.run;
        add(command.name(), command.where(), editor.keysOf(command.id), command.isEnabled(), run);
    }
    // Windows that plugins added.
    for (auto& item : editor.registry.panels) {
        Panel* panel = item.value.get();
        bool isCommand = false;
        for (const PaletteCommand& listed : commands) isCommand = isCommand || listed.title == panel->title;
        if (isCommand) continue;
        add(panel->title, "Window", "", hasProject, [panel, &editor] {
            panel->isOpen = true;
            editor.panelToFocus = panel->title;
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
                        ImGui::SetWindowFocus(ui::windowId("Explorer").c_str());
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
    if (showsAbout_ && !ImGui::IsPopupOpen("About Molten Engine")) ImGui::OpenPopup("About Molten Engine");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("About Molten Engine", &showsAbout_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Molten Engine v%s", kVersion);
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
    ImGui::SetNextWindowSize(ImVec2(620, std::min(460.0f, viewport->WorkSize.y - 60)), ImGuiCond_Appearing);
    if (!ImGui::Begin(ui::windowName("Build Settings").c_str(), &showsBuild_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    Project& project = editor.project;
    bool changed = false;
    // What is built: said once, here, and changed where it belongs (Edit > Project Settings > Game: the name,
    // the version, the identifier, the icon and the first scene; Input for the controls).
    {
        std::string iconFile = project.iconFile(findRootDirectory());
        if (ImTextureID icon = pictureAt(iconFile)) {
            ImGui::Image(icon, ImVec2(48, 48));
            ImGui::SameLine(0, 12);
        }
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, 18.0f);
        ImGui::Text("%s %s", project.name.c_str(), project.version.c_str());
        ImGui::PopFont();
        if (project.startScene.empty()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), "No first scene yet: save a scene, and it is the one the game starts with.");
        else ImGui::TextDisabled("Starts with %s%s", project.startScene.c_str(), project.identifier.empty() ? "" : ("  -  " + project.identifier).c_str());
        ImGui::TextDisabled("Its name, version, icon and first scene: Edit > Project Settings > Game.");
        ImGui::EndGroup();
    }
    int sticks = 0, buttons = 0;
    const ComponentType* stickType = ComponentRegistry::instance().find("TouchStick");
    const ComponentType* buttonType = ComponentRegistry::instance().find("TouchButton");
    for (Entity entity : editor.scene.entities()) {
        if (stickType && stickType->has(editor.engine.scene, entity)) sticks++;
        if (buttonType && buttonType->has(editor.engine.scene, entity)) buttons++;
        if (const kit::UIButton* button = editor.engine.scene.get<kit::UIButton>(entity); button && (button->isRound || !button->action.empty())) buttons++;
    }
    if (lastBuildFailed_) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "The last build of the scripts failed: the compiler's words are in the Console.");
        if (ImGui::SameLine(); ImGui::SmallButton("Try Again")) buildGameWithFeedback();
    }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
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
        if (wantsExport) exportGameWithFeedback();          // saves the scene and builds the scripts first
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
                editor.notify("Save a scene first: the game starts with the project's first scene", ConsoleMessage::Level::Warning);
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
        if (sticks + buttons == 0) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.35f, 1.0f), "The open scene has no touch controls: on a phone it cannot be played. Entity > UI has a Stick and a Round Button.");
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
            buildGameWithFeedback();
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
    const std::string withIds = ui::withWindowIds(*settings);         // a layout saved before the windows had icons
    ImGui::LoadIniSettingsFromMemory(withIds.c_str(), withIds.size());
    layoutName_ = name;
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
        inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
        hierarchy = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        scene = game = center;
    }
    ImGui::DockBuilderDockWindow(ui::windowId("Scene").c_str(), scene);
    ImGui::DockBuilderDockWindow(ui::windowId("Game").c_str(), game);
    ImGui::DockBuilderDockWindow(ui::windowId("Hierarchy").c_str(), hierarchy);
    ImGui::DockBuilderDockWindow(ui::windowId("Inspector").c_str(), inspector);
    ImGui::DockBuilderDockWindow(ui::windowId("Lighting").c_str(), inspector);
    ImGui::DockBuilderDockWindow(ui::windowId("Environment").c_str(), inspector);
    ImGui::DockBuilderDockWindow(ui::windowId("Rendering").c_str(), inspector);
    ImGui::DockBuilderDockWindow(ui::windowId("Post Processing").c_str(), inspector);
    ImGui::DockBuilderDockWindow(ui::windowId("Explorer").c_str(), bottom);
    ImGui::DockBuilderDockWindow(ui::windowId("Console").c_str(), console ? console : bottom);
    ImGui::DockBuilderDockWindow(ui::windowId("Visual Script").c_str(), bottom);
    ImGui::DockBuilderDockWindow(ui::windowId("Animation").c_str(), bottom);
    ImGui::DockBuilderDockWindow(ui::windowId("Timeline").c_str(), bottom);
    ImGui::DockBuilderDockWindow(ui::windowId("Animator").c_str(), bottom);
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
        ImGui::SetWindowFocus(ui::windowId("Explorer").c_str());
        ImGui::SetWindowFocus(ui::windowId("Inspector").c_str());
        ImGui::SetWindowFocus(ui::windowId("Hierarchy").c_str());
        showsSceneTab_ = true;
    }
    ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport());

    // The two views of the scene are windows like the panels: side by side if wanted.
    // Play brings the game to the front, Stop the scene.
    bool isPlaying = editor.scene.isPlaying();
    if (isPlaying != wasPlaying_) {
        ImGui::SetWindowFocus(ui::windowId(isPlaying ? "Game" : "Scene").c_str());
        wasPlaying_ = isPlaying;
    }
    drawSceneView(editor);
    drawGameView(editor, splashSeconds_ >= Splash::kDuration);
    if (showsSceneTab_) {
        // Asked for after both exist, so that it is the scene that ends up in front.
        ImGui::SetWindowFocus(ui::windowId("Scene").c_str());
        showsSceneTab_ = false;
    }
}

void EditorApp::drawPanels() {
    EditorContext& editor = *context_;
    // By index: a panel may register another panel while it draws.
    for (size_t i = 0; i < editor.registry.panels.size(); i++) {
        Panel* panel = editor.registry.panels[i].value.get();
        if (!panel->isOpen) continue;
        ImGui::SetNextWindowSize(panel->firstSize(), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        // A panel new to a layout saved before it existed: a tab beside its neighbour, not a window in the middle.
        if (!panel->firstBeside.empty() && !ImGui::FindWindowByName(ui::windowId(panel->title).c_str()) &&
            !ImGui::FindWindowSettingsByID(ImHashStr(ui::windowId(panel->title).c_str()))) {
            ImGuiWindow* beside = ImGui::FindWindowByName(ui::windowId(panel->firstBeside).c_str());
            if (beside && beside->DockId != 0) ImGui::SetNextWindowDockID(beside->DockId, ImGuiCond_Always);
        }
        bool* closes = ui::isCompactTab(panel->title) ? nullptr : &panel->isOpen;
        if (ImGui::Begin(ui::windowName(panel->title).c_str(), closes, panel->windowFlags())) panel->draw(editor);
        ui::windowTabTip(panel->title);
        ImGui::End();
    }
    // A panel asked for (Open Animator...): in front, once it has been drawn.
    if (!editor.panelToFocus.empty()) {
        if (ImGui::FindWindowByName(ui::windowId(editor.panelToFocus).c_str())) ImGui::SetWindowFocus(ui::windowId(editor.panelToFocus).c_str());
        editor.panelToFocus.clear();
    }
    // Clicking an asset brings the Explorer (where its viewer is) to the front of its tabs, and
    // selecting something in the scene the Inspector, without taking the keyboard from where it is.
    static std::string shownAsset;
    static size_t shownSelection = 0;
    auto bringForward = [](const char* name) {
        ImGuiWindow* window = ImGui::FindWindowByName(ui::windowId(name).c_str());
        if (window && window->DockNode && window->DockNode->TabBar) window->DockNode->TabBar->NextSelectedTabId = window->TabId;
    };
    const AssetInfo* asset = editor.selectedAsset.empty() ? nullptr : editor.assets.find(editor.selectedAsset);
    if (editor.selectedAsset != shownAsset) {
        shownAsset = editor.selectedAsset;
        if (asset) bringForward("Explorer");
    }
    size_t selection = editor.scene.selection().size();
    if (selection != shownSelection) {
        shownSelection = selection;
        if (selection > 0) bringForward("Inspector");
    }
    // Edit's Rename, Delete, Cut... are about what was chosen last: files, or things of the scene.
    static std::string lastAsset;
    static Entity lastPrimary;
    if (editor.selectedAsset != lastAsset) {
        lastAsset = editor.selectedAsset;
        editor.projectIsActive = !editor.selectedAsset.empty();
    }
    if (editor.scene.primary() != lastPrimary) {
        lastPrimary = editor.scene.primary();
        if (lastPrimary.isValid()) editor.projectIsActive = false;
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
            if (created) openProjectWithFeedback(created->directory);
            else projectWindow_.error = error;
            break;
        }
        case ProjectWindow::Action::Open: openProjectWithFeedback(asked.path, asked.scene); break;
        case ProjectWindow::Action::OpenOther: {
            std::string folder = platform::chooseFolder("Choose the folder of a Molten Metal Engine project");
            if (!folder.empty()) openProjectWithFeedback(folder);
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
        case ProjectWindow::Action::EditTemplate: {
            auto chosen = std::find_if(templates_.begin(), templates_.end(), [&](const ProjectTemplate& item) { return item.directory == asked.path; });
            if (chosen == templates_.end()) break;
            std::string folder = chosen->directory;
            if (!chosen->isYours) {
                // The engine's own is not changed: you change a copy of it, which takes its place in the list.
                std::string error;
                std::error_code code;
                fs::create_directories(yourTemplatesDirectory(), code);
                folder = copyAsTemplate(chosen->directory, yourTemplatesDirectory(), "", &error);
                if (folder.empty()) {
                    projectWindow_.error = "Could not copy the template: " + error;
                    break;
                }
            }
            templates_ = projectTemplates(findRootDirectory() + "/Templates", yourTemplatesDirectory());
            openProjectWithFeedback(folder);
            break;
        }
        case ProjectWindow::Action::ResetTemplate:
            if (platform::moveToTrash(asked.path)) {
                recent_.remove(asked.path);
                recent_.save(recentFile_);
                templates_ = projectTemplates(findRootDirectory() + "/Templates", yourTemplatesDirectory());
                projectWindow_.error.clear();
            } else {
                projectWindow_.error = "Could not move " + asked.path + " to the Trash.";
            }
            break;
        case ProjectWindow::Action::Delete:
            // To the Trash, where it can be taken back from; and off the list.
            if (platform::moveToTrash(asked.path)) {
                recent_.remove(asked.path);
                recent_.save(recentFile_);
                projectWindow_.error.clear();
            } else {
                projectWindow_.error = "Could not move " + asked.path + " to the Trash.";
            }
            break;
    }
}

}  // namespace molten::editor
