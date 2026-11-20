# Making a game

This is the shortest way from an empty editor to something you can play: what comes with the engine for the things nearly every game needs, and how the pieces fit. None of it needs code. All of it can be replaced by code, a piece at a time, when a game outgrows it ([GameCode.md](GameCode.md), [VisualScripting.md](VisualScripting.md)).

## Start from a game

The project window offers, next to **Empty**, three small complete games. Pick one, give it a name, and the new project is a copy of it that you own: open the scene, press Play (Cmd+P), then take it apart.

| Template | What it is | What it shows |
| --- | --- | --- |
| **Roll a Ball** | Roll around an arena and collect the coins. | Physics, pickups, a camera that follows, a score, winning. One pickup is a visual script. |
| **Space Shooter** | Fly left and right, shoot what comes down. | Prefabs, spawning, health and damage, explosions, lives, starting over. |
| **Brick Breaker** | Keep the ball in play and clear the wall. | A ball that bounces forever, a paddle, things that break, losing and winning. |
| **Driving** | A small circuit to lap against the clock in a Fiat 500: lights, exhaust smoke, a dashboard, touch controls. | A machine simulated from its parts: springs, tires, engines and a gearbox, with sound. |
| **Flying** | A light aeroplane, a runway, hills. | Flight from lift and drag on each surface: take off, stall, land. |

The example games are not part of the engine: they are a separate download, `MoltenProjects.zip`, to unzip anywhere and open as projects (a new project in the editor starts empty). They are written by `Tools/make_templates.py`, and every one is played from start to finish by the unit tests (`Tests/TemplateTests.cpp`) with the keys a person would press, without a screen.

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

### Names

Components find each other by name: `Follow` follows the entity called `Player`, `Damage` with `onlyTo` set to `Enemy` hurts everything whose name begins with `Enemy`. Spawned copies keep the prefab's name, so a rule written for `Enemy` holds for all of them.

## Materials

A material is an asset: a `.material` file in the project, made with **+ New > Material** in the assets (or from a mesh's Material menu). The first thing it says is its **shader**, which program draws the surface: *Lit* (color, metal, roughness, lit by the scene) or *Unlit* (its own color and nothing else). Select a material and the inspector shows its properties; select an entity and the material of its mesh is at the bottom of the inspector. Either way it is the same material: change it and everything that uses it changes, in every scene.

Give a mesh a material by dragging it from the assets onto the object in the view, or into the mesh's **Material**, or by choosing it from the list there. A scene made before materials were assets keeps its own; **Save This One as an Asset**, in the same menu, turns one into a file.

## Input

The **Input** window lists the game's actions: what the player can do (`moveX`, `moveZ`, `jump`, `fire`, `pause` to begin with) and which keys, mouse buttons and controller parts do it. It is saved in `Assets/Input.actions` and ships with the game. Components and code ask for actions and never for keys:

```cpp
float x = world.input.axis("moveX");
if (world.input.wasPressed("jump")) { ... }
```

`PlayerMover` moves by `moveX` and `moveZ` and jumps on `jump`: rebind them in the window and it follows. The bar under each action shows what it reads right now.

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

Controls write what is wanted into `VehicleBody::controls` (throttle, brake, steer, pitch, roll, yaw, flaps); parts read it. Your own code can write there instead: `body->setControls(...)`. `kit::makeCar` and `kit::makeAirplane` build one of each from code. The Driving and Flying templates are these parts with models on them.

Both read their numbers from the inspector (mass, spring rates, wing area, power), so a van, a go-kart, a glider or a jet trainer is a matter of changing them.

## Prefabs

A prefab is a piece of scene in a file: an entity, what is under it, and the materials it uses. Right-click an entity in the hierarchy and choose **Save as Prefab**; it goes to `Assets/Prefabs`. Drag a prefab from the asset browser into the 3D view to place it. `Shooter`, `Spawner` and the `effect` fields of other components spawn prefabs while the game runs; so does the **Spawn** node, and in code `world.spawn("Prefabs/Enemy.prefab", position)`.

A prefab is read when it is used: change the file and the next one spawned is the new one. An entity already placed in a scene is a copy and does not follow.

## Effects

**Create > Effects** has fire, smoke, sparks, an explosion, rain and snow. Each is an entity with a particle emitter you can then change in the inspector (fire also carries a light with `Flicker`; an explosion a light, `Explosion` and `Lifetime`). Save one as a prefab and it can be the `effect` of a `Health` or a `Collectible`.

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

**Layouts.** The menu at the right of the play bar (and Window > Layouts) has four arrangements, Default, 2 by 3, Tall and Wide. Arrange the windows as you like, choose Save Layout, give it a name: it joins the list, on this Mac, for every project.

In the scene view a camera is drawn as the pyramid of what it sees, a directional light as its rays, a spot light as its cone. A camera's scale changes nothing about its picture.

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
the **Joystick Axis** of an action. The standard actions start with a radio's usual order: axis 0 `moveX` (roll), 1 `moveZ` (pitch),
2 `throttleLever`, 3 `yaw`. `throttleLever` is a throttle that says where it is; `FlightControls` uses it instead of the keys when a
joystick is there.

## A game's own scripts

A project's scripts are in `Assets/Scripts`, where the Explorer shows them: click one to read it in the Viewer, with the component it
declares and a button to add it to the selected object. The templates' own are there: `Follow.cpp`, `DriveControls.cpp`, `Motor.cpp`,
`FlightControls.cpp`. They are built when the project opens and whenever a file changes. (A `Source` folder beside Assets, where
scripts were before, is still built.) A script with the name of a component that comes with the engine takes its
place; if the code does not build, the engine's own is used, so the game still runs. `lateUpdate(World&, Entity, float)` runs after every
`update`, with everything where it will be drawn: cameras that follow belong there.

A visual script can be written out as C++: select it in the Explorer and press **Convert to C++ Script** in the Viewer. Each box becomes
one `call("...")`, in the same order, in `Source/<Name>.cpp`. The other way cannot be done in general.

## Water, mist and traced light

- **Water** is a material: choose the shader *Water* (waves cross it, it mirrors the sky at a glancing angle). `Buoyancy` on a body makes it float at a water level.
- **Fog** has *Thins With Height* (it lies low, as mist does), *Glow Toward Sun* and *Light Shafts*, in Lighting > Environment:
  with shafts the fog glows only where the sun reaches it, and what stands in the sun's way casts beams of shadow through the mist
  (with shadow maps, not with ray traced sun shadows).
- **Clouds Have Depth** (Lighting > Environment > Sky) makes the clouds volumes one looks through, lit from the sun's side.
- With **ray traced sun shadows** on (Lighting > Shadows): *Ambient Light* hides the sky's light where something is really in the way;
  *Bounced Light* is global illumination, one bounce: the same rays bring back the light of what they hit, so a red wall in the sun
  tints the floor beside it; *Reflections* makes smooth surfaces mirror what is around them. What a ray hits is seen in its material's
  color (not its textures), lit by the sky and the sun. More *Ambient Rays* give less grain: there is no denoiser yet.

These shaders are compiled first with the extras and, if that fails on a system, again without: the console says which.

## Bodies that turn, and joints

A `RigidBody` with **Rotates** on turns when it is hit off-center, rolls down slopes and tumbles; boxes rest and stack flat. Off (as
bodies were before), it keeps the way it faces: what a character that walks wants. `body.applyImpulse(impulse, point)` pushes at a point.

`Joint` ties a body to another by name, or to the world: `ball` (joined at a point), `hinge` (turns about one axis: a door), `fixed`
(welded), `distance` (a rod, or with **Is Rope** a rope that only pulls). **Break Force** lets it give way.

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

## Liquid

**Liquid** on an entity with a Particle System is liquid made of drops: a block of it (`fill`) that collapses and fills whatever colliders
hold it, or a stream (`pourRate`) poured from the entity. `dropRadius` sets how fine it is; `viscosity` goes from water to honey. The
Particle System's size and color are how the drops are drawn: a size about three times the drop's radius reads as liquid. A couple of
thousand drops are comfortable on the processor; it is not a sea.

## Colliders

**Box Collider**, **Sphere Collider** and **Capsule Collider** are under Add Component > Physics; each takes the size of the object's
mesh when added. An object has one collider. A collider shaped like the mesh itself is not there yet.

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
Panel, an Image, a Bar, a Touch Stick, a Touch Button. Each is an entity with one component.
They are laid out in the **Game** view, where they are seen as the player will see them:
click a piece to choose it, drag it into place, pull its lower right corner to resize it.

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
- **Touch Stick**, **Touch Button**: for phones and tablets, where there are no keys. The
  stick pushes two axis actions (`moveX` and `moveZ` unless told otherwise), the button
  holds one (`jump`): a game written for keys is played by touch without changing a line.

All of them work with the mouse as with a finger, so they are tried on a Mac: press Run,
click in the Game view, and press, drag and click them there. A game that also has keys
can hide its touch controls until the screen is touched (**Only On Touch Screens**).
From code: `input.setVirtualAxis(...)`, `input.setVirtualButton(...)`.

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

### The icon

A game wears the engine's icon until it is given its own: in the Build window's first step, "Choose a Picture…" (a square PNG,
1024 pixels is best) copies it into the project as `Assets/Icon.png`, and "Use the Engine's" goes back. The Mac application gets an
`.icns` made from it, the iPhone and iPad application the sizes iOS asks for.
