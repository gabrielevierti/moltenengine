#include "Script/Convert.hpp"

#include <cstring>

#include "Core/World.hpp"
#include "Scene/Components.hpp"
#include "Script/CppBoxes.hpp"

#include <cstdio>
#include <set>

namespace molten::script {

namespace {

Value as(PinType type, const Value& value) {
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

ExecContext contextFor(Call& call, const Graph& graph, const Node& node, const NodeDef* definition) {
    ExecContext context;
    context.graph = &graph;
    context.node = &node;
    context.definition = definition;
    context.world = &call.world;
    context.scene = &call.world.scene;
    context.input = &call.world.input;
    context.physics = &call.world.physics;
    context.self = call.self;
    context.other = call.other;
    context.dt = call.dt;
    context.time = call.world.time.elapsed;
    context.state = &call.state;
    return context;
}

/// A graph of one node, with what was passed typed into its inputs.
const NodeDef* single(Graph& graph, const char* type, Call::Inputs inputs) {
    registerStandardNodes();
    const NodeDef* definition = NodeLibrary::instance().find(type);
    if (!definition) return nullptr;
    Node& node = graph.addNode(type, 0, 0);
    for (const auto& input : inputs) {
        const PinDef* pin = definition->findInput(input.first);
        node.values[input.first] = as(pin ? pin->type : input.second.type, input.second).toText();
    }
    return definition;
}

}  // namespace

Call::Call(World& world_, Entity self_, float dt_, ScriptState& state_, Entity other_) : world(world_), self(self_), other(other_), dt(dt_), state(state_) {}

std::string Call::operator()(const char* type, Inputs inputs) {
    Graph graph;
    const NodeDef* definition = single(graph, type, inputs);
    if (!definition || !definition->execute) return "then";
    ExecContext context = contextFor(*this, graph, graph.nodes.front(), definition);
    const char* next = definition->execute(context);
    return next ? next : "";
}

Value Call::value(const char* type, Inputs inputs, const char* output) {
    Graph graph;
    const NodeDef* definition = single(graph, type, inputs);
    if (!definition || !definition->evaluate) return Value{};
    ExecContext context = contextFor(*this, graph, graph.nodes.front(), definition);
    int index = output ? definition->outputIndex(output) : 0;
    return definition->evaluate(context, index < 0 ? 0 : index);
}

Value Call::otherName() const {
    const Name* name = world.scene.get<Name>(other);
    return Value::string(name ? name->value : std::string());
}

std::string typeNameFor(const std::string& fileStem) {
    std::string out;
    bool startsWord = true;
    for (char c : fileStem) {
        bool isLetter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'), isDigit = c >= '0' && c <= '9';
        if (!isLetter && !isDigit) {
            startsWord = true;
            continue;
        }
        if (out.empty() && isDigit) out += 'S';
        out += startsWord && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
        startsWord = false;
    }
    return out.empty() ? "Script" : out;
}

namespace {

std::string quotedText(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

std::string numberText(float value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%g", double(value));
    std::string text = buffer;
    if (text.find_first_of(".einf") == std::string::npos) return text;
    return text + "f";
}

std::string literal(PinType type, const std::string& text) {
    Value value = Value::fromText(type, text);
    switch (type) {
        case PinType::Bool: return std::string("script::Value::boolean(") + (value.asBool() ? "true" : "false") + ")";
        case PinType::String: return "script::Value::string(" + quotedText(value.asString()) + ")";
        case PinType::Vec3: return "script::Value::vector(" + numberText(value.x) + ", " + numberText(value.y) + ", " + numberText(value.z) + ")";
        default: return "script::Value::number(" + numberText(value.asFloat()) + ")";
    }
}

struct Writer {
    const Graph& graph;
    std::string out;
    std::set<int> timers;

    const NodeDef* definitionOf(const Node& node) const { return NodeLibrary::instance().find(node.type); }

    /// The value on an output of a node, as an expression.
    std::string expression(const Node& node, const std::string& output, int depth) {
        const NodeDef* definition = definitionOf(node);
        if (!definition || depth > 24) return "script::Value{}";
        if (definition->isEvent) {
            if (output == "dt") return "script::Value::number(dt)";
            if (output == "other") return "call.otherName()";
            return "script::Value{}";
        }
        return "call.value(" + quotedText(node.type) + ", " + inputs(node, *definition, depth + 1) + ", " + quotedText(output) + ")";
    }

    /// What goes into a node: what is wired to each input, or what was typed there.
    std::string inputs(const Node& node, const NodeDef& definition, int depth) {
        std::string list;
        for (const PinDef& pin : definition.inputs) {
            if (pin.type == PinType::Flow) continue;
            std::string value;
            if (const Link* link = graph.linkTo(node.id, pin.name)) {
                if (const Node* source = graph.find(link->fromNode)) value = expression(*source, link->fromPin, depth);
            } else if (auto typed = node.values.find(pin.name); typed != node.values.end()) {
                value = literal(pin.type, typed->second);
            }
            if (value.empty()) continue;            // left as the node's own default
            list += std::string(list.empty() ? "" : ", ") + "{" + quotedText(pin.name) + ", " + value + "}";
        }
        return "{" + list + "}";
    }

    void line(int indent, const std::string& text) { out += std::string(size_t(indent) * 4, ' ') + text + "\n"; }

    /// The steps that follow a flow output, one after another.
    void flow(const Node& from, const std::string& output, int indent, int depth) {
        const Link* link = graph.linkFrom(from.id, output);
        const Node* node = link ? graph.find(link->toNode) : nullptr;
        if (!node || depth > 64) return;
        const NodeDef* definition = definitionOf(*node);
        if (!definition) {
            line(indent, "// A node of a kind this program does not know: " + node->type);
            return;
        }
        std::vector<std::string> exits;
        for (const PinDef& pin : definition->outputs) {
            if (pin.type == PinType::Flow) exits.push_back(pin.name);
        }
        std::string call = "call(" + quotedText(node->type) + ", " + inputs(*node, *definition, 0) + ")";
        if (node->type == "Branch") {
            std::string condition = "script::Value::boolean(false)";
            if (const Link* wire = graph.linkTo(node->id, "condition")) {
                if (const Node* source = graph.find(wire->fromNode)) condition = expression(*source, wire->fromPin, 0);
            } else if (auto typed = node->values.find("condition"); typed != node->values.end()) {
                condition = literal(PinType::Bool, typed->second);
            }
            line(indent, "if (" + condition + ".asBool()) {");
            flow(*node, "true", indent + 1, depth + 1);
            if (graph.linkFrom(node->id, "false")) {
                line(indent, "} else {");
                flow(*node, "false", indent + 1, depth + 1);
            }
            line(indent, "}");
        } else if (exits.size() <= 1) {
            line(indent, call + ";");
            if (!exits.empty()) flow(*node, exits.front(), indent, depth + 1);
        } else {
            // Leaves by one of several ways: go on along the one it took.
            std::string taken = "taken" + std::to_string(node->id);
            line(indent, "std::string " + taken + " = " + call + ";");
            for (const std::string& exit : exits) {
                if (!graph.linkFrom(node->id, exit)) continue;
                line(indent, "if (" + taken + " == " + quotedText(exit) + ") {");
                flow(*node, exit, indent + 1, depth + 1);
                line(indent, "}");
            }
        }
    }
};

}  // namespace

std::string boxAsCppStatement(const Graph& graph, const Node& node) {
    registerStandardNodes();
    const NodeDef* definition = NodeLibrary::instance().find(node.type);
    if (!definition) return "// A box of a kind this program does not know: " + node.type;
    Writer writer{graph, {}, {}};
    return "{ static script::ScriptState boxes; script::Call call(world, self, world.time.delta, boxes); call(" + quotedText(node.type) + ", " +
           writer.inputs(node, *definition, 0) + "); }    // the box " + definition->title;
}

std::string graphToCpp(const Graph& graph, const std::string& name, const std::string& source) {
    registerStandardNodes();
    // A graph of C++ boxes is the file itself, laid out: written back as it says (Script/CppBoxes.hpp).
    if (hasCppBoxes(graph)) return cppFromBoxes(graph);
    // Otherwise, the code a programmer would write: what each box means, line by line.
    return graphToNaturalCpp(graph, name, source);
}

// ---------------------------------------------------------------- there and back

namespace {
const char* const kKeptBegin = "/* molten-graph ";
const char* const kKeptEnd = "*/";

/// A comment ends at the first "*/": a text in the graph that holds one is written apart.
std::string escaped(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        out += text[i];
        if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') out += ' ';
    }
    return out;
}

std::string unescaped(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == ' ' && i > 0 && text[i - 1] == '*' && i + 1 < text.size() && text[i + 1] == '/') continue;
        out += text[i];
    }
    return out;
}
}  // namespace

std::string graphToCppKeepingGraph(const Graph& graph, const std::string& name, const std::string& source) {
    // C++ boxes need no graph kept beside the code: the code is the graph, and goes back to it as it is.
    if (hasCppBoxes(graph)) return cppFromBoxes(graph);
    return graphToCpp(graph, name, source) + "\n" + kKeptBegin + name + "|" + source + "\n" +
           "The visual script this was made from, so that it can be turned back into one (Explorer: Convert to Visual Script).\n" +
           escaped(graph.toText()) + kKeptEnd + "\n";
}

std::optional<Graph> graphKeptIn(const std::string& code, bool* wasChanged, std::string* name) {
    size_t begin = code.rfind(kKeptBegin);
    if (begin == std::string::npos) return std::nullopt;
    size_t headerEnd = code.find('\n', begin);
    size_t end = code.find(kKeptEnd, headerEnd == std::string::npos ? begin : headerEnd);
    if (headerEnd == std::string::npos || end == std::string::npos) return std::nullopt;
    std::string header = code.substr(begin + std::strlen(kKeptBegin), headerEnd - begin - std::strlen(kKeptBegin));
    size_t bar = header.find('|');
    std::string typeName = header.substr(0, bar), source = bar == std::string::npos ? std::string() : header.substr(bar + 1);
    // The graph starts after the line that says what it is.
    size_t graphStart = code.find("molten-graph", headerEnd);
    if (graphStart == std::string::npos || graphStart > end) return std::nullopt;
    std::optional<Graph> graph = Graph::fromText(unescaped(code.substr(graphStart, end - graphStart)));
    if (!graph) return std::nullopt;
    if (wasChanged) *wasChanged = code.substr(0, begin) != graphToCpp(*graph, typeName, source) + "\n";
    if (name) *name = typeName;
    return graph;
}

}  // namespace molten::script
