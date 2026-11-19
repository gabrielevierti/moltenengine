// EditorApp.hpp
// The editor as the engine sees it: a Game whose "gameplay" is editing the scene it shows.
#pragma once

#include "Editor.hpp"

namespace molten::editor {

class EditorApp : public Game {
public:
    /// A project folder or .molten file to open at start. Empty shows the project list.
    std::string projectToOpen;
    /// A scene to open once the project is: a path relative to its Assets folder.
    std::string sceneToOpen;

    void start(Engine& engine) override;
    void draw(Engine& engine, Canvas& canvas) override;
    void shutdown(Engine& engine) override;

private:
    bool openProject(const std::string& path);
    void closeProject();
    void openSceneNow(const std::string& absolutePath);
    void saveScene(bool asNewFile);
    bool buildGame();
    void exportGame();
    void runPendingAction();
    void handleShortcuts();
    void handleWindowClose();

    void drawMenuBar();
    void drawRegisteredMenuItems(const std::string& menu);
    void drawStatusBar();
    void drawDockSpace();
    void buildDefaultLayout(ImGuiID dockSpace);
    void drawPanels();
    void drawProjectList();
    void drawUnsavedChangesDialog();
    void drawAboutDialog();
    void updateTitle();

    Engine* engine_ = nullptr;
    std::unique_ptr<EditorContext> context_;
    ImGuiLayer imgui_;
    PluginHost plugins_;
    GameModule gameModule_;             // the open project's own code
    RecentProjects recent_;
    std::string recentFile_;

    bool needsDefaultLayout_ = false;
    bool showsAbout_ = false;
    bool showsDemo_ = false;
    double nextAssetScan_ = 0;
    std::string shownTitle_;

    // The project list's "new project" form.
    std::string newProjectName_ = "My Game";
    std::string newProjectLocation_;
    std::string projectError_;

    // An action waiting for the answer to "save your changes?".
    std::string confirmAction_, confirmArgument_;
    bool confirmIsOpen_ = false;
};

}  // namespace molten::editor
