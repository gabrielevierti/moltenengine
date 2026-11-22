// ProjectWindow.hpp
// What the editor shows when no project is open: the launcher. On the left, the projects
// opened lately. On the right, the one chosen among them: what its file says, its scenes,
// its folders and files, and what can be done with it (open it, show it in the Finder, take
// it off the list, move it to the Trash). "New Project" shows the form for a new one, empty
// or from a template.
//
// It draws and reports what was asked; the editor does it. That keeps file dialogs, the
// Trash and the opening of projects out of here, and lets the window be drawn in a test.
#pragma once

#include "Project/Project.hpp"
#include "ProjectSummary.hpp"

#include <imgui.h>

#include <functional>
#include <future>
#include <string>
#include <unordered_set>
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
    /// The engine's logo, a wide picture (1200 x 268): at the top of the list of projects.
    std::string logoPath;
    /// A picture of the scene an empty project starts with, 16 by 9. None: a drawing instead.
    ImTextureID emptyPicture = ImTextureID(0);
    /// Projects are read off the main thread, so that going down the list never waits for a
    /// disk. A test that draws one frame turns it off and reads at once.
    bool readsInBackground = true;

    enum class Action { None, Create, Open, OpenOther, ChooseLocation, Forget, Reveal, Delete };
    struct Result {
        Action action = Action::None;
        std::string path;               // Open, Forget, Reveal, Delete: the project's folder (Reveal: or a file in it)
        std::string scene;              // Open: the scene to open it on, relative to its Assets; empty for the one it starts with
    };

    /// Draws the window over the whole work area. `recentPaths` are project folders, most
    /// recent first.
    Result draw(const std::vector<std::string>& recentPaths, const std::vector<ProjectTemplate>& templates, const char* version);

    /// Opens on the form for a new project (File > New Project).
    void showNewProject() {
        page_ = Page::New;
        hasChosenPage_ = true;
        focusesName_ = true;
    }
    /// What is known about the projects is read again: something changed on the disk.
    void forgetSummary() { summaryPath_.clear(); }

private:
    enum class Page { Project, New };

    void drawSidebar(Result& result, const std::vector<std::string>& recentPaths, const char* version);
    void drawProject(Result& result);
    void drawProjectHeader(Result& result, float width);
    void drawNumbers(float width);
    void drawScenes(Result& result, ImVec2 size);
    void drawFiles(Result& result, ImVec2 size);
    void drawDeleteQuestion(Result& result);
    void drawNewProject(Result& result, const std::vector<ProjectTemplate>& templates);
    void drawError();
    void choose(const std::string& path);
    void readChosenProject();

    Page page_ = Page::Project;
    bool hasChosenPage_ = false;        // the first page depends on whether there are recent projects
    std::string chosenPath_;            // the project chosen in the list
    bool focusesName_ = true;

    // What is known of the chosen project, read when it is chosen.
    ProjectSummary summary_;
    std::string summaryPath_;           // the folder summary_ is of, or is being read of
    std::future<ProjectSummary> reading_;
    double readingSince_ = 0;
    double lastDrawn_ = -10;            // when the project page was last on screen: shown again after a while, it is read again
    std::unordered_set<std::string> openFolders_;       // of the list of files, by path
    bool asksDelete_ = false;
};

}  // namespace molten::editor
