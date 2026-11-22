// EditorApp.hpp
// The editor as the engine sees it: a Game whose "gameplay" is editing the scene it shows.
#pragma once

#include "Shortcuts.hpp"
#include "Assets/FileChanges.hpp"
#include "CommandPalette.hpp"
#include "DeviceExport.hpp"
#include "ProjectWindow.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "Editor.hpp"

namespace molten::editor {

/// The editor's version, and where its source is.
inline constexpr const char* kVersion = "1.2";
inline constexpr const char* kRepository = "https://github.com/gabrielevierti/moltenengine";

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
    // ---- long work, a step at a time, with a screen that says what is being done
    struct WorkStep {
        std::string label;                  // "Building the game's code"
        std::function<bool()> run;          // on the main thread; false stops the work there
        /// The slow part of the step, done first and away from the main thread while the screen
        /// goes on moving. It must touch nothing the editor draws with: files, the compiler,
        /// values of its own. `begin`, on the main thread, hands it what it needs first.
        std::function<void()> begin, prepare;
        /// How far `prepare` is, 0 to 1, when it can say: the bar follows it.
        std::function<float()> progress;
        float seconds = 0.3f;               // about how long the step takes: its share of the bar
    };
    struct Work {
        std::string title;                  // "Opening Driving"
        bool coversAll = false;             // the whole window (opening a project) or a card over the editor
        std::vector<WorkStep> steps;
        size_t next = 0;
        int waitFrames = 2;                 // frames to show the screen before the next step makes the editor wait
        std::future<void> preparing;        // the step's slow part, while it runs
        bool isPreparing = false, isPrepared = false;
        double started = 0;
        double stepStarted = -1;            // when the step under way became the one under way
        double finishedAt = -1;             // when the last step ended
        double fullAt = -1;                 // when the bar reached its end: the screen fades from then
        int calmFrames = 0;                 // frames at an even pace since the work ended: what is under the screen has settled
        float shown = 0;                    // the bar, 0 to 1: it only moves forward, and never in a jump
    };
    std::optional<Work> work_;
    /// Shows the work's screen at once and does its steps over the next frames. Ignored while
    /// other work is under way.
    void startWork(Work work);
    void runWork();
    void drawWork();
    bool isWorking() const { return work_.has_value(); }
    /// What opening a project is, step by step; `sceneAfter` (relative to its Assets) is opened last.
    std::vector<WorkStep> projectSteps(const std::string& path, const std::string& sceneAfter);
    /// Building the game's code as a step: the compiler runs away from the main thread.
    WorkStep buildStep(const std::string& label);
    /// Opening a scene as a step: its models and pictures are read on other threads first.
    /// `pathNow` says which, when the step begins; empty for a new scene.
    WorkStep sceneStep(const std::string& label, std::function<std::string()> pathNow, std::function<void(bool opened)> then);
    void openProjectWithFeedback(const std::string& path, const std::string& sceneAfter = {});
    void openSceneWithFeedback(const std::string& absolutePath);
    void buildGameWithFeedback();
    void exportGameWithFeedback();

    bool openProject(const std::string& path);
    void closeProject();
    void openSceneNow(const std::string& absolutePath);
    void saveScene(bool asNewFile);
    bool buildGame();
    /// The second half of building: loads what the compiler made, or says what it said.
    bool finishBuild(const struct BuildResult& built);
    std::unordered_set<std::string> loadedLibraries_;      // builds of game code loaded since the editor started
    void drawNewScriptDialog();
    void exportGame();
    void runPendingAction();
    void handleShortcuts();
    void handleWindowClose();

    // The commands (EditorCommands.cpp): the one list the menus, the keys and the search are made from.
    void registerCommands();
    void drawMenuBar();
    void drawMenuRows(const Commands& commands, const std::string& menuPath);
    void drawRegisteredMenuItems(const std::string& menu);
    void copySelection(bool cuts);
    bool canPasteEntities() const;
    void pasteEntities();
    void refreshAssets();
    void showPreferences(const std::string& page = {});
    void showProjectSettings();
    void showManual(const std::string& page);
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

    // Everything the console hears also goes to a file beside the program (build/editor.log): what
    // happened on this Mac, to read after the fact or to send to someone who can help.
    std::FILE* logFile_ = nullptr;
    void openLogFile();
    void writeLog(const char* level, const std::string& message);
    void logRendererState();

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
    std::string settingsPage_;          // the page of Preferences to show when it opens; empty: where it was
    // Preferences > General, kept in the editor's own settings file.
    bool autosaves_ = true;             // the open scene's unsaved changes are kept aside, to recover after a crash
    int autosaveMinutes_ = 2;
    bool reopensLastProject_ = false;   // the editor opens on the project it had, not on the project window
    double nextAutosave_ = 0;           // in seconds of the editor's own clock
    std::string recoverPath_, recoverText_;     // an autosave newer than the scene just opened: asked about
    std::string autosavePathFor(const std::string& sceneFile) const;
    void autosave();
    void forgetAutosave();
    void lookForAutosave();
    void drawRecoverDialog();
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
