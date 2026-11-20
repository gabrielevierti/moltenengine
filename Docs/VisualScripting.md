# Visual scripts

A visual script is a behavior drawn instead of written: boxes for what happens, wires for the order it happens in and the values it uses. It is the quick way to make something move, react to a key, count. For anything long or intricate, write a component in C++ ([GameCode.md](GameCode.md)); the two live side by side on the same entities.

## Making one

**Game > New Visual Script**, or **+ New > Visual Script** in the asset browser. The script is a `.graph` file in the project's `Assets`; double-click it to open the drawing board.

A new script has two nodes on it: **On Start** and **On Update**. Right-click the board to add more.

To make an entity turn:

1. Right-click > Transform > **Rotate**.
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
| Right-click the board | add a node |
| Drag a node's title | move it |
| Drag from a pin to another | wire them |
| Right-click a pin | unplug it |
| Right-click a title, or select and press Delete | remove the node |
| Scroll, or drag the board | move around |

An arrow leads to one place; wiring it somewhere else moves the wire. An input takes one value; a value can feed many inputs.

## The nodes

**Events** — On Start · On Update (gives *dt*, the length of the frame) · On Key Pressed · On Key Released · On Pointer Pressed (the mouse button, or a finger on iPhone and iPad)

**Flow** — Branch (one way if a condition holds, another if not)

**Transform** — Move · Rotate · Set Position · Set Scale · Position · Forward

**Physics** (the entity needs a Rigid Body) — Set Velocity · Add Velocity · Velocity · Is Grounded

**Entity** — Destroy Self · Set Visible · Log

**Input** — Key Is Down · Key Axis (-1, 0 or 1 from two keys) · Pointer Is Down

**Math** — Number · Add · Subtract · Multiply · Divide · Sine · Random · Time · Vector · Split Vector · Add Vectors · Scale Vector

**Logic** — Greater Than · Less Than · And · Or · Not · Select

**Variables** — Set Variable · Get Variable (numbers remembered by name, separately for each entity)

Keys are written by name: `A`, `7`, `Space`, `Enter`, `Left`, `Right`, `Up`, `Down`, `Shift`, `Escape`, `Tab`.

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

## What is not there yet

- Undo on the drawing board (Cmd+Z there does nothing yet; the scene's undo is separate).
- Nodes that reach other entities (find by name, send a message), collisions as events, loops, timers, sound.
- Selecting and moving several nodes, copy and paste, comments, zoom.
- Functions: a script used as a node in another.
