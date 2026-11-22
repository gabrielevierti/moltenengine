# The game's code

A project's `Source` folder holds the game's C++. The editor compiles every `.cpp` file in it into one library, loads it, and from then on the game's components behave like the engine's own: they are in Add Component, their fields are in the inspector, scenes save them, Undo covers them, and their code runs when the game plays.

A new project has an empty `Assets/Scripts` folder: **Assets > Create > C++ Script...** (or **+ New > Script** in the Explorer) makes the first one, with one component in it. This page explains every part.

## A component

```cpp
#include "MoltenMetal.hpp"

using namespace molten;

struct Spinner {
    float turnsPerSecond = 0.25f;
    Vec3 axis{0, 1, 0};

    void update(Engine& engine, Entity self, float dt) {
        if (Transform* transform = engine.scene.get<Transform>(self)) {
            Quat turn = axisAngle(safeNormalize(axis), turnsPerSecond * 2.0f * kPi * dt);
            transform->rotation = simd::normalize(turn * transform->rotation);
        }
    }
};

MOLTEN_COMPONENT(Spinner, MOLTEN_FIELD(turnsPerSecond).range(-4, 4), MOLTEN_FIELD(axis))
```

- **The struct** is plain data with default values. Those defaults are what Add Component gives.
- **`MOLTEN_COMPONENT`** declares it: the type's name, then the fields to show and save. A field you do not list still exists; it is just private to the code (a timer, a cached entity).
- **The functions** are optional. The engine looks for these three and calls the ones that exist, only while the game plays:

| | When |
|---|---|
| `void start(Engine&, Entity self)` | once per component, when play begins or the component appears |
| `void update(Engine&, Entity self, float dt)` | every frame |
| `void fixedUpdate(Engine&, Entity self, float dt)` | every physics step (60 per second): forces and velocities go here |

`self` is the entity the component is on. Everything else is reached through `engine`: `engine.scene`, `engine.input`, `engine.physics`, `engine.assets()`, `engine.audio()`.

## Fields

| C++ type | In the inspector |
|---|---|
| `float` | a number to drag; a slider with `.range(low, high)` |
| `int` | a whole number; limited with `.range(low, high)` |
| `bool` | a checkbox |
| `Vec2`, `Vec3`, `Vec4` | two, three or four numbers |
| `Vec3`, `Vec4` with `.color()` | a color picker |
| `std::string` | a text field |

`.help("...")` adds a tooltip. The label is made from the name: `turnsPerSecond` is shown as "Turns Per Second".

References to other entities and to assets are not field types yet; until they are, find entities by name in `start` (`engine.scene.find("Door")`) and keep the result in a field you do not list.

## A new script

**Assets > Create > C++ Script...** asks for a name, makes `Assets/Scripts/<Name>.cpp` with a component of that name in it, opens the file in the program your Mac uses for C++ (Xcode, unless you chose another) and compiles. The scripts are in the Explorer like any other asset; Assets > Show Scripts Folder opens their folder in the Finder.

Scripts that are drawn instead of written are in [VisualScripting.md](VisualScripting.md).

## Building

**Saving a file is enough.** The editor watches the scripts and compiles them when one changes. **Assets > Compile Scripts**, or Cmd+B, compiles on demand; opening a project compiles too. The **Scripts** light at the right of the tool bar is green when the code is compiled and loaded, red when it did not compile: a click on it then opens the Console on the errors.

What the editor does: it writes the open scene to text, empties it, loads the new library, and reads the scene back. Components come back by name with the fields the new code has: a field you added starts at its default, one you removed is let go. The scene's undo history survives. If the code does not compile, the compiler's messages go to the console and the code that was loaded before stays.

The editor is stopped while the compiler runs: a second or two for a small game.

Two things to know:

- **A component whose code is gone is not lost.** If a scene has a `Hinge` and no loaded code declares `Hinge` (you renamed it, or the build failed), the inspector says so and the scene keeps the lines as they are. Bring the type back and the component is back.
- The library is compiled against the engine's headers in the Molten source tree the editor was built from, with `xcrun clang++`. It needs Xcode's command line tools, which building Molten needed too.

## Running without the editor

```sh
./build/molten-player ~/Games/MyGame                        # the project's first scene
./build/molten-player ~/Games/MyGame Scenes/Level2.scene    # another one
```

**File > Build Settings...** (Shift+Cmd+B) exports the game: for a Mac it asks for a folder and makes `MyGame.app` there: the player under the game's name, the engine's shaders and assets, the project's assets, and the built code. It is signed for the Mac it was made on. Giving it to other people needs a Developer ID signature and notarization with your own Apple account, which the editor does not do yet.

The first scene of the game is the project's First Scene (Edit > Project Settings); the first scene you save becomes it.

## What is not there yet

- Entity and asset references as fields.
- Events between components (collision callbacks reach `engine.physics` only).
- Building and running on iOS from the editor: see [iOS.md](iOS.md) for how it is done today.
- Stepping through game code with a debugger attached to the editor works (`lldb -p`), but nothing in the editor helps with it.
