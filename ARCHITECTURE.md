# How Molten Metal Engine works

This is the map. It says what each part does and why it is built that way; the source files say how, and their comments are written to be read. Names are given as `Folder/File`: the files are `Engine/Folder/File.hpp` and `.cpp`. The editor has its own section at the end.

The renderer described here is the one Molten starts from. [ROADMAP.md](ROADMAP.md) says what replaces it and in which order.

## Conventions

Get these wrong and everything is upside down or inside out, so here they are in one place.

- **Space.** Right-handed. +Y is up. A camera, a directional light and a spot light all look down their own -Z axis.
- **Triangles** are counter-clockwise seen from the front.
- **Depth** goes from 0 at the near plane to 1 at the far plane.
- **Texture coordinates** start at the top left. So does the 2D canvas, which is measured in points.
- **Colors.** Material colors and light colors are linear. Canvas colors are sRGB, the values a color picker shows. Color textures are sRGB and are converted by the GPU; data textures (normals, roughness) are not.
- **Tangents.** A vertex tangent points toward increasing u; its w is the sign that makes `cross(normal, tangent) * w` point to the top of the texture. Normal maps have green pointing up.
- **Vectors and matrices** are Apple's simd types.

## One frame

`Core/Engine` owns everything and runs the loop. Each frame:

1. **Input.** The window layer reports events to `Input/Input`.
2. **Fixed steps.** Real time is poured into an accumulator and consumed in steps of exactly 1/60 s (`Core/Time`). Each step calls the game's `fixedUpdate`, then `Physics.step`. A frame may run zero, one or several steps.
3. **Interpolation.** `Physics.writeTransforms` places every body between its last two physics positions, according to how much time is left in the accumulator. Motion looks smooth at any frame rate even though physics runs at 60 Hz.
4. **Update.** The game's `update`, then every `Behavior` component.
5. **Housekeeping.** Destroyed entities are removed, world matrices are computed parents first, particles move.
6. **Draw.** The game's `draw` fills the 2D canvas; `Renderer.render` draws the scene and then the canvas.

Why a fixed step: physics that advances by a different amount each frame gives different results on different machines, and becomes unstable when a frame is slow. The price is that key presses must be read in `update`, not in `fixedUpdate`, which may not run on a given frame.

## The scene

`Scene/Scene` is a small entity-component system. An entity is an index plus a generation number, so a handle to a destroyed entity can be recognized as stale. Each component type has its own storage, a "sparse set": the components sit back to back in one array, which is what a system walking all of them wants.

A component is any plain struct. The engine defines `Transform`, `Camera`, `Light`, `MeshRenderer`, `RigidBody`, `Collider`, `ParticleEmitter` and `Behavior`; a game adds its own.

`destroy` only marks an entity; it disappears at the end of the frame. That makes it safe to destroy things from inside a loop over components or from a collision callback.

## The renderer

`Rendering/Renderer` decides what to draw; `Rendering/RenderPasses` contains the draw calls.

### Extraction

Before any pass, the renderer walks the scene once and builds plain arrays: the camera's matrices, the lights (the first directional light is "the sun"), and one item per visible mesh. Items outside the view frustum are dropped. Opaque items are sorted so that those sharing a material and a mesh are adjacent, and each run becomes **one instanced draw call**: the per-instance matrices go into a buffer, and the vertex function indexes it with `instance_id`. Transparent items are sorted from far to near instead.

### Passes

| | Pass | Draws into | Notes |
|---|---|---|---|
| 0 | Environment | sky cubemap, irradiance cubemap, prefiltered cubemap, BRDF table | Only when the sky changes. One small pass per cube face and mip level. |
| 1 | Shadow maps | one slice of a depth array per cascade | Depth only. Depth clamping keeps casters that are in front of the cascade's box. |
| 2 | Depth and normals | depth + view-space normals | Only if ambient occlusion is on. Not multisampled. |
| 3 | Ambient occlusion, blur | one-channel textures | Twelve samples per pixel in a hemisphere, then a 4 x 4 blur. |
| 4 | Scene | HDR color (multisampled, resolved at the end) | Opaque, then sky, then transparent, particles, debug lines. |
| 5 | Bloom | a chain of half-size textures | Prefilter, downsample, then add back up. |
| 6 | Final | the drawable | Tone mapping; then the canvas, which must not be tone mapped; then `Renderer::overlay`, where the editor draws its interface. |

The scene is drawn **forward**: one fragment function computes a pixel completely. That suits Apple GPUs, which determine the visible surface of every pixel before running any fragment function, and it works with multisampling and transparency without special cases.

**Shadows.** The view distance is cut into four slices, and each gets its own shadow map fitted around it (`Rendering/Cascades`). Each cascade is a sphere around its slice, snapped to whole texels, so shadows do not shimmer as the camera moves or turns. The lookup is offset along the surface normal by two texels, which removes self-shadowing without pushing shadows away from their casters.

**Image-based lighting.** The sky is a few lines of shader code. It is rendered into a cubemap, and that cubemap is filtered into the two maps of the split-sum approximation. Reflections and ambient light therefore always agree with the sky the player sees, and changing the sky's colors relights the scene.

### Memory per frame

Everything that changes each frame (uniforms, instance matrices, canvas vertices, particles) is copied into one large buffer by a bump allocator (`Rendering/FrameAllocator`). There are three such buffers, used in turn, because the GPU may still be reading the previous two frames' data; a semaphore keeps the CPU from getting further ahead than that.

On Apple GPUs the multisampled color target and the depth target of the main pass are **memoryless**: they exist only in the GPU's tile memory while the pass runs, and take no RAM.

### Shaders and pipelines

`Rendering/ShaderSource` joins the `.metal` files into one text, resolving `#include "..."` itself, and `Rendering/ShaderLibrary` compiles it at startup. A render pipeline state is created the first time a combination of functions, formats, sample count and blending is needed, and kept. If a shader file changes on disk it is recompiled; if that fails, the error is logged and the old shaders stay in use.

### Binding slots

Declared once in `Shaders/Shared/ShaderTypes.h`:

| Buffers | | Textures | | Samplers | |
|---|---|---|---|---|---|
| 0 | vertices | 0 | base color | 0 | material (repeat, anisotropic) |
| 1 | per-pass uniforms | 1 | normal | 1 | shadow (comparison) |
| 2 | instances | 2 | metallic-roughness | 2 | linear, clamped |
| 3 | material | 3 | occlusion | | |
| 4 | per-frame uniforms | 4 | emissive | | |
| 5 | lights | 5 | shadow map array | | |
| | | 6 | irradiance cube | | |
| | | 7 | prefiltered cube | | |
| | | 8 | BRDF table | | |
| | | 9 | ambient occlusion | | |

### Ray tracing

On a GPU that can trace rays (`GraphicsDevice::supportsRayTracing`), every `Mesh` builds an acceleration structure of its triangles when it is created: a tree of boxes that lets a ray skip almost everything. Each frame that needs it, `Renderer::buildSceneAccelerationStructure` builds a second, small structure over those: one entry per object that casts a shadow, with its world matrix. The meshes' structures are never rebuilt; the scene's one is a tree over a few hundred boxes.

Ray-traced sun shadows (`RenderSettings::rayTracedShadows`) are computed in the forward pass itself: the fragment function sends a few rays from the pixel toward different points of the sun's disk and counts how many arrive (`sunVisibilityTraced` in `Shaders/Lighting.metal`). The spread of the rays is turned by a different angle in every pixel, which trades banding for fine grain.

Two decisions keep this from ever breaking a frame that does not use it:

- The ray tracing code is inside `#if MOLTEN_RAYTRACING`, and the shaders are compiled with it only on a GPU that supports it. If that compilation fails, the engine compiles again without it and says so.
- The traced fragment functions are **separate entry points** (`fragment_pbr_traced`), chosen by the renderer only in frames that trace. The ordinary ones do not mention the scene's structure, so nothing has to be bound for it.

## Game components

`Game/Component` is how a game's own C++ types become known to the engine without the engine being compiled with them. `MOLTEN_COMPONENT(Type, fields...)` makes a static object whose constructor adds a `ComponentType` to the `ComponentRegistry`: the type's name, its fields (each with a function that finds it inside an object, and conversions to and from text), functions to add, get, copy and remove it on an entity, and pointers to its `start`, `update` and `fixedUpdate` if the struct has them (found at compile time with `requires`).

- **Scene files** write every registered component as `component "Name"` followed by one `field` line per field. A component whose type is not registered is kept as text on the entity (`UnknownComponents`) and written back unchanged.
- **The engine** calls the components' code in `Engine::runComponents`, when `Engine::runsComponents` is true: after the game's own `fixedUpdate` and `update`.
- **A game module** (`Game/GameModule`) is a dynamic library; loading it runs its registrations. Types are tagged with the module that declared them, so they can be withdrawn together. A module is never unloaded, only retired: objects it made may still exist.
- **Reloading** (`SceneDocument::reloadGameCode` in the editor) is: scene to text, `Scene::clear` and `Scene::dropStorages`, load the new module, text to scene. The containers are dropped because a container of a game's component type was created by the old module's code.

## Several views of one scene

`Renderer::views` asks for pictures of the scene in textures of their own, each through a camera and at a size; with any, the frame's target receives only the background, the canvas and the overlay. Each view runs the whole pipeline (shadows are fitted to its camera) with its own set of working textures, kept between frames (`swapTargets`), so two views of different sizes do not reallocate anything. The acceleration structure for ray tracing is built once per frame. The editor asks for two, Scene (its camera, with the grid and outlines) and Game (the scene's camera, with the canvas), and shows them with `ImGui::Image` in ordinary dockable windows.

## The world, and the kit

`World` (`Engine/Core/World.hpp`) is the part of the engine a game's rules need: the scene, physics, input, time, named shared values, prefabs, and loading scenes. `Engine` derives from it and adds the window, the renderer, assets and sound. A component's functions may take either; one that takes a `World&` also runs in a `World` that is not an `Engine`, which is what makes a game playable in a test (`World::advance`).

Components hear about physics through `onCollision` and `onTriggerEnter`: the physics step collects the contacts and triggers that began, and `World::dispatchPhysicsEvents` tells both entities of each pair afterwards, so the code is free to destroy and spawn. `loadScene` and `reloadScene` only note what was asked; the scene is replaced at the end of the frame. A reload returns to the text the scene was loaded from (or that the editor handed over when Play was pressed), not to a file.

`Engine/Kit` is gameplay written on top of this and nothing else: the ready-made components (`Kit.cpp`) and the effect presets (`Effects.cpp`). The templates in `Templates/` are scenes that use them.

## Visual scripts

`Script/Graph` is a script as data: nodes (a kind, a place on the board, the values typed into unwired inputs) and links between named pins. `NodeLibrary` holds the kinds: each a `NodeDef` with its pins and one or two functions, `execute` for nodes on the white flow wires and `evaluate` for nodes that give values. A script is a text file, `.graph`.

`Script/Runtime` runs it. Once per frame per entity, `runFrame` looks for the event nodes whose event happened and follows the flow wire from each, node by node (`runFlow`). A node that needs an input calls `ExecContext::input_`, which asks the node at the other end of the wire, recursively; nothing is compiled or cached. Both walks are bounded, so a loop of wires ends instead of hanging the game. The `VisualScript` component (declared with `MOLTEN_COMPONENT` like a game's own) holds the script's name and the entity's variables, and reads the file again when it changes on disk, which is all that live editing is.

The editor's drawing board (`Editor/GraphEditor`) depends only on Dear ImGui and `Script/Graph`, so it can be driven without a window by a program that feeds ImGui a pretend mouse.

## Physics

`Physics/Collision` answers one question exactly: do these two shapes overlap, and if so, along which direction and how deep? It handles every pair of sphere, oriented box and capsule. `Physics/Physics` builds the simulation on top: integrate, find overlapping pairs (sort and sweep along x), resolve velocities with impulses over a few iterations, then push bodies apart.

It is an arcade model on purpose. Bodies do not spin from impacts, there are no joints, and fast objects can pass through thin ones. Those are the hard parts of a physics engine, and a game that needs them should use a dedicated library. What is here is enough for platforms, pushing crates, triggers and rolling a marble, and it is small enough to read.

## Sound

`Audio/Mixer` is the whole audio engine: a list of playing voices and a function that fills a block of samples by reading each voice's clip at its pitch, scaling by volume and pan, and adding. It knows nothing about sound cards. `Audio/Audio` connects it to one through miniaudio, which calls `Mixer.render` from the audio thread.

`Audio/AudioClip` can also synthesize clips from a waveform, a pitch and a duration, which is how a game has sound effects without shipping audio files.

## The window layer

`Platform/Window` is the only code that talks to the window system: GLFW, plus one Objective-C++ function that gives the window a `CAMetalLayer`. It translates events into calls on `Input`.

With `--capture` no window is created at all: the engine renders into an ordinary texture, copies it to memory and writes a PNG.

On iOS `Platform/iOS/PlatformiOS.mm` implements the same `Window` class over a `UIView` with a `CAMetalLayer`, and turns touches into `Input` calls (the first finger is also the mouse). There the system owns the loop, so `Engine::run` is split into `begin`, `frame` and `end`, and a display link calls `frame`. `Platform/Application` (`runApplication`) starts a program the right way on either platform.

## The canvas

`Rendering/Canvas` turns every 2D call into triangles in one vertex buffer; the renderer draws them after tone mapping, one draw call per change of texture or clip.

- **Smooth edges** come from the geometry, not from multisampling. A convex shape is drawn half a pixel smaller than asked and surrounded by a strip one pixel wide whose outer vertices have zero alpha; a line is a solid core with such a strip on each side. Rounded rectangles, circles, rings and arcs are paths given to those two routines. The canvas needs to know how many pixels a point is, which the engine passes to `begin`.
- **Clipping** (`pushClip`, `popClip`) is recorded per batch and applied by the renderer as a scissor rectangle, rounded to pixels and kept inside the target.

## Scene files and the Gui

- **Scene files** (`Scene/SceneFile`) are text, one statement per line, starting with `molten-scene 1`: settings, then materials, then entities with one line per component. Meshes and textures are stored by name (`Mesh.name`, `Texture.name`, set by `Assets`), never as data. Unknown lines are skipped. Numbers are written in the shortest form that reads back to the same float, so saving an unchanged scene changes nothing in the file.
- **The Gui** (`UI/Gui`) is immediate-mode: a control is a function call that draws it and returns what the user did. It needs only a `Canvas` and an `Input`. A panel clips its rows with the canvas, so they scroll smoothly under its edges; rows entirely outside are skipped and do not react. Sections fold and remember it under an id made from their title. Icons are drawn by code from lines and arcs. Besides the controls laid out in panels there are free ones (`bar`, `card`, `button`, `iconButton` with a position), which is what tool bars are made of. The pop-up of a list and the tooltip are drawn in `end()`, on top of everything; a pop-up's choice reaches the control one frame later.

## Testing

- **Unit tests** cover everything that does not need a GPU: math, the scene, input, time, meshes, collision (including a test that tries thousands of random shape pairs and checks that the reported contact really separates them), physics, cascades, particles, the synthesizer, the mixer, shader file handling, fonts, the canvas, the Gui (by feeding it mouse and key events), scene files (write, read, write again, compare), projects and the asset database (on real folders, in a temporary place), and FPV's flight model: hover throttle, the response to a stick, top speed, battery sag and endurance, and control at every throttle.

## The editor

`Editor/` is a program made with the engine: a `Game` (`EditorApp`) whose "gameplay" is editing the scene it shows. Its interface is Dear ImGui, drawn by the engine's renderer in `Renderer::overlay`; the 3D handles are ImGuizmo.

- **The 3D view is not a window.** The panels are docked around a hole, and `Renderer::sceneViewport` makes the renderer draw the scene into exactly that rectangle of the window. Nothing is rendered to a texture and copied. A transparent ImGui window over the hole takes the mouse and holds the toolbar.
- **Colors.** The window is sRGB-encoded by the GPU; ImGui's colors are already sRGB numbers. `ImGuiLayer` decodes every vertex color before drawing, so the two cancel and the interface looks as designed.
- **`SceneDocument`** is the open scene: its file, its materials, the selection, undo and play mode. It keeps its own camera in the scene, marked `EditorOnly` so that it is never saved, and points `Renderer::cameraOverride` at it. Undo is a stack of scene files in memory: after every finished edit the scene is written to text and compared with the last one. Play writes the scene once, and Stop reads that text back.
- **Projects** (`Engine/Project/Project`) are a folder with a `.molten` file, an `Assets` folder, and folders for plugins, settings and caches. `Assets` (the loader) looks in the project's folder first and in the engine's own second.
- **The asset database** (`Engine/Assets/AssetDatabase`) is a scan of the project's `Assets` folder. Every file gets a `.meta` beside it with an identifier that survives renaming and moving. Scenes still refer to assets by path; moving them to identifiers is the first item of the next milestone.
- **Everything is registered.** Panels, menu items, the components shown in the inspector, the entries of the Create menu and drawings over the 3D view all go through `Registry` (`Editor/API/EditorAPI.hpp`). The editor's own panels use it exactly as a plugin does, so there is nothing they can do that a plugin cannot.
- **Plugins** are dynamic libraries. The editor executable contains the whole engine and the whole of ImGui (linked with `WHOLE_ARCHIVE`) and exports their symbols; a plugin is linked against the executable itself (`-bundle_loader`), so both share one copy of everything, including ImGui's context and the scene's component tables. See [Docs/Plugins.md](Docs/Plugins.md).
