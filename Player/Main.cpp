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
#include "Project/Project.hpp"

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
            if (Window* window = engine.window()) window->setTitle(project->name);
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
        std::string sceneFile = project && !scenePath.empty() ? project->assetsDirectory() + "/" + scenePath
                                                              : engine.assets().path("Scenes/demo.scene");
        if (!loadSceneFile(sceneFile, engine.scene, materials_, engine.renderer().settings, sceneAssets(engine.assets()))) {
            logError("no scene to play: %s could not be read", sceneFile.c_str());
        }
        engine.runsComponents = true;
        engine.restartComponents();
    }

private:
    GameModule module_;
    std::vector<NamedMaterial> materials_;
};

}  // namespace

int main(int argc, char** argv) {
    EngineConfig defaults;
    defaults.title = "Molten Metal Engine";
    defaults.developerKeys = false;
    return runApplication(argc, argv, defaults, [](Engine&) { return std::make_unique<PlayerGame>(); });
}
