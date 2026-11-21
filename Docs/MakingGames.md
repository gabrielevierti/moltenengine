# Making a game

This is the shortest way from an empty editor to something you can play: what comes with the engine for the things nearly every game needs, and how the pieces fit. None of it needs code. All of it can be replaced by code, a piece at a time, when a game outgrows it ([GameCode.md](GameCode.md), [VisualScripting.md](VisualScripting.md)).

## Start from a game

The example game is **Driving**: a Fiat 500 on a small circuit, to race four rivals or to lap against the clock.

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
| **Effects** | The nine particle presets, each on a pedestal, in the wind. |
| **Physics** | A pyramid of crates, a curve of dominoes that falls by itself, five balls hung in a row, a wrecking ball, two gates on hinges, bowling pins, a chute that never runs out of balls. The car shoves what it hits. |
| **Jumps** | A table top, whoops and a kicker: models with mesh colliders, solid exactly as they are drawn. |
| **Water** | A basin of ocean waves, with crates and buoys floating on them. |
| **Liquids** | Two tanks of particle liquid, water and honey, poured as you watch. |

The scene's settings turn everything on: ray traced shadows, reflections and bounced light, clouds with depth, fog with light shafts, wind. It is the project to open for screenshots, and the heaviest to run.

## The components that come with the engine

They are under **Add Component** in the inspector, like a game's own. Each does one thing; a game is several of them on a few entities.

| Component | What it does |
| --- | --- |
| **PlayerMover** | Moves its entity with W A S D or the arrows, Space to jump. `rolls` makes it a ball that is pushed; `movesAlongZ` off and `limitX` make it a paddle or a ship. |
| **Follow** | Keeps its entity (a camera, usually) at an offset from another, smoothly. |
| **Spin** | Turns at a steady rate. |
| **Mover** | Moves at a steady velocity, or toward the entity named in `chases`. |
| **Lifetime** | Removes its entity after some seconds: bullets, effects. |
| **Collectible** | With a trigger collider: when `collector` touches it, a shared value goes up and it is gone. |
| **Health** | Takes damage; at zero the entity is removed, optionally with points, an effect, and the scene starting over. |
| **Damage** | Hurts the Health of what it touches; `onlyTo` limits it by name. |
| **Shooter** | Spawns a prefab in front of its entity while a key or the pointer is held. |
| **Spawner** | Spawns a prefab every few seconds, around its entity. |
| **HudText** | Text on the screen. `{name}` shows a shared value: `Coins {score}`. |
| **Goal** | When a shared value reaches a target: a message, and optionally the next scene. |
| **RestartWhenBelow** | Starts the scene over when its entity falls below a height. |
| **ConstantSpeed** | Keeps a body at one speed whatever it bounces off: the ball of a brick breaker. |
| **Breakable** | Removed when hit, with points. |
| **Explosion** | Pushes bodies away and hurts what has Health, once, when it appears. |
| **Flicker** | Makes a light waver like a flame. |

Their code is `Engine/Kit/Kit.cpp`, about three hundred lines, written against the same interface a game's code uses. When one almost fits, copy it into the project's `Source` folder under another name and change it.

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

The scene has two windows. **Scene** is the editor's view: fly around, select, move, change. **Game** is what the player will see, through the scene's own camera, with the game's interface. They start as two tabs; drag one beside the other to see both at once.

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

**Create > Effects** has fire, smoke, sparks, an explosion, rain and snow. Each is an entity with a particle emitter you can then change in the inspector (fire also carries a light with `Flicker`; an explosion a light, `Explosion` and `Lifetime`). Save one as a prefab and it can be the `effect` of a `Health` or a `Collectible`.

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

**Project** shows the assets with pictures made from the files: a model as its shape, a material as a lit ball, a sound as its wave, a scene or a prefab as a plan from above, a visual script as its boxes, code as its first lines. **Add Component** in the inspector is sorted as Unity's is (Mesh, Rendering, Physics, Audio, Effects, UI, Vehicles, Gameplay, Scripts) and can be searched; each component's menu has Reset, Copy Values, Paste Values and Remove.

**Layouts.** The default is Unity's: Hierarchy at the left, Inspector at the right with a small Viewer under it, Scene and Game as tabs in the middle, and Explorer, Console, Input and Visual Script as tabs along the bottom. Run, Hold and Frame are at the top right, beside the layout menu, which has four arrangements (Default, 2 by 3, Tall and Wide). Arrange the windows as you like and choose Save Layout: it joins the list, on this Mac, for every project. **Export Layout...** writes it to a file to keep or pass on; **Import Layout...** takes one back. The Scene and Game views never get smaller than 360 by 240.

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

**Phones that heat up.** `world.deviceHeat()` says how hot the device is (0 cool to 3 very hot) and `world.isSavingBattery()` whether Low Power Mode is on. When it is hot the renderer already draws the scene smaller until it cools (Rendering > Quality); a game can do more, such as fewer effects. **Dynamic Resolution** (in the same place) draws the scene smaller whenever the GPU falls behind the frame rate it is given, and **MetalFX Upscaling** makes a smaller picture the screen's size with sharp edges.

**Shipping.** In the Build window, **Send to App Store Connect** archives an iPhone and iPad game for release and uploads it: it appears under TestFlight, and from there goes to review. The app must exist in App Store Connect with the project's identifier, and Xcode must be signed in to your account; the build number is the date and time, so each upload is new. For the Mac, **For other Macs** signs the exported game with your Developer ID certificate and has Apple notarize it, so it opens on any Mac; once, in Terminal: `xcrun notarytool store-credentials molten --apple-id <your Apple ID> --team-id <your team>`. **Development** marks an export as a development build, where F1 shows the statistics and the profiler.

**Shaders ahead of time.** With the Metal compiler installed (Xcode > Settings > Components > Metal Toolchain), `build.command` compiles the engine's shaders first: a mistake in them is reported there, and the engine starts without compiling them. Without it, nothing changes: the engine compiles them as it starts.

**What the Scene view shows.** The arrow beside Snap opens its steps and the list of what is drawn over the scene: the grid (1 m squares near the ground, 10 m and 100 m from higher up, so it is there from far away too), the outline of what is selected, the colliders of everything, lights, cameras, particle effects, sounds, and what plugins draw. Each kind can be switched off, and its icons are then not clicked either. **Sound** at the right of the Game view's bar silences the game while you work, without changing its own settings. Pieces of the interface that are switched off (a menu the game opens later) are not chosen or moved by clicks in the Game view; choose them in the Hierarchy and switch them on to lay them out.

**Explorer.** Right-click anywhere in it (the folder tree, a folder, the empty space, inside any folder) for New with every kind of asset, Import and Reveal. The size slider goes down to a list. **Console** is Unity's: errors, warnings, info and debug messages, each kind shown or hidden by its button with its count; Collapse folds repeats into one row with a count; Clear on Play; a search; click a row for the whole message below, to copy.

In the Scene view, several selected objects move, turn and scale together. **Center** in the toolbar puts the handles at the middle of them all, which they turn and scale around; **Pivot** puts them on the last one chosen.

In the scene view a camera is drawn as the pyramid of what it sees, a directional light as its rays, a spot light as its cone. A camera's scale changes nothing about its picture.

## Shots, and a camera that glides between them

**Create > Camera Shot** makes a shot where the Scene view looks from now (and, the first time,
gives the scene's Camera a **Camera Director**). The director films the shot of the highest
`priority` among those switched on, and glides to a new one over its `blendSeconds` (0 cuts),
slowing out and in, its field of view too. Switch a close shot on (from a trigger, a script, a
timeline's Show track) and the camera goes to it; switch it off and it goes back. A shot can move:
give it Follow or Orbit Camera and the camera follows it, blends included.

## Cutscenes: Timeline

A **Timeline Player** (Add Component > Animation) plays a `.timeline` file: what happens when,
on tracks. **Window > Timeline** shows the tracks of the Timeline Player chosen (or the scene's
first); **New Timeline** makes its file.

- **Add Track > Move** (with an entity chosen): put the entity where it should be, drag the time,
  press **Key**; again at another time. It goes smoothly through its keys.
- **Camera cuts**: at each mark, that camera is the one seen (its Camera made active, the others
  the track names made inactive). **Add** puts the camera chosen in the Hierarchy.
- **Sounds**, **Set a shared value** (what a menu or a script waits for), **Show or hide**, and
  **Animation** (a trigger of its Animator: "wave").

Drag the time along the top to see the scene as it is then; **Preview** plays it here, without
its sounds and values; **Stop** puts everything back where it was. Drag a diamond to move it in
time; a right click changes or removes it. Every change is written to the file at once.

The player: `playsOnStart`, `loops`, `speed`, and `doneValue`, a shared value set to 1 when it
ends. From code: `player->play()`, `pause()`, `seek(world, seconds)`.

## Morning, noon and night

**Day and Night** (Add Component > Environment), on any entity, moves the scene's sun (the
Directional Light named in `sun`) across the sky as `hour` goes from 0 to 24: up in the east at
6, highest at noon (`highest` degrees), down in the west at 18; `facing` turns its path. Its light
warms near the horizon and goes at night, when a dim blue moon lights the scene instead. The sky,
the light it gives and the fog follow; by day they are the scene's own, as set in Rendering.
`dayMinutes` makes the clock run (real minutes for a whole day; 0 stops it). It sets the shared
values `hour` and `night` (0 by day, 1 in the deep night): turn on lamps and headlights with it.

## Hills and valleys: Terrain

**Create > 3D Object > Terrain** makes flat ground, 200 by 200 meters. With it chosen, brushes
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

**Create > 3D Object > Foliage** makes an entity whose Mesh Renderer's model (little cubes until
you give it yours: a tuft of grass, a tree, a rock) is painted on the ground. With it chosen,
brushes appear in the lower left of the Scene view: **Paint** puts copies on whatever is solid
under the brush (a Terrain, the ground, a roof), **Erase** (or Shift) takes them away. **Density**
is copies a square meter; `spacing` keeps them apart; each gets a turn and a size between
`smallest` and `largest`. They are drawn together, in few draw calls, and not beyond
`drawDistance` meters. Their places are a file (`Foliage/Grass.foliage`), written when a stroke
ends. For grass and a tree on the same hill, make two Foliage entities.

## Curves: roads, rails, paths

**Create > Paths > Spline** makes a curve; **Create > Paths > Road** makes one that is closed,
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

## Files changed in other programs

The editor looks at the project's files once a second. A picture saved again in Photoshop or
Pixelmator, a model exported again from Blender, a material or a sound written by another
program is read again into what already uses it: the scene changes in front of you, without
opening it again, and the Console says which file was read. A file is read once it has been
left alone for a moment, so a program that writes it in several goes is not caught halfway.
Shaders were already read again as they change; code is built again by itself.

## What uses what

The Viewer says, for the asset chosen in the Explorer, what uses it and what it uses
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

**Edit > Editor Settings** (Cmd+,). Under **Shortcuts**, click an action and press its new keys:
Backspace leaves it without one, Esc keeps what it had. An action whose keys another one uses
says so in orange. **Reset** puts one back; **All as They Were** puts them all back. The menus
and Cmd+K show the keys as you set them. Under **Look**: Forge (the orange of hot iron), Ocean,
Moss, Midnight. Both are kept for the next time the editor opens.

## Finding anything: Cmd+K

**Cmd+K** (or **Edit > Find Anything**) opens one field that finds by name everything the editor
has: what the menus do, the windows, the Create menu, every entity of the scene and every asset
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

**When / Then** (Add Component > Logic) does up to four things when something happens. All of
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

Give **Saveable** (Add Component > Logic) to what should be kept: a chest, a door, the player,
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

A project's scripts are in `Assets/Scripts`, where the Explorer shows them: click one to read it in the Viewer, with the component it
declares and a button to add it to the selected object. The templates' own are there: `Follow.cpp`, `DriveControls.cpp`, `Motor.cpp`,
`FlightControls.cpp`. They are built when the project opens and whenever a file changes. (A `Source` folder beside Assets, where
scripts were before, is still built.) A script with the name of a component that comes with the engine takes its
place; if the code does not build, the engine's own is used, so the game still runs. `lateUpdate(World&, Entity, float)` runs after every
`update`, with everything where it will be drawn: cameras that follow belong there.

A visual script can be written out as C++, and C++ made that way can go back to boxes: see [To C++ and back](VisualScripting.md#to-c-and-back).

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

**The sky from the air.** Under Sky, **From the Air** works the sky's colors out from how
sunlight scatters in the air, from the sun's height alone: blue at noon, gold and red as the
sun sets, dark at night (with a Day / Night, it goes round by itself). **Haze** whitens it.
**Stars** shows stars when the sun is down, turning slowly and twinkling.

**Decals.** A **Decal** (Add Component > Effects) projects a picture down its entity's box onto
whatever is under it: the entity's scale is its size, and its y how deep it reaches (turn it to
put a sign on a wall). **Darkens** multiplies what is there, as dirt, cracks, tire marks and
damp do; off, it is painted over it.

**Wind.** A material's **Sway** lets the scene's **Wind** (Rendering > Wind) move it, the more
the higher above the model's base: 1 for grass, 0.1 for a tree, in waves that cross the field
and gusts as gusty as the wind's gustiness. Painted Foliage sways with its material.

## Water, mist and traced light

- **Water** is a material: choose the shader *Water* (waves cross it, it mirrors the sky at a glancing angle). `Buoyancy` on a body makes it float at a water level.
- **Fog** has *Thins With Height* (it lies low, as mist does), *Glow Toward Sun* and *Light Shafts*, in Lighting > Environment:
  with shafts the fog glows only where the sun reaches it, and what stands in the sun's way casts beams of shadow through the mist
  (with shadow maps, not with ray traced sun shadows).
- **Clouds Have Depth** (Lighting > Environment > Sky) makes the clouds volumes one looks through, lit from the sun's side.
- **Lighting > Quality** has four levels, Low, Medium, High and Ultra, from shadow maps alone to every traced effect
  with four rays: choose one to start, then change any setting under it (the level then reads Custom).
- With **ray traced sun shadows** on: *Ambient Light* hides the sky's light where something is really in the way;
  *Bounced Light* is global illumination, one bounce: the same rays bring back the light of what they hit, so a red wall in the sun
  tints the floor beside it; *Reflections* makes smooth surfaces mirror what is around them: only those smoother than
  *Up To Roughness* (0.3 to begin with), fading toward it, and as much as *Strength* says. Rougher surfaces keep the sky's soft reflection. What a ray hits is seen in its material's
  color (not its textures), lit by the sky and the sun. More *Ambient Rays* give less grain: there is no denoiser yet.

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

## glTF models and animation

A `.gltf` or `.glb` file is a model like an `.obj`: choose it as an entity's mesh. If it has animations, add an **Animator**: `clip` names
which one plays (empty plays the first), `loops`, `speed`, and `blendTime` eases from one clip into the next. From code:
`animator->play("Run")`, `animator->isFinished()`. The model's vertices are moved by its bones on the processor each frame, which is
fine for a few characters and too slow for a crowd. Its textures are not read yet: give the entity a material.

## A sea that moves

Add **Water** to an entity that draws a mesh with a Water material: the mesh becomes a sheet whose surface is an ocean worked out with a
Fourier transform, from the wind you give it. `windSpeed` is in meters per second and raises the waves that wind really raises (3 ripples
a pond, 9 is a fresh breeze with waves of two meters, 20 a gale); `choppiness` sharpens the crests; where they fold over, foam shows.
`resolution` is how fine the sheet is and `patchSize` how far before the pattern repeats. A body with **Buoyancy** over a Water rides
its waves. From code: `waterHeightAt(world, position, height)`. For water you can see through, set the material's blending to Blend
and lower its alpha; it does not bend what is behind it yet.

**Floating things and boats.** Give a Buoyancy a **Hull** (half its size each way) and a
RigidBody that rotates: it is held up at its four lower corners, each as deep as the water is
there, so it rocks on the waves, lists and rights itself. **Current** carries it along, as a
river does. A **Boat** (Add Component > Vehicles) beside them goes ahead with W and turns with
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
  its triangles, hollows and all: for what stands still, such as a track, a building, uneven ground.
- **Convex**, on a Mesh Collider, wraps the model as tightly as a convex shape can (no hollows, no holes): it can then move with
  a Rigidbody, fall, roll, be hit and hit back, against every other kind of collider. A crate of an odd shape, a rock, a car's
  body. The Inspector says how many corners and faces it has.
- An object has one collider; for more shapes, put more objects under it.

**Vehicles** hit the world with the colliders on them or under them, of any kind (a convex Mesh Collider of the body is the
closest to the real shape): each point where one is in something is a stiff spring out of it, and what it hits is pushed back
if it can move. The wheels' rays pass through the vehicle's own colliders. A vehicle with no colliders of its own uses the
corners of a box of its size (`hitsWithItsBox`) and its Bumpers. The Driving example's Fiat has a convex Mesh Collider on its
body shell (`Body Paint`): it touches the walls, and lands on its roof, with its own shape.

## What the editor shows of an asset

Thumbnails in the Explorer and the Viewer's pictures are computed on the processor
(`Editor/SoftRender.cpp`): a material is shown on a ball under a sun and a sky with the
renderer's surface model and its own pictures; a scene or a prefab as what is in it, in its
materials' colors and under its own sun, around where its camera is; a model turned a little.

A sound in the Viewer can be changed and saved: louder or quieter, normalized, faded in and
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

**+ New > Shader** in the Explorer (or **New Shader...** in a material's Shader list) makes a
`.shader` file: a surface shader, written by hand in Metal's shading language. It says what
the surface is at each pixel; the engine lights it, shadows it and fogs it like any other.

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

## Import settings

A picture, a model or a sound has **Import Settings** at the top of the Viewer: how the file
becomes what the game uses. The file is never changed; the settings are kept in its `.meta`
(beside its identifier, so they follow it when it is renamed) and applied each time it loads.

- Pictures: read as color or as data, a largest size, drawn smooth or in sharp pixels (pixel
  art), with or without the smaller copies for far away, upside down.
- Models: scale (0.01 for centimeters), Z up, centered, normals worked out again smooth,
  faces turned around. A mesh collider made of the model follows the same settings.
- Sounds: mono, samples a second, normalized, volume.

A change is applied when you let go: the file is loaded again, and the open scene with it.

## Effects

A **Particle System** is set in sections (Emission, Shape, Life and Motion, Look), and
**Start From...** fills it with one of the engine's effects (Fire, Smoke, Sparks, Explosion,
Dust, Rain, Snow, Magic, Fountain) or one of the project's. **Save as Effect** makes a
`.effect` file of it; **+ New > Effect** makes one from nothing. Selected in the Explorer, an
effect runs in the Viewer above its settings, and what is changed there is saved as it is
changed. In code: `effectPreset("Fire")`, `readEffect(text, emitter)`, `writeEffect(emitter)`.

## The game's interface

**Create > UI** (the + of the Hierarchy) makes a piece of interface: a Text, a Button, a
Panel, an Image, a Bar, a Gauge, a Stick, a Round Button. Touch controls are interface like the rest,
in the same UI category. Each is an entity with one component.
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
- **Gauge**: a shared value as a dial, with numbers, a red zone and a needle: speed, revs, boost.
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

A **Layout** (Create > UI > Layout) places the pieces of interface under it in the Hierarchy,
one after the other: in a column, a row (`arrangement` 0) or a grid of `columns` (2). Their order
is the order they are in on the screen, so in the Game view a piece dragged above another takes
its place. **Create > UI > Menu** makes a column with three buttons in it, as wide as it.

- `padding` is the room inside its edge, `spacing` the room between two pieces; `alignment`
  puts them at the start, the middle or the end across.
- `stretches` makes them as wide as the column (as tall as the row, as big as a cell).
- `fitsContent` makes the layout as long as what it holds; a piece that is switched off takes no room.
- `scrolls`: what does not fit is dragged into view, with a finger or the mouse, or with the wheel;
  only what is inside it is seen and can be pressed. A thin bar at its side says where one is.
- Layouts go inside layouts: a row of icons in a column of a menu.

A **Slider** (Create > UI > Slider) sets a shared value between `least` and `most` by dragging
its bar, by `step` if it has one; `isSaved` keeps it between plays as a preference of that name.

A **Text Field** (Create > UI > Text Field) is a box to type in: a name for the best laps, a
code. Click or tap it and type; Return lets it go. What is typed is a shared text, shown by any
text as a value is: `Hello {playerName}`. While it is typed in, the letters are the field's and
not the game's (W does not drive the car), and a phone shows its keyboard. `isSaved` keeps it
between plays. From code: `world.text("playerName")`, `world.setText(...)`.

Texts and buttons can each have their own **font**: one made by `Tools/bake_font.py` and put in
Assets (`Fonts/Title.png` and `Fonts/Title.txt`), named without its ending: `Fonts/Title`.

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

A glTF file with several materials is several meshes: `Models/car.glb#0`, `#1`, … one for each material, in the order the file
names them. Give each its own entity and material, as the Driving project does for its car (paint, glass, chrome, lamps), and a
script can then change one of them alone: the rear lamps glow when the brakes are on.

## Build and Export

**Game > Build and Export...** (Shift+Cmd+B) goes from a project to a game that runs by
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

**Ragdolls.** A **Ragdoll** (Add Component > Characters) beside the Animator makes the
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
