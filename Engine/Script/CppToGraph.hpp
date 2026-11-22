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
// A line it has no box for is left out and named in `notConverted`, with its line number:
// nothing is dropped without saying so. Code made from a graph and not changed since goes
// back to that very graph, as it was laid out.
#pragma once

#include "Script/Graph.hpp"

#include <optional>
#include <string>
#include <vector>

namespace molten::script {

struct CppToGraphResult {
    std::optional<Graph> graph;
    std::string name;                           // the component's name: "PlayerController"
    std::vector<std::string> notConverted;      // "line 14: transform->rotation = lookRotation(target)"
    bool isTheKeptGraph = false;                // the graph the code was made from, unchanged
    std::string error;                          // why there is no graph
};

CppToGraphResult graphFromCpp(const std::string& code);

}  // namespace molten::script
