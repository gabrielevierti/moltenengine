# Roadmap

What Molten is, and stays: a game engine for Apple hardware only, on Metal, with games in C++ (and visual
scripts), made in a Unity-like editor. One person builds it, so every item has to earn its place: it goes in
when a game needs it.

`[x]` done · `[~]` started, works, not finished · `[ ]` to do

---

## Already there

The base the rest builds on. Each `[~]` says what it still lacks.

**Core**
- [x] Entities and components, with fields the editor reads (reflection: `MOLTEN_FIELD`)
- [x] Scenes saved as text and read back; prefabs
- [x] Game code in C++ built into a library and reloaded while the editor runs
- [x] Visual scripts, convertible to C++ and back
- [x] Plugins for the editor
- [x] Logging and a console; preferences kept between runs
- [x] Asset database: identifiers that survive renames and moves (`.meta`), import settings

**Rendering**
- [x] Physically based materials (lit, unlit, water) and hand-written surface shaders
- [x] Cascaded shadow maps; frustum culling
- [x] HDR, tone mapping, bloom, vignette, ambient occlusion
- [~] Hardware ray tracing: sun shadows, reflections, one bounce of light — [ ] denoiser, point and spot light shadows
- [~] Sky and clouds, height fog with shafts of light
- [~] FFT ocean with foam, floating
- [~] CPU particles with presets, streaks (rain) — [ ] on the GPU
- [~] LOD Groups: a near and a far model — [ ] more levels, made automatically, cross-fading
- [x] Quality levels and render scale

**Physics**
- [x] Rigid bodies, contacts, joints (ball, hinge, fixed, distance, rope, breaking)
- [x] Sphere, box, capsule, mesh and convex colliders (GJK and EPA)
- [x] Raycasts; buoyancy; liquids made of drops
- [x] Vehicles from parts: springs, tyres that slide, engine and gearbox, wings and propellers

**Audio**
- [x] Looping voices with pitch and volume, layered engines
- [~] Sounds placed in the world (volume and pan) — [ ] reverb, occlusion, buses

**Animation**
- [~] glTF skeletons, CPU skinning, clips with easing between them

**Interface and input**
- [x] Text, buttons, panels, images, gauges, touch buttons and stick; anchors; scales with the screen
- [x] Keys, mouse, touch, joystick axes; input actions remappable in the editor

**Editor**
- [x] Scene and Game views, docking, saved layouts, Hierarchy, Inspector, Explorer with previews, Viewer, Console
- [x] Move, rotate, scale gizmos; snapping; grid; several objects edited together; undo and redo
- [x] Run, Hold, Frame; the interface laid out by dragging in the Game view
- [x] Material editor, effect editor, visual script editor
- [x] Export to iPhone and iPad from the editor, with the game's icon

**Platforms**
- [x] macOS
- [~] iOS and iPadOS — [ ] orientation and safe areas, haptics

**Examples**
- [x] Driving: a circuit, a Fiat 500 from its parts, four rivals, start lights, pit stops, weather, menus, touch controls

---

## Phase 1 — Fast, and shippable

What the Driving game needs now: it runs at 30–40 fps in the editor with 1000 draw calls, and it cannot yet
reach the App Store.

- [x] **CPU profiler**: time per part of the frame and per kind of component, five seconds of history, a frame held to
      look at (Window > Profiler); the slowest parts in the F1 overlay of a running game
- [~] **GPU profiler**: the GPU's time per frame — [ ] per render pass (Metal counters), draw calls and triangles per pass
- [x] **GPU instancing**: runs of the same mesh and material are drawn as one instanced draw (it was there already)
- [~] **Precompiled shaders**: the build compiles them (target `molten-shaders`, Tools/compile_shaders.sh) when the Metal
      compiler is installed; a mistake stops there, not as a magenta screen; the engine loads them when no source is newer.
      [ ] For iPhone too; a pipeline cache (binary archives)
- [~] **MetalFX upscaling**: below render scale 1, the picture is made the screen's size by MetalFX's spatial scaler.
      [ ] The temporal scaler with temporal anti-aliasing (it needs motion vectors and a jittered camera)
- [x] **Dynamic resolution**: the scene drawn smaller while the GPU is behind the target frame rate, larger again when it is not
- [x] **Signing and stores**: from the Build window, an iPhone game archived and sent to App Store Connect (TestFlight, then
      review); a Mac game signed with the Developer ID and notarized. [ ] The Mac App Store
- [~] **Development and release builds**: a Mac export marked Development turns on F1 (statistics and the profiler) and the
      other developer keys. [ ] The same for iPhone
- [x] **Save games**: `SaveGame` with numbers, words and positions by name; slots as files beside the preferences
- [x] **Game controllers**: on the Mac through GLFW, on iPhone and iPad through the Game Controller framework, mapped to the
      same input actions as keys and touch
- [x] **Thermal state and Low Power Mode**: `world.deviceHeat()`, `world.isSavingBattery()`; hot, the scene is drawn smaller by itself

## Phase 2 — Games with characters

The next game after Driving: someone who walks, runs and jumps.

- [x] **Character controller**: a capsule moved, not pushed: slides along walls, climbs steps (their edges hold it as
      it goes up), stands on slopes it can walk and slides off steeper ones, shoves bodies that move
- [x] **Character mover and orbit camera**: walk and run where the input says, seen from the camera; jump, with a moment's
      grace after an edge; a camera turned with the mouse or the right stick that comes closer rather than go through a wall.
      [x] Blending from one camera to another: Camera Shots and a Camera Director (Create > Camera Shot)
- [x] **Animator with states**: a `.animator` file of parameters, states (one clip, or a blend tree of clips by a number,
      kept in step), and transitions on conditions, after an exit time, eased over a time of their own; the character mover
      sets `speed`, `grounded`, `verticalSpeed` and `jump` by itself
- [x] **Animation events**: named moments of a state, sounds played, the rest for the game. [x] Root motion
- [ ] **GPU skinning**, and **blend shapes** (faces)
- [x] **Two-bone IK**: a foot or a hand on a target, the knee or elbow toward a pole
- [x] **Navigation**: a Navigation Area finds the walkable ground (slope, steps, room for the agents' size); paths by A*,
      pulled straight; Nav Agents walk them, with their Character Controller if they have one
- [x] **Guards**: a state machine on a Nav Agent: a round of points, chasing what it sees (distance, angle, nothing in
      between) or hears, searching where it was last seen, going back
- [x] **Prefabs by reference and variants**: a prefab placed in a scene, or in another prefab, is a copy that follows it
      (Unpack Prefab makes it the scene's own); a variant is its base with some values changed (Create Variant).
      [x] A copy's own changes: a part changed in the Inspector or moved in the Scene view keeps it, saved with the copy
      [ ] Overrides on a placed copy, edited in the Inspector

## Phase 3 — How it looks

- [ ] **Reflection probes and light probes**: lighting for scenes and devices without ray tracing
- [ ] **Baked lighting** for static scenes
- [ ] **Clustered lights**: many point and spot lights at once
- [ ] **Decals**: skid marks, puddles, dirt, without extra meshes
- [ ] **Depth of field, motion blur, colour grading (LUT), auto exposure**
- [x] **A day and night cycle**: the sun crosses the sky, warms at dawn and dusk, a moon at night; sky and fog follow (Day and Night)
- [ ] **Physical sky** (scattering in the shader), stars at night
- [ ] **Particles on the GPU**: many more, with textures, trails and collisions
- [ ] **Ray tracing denoiser**; shadows of point and spot lights traced
- [ ] **Material graph**: materials made from nodes, beside the hand-written shaders
- [ ] **Water**: refraction and depth colour, shores

## Phase 4 — Working in the editor

- [x] **Search everywhere and a command palette** (Cmd+K): actions, windows, Create, entities (with where they are), assets, plugins' menu items
- [x] **Asset dependencies**: what uses what, shown in the Viewer; unused assets found
- [x] **Hot reload of every asset**: textures, models, sounds, materials as they change on disk, in the editor (shaders already were)
- [x] **Timeline**: things moved along keys, camera cuts, sounds, values, shown and hidden, animation triggers; Window > Timeline to make them
- [x] **Terrain**: heights kept in a file, brushes (raise, lower, smooth, flatten) in the Scene view, collision
- [ ] **Terrain layers**: grass, rock, sand painted on it (needs a shader that blends them: with Phase 3)
- [x] **Foliage**: grass, trees, rocks painted with a brush, drawn as copies of one mesh, faded out with distance
- [ ] Foliage moved by the wind (a shader: with Phase 3)
- [x] **Splines**: Spline (points = children), Spline Follower, Spline Road with its collider; drawn and laid in the Scene view
- [x] **Interface layout groups**: rows, columns, grids, lists that scroll; sliders
- [x] **Text fields and fonts per text**: typed names kept between plays, the iPhone keyboard, a font of Assets per text and button
- [x] **Localization**: texts in a table per language (`Assets/Localization/it.txt`), chosen in the game or by the system's language
- [x] **Git in the editor**: changed files and how, commit, history, pull and push (Window > Source Control)
- [ ] Scenes written to merge well (parents by name, not by number)
- [ ] **Frame debugger**: the passes of one frame, each one's result, step by step
- [x] **Keyboard shortcut editor**, editor themes (Edit > Editor Settings)

## Phase 6 — Making games faster

What every game would otherwise write for itself, in the order they are done.

- [x] **Messages between scripts**: `world.send("doorOpened")`, heard by components (onMessage), by code that listens, by visual scripts (On Message, Send Message)
- [x] **Waiting**: `world.after`, `every`, `when`, and coroutines (`co_await world.seconds(2)`); Wait and Wait Until nodes
- [x] **Triggers and actions without code**: When / Then (Add Component > Logic): when something enters, a message, a value, a click, every few seconds; then up to four things
- [x] **Saving the state by itself**: a Saveable component; places, values, what is on and off, what was destroyed (kit::saveState, Save Game node, When / Then)
- [x] **Pools and spawners**: world.take / release; Spawner in waves, at most so many about; Lifetime puts back
- [x] **Scenes loaded in the background**, with a Loading Screen and "{loading:percent}"; scenes added on top of others (addScene)
- [x] **2D blend trees**: walking and running in every direction, mixed
- [x] **Animation layers with masks**: the body does one thing, the legs another
- [x] **An audio mixer with groups**: music, effects, voices, interface; volumes; the music lowered under voices
- [x] **Reverb zones**, and sounds muffled behind walls
- [x] **Editing many at once** in the Inspector
- [x] **Searching the Hierarchy** by name, component and tag
- [x] **Snapping**: to vertices, onto surfaces; aligning and spacing many
- [x] **Debugging visual scripts**: values on the wires as it runs, stopping at a node
- [x] **What a scene weighs**: triangles, textures, memory, draw calls by object
- [x] **Ragdolls** from a character's bones
- [x] **Floating**: things and boats on water
- [x] **Ropes and chains** ready made from joints
- [x] **Assets made smaller when built**: pictures for iPhone and Mac, sounds in 16 bits, the art programs' files left out (models as they are)
- [x] **Color grading** with a lookup table (Assets/Grading/Neutral 32.png to start from), and **exposure that adapts**
- [x] **Decals**: a picture projected onto what is under it, darkening (dirt, marks) or painted
- [x] **Depth of field** and **motion blur** of the camera
- [x] **Grass and trees moved by the wind**: a material's Sway, with the scene's wind
- [x] **A sky from the air itself**, with **stars** at night

The five above are drawn by the GPU: written and built, to be seen on the Mac.

## Phase 5 — Apple, further

- [ ] **Game Center**: achievements, leaderboards (best laps), friends
- [ ] **iCloud saves**
- [ ] **Haptics** on iPhone and controllers
- [ ] **Metal 4** where it helps: argument tables, the new command structure
- [ ] **Indirect command buffers and GPU-driven culling**, when scenes are large enough to need them
- [ ] **visionOS**: the game in a window, then immersive
- [ ] **Spatial audio** through PHASE: real 3D sound, occlusion, reverb from the room

## Later, when a game needs it

- [ ] Audio buses, effects, reverb zones, ducking, adaptive music
- [ ] Ragdolls, cloth, ropes with links, soft bodies, destruction
- [ ] Continuous collision detection (fast bullets)
- [ ] Multiplayer: start from Game Center matches between friends; servers, prediction and rollback only for a game made for it
- [ ] Occlusion culling, HLOD, world streaming, additive scenes — for large worlds
- [ ] FBX and USD import (glTF covers most models today)
- [ ] Compressed textures (ASTC), streamed audio, asset bundles
- [ ] A 2D engine: sprites, atlases, tilemaps, 2D physics and lights — almost an engine of its own; only for a 2D game
- [ ] Modding support

## Not planned

These go against what Molten is, or are a company and not an engine.

- Windows, Linux, Android, WebGPU — Molten is Apple only, and that is its reason to exist
- C#, .NET, managed scripting — games are written in C++ and visual scripts
- Asset and plugin marketplaces, creator revenue, review systems, forums, developer portal
- Motion matching, crowd simulation, virtual texturing — AAA tools, for a team and an open world
