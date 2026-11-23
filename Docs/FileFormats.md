# File formats

Everything the editor saves is a text file you can read, diff and merge: a project is a folder of
these and the assets they name. This page says what each file holds and what Molten promises
about it, so that a game made today still opens in the editors that come after.

## The promise

Every file starts with its format and a version, on a line of its own:

```
molten-scene 1
```

Then come statements, one per line: a keyword and its values, separated by spaces. Text with
spaces is written in double quotes (`"Main Camera"`), with `\"`, `\\` and `\n` inside. Numbers are
written in the fewest digits that read back as the same number. An empty line, and a line that
starts with `#`, mean nothing. Indentation only shows what a line belongs to: the lines after
`entity` belong to that entity until the next `entity`.

From 1.3 on, these rules hold for every format below:

1. **A reader skips what it does not know.** A line from a newer engine (a setting that did not
   exist yet, a component type, a kind of timeline track) is passed over and the rest of the file
   is read. Opened in an older editor, a newer scene loses only what is new.
2. **The version goes up only when skipping would be wrong:** when an old engine would read the
   file as something it is not, rather than read less of it. Adding a statement, or a value at the
   end of a statement, never changes the version.
3. **Every engine reads every version before its own.** An old file is brought up to date as it
   is read, and written back in the current version the next time it is saved.
4. **A file of a newer version is refused, never guessed at.** The console says which version the
   file is and which this engine reads up to, and the file is left as it is on disk.
   A project file (`.molten`) is the one exception: it holds only names, so any version is
   read, and an older editor can still open a newer project and tell you what it cannot read.
5. **Values have fixed units:** meters, seconds, kilograms, degrees in the editor and radians in
   code, linear colors from 0 to 1, rotations as quaternions `x y z w`.

The versions this engine reads and writes are in one table, `kFileFormats` in
`Engine/Core/TextFile.hpp`, and `Tests/FormatTests.cpp` holds the engine to the rules above.

## The formats

| Header | Files | What it is |
| --- | --- | --- |
| `molten-project 1` | `Name.molten` | The project: its name, first scene, version and identity |
| `molten-scene 1` | `.scene`, `.prefab` | A scene, or the part of one a prefab keeps |
| `molten-variant 1` | `.prefab` | A prefab made from another, with what it changes |
| `molten-material 1` | `.material` | A material: a shader and its values |
| `molten-meta 1` | `asset.meta` | An asset's id and how it is imported |
| `molten-graph 1` | `.graph`, `.shadergraph` | A visual script, or a shader made of nodes |
| `molten-animator 1` | `.animator` | An Animator Controller: parameters, states, transitions |
| `molten-timeline 1` | `.timeline` | A timeline: tracks of keys and marks in time |
| `molten-input 1` | `Assets/Input.actions` | The game's input actions |
| `molten-quality 3` | `Settings/Quality.settings` | The game's quality levels, and the one it starts at |
| `molten-game-settings 1` | `Settings/Game.settings` | Gravity, physics steps, tags and layers |
| `molten-save 1` | the player's save files | What a game saved with SaveGame |
| `molten-lighting 1` | `.lighting`, beside its scene | A scene's baked light probes |
| `molten-recent 1` | the editor's own settings | The projects opened lately |

The files that are not Molten's own keep their usual formats: models (`.gltf`, `.glb`, `.fbx`,
`.obj`), pictures (`.png`, `.jpg`, `.hdr`), sounds (`.wav`, `.ogg`, `.mp3`), fonts (`.ttf`), C++ for
a game's code, and Metal for a `.shader` (see "Shaders" in [MakingGames.md](MakingGames.md)).

## Project

```
molten-project 1
name "Driving"
startScene "Scenes/Main.scene"
version "0.1.0"
identifier "com.example.driving"
icon "Branding/Icon.png"
splash 0
```

`identifier` and `icon` are what a shipped game is signed and shown with; `splash 0` leaves out
the Molten splash. Paths are relative to the project's `Assets` folder, always with `/`.

## Scene and prefab

```
molten-scene 1
settings
  skyZenith 0.07 0.16 0.42
  shadows 1 110
  fog 0.86 0.62 0.44 0.0016
material "Paint"
  shader lit
  baseColor 0.8 0.1 0.1 1
entity "Car"
  transform -27.9 0.35 -2.3  0 -0.707107 0 0.707107  1 1 1
  parent 12
  mesh "Models/car.glb" -1 1 1 1 1 1 1
  materialAsset "Materials/Asphalt.material"
  collider box 0.8 0.6 1.5  0 0.6 0  0
  body dynamic 820 0.1 0.8 0.05 1 1 0.05 1
  component "Motor"
    field maxBoost "1.1"
    field isPlayers "1"
```

- `settings` holds the look of the scene: sky, sun and fog, shadows, ray tracing, weather, sea,
  post effects, resolution. A prefab has none. Each line is one setting, and a scene without a line
  has that setting's default.
- `material` blocks are materials the scene keeps inside itself; most scenes name `.material`
  files with `materialAsset` instead.
- `entity` starts an entity, named. `transform` is position, rotation (`x y z w`) and scale;
  `parent` is the number of the entity it hangs under, counted from 0 in the file's order.
  The built-in components each have a line (`camera`, `light`, `mesh`, `collider`, `body`,
  `particles`, `tag`, `inactive`, ...), and `prefab "Prefabs/Cart.prefab"` with `override` lines
  marks a copy of a prefab and what it changes.
- `component "Type"` is a component a game or the Kit declared with `MOLTEN_COMPONENT`, and each
  `field` its value as text, as `Field::valueText` writes it. A component type that no loaded code
  declares is kept as its lines and written back unchanged, so a scene opened without its game's
  code loses nothing.

## Prefab variant

```
molten-variant 1
base "Prefabs/Cart.prefab"
name "Red Cart"
set "Cart/Body" "Motor" "maxBoost" "1.4"
```

A variant names the prefab it starts from, and each `set` changes one value of it: the entity (by
its path in the prefab), the component, the field and the new value. Everything else follows the
base, so a change to the base reaches every variant.

## Material

```
molten-material 1
shader lit
baseColor 0.045 0.047 0.052 1
metallic 0
roughness 0.82
emissive 0 0 0
baseColorTexture "Textures/Asphalt.png"
```

`shader` is `lit` or `unlit`. A material drawn with a shader of the project says so with
`surfaceShader "Shaders/Lava.shadergraph"` (or a `.shader`), and that shader's own properties
follow as `shaderValue name x y z w`.

## Asset meta

```
molten-meta 1
guid eda35951219ee06035f88d4ff42aeb9b
import scale 0.01
import up z
```

Every asset has one next to it. The `guid` is how scenes and materials keep finding an asset
that was moved or renamed; keep the `.meta` files in version control with the assets.

## Graph (visual scripts and shader graphs)

```
molten-graph 1
node 1 "OnUpdate" 40 60
node 2 "Rotate" 280 60
  value "degrees" "45"
link 1 "then" 2 "in"
```

Nodes have a number, a type, and a place on the canvas; `value` lines are what is typed into
their inputs, and `link` joins an output of one node to an input of another. A node of a type this
engine does not know is kept as written. A `.shadergraph` is the same format with the `SG.` nodes.

## Animator Controller

```
molten-animator 1
parameter float speed 0
parameter trigger jump
state Idle
  motion Idle
  place 60 60
state Move blend speed
  motion Walk 1.6
  motion Run 4.6
  event Audio/step.wav 0.03
start Idle
transition Idle Move when speed > 0.3 blend 0.15
transition Idle Jump when jump blend 0.08
```

## Timeline

```
molten-timeline 1
length 12
track camera
  at 0 "Wide"
  at 4.5 "Close"
track move "Car"
  key 0  0 0 0  0 0 0 1
  key 3  12 0 4  0 0.38 0 0.92
track sound
  at 2 "Audio/horn.wav" 0.8
```

Each `track` has a kind and, for most kinds, the entity it acts on. `key` lines are a time, a
position and a rotation; `at` lines are a time and what happens then.

## Input actions

```
molten-input 1
axis "Steer" negative A Left positive D Right gamepad LeftX
button "Jump" keys Space gamepad A
button "Fire" keys LeftControl mouse Left
```

## Quality and game settings

```
molten-quality 3
start "High"
level "Low"
  renderScale 0.85
  antiAliasing 2
  rayTraced 0
  cloudSteps 24
level "High"
  renderScale 1
  rayTraced 1
  shadowRays 8
```

The game's quality levels, in the order its menu shows them, and the one it starts at. Each level
lists every value it has (Rendering/Quality.hpp); one it leaves out is that of Molten's level of
the same name, or High's. Version 2 held one level of values: it is read as that level of the four.

```
molten-game-settings 1
gravity 0 -9.81 0
stepsPerSecond 60
tag Player
layer 3 Water
apart 3 4
```

`apart a b` is a pair of layers whose colliders do not meet; any pair not written does.

## Baked lighting

```
molten-lighting 1
grid 11 5 11  -10 1 -10  2 1
p 0.21 0.18 0.15 0.62  0.2 0.17 0.14 0.6  0 0 0 1  0.3 0.26 0.2 0  0.19 0.16 0.13 0.58  0.2 0.17 0.14 0.6
```

A `grid` is the probes' count along x, y and z, where the first one is, the meters between them,
and how far inside its edge it fades out. Then one `p` line a probe, x first, then y, then z, each
with six sides (+x, -x, +y, -y, +z, -z): the light bounced to it from that side (red, green, blue)
and the share of the sky it sees that way. Written by Lighting > Bake Light Probes; the scene
names it with `bakedLighting` in its settings.

## Save game

```
molten-save 1
number "level" 3
text "name" "Ann"
vector "checkpoint" 12 0 -4
```

A save of a newer version reads as empty rather than wrong, and the console says why.
