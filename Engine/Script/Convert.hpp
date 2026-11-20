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
// The other way, from C++ to a graph, cannot be done in general: code can say things boxes
// and wires have no shape for.
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

/// A name that can be a C++ type: letters and digits, starting with a capital letter.
std::string typeNameFor(const std::string& fileStem);

}  // namespace molten::script
