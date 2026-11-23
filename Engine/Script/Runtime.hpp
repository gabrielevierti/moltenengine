// Runtime.hpp
// Runs visual scripts (Script/Graph).
//
// A script is run once per frame for every entity that has a VisualScript component.
// Each run looks at the script's event nodes (On Start, On Update, On Key Pressed...)
// and, for those that happened, follows the white wires from node to node. A node that
// needs a value asks the node its input is wired to, which may ask others in turn.
#pragma once

#include "Game/Component.hpp"
#include "Input/Input.hpp"
#include "Physics/Physics.hpp"
#include "Script/Graph.hpp"

#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace molten::script {

/// What one entity's copy of a script remembers between frames.
struct ScriptState {
    std::unordered_map<std::string, Value> variables;
    bool hasStarted = false;
    std::vector<Entity> collidedWith, triggeredBy;      // since the script last ran
    std::vector<Message> heard;                         // messages since the script last ran
    /// Flows stopped at a Wait (until a time) or a Wait Until (until its condition holds).
    struct Waiting {
        int node = 0;
        double until = 0;
        bool isCondition = false;
        bool resumesHere = false;       // stopped at a breakpoint: the node itself runs when the game goes on
    };
    std::vector<Waiting> waiting;
};

/// What a node sees while it runs.
struct ExecContext {
    const Graph* graph = nullptr;
    const Node* node = nullptr;         // the node that is running
    const NodeDef* definition = nullptr;

    World* world = nullptr;             // may be null in a test of one node: then no shared values, sounds or scenes
    Scene* scene = nullptr;
    Input* input = nullptr;             // may be null: then no key is down
    Entity other;                       // for collision and trigger events: who it was
    const Message* message = nullptr;   // for On Message: what was heard
    Physics* physics = nullptr;
    Entity self;                        // the entity the script is on
    float dt = 0;                       // seconds since the last frame
    double time = 0;                    // seconds since the game started
    ScriptState* state = nullptr;
    int depth = 0;                      // of values asking values: stops a loop of wires
    std::string graphName;              // the script's file, for the Debugger

    /// The value of one of the running node's inputs: from the wire, or what was typed in.
    Value input_(const char* pin);
    float number(const char* pin) { return input_(pin).asFloat(); }
    bool flag(const char* pin) { return input_(pin).asBool(); }
    std::string string(const char* pin) { return input_(pin).asString(); }
    Vec3 vector(const char* pin);
    /// Says what went wrong in the running node, at one of its pins ("" for the node itself): the
    /// editor shows it on the node, and the console says it once.
    void fail(const std::string& pin, const std::string& message);
};

/// What went wrong as scripts ran, node by node: the Visual Script window marks the node (and
/// the pin) in red with the message, until the script runs without it or is changed.
struct ScriptProblem {
    std::string graph;                  // the script's file
    int node = 0;
    std::string pin;                    // empty: the node as a whole
    std::string message;
    double time = 0;                    // the game time it last happened
};

struct ScriptProblems {
    static ScriptProblems& instance();
    std::vector<ScriptProblem> list;
    /// Notes one (once: the same again only moves its time). True if it is new.
    bool report(const ScriptProblem& problem);
    void clear(const std::string& graph);
    void clear() { list.clear(); }
    std::vector<ScriptProblem> of(const std::string& graph) const;
};

/// Watching scripts as they run, for the editor's Visual Script panel: the last value that went
/// along each colored wire, when each node last ran, and breakpoints. At a breakpoint the flow
/// stops before the node, the game is held (wantsHold), and the node runs when it goes on.
struct Debugger {
    static Debugger& instance();
    bool isOn = false;                          // while the editor shows a script and the game runs
    std::string graph;                          // the script watched
    Entity entity;                              // which entity running it; none: the first to run it
    std::map<std::pair<int, std::string>, std::string> values;     // (node, output) -> its last value, as text
    std::map<int, double> ran;                  // node -> when it last ran, in game time
    std::set<std::pair<std::string, int>> breakpoints;             // (script, node)
    bool stepsOnce = false;                     // stop at the next node that runs, wherever it is
    struct Stop {
        std::string graph;
        int node = 0;                           // 0: not stopped
        Entity entity;
    } stopped;
    bool wantsHold = false;                     // set at a stop: the editor holds the game
    double now = 0;                             // the game time of the frame being run

    bool watches(const std::string& name, Entity self);
    void toggleBreakpoint(const std::string& name, int node);
    bool hasBreakpoint(const std::string& name, int node) const { return breakpoints.count({name, node}) > 0; }
    /// Forgets what was seen: another script, or another play.
    void clearSeen();
};

/// Adds the nodes that come with the engine to the NodeLibrary. Safe to call again.
void registerStandardNodes();

/// One frame of one script on one entity: fires the events that happened.
/// `context` supplies the scene, the input, the entity, dt and the state.
void runFrame(const Graph& graph, ExecContext context);


/// The script with this name (a path relative to Assets), read from `filePath` the first
/// time and again whenever the file changes. nullptr if it cannot be read.
const Graph* loadGraph(const std::string& name, const std::string& filePath);

}  // namespace molten::script

namespace molten {

/// Component: runs a visual script on its entity while the game plays.
struct VisualScript {
    std::string graph;                  // the script: a .graph file, as a path relative to Assets

    void update(World& world, Entity self, float dt);
    void onCollision(World& world, Entity self, Entity other);
    void onTriggerEnter(World& world, Entity self, Entity other);
    void onMessage(World& world, Entity self, const Message& message);

    script::ScriptState state;          // not saved: every play starts fresh
    std::string loadedGraph;
};

}  // namespace molten
