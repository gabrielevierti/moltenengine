// Editor.hpp
// The Molten editor, from the inside. Plugins include API/EditorAPI.hpp instead.
//
//   EditorApp.cpp        the frame: menus, the dock space, shortcuts, projects, dialogs
//   Viewport.cpp         the 3D view: camera, picking, gizmos with snapping, overlays
//   SceneDocument.cpp    the open scene: file, selection, undo, play mode
//   Panels/*.cpp         the windows: hierarchy, inspector, assets, materials, rendering, console, visual script
//   GraphEditor.cpp      the drawing board of a visual script: nodes, pins, wires
//   GameBuild.cpp        compiling a project's code, making a script file, exporting an application
//   PluginHost.cpp       the registry everything is registered in, and loading plugin libraries
//   ImGuiLayer.cpp       Dear ImGui on the engine's window and renderer
//   Platform.mm          macOS dialogs, the Trash, the Finder
#pragma once

#include "API/EditorAPI.hpp"
#include "ImGuiLayer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace molten::editor {

// ---- Panels/*.cpp: each adds what it provides to the registry, with a null owner.
void registerHierarchyPanel(EditorContext& editor);
void registerInspectorPanel(EditorContext& editor);
void registerBuiltinComponents(EditorContext& editor);
void registerAssetBrowserPanel(EditorContext& editor);
void registerAssetViewerPanel(EditorContext& editor);
/// The viewer of the asset chosen in the Explorer: drawn by the Explorer, at its right.
void drawAssetViewer(EditorContext& editor);
void registerRenderingPanel(EditorContext& editor);
void registerConsolePanel(EditorContext& editor);
void registerProfilerPanel(EditorContext& editor);
void registerAnimationPanel(EditorContext& editor);
void registerAnimatorPanel(EditorContext& editor);
void registerSourceControlPanel(EditorContext& editor);
void registerSceneWeightPanel(EditorContext& editor);
/// The Profiler's Weight page: what each thing of the scene costs in memory.
void drawSceneWeightPage(EditorContext& editor);
void registerProjectPanel(EditorContext& editor);
/// The project's game settings (tags, layers, gravity...) made so and written to its Settings/Game.settings.
void saveGameSettings(EditorContext& editor, const GameSettings& settings);
/// The Input page of Project Settings: the actions and what moves them.
void drawInputSettings(EditorContext& editor);
void registerVisualScriptPanel(EditorContext& editor);
void registerManualPanel(EditorContext& editor);
/// Opens Help > Molten Manual on a document, by its file's name without the ending: "MakingGames".
void showManual(EditorContext& editor, const std::string& page);
void registerCreateEntries(EditorContext& editor);

/// The entries of the Create menu as menu items. New entities become children of `parent`.
void drawCreateMenu(EditorContext& editor, Entity parent, bool withEmpty = true);
/// What the Explorer's "+ New" makes, as menu items: made in the folder it shows.
void drawAssetCreateMenu(EditorContext& editor);
/// Every component by category, as menu items: chosen, it is added to all the selected entities that lack it.
void drawAddComponentMenu(EditorContext& editor);
/// A point added to the chosen spline (or after the chosen point of one).
void addSplinePoint(EditorContext& editor);
/// True for a piece of the game's interface (a component of the UI category): on the screen, not in the world.
bool isInterfacePiece(Scene& scene, Entity entity);
/// Makes an entity as one entry of the Create menu does, under `parent` if it is one, and chooses it.
Entity createFromEntry(EditorContext& editor, const CreateEntry& entry, Entity parent);

/// A visual script (a .graph among the assets) as a C++ script in Assets/Scripts, which keeps
/// the graph so that it can go back; and back again, from C++ made that way. Each returns the
/// path of the file made (relative to Assets), or an empty text after saying why it could not.
std::string convertGraphToCpp(EditorContext& editor, const std::string& graphPath);
std::string convertCppToGraph(EditorContext& editor, const std::string& cppPath);

/// A small picture of a texture with a menu to choose, drop or clear it. True if it changed.
bool textureSlot(EditorContext& editor, const char* name, std::shared_ptr<Texture>& slot, TextureFormat format);

/// The settings of a particle system.
/// Its modules, as in Unity. True if one was changed.
bool drawEmitterProperties(EditorContext& editor, ParticleEmitter& emitter, bool offersFiles);

/// The scene that is open, as its own camera sees it, as a picture (Panels/AssetPreviews.cpp):
/// computed on the processor, for a window that shows a scene without drawing it.
std::shared_ptr<Texture> makeScenePicture(EditorContext& editor, int width, int height);

/// Materials (Panels/MaterialEditor.cpp). The material on a ball, rendered: under a sun and a
/// sky, with its pictures. `setMaterialPreviewAssets` says where pictures come from and are made.
void setMaterialPreviewAssets(Assets* assets);
void forgetMaterialPreviews();
/// Forgets every thumbnail, so that each is made again from its file: after a file's import settings change.
void forgetAssetPreviews();
void drawMaterialPreview(ImDrawList* drawList, ImVec2 center, float radius, const Material& material);
/// The properties of a material, shader first. Returns true if one was changed.
bool drawMaterialProperties(EditorContext& editor, Material& material);
/// The "Material" row of a mesh: shows which, lets another be chosen, dropped in, or made.
bool drawMaterialSlot(EditorContext& editor, std::shared_ptr<Material>& slot);
/// Writes a material asset back to its file.
bool saveMaterialAsset(EditorContext& editor, const Material& material);
/// Makes a new `.material` file in a folder of Assets, as a copy of `from` if given.
std::shared_ptr<Material> createMaterialAsset(EditorContext& editor, const std::string& folder, const std::string& name, const Material* from);
std::string materialDisplayName(EditorContext& editor, const std::shared_ptr<Material>& material);

/// The names Assets::mesh() accepts: the built-in shapes, then the models of the project.
std::vector<std::string> meshChoices(EditorContext& editor);

// ---- Viewport.cpp
/// Runs the 3D view in the given rectangle of the window (in points).
/// A picture of an asset made from what is in its file (Panels/AssetPreviews.cpp). Returns
/// false if there is none for this kind, or the file gave nothing to draw.
bool drawAssetPreview(EditorContext& editor, ImDrawList* drawList, ImVec2 min, ImVec2 max, const std::string& path, AssetType type, uintmax_t size,
                      ImU32 accent);
/// A few words about it: "340 triangles", "1.2 s", "12 entities". Empty if there is nothing to say.
std::string assetPreviewNote(EditorContext& editor, const std::string& path, AssetType type, uintmax_t size);

void drawSceneView(EditorContext& editor);
/// `takesCanvas`: the engine's canvas (the game's interface) is drawn into this view.
void drawGameView(EditorContext& editor, bool takesCanvas);
/// With no project open there is nothing to show in either.
void hideSceneAndGameViews(EditorContext& editor);
/// What drawing the picture in front of the user took last frame: the Scene view's when it
/// is on screen, the Game view's otherwise. The two side by side each have their own.
RenderStats shownRenderStats(EditorContext& editor);
/// The numbers of a picture, each kind of draw on its own line: for a tooltip or an overlay.
std::vector<std::string> renderStatsLines(const RenderStats& stats);

// ---- PluginHost.cpp
class PluginHost {
public:
    struct Loaded {
        std::string path, name;
        void* library = nullptr;
        Plugin* plugin = nullptr;
        bool isFromProject = false;
    };

    /// Loads every .dylib in a folder. Returns how many were loaded.
    int loadDirectory(EditorContext& editor, const std::string& directory, bool isFromProject);
    /// Unloads what came from the project's Plugins folder: called when another project opens.
    void unloadProjectPlugins(EditorContext& editor);
    void unloadAll(EditorContext& editor);
    void update(EditorContext& editor, float dt);
    const std::vector<Loaded>& plugins() const { return plugins_; }

private:
    bool load(EditorContext& editor, const std::string& path, bool isFromProject);
    void unload(EditorContext& editor, Loaded& loaded);
    std::vector<Loaded> plugins_;
};

}  // namespace molten::editor
