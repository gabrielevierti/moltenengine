// CppToGraph.hpp
// C++ turned into a visual script: the other way from Convert.hpp.
//
// A component's start, update, onCollision and onTriggerEnter become the events On Start,
// On Update, On Collision and On Trigger Enter; what they do becomes boxes and wires, in
// the same order. What it understands:
//
//   - the code a graph was turned into (call("Rotate", {...}), call.value(...)), changed or not;
//   - code written by hand in the style of a new script: the entity's Transform and Rigid
//     Body, its Animator, the keys and the mouse, numbers, points and yes-or-no, if and else,
//     the component's own number fields (they become the script's variables);
//   - a key pressed at the top of update (On Key Pressed), and a timer (On Timer).
//
// Code that says more than those boxes can (and most C++ does: a loop over the wheels, a
// table of gears, a function of its own) is not cut down to what they can say. It becomes a
// graph of C++ boxes instead (Script/CppBoxes.hpp): each statement in a box as it is written,
// wired in the shape the code has, and written back out it is the same file to the character.
// So every C++ file has a graph, and nothing in it is ever left out. Code made from a graph
// and not changed since goes back to that very graph, as it was laid out.
#pragma once

#include "Script/Graph.hpp"

#include <optional>
#include <string>
#include <vector>

namespace molten::script {

struct CppToGraphResult {
    std::optional<Graph> graph;
    std::string name;                           // the component's name: "PlayerController"
    std::vector<std::string> notConverted;      // what is in C++ boxes, or kept as C++ members: no box says it (yet)
    bool isTheKeptGraph = false;                // the graph the code was made from, unchanged
    bool isCodeAsBoxes = false;                 // the code itself, a statement to a box (Script/CppBoxes.hpp)
    std::string error;                          // why there is no graph (never, now)
};

CppToGraphResult graphFromCpp(const std::string& code);

}  // namespace molten::script
