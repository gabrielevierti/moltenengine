#include "Script/Convert.hpp"

#include "Core/World.hpp"
#include "Scene/Components.hpp"

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

std::string graphToCpp(const Graph& graph, const std::string& name, const std::string& source) {
    registerStandardNodes();
    Writer writer{graph, {}, {}};
    // Which events the graph has, by the function of a component each belongs in.
    std::string start, update, collision, trigger, members;
    for (const Node& node : graph.nodes) {
        const NodeDef* definition = NodeLibrary::instance().find(node.type);
        if (!definition || !definition->isEvent || !graph.linkFrom(node.id, "then")) continue;
        auto typed = [&](const char* pin, PinType type, const std::string& fallback) {
            auto found = node.values.find(pin);
            return Value::fromText(type, found != node.values.end() ? found->second : fallback);
        };
        writer.out.clear();
        if (node.type == "OnStart") {
            writer.flow(node, "then", 2, 0);
            start += writer.out;
        } else if (node.type == "OnUpdate") {
            writer.flow(node, "then", 2, 0);
            update += writer.out;
        } else if (node.type == "OnKeyPressed" || node.type == "OnKeyReleased") {
            std::string key = typed("key", PinType::String, "Space").asString();
            writer.line(2, std::string("if (world.input.") + (node.type == "OnKeyPressed" ? "wasPressed" : "wasReleased") + "(keyByName(" + quotedText(key) + "))) {");
            writer.flow(node, "then", 3, 0);
            writer.line(2, "}");
            update += writer.out;
        } else if (node.type == "OnPointerPressed") {
            writer.line(2, "if (world.input.wasPressed(MouseButton::Left)) {");
            writer.flow(node, "then", 3, 0);
            writer.line(2, "}");
            update += writer.out;
        } else if (node.type == "OnTimer") {
            std::string timer = "timer" + std::to_string(node.id);
            float seconds = std::max(typed("seconds", PinType::Float, "1").asFloat(), 0.001f);
            members += "    float " + timer + " = 0;                   // seconds since it last rang\n";
            writer.line(2, timer + " += dt;");
            writer.line(2, "if (" + timer + " >= " + numberText(seconds) + ") {");
            writer.line(3, timer + " -= " + numberText(seconds) + ";");
            writer.flow(node, "then", 3, 0);
            writer.line(2, "}");
            update += writer.out;
        } else if (node.type == "OnCollision") {
            writer.flow(node, "then", 2, 0);
            collision += writer.out;
        } else if (node.type == "OnTriggerEnter") {
            writer.flow(node, "then", 2, 0);
            trigger += writer.out;
        } else {
            update += "        // An event this conversion has no place for: " + node.type + "\n";
        }
    }

    std::string out;
    out += "// " + name + ".cpp\n";
    out += "// Made from the visual script " + source + ": the same events and the same steps, as code.\n";
    out += "// Each call(\"...\") does what the box of that name does. From here it is yours to change:\n";
    out += "// replace a call with code of your own whenever the game needs more than the box gives.\n";
    out += "#include \"MoltenMetal.hpp\"\n#include \"Script/Convert.hpp\"\n\nusing namespace molten;\n\n";
    out += "struct " + name + " {\n";
    out += "    script::ScriptState state;          // the script's own variables\n" + members;
    auto function = [&](const std::string& signature, const std::string& open, const std::string& body) {
        if (body.empty()) return;
        out += "\n    " + signature + " {\n        " + open + "\n" + body + "    }\n";
    };
    function("void start(World& world, Entity self)", "script::Call call(world, self, 0, state);", start);
    function("void update(World& world, Entity self, float dt)", "script::Call call(world, self, dt, state);", update);
    function("void onCollision(World& world, Entity self, Entity other)", "script::Call call(world, self, 0, state, other);", collision);
    function("void onTriggerEnter(World& world, Entity self, Entity other)", "script::Call call(world, self, 0, state, other);", trigger);
    out += "};\n\nMOLTEN_COMPONENT(" + name + ")\n";
    return out;
}

}  // namespace molten::script
