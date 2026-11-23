// CppBoxes.hpp
// Any C++ as boxes and wires, and back again without losing a character.
//
// Script/CppToGraph.hpp turns the C++ it understands into the boxes that mean the same
// (Rotate, Set Position, Key Is Down...). Most C++ says things those boxes have no shape
// for. This is the other half: every C++ file can be laid out as a graph whose boxes hold
// the code itself, one statement to a box, in the shape the code has:
//
//     void Motor::fixedUpdate(World& world, Entity self, float dt)      a C++ Function box
//       ├─▶ [ Scene& scene = world.scene; ]                             a C++ box
//       ├─▶ [ if (!body) ] ── true ─▶ [ return; ]                       a C++ If box
//       └─▶ [ scene.each<Wheel>([&](Entity e, Wheel& wheel) ]           a C++ Block box:
//                 └─ body ─▶ [ driven++; ] ─▶ [ spin += wheel.spin; ]   a loop, a lambda, braces
//
// What is outside the functions (the #includes, a table of numbers, a struct's fields) is in
// C++ File boxes, in the order of the file. Every character of the file is in exactly one
// box, the spaces and the comments between statements too, so writing the graph back out
// (cppFromBoxes) gives the file it was made from, byte for byte; and a graph that was
// changed (a statement moved, a condition edited, a box added) gives the file that says so.
//
// The boxes cannot be run as they are: C++ is run by compiling it. A graph of them is a
// way to see and rearrange a script, and Convert to C++ Script writes it back.
#pragma once

#include "Script/Graph.hpp"

#include <string>

namespace molten::script {

/// The node kinds: CppFile, CppFunction, CppCode, CppIf, CppBlock. (registerStandardNodes calls it.)
void registerCppBoxes();

/// A C++ file as a graph of those boxes. Never fails: a file whose shape it cannot follow
/// (braces that do not match, a preprocessor that cuts a statement in two) is one File box.
Graph boxesFromCpp(const std::string& code);

/// The C++ a graph of those boxes says. Boxes of other kinds among them (a Rotate wired
/// into a function) are written as the call that runs them.
std::string cppFromBoxes(const Graph& graph);

/// Whether a graph is of this kind: it has a C++ File or a C++ Function box.
bool hasCppBoxes(const Graph& graph);
bool isCppBox(const std::string& nodeType);

/// What a box shows: its code as it is read (without the indentation it has in the file),
/// and the comment written above it, if any.
std::string cppBoxText(const Node& node);
std::string cppBoxComment(const Node& node);
/// How large a box is drawn, in the board's units, for its code to fit (these, and the Code box a
/// script made from C++ has where no box says a line). False for other kinds of node.
bool cppBoxSize(const Node& node, float& width, float& height);

}  // namespace molten::script
