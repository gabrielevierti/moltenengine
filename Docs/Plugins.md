# Editor plugins

A plugin is a dynamic library the editor loads at start. It is written in C++ against one header, `Editor/API/EditorAPI.hpp`: the same one the editor's own panels use. A plugin can add:

| | With | Example |
|---|---|---|
| A dockable window | `registry.addPanel` | a level statistics window |
| Menu items | `registry.addMenuItem` | "Tools/Drop Selection to Ground" |
| A component in the inspector | `registry.addComponent<T>` | fields of your own component, with Add and Remove |
| Entries of the Entity menu (and the + of the Hierarchy) | `registry.addCreateEntry` | "Gameplay/Spawn Point" |
| Drawings over the 3D view | `registry.addViewportOverlay` | labels, paths, zones |
| Work every frame | `Plugin::onUpdate` | shortcuts, watching files |

`Plugins/SceneTools/SceneTools.cpp` does four of these in a hundred lines. This page walks through it. It is an example to learn from, not part of the editor: it is built only when asked for (`cmake -DMOLTEN_BUILD_EXAMPLE_PLUGIN=ON`), and then adds a Tools menu.

## The smallest plugin

```cpp
#include "API/EditorAPI.hpp"

using namespace molten::editor;

struct Hello : Plugin {
    const char* name() const override { return "Hello"; }

    void onLoad(EditorContext& editor) override {
        editor.registry.addMenuItem(this, "Tools/Say Hello", [](EditorContext& e) { e.notify("Hello!"); });
    }
};

MOLTEN_EDITOR_PLUGIN(Hello)
```

`MOLTEN_EDITOR_PLUGIN` defines the two functions the editor looks for in the library. The first argument of every `add…` function is the **owner**: pass `this`. When the plugin is unloaded, everything it registered is removed for it.

## Building it

In Molten's own `CMakeLists.txt`:

```cmake
molten_add_editor_plugin(Hello Plugins/Hello/Hello.cpp)
```

This builds `build/Plugins/Hello.dylib`. The editor loads every `.dylib` in the `Plugins` folder next to its executable, and every one in the `Plugins` folder of the project that is open (those are unloaded again when another project opens). Help > Plugins lists what is loaded; the console says why a library was refused.

A plugin does not contain a copy of the engine or of ImGui. It is linked against the editor executable itself and finds everything there, so there is one scene, one ImGui context, one set of component tables. Two consequences:

- **Build plugins with the same Molten they will run in.** `MOLTEN_EDITOR_API_VERSION` catches a plugin built for another version of the interface; it cannot catch one built against different engine headers.
- For now a plugin is built from Molten's build tree, with the function above. (A project's *game* code is different: the editor builds it itself.)
- Do not call Metal directly from a plugin yet (`texture->handle()->width()` and the like): go through the engine's own classes.

## What a plugin sees

`EditorContext` is the editor:

```cpp
editor.engine           // the whole engine: scene, renderer, assets, physics, input
editor.scene            // the open scene as the editor handles it (SceneDocument)
editor.project          // name, folders, the first scene
editor.assets           // the asset database of the project
editor.gizmo            // the current tool, snapping steps
editor.viewport         // where the 3D view is, its camera matrices
editor.notify("...")    // status bar and console
```

`SceneDocument` is where editing goes through:

```cpp
for (molten::Entity entity : editor.scene.selection()) { ... }
editor.scene.select(entity);
editor.scene.create(Primitive::Cube);
editor.scene.setWorldMatrix(entity, matrix);    // whatever its parent
editor.scene.reparent(child, parent);           // keeps its place in the world
editor.scene.markChanged();                     // "I edited the scene": makes it one step for Undo
```

**Undo needs nothing else.** Change components directly, then call `markChanged()`. The editor records the step when the interaction ends (the mouse button is released, the text field is left).

## A panel

```cpp
class StatisticsPanel final : public Panel {
public:
    StatisticsPanel() { title = "Scene Statistics"; }

    void draw(EditorContext& editor) override {
        ImGui::Text("Entities  %zu", editor.scene.entities().size());
    }
};

// in onLoad:
editor.registry.addPanel(this, std::make_unique<StatisticsPanel>());
```

`draw` is called between `ImGui::Begin` and `ImGui::End`, with any Dear ImGui call available. The panel appears in the Window menu under its title and docks like the built-in ones. `Editor/Panels/Widgets.hpp` has the rows the built-in panels are made of (`ui::dragVec3`, `ui::color`, `ui::combo`, …) if you want yours to look the same.

## A component in the inspector

```cpp
struct Spinner {
    float turnsPerSecond = 0.5f;
};

// in onLoad:
editor.registry.addComponent<Spinner>(this, "Spinner", [](EditorContext&, molten::Entity, Spinner& spinner) {
    return ui::dragFloat("Turns per Second", spinner.turnsPerSecond, 0.01f);
});
```

Return `true` when a field changed. "Spinner" then appears under Add Component, and its section has Remove.

One limit to know about: **a component registered this way is not saved in scenes.** This is the editor's way to give a component a hand-made inspector. A component that belongs to the game should be declared with `MOLTEN_COMPONENT` instead (in the project's `Source` folder, see [GameCode.md](GameCode.md)): then it is shown, saved, undone and run without any editor code.

## Drawing over the 3D view

```cpp
editor.registry.addViewportOverlay(this, [](EditorContext& editor, const ViewportInfo& viewport) {
    ImVec2 at;
    if (viewport.worldToScreen(molten::Vec3{0, 2, 0}, at)) {
        viewport.drawList->AddText(at, IM_COL32_WHITE, "Spawn");
    }
});
```

For lines in the world itself (they are hidden by what is in front of them), use the engine's debug lines: `editor.engine.debug().line(a, b, color)`.
