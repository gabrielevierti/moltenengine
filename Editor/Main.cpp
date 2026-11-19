// The Molten editor.
//
//   ./molten                         shows the list of projects
//   ./molten ~/Games/MyGame          opens a project (its folder or its .molten file)
//   ./molten ~/Games/MyGame Scenes/Level2.scene     and a scene of it
//
// Right mouse button + W A S D   look around and fly        W E R T   move, rotate, scale, all three
// Alt + left button              orbit                      X         world or local handles
// Middle button, scroll          pan, zoom                  F         frame the selection
// Cmd+S save   Cmd+Z undo   Shift+Cmd+Z redo   Cmd+D duplicate   Del delete   Cmd+P play / stop
#include "EditorApp.hpp"

int main(int argc, char** argv) {
    molten::EngineConfig defaults;
    defaults.title = "Molten Metal Engine";
    defaults.width = 1600;
    defaults.height = 960;
    defaults.developerKeys = false;         // F1 to F4 belong to the editor here
    molten::EngineConfig config = molten::EngineConfig::fromArguments(argc, argv, defaults);
    molten::Engine engine(config);

    molten::editor::EditorApp editor;
    for (const std::string& argument : config.arguments) {
        if (argument.empty() || argument[0] == '-') continue;
        if (editor.projectToOpen.empty()) editor.projectToOpen = argument;
        else if (editor.sceneToOpen.empty()) editor.sceneToOpen = argument;
    }
    return engine.run(editor);
}
