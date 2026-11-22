# Templates

A project folder put here is offered as a starting point in the editor's New Project window,
beside the empty project.

The engine comes with six, each a small game that can be played the moment it opens and
taken apart in the Inspector. None has code of its own: they are made of the kit's parts.

| Template | What it is | Made of |
|---|---|---|
| **Car Game** | A car, a circuit through a park, eight stars to collect, a yard with a ramp and crates to knock about | Vehicle Body, Wheel, Suspension, Motor, Drive Controls; Follow; Collectible, Goal; UI Gauge |
| **Plane Game** | A light aeroplane, a runway, eight rings in the sky | Vehicle Body, Wing, Hinge, Propeller, Flight Controls (with the flying actions in Assets/Input.actions) |
| **Boat Game** | A speedboat on a sea with waves, islands, ten buoys that float | Water, Rigidbody, Buoyancy, Boat |
| **Third Person Game** | A figure that walks, runs and jumps, with its animations, a camera the mouse turns, ten gems about a garden of stone | Character Controller, Character Mover, Orbit Camera; Animator with a controller (Animations/Player.animator) on a model with bones (Models/Figure.glb) |
| **Shooter Game** | Seen from your own eyes: a rifle with a magazine to load, twelve plates to shoot down, four machines that hunt you about a training yard | Character Mover, First Person Look, Weapon; Shooting Target, Shuttle; Hunter, Health; Supply, Damage Flash; prefabs for what a shot and a machine leave (with its own keys in Assets/Input.actions) |

| **Online Game** | The garden of the Third Person Game for several players: one hosts, the others join at its address, everyone has a figure of their own and the gems are counted for all | Network Session (its menu, its player prefab, its spawn points, its shared values), Networked on the player prefab, the gems and the crates |

They share the same few pieces of interface (a count, what the keys do, a Restart button that
is a When/Then) and the same rule: collect, or shoot down, everything and the Goal says so.

## How they are made

In code, by the engine's own tests (`Tests/StarterGames.cpp`): each game is built from the
kit's parts, written to a folder as a project, read back the way the player reads it and
played without a screen with the keys a person would press. The car is driven round to every
star, the aeroplane takes off and flies through its rings, the boat rounds its buoys, the
figure is walked up the stairs and jumped from block to block to every gem, the yard is
shot clear from its firing line and from behind its walls, and the online garden is played by
three machines at once (three worlds in one program, joined by a network that is only memory). To write them here again (after
changing a component they use, say):

    MOLTEN_WRITE_TEMPLATES=Templates build/moltentests starter

Their models are a few dozen flat faces each, written as `.obj` files by the same code. Their
sounds come from `Tools/make_sounds.py` (the engines, the propeller, the outboard, wind and
tires, footsteps, a rifle) and are not rewritten by the tests.

The figure of the Third Person Game is made the same way (`Tests/StarterFigure.cpp`): a model
with seventeen bones and five clips (Idle, Walk, Run, Jump, Fall) written as one `.glb`, its
colors a small picture beside it. Its walk and its run are worked out from the feet up, so
that a foot on the ground stays where it is at the speed the game moves the figure. To use a
character of your own instead (one made in a modeling program, or downloaded with its
animations from a library of them and saved as `.glb` with its clips in it), put its file in
Assets/Models, choose it as the mesh of the entity "Figure" and as its Animator's model, and
name its clips in Animations/Player.animator: see Docs/MakingGames.md, "A character of your own".

A template's picture on the New Project page is `preview.png` in its folder; the editor makes
it the first time it finds none, by running the template for a moment without a window. Until
then (and for templates it has no picture for) it draws one.

## Other projects as templates

Any project folder copied here becomes a template: its `template.txt`, if it has one, is the
line or two shown under its name. The example project **Driving** (a racing game with its own
scripts, written by `Tools/make_templates.py` into `../MoltenProjects`) can be used that way.
