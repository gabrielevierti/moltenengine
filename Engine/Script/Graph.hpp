// Graph.hpp
// A visual script: boxes ("nodes") joined by wires. This file is the script as data;
// Script/Runtime runs it and the editor's graph panel draws it.
//
// There are two kinds of wire. White ones carry the flow: "when this is done, do that".
// Colored ones carry a value: a number, a yes or no, a point in space, a text.
//
//     [On Update] ──▶ [Rotate]            every frame, turn the entity
//                      degrees ◀── [Number 90]
//
// A script is a text file (.graph) in the project's Assets:
//
//     molten-graph 1
//     variable "speed" number "90" "How fast it turns, degrees a second"
//     node 1 "OnUpdate" 80 80
//     node 2 "Rotate" 320 80
//       value "degrees" "90"
//       comment "Turns the wheel"
//     link 1 "then" 2 "in"
//
// The variables are the script's own (a C++ component's fields): declared once, with a type, a
// first value and what they are for, and read and set by the Get and Set Variable boxes. A
// node's comment is shown when the pointer is on it, and written above its line in C++.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace molten::script {

enum class PinType : uint8_t { Flow, Float, Bool, Vec3, String };

/// What travels along a colored wire. A value of one type can be read as another:
/// a number as yes/no (not zero), a number as a point (the same in x, y and z), and so on.
struct Value {
    PinType type = PinType::Float;
    float x = 0, y = 0, z = 0;
    std::string text;

    static Value number(float value);
    static Value boolean(bool value);
    static Value vector(float x, float y, float z);
    static Value string(std::string value);

    float asFloat() const;
    bool asBool() const;
    /// The three numbers of a point; a single number is repeated.
    void asVector(float& outX, float& outY, float& outZ) const;
    std::string asString() const;

    /// As written in a .graph file, and back. `type` says how to read the text.
    std::string toText() const;
    static Value fromText(PinType type, const std::string& text);
};

struct ExecContext;

struct PinDef {
    std::string name;                   // as in files and code: "degrees"
    PinType type = PinType::Float;
    Value defaultValue;                 // of an input nothing is wired to
    std::string pick;                   // a text input: what it holds, for the board to offer (see Field::pick); empty: free words
};

/// A kind of node: what it is called, its pins, and what it does.
struct NodeDef {
    std::string type;                   // as in files: "Rotate"
    std::string category;               // where the "add node" menu puts it: "Transform"
    std::string title;                  // on the node: "Rotate"
    std::string help;
    std::vector<PinDef> inputs, outputs;
    bool isEvent = false;               // starts a flow by itself: On Start, On Update

    /// For a node with a flow input: does its work and returns the name of the flow
    /// output to go on from ("then"; "true" or "false" for a branch), or nullptr to stop.
    std::function<const char*(ExecContext&)> execute;
    /// For a node with value outputs: the value of output number `output`.
    std::function<Value(ExecContext&, int output)> evaluate;

    const PinDef* findInput(const std::string& name) const;
    const PinDef* findOutput(const std::string& name) const;
    int outputIndex(const std::string& name) const;
};

/// Every kind of node there is. Script/Runtime adds the standard ones; a game or a
/// plugin may add its own.
class NodeLibrary {
public:
    static NodeLibrary& instance();
    void add(NodeDef definition);
    const NodeDef* find(const std::string& type) const;
    const std::vector<NodeDef>& all() const { return definitions_; }

private:
    std::vector<NodeDef> definitions_;
};

struct Node {
    int id = 0;
    std::string type;
    float x = 0, y = 0;                 // where it is drawn
    /// Values typed into inputs nothing is wired to, as text, by pin name.
    std::map<std::string, std::string> values;
    /// What it is for, in words: shown when the pointer is on it; a // comment above it in C++.
    std::string comment;
};

/// A variable of the script: declared with a type and a first value. A C++ component's field.
struct Variable {
    std::string name;
    PinType type = PinType::Float;
    std::string value;                  // its first value, as text
    std::string comment;
    bool isShown = true;                // shown in the Inspector, to set for each entity (a C++ field the component declares)
};

/// The words a file uses for a type: "number", "yes-no", "vector", "text".
const char* pinTypeWord(PinType type);
PinType pinTypeOfWord(const std::string& word);

struct Link {
    int fromNode = 0;
    std::string fromPin;                // an output
    int toNode = 0;
    std::string toPin;                  // an input
};

struct Graph {
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Variable> variables;
    /// Members of a C++ component no box says (a function of its own, a table, a pointer), kept as
    /// they are written and written back the same: the graph runs only as C++ while it has any.
    std::vector<std::string> code;
    Variable* variable(const std::string& name);
    const Variable* variable(const std::string& name) const;
    int nextId = 1;
    /// The kinds of node it is made of: null for a visual script's (NodeLibrary::instance()); a
    /// shader graph has its own (Rendering/ShaderGraph.hpp). Not saved: whoever reads the file knows.
    const NodeLibrary* library = nullptr;
    const NodeLibrary& kinds() const { return library ? *library : NodeLibrary::instance(); }

    Node* find(int id);
    const Node* find(int id) const;
    Node& addNode(const std::string& type, float x, float y);
    /// Removes a node and every wire on it.
    void removeNode(int id);

    /// Wires an output to an input. Refused (false) if the pins do not exist, if one is
    /// flow and the other is not, or if it would wire a node to itself. An input holds
    /// one value wire and a flow output leads to one place: an older wire there is replaced.
    bool connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin);
    void disconnectInput(int node, const std::string& pin);
    void disconnectOutput(int node, const std::string& pin);
    /// The wire that ends at an input, or the one that starts at a flow output.
    const Link* linkTo(int node, const std::string& inputPin) const;
    const Link* linkFrom(int node, const std::string& outputPin) const;

    std::string toText() const;
    static std::optional<Graph> fromText(const std::string& text);
    /// What a new script starts with: On Start and On Update, ready to be wired.
    static Graph starter();
};

}  // namespace molten::script
