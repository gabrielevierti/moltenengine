#include "Script/Runtime.hpp"

#include "Script/CppBoxes.hpp"

#include "Animation/Animator.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Saveable.hpp"
#include "Rendering/Particles.hpp"

#include "Core/World.hpp"
#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Rendering/Material.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    Value value = definition->evaluate(context, index);
    Debugger& debugger = Debugger::instance();
    if (debugger.isOn && debugger.watches(context.graphName, context.self)) debugger.values[{node.id, pin}] = value.toText();
    return value;
}

/// Before a node of a flow runs: noted for the debugger, and stopped at if it is a breakpoint
/// (or the next step). True if the flow must stop there.
bool stopsBefore(ExecContext& context, const Node& node) {
    Debugger& debugger = Debugger::instance();
    if (!debugger.isOn) return false;
    if (debugger.watches(context.graphName, context.self)) debugger.ran[node.id] = debugger.now;
    if (debugger.stopped.node != 0) return false;           // already stopped this frame: the rest goes on
    if (!debugger.stepsOnce && !debugger.hasBreakpoint(context.graphName, node.id)) return false;
    debugger.stepsOnce = false;
    debugger.stopped = Debugger::Stop{context.graphName, node.id, context.self};
    debugger.graph = context.graphName;
    debugger.entity = context.self;                         // its values are the ones to show
    debugger.wantsHold = true;
    if (context.state) context.state->waiting.push_back(ScriptState::Waiting{node.id, 0, false, true});
    return true;
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
        if (stopsBefore(context, *next)) return;
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
PinDef string(const char* name, const char* value = "", const char* pick = "") { return PinDef{name, PinType::String, Value::string(value), pick}; }

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

/// An entity as it travels along a wire: "@index:generation", so that two entities of the same name
/// are still told apart (what Nearest, For Each, Raycast and Spawn give). "@none" is nobody.
std::string referenceTo(Entity entity) {
    if (!entity.isValid()) return "@none";
    return "@" + std::to_string(entity.index) + ":" + std::to_string(entity.generation);
}

/// The entity a text names: "self" (or nothing), the one the script is on; "other", the other of a
/// collision or a trigger; "@12:3", that very entity; anything else, the entity of that name.
Entity entityNamed(ExecContext& context, const std::string& text) {
    if (!context.scene) return Entity{};
    if (text.empty() || text == "self") return context.self;
    if (text == "other") return context.other;
    if (text[0] == '@') {
        unsigned index = 0, generation = 0;
        if (std::sscanf(text.c_str(), "@%u:%u", &index, &generation) != 2) return Entity{};
        Entity entity{uint32_t(index), uint32_t(generation)};
        return context.scene->isAlive(entity) ? entity : Entity{};
    }
    return context.scene->find(text);
}

/// The entity a node acts on: what its "on" input says; the script's own if it has none.
Entity subject(ExecContext& context) {
    if (!context.definition || !context.definition->findInput("on")) return context.self;
    return entityNamed(context, context.string("on"));
}

Transform* transformOf(ExecContext& context) {
    return context.scene ? context.scene->get<Transform>(subject(context)) : nullptr;
}

/// A value made the type a variable is declared as.
Value asType(const Value& value, PinType type) {
    switch (type) {
        case PinType::Float: return Value::number(value.asFloat());
        case PinType::Bool: return Value::boolean(value.asBool());
        case PinType::String: return Value::string(value.asString());
        case PinType::Vec3: {
            float x, y, z;
            value.asVector(x, y, z);
            return Value::vector(x, y, z);
        }
        case PinType::Flow: break;
    }
    return value;
}

/// What a node keeps between two runs (the index of a loop, when a cooldown ends), under a name no script would choose.
Value& kept(ExecContext& context, const char* what) {
    static Value none;
    if (!context.state || !context.node) return none = Value{};
    return context.state->variables[std::string("\x01") + what + std::to_string(context.node->id)];
}

}  // namespace

// ---------------------------------------------------------------- the debugger

Debugger& Debugger::instance() {
    static Debugger debugger;
    return debugger;
}

bool Debugger::watches(const std::string& name, Entity self) {
    if (name.empty() || name != graph) return false;
    if (!entity.isValid()) entity = self;                   // the first to run it
    return entity == self;
}

void Debugger::toggleBreakpoint(const std::string& name, int node) {
    auto key = std::make_pair(name, node);
    if (!breakpoints.erase(key)) breakpoints.insert(key);
}

void Debugger::clearSeen() {
    values.clear();
    ran.clear();
    entity = Entity{};
    stopped = Stop{};
    wantsHold = false;
    stepsOnce = false;
}

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

void ExecContext::fail(const std::string& pin, const std::string& message) {
    ScriptProblem problem{graphName, node ? node->id : 0, pin, message, time};
    if (ScriptProblems::instance().report(problem)) {
        const NodeDef* kind = node ? NodeLibrary::instance().find(node->type) : nullptr;
        logWarning("%s, box %s%s%s: %s", graphName.empty() ? "a visual script" : graphName.c_str(), kind ? kind->title.c_str() : "?",
                   pin.empty() ? "" : ", at ", pin.c_str(), message.c_str());
    }
}

ScriptProblems& ScriptProblems::instance() {
    static ScriptProblems problems;
    return problems;
}

bool ScriptProblems::report(const ScriptProblem& problem) {
    for (ScriptProblem& known : list) {
        if (known.graph == problem.graph && known.node == problem.node && known.pin == problem.pin && known.message == problem.message) {
            known.time = problem.time;
            return false;
        }
    }
    if (list.size() > 500) list.erase(list.begin());
    list.push_back(problem);
    return true;
}

void ScriptProblems::clear(const std::string& graph) {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ScriptProblem& p) { return p.graph == graph; }), list.end());
}

std::vector<ScriptProblem> ScriptProblems::of(const std::string& graph) const {
    std::vector<ScriptProblem> out;
    for (const ScriptProblem& p : list)
        if (p.graph == graph) out.push_back(p);
    return out;
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
    // Flows waiting at a Wait or a Wait Until whose moment has come go on from there.
    std::vector<ScriptState::Waiting> waiting = std::move(context.state->waiting);
    context.state->waiting.clear();
    for (const ScriptState::Waiting& wait : waiting) {
        const Node* node = graph.find(wait.node);
        const NodeDef* definition = node ? NodeLibrary::instance().find(node->type) : nullptr;
        if (!definition) continue;
        context.node = node;
        context.definition = definition;
        context.depth = 0;
        if (wait.resumesHere) {
            // Stopped before this node at a breakpoint: it runs now, and its flow goes on.
            if (!definition->execute) continue;
            const char* pin = definition->execute(context);
            if (!context.scene->isAlive(context.self)) return;
            runFlow(context, node, pin);
            if (!context.scene->isAlive(context.self)) return;
            continue;
        }
        bool isTime = wait.isCondition ? context.flag("condition") : context.time >= wait.until;
        if (!isTime) {
            context.state->waiting.push_back(wait);
            continue;
        }
        runFlow(context, node, "then");
        if (!context.scene->isAlive(context.self)) return;
    }
    std::vector<Message> heard = std::move(context.state->heard);
    context.state->heard.clear();
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
        } else if (node.type == "OnActionPressed") {
            happened = context.input && context.input->wasPressed(context.string("action"));
        } else if (node.type == "OnButtonClicked") {
            const kit::UIButton* button = context.scene->get<kit::UIButton>(entityNamed(context, context.string("button")));
            happened = button && button->wasClicked;
        } else if (node.type == "OnClicked") {
            // The pointer went down on this entity's collider, or on one of something under it.
            if (context.input && context.world && context.physics && context.input->wasPressed(MouseButton::Left)) {
                std::optional<RaycastHit> hit = context.physics->raycast(*context.scene, context.world->screenRay(context.input->mousePosition()));
                Entity up = hit ? hit->entity : Entity{};
                for (int depth = 0; depth < 32 && context.scene->isAlive(up) && up != context.self; depth++) {
                    const Transform* transform = context.scene->get<Transform>(up);
                    up = transform ? transform->parent : Entity{};
                }
                if (hit && up == context.self) {
                    kept(context, "point") = vectorValue(hit->point);
                    happened = true;
                }
            }
        } else if (node.type == "OnValueChanged") {
            if (context.world) {
                // The first frame only notes what it is.
                Value& seen = kept(context, "value");
                float now = context.world->value(context.string("value"));
                happened = !isFirstFrame && seen.x != now;
                seen = Value::number(now);
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
        } else if (node.type == "OnMessage") {
            // Once for each message of its name heard since the last frame.
            std::string wanted = context.string("message");
            for (const Message& message : heard) {
                if (!wanted.empty() && message.name != wanted) continue;
                context.message = &message;
                context.node = &node;
                context.definition = definition;
                runFlow(context, &node, "then");
                if (!context.scene->isAlive(context.self)) return;
            }
            context.message = nullptr;
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
    registerCppBoxes();                 // the boxes that hold C++ as it is written (Script/CppBoxes.hpp)

    // ---- events
    event("OnStart", "On Start", "Once, when the game starts or the entity appears.", {}, {});
    event("OnUpdate", "On Update", "Every frame. \"dt\" is the length of the frame in seconds.", {}, {number("dt")},
          [](ExecContext& context, int) { return Value::number(context.dt); });
    event("OnKeyPressed", "On Key Pressed", "When a key goes down.", {string("key", "Space", "key")}, {});
    event("OnKeyReleased", "On Key Released", "When a key comes up.", {string("key", "Space", "key")}, {});
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

    event("OnMessage", "On Message",
          "When a message is sent to this entity, or to everyone: \"doorOpened\". Empty: any message. "
          "It carries a number and a text; \"from\" is the name of who sent it.",
          {string("message", "doorOpened", "message")}, {number("number"), string("text"), string("from"), string("name")},
          [](ExecContext& context, int output) {
              if (!context.message) return Value{};
              if (output == 1) return Value::number(context.message->number);
              if (output == 2) return Value::string(context.message->text);
              if (output == 3) {
                  const Name* name = context.scene->get<Name>(context.message->from);
                  return Value::string(name ? name->value : std::string());
              }
              return Value::string(context.message->name);
          });

    // ---- messages and waiting
    action("SendMessage", "Flow", "Send Message",
           "Tells something happened: to the entity named in \"to\", or to everyone when it is empty. "
           "Scripts hear it in On Message, code in onMessage.",
           {string("message", "doorOpened", "message"), string("to", "", "entity"), number("number", 0), string("text", "")}, [](ExecContext& context) {
               if (!context.world) return;
               std::string to = context.string("to");
               Entity target = to.empty() ? Entity{} : context.scene->find(to);
               if (!to.empty() && !target.isValid()) return;      // to someone who is not there: nobody hears it
               context.world->send(context.string("message"), target, context.number("number"), context.string("text"), context.self);
           });
    {
        NodeDef wait;
        wait.type = "Wait";
        wait.category = "Flow";
        wait.title = "Wait";
        wait.help = "Goes on after so many seconds of game time; the rest of the script runs meanwhile.";
        wait.inputs = {flow("in"), number("seconds", 1)};
        wait.outputs = {flow("then")};
        wait.execute = [](ExecContext& context) -> const char* {
            if (!context.state) return "then";
            context.state->waiting.push_back(ScriptState::Waiting{context.node->id, context.time + std::max(context.number("seconds"), 0.0f), false});
            return nullptr;
        };
        NodeLibrary::instance().add(std::move(wait));
        NodeDef until;
        until.type = "WaitUntil";
        until.category = "Flow";
        until.title = "Wait Until";
        until.help = "Goes on the first frame its condition holds: until the door is open, until the score is 10.";
        until.inputs = {flow("in"), flag("condition")};
        until.outputs = {flow("then")};
        until.execute = [](ExecContext& context) -> const char* {
            if (context.flag("condition") || !context.state) return "then";
            context.state->waiting.push_back(ScriptState::Waiting{context.node->id, 0, true});
            return nullptr;
        };
        NodeLibrary::instance().add(std::move(until));
    }

    action("SaveGame", "Game", "Save Game", "Keeps the game as it is now (everything with a Saveable, the shared values) in a slot.",
           {string("slot", "Save")}, [](ExecContext& context) {
               if (context.world) kit::saveState(*context.world, context.string("slot"));
           });
    action("LoadGame", "Game", "Load Game", "Goes back to a saved game: its scene, as it was.", {string("slot", "Save")}, [](ExecContext& context) {
        if (context.world) kit::loadState(*context.world, context.string("slot"));
    });

    // ---- the game as a whole
    action("AddValue", "Game", "Add to Value", "Adds to a number the whole game shares: the score, coins, lives.",
           {string("name", "score", "value"), number("amount", 1)}, [](ExecContext& context) {
               if (context.world) context.world->addValue(context.string("name"), context.number("amount"));
           });
    action("SetValue", "Game", "Set Value", "Sets a number the whole game shares.", {string("name", "score", "value"), number("value", 0)},
           [](ExecContext& context) {
               if (context.world) context.world->setValue(context.string("name"), context.number("value"));
           });
    value("GetValue", "Game", "Value", "A number the whole game shares; zero if nobody set it.", {string("name", "score", "value")}, {number("value")},
          [](ExecContext& context, int) { return Value::number(context.world ? context.world->value(context.string("name")) : 0.0f); });
    // The quality levels the game's maker made (Project Settings > Quality), for a settings menu.
    action("SetQualityLevel", "Game", "Set Quality Level", "Draws the game at one of its quality levels from the next frame (Project Settings > Quality).",
           {string("level", "High")}, [](ExecContext& context) {
               if (context.world) context.world->setQualityLevel(context.string("level"));
           });
    action("NextQualityLevel", "Game", "Next Quality Level", "The quality level after the one the game is drawn at, back to the first after the last: a button of a settings menu.",
           {}, [](ExecContext& context) {
               if (!context.world) return;
               std::vector<std::string> levels = context.world->qualityLevels();
               if (levels.empty()) return;
               auto at = std::find(levels.begin(), levels.end(), context.world->qualityLevel());
               size_t next = at == levels.end() ? 0 : (size_t(at - levels.begin()) + 1) % levels.size();
               context.world->setQualityLevel(levels[next]);
           });
    value("QualityLevel", "Game", "Quality Level", "The quality level the game is drawn at, and how many levels there are.", {},
          {string("level"), number("count")}, [](ExecContext& context, int output) {
              if (output == 1) return Value::number(context.world ? float(context.world->qualityLevels().size()) : 0.0f);
              return Value::string(context.world ? context.world->qualityLevel() : std::string());
          });
    action("LoadScene", "Game", "Load Scene", "Goes to another scene: the next level, the menu.", {string("scene", "Scenes/Main.scene", "scene")},
           [](ExecContext& context) {
               if (context.world) context.world->loadScene(context.string("scene"));
           });
    action("LoadSceneInBackground", "Game", "Load Scene in Background",
           "Goes to another scene once its models are read, the game going on meanwhile: show a Loading Screen.",
           {string("scene", "Scenes/Main.scene", "scene")}, [](ExecContext& context) {
               if (context.world) context.world->loadSceneInBackground(context.string("scene"));
           });
    action("AddScene", "Game", "Add Scene", "Adds another scene's entities to this one: the next room, a menu over the game.",
           {string("scene", "Scenes/Room.scene", "scene")}, [](ExecContext& context) {
               if (context.world) context.world->addScene(context.string("scene"));
           });
    action("RemoveScene", "Game", "Remove Scene", "Takes away a scene that was added.", {string("scene", "Scenes/Room.scene", "scene")},
           [](ExecContext& context) {
               if (context.world) context.world->removeScene(context.string("scene"));
           });
    action("ReloadScene", "Game", "Restart Scene", "Starts the scene over.", {}, [](ExecContext& context) {
        if (context.world) context.world->reloadScene();
    });
    action("PlaySound", "Game", "Play Sound", "Plays a sound file from the project's Assets.", {string("sound", "Audio/sound.wav", "sound"), number("volume", 1)},
           [](ExecContext& context) {
               if (context.world) context.world->playSound(context.string("sound"), context.number("volume"));
           });
    action("SetVolume", "Game", "Set Volume", "The volume of a group of sounds, 0 to 1: Music, Effects, Voices, Interface.",
           {string("group", "Music", "soundGroup"), number("volume", 1)}, [](ExecContext& context) {
               if (context.world) context.world->setVolume(context.string("group"), context.number("volume"));
           });
    {
        NodeDef spawn;
        spawn.type = "Spawn";
        spawn.category = "Game";
        spawn.title = "Spawn";
        spawn.help = "Adds a prefab to the scene at a point. \"entity\" is what it made: wire it into the \"on\" of other nodes to change it.";
        spawn.inputs = {flow("in"), string("prefab", "Prefabs/Thing.prefab", "prefab"), vector("position")};
        spawn.outputs = {flow("then"), string("entity")};
        spawn.execute = [](ExecContext& context) -> const char* {
            Entity made = context.world ? context.world->spawn(context.string("prefab"), context.vector("position")) : Entity{};
            kept(context, "made") = Value::string(referenceTo(made));
            return "then";
        };
        spawn.evaluate = [](ExecContext& context, int) { return kept(context, "made"); };
        NodeLibrary::instance().add(std::move(spawn));
    }
    value("PositionOf", "Game", "Position Of", "Where the entity with this name is.", {string("name", "Player", "entity")}, {vector("position"), flag("exists")},
          [](ExecContext& context, int output) {
              Entity entity = context.scene->find(context.string("name"));
              const Transform* transform = context.scene->get<Transform>(entity);
              if (output == 1) return Value::boolean(transform != nullptr);
              return vectorValue(transform ? transform->worldPosition() : Vec3{0, 0, 0});
          });
    value("OtherIs", "Game", "Other Is", "In a collision or trigger event: yes if the other thing has this name.", {string("name", "Player", "entity")},
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
           {vector("offset", 0, 0, 0), flag("perSecond", true), flag("local", false), string("on", "self", "target")}, [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) {
                   Vec3 offset = context.vector("offset") * (context.flag("perSecond") ? context.dt : 1.0f);
                   transform->position += context.flag("local") ? rotate(transform->rotation, offset) : offset;
               }
           });
    action("Rotate", "Transform", "Rotate", "Turns the entity around an axis, in degrees. With \"per second\" it is a speed.",
           {vector("axis", 0, 1, 0), number("degrees", 90), flag("perSecond", true), string("on", "self", "target")}, [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) {
                   float angle = radians(context.number("degrees")) * (context.flag("perSecond") ? context.dt : 1.0f);
                   Quat turn = axisAngle(safeNormalize(context.vector("axis")), angle);
                   transform->rotation = simd::normalize(turn * transform->rotation);
               }
           });
    action("SetPosition", "Transform", "Set Position", "Puts the entity at a point.", {vector("position"), string("on", "self", "target")}, [](ExecContext& context) {
        Vec3 position = context.vector("position");
        if (Transform* transform = transformOf(context)) transform->position = position;
        // A body is where physics says it is: tell physics too.
        if (context.physics && context.scene->has<RigidBody>(subject(context))) context.physics->teleport(*context.scene, subject(context), position);
    });
    action("SetScale", "Transform", "Set Scale", "Sets the entity's size along its three axes.", {vector("scale", 1, 1, 1), string("on", "self", "target")},
           [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) transform->scale = context.vector("scale");
           });
    value("GetPosition", "Transform", "Position", "Where the entity is.", {string("on", "self", "target")}, {vector("position")}, [](ExecContext& context, int) {
        Transform* transform = transformOf(context);
        return vectorValue(transform ? transform->position : Vec3{0, 0, 0});
    });
    value("GetForward", "Transform", "Forward", "The direction the entity faces.", {string("on", "self", "target")}, {vector("forward")}, [](ExecContext& context, int) {
        Transform* transform = transformOf(context);
        return vectorValue(transform ? transform->forward() : Vec3{0, 0, -1});
    });

    // ---- physics
    action("SetVelocity", "Physics", "Set Velocity", "Sets how fast the entity's body moves. It needs a Rigid Body.", {vector("velocity"), string("on", "self", "target")},
           [](ExecContext& context) {
               if (RigidBody* body = context.scene->get<RigidBody>(subject(context))) body->velocity = context.vector("velocity");
           });
    action("AddVelocity", "Physics", "Add Velocity", "Adds to the body's velocity: a push, or a jump.", {vector("velocity", 0, 5, 0), string("on", "self", "target")},
           [](ExecContext& context) {
               if (RigidBody* body = context.scene->get<RigidBody>(subject(context))) body->velocity += context.vector("velocity");
           });
    value("GetVelocity", "Physics", "Velocity", "How fast the body moves.", {string("on", "self", "target")}, {vector("velocity")}, [](ExecContext& context, int) {
        const RigidBody* body = context.scene->get<RigidBody>(subject(context));
        return vectorValue(body ? body->velocity : Vec3{0, 0, 0});
    });
    value("IsGrounded", "Physics", "Is Grounded", "Yes while the body rests on something.", {string("on", "self", "target")}, {flag("grounded")},
          [](ExecContext& context, int) {
              const RigidBody* body = context.scene->get<RigidBody>(subject(context));
              return Value::boolean(body && body->isGrounded);
          });

    // ---- the entity
    action("DestroySelf", "Entity", "Destroy Self", "Removes the entity from the scene.", {},
           [](ExecContext& context) { context.scene->destroy(context.self); });
    action("SetVisible", "Entity", "Set Visible", "Shows or hides the entity's mesh.", {flag("visible", true), string("on", "self", "target")}, [](ExecContext& context) {
        if (MeshRenderer* renderer = context.scene->get<MeshRenderer>(subject(context))) renderer->isVisible = context.flag("visible");
    });
    action("Log", "Entity", "Log", "Writes a message to the console.", {string("message", "Hello")},
           [](ExecContext& context) { logInfo("%s", context.string("message").c_str()); });

    // ---- input
    value("KeyIsDown", "Input", "Key Is Down", "Yes while a key is held.", {string("key", "Space", "key")}, {flag("down")},
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

    value("GreaterOrEqual", "Logic", "At Least", "Yes if the first number is greater than the second, or the same.", {number("a"), number("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.number("a") >= context.number("b")); });
    value("LessOrEqual", "Logic", "At Most", "Yes if the first number is less than the second, or the same.", {number("a"), number("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.number("a") <= context.number("b")); });
    value("NotEqual", "Logic", "Not Equal", "Yes if the two numbers are not the same.", {number("a"), number("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.number("a") != context.number("b")); });
    value("Negate", "Math", "Negate", "The number with its sign turned: 3 becomes -3.", {number("value")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(-context.number("value")); });
    value("Floor", "Math", "Floor", "The whole number at or below: 2.7 becomes 2, -2.7 becomes -3.", {number("value")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::floor(context.number("value"))); });

    // ---- any component's fields, the game's own too, by name
    auto componentNamed = [](ExecContext& context, const ComponentType*& type) -> void* {
        type = ComponentRegistry::instance().find(context.string("component"));
        if (!type) {
            context.fail("component", "there is no component \"" + context.string("component") + "\"");
            return nullptr;
        }
        Entity entity = subject(context);
        void* component = type->get && context.scene ? type->get(*context.scene, entity) : nullptr;
        if (!component) context.fail("on", "it has no " + type->name);
        return component;
    };
    value("GetField", "Components", "Get Field", "A field of one of the entity's components, by name: a number, yes-no, vector or text.",
          {string("component", "", "component"), string("field", ""), string("on", "self", "target")}, {number("value")},
          [componentNamed](ExecContext& context, int) {
              const ComponentType* type = nullptr;
              void* component = componentNamed(context, type);
              if (!component) return Value::number(0);
              const Field* field = type->findField(context.string("field"));
              if (!field) {
                  context.fail("field", type->name + " has no field \"" + context.string("field") + "\"");
                  return Value::number(0);
              }
              std::string text = field->valueText(component);
              if (field->type == FieldType::Bool) return Value::boolean(text == "1" || text == "true");
              if (field->type == FieldType::Vec3) return Value::fromText(PinType::Vec3, text);
              if (field->type == FieldType::Float || field->type == FieldType::Int) return Value::number(std::strtof(text.c_str(), nullptr));
              return Value::string(text);
          });
    action("SetField", "Components", "Set Field", "Sets a field of one of the entity's components, by name: the game's own components too.",
           {string("component", "", "component"), string("field", ""), number("value"), string("on", "self", "target")},
           [componentNamed](ExecContext& context) {
               const ComponentType* type = nullptr;
               void* component = componentNamed(context, type);
               if (!component) return;
               const Field* field = type->findField(context.string("field"));
               if (!field) {
                   context.fail("field", type->name + " has no field \"" + context.string("field") + "\"");
                   return;
               }
               Value value = context.input_("value");
               std::string text = field->type == FieldType::Bool ? (value.asBool() ? "1" : "0") : value.toText();
               field->setValueText(component, text);
           });

    // ---- memory
    action("SetVariable", "Variables", "Set Variable", "Sets one of the script's variables (declared at the left of the window), for this entity.",
           {string("name", "score", "variable"), number("value")}, [](ExecContext& context) {
               if (!context.state) return;
               std::string name = context.string("name");
               Value value = context.input_("value");
               // As the variable is declared: a vector stays a vector, a text a text.
               if (const Variable* declared = context.graph ? context.graph->variable(name) : nullptr) {
                   value = asType(value, declared->type);
               } else if (context.graph && !context.graph->variables.empty()) {
                   context.fail("name", "there is no variable \"" + name + "\": declare it at the left (+)");
               }
               context.state->variables[name] = value;
           });
    action("AddToVariable", "Variables", "Change Variable", "Adds to the number remembered under a name (a negative amount takes away): one more coin, one life less.",
           {string("name", "score", "variable"), number("amount", 1)}, [](ExecContext& context) {
               if (!context.state) return;
               Value& kept = context.state->variables[context.string("name")];
               kept = Value::number(kept.asFloat() + context.number("amount"));
           });
    value("GetVariable", "Variables", "Get Variable", "One of the script's variables: what it was last set to, or its first value.",
          {string("name", "score", "variable")}, {number("value")}, [](ExecContext& context, int) {
              std::string name = context.string("name");
              const Variable* declared = context.graph ? context.graph->variable(name) : nullptr;
              if (context.state) {
                  auto found = context.state->variables.find(name);
                  if (found != context.state->variables.end()) return found->second;
              }
              if (declared) return Value::fromText(declared->type, declared->value);
              if (context.graph && !context.graph->variables.empty()) context.fail("name", "there is no variable \"" + name + "\"");
              return Value::number(0);
          });

    // ---- animation: the entity's Animator, as code drives it (setFloat, setBool, setTrigger,
    // crossFade, play). Its events come as messages: On Message hears them.
    auto animatorOf = [](ExecContext& context) -> Animator* { return context.scene ? context.scene->get<Animator>(subject(context)) : nullptr; };
    action("SetAnimatorFloat", "Animation", "Set Animator Float", "Sets a number parameter of the entity's Animator Controller: speed, direction.",
           {string("parameter", "speed", "parameter"), number("value"), string("on", "self", "target")}, [animatorOf](ExecContext& context) {
               if (Animator* animator = animatorOf(context)) animator->setFloat(context.string("parameter"), context.number("value"));
           });
    action("SetAnimatorBool", "Animation", "Set Animator Bool", "Sets a yes-or-no parameter of the entity's Animator Controller: grounded, open.",
           {string("parameter", "grounded", "parameter"), flag("value", true), string("on", "self", "target")}, [animatorOf](ExecContext& context) {
               if (Animator* animator = animatorOf(context)) animator->setBool(context.string("parameter"), context.flag("value"));
           });
    action("SetAnimatorTrigger", "Animation", "Set Animator Trigger",
           "Fires a trigger of the entity's Animator Controller: jump, open. It stays set until a transition that waits for it is taken.",
           {string("parameter", "jump", "parameter"), string("on", "self", "target")}, [animatorOf](ExecContext& context) {
               if (Animator* animator = animatorOf(context)) animator->setTrigger(context.string("parameter"));
           });
    action("CrossFade", "Animation", "Play State", "Goes to a state of the Animator Controller now, easing into it over some seconds (0: at once), whatever its transitions say.",
           {string("state", "", "state"), number("seconds", 0.2f), string("on", "self", "target")}, [animatorOf](ExecContext& context) {
               if (Animator* animator = animatorOf(context)) animator->crossFade(context.string("state"), context.number("seconds"));
           });
    action("PlayAnimation", "Animation", "Play Animation", "Plays one of the entity's animations by its name (with a controller: a state of it), from its start.",
           {string("name", "", "ownAnimation"), string("on", "self", "target")}, [animatorOf](ExecContext& context) {
               if (Animator* animator = animatorOf(context)) animator->play(context.string("name"));
           });
    action("StopAnimation", "Animation", "Stop Animation", "Stops the entity's animation, back at its start.", {string("on", "self", "target")}, [animatorOf](ExecContext& context) {
        if (Animator* animator = animatorOf(context)) animator->stop();
    });
    action("PauseAnimation", "Animation", "Pause Animation", "Stops the entity's animation where it is.", {string("on", "self", "target")}, [animatorOf](ExecContext& context) {
        if (Animator* animator = animatorOf(context)) animator->pause();
    });
    action("ResumeAnimation", "Animation", "Resume Animation", "Goes on with the animation from where it was paused.", {string("on", "self", "target")}, [animatorOf](ExecContext& context) {
        if (Animator* animator = animatorOf(context)) animator->resume();
    });
    value("IsAnimationPlaying", "Animation", "Is Animation Playing", "Whether the entity is playing that animation (or is in that state).",
          {string("name", "", "ownAnimation"), string("on", "self", "target")}, {flag("playing")}, [animatorOf](ExecContext& context, int) {
              Animator* animator = animatorOf(context);
              return Value::boolean(animator && animator->isPlayingAnimation(context.string("name")));
          });
    action("SetAnimatorSpeed", "Animation", "Set Animator Speed", "How fast the Animator plays: 1 as made, 0 stopped, 2 twice as fast.", {number("speed", 1), string("on", "self", "target")},
           [animatorOf](ExecContext& context) {
               if (Animator* animator = animatorOf(context)) animator->speed = context.number("speed");
           });
    value("GetAnimatorFloat", "Animation", "Animator Parameter", "The value of a parameter of the entity's Animator Controller (a yes is 1).",
          {string("parameter", "speed", "parameter"), string("on", "self", "target")}, {number("value")}, [animatorOf](ExecContext& context, int) {
              Animator* animator = animatorOf(context);
              return Value::number(animator ? animator->parameter(context.string("parameter")) : 0.0f);
          });
    value("AnimatorState", "Animation", "Animator State", "The state the entity's Animator is in, and whether it is the one named; and whether a clip that does not loop has ended.",
          {string("state", "", "state"), string("on", "self", "target")}, {string("current"), flag("isIn"), flag("hasEnded")}, [animatorOf](ExecContext& context, int output) {
              Animator* animator = animatorOf(context);
              if (!animator) return output == 0 ? Value::string("") : Value::boolean(false);
              if (output == 0) return Value::string(animator->currentState());
              if (output == 1) return Value::boolean(animator->currentState() == context.string("state"));
              return Value::boolean(animator->isFinished());
          });

    // ================================================================ what a whole game needs, without code

    // ---- more events
    event("OnActionPressed", "On Action Pressed",
          "When a button action of the Input settings goes down: \"jump\", \"fire\". The same for a key, a controller and a touch button.",
          {string("action", "jump", "action")}, {});
    event("OnButtonClicked", "On Button Clicked", "When a Button of the interface is clicked or tapped.", {string("button", "", "entity")}, {});
    event("OnClicked", "On Clicked", "When this entity is clicked or tapped in the world. It needs a Collider. \"point\" is where.", {}, {vector("point")},
          [](ExecContext& context, int) { return kept(context, "point"); });
    event("OnValueChanged", "On Value Changed", "When a shared value changes: the score, the lives, the keys collected.", {string("value", "score", "value")},
          {number("now")}, [](ExecContext& context, int) { return Value::number(context.world ? context.world->value(context.string("value")) : 0.0f); });

    // ---- loops: again while something holds, or for each number of a range
    {
        NodeDef loop;
        loop.type = "While";
        loop.category = "Flow";
        loop.title = "While";
        loop.help = "Does what follows \"do\" again and again while the condition holds, then goes on from \"done\". (At most 10000 times a frame.)";
        loop.inputs = {flow("in"), flag("condition")};
        loop.outputs = {flow("do"), flow("done")};
        loop.execute = [](ExecContext& context) -> const char* {
            const Node* here = context.node;
            const NodeDef* definition = context.definition;
            int turns = 0;
            for (; turns < kMaximumSteps; turns++) {
                context.node = here;
                context.definition = definition;
                if (!context.flag("condition")) break;
                runFlow(context, here, "do");
                if (!context.scene->isAlive(context.self)) return nullptr;
            }
            context.node = here;
            context.definition = definition;
            if (turns == kMaximumSteps) context.fail("condition", "it still held after 10000 turns: the loop was stopped");
            return "done";
        };
        NodeLibrary::instance().add(std::move(loop));

        NodeDef count;
        count.type = "For";
        count.category = "Flow";
        count.title = "For";
        count.help = "Does what follows \"do\" once for each number from \"from\" up to (not including) \"to\", a step at a time; "
                     "\"index\" is the number of the turn. Then goes on from \"done\".";
        count.inputs = {flow("in"), number("from", 0), number("to", 10), number("step", 1)};
        count.outputs = {flow("do"), flow("done"), number("index")};
        count.execute = [](ExecContext& context) -> const char* {
            const Node* here = context.node;
            const NodeDef* definition = context.definition;
            float from = context.number("from"), to = context.number("to"), step = context.number("step");
            if (step == 0) {
                context.fail("step", "a step of 0 never gets there");
                return "done";
            }
            int turns = 0;
            for (float i = from; (step > 0 ? i < to : i > to) && turns < kMaximumSteps; i += step, turns++) {
                context.node = here;
                kept(context, "index") = Value::number(i);
                runFlow(context, here, "do");
                if (!context.scene->isAlive(context.self)) return nullptr;
            }
            context.node = here;
            context.definition = definition;
            return "done";
        };
        count.evaluate = [](ExecContext& context, int) { return kept(context, "index"); };
        NodeLibrary::instance().add(std::move(count));

        NodeDef stop;
        stop.type = "Return";
        stop.category = "Flow";
        stop.title = "Return";
        stop.help = "Stops here: nothing after it in this flow is done (C++'s return).";
        stop.inputs = {flow("in")};
        stop.execute = [](ExecContext&) -> const char* { return nullptr; };
        NodeLibrary::instance().add(std::move(stop));

        // A line of C++ that no box says yet. It is kept as written, and written back as it is;
        // run as a visual script it does nothing, and says so on the box.
        NodeDef code;
        code.type = "Code";
        code.category = "C++";
        code.title = "C++";
        code.help = "A line of C++ that no box says yet: kept as it is written, and written back the same. A visual script does not run it: "
                    "convert the script to C++ to run it, or replace it with boxes.";
        code.inputs = {flow("in"), string("code", "")};
        code.outputs = {flow("then")};
        code.execute = [](ExecContext& context) -> const char* {
            context.fail("", "C++ that no box says: it runs only as C++ (Convert to C++)");
            return "then";
        };
        NodeLibrary::instance().add(std::move(code));
    }

    // ---- more flow: one after the other, again and again, only so often
    {
        NodeDef sequence;
        sequence.type = "Sequence";
        sequence.category = "Flow";
        sequence.title = "Sequence";
        sequence.help = "Does what follows \"first\", then what follows \"second\", then \"third\": several things from one event, kept apart.";
        sequence.inputs = {flow("in")};
        sequence.outputs = {flow("first"), flow("second"), flow("third")};
        sequence.execute = [](ExecContext& context) -> const char* {
            const Node* here = context.node;
            for (const char* pin : {"first", "second"}) {
                runFlow(context, here, pin);
                if (!context.scene->isAlive(context.self)) return nullptr;
            }
            return "third";
        };
        NodeLibrary::instance().add(std::move(sequence));
    }
    {
        NodeDef repeat;
        repeat.type = "Repeat";
        repeat.category = "Flow";
        repeat.title = "Repeat";
        repeat.help = "Does what follows \"loop\" so many times, at once (\"index\" counts from 0), then goes on from \"done\": a row of ten coins.";
        repeat.inputs = {flow("in"), number("times", 3)};
        repeat.outputs = {flow("loop"), number("index"), flow("done")};
        repeat.execute = [](ExecContext& context) -> const char* {
            const Node* here = context.node;
            int times = std::clamp(int(context.number("times")), 0, 10000);
            for (int i = 0; i < times; i++) {
                kept(context, "index") = Value::number(float(i));
                runFlow(context, here, "loop");
                if (!context.scene->isAlive(context.self)) return nullptr;
            }
            return "done";
        };
        repeat.evaluate = [](ExecContext& context, int) { return kept(context, "index"); };
        NodeLibrary::instance().add(std::move(repeat));
    }
    {
        NodeDef each;
        each.type = "ForEachWithTag";
        each.category = "Flow";
        each.title = "For Each With Tag";
        each.help = "Does what follows \"loop\" once for every entity with a tag: wire \"entity\" into the \"on\" of other nodes. Then goes on from \"done\".";
        each.inputs = {flow("in"), string("tag", "Enemy", "tag")};
        each.outputs = {flow("loop"), string("entity"), flow("done")};
        each.execute = [](ExecContext& context) -> const char* {
            const Node* here = context.node;
            std::string wanted = context.string("tag");
            std::vector<Entity> all;
            context.scene->each<Tag>([&](Entity entity, Tag& tag) {
                if (tag.value == wanted) all.push_back(entity);
            });
            for (Entity entity : all) {
                if (!context.scene->isAlive(entity)) continue;
                kept(context, "item") = Value::string(referenceTo(entity));
                runFlow(context, here, "loop");
                if (!context.scene->isAlive(context.self)) return nullptr;
            }
            return "done";
        };
        each.evaluate = [](ExecContext& context, int) { return kept(context, "item"); };
        NodeLibrary::instance().add(std::move(each));
    }
    {
        // Do Once, Cooldown, Flip Flop and Chance choose whether and where the flow goes on: they are written out.
        NodeDef once;
        once.type = "DoOnce";
        once.category = "Flow";
        once.title = "Do Once";
        once.help = "Lets the flow through the first time only: a door that opens once, a hint shown once.";
        once.inputs = {flow("in")};
        once.outputs = {flow("then")};
        once.execute = [](ExecContext& context) -> const char* {
            Value& done = kept(context, "once");
            if (done.x != 0) return nullptr;
            done = Value::number(1);
            return "then";
        };
        NodeLibrary::instance().add(std::move(once));
        NodeDef cooldown;
        cooldown.type = "Cooldown";
        cooldown.category = "Flow";
        cooldown.title = "Cooldown";
        cooldown.help = "Lets the flow through, then not again until so many seconds have passed: a gun that fires twice a second, damage once a second.";
        cooldown.inputs = {flow("in"), number("seconds", 0.5f)};
        cooldown.outputs = {flow("then")};
        cooldown.execute = [](ExecContext& context) -> const char* {
            Value& next = kept(context, "gate");
            if (float(context.time) < next.x) return nullptr;
            next = Value::number(float(context.time) + std::max(context.number("seconds"), 0.0f));
            return "then";
        };
        NodeLibrary::instance().add(std::move(cooldown));
        NodeDef flip;
        flip.type = "FlipFlop";
        flip.category = "Flow";
        flip.title = "Flip Flop";
        flip.help = "Goes one way the first time, the other way the next, and so on: a switch that turns a light on, then off.";
        flip.inputs = {flow("in")};
        flip.outputs = {flow("a"), flow("b")};
        flip.execute = [](ExecContext& context) -> const char* {
            Value& side = kept(context, "toggle");
            side = Value::number(side.x != 0 ? 0.0f : 1.0f);
            return side.x != 0 ? "a" : "b";
        };
        NodeLibrary::instance().add(std::move(flip));
        NodeDef chance;
        chance.type = "Chance";
        chance.category = "Flow";
        chance.title = "Chance";
        chance.help = "Goes on from \"yes\" so many times in a hundred, and from \"no\" the others: an enemy that drops a coin one time in four.";
        chance.inputs = {flow("in"), number("percent", 50)};
        chance.outputs = {flow("yes"), flow("no")};
        chance.execute = [](ExecContext& context) -> const char* {
            static Random random{20261121};
            return random.range(0.0f, 100.0f) < context.number("percent") ? "yes" : "no";
        };
        NodeLibrary::instance().add(std::move(chance));
    }

    // ---- other entities
    action("Destroy", "Entity", "Destroy", "Removes an entity from the scene: the other of a collision, one found by name, the nearest with a tag.",
           {string("on", "other", "target")}, [](ExecContext& context) {
               Entity entity = subject(context);
               if (context.scene->isAlive(entity)) context.scene->destroy(entity);
           });
    action("SetActive", "Entity", "Set Active", "Switches an entity on or off, with everything under it: off, it is not drawn, does not collide and its components do not run.",
           {flag("active", true), string("on", "self", "target")}, [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) transform->isActive = context.flag("active");
           });
    value("IsActive", "Entity", "Is Active", "Yes while an entity (and everything above it) is switched on.", {string("on", "self", "target")}, {flag("active")},
          [](ExecContext& context, int) {
              const Transform* transform = transformOf(context);
              return Value::boolean(transform && transform->isActive && transform->isActiveInHierarchy);
          });
    value("Exists", "Entity", "Exists", "Yes while an entity is in the scene: is the player still there, is the boss gone.", {string("on", "Player", "target")},
          {flag("exists")}, [](ExecContext& context, int) { return Value::boolean(context.scene->isAlive(subject(context))); });
    value("NameOf", "Entity", "Name Of", "An entity's name, as the Hierarchy shows it.", {string("on", "other", "target")}, {string("name")},
          [](ExecContext& context, int) {
              const Name* name = context.scene->get<Name>(subject(context));
              return Value::string(name ? name->value : std::string());
          });
    value("HasTag", "Entity", "Has Tag", "Yes if an entity has that tag: is what I hit an enemy.", {string("tag", "Enemy", "tag"), string("on", "other", "target")},
          {flag("result")}, [](ExecContext& context, int) {
              const Tag* tag = context.scene->get<Tag>(subject(context));
              return Value::boolean(tag && tag->value == context.string("tag"));
          });
    value("CountWithTag", "Entity", "Count With Tag", "How many entities have that tag: how many enemies are left.", {string("tag", "Enemy", "tag")},
          {number("count")}, [](ExecContext& context, int) {
              std::string wanted = context.string("tag");
              int count = 0;
              context.scene->each<Tag>([&](Entity, Tag& tag) { count += tag.value == wanted ? 1 : 0; });
              return Value::number(float(count));
          });
    value("Nearest", "Entity", "Nearest", "The nearest entity with a tag, from where this one is: the nearest enemy, the nearest coin. Wire \"entity\" into the \"on\" of other nodes.",
          {string("tag", "Enemy", "tag"), number("within", 1000)}, {string("entity"), flag("exists"), number("distance"), vector("position")},
          [](ExecContext& context, int output) {
              const Transform* own = context.scene->get<Transform>(context.self);
              Vec3 from = own ? own->worldPosition() : Vec3{0, 0, 0};
              std::string wanted = context.string("tag");
              float best = std::max(context.number("within"), 0.0f);
              Entity found;
              Vec3 where{0, 0, 0};
              Entity self = context.self;
              context.scene->each<Tag, Transform>([&](Entity entity, Tag& tag, Transform& transform) {
                  if (entity == self || tag.value != wanted || !transform.isActiveInHierarchy) return;
                  float distance = simd::distance(transform.worldPosition(), from);
                  if (distance <= best) best = distance, found = entity, where = transform.worldPosition();
              });
              if (output == 0) return Value::string(referenceTo(found));
              if (output == 1) return Value::boolean(found.isValid());
              if (output == 2) return Value::number(found.isValid() ? best : 0.0f);
              return vectorValue(where);
          });
    value("DistanceTo", "Entity", "Distance To", "How far this entity is from another.", {string("to", "Player", "target")}, {number("distance")},
          [](ExecContext& context, int) {
              const Transform* own = context.scene->get<Transform>(context.self);
              const Transform* other = context.scene->get<Transform>(entityNamed(context, context.string("to")));
              return Value::number(own && other ? simd::distance(own->worldPosition(), other->worldPosition()) : 0.0f);
          });
    action("SetTint", "Entity", "Set Color", "Tints an entity's mesh: red when it is hurt, green when it is chosen. White (1 1 1) is as it was made.",
           {vector("color", 1, 1, 1), string("on", "self", "target")}, [](ExecContext& context) {
               if (MeshRenderer* renderer = context.scene->get<MeshRenderer>(subject(context))) {
                   Vec3 color = context.vector("color");
                   renderer->tint = Vec4{color.x, color.y, color.z, renderer->tint.w};
               }
           });

    // ---- more of the transform
    action("LookAt", "Transform", "Look At", "Turns the entity to face a point. Upright: it only turns round, as a person does; off, it also tilts, as a turret does.",
           {vector("point"), flag("upright", true), string("on", "self", "target")}, [](ExecContext& context) {
               Transform* transform = transformOf(context);
               if (!transform) return;
               Vec3 to = context.vector("point") - transform->position;
               if (context.flag("upright")) to.y = 0;
               if (simd::length(to) > 1e-4f) transform->rotation = lookRotation(simd::normalize(to));
           });
    action("MoveToward", "Transform", "Move Toward", "Moves the entity toward a point at a speed, and stops there: a door that slides open, an enemy that comes closer.",
           {vector("point"), number("speed", 2), string("on", "self", "target")}, [](ExecContext& context) {
               Transform* transform = transformOf(context);
               if (!transform) return;
               Vec3 point = context.vector("point"), to = point - transform->position;
               float distance = simd::length(to), step = std::max(context.number("speed"), 0.0f) * context.dt;
               transform->position = distance <= step || distance < 1e-5f ? point : transform->position + to * (step / distance);
           });
    action("SetRotation", "Transform", "Set Rotation", "Sets the way the entity faces, in degrees round its x, y and z axes.",
           {vector("degrees"), string("on", "self", "target")}, [](ExecContext& context) {
               if (Transform* transform = transformOf(context)) {
                   Vec3 angles = context.vector("degrees");
                   transform->rotation = eulerAngles(radians(angles.x), radians(angles.y), radians(angles.z));
               }
           });
    value("GetRotation", "Transform", "Rotation", "The way the entity faces, in degrees round its x, y and z axes.", {string("on", "self", "target")},
          {vector("degrees")}, [](ExecContext& context, int) {
              const Transform* transform = transformOf(context);
              Vec3 angles = transform ? toEulerAngles(transform->rotation) : Vec3{0, 0, 0};
              return Value::vector(degrees(angles.x), degrees(angles.y), degrees(angles.z));
          });

    // ---- more of physics
    value("Raycast", "Physics", "Raycast", "Looks along a line for the first collider in the way: what a shot hits, whether there is ground ahead, whether the player is in sight.",
          {vector("from"), vector("direction", 0, 0, -1), number("distance", 100)}, {flag("hit"), vector("point"), number("distance"), string("entity")},
          [](ExecContext& context, int output) {
              std::optional<RaycastHit> hit;
              Vec3 direction = context.vector("direction");
              if (context.physics && simd::length(direction) > 1e-5f) {
                  hit = context.physics->raycast(*context.scene, Ray{context.vector("from"), simd::normalize(direction)}, std::max(context.number("distance"), 0.0f),
                                                 0xFFFFFFFFu, context.self);
              }
              if (output == 0) return Value::boolean(hit.has_value());
              if (output == 1) return vectorValue(hit ? hit->point : Vec3{0, 0, 0});
              if (output == 2) return Value::number(hit ? hit->distance : 0.0f);
              return Value::string(referenceTo(hit ? hit->entity : Entity{}));
          });

    // ---- input by action: the same for keys, a controller and touch
    value("ActionIsDown", "Input", "Action Is Down", "Yes while a button action of the Input settings is held: \"fire\", \"run\".", {string("action", "jump", "action")},
          {flag("down")}, [](ExecContext& context, int) { return Value::boolean(context.input && context.input->isDown(context.string("action"))); });
    value("Axis", "Input", "Axis", "An axis action of the Input settings, from -1 to 1: \"moveX\", \"moveZ\". Keys, a stick and a touch stick all move it.",
          {string("axis", "moveX", "axis")}, {number("value")},
          [](ExecContext& context, int) { return Value::number(context.input ? context.input->axis(context.string("axis")) : 0.0f); });
    value("PointerPosition", "Input", "Pointer Position", "Where the mouse or the finger is on the screen, in points from its top left corner.", {},
          {number("x"), number("y")}, [](ExecContext& context, int output) {
              Vec2 at = context.input ? context.input->mousePosition() : Vec2{0, 0};
              return Value::number(output == 0 ? at.x : at.y);
          });
    value("PointerMoved", "Input", "Pointer Moved", "How far the mouse moved this frame, in points: for looking around.", {}, {number("x"), number("y")},
          [](ExecContext& context, int output) {
              Vec2 moved = context.input ? context.input->mouseDelta() : Vec2{0, 0};
              return Value::number(output == 0 ? moved.x : moved.y);
          });
    value("PointerInWorld", "Input", "Pointer In World", "Where the mouse or the finger points in the world: on the first collider under it, or else on the ground (height 0). For moving to where one clicks.",
          {}, {vector("point"), flag("hit"), string("entity")}, [](ExecContext& context, int output) {
              std::optional<RaycastHit> hit;
              Vec3 point{0, 0, 0};
              bool found = false;
              if (context.world && context.input) {
                  Ray ray = context.world->screenRay(context.input->mousePosition());
                  if (context.physics) hit = context.physics->raycast(*context.scene, ray);
                  if (hit) {
                      point = hit->point;
                      found = true;
                  } else if (std::abs(ray.direction.y) > 1e-5f && -ray.origin.y / ray.direction.y > 0) {
                      point = ray.at(-ray.origin.y / ray.direction.y);
                      found = true;
                  }
              }
              if (output == 1) return Value::boolean(found);
              if (output == 2) return Value::string(referenceTo(hit ? hit->entity : Entity{}));
              return vectorValue(point);
          });

    // ---- more numbers
    value("Clamp", "Math", "Clamp", "The value, kept between two limits.", {number("value"), number("least", 0), number("most", 1)}, {number("result")},
          [](ExecContext& context, int) {
              float least = context.number("least"), most = context.number("most");
              return Value::number(std::clamp(context.number("value"), std::min(least, most), std::max(least, most)));
          });
    value("Lerp", "Math", "Blend", "A number between a and b: a at 0, b at 1, half way at 0.5.", {number("a"), number("b", 1), number("t", 0.5f)}, {number("result")},
          [](ExecContext& context, int) { return Value::number(lerp(context.number("a"), context.number("b"), context.number("t"))); });
    value("Min", "Math", "Smaller", "The smaller of two numbers.", {number("a"), number("b")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::min(context.number("a"), context.number("b"))); });
    value("Max", "Math", "Larger", "The larger of two numbers.", {number("a"), number("b")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::max(context.number("a"), context.number("b"))); });
    value("Abs", "Math", "Absolute", "The number without its sign: -3 is 3.", {number("value")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::abs(context.number("value"))); });
    value("Round", "Math", "Round", "The nearest whole number.", {number("value")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::round(context.number("value"))); });
    value("Modulo", "Math", "Remainder", "What is left of a divided by b: 7 by 3 leaves 1. For things that come round again.", {number("a"), number("b", 2)}, {number("result")},
          [](ExecContext& context, int) {
              float b = context.number("b");
              return Value::number(b != 0 ? std::fmod(context.number("a"), b) : 0.0f);
          });
    value("Power", "Math", "Power", "a raised to b: 3 to the 2 is 9.", {number("a"), number("b", 2)}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::pow(context.number("a"), context.number("b"))); });
    value("Sqrt", "Math", "Square Root", "The number that times itself gives this one. Of a negative number: zero.", {number("value")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::sqrt(std::max(context.number("value"), 0.0f))); });
    value("Cos", "Math", "Cosine", "The cosine of an angle in degrees: with Sine, a point going round a circle.", {number("degrees")}, {number("result")},
          [](ExecContext& context, int) { return Value::number(std::cos(radians(context.number("degrees")))); });
    value("Equal", "Logic", "Equal", "Yes if two numbers are the same.", {number("a"), number("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(std::abs(context.number("a") - context.number("b")) < 1e-4f); });

    // ---- more points in space
    value("SubtractVectors", "Math", "Subtract Vectors", "a - b: the way from b to a.", {vector("a"), vector("b")}, {vector("result")},
          [](ExecContext& context, int) { return vectorValue(context.vector("a") - context.vector("b")); });
    value("Length", "Math", "Length", "How long a direction is: how fast a velocity is.", {vector("vector")}, {number("length")},
          [](ExecContext& context, int) { return Value::number(simd::length(context.vector("vector"))); });
    value("Normalize", "Math", "Normalize", "The same direction, one unit long.", {vector("vector")}, {vector("result")},
          [](ExecContext& context, int) { return vectorValue(safeNormalize(context.vector("vector"), Vec3{0, 0, 0})); });
    value("Dot", "Math", "Dot", "How much two directions agree: 1 the same way, 0 across, -1 opposite (for directions one unit long).", {vector("a"), vector("b")},
          {number("result")}, [](ExecContext& context, int) { return Value::number(simd::dot(context.vector("a"), context.vector("b"))); });
    value("Cross", "Math", "Cross", "The direction across both a and b.", {vector("a"), vector("b")}, {vector("result")},
          [](ExecContext& context, int) { return vectorValue(simd::cross(context.vector("a"), context.vector("b"))); });
    value("LerpVectors", "Math", "Blend Vectors", "A point between a and b: a at 0, b at 1.", {vector("a"), vector("b"), number("t", 0.5f)}, {vector("result")},
          [](ExecContext& context, int) { return vectorValue(lerp(context.vector("a"), context.vector("b"), context.number("t"))); });
    value("RandomPoint", "Math", "Random Point", "A different point each time it is asked, within a distance of a center; flat: at the center's height.",
          {vector("center"), number("radius", 5), flag("flat", true)}, {vector("point")}, [](ExecContext& context, int) {
              static Random random{20261122};
              Vec3 offset = random.insideUnitSphere() * std::max(context.number("radius"), 0.0f);
              if (context.flag("flat")) offset.y = 0;
              return vectorValue(context.vector("center") + offset);
          });

    // ---- words
    value("Text", "Text", "Text", "Some words.", {string("value", "Hello")}, {string("text")},
          [](ExecContext& context, int) { return Value::string(context.string("value")); });
    value("JoinText", "Text", "Join", "Two texts, one after the other.", {string("a"), string("b")}, {string("text")},
          [](ExecContext& context, int) { return Value::string(context.string("a") + context.string("b")); });
    value("NumberToText", "Text", "Number To Text", "A number as words, with so many digits after the point.", {number("number"), number("decimals", 0)},
          {string("text")}, [](ExecContext& context, int) {
              char buffer[48];
              std::snprintf(buffer, sizeof(buffer), "%.*f", std::clamp(int(context.number("decimals")), 0, 6), double(context.number("number")));
              return Value::string(buffer);
          });
    value("TextEquals", "Text", "Text Equals", "Yes if two texts are the same.", {string("a"), string("b")}, {flag("result")},
          [](ExecContext& context, int) { return Value::boolean(context.string("a") == context.string("b")); });
    action("SetText", "Text", "Set Text", "Sets a shared text: a Text of the interface shows it as {name}. \"Game over\", the name of the level.",
           {string("name", "message", "value"), string("text", "")}, [](ExecContext& context) {
               if (context.world) context.world->setText(context.string("name"), context.string("text"));
           });
    value("GetText", "Text", "Get Text", "A shared text: what a Text Field holds, what Set Text set.", {string("name", "message", "value")}, {string("text")},
          [](ExecContext& context, int) { return Value::string(context.world ? context.world->text(context.string("name")) : std::string()); });

    // ---- the game itself
    action("SetTimeScale", "Game", "Set Game Speed", "How fast the game's time passes: 1 as usual, 0.3 slow motion, 0 stopped (a pause).", {number("speed", 1)},
           [](ExecContext& context) {
               if (context.world) context.world->time.timeScale = std::clamp(context.number("speed"), 0.0f, 10.0f);
           });
    action("Damage", "Game", "Damage", "Takes health from an entity that has a Health: with none left it is gone, and what its Health says happens.",
           {number("amount", 1), string("on", "other", "target")}, [](ExecContext& context) {
               Entity entity = subject(context);
               kit::Health* health = context.world ? context.scene->get<kit::Health>(entity) : nullptr;
               if (health) health->damage(*context.world, entity, context.number("amount"));
           });
    value("GetHealth", "Game", "Health", "How much health an entity with a Health has left.", {string("on", "self", "target")}, {number("health")},
          [](ExecContext& context, int) {
              const kit::Health* health = context.scene->get<kit::Health>(subject(context));
              return Value::number(health ? health->amount : 0.0f);
          });
    action("PlayParticles", "Effects", "Play Particles", "Starts or stops a Particle System's emitting: smoke when the engine starts, sparks while it grinds.",
           {flag("playing", true), string("on", "self", "target")}, [](ExecContext& context) {
               if (ParticleEmitter* emitter = context.scene->get<ParticleEmitter>(subject(context))) emitter->isEmitting = context.flag("playing");
           });
    action("BurstParticles", "Effects", "Burst Particles", "Makes a Particle System emit so many particles at once: a puff, a splash, a shower of sparks.",
           {number("count", 20), string("on", "self", "target")}, [](ExecContext& context) {
               if (ParticleEmitter* emitter = context.scene->get<ParticleEmitter>(subject(context))) emitter->burst(std::max(int(context.number("count")), 0));
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
    context.graphName = graph;
    script::Debugger::instance().now = world.time.elapsed;
    // The state is copied out and back: running the script may add components and move this one.
    script::ScriptState working = std::move(state);
    context.state = &working;
    script::runFrame(*loaded, context);
    working.collidedWith.clear();
    working.triggeredBy.clear();
    if (VisualScript* still = world.scene.get<VisualScript>(self)) {
        // Messages that came while it ran (its own, to itself): heard next frame.
        for (Message& message : still->state.heard) working.heard.push_back(std::move(message));
        still->state = std::move(working);
    }
}

void VisualScript::onMessage(World&, Entity, const Message& message) {
    state.heard.push_back(message);
}

// What touched the entity is remembered until the script's next run, which fires the events.
void VisualScript::onCollision(World&, Entity, Entity other) {
    state.collidedWith.push_back(other);
}

void VisualScript::onTriggerEnter(World&, Entity, Entity other) {
    state.triggeredBy.push_back(other);
}

MOLTEN_COMPONENT(VisualScript, MOLTEN_FIELD(graph).picks("graph").help("A .graph file from the project's Assets"))

}  // namespace molten
