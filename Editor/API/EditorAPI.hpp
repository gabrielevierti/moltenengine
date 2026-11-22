// EditorAPI.hpp
// Everything an editor plugin can see, in one include. The editor's own panels are
// written against this same interface: whatever they do, a plugin can do.
//
// A plugin is a dynamic library with one class:
//
//     #include "API/EditorAPI.hpp"
//
//     struct Hello : molten::editor::Plugin {
//         const char* name() const override { return "Hello"; }
//         void onLoad(molten::editor::EditorContext& editor) override {
//             editor.registry.addMenuItem(this, "Tools/Say Hello", [](molten::editor::EditorContext& e) { e.notify("Hello!"); });
//         }
//     };
//     MOLTEN_EDITOR_PLUGIN(Hello)
//
// The editor loads every .dylib in the Plugins folder next to its executable and in
// the Plugins folder of the open project. See Docs/Plugins.md.
#pragma once

#include "MoltenMetal.hpp"

#include "Assets/AssetDatabase.hpp"
#include "Project/Project.hpp"
#include "Commands.hpp"
#include "SceneDocument.hpp"
#include "Shortcuts.hpp"

#include <imgui.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

/// Raised whenever something here changes in a way that breaks plugins built before.
#define MOLTEN_EDITOR_API_VERSION 2

namespace molten::editor {

struct EditorContext;
/// The scene being edited: editor.engine.scene, for code that only has the declaration above.
Scene& sceneOf(EditorContext& editor);

// ---------------------------------------------------------------- the viewport's tools

enum class GizmoOperation { Move, Rotate, Scale, Universal };
enum class GizmoSpace { World, Local };

struct GizmoSettings {
    GizmoOperation operation = GizmoOperation::Move;
    GizmoSpace space = GizmoSpace::World;
    bool snapEnabled = false;           // holding Cmd while dragging inverts it
    bool aroundCenter = true;           // several selected: the handles at their middle (Center) or on the last chosen (Pivot)
    float moveSnap = 0.5f;              // world units
    float rotateSnap = 15.0f;           // degrees
    float scaleSnap = 0.1f;
    /// What moving snaps to: steps of moveSnap (with Snap on), the surface under the mouse, or
    /// the nearest corner of another model. Holding V while moving snaps to corners too.
    enum class SnapTo { Steps, Surfaces, Corners } snapTo = SnapTo::Steps;
    bool standsOnSurface = true;        // snapped to a surface: turned to stand on it, too
    bool showGrid = true;
    // What the scene view draws over the scene, kind by kind (the Snap menu lists them).
    bool showLights = true;             // their icon, and which way they shine
    bool showCameras = true;            // their icon, and what they see
    bool showEffects = true;            // particle emitters' icon
    bool showSounds = true;             // Audio Sources' icon
    bool showSplines = true;            // the line of each Spline, and its points
    bool showSelection = true;          // the box around what is selected, and its colliders
    bool showPluginOverlays = true;     // what plugins draw in the view
    float flySpeed = 6.0f;              // of the camera, world units per second
    float handleSize = 0.085f;          // of the move, rotate and scale handles: a share of the view's height
    float iconSize = 0.85f;             // of the icons of lights, cameras, emitters and sounds: 1 is 23 points across
};

/// What a viewport overlay gets: where the 3D view is on screen and how to draw on it.
struct ViewportInfo {
    ImVec2 position{0, 0}, size{0, 0};  // in points
    Mat4 view = identity(), projection = identity();
    Vec3 cameraPosition{0, 0, 0};
    ImDrawList* drawList = nullptr;
    bool isHovered = false;             // the mouse is over the 3D view and nothing is in front of it

    /// Where a point of the world appears. False if it is behind the camera.
    bool worldToScreen(Vec3 world, ImVec2& screen) const;
    /// The ray from the camera through a point of the screen.
    Ray screenRay(ImVec2 screen) const;
};

// ---------------------------------------------------------------- what plugins add

/// A dockable window. `draw` is called between ImGui::Begin and ImGui::End.
class Panel {
public:
    virtual ~Panel() = default;
    virtual void draw(EditorContext& editor) = 0;
    virtual ImGuiWindowFlags windowFlags() const { return 0; }
    /// How large it is the first time it is opened, before it is docked or sized by hand.
    virtual ImVec2 firstSize() const { return ImVec2(420, 520); }

    std::string title;                  // also its identity: unique among panels
    bool isOpen = true;
};

struct MenuItem {
    std::string path;                   // "Tools/Level/Align to Ground": the first part is the menu
    std::string shortcut;               // only shown; bind keys yourself in onUpdate
    std::function<void(EditorContext&)> action;
    std::function<bool(EditorContext&)> isEnabled;      // empty: always
};

/// How one component type appears in the inspector and in "Add Component".
struct ComponentEditor {
    std::string name;
    std::function<bool(Scene&, Entity)> has;
    std::function<void(Scene&, Entity)> add;
    std::function<void(Scene&, Entity)> remove;
    /// Draws the fields. Return true if something was edited, so it can be undone.
    std::function<bool(EditorContext&, Entity)> draw;
};

/// An entry of the Create menu: makes an entity and returns it.
struct CreateEntry {
    std::string path;                   // "Light/Point Light"
    std::function<Entity(EditorContext&)> create;
};

/// Everything registered, by the editor itself and by plugins. `owner` is the plugin
/// (or any pointer of yours): when a plugin is unloaded, what it registered goes too.
class Registry {
public:
    Panel* addPanel(const void* owner, std::unique_ptr<Panel> panel);
    void addMenuItem(const void* owner, std::string path, std::function<void(EditorContext&)> action, std::string shortcut = {},
                     std::function<bool(EditorContext&)> isEnabled = {});
    void addComponent(const void* owner, ComponentEditor editor);
    void addCreateEntry(const void* owner, std::string path, std::function<Entity(EditorContext&)> create);
    void addViewportOverlay(const void* owner, std::function<void(EditorContext&, const ViewportInfo&)> draw);

    /// The usual component: default-constructed by Add, drawn by `draw`.
    template <class T> void addComponent(const void* owner, std::string name, std::function<bool(EditorContext&, Entity, T&)> draw) {
        ComponentEditor editor;
        editor.name = std::move(name);
        editor.has = [](Scene& scene, Entity entity) { return scene.has<T>(entity); };
        editor.add = [](Scene& scene, Entity entity) { scene.add(entity, T{}); };
        editor.remove = [](Scene& scene, Entity entity) { scene.remove<T>(entity); };
        editor.draw = [draw = std::move(draw)](EditorContext& context, Entity entity) {
            T* component = sceneOf(context).template get<T>(entity);
            return component ? draw(context, entity, *component) : false;
        };
        addComponent(owner, std::move(editor));
    }

    void removeAll(const void* owner);
    Panel* findPanel(const std::string& title);

    template <class T> struct Owned {
        const void* owner = nullptr;
        T value;
    };
    std::vector<Owned<std::unique_ptr<Panel>>> panels;
    std::vector<Owned<MenuItem>> menuItems;
    std::vector<Owned<ComponentEditor>> components;
    std::vector<Owned<CreateEntry>> createEntries;
    std::vector<Owned<std::function<void(EditorContext&, const ViewportInfo&)>>> viewportOverlays;
};

// ---------------------------------------------------------------- the editor's state

struct ConsoleMessage {
    enum class Level { Info, Warning, Error, Debug };
    Level level = Level::Info;
    std::string text;
    double time = 0;                    // seconds since the editor started
    int count = 1;                      // identical messages in a row are shown once, with a number
};

/// The editor as plugins and panels see it.
struct EditorContext {
    explicit EditorContext(Engine& engine) : engine(engine), scene(engine) {}

    Engine& engine;

    bool hasProject = false;
    Project project;
    AssetDatabase assets;               // of the open project

    SceneDocument scene;                // the scene being edited: files, undo, play, entities
    GizmoSettings gizmo;
    Registry registry;

    /// The asset shown in the inspector when no entity is selected: a path relative to Assets.
    std::string selectedAsset;
    /// The folder the asset browser shows. New assets are made here.
    std::string currentFolder;

    /// Opens a visual script (a .graph file, as a path relative to Assets) on the drawing board.
    void editGraph(const std::string& path);
    /// Opens a panel (Window menu) and brings it to the front of its tabs: "Animator", "Animation".
    void openPanel(const std::string& title);
    /// Opens Edit > Project Settings, on a page of it if one is named: "Game", "Quality", "Input", "Physics", "Tags and Layers".
    void showProjectSettings(const std::string& page = {});
    std::string projectSettingsPage;    // asked for by showProjectSettings; the window clears it
    std::string panelToFocus;           // asked for by openPanel; the editor clears it
    std::string graphToEdit;            // what the board has been asked to open; it clears this

    /// A panel that edits something of its own (the visual script board) sets this every
    /// frame it has the keyboard: Delete, Cmd+Z, Cmd+D and Cmd+A are then its, not the scene's.
    bool panelOwnsEditKeys = false;
    /// The Explorer has the keyboard: Delete and the like are for its files, not the scene. Set each frame it does.
    bool explorerHasKeys = false;

    std::vector<ConsoleMessage> console;

    /// Says something to the user: in the status bar for a few seconds, and in the console.
    void notify(const std::string& message, ConsoleMessage::Level level = ConsoleMessage::Level::Info);
    std::string statusText;
    double statusUntil = 0;

    // ---- things panels ask the editor to do. Carried out at the start of the next frame,
    // after asking about unsaved changes where that matters.
    void requestOpenScene(const std::string& path) { pendingAction = "open-scene"; pendingArgument = path; }
    void requestNewScene() { pendingAction = "new-scene"; pendingArgument.clear(); }
    void requestOpenProject(const std::string& directory) { pendingAction = "open-project"; pendingArgument = directory; }
    void requestQuit() { pendingAction = "quit"; pendingArgument.clear(); }
    std::string pendingAction, pendingArgument;

    /// The 3D view of this frame; valid while panels draw.
    ViewportInfo viewport;
    /// Moves the editor's camera to look at the selection.
    bool frameSelectionRequested = false;
    bool alignWithViewRequested = false;    // put the selection where the view's camera is, facing the same way
    bool wantsNewScript = false;            // a panel asked for the New Script window
    bool gameViewHasFocus = false;          // the Game window has the keyboard: while playing, keys are the game's

    // ---- commands: what the menus, the keys and the search (Cmd+K) all do. See Commands.hpp.
    Commands commands;
    Shortcuts shortcuts;                    // the keys of the commands, as changed in Preferences
    /// A command's keys as a menu shows them, "Cmd+D"; empty if it has none.
    std::string keysOf(const std::string& commandId) const;
    /// A row of a menu for a command, with its name, its keys and whether it may be done now:
    /// what a panel's right-click menu is made of. Returns true if it was chosen (and has been done).
    bool menuItem(const std::string& commandId, const char* label = nullptr);

    // ---- asked for by a command; the panel that does it clears it
    bool renameRequested = false;           // rename what is selected: the Hierarchy, or the Project window while it has the keys
    bool hierarchySearchRequested = false;  // put the keyboard in the Hierarchy's search
    bool addComponentRequested = false;     // open the Inspector's Add Component list
    std::string assetRequest;               // for the Project window: "import", "rename", "delete", "reveal", "open"
    std::string entityClipboard;            // what Copy took, as scene text (the system's clipboard has it too)
    bool projectIsActive = false;           // files were chosen after anything of the scene: Edit's Rename, Delete... are about them
};

inline Scene& sceneOf(EditorContext& editor) { return editor.engine.scene; }

// ---------------------------------------------------------------- plugins

class Plugin {
public:
    virtual ~Plugin() = default;
    virtual const char* name() const = 0;
    /// Register panels, menu items, components here.
    virtual void onLoad(EditorContext& editor) = 0;
    /// Before the library is closed. What was registered with `this` as owner is removed for you.
    virtual void onUnload(EditorContext& editor) {}
    /// Every frame, before the panels draw.
    virtual void onUpdate(EditorContext& editor, float dt) {}
};

}  // namespace molten::editor

/// Put this once in a plugin, after its class.
#define MOLTEN_EDITOR_PLUGIN(Type)                                                                          \
    extern "C" __attribute__((visibility("default"))) int moltenEditorPluginAPIVersion() { return MOLTEN_EDITOR_API_VERSION; } \
    extern "C" __attribute__((visibility("default"))) molten::editor::Plugin* moltenCreateEditorPlugin() { return new Type(); }
