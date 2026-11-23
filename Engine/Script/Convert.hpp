// Convert.hpp
// A visual script written out as C++, and what the C++ made from one runs with.
//
//     [On Update] ──▶ [Rotate degrees speed]       void update(World& world, Entity self, float dt) {
//                                                       Transform* transform = world.scene.get<Transform>(self);
//                                                       if (!transform) return;
//                                                       transform->rotation = transform->rotation * axisAngle(Vec3{0, 1, 0}, radians((speed) * dt));
//                                                   }
//
// Each box is the line of code it means, the script's variables are the component's fields, a
// box's comment is a comment above its line (Script/GraphToCpp.cpp). A box that has no line of its
// own (one a plugin added, one that acts on another entity) is called as it is: call("Type",
// {...}), which runs that very box. Script/CppToGraph reads all of it back to the same boxes.
// Code made from a graph also keeps the graph in a comment at its end: unchanged, it goes back
// to that very graph, as it was laid out.
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

/// A point as a script::Value, and back: for a box called from code (call("...")).
inline Value asValue(Vec3 v) { return Value::vector(v.x, v.y, v.z); }
inline Vec3 asVec3(const Value& value) {
    float x, y, z;
    value.asVector(x, y, z);
    return Vec3{x, y, z};
}

/// The C++ of a graph, as a component called `name`: the script's variables its fields, each box
/// the line of code it means (Script/GraphToCpp.cpp). `source` is where the graph came from, for
/// the comment at the top.
std::string graphToCpp(const Graph& graph, const std::string& name, const std::string& source);
/// The same, written so: what graphToCpp gives for a graph of boxes.
std::string graphToNaturalCpp(const Graph& graph, const std::string& name, const std::string& source);

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
