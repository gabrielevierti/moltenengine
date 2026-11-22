# Getting Started

Molten Metal Engine makes games for the Mac, the iPhone and the iPad. This page goes from
nothing to something you can play, in about ten minutes. Everything else is in **Making
Games**, the next document in this list.

## 1. A project

The editor opens on its project window: your projects at the left, **New Project** above them.
Give the project a name, leave it empty or start from a template (a car, a plane or a boat to
drive; a figure to walk with, seen from behind; a rifle, seen from your own eyes; a garden to
walk about with others, each on their own machine), and choose **Create Project**. A project is a folder: `Assets` holds everything
the game is made of (scenes, models, pictures, sounds, scripts), `Settings` what you set in
Project Settings.

Choosing a project in the list shows what is in it before you open it: its scenes, its files
and folders, how much of each kind of thing it has. **Open Project** opens it on the scene the
game starts with, and the **Open** beside a scene opens it on that one. **Delete...** moves the
project's folder to the Trash, from where it can be put back.

## 2. The windows

- **Scene**, in the middle: the world, to look around in and arrange. Hold the right mouse
  button and use W A S D to fly; Alt and drag turns round what you look at; the wheel goes
  forward and back; F goes to what is selected.
- **Game**, the tab beside it: what the player sees, through the scene's camera.
- **Hierarchy**, at the left: everything in the scene, as a tree. Drag one thing onto another
  to put it under it.
- **Inspector**, at the right: what the selected thing is made of, its components.
- **Explorer**, at the bottom: the project's files. **Console**, beside it: what the engine
  says. An error is red, and the last thing said is always in the bar along the bottom.
- **Run**, **Hold** and **Frame**, at the top right: play the scene here in the editor, freeze
  it, advance it one frame. What you change while it runs is undone when you stop.

Every command is in the menus with its keys beside it, and **Cmd+K** finds any of them by
name, together with every entity and every file.

## 3. Things in the scene

**Entity > 3D Object > Cube** makes a cube (the **+** of the Hierarchy has the same menu).
The handles move it; **W**, **E** and **R** choose between moving, turning and sizing.
**Cmd+D** makes another, **Cmd+C** and **Cmd+V** copy and paste, **Cmd+G** puts what is
selected in a group. **Entity > 3D Object > Plane** is a floor, ten meters a side, that
things stand on.

A new scene already has a sun and a camera. **Window > Rendering > Lighting** has the sky,
the clouds, the fog and the shadows.

## 4. Components make things do something

A thing does what its components do. Select the cube, **Add Component** at the bottom of the
Inspector (or the Component menu), and choose **Physics > Rigidbody**: press **Run** and it
falls onto the floor. Stop, and it is back where it was.

The engine comes with components for most of what games do: a character that walks and
jumps, things to collect, health and damage, doors and platforms that move, things that
spawn, cars, planes and boats, an interface of buttons and texts. **Entity > Characters** and
**Entity > Gameplay** make the usual ones in one click, already set up:

- **Third Person Player**: a character you walk with W A S D, run with Shift and jump with
  Space, and a camera that follows it and turns with the mouse.
- **First Person Player**: the same seen from its own eyes: the mouse looks about, with no
  button held (Esc gives the pointer back).
- **Collectible**: a coin that adds one to the score when the player touches it.
- **Trigger Zone**: an invisible box that does something when the player walks into it
  (shows or hides a thing, plays a sound, loads a scene...), chosen from lists in the Inspector.
- **Goal**: says "You win!" when the score reaches a number, and can go on to the next scene.

## 5. Your own rules

When the ready-made components are not enough there are two ways to write your own, and each
can be turned into the other:

- **Visual scripts**: boxes joined by wires. **Assets > Create > Visual Script**, then drag it
  onto an entity. *On Update*, *On Collision*, *On Action Pressed* start things; *Translate*,
  *Destroy*, *Spawn*, *Play Sound* do them. See **Visual Scripting**.
- **C++**: **Assets > New C++ Script...** makes a component with `start` and `update` to fill
  in. Saving the file compiles it and the editor uses it at once. See **Writing Game Code**.

## 6. The game's own settings

**Edit > Project Settings** has what the whole game decides once:

- **Game**: its name, its version, the scene it starts with.
- **Quality**: how well it draws, from Low to Ultra.
- **Input**: the actions the game asks about ("jump", "fire", "moveX") and the keys, buttons
  and sticks that move them. Scripts ask for actions, never for keys.
- **Physics**: gravity, and which layers collide with which.
- **Tags and Layers**: the names you give to kinds of things.

## 7. A game that runs by itself

**File > Build Settings...** makes an application for the Mac, or an Xcode project for the
iPhone and the iPad, from the project as it is.

## If something goes wrong

- The **Console** says what the engine could not do, in words. Start there.
- **Cmd+Z** undoes anything done to the scene. The editor also keeps a copy of unsaved work
  every two minutes: if it ever closes badly, opening the scene again offers it back.
- A file named `editor.log` beside the editor's program (in its `build` folder) keeps
  everything the Console said in the last run.
