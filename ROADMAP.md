# Roadmap

Where Molten is going, in the order it is going there.

Two rules shape the order:

1. **Each milestone ends with something you can play or use**, not with a layer waiting for the next one.
2. **What everything else stands on comes first.** Ray tracing, clouds and water are the visible goals, but they all need a renderer that can schedule many passes and a material system that does not care how many textures a scene has. An editor that cannot hold a real project makes every later feature slower to build and to show.

Milestones are not dated. A few of them are large enough that they will take months each.

---

## 0.1 — A repository of its own, the editor's foundations, and a first slice of everything ✅

*What is in the repository now.*

- One engine, C++ only. The Swift twin of the tutorial engine is gone; Marble and FPV were its samples (since removed: the example projects took their place).
- Projects: create, open, recent list, settings, a folder structure made for version control.
- Asset database with identifiers that survive renames (`.meta` files); asset browser with import, move, rename, Trash.
- Hierarchy with drag-and-drop parenting, multi-selection, search, rename.
- 3D view with move / rotate / scale / universal gizmos, world or local, snapping in meters, degrees and scale; view cube; fly, orbit, pan, frame.
- Inspector built from registered component editors; materials and render settings panels; console.
- Undo for everything, play mode in place, dockable layout.
- Editor plugins in C++: panels, menus, component editors, create entries, viewport overlays.
- **Game code in the project**: components declared in C++ with their fields, shown, saved and run; rebuilt and reloaded from the editor; a player; export of a Mac application. *(The first part of 0.3.)*
- **Scripts**: New Script and rebuild-on-save for C++; **visual scripting** with a node editor, about forty nodes and live editing during play. *(Ahead of 0.3 and of "after 1.0".)*
- A splash screen for the editor and for exported games.
- **iPhone and iPad**: the platform layer, touch, and the player as an iOS application built with Xcode. *(The first part of 0.7.)*
- **Ray tracing**: acceleration structures for every mesh and for the scene, and ray-traced sun shadows with soft edges. *(The first part of 0.8.)*

## 0.2 — An editor that holds a whole game

*Done when: a project with a few hundred assets and several scenes can be worked on for a week without touching a text editor for anything but code.*

- **References by identifier.** Scenes refer to meshes and textures by GUID, not by path, so that renaming never breaks a scene.
- **Materials as assets** (`.material` files) shared between scenes; a material editor with a live preview sphere.
- **Prefabs**: an entity tree saved as an asset, instanced in scenes, with overrides.
- **Import pipeline.** glTF 2.0 (cgltf) with hierarchy, materials and several meshes per file; textures compressed to ASTC with mipmaps at import, cached in `Library/`; import settings in the `.meta`.
- **Thumbnails** for models, materials and prefabs, rendered off screen and cached.
- **Hot reload** of textures, meshes and shaders while the editor runs.
- **Several scenes** open at once, additive loading.
- **A game view**: while playing, the game's own canvas and its mouse input are mapped to the view's rectangle instead of the whole window.
- **Inspector**: editing several entities at once; copy and paste of components; undo by command instead of whole-scene snapshots once scenes get large.
- **Selection outline** by stencil instead of bounding boxes; picking by ID buffer, exact to the pixel.
- **The editor as a real Mac app**: an `.app` bundle with an icon, native menu bar, document handling, per-project layout.
- **Plugins**: asset importers and custom asset types; hot reload of a plugin when its library is rebuilt.

## 0.3 — Gameplay in C++, and a game you can ship

*Done when: a game made entirely in the editor, with its own C++ components, exports to a signed `.app` that runs on another Mac.*

- ✅ **The game module.** A project's `Source/` folder builds into a dynamic library the editor loads; components are declared with one macro.
- ✅ **Reflection.** Fields declared once appear in the inspector, are saved in scenes, and are undoable.
- ✅ **Reload of game code** in the editor: rebuild, and the scene continues with the new code.
- ✅ **A game without code.** Ready-made components for moving, following, collecting, health and damage, shooting, spawning, score and goals; collision and trigger events for components and visual scripts; shared values; scene changes and restart.
- ✅ **Prefabs**: save part of a scene, place it, spawn it while the game runs. Still to come: placed copies that follow their prefab, overrides, nesting.
- ✅ **Project templates**, each played start to finish by the tests without a screen.
- **Visual scripts, next**: undo on the board, multi-selection, copy and paste, zoom, comments; nodes for other entities, collisions, timers, loops and sound; scripts as functions of other scripts.
- Field types still to come: references to entities, assets and prefabs; enumerations; lists.
- Building in the background, with the editor responsive and errors that jump to the line.
- Keeping the running state across a rebuild while playing (today a rebuild stops play).
- **Systems and scheduling**: an order for update, fixed update and late update; events between systems.
- **Input assets**: actions and bindings edited in the editor, for keyboard, mouse, touch and controllers.
- ✅ **A player and a first export**: the project, its code and the engine put together as a macOS `.app`, signed for the Mac it was made on.
- **Export for release**: assets packed into archives, shaders compiled to a `.metallib` ahead of time, Developer ID signing and notarization, an icon.
- **A job system** on Grand Central Dispatch with performance and efficiency cores in mind; frame pacing with `CAMetalDisplayLink`.
- **Profiler panel**: CPU scopes, GPU pass times from Metal counters, memory.

## 0.4 — The renderer Molten is for

*Done when: a scene with ten thousand objects, hundreds of lights and thousands of textures runs at the display's refresh rate on a MacBook Air, and adding a rendering feature means adding a node to a graph.*

- **Render graph**: passes declare what they read and write; the graph orders them, allocates and aliases transient targets, and merges passes into single tile passes wherever Apple GPUs allow.
- **Metal 4**: command allocators, argument tables, residency sets, pipeline compilation ahead of time; the Metal 3 path kept while macOS versions require it.
- **Bindless materials** with argument buffers: one draw path whatever the number of textures.
- **GPU-driven rendering**: culling in compute, indirect draws, meshlets with mesh shaders, occlusion culling against the previous frame's depth.
- **Many lights**: clustered forward shading, using tile shaders and imageblocks to keep it in one pass on TBDR.
- **Material graph** in the editor, generating Metal shaders; shader variants compiled and cached in binary archives.
- **Temporal anti-aliasing and MetalFX**: upscaling, and frame interpolation where the OS offers it.
- **HDR output** on XDR and EDR displays, with correct tone mapping for each.
- **Shadows**: point and spot light shadows, contact shadows, a shadow atlas with caching.
- **Reflection probes and screen-space reflections** as the baseline that ray tracing later improves on.

## 0.5 — 2D as a first-class citizen

*Done when: a complete 2D game (a platformer with tile maps, animated sprites, lights and a pause menu) is made in the editor.*

- Sprite renderer with atlases and batching; sprite and animation editors.
- Tile maps with auto-tiling, and an editor for them.
- 2D physics (Box2D) with the same components and callbacks as 3D.
- 2D lights and shadows; pixel-perfect camera.
- A runtime UI system with layout, anchors and an editor: what a game's menus and HUD are made with.
- Text: any font, rich text, right-to-left and CJK through Core Text shaping.

## 0.6 — Physics, animation, sound

*Done when: a third-person character runs, jumps and pushes things through a level with sound that comes from where it should.*

- **Jolt Physics** replaces the arcade solver: rotation from impacts, joints, continuous collision, mesh and convex colliders, vehicles, ragdolls. Deterministic fixed step with interpolation, as now.
- **Character controller.**
- **Skeletal animation**: import from glTF, GPU skinning, blending, state machines and blend trees with an editor; root motion; inverse kinematics.
- **Timeline** for cut scenes and property animation.
- **Audio**: spatial sound with Apple's PHASE (occlusion, reverb zones, head tracking with AirPods), streaming music, a mixer with buses and effects.
- **Navigation**: nav mesh baking and path finding (Recast and Detour).

## 0.7 — iPhone and iPad

*Done when: the same project exports to macOS, iPadOS and iOS, and a touch game runs on an iPhone from the editor's Build menu.*

- ✅ An iOS and iPadOS platform layer (UIKit, `CAMetalLayer`, a display link) beside the macOS one; GLFW stays a Mac-only detail behind `Platform/`.
- ✅ Touch, with the first finger doubling as the mouse. ✅ The player as an iOS application, with the project's code compiled in.
- Gestures, Apple Pencil, game controllers, the keyboard, haptics, motion sensors.
- Building and running on a device from the editor's Game menu.
- Screens: safe areas, ProMotion, orientation changes, split view and Stage Manager on iPad, and folding or dual-screen iPhones as layouts that change at run time.
- Xcode project export with signing, App Store packaging, on-demand resources.
- Budgets: thermal state and low power mode feed the renderer's quality settings; memory limits enforced in the editor's build report.
- Game Mode, Game Center, iCloud saves, StoreKit as optional modules.
- visionOS afterwards, as a compositor target of the same renderer.

## 0.8 — Ray tracing

*Done when: one switch replaces shadow maps, screen-space reflections and ambient occlusion with ray-traced versions on every device that supports Metal ray tracing, and the difference shows in a sample scene.*

- ✅ Acceleration structures: one per mesh, built once, and one for the scene, rebuilt every frame from the objects that cast shadows.
- ✅ **Ray-traced sun shadows**, soft, in the forward pass. Shadow maps remain where the GPU cannot trace rays.
- Shadows from point and spot lights; cut-out materials casting shadows with their holes (today they cast solid ones when traced); skinned meshes by refitting.
- **Reflections** and **ambient occlusion**, traced.
- **Global illumination**: probe-based first (DDGI), then a ReSTIR path for the fastest hardware.
- Denoising with the MetalFX denoising upscaler; temporal accumulation.
- **Light map baking** on the GPU for devices and scenes where real-time tracing is too costly.
- A reference path tracer in the editor, to judge every approximation against.

## 0.9 — Sky, clouds and volumes

*Done when: a day passes in a sample scene, from dawn to night, with clouds that cast shadows and fog that light shafts cross.*

- **Physically based atmosphere** (sky and aerial perspective from lookup tables), sun and moon, stars, time of day.
- **Volumetric clouds**: ray-marched, with weather maps, self-shadowing, shadows on the ground, temporal reprojection.
- **Volumetric fog and gas**: a froxel grid lit by every light, with shadows and local density volumes; smoke and gas as sparse volumes.
- Exposure adaptation, and the lens effects that depend on it.

## 1.0 — Water, fire and wind

*Done when: a boat rides waves under a storm, a fire spreads smoke the wind carries, and none of it is an animation.*

- ✅ A first step: a global wind with gusts that particles follow, and ready-made fire, smoke, sparks, explosions, rain and snow built on the CPU particle system. What follows replaces them with simulation.
- **Wind** as a field the whole engine reads: global direction and gusts plus local sources; vegetation, cloth, particles, smoke and water all respond to it.
- **Water**: ocean from a wave spectrum (FFT), rivers and lakes with shallow-water flow, buoyancy, foam, refraction, caustics, underwater rendering; reflections from the ray tracer where available.
- **Fire and smoke**: fluid simulation on a GPU grid in compute, rendered through the volume system, lighting the scene around it.
- **GPU particles** in compute, by the million, with collisions against depth and signed distance fields; a visual effect graph in the editor.
- **Cloth and soft bodies.**

## After 1.0

Terrain with sculpting and layers, foliage and procedural placement, world streaming for large maps, networking, localization, accessibility helpers, an asset store format. Their order will follow what the people making games with Molten need first.

---

## What Molten will not do

- **Other platforms.** No Vulkan, no DirectX, no abstraction layer in case there is one some day. Every design decision may assume a tile-based Apple GPU with unified memory.
- **Other languages for the engine.** C++ throughout, Objective-C++ only where a system API demands it, Metal Shading Language for shaders.

## The long list

What a modern engine has, and where Molten stands. `[x]` is there and tested, `[~]` is there in part, `[ ]` is not.
Work goes down this list in the order of the section "Next".

### Next
1. [x] Bodies that turn, contacts at points, joints (ball, hinge, fixed, distance and rope, breaking)
2. [~] glTF import (.gltf, .glb): meshes, materials' factors, skeletons, animations played by `Animator` with easing between clips.
   Still to do: its textures, blend shapes, state machines and blend trees, IK, skinning on the GPU
3. [~] Ray traced reflections that show what is reflected (in its color and light, without its textures or its true facing);
   global illumination as one traced bounce per pixel. Never yet compiled on a Mac. Still to do: a denoiser, probes for scenes without ray tracing
4. [~] Water from an FFT ocean: a moving surface with sharp crests, foam where waves break, things that float on the waves.
   Still to do: refraction and depth colour (they need a copy of the picture behind the water), shore, wakes, underwater, an endless sea that follows the camera
5. [~] Fog walked through from the eye, lit where the shadow maps say the sun reaches: shafts of light. Clouds with depth,
   marched through and lit from the sun's side. Never yet compiled on a Mac. Still to do: fog in froxels (shared by all pixels and
   lit by every light), shafts with ray traced shadows and against the sky, local volumes of smoke and gas, clouds that shade the ground
6. [~] Liquids made of drops (position based fluids) that fill what holds them and pour. [ ] Particles on the GPU, an effects editor
7. [~] Mesh colliders: solid hills, tracks and models as they are shaped (the Flying project's hills are one). [ ] Convex ones, and mesh against mesh.
8. [~] Shaders written by hand as assets (`.shader`: a surface function and its properties), chosen in a material beside Lit, Unlit and Water,
   compiled with the engine's own and again when saved; a mistake is reported and costs nothing else. Not yet seen compiling on a Mac.
   [ ] Their own textures, a vertex stage, unlit and transparent variants, the preview ball drawn with them
9. [~] Import settings for pictures, models and sounds, kept in the file's `.meta` and changed in the Viewer (pictures: color or data, largest size,
   sharp pixels, mipmaps, upside down; models: scale, Z up, centered, smooth normals, faces turned; sounds: mono, rate, normalized, volume).
   [ ] Compressed textures, wrap modes, per-platform settings, glTF materials and animations chosen at import
10. [~] Effects: emitters with shapes (sphere, box, ring), a middle color and size, repeating bursts, a duration, turbulence, bouncing; nine to start
    from (fire, smoke, sparks, explosion, dust, rain, snow, magic, fountain); `.effect` files; an effect editor in the Viewer with a running preview.
    [ ] Particles simulated on the GPU, textured and turning particles, trails, sub-emitters
11. [~] An interface made of pieces (text, button, panel, image, bar, touch stick, touch button) created from the Hierarchy and laid out by
    dragging in the Game view, with anchors; all of them work with the mouse as with a finger. A Build and Export window in steps.
    [ ] Layout groups, lists and scrolling, sliders and text fields, fonts chosen per text, pieces inside pieces
    [~] Exporting for iPhone and iPad from the editor without a terminal; an icon for the editor and one per game, chosen in the Build
    window (the engine's until then), made into `.icns` for the Mac and the icon sizes of iOS. Not yet seen on a Mac. [ ] Orientation and safe areas
12. [~] A glTF file's parts by material as meshes of their own (`Models/car.glb#3`), so each takes its own material.
    The Driving project rebuilt on them: a circuit with curbs, walls, a paddock and lamps, a timed lap, headlights and brake lights,
    exhaust and tyre smoke, brakes shared by the load on each wheel, a dashboard and touch controls

### Rendering
- [~] Ray tracing: sun shadows, ambient occlusion, reflections of objects, one bounce of light. [ ] shadows of point and spot lights, denoiser, textures in reflections
- [~] Global illumination: one traced bounce. [ ] baked lightmaps, probes, DDGI, path tracer for reference
- [ ] Many lights (clustered), [ ] GPU-driven rendering, occlusion culling, automatic LOD
- [~] Shadows: cascades. [ ] contact shadows, virtual shadow maps
- [ ] Temporal upscaling (MetalFX), TAA, dynamic resolution
- [~] Post: bloom, tone mapping, vignette, ambient occlusion. [ ] motion blur, depth of field, colour grading LUT, auto exposure
- [~] Materials: lit, unlit, water. [ ] shader graph, subsurface, clear coat, anisotropy, cloth, hair, refraction, decals, parallax
- [~] Sky, clouds flat or with depth, fog with height and shafts of light. [ ] physical atmosphere, day and night, weather
- [ ] Terrain: heightmap, layers, sculpting, streaming
- [ ] Vegetation: instancing, grass, wind, impostors
- [~] Water: FFT ocean with foam, floating. [ ] refraction, rivers, shore, underwater
- [~] Effects: CPU particles with presets. [ ] GPU particles, node editor, trails, ribbons

### Animation
- [x] Skeletons, skinning (on the CPU), clips with easing between them. [ ] blend shapes, state machines, blend trees, root motion, IK, ragdoll, timeline

### Physics
- [x] Rotation, joints, stacking. [~] Vehicles from parts. [x] Buoyancy at a level
- [x] Liquids made of drops. [ ] Convex and mesh colliders, continuous detection, character controller, cloth, soft bodies, ropes with links, fracture

### Audio
- [~] Looping voices with pitch and volume, layered engines. [ ] HRTF, occlusion, reverb zones, mixer buses, streaming, adaptive music

### Gameplay
- [ ] Navmesh and pathfinding, behaviour trees, save games, localization

### Game interface
- [~] Text and shapes on a canvas that follows the screen's shape. [ ] visual editor with anchors, rich text, gamepad navigation

### Editor
- [x] Scene and Game views, layouts, Explorer with previews, Viewer, tags and layers, joystick input, screen shapes
- [~] Prefabs. [ ] variants and overrides, nested prefabs
- [ ] Profiler, frame debugger, version control, packages

### Assets
- [~] OBJ, glTF, PNG, WAV. [ ] FBX, USD, compressed textures and audio, streaming, bundles

### Platforms and network
- [~] macOS; iOS in part. [ ] visionOS, tvOS, signing and App Store packaging, haptics, multiplayer, Game Center

### Architecture
- [ ] Job system, data-oriented ECS for large scenes, additive scenes and world streaming
