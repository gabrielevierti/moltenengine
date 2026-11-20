#include "Script/Runtime.hpp"

#include "Core/World.hpp"
#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Rendering/Material.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace molten::script {

namespace {

const int kMaximumSteps = 10000;        // nodes run per event: a loop of white wires ends here
const int kMaximumDepth = 64;           // values asking values: a loop of colored wires ends here

/// The value of an output of a node: asks the node's kind to work it out.
Value evaluateOutput(ExecContext context, const Node& node, const std::string& pin) {
    const NodeDef* definition = NodeLibrary::instance().find(node.type);
    if (!definition || !definition->evaluate || context.depth >= kMaximumDepth) return Value{};
    int index = definition->outputIndex(pin);
    if (index < 0) return Value{};
    context.node = &node;
    context.definition = definition;
    context.depth++;
    return definition->evaluate(context, index);
}

/// Follows the white wire that leaves `node` at `pin`, node after node, until one stops.
void runFlow(ExecContext context, const Node* node, const char* pin) {
    for (int step = 0; step < kMaximumSteps && node && pin; step++) {
        const Link* link = context.graph->linkFrom(node->id, pin);
        if (!link) return;
        const Node* next = context.graph->find(link->toNode);
        const NodeDef* definition = next ? NodeLibrary::instance().find(next->type) : nullptr;
        if (!definition || !definition->execute) return;
        context.node = next;
        context.definition = definition;
        context.depth = 0;
        pin = definition->execute(context);
        node = next;
        // A node may have destroyed the entity the script is on: nothing more to do then.
        if (!context.scene || !context.scene->isAlive(context.self)) return;
    }
}

// ---- shorthand for the definitions below

PinDef flow(const char* name) { return PinDef{name, PinType::Flow, Value{}}; }
PinDef number(const char* name, float value = 0) { return PinDef{name, PinType::Float, Value::number(value)}; }
PinDef flag(const char* name, bool value = false) { return PinDef{name, PinType::Bool, Value::boolean(value)}; }
PinDef vector(const char* name, float x = 0, float y = 0, float z = 0) { return PinDef{name, PinType::Vec3, Value::vector(x, y, z)}; }
PinDef string(const char* name, const char* value = "") { return PinDef{name, PinType::String, Value::string(value)}; }

Value vectorValue(Vec3 v) { return Value::vector(v.x, v.y, v.z); }

/// A node that does something: a flow in, a flow out ("then"), and inputs.
void action(const char* type, const char* category, const char* title, const char* help, std::vector<PinDef> inputs,
            std::function<void(ExecContext&)> work) {
    NodeDef definition;
    definition.type = type;
    definition.category = category;
    definition.title = title;
    definition.help = help;
    definition.inputs.push_back(flow("in"));
    for (PinDef& pin : inputs) definition.inputs.push_back(std::move(pin));
    definition.outputs.push_back(flow("then"));
    definition.execute = [work = std::move(work)](ExecContext& context) -> const char* {
        work(context);
        return "then";
    };
    NodeLibrary::instance().add(std::move(definition));
}

/// A node that only gives values.
void value(const char* type, const char* category, const char* title, const char* help, std::vector<PinDef> inputs,
           std::vector<PinDef> outputs, std::function<Value(ExecContext&, int)> evaluate) {
    NodeDef definition;
    definition.type = type;
    definition.category = category;
    definition.title = title;
    definition.help = help;
    definition.inputs = std::move(inputs);
    definition.outputs = std::move(outputs);
    definition.evaluate = std::move(evaluate);
    NodeLibrary::instance().add(std::move(definition));
}

/// A node that starts a flow. `outputs` are the values it offers besides "then".
void event(const char* type, const char* title, const char* help, std::vector<PinDef> inputs, std::vector<PinDef> outputs,
           std::function<Value(ExecContext&, int)> evaluate = {}) {
    NodeDef definition;
    definition.type = type;
    definition.category = "Events";
    definition.title = title;
    definition.help = help;
    definition.isEvent = true;
    definition.inputs = std::move(inputs);
    definition.outputs.push_back(flow("then"));
    for (PinDef& pin : outputs) definition.outputs.push_back(std::move(pin));
    definition.evaluate = std::move(evaluate);
    NodeLibrary::instance().add(std::move(definition));
}

Transform* transformOf(ExecContext& context) {
    return context.scene ? context.scene->get<Transform>(context.self) : nullptr;
}

}  // namespace

// ---------------------------------------------------------------- what nodes see

Value ExecContext::input_(const char* pin) {
    if (!graph || !node) return Value{};
    if (const Link* link = graph->linkTo(node->id, pin)) {
        if (const Node* source = graph->find(link->fromNode)) return evaluateOutput(*this, *source, link->fromPin);
    }
    const PinDef* definitionPin = definition ? definition->findInput(pin) : nullptr;
    auto typed = node->values.find(pin);
    if (typed != node->values.end()) return Value::fromText(definitionPin ? definitionPin->type : PinType::Float, typed->second);
    return definitionPin ? definitionPin->defaultValue : Value{};
}

Vec3 ExecContext::vector(const char* pin) {
    float x, y, z;
    input_(pin).asVector(x, y, z);
    return Vec3{x, y, z};
}

// ---------------------------------------------------------------- one frame

void runFrame(const Graph& graph, ExecContext context) {
    if (!context.scene || !context.state) return;
    context.graph = &graph;
    bool isFirstFrame = !context.state->hasStarted;
    context.state->hasStarted = true;
    // By index: the nodes of a script do not change while it runs, but this keeps it so
    // even if a node one day edits its own script.
    for (size_t i = 0; i < graph.nodes.size(); i++) {
        const Node& node = graph.nodes[i];
        const NodeDef* definition = NodeLibrary::instance().find(node.type);
        if (!definition || !definition->isEvent) continue;
        context.node = &node;
        context.definition = definition;
        context.depth = 0;
        bool happened = false;
        if (node.type == "OnStart") {
            happened = isFirstFrame;
        } else if (node.type == "OnUpdate") {
            happened = true;
        } else if (node.type == "OnKeyPressed") {
            happened = context.input && context.input->wasPressed(keyByName(context.string("key")));
        } else if (node.type == "OnKeyReleased") {
            happened = context.input && context.input->wasReleased(keyByName(context.string("key")));
        } else if (node.type == "OnPointerPressed") {
            happened = context.input && context.input->wasPressed(MouseButton::Left);
        } else if (node.type == "OnTimer") {
            // Each timer node keeps its own count, under a name no script would choose.
            Value& waited = context.state->variables["\x01timer" + std::to_string(node.id)];
            waited.x += context.dt;
            float every = std::max(context.number("seconds"), 0.01f);
            if (waited.x >= every) {
                waited.x -= every;
                happened = true;
            }
        } else if (node.type == "OnCollision" || node.type == "OnTriggerEnter") {
            // Once for each thing that touched the entity since the last frame.
            const std::vector<Entity> others = node.type == "OnCollision" ? context.state->collidedWith : context.state->triggeredBy;
            for (Entity other : others) {
                context.other = other;
                context.node = &node;
                context.definition = definition;
                runFlow(context, &node, "then");
                if (!context.scene->isAlive(context.self)) return;
            }
            context.other = Entity{};
        }
        if (happened) runFlow(context, &node, "then");
        if (!context.scene->isAlive(context.self)) return;
    }
}

// ---------------------------------------------------------------- the standard nodes

void registerStandardNodes() {
    static bool isDone = false;
    if (isDone) return;
    isDone = true;

    // ---- events
    event("OnStart", "On Start", "Once, when the game starts or the entity appears.", {}, {});
    event("OnUpdate", "On Update", "Every frame. \"dt\" is the length of the frame in seconds.", {}, {number("dt")},
          [](ExecContext& context, int) { return Value::number(context.dt); });
    event("OnKeyPressed", "On Key Pressed", "When a key goes down. Write the key's name: A, Space, Left, 1.", {string("key", "Space")}, {});
    event("OnKeyReleased", "On Key Released", "When a key comes up.", {string("key", "Space")}, {});
    event("OnPointerPressed", "On Pointer Pressed", "When the mouse button goes down, or a finger touches the screen.", {}, {});

    event("OnTimer", "On Timer", "Again and again, every so many seconds.", {number("seconds", 1)}, {});
    auto otherName = [](ExecContext& context, int) {
        const Name* name = context.scene->get<Name>(context.other);
        return Value::string(name ? name->value : std::string());
    };
    event("OnCollision", "On Collision", "When the entity bumps into something solid. \"other\" is that thing's name.", {}, {string("other")}, otherName);
    event("OnTriggerEnter", "On Trigger Enter",
          "When something enters this entity's trigger collider, or this entity enters another's. \"other\" is its name.", {}, {string("other")},
          otherName);

    // ---- the game as a whole
    action("AddValue", "Game", "Add to Value", "Adds to a number the whole game shares: the score, coins, lives.",
           {string("name", "score"), number("amount", 1)}, [](ExecContext& context) {
               if (context.world) context.world->addValue(context.string("name"), context.number("amount"));
           });
    action("SetValue", "Game", "Set Value", "Sets a number the whole game shares.", {string("name", "score"), number("value", 0)},
           [](ExecContext& context) {
               if (context.world) context.world->setValue(context.string("name"), context.number("value"));
           });
    value("GetValue", "Game", "Value", "A number the whole game shares; zero if nobody set it.", {string("name", "score")}, {number("value")},
          [](ExecContext& context, int) { return Value::number(context.world ? context.world->value(context.string("name")) : 0.0f); });
    action("LoadScene", "Game", "Load Scene", "Goes to another scene: the next level, the menu.", {string("scene", "Scenes/Main.scene")},
           [](ExecContext& context) {
               if (context.world) context.world->loadScene(context.string("scene"));
           });
    action("ReloadScene", "Game", "Restart Scene", "Starts the scene over.", {}, [](ExecContext& context) {
        if (context.world) context.world->reloadScene();
    });
    action("PlaySound", "Game", "Play Sound", "Plays a sound file from the project's Assets.", {string("sound", "Audio/sound.wav"), number("volume", 1)},
           [](ExecContext& context) {
               if (context.world) context.world->playSound(context.string("sound"), context.number("volume"));
           });
    action("Spawn", "Game", "Spawn", "Adds a prefab to the scene at a point.", {string("prefab", "Prefabs/Thing.prefab"), vector("position")},
           [](ExecContext& context) {
               if (context.world) context.world->spawn(context.string("prefab"), context.vector("position"));
           });
    value("PositionOf", "Game", "Position Of", "Where the entity with this name is.", {string("name", "Player")}, {vector("position"), flag("exists")},
          [](ExecContext& context, int output) {
              Entity entity = context.scene->find(context.string("name"));
              const Transform* transform = context.scene->get<Transform>(entity);
              if (output == 1) return Value::boolean(transform != nullptr);
              return vectorValue(transform ? transform->worldPosition() : Vec3{0, 0, 0});
          });
    value("OtherIs", "Game", "Other Is", "In a collision or trigger event: yes if the other thing has this name.", {string("name", "Player")},
          {flag("result")}, [](ExecContext& context, int) {
              const Name* name = context.scene->get<Name>(context.other);
              return Value::boolean(name && name->value == context.string("name"));
          });

    // ---- flow
    {
        NodeDef branch;
        branch.type = "Branch";
        branch.category = "Flow";
        branch.title = "Branch";
        branch.help = "Goes one way if the condition holds and the other way if not.";
        branch.inputs = {flow("in"), flag("condition")};
        branch.outputs = {flow("true"), flow("false")};
        branch.execute = [](ExecContext& context) -> const char* { return context.flag("condition") ? "true" : "false"; };
        NodeLibrary::instance().add(std::move(branch));
    }

    // ---- transform
    action("Translate", "Transform", "Move", "Moves the entity. With \"per second\" the offset is a speed.",
           {vector("offset", 0, 0, 0), flag("perSecond", true), flag("local", false)}, [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) {
                   Vec3 offset = context.vector("offset") * (context.flag("perSecond") ? context.dt : 1.0f);
                   transform->position += context.flag("local") ? rotate(transform->rotation, offset) : offset;
               }
           });
    action("Rotate", "Transform", "Rotate", "Turns the entity around an axis, in degrees. With \"per second\" it is a speed.",
           {vector("axis", 0, 1, 0), number("degrees", 90), flag("perSecond", true)}, [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) {
                   float angle = radians(context.number("degrees")) * (context.flag("perSecond") ? context.dt : 1.0f);
                   Quat turn = axisAngle(safeNormalize(context.vector("axis")), angle);
                   transform->rotation = simd::normalize(turn * transform->rotation);
               }
           });
    action("SetPosition", "Transform", "Set Position", "Puts the entity at a point.", {vector("position")}, [](ExecContext& context) {
        Vec3 position = context.vector("position");
        if (Transform* transform = transformOf(context)) transform->position = position;
        // A body is where physics says it is: tell physics too.
        if (context.physics && context.scene->has<RigidBody>(context.self)) context.physics->teleport(*context.scene, context.self, position);
    });
    action("SetScale", "Transform", "Set Scale", "Sets the entity's size along its three axes.", {vector("scale", 1, 1, 1)},
           [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) transform->scale = context.vector("scale");
           });
    value("GetPosition", "Transform", "Position", "Where the entity is.", {}, {vector("position")}, [](ExecContext& context, int) {
        Transform* transform = transformOf(context);
        return vectorValue(transform ? transform->position : Vec3{0, 0, 0});
    });
    value("GetForward", "Transform", "Forward", "The direction the entity faces.", {}, {vector("forward")}, [](ExecContext& context, int) {
        Transform* transform = transformOf(context);
        return vectorValue(transform ? transform->forward() : Vec3{0, 0, -1});
    });

    // ---- physics
    action("SetVelocity", "Physics", "Set Velocity", "Sets how fast the entity's body moves. It needs a Rigid Body.", {vector("velocity")},
           [](ExecContext& context) {
               if (RigidBody* body = context.scene->get<RigidBody>(context.self)) body->velocity = context.vector("velocity");
           });
    action("AddVelocity", "Physics", "Add Velocity", "Adds to the body's velocity: a push, or a jump.", {vector("velocity", 0, 5, 0)},
           [](ExecContext& context) {
               if (RigidBody* body = context.scene->get<RigidBody>(context.self)) body->velocity += context.vector("velocity");
           });
    value("GetVelocity", "Physics", "Velocity", "How fast the body moves.", {}, {vector("velocity")}, [](ExecContext& context, int) {
        const RigidBody* body = context.scene->get<RigidBody>(context.self);
        return vectorValue(body ? body->velocity : Vec3{0, 0, 0});
    });
    value("IsGrounded", "Physics", "Is Grounded", "Yes while the body rests on something.", {}, {flag("grounded")},
          [](ExecContext& context, int) {
              const RigidBody* body = context.scene->get<RigidBody>(context.self);
              return Value::boolean(body && body->isGrounded);
          });

    // ---- the entity
    action("DestroySelf", "Entity", "Destroy Self", "Removes the entity from the scene.", {},
           [](ExecContext& context) { context.scene->destroy(context.self); });
    action("SetVisible", "Entity", "Set Visible", "Shows or hides the entity's mesh.", {flag("visible", true)}, [](ExecContext& context) {
        if (MeshRenderer* renderer = context.scene->get<MeshRenderer>(context.self)) renderer->isVisible = context.flag("visible");
    });
    action("Log", "Entity", "Log", "Writes a message to the console.", {string("message", "Hello")},
           [](ExecContext& context) { logInfo("%s", context.string("message").c_str()); });

    // ---- input
    value("KeyIsDown", "Input", "Key Is Down", "Yes while a key is held.", {string("key", "Space")}, {flag("down")},
          [](ExecContext& context, int) { return Value::boolean(context.input && context.input->isDown(keyByName(context.string("key")))); });
    value("KeyAxis", "Input", "Key Axis", "-1, 0 or 1 from two keys: for moving left and right, or back and forth.",
          {string("negative", "A"), string("positive", "D")}, {number("value")}, [](ExecContext& context, int) {
              if (!context.input) return Value::number(0);
              float axis = 0;
              if (context.input->isDown(keyByName(context.string("negative")))) axis -= 1;
              if (context.input->isDown(keyByName(context.string("positive")))) axis += 1;
              return Value::number(axis);
          });
    value("PointerIsDown", "Input", "Pointer Is Down", "Yes while the mouse button is held or a finger is on the screen.", {}, {flag("down")},
          [](ExecContext& context, int) { return Value::boolean(context.input && context.input->isDown(MouseButton::Left)); });

    // ---- numbers
    value("Number", "Math", "Number", "A number.", {number("value", 1)}, {number("value")},
          [](ExecContext& context, int) { return Value::number(context.number("value")); });
    value("Add", "Math", "Add", "a + b", {number("a"), number("b")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(context.number("a") + context.number("b")); });
    value("Subtract", "Math", "Subtract", "a - b", {number("a"), number("b")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(context.number("a") - context.number("b")); });
    value("Multiply", "Math", "Multiply", "a x b", {number("a", 1), number("b", 1)}, {number("result")},
          [](ExecContext& context, int) { return Value::number(context.number("a") * context.number("b")); });
    value("Divide", "Math", "Divide", "a / b. Dividing by zero gives zero.", {number("a", 1), number("b", 1)}, {number("result")},
          [](ExecContext& context, int) {
              float b = context.number("b");
              return Value::number(b != 0 ? context.number("a") / b : 0.0f);
          });
    value("Sin", "Math", "Sine", "The sine of an angle in degrees: swings between -1 and 1.", {number("degrees")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::sin(radians(context.number("degrees")))); });
    value("Random", "Math", "Random", "A different number each time it is asked, between two limits.", {number("from", 0), number("to", 1)},
          {number("value")}, [](ExecContext& context, int) {
              static Random random{20261119};
              return Value::number(random.range(context.number("from"), context.number("to")));
          });
    value("Time", "Math", "Time", "Seconds since the game started, and the length of this frame.", {}, {number("seconds"), number("dt")},
          [](ExecContext& context, int output) { return Value::number(output == 0 ? float(context.time) : context.dt); });

    // ---- points in space
    value("Vector", "Math", "Vector", "A point or a direction from three numbers.", {number("x"), number("y"), number("z")}, {vector("vector")},
          [](ExecContext& context, int) { return Value::vector(context.number("x"), context.number("y"), context.number("z")); });
    value("Split", "Math", "Split Vector", "The three numbers of a point.", {vector("vector")}, {number("x"), number("y"), number("z")},
          [](ExecContext& context, int output) {
              Vec3 v = context.vector("vector");
              return Value::number(output == 0 ? v.x : output == 1 ? v.y : v.z);
          });
    value("AddVectors", "Math", "Add Vectors", "a + b", {vector("a"), vector("b")}, {vector("result")},
          [](ExecContext& context, int) { return vectorValue(context.vector("a") + context.vector("b")); });
    value("ScaleVector", "Math", "Scale Vector", "A direction made longer or shorter.", {vector("vector"), number("by", 1)}, {vector("result")},
          [](ExecContext& context, int) { return vectorValue(context.vector("vector") * context.number("by")); });

    value("Distance", "Math", "Distance", "How far apart two points are.", {vector("a"), vector("b")}, {number("distance")},
          [](ExecContext& context, int) { return Value::number(simd::distance(context.vector("a"), context.vector("b"))); });
    value("Direction", "Math", "Direction", "The direction from one point to another, one unit long.", {vector("from"), vector("to")}, {vector("direction")},
          [](ExecContext& context, int) { return vectorValue(safeNormalize(context.vector("to") - context.vector("from"), Vec3{0, 0, 0})); });

    // ---- yes and no
    value("Greater", "Logic", "Greater Than", "Yes if a is more than b.", {number("a"), number("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.number("a") > context.number("b")); });
    value("Less", "Logic", "Less Than", "Yes if a is less than b.", {number("a"), number("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.number("a") < context.number("b")); });
    value("And", "Logic", "And", "Yes if both are.", {flag("a"), flag("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.flag("a") && context.flag("b")); });
    value("Or", "Logic", "Or", "Yes if either is.", {flag("a"), flag("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.flag("a") || context.flag("b")); });
    value("Not", "Logic", "Not", "Yes becomes no and no becomes yes.", {flag("value")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(!context.flag("value")); });
    value("Select", "Logic", "Select", "One of two numbers, chosen by a condition.", {flag("condition"), number("ifYes", 1), number("ifNo", 0)},
          {number("result")},
          [](ExecContext& context, int) { return Value::number(context.flag("condition") ? context.number("ifYes") : context.number("ifNo")); });

    // ---- memory
    action("SetVariable", "Variables", "Set Variable", "Remembers a number under a name, for this entity.", {string("name", "score"), number("value")},
           [](ExecContext& context) {
               if (context.state) context.state->variables[context.string("name")] = Value::number(context.number("value"));
           });
    value("GetVariable", "Variables", "Get Variable", "The number remembered under a name; zero if there is none.", {string("name", "score")},
          {number("value")}, [](ExecContext& context, int) {
              if (!context.state) return Value::number(0);
              auto found = context.state->variables.find(context.string("name"));
              return found == context.state->variables.end() ? Value::number(0) : found->second;
          });
}

// ---------------------------------------------------------------- scripts from files

const Graph* loadGraph(const std::string& name, const std::string& filePath) {
    struct Cached {
        Graph graph;
        std::filesystem::file_time_type modified{};
        bool isValid = false;
    };
    static std::unordered_map<std::string, Cached> cache;
    std::error_code code;
    std::filesystem::file_time_type modified = std::filesystem::last_write_time(filePath, code);
    if (code) return nullptr;
    Cached& cached = cache[name + "|" + filePath];
    // Read again when the file is newer: a script saved in the editor takes effect while the game plays.
    if (!cached.isValid || cached.modified != modified) {
        cached.modified = modified;
        std::optional<std::string> contents = text::readFile(filePath);
        std::optional<Graph> graph = contents ? Graph::fromText(*contents) : std::nullopt;
        cached.isValid = graph.has_value();
        if (graph) cached.graph = std::move(*graph);
        else logError("%s is not a visual script", filePath.c_str());
    }
    return cached.isValid ? &cached.graph : nullptr;
}

}  // namespace molten::script

// ---------------------------------------------------------------- the component

namespace molten {

void VisualScript::update(World& world, Entity self, float dt) {
    if (graph.empty()) return;
    if (graph != loadedGraph) {                 // another script was chosen: start over
        loadedGraph = graph;
        state = script::ScriptState{};
    }
    script::registerStandardNodes();
    const script::Graph* loaded = script::loadGraph(graph, world.assetPath(graph));
    if (!loaded) return;
    script::ExecContext context;
    context.world = &world;
    context.scene = &world.scene;
    context.input = &world.input;
    context.physics = &world.physics;
    context.self = self;
    context.dt = dt;
    context.time = world.time.elapsed;
    // The state is copied out and back: running the script may add components and move this one.
    script::ScriptState working = std::move(state);
    context.state = &working;
    script::runFrame(*loaded, context);
    working.collidedWith.clear();
    working.triggeredBy.clear();
    if (VisualScript* still = world.scene.get<VisualScript>(self)) still->state = std::move(working);
}

// What touched the entity is remembered until the script's next run, which fires the events.
void VisualScript::onCollision(World&, Entity, Entity other) {
    state.collidedWith.push_back(other);
}

void VisualScript::onTriggerEnter(World&, Entity, Entity other) {
    state.triggeredBy.push_back(other);
}

MOLTEN_COMPONENT(VisualScript, MOLTEN_FIELD(graph).help("A .graph file from the project's Assets"))

}  // namespace molten
