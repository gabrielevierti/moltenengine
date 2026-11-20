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
#include <unordered_map>
#include <vector>

namespace molten::script {

/// What one entity's copy of a script remembers between frames.
struct ScriptState {
    std::unordered_map<std::string, Value> variables;
    bool hasStarted = false;
    std::vector<Entity> collidedWith, triggeredBy;      // since the script last ran
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
    Physics* physics = nullptr;
    Entity self;                        // the entity the script is on
    float dt = 0;                       // seconds since the last frame
    double time = 0;                    // seconds since the game started
    ScriptState* state = nullptr;
    int depth = 0;                      // of values asking values: stops a loop of wires

    /// The value of one of the running node's inputs: from the wire, or what was typed in.
    Value input_(const char* pin);
    float number(const char* pin) { return input_(pin).asFloat(); }
    bool flag(const char* pin) { return input_(pin).asBool(); }
    std::string string(const char* pin) { return input_(pin).asString(); }
    Vec3 vector(const char* pin);
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

    script::ScriptState state;          // not saved: every play starts fresh
    std::string loadedGraph;
};

}  // namespace molten
