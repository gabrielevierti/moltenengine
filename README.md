# Molten Metal Engine

**A game engine for Apple silicon, built on Metal.** Molten for short. Written in C++, open source, with an editor.

Molten targets one family of machines (Mac, iPad, iPhone: Apple GPUs and nothing else) so that it can use what they offer without a layer of compromise in between: tile-based deferred rendering, memoryless render targets, unified memory, MetalFX, hardware ray tracing.

> **Status: 0.1, early.** What is described below is what exists today. Much of what makes a complete engine is still ahead; [ROADMAP.md](ROADMAP.md) lists it in order, and says what "done" means for each step. Molten grew out of the engine of the [LearnMetal](https://gabrielevierti.github.io/learnmetal/) tutorials.

## Build and run

You need a Mac with Apple silicon, **macOS 13+**, **Xcode 15+** (or its command line tools) and **CMake 3.24+** (`brew install cmake`).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/molten                      # the editor
./build/molten-player MyGame        # runs a project without the editor
```

Or double-click `build.command` in the Finder: it builds everything, runs the tests and writes what happened to `build.log`.

The first configuration downloads the libraries Molten uses: GLFW, metal-cpp, stb, miniaudio, Dear ImGui and ImGuizmo.

## The editor

`./build/molten` opens the list of projects; make a new one or open one you have. `./build/molten path/to/MyGame` opens a project directly.

**Start from a game.** A new project can be empty or a copy of one of five small complete games (roll a ball, space shooter, brick breaker, a car and an aeroplane simulated from their parts), each made only of components that come with the engine: a player that moves, a camera that follows, pickups, health and damage, shooting, spawning, a score on screen, winning and starting over. [Docs/MakingGames.md](Docs/MakingGames.md) is the tour.

**Projects.** A project is a folder: a `.molten` file, `Assets/` for everything the game ships, `Plugins/` for editor plugins, and two folders (`Settings/`, `Library/`) that stay out of version control. New projects come with a `.gitignore` that says so.

**Hierarchy.** Every entity of the scene as a tree. Drag one onto another to parent it (it keeps its place in the world), drag it to the empty space to free it. Cmd-click selects several, Shift-click a range; arrows walk the rows; an eye hides a mesh; F2 renames; Shift+Cmd+F puts the selection where the view's camera is; right-click creates children, duplicates, deletes. Search shows matches as a flat list.

**The 3D view.** Move, rotate, scale, or all three at once (W, E, R, T), along the world's axes or the entity's own (X). **Snapping** to steps you choose: meters, degrees (with 5°, 15°, 45°, 90° one click away), scale; hold Cmd while dragging to do the opposite of the Snap button. Rotation shows its angle while you turn. Several selected entities move together. A cube in the corner shows where the camera faces: drag it to orbit, click a face to look along an axis. Right button and W A S D fly (the wheel changes speed), Alt-drag orbits, middle button pans, F frames the selection.

**Inspector.** The components of the selected entity, each in a section that folds, with Add Component and Remove. Rotation is in degrees. Select an asset instead and it shows the asset: a texture's picture and size, a scene's Open button.

**Assets.** The project's `Assets` folder: folders on the left, tiles on the right, textures shown as pictures. Import copies files in; drag files between folders; rename in place; delete goes to the Trash, after asking. Drag a model into the 3D view and it lands where the pointer is on the ground; drag a texture onto a material's slot. Every asset gets an identifier (a `.meta` file beside it) that survives renaming and moving. Changes made outside the editor (the Finder, git) show up within a second.

**Materials.** A material is an asset with a shader (lit or unlit) and its properties, edited in the inspector; drag it onto an object. **Lighting** has the sun, the sky, fog, wind, shadows and ambient occlusion; **Rendering** has bloom, the camera's film and quality; **Input** has the game's actions and the keys that trigger them.

**Undo for everything** (Cmd+Z, Shift+Cmd+Z), **Play** in place (Cmd+P; Stop puts the scene back exactly) with pause and single steps; **Scene and Game are separate windows**, side by side if you like, so the running game can be watched, inspected and changed from the editor's own camera, a console that collects what the engine and plugins say, and a window layout you arrange by dragging tabs, remembered between sessions.

**Scripts in C++.** A project has a `Source` folder; every `.cpp` in it is the game. Game > New Script makes one and opens it; saving it rebuilds. A component is a plain struct followed by one line that declares it; its fields appear in the inspector, are saved in scenes, and its `update` runs when you press Play. Cmd+B rebuilds the code and puts it under the open scene without restarting anything. [Docs/GameCode.md](Docs/GameCode.md).

**Visual scripts.** The same behaviors can be drawn instead of written: nodes for what happens, wires for order and values, on a drawing board in the editor. About forty nodes come with the engine (events, movement, physics, input, math, logic, variables); a script can be rewired while the game plays. [Docs/VisualScripting.md](Docs/VisualScripting.md).

**Prefabs and effects.** Right-click an entity and save it as a prefab; drag prefabs into the scene, or let the game spawn them. Create > Effects has fire, smoke, sparks, explosions, rain and snow, and the scene has a wind that carries them.

**Export.** Game > Export for macOS makes an application from the project: the player, the assets, the built code.

**Plugins.** Panels, menu items, component editors, entries of the Create menu and drawings over the 3D view can all be added by a dynamic library, in C++, with the same interface the editor's own panels are written against. [Docs/Plugins.md](Docs/Plugins.md) has a walk through the example in `Plugins/SceneTools`.

## The engine

**Rendering**

- Physically based materials (glTF metallic-roughness) in HDR, with 4× multisampling
- One sun with cascaded shadow maps, plus up to 63 point and spot lights
- **Ray-traced sun shadows** on every GPU that traces rays (A13 and M1 onward): exact at any distance, sharp at contact and soft away from it. A setting of the scene; shadow maps remain the fallback
- Image-based lighting generated from the sky: irradiance, prefiltered reflections, BRDF lookup table
- Normal maps; cut-out materials (with holes in their shadows too); transparent materials sorted back to front
- Automatic instancing, frustum culling, particles, debug lines
- Screen-space ambient occlusion, bloom, tone mapping, fog
- A sky with drifting clouds that also lights the scene
- Lens distortion, color fringes, film grain
- Shaders compiled at startup and reloaded when you save a `.metal` file

**2D**

- A splash screen when the editor and exported games start (a project can turn its own off)
- A canvas for interfaces and 2D drawing: rectangles, rounded rectangles, lines, circles, rings, arcs, polygons, images, and text that stays sharp at any size (signed distance field fonts)
- An immediate-mode Gui on top of it, for a game's own menus

**Everything else**

- Entities and components, with parent-child transforms
- Fixed-step physics: spheres, boxes and capsules; static, kinematic and dynamic bodies; triggers, raycasts, collision layers
- Keyboard, mouse and gamepad behind named actions (`"jump"`, `"moveX"`); touches on iPhone and iPad
- Components declared in C++ with their fields (`MOLTEN_COMPONENT`): edited, saved and run by the engine
- Sound: a software mixer with volume, pitch, pan and looping
- Scenes as text files that read well in a diff
- `--capture file.png` runs a program without a window and saves a frame: this is how whole programs are tested unattended

[ARCHITECTURE.md](ARCHITECTURE.md) explains how the parts fit together and why they are built the way they are.

## iPhone and iPad

The engine runs on iOS and iPadOS 16 and later: a UIKit view with a Metal layer, the display's own refresh as the loop, fingers as touches (the first one also plays the mouse). The same player that runs an exported game on a Mac is the iOS application; because iOS does not let an application load code of its own making, the project's `Source` folder is compiled into it.

```sh
cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DMOLTEN_IOS_PROJECT=/path/to/MyGame -DMOLTEN_IOS_TEAM=YOURTEAMID
open build-ios/MoltenMetalEngine.xcodeproj
```

Choose your device and press Run. [Docs/iOS.md](Docs/iOS.md) has the details and what is still missing there (controllers, the editor's Build menu for iOS).

## A game without the editor

A game can also be an ordinary C++ program that links the engine, like this:

```cpp
#include "MoltenMetal.hpp"

struct MyGame : molten::Game {
    void start(molten::Engine& engine) override { /* build the scene, or molten::loadSceneFile(...) */ }
    void update(molten::Engine& engine, float dt) override { /* every frame */ }
};

int main(int argc, char** argv) {
    molten::Engine engine(molten::EngineConfig::fromArguments(argc, argv));
    MyGame game;
    return engine.run(game);
}
```

Complete games to open in the editor are the example projects in `MoltenProjects.zip`: see [Docs/MakingGames.md](Docs/MakingGames.md).

## Tests

```sh
ctest --test-dir build -L unit      # everything that does not need a GPU
ctest --test-dir build -L gpu       # whole programs on the GPU, without a window
```

## Layout

```
Engine/      the engine: Core, Scene, Rendering, Physics, Audio, Input, Assets, Project, UI, Platform
Editor/      the editor; Editor/API is what plugins include
Player/      the program an exported game is, on the Mac and on iOS
Plugins/     editor plugins that come with Molten (the example)
Shaders/     Metal shaders, and the header that C++ and the shaders share
Assets/      the engine's own assets: the default font, sample textures and a scene
Tests/       unit tests
Templates/   project folders put here are offered as starting points (empty: the example games are a separate download, MoltenProjects.zip)
Tools/       scripts that generated the assets
```

## License

GPL-3.0. See [LICENSE](LICENSE). The libraries Molten downloads keep their own licenses (MIT, zlib, public domain, Apache 2.0 for metal-cpp).
