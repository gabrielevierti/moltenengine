// Editor.hpp
// The Molten editor, from the inside. Plugins include API/EditorAPI.hpp instead.
//
//   EditorApp.cpp        the frame: menus, the dock space, shortcuts, projects, dialogs
//   Viewport.cpp         the 3D view: camera, picking, gizmos with snapping, overlays
//   SceneDocument.cpp    the open scene: file, selection, undo, play mode
//   Panels/*.cpp         the windows: hierarchy, inspector, assets, materials, rendering, console
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
void registerMaterialsPanel(EditorContext& editor);
void registerRenderingPanel(EditorContext& editor);
void registerConsolePanel(EditorContext& editor);
void registerProjectPanel(EditorContext& editor);
void registerCreateEntries(EditorContext& editor);

/// The entries of the Create menu as menu items. New entities become children of `parent`.
void drawCreateMenu(EditorContext& editor, Entity parent);

/// A small picture of a texture with a menu to choose, drop or clear it. True if it changed.
bool textureSlot(EditorContext& editor, const char* name, std::shared_ptr<Texture>& slot, TextureFormat format);

/// The names Assets::mesh() accepts: the built-in shapes, then the models of the project.
std::vector<std::string> meshChoices(EditorContext& editor);

// ---- Viewport.cpp
/// Runs the 3D view in the given rectangle of the window (in points).
void drawViewport(EditorContext& editor, ImVec2 position, ImVec2 size);

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
