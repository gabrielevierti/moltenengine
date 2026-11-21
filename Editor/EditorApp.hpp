// EditorApp.hpp
// The editor as the engine sees it: a Game whose "gameplay" is editing the scene it shows.
#pragma once

#include "Shortcuts.hpp"
#include "Assets/FileChanges.hpp"
#include "CommandPalette.hpp"
#include "DeviceExport.hpp"
#include "ProjectWindow.hpp"

#include <filesystem>
#include <unordered_map>

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
    void drawNewScriptDialog();
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
    void saveProjectPreview();
    void makeMissingTemplatePreviews();
    ImTextureID pictureAt(const std::string& path);
    void drawPlayBar();
    void drawLayoutItems();
    void drawSaveLayoutDialog();
    void applyPendingLayout();
    std::string layoutsDirectory() const;
    std::vector<std::string> savedLayouts() const;
    void chooseAndOpenProject();
    void chooseAndOpenScene();
    void drawUnsavedChangesDialog();
    void drawAboutDialog();
    void drawCommandPalette();
    void drawSettingsWindow();
    /// The action's keys were pressed this frame (see Shortcuts).
    bool isPressed(const std::string& action);
    void saveEditorSettings();
    std::vector<PaletteCommand> paletteCommands();
    void drawBuildWindow();
    void updateTitle();

    Engine* engine_ = nullptr;
    std::unique_ptr<EditorContext> context_;
    ImGuiLayer imgui_;
    PluginHost plugins_;
    GameModule gameModule_;             // the open project's own code
    RecentProjects recent_;
    std::string recentFile_;

    // Scripts written as code.
    uint64_t builtSignature_ = 0;       // of the Source folder when it was last built
    double nextSourceCheck_ = 0;
    bool showsNewScript_ = false;
    std::string newScriptName_ = "PlayerController";
    std::string newScriptError_;

    float splashSeconds_ = 0;           // how long the splash has been up
    std::shared_ptr<Texture> emptyScenePicture_;        // what an empty project starts with, for the project window
    bool needsDefaultLayout_ = false;
    bool showsAbout_ = false;
    CommandPalette palette_;            // Cmd+K
    Shortcuts shortcuts_;               // changed in Edit > Editor Settings
    bool showsSettings_ = false;
    std::string recording_;             // the action whose new keys are being pressed
    std::string settingsFile_;
    bool showsBuild_ = false;           // the Build and Export window
    int buildPlatform_ = 0;             // 0 Mac, 1 iPhone and iPad
    bool lastBuildFailed_ = false;
    DeviceExport deviceExport_;         // the work of putting the game on an iPhone or an iPad
    std::string appleTeam_;             // the developer team that signs it: the user's, kept with the editor's own settings
    std::vector<std::string> deviceLines_;      // the last things that work said
    bool exportsForOtherMacs_ = false;  // Developer ID and notarization after a Mac export
    bool exportsDevelopment_ = false;   // a development build: the developer keys on in the exported game
    double nextAssetScan_ = 0;
    FileChanges assetChanges_;          // pictures, models, materials written by other programs: read again
    std::string shownTitle_;

    // The project list's "new project" form.
    ProjectWindow projectWindow_;
    int frontTabsIn_ = 0;
    struct Picture {
        std::shared_ptr<Texture> texture;
        std::filesystem::file_time_type time;
    };
    std::unordered_map<std::string, Picture> pictures_;     // preview images, by path
    std::string layoutName_ = "Default";       // what the layout menu shows as current
    std::string pendingLayout_;                 // asked for; applied between frames
    std::string newLayoutName_;
    bool showsSaveLayout_ = false;
    bool wasPlaying_ = false;
    bool showsSceneTab_ = false;
    std::vector<ProjectTemplate> templates_;     // what a new project can start from

    // An action waiting for the answer to "save your changes?".
    std::string confirmAction_, confirmArgument_;
    bool confirmIsOpen_ = false;
};

}  // namespace molten::editor
