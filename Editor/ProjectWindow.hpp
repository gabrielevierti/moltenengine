// ProjectWindow.hpp
// What the editor shows when no project is open: the projects opened lately on the left,
// and on the right a new project, empty or started from a small game.
//
// It draws and reports what was asked; the editor does it. That keeps file dialogs and
// the opening of projects out of here, and lets the window be drawn in a test.
#pragma once

#include "Project/Project.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace molten::editor {

class ProjectWindow {
public:
    std::string name = "My Game";
    std::string location;               // the folder the project's folder is created in
    int chosenTemplate = -1;            // an index into the templates; -1 for an empty project
    std::string error;                  // shown until dismissed or until something else is tried
    /// Gives a picture file as something ImGui can draw; 0 if there is none. Set by the editor:
    /// a project is shown with the picture of its scene (preview.png in its folder) when it has one.
    std::function<ImTextureID(const std::string& path)> picture;
    /// The engine's logo, a wide picture: shown at the top of the list of projects.
    std::string logoPath;
    /// A picture of the scene an empty project starts with, 16 by 9. None: a drawing instead.
    ImTextureID emptyPicture = ImTextureID(0);

    enum class Action { None, Create, Open, OpenOther, ChooseLocation, Forget, Reveal };
    struct Result {
        Action action = Action::None;
        std::string path;               // Open, Forget, Reveal: the project's folder
    };

    /// Draws the window over the whole work area. `recentPaths` are project folders, most
    /// recent first.
    Result draw(const std::vector<std::string>& recentPaths, const std::vector<ProjectTemplate>& templates, const char* version);

private:
    bool focusesName_ = true;
};

}  // namespace molten::editor
