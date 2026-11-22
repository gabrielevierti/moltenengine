// Convert.hpp
// A visual script written out as C++: the same events, the same steps in the same order,
// as a component a programmer can go on from. Each box of the graph becomes one call of
// the node of that name, so the code does exactly what the graph did.
//
//     [On Update] ──▶ [Rotate degrees 180]        void update(World& world, Entity self, float dt) {
//                                                     script::Call call(world, self, dt, state);
//                                                     call("Rotate", {{"degrees", script::Value::number(180)}});
//                                                 }
//
// The other way, from any C++ to a graph, cannot be done: code can say things boxes and
// wires have no shape for. Code that was made from a graph keeps the graph in a comment at
// its end, so that it can go back: the graph it was made from.
#pragma once

#include "Script/Runtime.hpp"

#include <initializer_list>
#include <string>
#include <utility>

namespace molten::script {

/// Runs single nodes from code: what the C++ made from a graph is written with.
struct Call {
    Call(World& world, Entity self, float dt, ScriptState& state, Entity other = Entity{});

    using Inputs = std::initializer_list<std::pair<const char*, Value>>;
    /// Does what an action node does. Returns the name of the flow output it leaves by
    /// ("then"; "true" or "false" for a branch), or an empty text if it stops there.
    std::string operator()(const char* type, Inputs inputs = {});
    /// The value of a node's output; the first, if none is named.
    Value value(const char* type, Inputs inputs = {}, const char* output = nullptr);
    /// The name of the other entity of a collision or a trigger.
    Value otherName() const;

    World& world;
    Entity self, other;
    float dt;
    ScriptState& state;
};

/// The C++ of a graph, as a component called `name`. `source` is where the graph came
/// from, for the comment at the top.
std::string graphToCpp(const Graph& graph, const std::string& name, const std::string& source);

/// The same, with the graph kept in a comment at the end of the file: what can go back to boxes.
std::string graphToCppKeepingGraph(const Graph& graph, const std::string& name, const std::string& source);

/// The graph kept in C++ made from one, if there is one. `wasChanged` is set to true if the
/// code was edited after it was made: the graph is then the one it was made from, not what
/// the code does now.
std::optional<Graph> graphKeptIn(const std::string& code, bool* wasChanged = nullptr, std::string* name = nullptr);

/// One box as a line of C++ that does what it does, for a box wired in among C++ boxes
/// (Script/CppBoxes.hpp): it expects `world` and `self` where it is written, as a component's functions have them.
std::string boxAsCppStatement(const Graph& graph, const Node& node);

/// A name that can be a C++ type: letters and digits, starting with a capital letter.
std::string typeNameFor(const std::string& fileStem);

}  // namespace molten::script
