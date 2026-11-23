# Making a game

This is the shortest way from an empty editor to something you can play: what comes with the engine for the things nearly every game needs, and how the pieces fit. None of it needs code. All of it can be replaced by code, a piece at a time, when a game outgrows it ([GameCode.md](GameCode.md), [VisualScripting.md](VisualScripting.md)).

## Start from a game

A new project starts empty, or from one of the six small games the editor's New Project page
offers (**Car Game**, **Plane Game**, **Boat Game**, **Third Person Game**, **Shooter Game**, **Online Game**:
see [Templates/README.md](../Templates/README.md)). Each is made only of what the editor offers:
components from Add Component, and the game's own scripts in its `Assets/Scripts` (the boat, the
car's driver, the plane's pilot, the camera that chases them), so everything in it can be changed
in the Inspector, and its code read and changed like any script.

The larger example game is **Driving**: a Fiat 500 on a small circuit, to race four rivals or to lap against the clock.

| | |
| --- | --- |
| **What it is** | A 566 m circuit with curbs, walls, a pit lane in front of the garages and lamps, in evening light. A race against four rivals the computer drives (each its own colour), from a grid of five and five start lights, over 3, 5 or 10 laps, with the places live and the results at the end; or a time trial alone. Lap times and a best lap kept between games, laps that do not count with four wheels on the grass, tyres that wear and a pit stop that changes them, sun, clouds or rain, a title screen, a pause menu and settings, a dashboard (speed, revs, gear, boost), and controls for a touch screen. |
| **What it shows** | A car simulated from its parts (springs and dampers, tires that grip and slide, a turbocharged four-cylinder behind a gearbox, stability control that allows a drift, brakes shared by the load on each wheel), a glTF model used in parts with a material each, the game's own scripts in `Assets/Scripts`, an interface laid out in the Game view. |
| **Keys** | W A S D or the arrows to drive, Space for the handbrake, B for ABS, L for the lamps, R to be put back on the wheels, T for the touch controls, Esc to pause; in the pit lane, 1 to 4 choose the next tyres. On a phone: ← and → on the left steer; GAS, BRAKE and HB on the right, II at the top left pauses. |

Its scripts, each a small piece of the game to read and change: `DriveControls` (keys and touch to the car, the pit limiter, putting it back on its wheels), `Motor` (the turbocharged engine and its sounds), `LapTimer` (laps, gates, track limits, the best lap kept), `Tyres` (compounds, wear, the pit stop), `Weather` (sky, mist, rain, a wet road), `GameMenu` (title, pause, settings, results), `RacerAI` (a rival: the racing line, corner speeds from each corner's radius and braking for it, passing, rain, getting unstuck), `RaceDirector` (the grid, the lights, the places, the finish), and `CarLights`, `Exhaust`, `TyreSmoke`, `SkidMarks`, `Follow`.

It is not part of the engine: it is a separate download, `MoltenProjects.zip`, to unzip anywhere and open as a project (a new project in the editor starts empty). It is written by `Tools/make_templates.py` (the car by `Tools/import_fiat500.py`), and the unit tests (`Tests/TemplateTests.cpp`) drive it without a screen: a full stop from speed, and three laps by an autopilot that has to stay on the road. The car is "Fiat 500 x Outlaw" by Subin, under CC-BY-4.0: see the project's `CREDITS.txt`.

### Showcase

The second project is a plaza of everything the engine does, to drive around in the same car. The interface names the station you are at; eight stars are hidden around the middle, each turned and picked up by a visual script (`Scripts/Star.graph`).

| Station | What is there |
| --- | --- |
| **The middle** | A chrome ball in a fountain: ray traced reflections of the whole plaza, and particles that bounce off the basin. |
| **Materials** | Plastic and gold from smooth to rough, a mirror, glass, a glowing ball, a textured car paint with its normal map, and two of the project's own shaders. |
| **Shaders** | A lake of lava and a hologram on a column, each a `.shader` file in `Assets/Shaders`: change one, save, and it changes. |
| **Lights** | A dark hall: red, green and blue spot lights crossing on a statue, braziers that flicker, neon strips that bloom. |
| **Effects** | Particle systems on pedestals, in the wind. |
| **Physics** | A pyramid of crates, a curve of dominoes that falls by itself, five balls hung in a row, a wrecking ball, two gates on hinges, bowling pins, a chute that never runs out of balls. The car shoves what it hits. |
| **Jumps** | A table top, whoops and a kicker: models with mesh colliders, solid exactly as they are drawn. |
| **Water** | A basin of ocean waves, with crates and buoys floating on them. |
| **Liquids** | Two tanks of particle liquid, water and honey, poured as you watch. |

The scene's settings turn everything on: ray traced shadows, reflections and bounced light, clouds with depth, fog with light shafts, wind. It is the project to open for screenshots, and the heaviest to run.

## The components that come with the engine

Under **Add Component**, as in Unity, are the parts a game is built of; what the game does with
them is its own code (a script, or a visual script):

| Category | Components |
| --- | --- |
| Mesh | Mesh Renderer |
| Rendering | Camera, Light, Decal, Terrain, Foliage, LOD Group |
| Physics | Box, Sphere, Capsule and Mesh Collider, Rigidbody, Joint, Character Controller, Buoyancy, Rope, Cloth, Ragdoll |
| Animation | Animator, Two Bone IK |
| Audio | Audio Source, Reverb Zone |
| Effects | Particle System, Liquid, Explosion |
| Player | Character Mover, Orbit Camera, First Person Look |
| Gameplay | Collectible, Health, Damage, Goal, Spin, Shuttle, Lifetime, When / Then |
| Navigation | Navigation Area, Nav Agent |
| Paths | Spline, Spline Follower, Spline Road |
| Vehicles | Vehicle Body, Wheel, Suspension, Deformable Tire, Bumper, Motor, Wing, Hinge, Propeller |
| UI | Panel, Image, Text, Button, Toggle, Slider, Text Field, Bar, Arc, Layout, Stick, Loading Screen |
| Network | Network Session, Networked |
| Miscellaneous | Saveable |

The Player and Gameplay pieces are what **Entity > Characters** and **Entity > Gameplay** put
together (a player that walks with its camera, a coin, a trigger zone, a goal). Anything more
particular is the game's own scripts: what drives a boat, a car or a plane, a camera that chases
it, a gun. The templates come with theirs in `Assets/Scripts` (the Boat Game's `Boat` and
`Follow`, the Car Game's `DriveControls`, the Plane Game's `FlightControls`, the Shooter Game's
gun, plates, hunters and supplies), to read, copy and change. A script takes the place of an
engine component of the same name, and is offered under Add Component with the other scripts.
The engine's components that once did such things (PlayerMover, Follow, Mover, Shooter, Spawner,
Flicker, Weapon, Boat and the vehicles' controls...) still work in the scenes that have them, and
their code is in `Engine/Kit` to copy from, but Add Component does not offer them.

### Shared values

A score, the lives left, how many keys were found: numbers with a name that anything can read and change. `Collectible`, `Health` and `Breakable` change them, `HudText` shows them, `Goal` waits for them; visual scripts have **Add Value**, **Set Value** and **Get Value**; code has `world.value("score")`, `world.setValue`, `world.addValue`. Loading the next scene keeps them. Starting the scene over clears them.

**Preferences** are numbers kept from one time the game is played to the next: settings, records. `world.preference("volume", 2)` reads one (2 if it was never set), `world.setPreference("volume", 1)` keeps one at once. A game keeps them in its own folder (Library/Application Support/<the game's name>, inside the application's own on a phone); while it is tried in the editor, in the project's Library folder. The Driving example keeps its best lap and its settings there. `world.soundVolume` (0 to 1) turns every sound of the game up or down: a volume setting, a pause.

### Names

Components find each other by name: `Follow` follows the entity called `Player`, `Damage` with `onlyTo` set to `Enemy` hurts everything whose name begins with `Enemy`. Spawned copies keep the prefab's name, so a rule written for `Enemy` holds for all of them.

## Materials

A material is an asset: a `.material` file in the project, made with **+ New > Material** in the assets (or from a mesh's Material menu). The first thing it says is its **shader**, which program draws the surface: *Lit* (color, metal, roughness, lit by the scene) or *Unlit* (its own color and nothing else). Select a material and the inspector shows its properties; select an entity and the material of its mesh is at the bottom of the inspector. Either way it is the same material: change it and everything that uses it changes, in every scene.

Give a mesh a material by dragging it from the assets onto the object in the view (it shows on whatever the pointer is over, and stays when you let go), onto its row in the Hierarchy, or into the mesh's **Material**, or by choosing it from the list there. A scene made before materials were assets keeps its own; **Save This One as an Asset**, in the same menu, turns one into a file.

## Input

The **Input** window lists the game's actions: what the player can do (`moveX`, `moveZ`, `jump`, `fire`, `pause` to begin with) and which keys, mouse buttons and controller parts do it. It is saved in `Assets/Input.actions` and ships with the game. Components and code ask for actions and never for keys:

```cpp
float x = world.input.axis("moveX");
if (world.input.wasPressed("jump")) { ... }
```

`PlayerMover` moves by `moveX` and `moveZ` and jumps on `jump`: rebind them in the window and it follows. The bar under each action shows what it reads right now.

The standard list is only what nearly every game uses. A game that flies adds the actions `FlightControls` reads with **+ Flying Actions**: `yaw` (Q E), `throttle` (Shift and Ctrl, or Z X), `throttleLever`, `flaps` (F) and `brake` (B). A Button on the screen can hold an action that is in no list (the Driving example's buttons hold `steerLeft`, `steerRight`, `gas` and `brake`).

## Playing, and changing things while it plays

The scene has two windows. **Scene** is the editor's view: fly around, select, move, change. **Game** is what the player will see, through the scene's own camera, with the game's interface. They start as two tabs; drag one beside the other to see both at once. **Maximize** in the Game view's bar (or Shift+Space over it) makes it fill the editor, to play in a larger picture, until **Restore**; with **Maximize On Play** on, Run does that by itself and Stop puts it back.

**Play** (Cmd+P, or the button at the top) runs the scene and brings Game to the front; **Pause** (Shift+Cmd+P) freezes it and **Step** (Alt+Cmd+P) lets one frame through. The game keeps running in both windows: in Scene you can watch it from anywhere, select what is moving and change it with the same tools as when editing. Click the Game window to give the game the keyboard (it shows an orange line around it). What you change while playing is undone by Stop: it is for trying things, and for finding out why something happens.

## Vehicles

Two components simulate a machine from its parts. They move their own body, find the ground with rays against the scene's colliders, and are driven in the tests without a screen (`Tests/VehicleTests.cpp`).

Vehicles are built from parts, each a component, so that the same parts make a car, an aeroplane, a trike or a glider.

| Part | Goes on | What it is |
|---|---|---|
| `VehicleBody` | the root object | The thing with mass. It runs every part below it in the hierarchy. |
| `Wheel` | a child, where the wheel stands | Grip from one budget shared between pulling, braking and cornering. Can steer, be driven, brake. A child named `Spin` turns with it. |
| `Suspension` | the wheel's object | A spring and a damper, tied to the wheel across from it by an anti-roll bar. |
| `DeformableTire` | the wheel's object | Air in the tire: it flattens under load, more when soft. |
| `Motor` | the root or a child | An engine (`i4`, `v8`, `diesel`, `electric`) behind a gearbox that shifts by itself, turning every driven wheel. Four recordings per engine, blended by revs. |
| `Wing` | a child, placed and tilted as the wing is | Lift and drag from the air that meets it. Stand it on its side for a fin. Nothing says "stall": past its angle the lift goes. |
| `Hinge` | a child of a wing (or anything) | A part that turns with a control: aileron, elevator, rudder, flap. Under a wing it changes what the wing does. |
| `Propeller` | a child, pointing where it pulls | The engine's power at speed, less at rest. A child named `Blades` spins. |
| `Bumper` | a child | A point that must not go through things: a nose, a wingtip. |
| `DriveControls` | the root | The driver's keys. Sets `speed`, `rpm`, `gear`. |
| `FlightControls` | the root | The pilot's keys, with an assist that keeps the wings level and the nose out of a stall. Sets `airspeed`, `altitude`, `climb`, `throttle`, `flaps`, `stall`. |

Controls write what is wanted into `VehicleBody::controls` (throttle, brake, steer, pitch, roll, yaw, flaps); parts read it. Your own code can write there instead: `body->setControls(...)`. `kit::makeCar` and `kit::makeAirplane` build one of each from code. The Driving example is these parts with a model on them.

Both read their numbers from the inspector (mass, spring rates, wing area, power), so a van, a go-kart, a glider or a jet trainer is a matter of changing them.

## Prefabs

A prefab is a piece of scene in a file: an entity, what is under it, and the materials it uses. Right-click an entity in the hierarchy and choose **Save as Prefab**; it goes to `Assets/Prefabs`. Drag a prefab from the asset browser into the 3D view to place it. `Shooter`, `Spawner` and the `effect` fields of other components spawn prefabs while the game runs; so does the **Spawn** node, and in code `world.spawn("Prefabs/Enemy.prefab", position)`.

A prefab is read when it is used: change the file and the next one spawned is the new one. An entity already placed in a scene is a copy and does not follow.

## Effects

**Entity > Effects > Particle System** makes an entity with a particle emitter, which the Inspector shapes into fire, smoke, sparks, rain or anything else (see Particle systems below). Save one as a prefab and it can be the `effect` of a `Health` or a `Collectible`.

A particle can be drawn as a streak along the way it goes instead of a round dot: **Streak** in the inspector, in seconds of its movement (rain falls in streaks of about 0.05).

**Wind** is a setting of the scene (the Rendering panel): a direction and speed, and how gusty it is. Each emitter says how much it is carried (`Wind Influence`): 0 for sparks, 1 for smoke.

From code:

```cpp
effects::create(world.scene, effects::Preset::Fire, Vec3{0, 0, 0});
```

## Scenes: starting over and moving on

`Health` with `restartsSceneOnDeath`, and `RestartWhenBelow`, start the scene over. `Goal` with `nextScene` goes to another. From a visual script: **Reload Scene**, **Load Scene**. From code: `world.reloadScene()`, `world.loadScene("Scenes/Level2.scene")`. Both happen at the end of the frame, so they are safe to ask for from anywhere.

In the editor, Play runs the scene as it is open, saved or not, and starting over returns to exactly that. Stop puts back what you were editing.

## The editor's windows

**Starting the editor.** `build/Molten Metal Engine.app` is the editor with its icon: double-click it,
or keep it in the Dock (`./build/molten` from the Terminal is the same editor). It opens on the engine's
logo, then the projects opened lately on the left: choose one and it is shown on the right, with the
question whether to open it (Return opens it, a double-click opens it at once); **New Project** starts
one with an empty scene. Opening a project, a scene, building the game's code and exporting each show
what is being done, step by step, until it is done. Opening a project also keeps a log under the bar:
each step done and how long it took (reading the project, looking through its assets, loading its
plugins, building its code, reading the scene and the files it uses, counted as they are read), and
every warning or error said on the way, so a script that did not build is seen before the editor opens.

**Icons.** Every window has its icon on its tab; where several share a narrow place (the Inspector with
Lighting, Environment, Rendering and Post Processing), the tabs behind show their icon alone and say
their name when the pointer rests on them. Components, assets, the Scene view's tools and the quality
groups have theirs; in the Scene view, lights (a sun, a bulb, a flashlight for a spot), cameras,
particle systems and sounds, which have no mesh to click, are shown by a round badge with their icon,
a light's in its color, ringed in orange while selected. The icons are Lucide's (ISC license), in
`Assets/Fonts/Icons.ttf`; `Tools/make_icon_font.py` adds one.

**Moving and deleting.** In the **Explorer**, Cmd and a click chooses several files, Shift a range,
Cmd+A all; Delete (or Backspace) moves them to the Trash after asking; Cmd+X and Cmd+V, or Move To in
the right-click menu, or dragging onto a folder, move them, and the scenes, prefabs, materials and
animations that name a moved file are changed to name it where it is now (the open scene too). In the
**Hierarchy**, a row dropped on the line between two others goes there, under the same parent (between
two children of a parent: under it, in that place); dropped on a row, it goes under it; with several
chosen, they all go, in their order. Delete or Cmd+Backspace deletes what is chosen in the scene.
A material file deleted (here or in the Finder) is taken off what wore it: it wears the scene's plain material, and the scene
no longer names the file.

**Project** shows the assets with pictures made from the files: a model as its shape, a material as a lit ball, a sound as its wave, a scene or a prefab as a plan from above, a visual script as its boxes, code as its first lines. **Add Component** in the inspector is sorted as Unity's is (Mesh, Rendering, Physics, Audio, Effects, UI, Vehicles, Gameplay, Scripts) and can be searched; each component's menu has Reset, Copy Values, Paste Values and Remove.

**Inspector.** The Inspector shows what was chosen last: an entity in the view or the Hierarchy, or an asset (or a folder) in the Explorer. Each component has an icon by its name (a cube for a mesh, a bulb for a light, a speaker for a sound, braces for a script), so they are told apart at a glance; titles inside one (a material's Surface, Textures) are lines, not more folding headers. With nothing chosen, the Inspector shows the scene at a glance, what was chosen lately, and tiles to make the usual things. Choose an asset and the Inspector shows it: a model to turn, a picture, a sound to play and trim, a material, a text or script with its words colored by kind and its lines numbered; its **x** goes back to the chosen entity. A right click anywhere in the Explorer offers what can be made there. An asset shown in the Inspector can still be dragged onto an entity in the Hierarchy or the view.

**Menus.** On a Mac they are in the Mac's own menu bar, at the top of the screen, as every program's are, and only there, in full screen too. **Molten Engine v1.3** (the program's own menu, named with its version) has About, **Preferences** (Cmd+,: autosave, the editor's look, and every shortcut, searchable and changed by pressing the new keys) and Quit. **File** has the scenes (new, open, save), the projects (new, open, recent, close, Save Project as Template) and **Build Settings** (Shift+Cmd+B), which makes the game into an application. **Edit** has undo, cut, copy and paste, the selection, Find and **Search Everything** (Cmd+K, which finds any command by name, with the entities and the assets), Play, and **Project Settings**: what the game is (its name, version, icon and first scene), Quality, Input, Physics, Tags and Layers. **Assets** makes and imports files; **Entity** makes things and arranges them (Unity's GameObject); **Component** adds one to what is selected; **Window** opens the windows (the layouts are at the top right), and has **Full Screen** (as the window's green button does): the whole screen but the Mac's menu bar, which stays with the menus in it, and no title bar; the Dock hides until the pointer goes down to it; **Help** has the manuals. Each command is in one place, with its keys beside it. The Scene view's **Gizmos** button chooses what it shows besides the scene, the outline of every collider among it. **Simulate** (in the Scene view and the Game view) lets what moves by itself move while you edit: particles, water's waves, plants swaying in the wind, the clouds. It is off by default, and then all of it stands still except while such a thing is selected (a particle system, a water or swaying material), so that it can be tuned while it is seen moving; the button is lit whenever it moves. The **Profiler** has three pages: the frame's time, what the scene holds (Scene), and what it weighs (Memory). The **Input** page of Project Settings shows a controller and a keyboard, lit as they are pressed (and whether a controller is connected), with what the chosen action is bound to in orange; **Bind by Pressing** takes the next key or button.

**Text fields name nothing.** Wherever a field names something (a sound, a scene, a prefab, a timeline, an entity, a tag, a bone, an animation, a state or parameter of the Animator, an input action, a key, a shared value, a message), the Inspector and the Visual Script boxes offer a list of what it can actually be, with a search box when the list is long; files can also be dropped on it from the Explorer. A name that is no longer there shows in orange as missing. Values and messages, which are new names a game invents, have **A new name** at the end of their list. Only words people read (a button's label, a text on screen, a win message) are typed.

**Layouts.** The default is Unity's: Hierarchy at the left, Inspector at the right, Scene and Game as tabs in the middle, and Explorer, Console and Visual Script as tabs along the bottom. Play, Pause and Step are in the middle of the tool bar, as in Unity and Unreal, with the project and its scene at their left (and a disk that is lit while the scene has changes to save) and, at their right, the scripts' light and the layout menu, which has four arrangements (Default, 2 by 3, Tall and Wide). Arrange the windows as you like and choose Save Layout: it joins the list, on this Mac, for every project. **Export Layout...** writes it to a file to keep or pass on; **Import Layout...** takes one back. The Scene and Game views never get smaller than 360 by 240.

**Where the time goes.** Window > Profiler shows the last five seconds, a bar per frame for the CPU and a line for the GPU, and below them the parts of a frame (events, fixed update, physics, update and each kind of component in it, particles, interface, each pass the renderer prepares, and the time spent waiting for the GPU or the display) with their times: the average of the last second, or one frame's when its bar is clicked. When the CPU's bar is short and the GPU's line is high, the GPU is the limit; much time "Waiting for the GPU" says the same. In a running game, F1 shows the slowest parts of the last frame. Your own code can add parts: `Profiler::Scope timed(world.profiler, "Pathfinding");` times the rest of its block.

**Saving the game.** Preferences hold settings and records; a game in progress goes in a `SaveGame`: numbers, words and positions by name, kept in a slot (a file in the game's Saves folder).

```cpp
SaveGame save;
save.setNumber("level", 3);
save.setVector("player", position);
world.saveGame(save);                        // the slot "Save"; world.saveGame(save, "Slot 2") for another
if (std::optional<SaveGame> saved = world.loadGame()) level = int(saved->number("level", 1));
```

`world.hasSavedGame()`, `world.deleteSavedGame()` and `world.savedGames()` (the slots there are) go with them.

**Phones that heat up.** `world.deviceHeat()` says how hot the device is (0 cool to 3 very hot) and `world.isSavingBattery()` whether Low Power Mode is on. When it is hot the renderer already draws the scene smaller until it cools (Project Settings > Quality > Resolution); a game can do more, such as a lighter quality level. **Dynamic Resolution** (in the same place) draws the scene smaller whenever the GPU falls behind the frame rate it is given, and **MetalFX Upscaling** makes a smaller picture the screen's size with sharp edges.

**Shipping.** In the Build window, **Send to App Store Connect** archives an iPhone and iPad game for release and uploads it: it appears under TestFlight, and from there goes to review. The app must exist in App Store Connect with the project's identifier, and Xcode must be signed in to your account; the build number is the date and time, so each upload is new. For the Mac, **For other Macs** signs the exported game with your Developer ID certificate and has Apple notarize it, so it opens on any Mac; once, in Terminal: `xcrun notarytool store-credentials molten --apple-id <your Apple ID> --team-id <your team>`. **Development** marks an export as a development build, where F1 shows the statistics and the profiler.

**Shaders ahead of time.** With the Metal compiler installed (Xcode > Settings > Components > Metal Toolchain), `build.command` compiles the engine's shaders first: a mistake in them is reported there, and the engine starts without compiling them. Without it, nothing changes: the engine compiles them as it starts.

**What the Scene view shows.** The scene as it is, through the editor's own camera: what a game's
camera does to the picture (drops on the glass coming out of the water, lens distortion, color
fringes, grain, interference, vignette, dirt on the lens, depth of field, motion blur) is only in
the Game view. The light is the same in both (exposure, tone mapping, bloom, color grading), so
what is lit is judged rightly. The arrow beside Snap opens its steps and the list of what is drawn over the scene: the grid (1 m squares near the ground, 10 m and 100 m from higher up, so it is there from far away too), the outline of what is selected, the colliders of everything, lights, cameras, particle effects, sounds, and what plugins draw. Each kind can be switched off, and its icons are then not clicked either. **Sound** at the right of the Game view's bar silences the game while you work, without changing its own settings. Pieces of the interface that are switched off (a menu the game opens later) are not chosen or moved by clicks in the Game view; choose them in the Hierarchy and switch them on to lay them out.

**Explorer.** Right-click anywhere in it (the folder tree, a folder, the empty space, inside any folder) for New with every kind of asset, Import and Reveal. The size slider goes down to a list. **Console** is Unity's: errors, warnings, info and debug messages, each kind shown or hidden by its button with its count; Collapse folds repeats into one row with a count; Clear on Play; a search; click a row for the whole message below, to copy.

In the Scene view, several selected objects move, turn and scale together. **Center** in the toolbar puts the handles at the middle of them all, which they turn and scale around; **Pivot** puts them on the last one chosen.

In the scene view a camera is drawn as the pyramid of what it sees, a directional light as its rays, a spot light as its cone. A camera's scale changes nothing about its picture.

## Cutscenes

There are two ways, as in Unity. A cutscene can be an animation like any other: of an entity that
holds what moves in it (the camera, the characters, the doors), made in **Window > Animation** with
Record, its sounds and the moments the game waits for as events (a sound plays; any other name is a
message the scripts hear). Its Animator plays it when told: `animator->play("Intro")`, or Play
Animation in a visual script.

Or it can be a **timeline** (`.timeline`), made in **Window > Timeline** (**New** there, or **+ New >
Timeline** in the Explorer): tracks along time, each for one thing. **Move** takes an entity
through its keys (move it in the view, then **Key** on its track: where it is in the world now, at
the playhead); **Camera** cuts from one camera to another; **Sound** plays sounds; **Value** sets a
shared value (what a script or a menu waits for); **Show** shows or hides an entity; **Trigger** sets
a trigger of an Animator; **Clip** starts an animation. Double-click a track for a key or a mark,
drag them along it, right-click one to change it or remove it. **Preview** shows the scene as the
timeline has it at the playhead, while the time is dragged or played, and puts everything back
when it stops. A **Timeline Player** (Add Component > Animation) plays it in the game: from the
start, looping or not, at a speed, setting its **Done Value** when it ends; **Edit in Timeline** on
it opens its file.

## Morning, noon and night

The sky follows the sun by itself (Environment > Sky > Automatic): turn the sun, its rotation, and
the day goes by, gold at sunset, then night with its moon and stars. A day that passes while the
game runs is the game's own code: a script that turns the sun a little every frame.

## Hills and valleys: Terrain

**Entity > 3D Object > Terrain** makes flat ground, 200 by 200 meters. With it chosen, brushes
appear in the lower left of the Scene view: **Raise**, **Lower**, **Smooth**, **Flatten**. Paint on
the ground with the left button; Shift does the other way round (Raise lowers); Alt and the left
button still turn the view. **Size** is the brush's radius, **Strength** how fast it works.
Flatten brings the ground to the height where the stroke began: a level place for a building.
**Undo Stroke** takes back the last strokes. **Move it** puts the brushes away, for the gizmo.

Its heights are a file of their own (`Terrains/Terrain.terrain`), written when a stroke ends
and named in the Terrain's `heights`. `resolution` is how many squares along each side (128:
squares of a meter and a half on 200 meters); changing it reads the same hills at the new one.
It is solid: cars drive on it, characters walk on it. Give it a material in its Mesh Renderer;
`uvTiling` is how many meters one repeat of the material's picture covers.

From code: `terrain->heightAt(Vec2{x, z})`, `normalAt`, and `sculpt(...)` to dig a crater.

## Grass, trees and rocks: Foliage

**Entity > 3D Object > Foliage** makes an entity whose Mesh Renderer's model (little cubes until
you give it yours: a tuft of grass, a tree, a rock) is painted on the ground. With it chosen,
brushes appear in the lower left of the Scene view: **Paint** puts copies on whatever is solid
under the brush (a Terrain, the ground, a roof), **Erase** (or Shift) takes them away. **Density**
is copies a square meter; `spacing` keeps them apart; each gets a turn and a size between
`smallest` and `largest`. They are drawn together, in few draw calls, and not beyond
`drawDistance` meters. Their places are a file (`Foliage/Grass.foliage`), written when a stroke
ends. For grass and a tree on the same hill, make two Foliage entities.


Foliage is copies of the entity's mesh (grass, stones, trees) painted on whatever is under them:
choose it, and paint in the Scene view with the brush that shows there (Paint, Erase, Size,
Density). Where the copies are kept is the editor's business: it is not shown.

## Curves: roads, rails, paths

**Entity > Paths > Spline** makes a curve; **Entity > Paths > Road** makes one that is closed,
with a road along it. A Spline's points are its children, named **Point 1**, **Point 2** and so
on: the curve goes through them in the order of those numbers, smoothly. Move them in the Scene
view like anything else; the curve is drawn there in blue (orange when it or one of its points
is chosen), with an arrow that says which way it goes.

**Edit > Add Spline Point** (or Cmd+K, "add point") puts a point after the one that is chosen,
halfway to the next, or at the end when the Spline itself is chosen, and numbers them again.

- **Spline**: `closed` joins the end to the start; `smoothness` is how many points are made
  between two of its points.
- **Spline Follower**, on anything: goes along the Spline whose entity is named in `spline`, at
  `speed` meters a second; at the end it goes round again, comes back, or stops (`ending` 0, 1, 2).
  `offset` moves it to a side of the curve, for a second lane.
- **Spline Road**, on the Spline's own entity: a strip `width` meters wide, laid again as soon as a
  point moves, with a collider to drive on. Give it a material in its Mesh Renderer.

In code, `findSpline(world, "Track")->path` gives `pointAt(meters)`, `directionAt(meters)`,
`length()` and `closestDistance(point)`: how far along the curve something is.


A spline becomes a road with **Make It a Road** under its Spline in the Inspector: a Road
component (width, banking, texture) and the mesh it makes are added to it.

## Files changed in other programs

The editor looks at the project's files once a second. A picture saved again in Photoshop or
Pixelmator, a model exported again from Blender, a material or a sound written by another
program is read again into what already uses it: the scene changes in front of you, without
opening it again, and the Console says which file was read. A file is read once it has been
left alone for a moment, so a program that writes it in several goes is not caught halfway.
Shaders were already read again as they change; code is built again by itself.

## What uses what

The Inspector says, for the asset chosen in the Explorer, what uses it and what it uses
("Used by 2, uses 1"): the scenes and prefabs that put a model in, the materials that paint a
picture on, the scripts that play a sound. Click one to go to it. With nothing chosen, **Find
Assets Nothing Uses** lists the pictures, models, sounds and materials no scene, prefab,
material or script names. Code that builds a name from pieces (`"engine_" + rpm + ".wav"`)
cannot be read, so look before deleting.

## Keeping every version: Source Control

**Window > Source Control** is the project in Git. A project not in it yet is put in it with one
button (the Library folder, which the editor makes again, is left out). Then it shows what has
changed since the last commit: click a file to see how, untick one to leave it for later, a right
click undoes its changes. Write what changed, press **Commit**. **History** lists the commits
before; **Pull** and **Push** bring and send them (to GitHub, for example). Git asks for a sign
in only once: if a push says so, push once from Terminal or set up a key, and it works from here.
Save the scene before committing it.

## Your keys, your colors

**Molten Engine > Preferences** (Cmd+,). Under **Shortcuts**, click an action and press its new keys:
Backspace leaves it without one, Esc keeps what it had. An action whose keys another one uses
says so in orange. **Reset** puts one back; **All as They Were** puts them all back. The menus
and Cmd+K show the keys as you set them. Under **Look**: Forge (the orange of hot iron), Ocean,
Moss, Midnight. Both are kept for the next time the editor opens.

## Finding anything: Cmd+K

**Cmd+K** (or **Edit > Search**) opens one field that finds by name everything the editor
has: what the menus do, the windows, everything that can be created, every entity of the scene and every asset
of the project. A few letters are enough, in order: `svsc` finds Save Scene, `cpl` Create Point
Light. Up and Down choose, Enter does it, Esc closes.

An entity is chosen and the view goes to it; the place it is in is shown at the right and is
searched too, so `rival 2 wheel` finds the wheels of that car and not the others. A scene opens,
a visual script opens to be edited, any other asset is shown in the Explorer. Items plugins add
to the menus are found as well.

## Reacting to what happens, in your own components

Besides `start`, `update` and `fixedUpdate`, a component may have any of these, and the engine calls the ones it finds:

```cpp
struct Coin {
    int points = 1;
    void onTriggerEnter(World& world, Entity self, Entity other);   // needs a trigger collider
    void onCollision(World& world, Entity self, Entity other);      // two solid things touched
    void draw(World& world, Entity self, Canvas& canvas);           // every frame, on top of the scene
};
```

Take a `World&` when the component only needs the scene, physics, input, time and shared values: it then also runs in tests, without a window. Take an `Engine&` when it needs the renderer, assets or sound.

## Game logic without code: When / Then

**When / Then** (kept for the scenes that have it; Add Component no longer offers it: a visual script does the same) does up to four things when something happens. All of
it is chosen from lists in the Inspector.

- **when**: *Something enters* its trigger (a Collider with Is Trigger on the same entity; `who`
  says which: a name, a tag, or anything), *A message* (named in `message`), *It starts*, *A value
  reaches* (`value` at least `atLeast`), *It is clicked* (a UI Button on it), *Every few seconds*.
- **then** (`action1` to `action4`, each with a `target` and an `amount`): Show, Hide, Switch,
  Destroy, Play sound, Start timeline, Load scene, Restart scene, Set value, Add to value, Send
  message, Spawn prefab, Move here (a checkpoint, a teleporter). The target is an entity's name or
  tag, `self`, `other` (who entered), a file, or a value's name.
- `seconds` waits before doing them; `once` does them only the first time.

A coin: a trigger, *Something enters* `Player`, then *Add to value* `coins` 1 and *Destroy* `self`.
A door that opens with three keys: *A value reaches* `keys` 3, then *Hide* `Door` and *Play sound*.

## Saving the game without writing what to save

Give **Saveable** (Add Component > Miscellaneous) to what should be kept: a chest, a door, the player,
each coin. `kit::saveState(world, "Slot 1")` keeps, for each of them, where it is (unless
`keepsPlace` is off), whether it is on, the values of its components (a door's state, an agent's
speed) and whether it was destroyed (a coin picked up stays picked up), with the shared values
and texts and the scene it is in. `kit::loadState(world, "Slot 1")` opens that scene again, at
the end of the frame, and puts all of it back. The same from When / Then (*Save game*, *Load
game*) and from visual scripts (**Save Game**, **Load Game**). What is not Saveable is as the
scene file has it. What was spawned while playing is not made again.

## Hundreds that come and go: pools and spawners

`world.take("Prefabs/Bullet.prefab", position)` is `spawn` that reuses: something put back with
`world.release(entity)` is switched on again where asked, still, and its components' `start()`
run again (set in start what must be fresh). Anything else given to `release` is destroyed.
A **Lifetime** puts back what it is on; When / Then's *Destroy* and *Spawn prefab* use the pools
too. Bullets, sparks and enemies by the hundred then cost nothing to make.

The **Spawner** makes them, around it (`radius`), every `interval` seconds; with `count` set, in
waves of that many, `waveSeconds` apart, `waves` of them (0 forever); never more than `maxAlive`
about at once; `aliveValue` keeps a shared value at how many are about. It sends the messages
`waveDone` (with the wave's number) and, once everything it made is gone, `wavesDone`.

## Big scenes, and scenes on top of scenes

`world.loadSceneInBackground("Scenes/Level 2.scene")` reads the scene and its models on another
thread while the game goes on; when all is read, the new scene takes the old one's place at the
end of a frame. Meanwhile the shared values `isLoading` (1) and `loading` (0 to 1) say how it
goes: a **Loading Screen** (Add Component > UI) shows what is under it only while a scene loads,
with a text of `{loading:percent}` or a bar of `loading`. `world.afterSceneLoads(...)` runs
something once the new scene is in place.

`world.addScene("Scenes/Room.scene")` adds another scene's entities to the one open, under an
entity named "Scene: Scenes/Room.scene" (the open scene's light and sky stay); `removeScene`
takes them away. `loadSceneInBackground(name, true)` adds one in the background. The same from
When / Then and from visual scripts.

## Telling each other: messages

A script that needs another to react does not need to know it: it tells what happened.

```cpp
world.send("doorOpened");                       // to everyone listening
world.send("damage", enemy, 25, "", self);      // to one entity: a number, a text, who sent it
```

A component hears with `void onMessage(World& world, Entity self, const Message& message)`
(`message.name`, `number`, `text`, `from`). Other code listens:
`int id = world.listen("lapDone", [&](const Message& m) { ... }, self);` until
`world.stopListening(id)` or until `self` is destroyed (`"*"` hears every message). In a visual
script: **Send Message** (to the entity named in `to`, or everyone) and the **On Message** event.
Messages are delivered at once, in order; one that sends itself again and again is stopped
after 32 times.

## Later: waiting

```cpp
world.after(2, [&] { door.open(); });                          // in two seconds
int ticking = world.every(0.5, [&] { blink(); });              // again and again (world.cancel(ticking))
world.when([&] { return world.value("keys") >= 3; }, [&] { gate.open(); });
```

A sequence reads better as one function that waits, a C++ coroutine:

```cpp
molten::Task intro(molten::World& world) {
    world.setValue("lights", 1);
    co_await world.seconds(2);
    world.playSound("Audio/horn.wav");
    co_await world.until([&] { return world.value("ready") > 0; });
    world.loadScene("Scenes/Race.scene");
}
...
world.start(intro(world), self);
```

All in game time: paused when the game is, slowed with `timeScale`. Given an owner (the last
argument), a task stops when its owner is destroyed; every task stops when the scene changes.
In a visual script: **Wait** (seconds) and **Wait Until** (a condition); the rest of the script
goes on meanwhile.

## Playing a game without a screen

`World` is the game without the machine around it. A test can load a scene, hold keys, advance time, and look at what happened:

```cpp
World world;
world.assetRoot = "MyGame/Assets";
world.loadSceneNow("Scenes/Main.scene");
world.input.handleKey(Key::D, true);
for (int i = 0; i < 120; i++) world.advance(1.0 / 60.0);
CHECK(world.value("score") == 1);
```

That is how the templates are tested, and a way to keep a game working as it grows.

## Fields that are a choice

A text field that can only be one of a few words is shown as a list in the Inspector:

    MOLTEN_FIELD(drive).choices({"front", "rear", "all"})

## Active, tags and layers

Every object has an active box (the eye in the Hierarchy), a tag and a layer; every component has its own enable box.
`world.findWithTag("Player")` finds an object by its tag. An inactive object is not drawn, simulated or updated, and neither are its children.

## Models and sounds of the templates

`Tools/make_models.py` builds the car, the aeroplane, the circuit, the hills and the trees as OBJ files; `Tools/make_sounds.py` builds the
engine recordings, four per engine at different speeds, which `Motor` crossfades as the revs change. A `Wheel` is on an object such as
`Wheel FL` (FR, RL, RR) with a child named `Spin` holding the tire and rim: the wheel steers and rides the suspension, `Spin` turns.

## The Game window's screen shape

The list at the top of the Game window chooses the shape of the screen the game is shown in: free, an aspect (16:9, 4:3, portrait...) or an
exact resolution. The camera, its outline in the Scene window and the game's interface all follow it.

## Joysticks and radios

A joystick that is not laid out like a game controller (a radio for model aircraft plugged in by USB, a flight stick, a wheel) is read by
its numbered axes. The Input window shows it by name with a bar for each axis: move a stick, see which bar moves, and set that number as
the **Joystick Axis** of an action. The standard actions start with a radio's usual order: axis 0 `moveX` (roll), 1 `moveZ` (pitch); with
the flying actions added, 2 `throttleLever`, 3 `yaw`. `throttleLever` is a throttle that says where it is; `FlightControls` uses it instead of the keys when a
joystick is there.

## A game's own scripts

A project's scripts are in `Assets/Scripts`, where the Explorer shows them: click one to read it in the Inspector, with the component it
declares and a button to add it to the selected object. The templates' own are there: `Follow.cpp`, `DriveControls.cpp`, `Motor.cpp`,
`FlightControls.cpp`. They are built when the project opens and whenever a file changes. (A `Source` folder beside Assets, where
scripts were before, is still built.) A script with the name of a component that comes with the engine takes its
place; if the code does not build, the engine's own is used, so the game still runs. `lateUpdate(World&, Entity, float)` runs after every
`update`, with everything where it will be drawn: cameras that follow belong there.

A visual script can be written out as C++, and C++ made that way can go back to boxes: see [To C++ and back](VisualScripting.md#to-c-and-back).

## Lighting, Environment, Rendering and Quality

**Lighting** is what lights the scene: the Sun, how much the sky lights it (**Sky Light**), how far
its **Shadows** reach, and the scene's lights in a list that opens when wanted. **Environment** is
the world of the game: the **Sky**, from a simulated atmosphere by default (sunlight scattered by
the air and the haze, as on the Earth: blue by day; gold, orange and red round a low sun with the
earth's shadow rising opposite; dark at night; all from the sun's height, so turn the sun);
**Fog** and its light shafts; the **Weather**: rain, storm and snow (which can settle), the
**Clouds** (with depth, at a height and a thickness of your choosing: a camera can fly in, through
and above them, and inside them the world fades into white) and the **Wind** (how hard, which way,
how gusty), which the weather raises; and the sea (**Water**). How all of it is drawn (shadow maps or ray tracing, how sharp, how many rays) is
not set here: it is the project's, in Project Settings > Quality.

The sky is **Automatic** by default: turn the sun and the sky follows it, day, sunset, then night
with the moon (across the sky from the sun, taking over once the sun's last light has gone) and
the stars; the fog is **lit by the sun** with it (its color is its color at noon: gold at sunset,
dark at night), and so are the clouds. **Fixed colors** keeps the sky as its colors say, whatever
the sun does. The clouds go with the scene's **Wind**: carried along it, drawn out along it,
slowly changing shape; and the fewer there are, the more they gather in groups, as fair-weather
clouds do. **Light Shafts** lets the air glow where the sun reaches it, so that what stands in
its way casts beams through it.

**Weather** is built in, none by default, and the wind rises with it (a shower freshens it, a
storm makes a gale of it: the clouds, the waves, the rain and what sways all go the same way).
**Rain** falls round the camera, carried by the air: a drop goes sideways as fast as the wind
where it falls, gusts and all, so the rain slants along the wind, the more the harder it blows
(snow, lighter, the same, drifting). Each drop is aimed, as it is born, at what it will
hit (any collider, the ground's mesh, a car's roof) or at the water, and ends there: a splash of
droplets thrown off the way the surface faces, a widening ring on water. What is out in the rain
gets wet. What soaks water up (asphalt, stone, wood: rough and not metal) darkens, and where it
lies flat the drops' small rings spread on the water standing on it. On what is smooth or metal
(paint, glass, plastic, steel) the water stands in beads; when what pulls them beats how they
cling, they run: down the surface by their weight, and along it in the air going by it (the wind,
and the thing's own speed: on a car that drives, the drops on the bonnet and the windscreen are
blown back and up over it, the faster the harder), each in fits, wandering a little, gathering
the beads in its way and leaving a wet trail. The drops are the thing's own: they stay on a car
that drives off, and follow its shape, not its texture; under a roof, a porch or inside a car
it stays dry (the scene round the camera is seen from above to know). As it rains the sky clouds
over, the sun dims and the air thickens. **Storm** adds lightning, a few kilometers away or
nearer: some strokes stay inside the clouds and light them from within, round where they are;
the others are a bolt down from the clouds to the ground, zigzagging, with branches, flashing a
few times in quick succession, the clouds glowing round it and the scene lit the more the nearer
it is (the moment of each stroke is in `World::weatherNow`, for a game's thunder). **Snow** falls
as rain does but slowly: flakes that sway and go with the wind. With **Snow Settles** it lies: what
faces up and is out in it turns white, unevenly, in drifts, more the longer it snows, and melts in
some minutes once it stops (how much lies now is in `World::weatherNow.snowCover`); without, it melts
a moment after it lands. A game that wants a weather of its own leaves all three at 0 and makes its
own from the Rain effect. The Hierarchy's funnel button shows only one kind of thing (lights, cameras...).

**Rendering** is what is drawn of the scene beyond its light: ambient occlusion (how the screen's
own looks), depth of field and motion blur. **Post Processing** is what is done to the picture
afterwards, as a camera does: exposure and tone mapping (none by default), bloom, color (contrast,
saturation, temperature, tint, grading, sharpen), and the lens and the film: vignette, distortion,
color fringes, dirt on the glass lit up by the bloom, grain.

**Project Settings > Quality** is how well the game draws, the same for every scene. A scene says
what it wants to look like (where its sun is, whether its clouds have depth, how far its shadows
should reach); a quality level says how much of that the machine is asked for, and all that a
scene has no say in, ray tracing first.

The levels are the game's own. A new project has four, **Low, Medium, High** and **Ultra**, and
every value of each can be changed: choose a level in the row at the top and its values are all
under it, in seven groups. **+** makes a new level from the one chosen ("Handheld", "Cinematic",
"Battery Saver"); the name field renames it; the arrows put it earlier or later in the list (the
game's menu shows them in that order); **Delete** takes it away; **Start Here** makes it the level
the game starts at (marked with a star). **Copy From...** on a group's row sets that group as
another level has it, or as Molten's own Low, Medium, High or Ultra. The editor draws at the level
chosen there, so a change shows at once.

| Group | What it decides |
|---|---|
| Resolution | How many pixels are drawn before the picture is made the screen's size (MetalFX keeps it sharp), and **dynamic resolution**: fewer pixels while the GPU cannot keep the frame rate given, down to the smallest share given, more again when it can; smaller when the device is hot |
| Anti-Aliasing | Multisampling: off, 2x or 4x |
| Shadows | Shadows on or off; how far the sun's shadows reach, as a share of what each scene asks (Window > Lighting > Shadows > Distance); the shadow map's size and how many slices; how many point and spot lights may cast shadows at once, and how sharp |
| Ray Tracing | On or off, and each part on its own: the sun's **shadows** (and how many rays along their edges), the **light around things** (and how many rays a pixel), **bounced light** (a red wall in the sun tints the floor), **reflections** (and the roughest surface that still mirrors). Off, or where the GPU cannot trace rays: shadow maps, the screen's ambient occlusion, reflections of the sky |
| Effects | The screen's ambient occlusion, bloom, the steps of light shafts, lights that light the air, motion blur, depth of field |
| Sky | Clouds with depth or flat, in how many steps, into how large a picture (a share of the screen's size, never under 35%, however small the scene is drawn) |
| Detail | How far the far levels of LOD groups and painted foliage last; whether the baked light probes are used |

The game's own settings menu lists the levels and lets the player choose: `world.qualityLevels()`
gives their names, `world.qualityLevel()` the one in use, `world.setQualityLevel("Low")` changes
it from the next frame. In a visual script, **Set Quality Level**, **Next Quality Level** (a button
that goes round them) and **Quality Level**. The choice is not kept by itself: keep it in a
SaveGame and set it again when the game starts.

Molten's **Low** is meant to still look good on the slowest machine the game runs on: a little
smaller and made sharp by MetalFX, 2x multisampling, shadow maps not quite as far as each scene
asks, the screen's ambient occlusion, light shafts and clouds with depth, walked coarsely but into a
picture large enough to stay soft; only ray tracing is off. **Ultra** is the best the engine draws
at 60 frames a second on a recent Mac (an M3): 4x multisampling, shadows half again as far as a
scene asks, at 4096, the most rays (twelve along a shadow's edge, three for the light around things,
mirrors on rougher things), clouds with depth in 64 steps, the finest light shafts, and detail that
lasts half as far again. Every level keeps its frame rate by drawing fewer pixels while the GPU
cannot keep up.

What makes it cheap: the clouds are walked for one pixel in four each frame, in turn, and the rest
kept from the frames before, moved to where they are now seen (a quarter of the cost, and smoother
than walking them all); the sea says its own light and the colors that get through it, and the
blending puts them over what is under it, so with multisampling it is shaded once a pixel rather
than once a sample; with the sun's shadow traced, the shadow maps are drawn only for the light
shafts, smaller and with two slices. **Profiler > Frame** shows what each pass of
the picture costs the GPU (the shadow maps, the clouds, the scene, the bloom...), in milliseconds:
where to look first when a frame is slow.

Nothing a scene sets is silently ignored: where the quality turns something off or takes it over
(no shadows at Low, the light around things found by rays instead of the screen's ambient
occlusion, flat clouds), the panel where the scene sets it says so in orange, with a button to
Project Settings > Quality; and the Lighting panel shows how far the shadows reach at the
project's quality. The quality is kept in Settings/Quality.settings and goes into the exported game.

**Ray tracing costs about a ray and a half a pixel**: one ray toward the sun from each pixel
that faces it, from the pixel's own place and the same in every frame (so shadows stand still
while the camera moves), and more only along the edges of shadows; the light around things (the
sky's, hidden in corners and under cars, and what is bounced off lit walls) is traced for one
pixel in four before the scene is drawn, gathered over frames and smoothed; and one ray from each
smooth pixel for what it mirrors. On a laptop at the size of its own screen that is still
millions of rays a frame: leave Dynamic Resolution on, and a GPU that cannot keep up draws fewer
pixels instead of fewer frames.

**Area lights** (Entity > Light > Area Light) are glowing rectangles: a window, a ceiling panel, a
screen, shining from their front, softly, as much as their size.

**Point and spot lights cast shadows** with **Casts Shadows** on (the Light in the Inspector): a
lamp in a room, a torch, a car's headlights, the bars of a window in a spot light's beam. A spot
light draws one shadow map, a point light six (one each way); as many lights cast them at once as
the project's quality allows (Project Settings > Quality > Shadows > Lights), the nearest and
brightest first, and only those whose light reaches what is seen. Their shadows fall in the air
too, with Volumetric on: beams through a window, the dark behind a pillar.

A **spot light's cookie**, as in Unity: a picture in Assets that the light shines through, spread
across its cone (white lets it through, black stops it, colors tint it): the frame of a window on
the floor, leaves, stained glass, a theatre's gobo. It turns with the light about its axis. Drop a
picture on the Light's **Cookie** slot in the Inspector.

**Volumetric** lights (point and spot, a box in the Inspector) light the air they pass through: a
lamp's glow in the mist, a car's headlights in the rain, a beam across a dusty room, the cookie's
pattern in it too. **In The Air** is how much (1 as fog would be lit); the fog makes it thicker
(Environment > Fog), and what is far is dimmed by the fog in front of it. It is seen in front of
what it lights and not through walls; with Casts Shadows on, what stands in its light shades the
air behind it.

## The picture: grading, exposure, focus, motion, sky, decals, wind

All in **Window > Rendering**, saved with the scene.

**Exposure that adapts.** Under Exposure and Color, **Adapts** makes the exposure follow how
bright the scene is, as eyes do: brighter in a tunnel, darker out in the sun, quickly when it
gets brighter and more slowly when it gets darker. **Compensation** (in stops) leans it one way;
**Darkest** and **Brightest** bound it.

**Color grading.** A **Grading Table** is a picture in Assets: 32 squares of 32 pixels side by
side (red across each, green down it, blue from square to square). `Grading/Neutral 32.png`
(in the engine's Assets) changes nothing: take a screenshot of the game, put the table beside
it in a photo editor, change the colors of both together (curves, warmth, a film look), and
save the table alone in the project's Assets. **Grading Amount** blends it in.

**Depth of field** blurs what is nearer or farther than the **Focus Distance**, keeping
**In Focus** meters around it sharp, by up to **Blur** pixels; a sharp thing in front does not
smear into the blur behind it. **Motion Blur** streaks what moves across the picture as the
camera turns or speeds along (the camera's own movement; **Shutter** is how long).

**Sunrise and sunset** come by themselves (Sky > Sunset Colors, on to begin with): as the sun
nears the horizon its light goes gold, orange and red through the thicker air, the sky glows
round it with a pink belt across from it, the clouds are lit from below in the same colors
(and stay lit a little after it has set, being higher), and then the sky fades through the blue
hour into night. Just turn the Sun, or let a Day / Night move it.

**At night** (with Sunset Colors) the **Moon** takes over from the sun once it has set: across
the sky from it, it lights the scene with a cold, dim light and casts its own shadows, and the sky
is a deep blue, not black (Sky > Moon: 0 a night without a moon, 1 a full moon). **Stars** shows
stars when the sun is down, turning slowly and twinkling.

**Clouds with depth** (Clouds > With Depth) are heaped clouds, flat underneath and billowing
above, between a **Height** and a **Thickness**: the sun lights them through, gold and red when it
is low, and a camera can fly under, into and above them.

**Decals.** A **Decal** (Add Component > Rendering) projects a picture down its entity's box onto
whatever is under it: the entity's scale is its size, and its y how deep it reaches (turn it to
put a sign on a wall). **Darkens** multiplies what is there, as dirt, cracks, tire marks and
damp do; off, it is painted over it.

**Wind.** A material's **Sway** lets the scene's **Wind** (Environment > Weather > Wind) move it, the more
the higher above the model's base: 1 for grass, 0.1 for a tree, in waves that cross the field
and gusts as gusty as the wind's gustiness. Painted Foliage sways with its material.

### Baked light: Light Probe Volumes

Without baked light every surface is lit by the whole sky, indoors as out: a room with one small
window is as bright as the field outside. A **Light Probe Volume** (Entity > Light > Light Probe
Volume) fixes that for the part of the scene inside its box. Put one round a building, a cave or
a street between tall houses, and press **Bake Light Probes** in Lighting > Baked Light.

Baking sends rays from probes spread through the box (**Spacing** meters apart: 1 for rooms, 4 or
more for open ground) into the scene's triangles. Each probe keeps, for six directions, how much
of the sky it sees that way and the light thrown back at it by what is there: the floor in the
sun lights the ceiling, a red wall tints the floor beside it. **Bounce** says how much the walls
give back (1 as their colors say). What moves (bodies that are not static) and what is seen
through is left out of the bake, but everything drawn inside the box is lit by it, moving or not.
The probes do not hold the sky itself: turn the sun to evening and the rooms still darken as they
should, though the light bounced inside them stays that of the sun they were baked in. Bake again
when the walls or the lights change.

The bake runs while the editor goes on, and takes seconds for a house, a minute or two for a town
at fine spacing. It is written beside the scene (`Scenes/Main.lighting` for `Scenes/Main.scene`,
see [FileFormats.md](FileFormats.md)), and the scene names it, so a built game has it too. **Stop
Using It** goes back to the sky's light everywhere and keeps the file. With traced light (ray
tracing at the project's quality) the rays already say all of this, more exactly, and the probes
are not used.

## Water, mist and traced light

- **Water** is the scene's sea (Environment > Water; its waves are the wind's and the weather's), or a shader of the game's own (Explorer > + New > Water Shader as Code). `Buoyancy` on a body makes it float on the sea.
- **Fog** has *Thins With Height* (it lies low, as mist does), *Glow Toward Sun* and *Light Shafts*, in Environment > Fog:
  with shafts the fog glows only where the sun reaches it, and what stands in the sun's way casts beams of shadow through the mist
  (with shadow maps, not with ray traced sun shadows).
- **Clouds Have Depth** (Environment > Clouds) makes the clouds volumes one looks through, lit from the sun's side.
- **Ray tracing** is in Project Settings > Quality, each part of it on or off for each quality level; on, it traces
  the light: the sun's shadows; the sky's light hidden where something is really in the way; bounced light (global
  illumination, one bounce: the same rays bring back the light of what they hit, so a red wall in the sun tints the
  floor beside it); and reflections of what is around on smooth surfaces (rougher ones keep the sky's soft
  reflection; how rough a surface still mirrors grows with the level). What a ray hits is seen in its material's
  color (not its textures), lit by the sky and the sun.

These shaders are compiled first with the extras and, if that fails on a system, again without: the console says which.

## Bodies that turn, and joints

A `RigidBody` with **Rotates** on turns when it is hit off-center, rolls down slopes and tumbles; boxes rest and stack flat. Off (as
bodies were before), it keeps the way it faces: what a character that walks wants. `body.applyImpulse(impulse, point)` pushes at a point.

`Joint` ties a body to another by name, or to the world: `ball` (joined at a point), `hinge` (turns about one axis: a door), `fixed`
(welded), `distance` (a rod, or with **Is Rope** a rope that only pulls). **Break Force** lets it give way.

## Ropes and chains

A **Rope** (Add Component > Physics) on an entity hangs from it, `length` meters long, or runs
to the entity named in **To** (a crate it carries, a post it is tied to; with a length longer
than the distance it sags). It is made of **Links** joined end to end: bodies that swing, wrap
round what they meet and are pulled by what hangs from them, and it does not stretch however
heavy the load. One mesh is drawn along it, of the entity's own material: a tube, or rings with
**Is Chain**. If the entity it starts at has a RigidBody, the rope moves with it and pulls it.
**Bend** makes it stiffer: the most each link turns from the next.

## Cloth

**Entity > 3D Object > Cloth** makes a sheet of cloth stood up, hanging from its top edge; or add
**Cloth** (Add Component > Physics) to any entity with a mesh. With the built-in plane it is a sheet
of **Segments** by Segments squares, as large as the entity's scale; with a model, the model's own
points (a cape, a sail, a flag made in Blender). While the game runs it hangs, falls, folds,
drapes over what it meets and flaps in the wind.

- **Pinned**: which points hold where the entity puts them, and go where it goes: the top edge (a
  curtain, washing on a line), the top corners (a banner, a hammock), the left edge (a flag on its
  pole), the four corners (a canopy, a sail), or none (it falls: a tablecloth). The top is the
  mesh's up; for the plane, the edge on top once it is stood up. Put the entity under a moving one
  (a boat, a character) and the cloth is carried with it, the rest of it trailing.
- **Stretch** (1: it does not stretch; lower as a knit, 0 as rubber), **Stiffness** (how it resists
  folding: 0 silk, 0.2 cotton, 0.6 leather), **Weight** (kilograms per square meter: 0.05 silk,
  0.25 cotton, 0.5 canvas), **Damping** (how soon it settles).
- **Wind**: how much the scene's wind and its gusts push it (Environment > Weather > Wind); a flag
  edge-on to the wind still flaps, in waves that run down it. At 0 it still falls through the
  air, slowly, as cloth does.
- **Collides**: kept out of the colliders around it (box, sphere, capsule, mesh, convex), held a
  little by them (it lies on a table and does not slide off at once), **Thickness** away from
  them. It does not collide with itself, and it does not push bodies.

It is seen from both sides whatever its material. From a game's code, `kit::Cloth` on the entity has
the same fields, changed at once while it runs; its `simulation->points()` are where it is.

## Animation, as in Unity

- **An entity's animations** are its own: made for it, and for nothing else. Choose the entity,
  open **Window > Animation** (or **Open Animation** on its Animator) and **Create Animation**: it
  is kept in Animations/<its name>, its **Animator** is made if it has none, and it is listed in
  the Animator's **Animations**. A model's animations (a `.gltf` or `.glb`) are its too.
- **The Animation window** keys it, as Unity's does: press **Record**, and where things
  are is kept at the playhead; click another time, change things in the Inspector or move them in
  the view, and each change is a key there. **Key** keeps everything animated as it is now. Keys
  are dragged along their track, a double click adds one, a right click chooses how the curve
  comes into it (smooth, linear, step); a right click on the ruler adds an event (a sound, or a
  message the entity's scripts hear). **Stop** puts everything back.
- **Procedural**: a right click on a track's name makes it move by itself, over its keys or alone:
  a wave, a wander, or a parameter of the Animator that the game sets (a door opened as much as
  "open" says). **Time by** makes a whole animation follow a parameter (0 its start, 1 its end)
  instead of time. **Create Procedural Animation** (in the Animation window, or in the Animator window's
  right click) starts one.
- **The Animator** plays one of them from the start (**Clip**), or, with a **Controller** (a
  `.animator`, drawn in **Window > Animator**: states, transitions on parameters, blend trees,
  layers, lit while the game runs), what its states say.
- **From code**: `animator->play("Open")`, `stop()`, `pause()`, `resume()`,
  `isPlayingAnimation("Open")`, `setFloat("speed", v)`, `setBool`, `setTrigger("jump")`,
  `crossFade("Jump", 0.1f)`. **From a visual script**: the Animation boxes (Play Animation, Stop,
  Pause, Resume, Is Animation Playing, Set Animator Float, Bool and Trigger, Play State).

## Someone to walk with

**Entity > Characters > Third Person Player** makes one: a **Character Controller** (the capsule
that is moved, slides along walls, climbs steps and stands on slopes), a **Character Mover**
(W A S D or the left stick, seen from the camera; Shift is the other pace; Space jumps) and an
**Orbit Camera** on the scene's camera. The template **Third Person Game** is the same with a
figure that is animated.

- **Its animations** follow how it moves: the mover tells the **Animator** under it its
  `speed` (meters a second), whether it is `grounded`, its `verticalSpeed`, and the trigger
  `jump`. A controller with the states Idle, Move (a blend of Walk and Run by `speed`), Jump and
  Fall, and transitions on those, is all a character needs: the template's
  `Animations/Player.animator` is one to copy, and **Window > Animator** draws it.
- **Feet that do not slide**: a walk covers the ground at one speed and a run at another. Set
  the mover's **Walk Speed** and **Run Speed** to the speeds the clips were made for, and put
  those same numbers on the two motions of the blend.
- **The camera**: turned by dragging with a mouse button, or, with **Captures Mouse**, without
  one (see "The pointer" below); the right stick always turns it. It comes closer rather than
  go behind a wall.

### A character of your own

A character is a model with bones and clips in one `.gltf` or `.glb` file. One made in a
modeling program, or downloaded from a library of characters and animations, is brought in
like this:

1. Save it as **glTF Binary (.glb)** with its animations in the same file, each named for what
   it is. (A library that gives one file a clip: open them together in a modeling program,
   name the clips Idle, Walk, Run, Jump, Fall, and export once.)
2. Put the file in the project's `Assets/Models`. Choose it as the **mesh** of the entity that
   shows the character, and as the **Model** of its Animator; give the entity a material (the
   model's own textures are not read: its color picture is chosen in the material).
3. Models face +z and the engine's forward is -z: turn that entity half way round (0, 180, 0)
   under the entity that walks. Scale it there too, if it was made in other units.
4. In the controller, each motion names a clip of the model. If its walk and run cover the
   ground at other speeds, change the mover's speeds and the blend's numbers to match.

## Seen from your own eyes

A body with a camera under it at the height of its eyes:

- on the body, a **Character Controller** and a **Character Mover** with **Turns To Face** off
  (its view turns it, not the way it goes);
- on the camera, **First Person Look**: the mouse, or the right stick, turns the body left and
  right and tips the camera up and down.

The template **Shooter Game** is that, with a rifle.

### The pointer

A view turned by the mouse with no button held needs the pointer: it is hidden and kept in the
window, and only how the mouse moves is told. **First Person Look** (and an **Orbit Camera**
with **Captures Mouse**) take it when the game starts; **Esc** (the action `pause`) gives it
back, so that the buttons on the screen can be clicked, and a click on the game takes it
again. In the editor the game hears the keys, the mouse and the controllers only while it has
them: from Run, or from a click on its picture, until Esc, a click anywhere else (a menu, another
window, the bar over the picture) or another program in front. It keeps running meanwhile, and the
pointer is given back. Esc again, with the Game window still the one in use, stops the game. From
code: `world.input.setPointerCaptured(true)`.

### A weapon

**Weapon**, on an entity under the camera (the rifle's model goes under it):

| | |
| --- | --- |
| **Fire** | The action `fire` (the mouse's button; a game with a gun should take Space out of it, in Project Settings > Input, since Space jumps). **Is Automatic**: on firing while it is held, at **Fire Rate** shots a second. |
| **A shot** | A ray from the camera to the first solid Collider within **Range**: it does not fly, it lands at once. It strays by **Spread** degrees standing, **Moving Spread** at a run, and a little more for having just fired; the sights drawn on the screen open and close with it. |
| **What is hit** | is told with the message `shot` (the number it carries is the damage), it and what it is part of: a component hears it in `onMessage`, a visual script in On Message. The first of them with **Health** loses **Damage**; a body that can move is shoved by **Push**; **Hit Effect** is a prefab left there. |
| **Loading** | **Magazine Size** rounds, **Reserve** besides (-1: no end of them). The action `reload` (or R) loads what the magazine lacks in **Reload Time** seconds; firing with nothing in it does too. |
| **On the screen** | The shared values `ammo`, `ammoReserve` and `ammoLoading` (0 to 1): a Text with `{ammo} / {ammoReserve}`, a Bar on `ammoLoading`. |
| **In the hand** | It rocks with the walk, jumps back at a shot (and kicks the view up by **Kick** degrees), is lowered while it loads, and is drawn back against a wall instead of going through it. **Muzzle** is an entity at the end of the barrel: its Light is on for a moment at each shot, and what is drawn under it shows for that moment. |

### Things to shoot, and things that come for you

- **Shooting Target**: a plate that falls back about its foot when it is shot and adds to a
  shared value; with **Stands After**, it stands up again. Put its plate (with a solid
  Collider) and its post under it. On a **Shuttle** (to a place and back, again and again:
  also a platform, a gate) it is a moving target.
- **Hunter**: a machine that hovers where it was put until it sees its prey (near enough,
  nothing solid between), then flies at them, over what is in its way, and hurts them while it
  is within reach. With a solid Collider and **Health** it can be shot down; Health's
  **Effect** is the prefab it leaves.
- **Supply**: with a trigger Collider, rounds for the Weapon of whoever touches it, or Health.
- **Damage Flash**: the edges of the screen go red when a shared value (`health`) drops.
- **Guard** (with a **Nav Agent**, see Navigation) is the enemy that walks: it goes its
  round, chases the player it sees and looks for the one it lost.

## Playing together

A game on several machines: one **hosts** (it listens, and plays too) or is a **server** (it
listens, and does not play), and the others **join** it at its address. The template **Online
Game** is the garden of the Third Person Game for several players, to take apart.

Two components do it:

- **Network Session** (**Entity > Gameplay > Network Session**), one in the scene: who is in the
  game. Everyone who is in gets a copy of its **Player Prefab**, theirs to move, at the next of
  the entities whose names start with **Spawn Points**. Its **Mode** says what happens when
  the game starts: **Menu** asks on the screen (Host a game, Join one at an address, or Play
  alone), **Host**, **Join**, **Server** and **Alone** do that at once, **Off** waits for code
  (`session->host(world)`, `join`, `serve`, `playAlone`, `leave`). Alone, the game is its own
  server and only player, with no network at all: a game made for several is also one for one.
- **Networked**, on every entity that must be the same thing for everyone: where it is goes
  from the machine that owns it to the others (and what its Animator was told, so that a
  figure is seen walking); destroyed on the server, it is gone everywhere.

Who owns what decides everything else. What is in the scene, and what the server spawns
(`session->spawn(world, "Prefabs/Crate.prefab", position)`), is the **server's**: only it runs
their rules, and the others show the result. A player's own object is **that player's**: their
machine moves it with their keys. So on each Networked, **Owner Only** names what only the
owner's machine keeps, by commas, components and entities under it alike:

| On | Owner Only | Why |
| --- | --- | --- |
| the player prefab | `CharacterController, CharacterMover` (and the camera, if it is under the player) | a copy on another machine has no keys to read: it goes where it is told |
| a gem | `Collectible` | only the server says who collected it; then it is gone from every machine |
| a crate that physics moves | `RigidBody` | only the server pushes it about; the others show where it has it |

**Shared Values** on the session are the shared values (the score) that are the server's on
every machine. A message reaches other machines with
`session->tell(world, entity, "wave")`: there, the same entity hears it as if it had been sent
to it there (to everyone else by default; `NetTo::Owner` or `NetTo::Server` for one machine).
**Forwards** on a Networked does that without code: `shot=damage` on a player tells its
owner's machine "damage" when a copy of it, on any machine, hears "shot" (a Weapon's shot),
and Health takes the message `damage` off itself.

To try it on one Mac: run the game twice (the editor's Run for one, the built game for the
other, or the built game twice), host in one and join `127.0.0.1` in the other. From a
terminal, `MOLTEN_NET=host`, `MOLTEN_NET=server`, `MOLTEN_NET=alone` or `MOLTEN_NET=join:192.168.1.20` before the
program's name starts it that way without the menu. Keep both windows in view: macOS slows a
window that is covered, and a host that stands still for eight seconds is taken to have gone. On a home network the others type the
host's address, which its screen shows; from outside it, the host's router must send UDP port
7777 on to the host (the session's **Port**). macOS asks once whether the game may accept
connections: it must be allowed to host.

What it does not do: everyone is in one scene, the session's, and loading another ends the
game for that machine; there is no taking over when the host leaves; what physics moves is
the server's alone, so a player who joined cannot shove a crate; and nothing hides from a
player who cheats what their own machine decides (where they are).

In code, under all this (`Engine/Net/Net.hpp`): `NetHost` is one end of a game over UDP
(`openUdp`), whose reliable messages arrive once and in order however many packets are lost,
and whose others arrive at once or never; `MemoryNetwork` is a network of nothing but memory,
with loss and delay to ask for, which is how the tests play a game on three machines in one
program (`Tests/NetworkTests.cpp`).

## Models: importing them, and their animations

Molten reads the model files most programs and stores give: **glTF** (`.gltf`, `.glb`), **FBX** (`.fbx`, through
[ufbx](https://github.com/ufbx/ufbx)), **Collada** (`.dae`), **Wavefront OBJ** with its materials (`.obj` and `.mtl`), **STL**
and **PLY**. Each is brought to the engine's way: meters, Y up.

**Importing** is as in Unity: drag files or whole folders from the Finder onto the **Explorer** and they are copied into the
folder it shows (onto the Scene view, they are copied and a model or a prefab is put in the scene where it is let go;
anywhere else, into the Explorer's folder). **Import...** does the same from a list.

**Into the scene**: drag a model from the Explorer onto the Scene view (where the pointer is on the ground), onto the
Hierarchy (in front of the Scene view's camera; onto a row, under that entity), or **Add to Scene** in the Inspector. It
becomes an entity for the model, with one under it for each of its parts, in its place, each with its material. Its
materials become files in Materials (`Materials/<model> <material>.material`), made once: change them as any other; its
pictures are found beside the model as it names them, or by their name anywhere in the project (pictures are often moved
into a folder of their own), and those carried inside the file (a `.glb`'s, an FBX's) are written out into Textures. Pictures
named as glTF exports name them (`Hull_baseColor`, `Hull_normal`, `Hull_metallicRoughness`) are taken together even where an
OBJ's material names only the color. A model with bones stays one entity. An OBJ that is only a shape stays one mesh, as ever.

If it has animations (glTF, FBX: a Mixamo character with its takes), add an **Animator**: `clip` names
which one plays (empty plays the first), `loops`, `speed`, and `blendTime` eases from one clip into the next. From code:
`animator->play("Run")`, `animator->isFinished()`. The model's vertices are moved by its bones on the processor each frame, which is
fine for a few characters and too slow for a crowd.

## A sea that moves

**The scene's sea** is in Window > Environment > **Water**: water everywhere below a **Level**, out
to the horizon, unless it is turned off. A new scene has it, half a meter under its floor; a
scene of before, or one a game makes in code, has none until it is turned on. Its waves are not
set there at all: they are **the wind's and the weather's** (Environment > Weather > Wind: 2
meters a second ripples it, 6 is a breeze with small waves, 12 a strong wind with white crests,
20 a gale; a storm raises the wind to one). There is one wind: the one that sways the grass and
carries the smoke. How it looks is set there, from a real sea to a cartoon lagoon:

| Water | |
|---|---|
| **Deep** | Its color where it is deep: what is left of white light after a long way through it |
| **Shallow** | The color it takes over a light bottom a meter or two down: turquoise over sand |
| **Clarity** | How far one sees through it, in meters: down from above, and across under it |
| **Scattering** | How much light its body sends back up: 0 dark ink, 1 a real sea, 2 a glowing lagoon |
| **Reflections** | How much it mirrors the sky and what stands over it |
| **Ripples** | The small waves over the swell: 0 glassy, 1 a real sea's, 2 choppy |
| **Caustics** | The lines of light its waves gather on its bottom: 0 none, 1 as real, 3 drawn strong |
| **Shore** | Foam where it meets the shore and what stands in it |
| **Whitecaps** | White on the crests of waves that break: none below a fresh breeze, more the harder it blows (as much of the sea as is white at sea, at each wind) |
| **Through the Surface** | The camera going through it: the water's edge across the picture as it goes, the other side blurred, a shake going in, drops on the glass that slide off coming out. Off: it just cuts |

Things float on it (Buoyancy), the rain rings it, a boat leaves its wake on it. The waves rise
and fall for real: the sea is drawn with a grid that follows the eye, fine nearby and coarser far
off, so the water beside the boat has every wave and the water at the horizon costs little.
Under its level all is under water: seen through it from above, the deeper the bluer; lit through
it, dimmer and bluer the deeper, with the sun's light gathered by the very waves above into bright
lines that crawl over the bottom and over whatever lies there, as they do in a real sea (they move
as the waves move, sharp a little way down, blurred deeper); and with the camera under it, the
world is seen from inside the water: hazy, blue, its surface overhead a mirror but for a round
window where the sky shows through, bent.

**Water of a game's own** (a pond, a pool, a lake above the sea, a cartoon lagoon) is a surface
shader, as in Unity: **Explorer > + New > Water Shader as Code** (or New Water Shader... in a material's Shader)
makes one that already works, to change: it is told how deep the water is at each pixel
(`in.behind`), the scene's wind (`in.wind`) and the time, and says the color, how much shows
through, the ripples, the foam. Put it on a plane. (Scenes of before, whose water was a material,
read with their sea where that water was, in its colors, if it was as large as a sea; a pool of
before stays a plane of clear blue, until it is given a water shader.)

**What floats leaves a wake.** Each body with a Buoyancy that is in the water has foam where the
water meets its hull (as large as the Buoyancy's hull, or else its collider), thrown up at the
bow as it moves; behind it, the churned trail it leaves, white right behind the stern and
breaking into streaks of foam over paler water as it spreads and fades over a few seconds; and
the two arms of its wake opening at the angle every wake opens at, which break white at speed.
It follows the way the boat really went, turns and all. Nothing to add: a boat, a buoy and a
crate have it as they float.

A body with **Buoyancy** over the sea rides its waves: the same sea is worked out for it, from
the same wind. From code: `waterHeightAt(world, position, height)`.

**Floating things and boats.** Give a Buoyancy a **Hull** (half its size each way) and a
RigidBody that rotates: it is held up at its four lower corners, each as deep as the water is
there, so it rocks on the waves, lists and rights itself. **Current** carries it along, as a
river does. A **Boat** (in the Boat Game template's scenes; no longer offered by Add Component) beside them goes ahead with W and turns with
A and D (or the left stick) while it floats, the rudder biting the more the faster it goes;
from code, untick Uses Input and set `throttle` and `steer`.

## Liquid

**Liquid** on an entity with a Particle System is liquid made of drops: a block of it (`fill`) that collapses and fills whatever colliders
hold it, or a stream (`pourRate`) poured from the entity. `dropRadius` sets how fine it is; `viscosity` goes from water to honey. The
Particle System's size and color are how the drops are drawn: a size about three times the drop's radius reads as liquid. A couple of
thousand drops are comfortable on the processor; it is not a sea.

## Colliders

As in Unity: **Rigidbody** makes an object move (falls, is pushed, bounces); the colliders give it its shape. **Box Collider**,
**Sphere Collider**, **Capsule Collider** and **Mesh Collider** are under Add Component > Physics; each takes the size of the
object's mesh when added. Without a Rigidbody, a collider is something that stands still: a wall, the ground. Colliders under
a parent stand where the parent puts them.

- **Mesh Collider** is the model's own shape (chosen from the list of models, or the one its Mesh Renderer draws). As it is,
  its triangles, hollows and all: for what stands still, such as a track, a building, uneven ground. Chosen, it is drawn as
  those triangles, where the model is (a mesh collider has no offset of its own: it is the model).
- **Convex**, on a Mesh Collider, wraps the model as tightly as a convex shape can (no hollows, no holes): it can then move with
  a Rigidbody, fall, roll, be hit and hit back, against every other kind of collider. A crate of an odd shape, a rock, a car's
  body. The Inspector says how many corners and faces it has.
- An object has one collider; for more shapes, put more objects under it.

**Sleeping.** A body that has lain still for a moment falls asleep, with whatever it rests against: it is left out of
the physics (no gravity, no contacts with what stays put) until something wakes it: a moving body touching it, a push,
its velocity set by a script, being moved by hand, or what it lay on being deleted or switched off. A heap of five
hundred crates asleep costs about a quarter of what it costs awake. The Rigidbody says "asleep" beside **Can Sleep**;
switch that off for a body that must react to something that does not touch it. Bodies on a Joint never sleep. With
many pairs to test exactly, the tests are shared among the processor's cores.

**Vehicles** hit the world with the colliders on them or under them, of any kind (a convex Mesh Collider of the body is the
closest to the real shape): each point where one is in something is a stiff spring out of it, and what it hits is pushed back
if it can move. The wheels' rays pass through the vehicle's own colliders. A vehicle with no colliders of its own uses the
corners of a box of its size (`hitsWithItsBox`) and its Bumpers. The Driving example's Fiat has a convex Mesh Collider on its
body shell (`Body Paint`): it touches the walls, and lands on its roof, with its own shape.

## What the editor shows of an asset

Thumbnails in the Explorer and the Inspector's pictures are computed on the processor
(`Editor/SoftRender.cpp`): a material is shown on a ball under a sun and a sky with the
renderer's surface model and its own pictures; a scene or a prefab as what is in it, in its
materials' colors and under its own sun, around where its camera is; a model turned a little.

A sound in the Inspector can be changed and saved: louder or quieter, normalized, faded in and
out, silenced, cut down or cut out, reversed, filtered low or high, made to loop without a
click, mono or stereo, with a few steps of undo. The same operations are in
`Audio/AudioEdit.hpp` (`molten::audio`) for a game's own clips.

## Seeing what is solid

The selected object's collider, and those of everything under it, are outlined in green
(triggers in yellow, mesh colliders by their triangles). **Colliders** in the Scene view's
bar outlines every collider of the scene.

Particle systems run in the editor only while they, or something above them, are selected:
a scene full of them costs nothing to build. In the game they all run.

## Shaders of your own

A shader says what a surface is at each pixel; the engine lights it, shadows it and fogs it like
any other. **+ New > Shader** in the Explorer (or **New Shader...** in a material's Shader list)
makes one drawn as a graph, and opens it in the Shader Graph (below). **+ New > Shader as Code**
makes one written by hand in Metal's shading language, as here. Both are `.shader` files, and a
material chooses either the same way; the Shader Graph's **Save as Code** turns a graph into code
to go on from.

```
properties {
    float speed = 1 range 0 10
    color glow = 1 0.4 0.1 1
    vector flow = 1 0 0 0
}

Surface surface(Surface s, Input in, Props props) {
    float band = 0.5 + 0.5 * sin(in.position.y * 6.0 - in.time * props.speed);
    s.emissive += props.glow.rgb * band;
    return s;
}
```

- `Surface s` arrives as the material's settings and textures made it: `color`, `alpha`,
  `metallic`, `roughness`, `occlusion`, `emissive`, `normal` (in the world). Return it changed.
- `Input in`: `uv`, `position` (in the world), `normal` (the mesh's), `view` (toward the eye),
  `time` (seconds), `tint` (the entity's).
- `Props props`: the file's properties by name, up to eight. A `float` is a number (with
  `range low high`, a slider), a `color` and a `vector` are `float4`.
- Helper functions may be written above `surface`.

A material chooses it under **Shader**, below Lit, Unlit and Water, and shows its properties.
Saving the file compiles it again while the editor (or the game) runs. If it does not
compile, the compiler's message is in the console and on the material, and materials that
use a project shader are drawn as plain lit surfaces until it does: a mistake never costs
the rest of the picture. In code: `material.shader = "Shaders/Lava.shader"` and
`material.setShaderValue("speed", Vec4{2, 0, 0, 0})`.


### Shader graphs

**+ New > Shader** in the Explorer (or **+ New** in **Window > Shader Graph**) makes a shader
drawn as boxes and wires on the board the visual scripts use, instead of written; a double click
on it in the Explorer opens it there. Space (or a right click) adds a node; drag from a pin to wire it.

- **Input**: UV, Position, Normal, View Direction, Time, Entity Tint, Wind, and **Material**: what
  the material's own settings and pictures made of the surface, before the graph.
- **Value** and **Property**: a number, a color or a vector typed in; a **Number Property** or a
  **Color Property** is one each material sets for itself, by its name (up to eight).
- **Math**: Add, Subtract, Multiply, Divide, Power, Min, Max, Mix, One Minus, Absolute, Fraction,
  Sine, Cosine, Saturate, Normalize, Negate, Step, Smoothstep, Remap, Dot, Length, Distance,
  Split and Combine. Every value is three numbers or one: one goes where three are wanted as
  three of itself.
- **Pattern**: Noise, Scroll UV (with time), Checker, Fresnel (a rim where the surface turns away).
- **Surface**: what the surface becomes. What is wired or typed into it replaces what the
  material made; the rest stays as the material has it.

It is saved as soon as the hand is off and every material using it changes at once; the window
says what is wrong, if anything, and **Show Code** shows the surface shader the engine makes of
it, and **Save as Code** writes that code as a shader of its own. A material chooses it under
**Shader** as it chooses any. (Graphs saved before 1.3 as `.shadergraph` files still work.)

## Import settings

A picture, a model or a sound has **Import Settings** at the top of the Inspector: how the file
becomes what the game uses. The file is never changed; the settings are kept in its `.meta`
(beside its identifier, so they follow it when it is renamed) and applied each time it loads.

- Pictures: read as color or as data, a largest size, drawn smooth or in sharp pixels (pixel
  art), with or without the smaller copies for far away, upside down.
- Models: scale (0.01 for centimeters), Z up, centered, normals worked out again smooth,
  faces turned around. A mesh collider made of the model follows the same settings.
- Sounds: mono, samples a second, normalized, volume.

A change is applied when you let go: the file is loaded again, and the open scene with it.

## Particle systems

**Entity > Effects > Particle System** makes one: soft white particles rising in a cone, to make
into whatever it should be (fire, smoke, rain, sparks are made, not picked). **Save as Effect**, in
the menu of its header in the Inspector, keeps it as a file in `Assets/Effects` (`+ New > Particle
Effect` makes one from the start); an `.effect` dragged into the Scene view or onto the Hierarchy, or
added with the viewer's **Add to Scene**, becomes that particle system again, as many times as
wanted. It is made of modules, as in Unity, each folding open in the Inspector; the ones with a box
are switched off until they are wanted:

- **Main**: playing or not, duration and looping, prewarm (already running when the scene
  starts), start delay, lifetime and speed (between two numbers), size and shade variation,
  gravity, **Simulation Space** (World: what it emitted stays behind as the entity moves on, a
  trail; Local: carried along), simulation speed, inherit velocity, the most at once.
- **Emission**: so many a second, so many a meter the entity moves (a trail), a burst at the
  start and bursts that repeat.
- **Shape**: a cone, a box, a ring, a sphere, a hemisphere or a line, from inside or from the surface.
- **Velocity over Lifetime**: a drift, outward (radial), round the axis (orbital: a whirl).
- **Forces**: **Buoyancy** (hot gas rises, and less as it cools: flames and smoke rise by this),
  drag, and how much the scene's wind carries them.
- **Noise**: swirling air (curl noise, which curls without clumping), its size, how fast it
  changes, and octaves for swirls inside swirls, as real smoke has.
- **Collision**: with the colliders of the scene and a floor; bounce, friction, the life lost on
  a hit, or gone on a hit.
- **Color** and **Size over Lifetime**: at birth, at a middle point, at death.
- **Rotation**, **Texture Sheet Animation** (frames a second, a random first frame).
- **Renderer**: billboard, stretched (a streak), horizontal (lying flat) or vertical (upright);
  a picture with clear around its shape (the engine's Particles/Smoke.png, Flame.png, Spark.png,
  Debris.png, or your own); additive (light) or alpha (matter) blending; **Lit** (lit by the sun
  and the sky like smoke, brighter with the sun behind it); **Soft Particles** (fading where they
  meet a surface instead of a hard line).

A moving emitter spreads what it emits along the way it came each frame, each particle as old as
it would be had it been born there: it leaves a smooth line, not a clump every frame.

## The game's interface

**Entity > UI** (the + of the Hierarchy) makes a basic piece of interface: a Panel, an Image, a
Text, a Button, a Toggle, a Slider, a Text Field, a Bar, an Arc, a Layout, a Touch Stick. Anything
more (a gauge, a menu, a health bar with a frame) is made of these, put under one another: each is
an entity with one component. The templates' dial is one: a full Arc as its face, an Arc for its
track and one that fills with the value, and two Texts (`{speed:0}`, the value as a whole number,
and the unit).

A Text shows shared values in braces: `{score}`, `{speed:0}` rounded to a whole number,
`{loading:percent}`, `{lap:time}` as a clock, `{weather:SUN|RAIN}` the word the value counts to.
Its **Align** says which way the words run from where it is (Left, Center, Right); Auto runs
them by the side of the screen it is anchored to.
They are laid out in the **Game** view, where they are seen as the player will see them:
click a piece to choose it, drag it into place, pull its lower right corner to resize it.

**Several at once.** Shift or Cmd and a click adds a piece to the chosen ones (or takes it
away); a box dragged over the empty picture chooses every piece it touches; choosing a parent
in the Hierarchy chooses every piece under it. Dragging any of them moves them all. A box is
drawn around them with a handle at each corner: pull one and they all grow or shrink together
from the opposite corner (from the middle with Alt), as one picture: sizes, radii, letters and
line widths alike, each keeping its place among the others. That is how a whole dashboard (dials,
the gear, the boost) is made bigger in one go. A single rectangle's corner resizes it freely;
with Shift, in proportion. The nine anchor squares change the anchor of every chosen piece.

Every piece is placed from an **anchor**, a point of the screen it holds on to (a corner, a
side, the middle), and an **offset** from there in points: a health bar anchored to the top
left stays in that corner on a phone and on a wide monitor. With a piece chosen, the nine
squares beside the Game view's shape list change its anchor and leave it where it is. Pick
another shape in that list (iPhone portrait, 16:9) to see the layout on that screen.

- **Text**: `{score}` shows a shared value, `{abs:off|on}` a word for it.
- **Button**: clicked or tapped. While pressed it holds a button action (as a key would);
  when clicked it can add to a shared value and load a scene. Scripts can ask `wasClicked`.
- **Bar**: a shared value as a share of its most: health, fuel.
- **Panel**, **Image**: a rectangle of color, a picture of Assets.
- **Toggle**: a box ticked or not, which sets a shared value to 1 or 0: sound on, subtitles.
- **Arc**: part of a ring, which can fill with a shared value: the face of a dial, a timer.
- **Stick**, and a **Button** made round: for phones and tablets, where there are no keys. The
  stick pushes two axis actions (`moveX` and `moveZ` unless told otherwise), the button
  holds one (`jump`): a game written for keys is played by touch without changing a line.

The interface is laid out for a 1280 by 720 screen and scaled to the one it is shown on
(between the width and the height, as Unity's Canvas Scaler does with *Match* 0.5): it keeps
its proportions as the Game view is made smaller. The Game view says the scale it is drawn at.

All of them work with the mouse as with a finger, so they are tried on a Mac: press Run,
click in the Game view, and press, drag and click them there. A game that also has keys
can hide its touch controls until the screen is touched (**Only On Touch Screens**).
From code: `input.setVirtualAxis(...)`, `input.setVirtualButton(...)`.

### Rows, columns, grids, lists

A **Layout** (Entity > UI > Layout) places the pieces of interface under it in the Hierarchy,
one after the other: in a column, a row (`arrangement` 0) or a grid of `columns` (2). Their order
is the order they are in on the screen, so in the Game view a piece dragged above another takes
its place. A menu is a Layout with Buttons under it.

- `padding` is the room inside its edge, `spacing` the room between two pieces; `alignment`
  puts them at the start, the middle or the end across.
- `stretches` makes them as wide as the column (as tall as the row, as big as a cell).
- `fitsContent` makes the layout as long as what it holds; a piece that is switched off takes no room.
- `scrolls`: what does not fit is dragged into view, with a finger or the mouse, or with the wheel;
  only what is inside it is seen and can be pressed. A thin bar at its side says where one is.
- Layouts go inside layouts: a row of icons in a column of a menu.

A **Slider** (Entity > UI > Slider) sets a shared value between `least` and `most` by dragging
its bar, by `step` if it has one; `isSaved` keeps it between plays as a preference of that name.

A **Text Field** (Entity > UI > Text Field) is a box to type in: a name for the best laps, a
code. Click or tap it and type; Return lets it go. What is typed is a shared text, shown by any
text as a value is: `Hello {playerName}`. While it is typed in, the letters are the field's and
not the game's (W does not drive the car), and a phone shows its keyboard. `isSaved` keeps it
between plays. From code: `world.text("playerName")`, `world.setText(...)`.

Texts and buttons can each have their own **font**: one made by `Tools/bake_font.py` and put in
Assets (`Fonts/Title.png` and `Fonts/Title.txt`), named without its ending: `Fonts/Title`.


A piece of interface lives on the screen, not in the world: the Inspector shows no 3D Transform
for it, only its place (anchor, offset, size), and it is moved in the Game view, where the pieces
under it move with it. A dial is made of the basic pieces under one: a Panel or an Arc for its
face, an Arc that fills with the value, Texts for the number and its unit.

## The game in other languages

Write the texts of the interface in one language, the one you work in. For every other
language, add a file to **Assets/Localization**, named for the language (`it.txt`, `fr.txt`,
`de.txt`), with a line per text: the text as the game writes it, then the text shown.

```
# Driving in Italian
"RACE" = "GARA"
"BEST LAP   {bestLap:time}" = "GIRO MIGLIORE   {bestLap:time}"
"RAIN" = "PIOGGIA"
```

Texts, button labels and the words chosen by `{weather:SUN|CLOUDS|RAIN}` are all looked up.
A text with no line is shown as written, so a table can start small. `\n` is a new line and
`\"` a quote; lines that start with `#` are notes.

A game made with **Build** starts in the language of the system, when it has a table for it.
A script can change it with `world.language = "it"` (empty: as written), and
`world.languages()` lists the tables the game has. Driving has a LANGUAGE row in its
Settings that does this.

## Models in parts

A model with several materials (of any kind above) is several meshes: `Models/car.glb#0`, `#1`, … one for each material, in the order the file
names them. Give each its own entity and material, as the Driving project does for its car (paint, glass, chrome, lamps), and a
script can then change one of them alone: the rear lamps glow when the brakes are on.

## Build and Export

**File > Build Settings...** (Shift+Cmd+B) goes from a project to a game that runs by
itself in five steps: the game (name, version, identifier, splash), where it starts, how it
is played (touch controls), its scripts (built by themselves when saved and before the
export: there is nothing to press), and the export, for the Mac or as an Xcode project for
iPhone and iPad.

### Smaller when built

The game takes the project's Assets along made smaller, the project's own files untouched:
pictures larger than the device shows are brought down once at the build (4096 pixels on the
longer side for the Mac, 2048 for iPhone and iPad, or less where their import settings say),
instead of every time the game loads them; WAV sounds kept in floating point or 24 and 32 bits
become 16-bit (and mono, or another rate, where their settings ask); and the art programs' own
files kept beside the art (.psd, .blend, .kra, .xcf...) and .DS_Store are left behind. For the
phone the smaller copy is made in Build/iOS/Assets. Models are taken as they are.

### The icon

A game wears the engine's icon until it is given its own: in the Build window's first step, "Choose a Picture…" (a square PNG,
1024 pixels is best) copies it into the project as `Assets/Icon.png`, and "Use the Engine's" goes back. The Mac application gets an
`.icns` made from it, the iPhone and iPad application the sizes iOS asks for.

## Far away: LOD Groups

A **LOD Group** on a model draws a lighter version of it from far away. Under the group, its child called "Far" (the name is the group's `far` field) holds the light version; everything else under the group is the full one. Within `distance` meters of the camera the full one is drawn, beyond it the far one, for each view. The Driving example's cars have one at 35 m: `Tools/make_far_model.py` made their far model, with 4% of the triangles.

## Sounds that come from somewhere

`world.placeSound(sound, position, fullWithin, silentBeyond)` puts a sound started with `world.startSound` at a place in the world: it is heard from that side of the camera, at full volume within `fullWithin` meters and fading to nothing at `silentBeyond`. Call it again as the thing moves. The rivals' engines in the Driving example are placed this way; the player's own is not, as it is always just there.

## Characters

A character is a **Character Controller** (a capsule the size of someone: radius, height, the step it climbs, the slope it walks) moved rather than pushed: it slides along walls, goes up steps and curbs, stands on slopes it can walk and slides off the steeper ones, and shoves crates out of its way. Its entity's position is its feet. Code moves it with `controller->move(world, self, displacement)`.

For a player, add a **Character Mover**: it walks (and runs with Shift or the action "run") where the move actions say, as seen from the camera, jumps with the jump action, and turns to face where it goes. An **Orbit Camera** on the camera follows it from behind, turned by dragging with the mouse or with the right stick, and comes closer rather than go behind a wall.

**Animation with states.** An Animator with a `controller` (a `.animator` file) plays states instead of one clip: the parameters the game sets, states that are a clip or a blend tree (Idle, Walk and Run by speed, kept in step), and transitions when the parameters say so. The Character Mover sets `speed`, `grounded`, `verticalSpeed` and the trigger `jump` on the Animator of its model by itself. The format is in Engine/Animation/AnimatorController.hpp; for example:

```
molten-animator 1
parameter float speed 0
parameter bool grounded 1
parameter trigger jump
state Locomotion blend speed
  motion Idle 0
  motion Walk 2.2
  motion Run 5.5
state Jump loops 0
  motion Jump
  event Audio/jump.wav 0
start Locomotion
transition Locomotion Jump when jump
transition Jump Locomotion exit 0.9 blend 0.15
```

**In every direction.** A blend tree with two parameters places each motion at a point:
`state Move blend moveX moveY`, then `motion Forward 0 1`, `motion Left -1 0`, `motion Idle 0 0`
and so on. At a motion's point it plays alone; between points the nearest are mixed, and the
mix always adds up to the whole (strafing, walking backwards, the diagonals between).

**Layers.** Under the states, `layer Arms mask Spine weight aim` begins a layer: its own states
and transitions, played over the ones above on the bones of its mask (each with all those below
it; no mask, the whole body), by its weight, a number or a float parameter. The legs walk while
the arms aim, wave or carry. `additive` adds the layer's movement to what is under it instead
(a breath, a flinch, a look to one side):

```
layer Arms mask Spine weight aim
  state Relaxed
    motion Idle
  state Aiming
    motion Aim
  transition Relaxed Aiming when aiming
layer Breath additive weight 0.5
  state Breathe
    motion Breathe
```

**Ragdolls.** A **Ragdoll** (Add Component > Physics) beside the Animator makes the
character go limp when it is ticked, at `ragdoll->limp(world, self, push)` or at the message
"limp": a capsule body for each main bone of the skeleton (or those named in Bones), joined to
the one above by a joint that bends at most Bend degrees, and the bones follow the bodies as they
fall, keeping the speed it had. "stand up" (or `standUp`) takes the bodies away and the animation
goes on. Its parts do not collide with one another, nor with the character's own collider.
Joints of your own can bend only so far too: a ball joint's **Swing Limit**.

A **Two Bone IK** on the same entity puts the end of a limb (a foot, a hand, by its bone's name) on a target entity, bending the knee or elbow toward a pole.

**Finding the way.** A **Navigation Area** looks at a rectangle of the world when the game starts: which ground can be walked (not too steep, with room above it for the agents' size) and which steps join it. A **Nav Agent** then walks to an entity (its `target`) or to a point (`agent->goTo(point)`) along the shortest path, round walls, with its Character Controller if it has one. A **Guard** on a Nav Agent walks a round of points ("Patrol Point 1", "Patrol Point 2"...), chases the player when it sees them, searches where it saw them last and goes back to its round.


### Root motion

An animation made walking forward (the way animators make walks, runs, dodges, climbs) can move
its entity: tick **Root Motion** on the Animator. The movement of the skeleton's first bone
across the ground (or of `rootBone`) moves the entity, turned as it is, through its Character
Controller if it has one (so walls still stop it); the bone itself stays over the entity, with
only its bounce. The feet then stay on the ground instead of sliding. `rootMoved` says how far
it went this frame.

## Finding things, and changing many at once

The Hierarchy's search box finds by name, and by more with a word before a colon:
`c:light` (with a component: Light, Point Light, Collider, Health...), `tag:enemy`,
`layer:water` (or its number), `prefab:wheel` (the copies of a prefab), `is:off`. Every word
must hold, so `tag:enemy c:collider` is enemies with colliders; a word with `-` before it must
not: `c:light -c:camera`. **Select all** picks everything found.

With several entities selected (Cmd-click, Shift-click, or Select all), the Inspector shows the
last one, and what is changed there is changed on all of them, value by value: setting x of the
position sets it on all, each keeping its own y and z; the intensity of a light set on one is
set on every one that has a light, their colours left as they were. A component added or
removed is added to or removed from each. Undo takes back the change on all of them.

### Snapping and arranging

The arrow beside **Snap** in the Scene view's bar says what moving snaps to: **Steps** (of the
Move step, with Snap on), **Surfaces** (the selection goes onto whatever is under the mouse,
and with "Stands on the surface" is turned to stand on it: a lamp on a slope, a picture on a
wall), or **Corners** (onto the nearest corner of another model, for pieces of a building that
meet exactly). Holding **V** while moving snaps to corners whatever is chosen.

Under it, **Arrange the selection** lines several up along X, Y or Z (at the least of them,
their middle or the most), **Spread**s them evenly between the two furthest apart, or **Drops**
each to the ground under it.

## What a scene weighs

**Window > Scene Weight** lists the objects that draw a model, the heaviest first: the triangles
each draws (all its copies, for foliage), its share of the scene's, and the memory of its model
and its pictures. At the top, the totals: models and pictures shared by several objects are
counted once, and the draws are one for each model and material, however many copies (the
renderer draws them together). Order by memory to find the pictures that are too big; a click
on a row selects the object.

## The mixer: groups, ducking, rooms and walls

Every sound plays in a group: **Effects** (the default), **Music**, **Voices**, **Interface**,
or one the game names. `world.setVolume("Music", 0.5f)` sets a group's volume (a settings
menu's sliders, or the Set Volume node); `setMuted` silences it; both last from scene to scene.
An Audio Source says its group; `world.startSound(name, volume, pitch, loops, "Voices")` and
`world.playSound(name, volume, "Interface")` too.

The music **ducks** under the voices from the start: while anything plays in Voices, Music goes
down to 30% and comes back up after. `world.duck("Music", "Voices", 0.2f)` changes it, and
`world.duck("Ambience", "Effects")` makes another; an empty second name stops it.

**Rooms.** A **Reverb Zone** (Add Component > Audio) makes the sounds heard inside it ring: a
Small Room, a Hall, a Cave, a Bathroom, a Cathedral, a Forest, or Custom (size, damping, wet).
It is a sphere of `radius` round its entity, or a box; at its edge it fades in over `edge`
meters, and where zones meet the one the listener is most inside is heard. Effects and voices
ring; music and the interface do not.

**Walls.** A sound placed in the world (an Audio Source with **Comes From Here**, or
`world.placeSound`) is muffled when something that stands still is between it and the listener:
quieter, and without its highs, as through a wall. `world.muffledByWalls = false` turns it off.

## Prefabs in prefabs, and variants

A prefab dragged into a scene is a copy by reference (its name in blue in the Hierarchy): the scene keeps only where it is, and what the prefab holds is put under it each time the scene is opened, so that a change to the prefab shows in every scene. A prefab can hold copies of other prefabs in the same way. **Unpack Prefab** (right click in the Hierarchy) makes a copy the scene's own, to change freely.

A **variant** (right click on a prefab in the Explorer: Create Variant) is its base with some values changed, written as the changes:

```
molten-variant 1
base "Prefabs/Enemy.prefab"
name "Fast Enemy"
set "" NavAgent speed "6"
set "Body" MeshRenderer material "Materials/Red.material"
set "Body" Transform scale "1.2 1.2 1.2"
```


A copy can also differ from its prefab on its own. Change a part of it in the Inspector, or move
it in the Scene view, and the change is kept with that copy only: the Inspector says "Part of a
copy of ..." and how many of its values are its own, and **Take Back** makes the part as the
prefab has it again. Everything else still follows the prefab. In the scene file these are the
`override` lines under the copy's `prefab` line.
