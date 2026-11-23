# Visual scripts

A visual script is a behavior drawn instead of written: boxes for what happens, wires for the order it happens in and the values it uses. It is the quick way to make something move, react to a key, count. For anything long or intricate, write a component in C++ ([GameCode.md](GameCode.md)); the two live side by side on the same entities.

## Making one

**Assets > Create > Visual Script**, or **+ New > Visual Script** in the Explorer. The script is a `.graph` file in the project's `Assets`; double-click it to open the drawing board.

A new script has two nodes on it: **On Start** and **On Update**. Press Space over the board (or right-click it) to add more: type a few letters, press Enter.

To make an entity turn:

1. Space, type `rot`, Enter: a **Rotate** node.
2. Drag from the arrow on the right of **On Update** to the arrow on the left of **Rotate**.
3. Type `90` next to *degrees*.
4. Select an entity in the scene and press **Use on Selection**.
5. Press Play.

The script is saved by itself when you let go of the mouse. A game that is playing picks the change up at once: you can rewire a script while it runs.

## Reading a script

**White wires are the order of things.** They start at an event (a red node: something that happens) and go from arrow to arrow: when this is done, do that. A node with white arrows *does* something.

**Colored wires are values.** A node without arrows works a value out when it is asked: a number, a position, whether a key is down. The color says what kind:

| Color | Kind |
|---|---|
| green | a number |
| red | yes or no |
| yellow | a point or a direction: three numbers |
| purple | a text |

An input nothing is wired to shows a field: type the value there. Kinds convert where it makes sense: a number wired to a yellow input becomes that number in x, y and z; a number wired to a red one is "yes" unless it is zero.

## On the board

| | |
|---|---|
| Space, or right-click the board | search for a node and add it where the pointer is (arrows and Enter choose) |
| Click a title | select the node; Shift+click adds or takes it out of the selection |
| Drag on the board | a box that selects every node it touches (Shift keeps what was selected) |
| Drag a title | move it, with every other selected node |
| Drag from a pin to another | wire them |
| Right-click a pin | unplug it |
| Delete or Backspace, or right-click a title | remove the selected nodes |
| Cmd+A | select every node |
| Scroll, or drag with the middle or right button | move around |
| Cmd+scroll, or - and + in the toolbar | zoom, about the pointer |
| F, or Fit | every node in view |

Dots run along the wires the way things go: quick and white on the wires that say what happens next, slower on the wires that carry a value. Zoomed far out, a node shows its values without the boxes to change them.

An arrow leads to one place; wiring it somewhere else moves the wire. An input takes one value; a value can feed many inputs.

## The nodes

**Events** — On Start · On Update (gives *dt*, the length of the frame) · On Key Pressed · On Key Released · On Pointer Pressed (the mouse button, or a finger on iPhone and iPad)

**Flow** — Branch (one way if a condition holds, another if not) · While (*do* again and again while a condition holds, then *done*) · For (*do* once for each number from *from* to *to*, the number on *index*) · Return (the rest of the event is not run) · C++ (a line of C++ no box says yet: see below)

**Transform** — Move · Rotate · Set Position · Set Scale · Position · Forward

**Physics** (the entity needs a Rigid Body) — Set Velocity · Add Velocity · Velocity · Is Grounded

**Entity** — Destroy Self · Set Visible · Log

**Input** — Key Is Down · Key Axis (-1, 0 or 1 from two keys) · Pointer Is Down

**Math** — Number · Add · Subtract · Multiply · Divide · Sine · Random · Time · Vector · Split Vector · Add Vectors · Scale Vector

**Logic** — Greater Than · Less Than · Greater or Equal · Less or Equal · Equal · Not Equal · And · Or · Not · Select

**Variables** — Set Variable · Get Variable · Add to Variable · Get Field · Set Field (a field of any of the entity's components, by its name)

### The script's variables

A script's variables are listed at the left of the Visual Script window. **+** makes one; each has a name, a kind (a number, a yes-no, a vector or a text), the value it starts with, what it is for, and **In the Inspector**: shown on each entity that runs the script, to be set there for each one. **−** takes it away. Renaming one renames it in every Get and Set Variable box that uses it, and those boxes choose the name from the list. Each entity running the script has its own values.

### Comments, and what went wrong

A box's comment is written with **Add Comment…** in its right-click menu. A box with one has a small note mark on its title, and the comment shows when the pointer rests on the title, above what the box does. In C++ the comment is the line above the box's line.

When something goes wrong as a script runs (a variable no one declared, a field a component does not have, a line of C++ no box says yet), the box is ringed in red with the words under it, and the input it was about, if it was one, is in red too. It is cleared once the script is changed.

**Animation** (the entity's Animator) — Play Animation (one of its own, by name; with a controller, a state) · Stop Animation · Pause Animation · Resume Animation · Is Animation Playing · Set Animator Float · Set Animator Bool · Set Animator Trigger · Play State (eased over some seconds) · Set Animator Speed · Animator Parameter · Animator State. Names, parameters and states are chosen from lists; the events of animations come as messages to On Message.

Every text box that names something (a key, a sound, a scene, a prefab, an entity, a message, a shared value, a variable, a parameter, a state) is a list of what it can be, never typed: only the words of a Log are.

Keys are chosen by name from the list: `A`, `7`, `Space`, `Enter`, `Left`, `Right`, `Up`, `Down`, `Shift`, `Escape`, `Tab`.

Hover a node's title to read what it does.

### An example: move with the keys, jump with Space

```
[On Update] ─▶ [Move]                          [On Key Pressed: Space] ─▶ [Branch] ─true▶ [Add Velocity 0 6 0]
                offset ◀─ [Vector]                                          condition ◀─ [Is Grounded]
                           x ◀─ [Multiply] ◀─ [Key Axis A D]
                                 b: 4
```

## Adding nodes of your own

A node is a few lines of C++, in the game's code or in an editor plugin:

```cpp
#include "MoltenMetal.hpp"
using namespace molten::script;

static const bool registered = [] {
    NodeDef node;
    node.type = "Double";                 // the name in files: never change it afterwards
    node.category = "Math";
    node.title = "Double";
    node.help = "Twice a number.";
    node.inputs = {PinDef{"value", PinType::Float, Value::number(1)}};
    node.outputs = {PinDef{"result", PinType::Float, {}}};
    node.evaluate = [](ExecContext& context, int) { return Value::number(context.number("value") * 2); };
    NodeLibrary::instance().add(node);
    return true;
}();
```

A node that does something has a `Flow` input called `in`, a `Flow` output, and an `execute` function that returns the name of the output to continue from.

## Watching it run, and stopping it

While the game runs, the board shows the script at work: on each colored wire, the last value
that went along it ("3.14", "0 1 0", "true"), and each node that runs lights up for a moment.
When several entities run the same script, the list at the top says which one is watched.

**Breakpoints.** Right-click a node and choose **Add Breakpoint** (or select it and press F9):
a red dot. When the flow comes to it, the game is held before the node runs, its ring turns
yellow, and the board shows the values of that moment. **Continue** runs the node and lets the
game go on until the next breakpoint; **Step** runs it and holds again before the next node
that runs. Breakpoints stay while the editor is open, playing or not.

## What is not there yet

- Undo on the drawing board (Cmd+Z there does nothing yet; the scene's undo is separate).
- Copy and paste.
- Functions: a script used as a node in another.

## To C++ and back

**To C++** in the Visual Script toolbar (or **Convert to C++ Script** in the Inspector when a script is chosen in the Explorer, or in the Explorer's right-click menu) writes the same behavior as a C++ component in `Assets/Scripts/<Name>.cpp`, written as a programmer would write it: the variables are the component's fields (with `MOLTEN_FIELD` for the ones shown in the Inspector, and their comments beside them), each event is its function (`start`, `update`, `onCollision`, `onTriggerEnter`), and each box is the line it means: `transform->rotation = ...`, `if (world.input.isDown(Key::Space)) { ... }`, `for (float i = 0; i < 3; i += 1) { ... }`, `world.playSound(...)`, `lives += -1;`. A box's comment is the comment above its line. The few boxes that have no line of their own in the engine's C++ are written as `call("Box", {...})`, which runs that very box.

The way back is **Convert to Visual Script** (in the Inspector, and in the Explorer's right-click menu on a `.cpp`): it writes the `.graph` beside it and opens it. A script is read into **the same boxes** the Add menu has, so the same script drawn by hand and the one read from C++ are the same: the fields of a number, `bool`, `Vec3` or `std::string` become the script's variables (with their first values and comments); `start`, `update`, `onCollision` and `onTriggerEnter` their events; `if`/`else`, `while` and counted `for` loops, `return`, comparisons, math, the Transform, the Rigid Body, the Animator, input, sound, scenes, shared values and logging their boxes. The comment above a line goes on its box. Write it to C++ again and the same code comes out.

A line no box says yet (a call to a function of your own, a lambda, a container) is kept as a **C++** box, in yellow, holding the line as it is written, in its place in the flow; members of the component that are not variables (a `std::vector`, a function of its own) and `#include`s are kept with the script and written back as they were. A note above the board counts them. These lines run once the script is converted to C++; on the board they are said, not run.

**Code that is not a single component becomes its own code as boxes.** A file with several types, templates or code outside a component is shown as **C++ Function**, **C++**, **C++ If**, **C++ Block** and **C++ File** boxes, one for each function, statement, `if` or block and the text between them, so **To C++** writes back the file it was made from byte for byte. What the `.cpp` said before is kept in `Library/Before Conversion`.

## Watching one entity's script

With **Follow Selection** on (it is to begin with), choosing an entity with a Visual Script in the
scene shows its script. While the game runs, the board shows that entity's own copy of it: the
values on its wires, the nodes it ran lighting up, and its variables beside its name at the top.
Several entities can run the same script: choose another and the board shows what that one is doing.

The board glides when it is zoomed (Cmd and the wheel, or - and +) and scrolled; F fits every node in
view, and the map in its corner shows where the view is (a click there goes to that place). A wire
pulled from a pin and let go on the empty board offers the nodes to add there, plugged into it. While
a wire is pulled, the pins it fits light up.
