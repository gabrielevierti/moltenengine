# Roadmap

Where Molten is going, in the order it is going there.

Two rules shape the order:

1. **Each milestone ends with something you can play or use**, not with a layer waiting for the next one.
2. **What everything else stands on comes first.** Ray tracing, clouds and water are the visible goals, but they all need a renderer that can schedule many passes and a material system that does not care how many textures a scene has. An editor that cannot hold a real project makes every later feature slower to build and to show.

Milestones are not dated. A few of them are large enough that they will take months each.

---

## 0.1 — A repository of its own, the editor's foundations, and a first slice of everything ✅

*What is in the repository now.*

- One engine, C++ only. The Swift twin of the tutorial engine is gone; Marble and FPV are samples.
- Projects: create, open, recent list, settings, a folder structure made for version control.
- Asset database with identifiers that survive renames (`.meta` files); asset browser with import, move, rename, Trash.
- Hierarchy with drag-and-drop parenting, multi-selection, search, rename.
- 3D view with move / rotate / scale / universal gizmos, world or local, snapping in meters, degrees and scale; view cube; fly, orbit, pan, frame.
- Inspector built from registered component editors; materials and render settings panels; console.
- Undo for everything, play mode in place, dockable layout.
- Editor plugins in C++: panels, menus, component editors, create entries, viewport overlays.
- **Game code in the project**: components declared in C++ with their fields, shown, saved and run; rebuilt and reloaded from the editor; a player; export of a Mac application. *(The first part of 0.3.)*
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

- **Wind** as a field the whole engine reads: global direction and gusts plus local sources; vegetation, cloth, particles, smoke and water all respond to it.
- **Water**: ocean from a wave spectrum (FFT), rivers and lakes with shallow-water flow, buoyancy, foam, refraction, caustics, underwater rendering; reflections from the ray tracer where available.
- **Fire and smoke**: fluid simulation on a GPU grid in compute, rendered through the volume system, lighting the scene around it.
- **GPU particles** in compute, by the million, with collisions against depth and signed distance fields; a visual effect graph in the editor.
- **Cloth and soft bodies.**

## After 1.0

Terrain with sculpting and layers, foliage and procedural placement, world streaming for large maps, a visual scripting layer over the C++ API, networking, localization, accessibility helpers, an asset store format. Their order will follow what the people making games with Molten need first.

---

## What Molten will not do

- **Other platforms.** No Vulkan, no DirectX, no abstraction layer in case there is one some day. Every design decision may assume a tile-based Apple GPU with unified memory.
- **Other languages for the engine.** C++ throughout, Objective-C++ only where a system API demands it, Metal Shading Language for shaders.
