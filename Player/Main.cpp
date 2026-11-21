// The player: the program an exported game is. It has no game in it. It loads a
// project (its first scene, its assets) and the library the project's code was built
// into, and runs them.
//
//   MyGame.app/Contents/MacOS/MyGame            this program, renamed
//   MyGame.app/Contents/Resources/Shaders       the engine's shaders
//   MyGame.app/Contents/Resources/Assets        the engine's own assets
//   MyGame.app/Contents/Resources/Project       the project: its .molten file and Assets
//   MyGame.app/Contents/Resources/Game.dylib    the project's code
//
// While developing it can also be pointed at a project folder:
//
//   ./molten-player ~/Games/MyGame [Scenes/Level2.scene]
//
// On iOS there are no loadable libraries of one's own: the project's code is compiled
// into this program instead (see the iOS section of CMakeLists.txt), and the rest is the same.
#include "MoltenMetal.hpp"

#include "Core/Paths.hpp"
#include "Platform/Application.hpp"
#include "Platform/DeviceState.hpp"
#include "Project/Project.hpp"

#include <algorithm>
#include <filesystem>

using namespace molten;
namespace fs = std::filesystem;

namespace {

class PlayerGame final : public Game {
public:
    void start(Engine& engine) override {
        std::string root = findRootDirectory(engine.config().executablePath);
        std::error_code code;

        // The project: the folder given on the command line, or the one packed with the program.
        std::string projectPath, scenePath;
        for (const std::string& argument : engine.config().arguments) {
            if (argument.empty() || argument[0] == '-') continue;
            if (projectPath.empty()) projectPath = argument;
            else if (scenePath.empty()) scenePath = argument;
        }
        bool isPacked = projectPath.empty();
        if (isPacked && fs::exists(fs::path(root) / "Project", code)) projectPath = (fs::path(root) / "Project").string();

        std::string error;
        std::optional<Project> project = projectPath.empty() ? std::nullopt : Project::load(projectPath, &error);
        if (project) {
            engine.assets().setProjectDirectory(project->assetsDirectory());
            // What the game keeps for the player: settings, records. In the player's own folder,
            // named after the game (on a phone, inside the application's own).
            engine.preferencesFile = userDataDirectory(project->name.empty() ? std::string("Molten Game") : project->name) + "/Preferences.txt";
            // The person's language, when the game has a table for it (Localization/<language>.txt).
            std::string spoken = preferredLanguage();
            for (const std::string& offered : engine.languages()) {
                if (offered == spoken) engine.language = spoken;
            }
            // A development build (the editor's export marks it): the developer keys, the profiler's overlay with F1.
            if (fs::exists(fs::path(projectPath) / "Development", code)) {
                engine.setDeveloperKeys(true);
                logInfo("a development build: F1 statistics and profiler, F2 colliders, F3 full screen, F4 screenshot");
            }
            if (Window* window = engine.window()) {
                window->setTitle(project->name);
                if (!project->icon.empty()) window->setIcon(project->assetsDirectory() + "/" + project->icon);
            }
            // The project's code: packed beside the project, or where the editor builds it.
            fs::path library = isPacked ? fs::path(root) / "Game.dylib" : fs::path(project->libraryDirectory()) / "Game" / "Game.dylib";
            if (fs::exists(library, code) && !module_.load(library.string(), &error)) {
                logError("the game's code could not be loaded: %s", error.c_str());
            }
            if (scenePath.empty()) scenePath = project->startScene;
        } else if (!projectPath.empty()) {
            logError("%s", error.c_str());
        }

        // The scene: the project's first one, or the engine's sample when there is no project.
        // Loaded by name, as the game itself loads scenes: it can then start this one over
        // (reloadScene) or go on to the next (loadScene).
        std::string sceneName = project && !scenePath.empty() ? scenePath : std::string("Scenes/demo.scene");
        if (!engine.loadSceneNow(sceneName)) logError("no scene to play: %s could not be read", sceneName.c_str());
        // The splash first, unless the project turned it off or nobody is watching.
        bool wantsSplash = engine.window() != nullptr && !engine.config().hasArgument("--no-splash") && (!project || project->showsSplash);
        splashSeconds_ = wantsSplash ? 0.0f : Splash::kDuration;
        subtitle_ = project ? project->name : std::string();
        engine.runsComponents = !wantsSplash;
        engine.showsInterfaceWhileStopped = false;      // the splash alone, then the game with its interface
        engine.isSimulationPaused = wantsSplash;        // the game waits behind the splash
    }

    // Over the game and its interface: the splash, and then the game coming through it as it fades.
    void drawOver(Engine& engine, Canvas& canvas) override {
        if (Splash::isOver(splashSeconds_)) return;
        if (!logo_ && !triedLogo_) {
            triedLogo_ = true;
            logo_ = engine.assets().texture("Branding/logo.png");
        }
        Splash::draw(canvas, splashSeconds_, subtitle_, logo_.get());
        splashSeconds_ += std::min(engine.time.unscaledDelta, 0.1f);
        // A key, a click or a tap is "I have seen it".
        bool skipped = engine.input.wasPressed(MouseButton::Left) || engine.input.wasPressed(Key::Space) ||
                       engine.input.wasPressed(Key::Enter) || engine.input.wasPressed(Key::Escape);
        if (skipped) splashSeconds_ = std::max(splashSeconds_, Splash::kDuration - Splash::kFade);
        // The game starts as the splash begins to fade: what shows through it is the game
        // itself, running, with its interface; not a still picture that starts later.
        if (splashSeconds_ >= Splash::kDuration - Splash::kFade || skipped) {
            engine.runsComponents = true;
            engine.isSimulationPaused = false;
        }
    }

private:
    std::shared_ptr<Texture> logo_;
    bool triedLogo_ = false;
    GameModule module_;
    float splashSeconds_ = Splash::kDuration;
    std::string subtitle_;
};

}  // namespace

int main(int argc, char** argv) {
    EngineConfig defaults;
    defaults.title = "Molten Metal Engine";
    defaults.developerKeys = false;
    return runApplication(argc, argv, defaults, [](Engine&) { return std::make_unique<PlayerGame>(); });
}
