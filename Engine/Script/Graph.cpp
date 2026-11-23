#include "Script/Graph.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace molten::script {

namespace {

std::string floatText(float value) {
    char buffer[32];
    for (int digits = 6; digits <= 9; digits++) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", digits, double(value));
        if (std::strtof(buffer, nullptr) == value) break;
    }
    return buffer;
}

}  // namespace

// ---------------------------------------------------------------- values

Value Value::number(float value) {
    Value v;
    v.type = PinType::Float;
    v.x = value;
    return v;
}

Value Value::boolean(bool value) {
    Value v;
    v.type = PinType::Bool;
    v.x = value ? 1.0f : 0.0f;
    return v;
}

Value Value::vector(float x, float y, float z) {
    Value v;
    v.type = PinType::Vec3;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

Value Value::string(std::string value) {
    Value v;
    v.type = PinType::String;
    v.text = std::move(value);
    return v;
}

float Value::asFloat() const {
    if (type == PinType::String) return std::strtof(text.c_str(), nullptr);
    return x;
}

bool Value::asBool() const {
    if (type == PinType::String) return !text.empty() && text != "0" && text != "false";
    if (type == PinType::Vec3) return x != 0 || y != 0 || z != 0;
    return x != 0;
}

void Value::asVector(float& outX, float& outY, float& outZ) const {
    if (type == PinType::Vec3) {
        outX = x;
        outY = y;
        outZ = z;
    } else {
        outX = outY = outZ = asFloat();
    }
}

std::string Value::asString() const {
    switch (type) {
        case PinType::String: return text;
        case PinType::Bool: return x != 0 ? "yes" : "no";
        case PinType::Vec3: return floatText(x) + ", " + floatText(y) + ", " + floatText(z);
        default: return floatText(x);
    }
}

std::string Value::toText() const {
    switch (type) {
        case PinType::String: return text;
        case PinType::Bool: return x != 0 ? "1" : "0";
        case PinType::Vec3: return floatText(x) + " " + floatText(y) + " " + floatText(z);
        default: return floatText(x);
    }
}

Value Value::fromText(PinType type, const std::string& text) {
    switch (type) {
        case PinType::String: return string(text);
        case PinType::Bool: return boolean(!text.empty() && (text[0] == '1' || text[0] == 't' || text[0] == 'y'));
        case PinType::Vec3: {
            float values[3] = {0, 0, 0};
            const char* cursor = text.c_str();
            for (float& value : values) {
                char* end = nullptr;
                float parsed = std::strtof(cursor, &end);
                if (end == cursor) break;
                value = parsed;
                cursor = end;
            }
            return vector(values[0], values[1], values[2]);
        }
        default: return number(std::strtof(text.c_str(), nullptr));
    }
}

// ---------------------------------------------------------------- kinds of node

const PinDef* NodeDef::findInput(const std::string& name) const {
    for (const PinDef& pin : inputs) {
        if (pin.name == name) return &pin;
    }
    return nullptr;
}

const PinDef* NodeDef::findOutput(const std::string& name) const {
    for (const PinDef& pin : outputs) {
        if (pin.name == name) return &pin;
    }
    return nullptr;
}

int NodeDef::outputIndex(const std::string& name) const {
    for (size_t i = 0; i < outputs.size(); i++) {
        if (outputs[i].name == name) return int(i);
    }
    return -1;
}

NodeLibrary& NodeLibrary::instance() {
    static NodeLibrary library;
    return library;
}

void NodeLibrary::add(NodeDef definition) {
    for (NodeDef& existing : definitions_) {
        if (existing.type == definition.type) {
            existing = std::move(definition);
            return;
        }
    }
    definitions_.push_back(std::move(definition));
}

const NodeDef* NodeLibrary::find(const std::string& type) const {
    for (const NodeDef& definition : definitions_) {
        if (definition.type == type) return &definition;
    }
    return nullptr;
}

// ---------------------------------------------------------------- the graph

const char* pinTypeWord(PinType type) {
    switch (type) {
        case PinType::Float: return "number";
        case PinType::Bool: return "yes-no";
        case PinType::Vec3: return "vector";
        case PinType::String: return "text";
        case PinType::Flow: return "flow";
    }
    return "number";
}

PinType pinTypeOfWord(const std::string& word) {
    if (word == "yes-no" || word == "bool") return PinType::Bool;
    if (word == "vector" || word == "Vec3") return PinType::Vec3;
    if (word == "text" || word == "string") return PinType::String;
    return PinType::Float;
}

Variable* Graph::variable(const std::string& name) {
    for (Variable& v : variables)
        if (v.name == name) return &v;
    return nullptr;
}

const Variable* Graph::variable(const std::string& name) const {
    for (const Variable& v : variables)
        if (v.name == name) return &v;
    return nullptr;
}

Node* Graph::find(int id) {
    for (Node& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

const Node* Graph::find(int id) const {
    for (const Node& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

Node& Graph::addNode(const std::string& type, float x, float y) {
    Node node;
    node.id = nextId++;
    node.type = type;
    node.x = x;
    node.y = y;
    nodes.push_back(std::move(node));
    return nodes.back();
}

void Graph::removeNode(int id) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [id](const Node& node) { return node.id == id; }), nodes.end());
    links.erase(std::remove_if(links.begin(), links.end(), [id](const Link& link) { return link.fromNode == id || link.toNode == id; }),
                links.end());
}

bool Graph::connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    const Node* from = find(fromNode);
    const Node* to = find(toNode);
    if (!from || !to || fromNode == toNode) return false;
    const NodeDef* fromDef = kinds().find(from->type);
    const NodeDef* toDef = kinds().find(to->type);
    const PinDef* output = fromDef ? fromDef->findOutput(fromPin) : nullptr;
    const PinDef* input = toDef ? toDef->findInput(toPin) : nullptr;
    if (!output || !input) return false;
    bool isFlow = output->type == PinType::Flow;
    if (isFlow != (input->type == PinType::Flow)) return false;
    if (isFlow) disconnectOutput(fromNode, fromPin);        // a flow goes one way
    else disconnectInput(toNode, toPin);                    // an input has one value
    links.push_back(Link{fromNode, fromPin, toNode, toPin});
    return true;
}

void Graph::disconnectInput(int node, const std::string& pin) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& link) { return link.toNode == node && link.toPin == pin; }),
                links.end());
}

void Graph::disconnectOutput(int node, const std::string& pin) {
    links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& link) { return link.fromNode == node && link.fromPin == pin; }),
                links.end());
}

const Link* Graph::linkTo(int node, const std::string& inputPin) const {
    for (const Link& link : links) {
        if (link.toNode == node && link.toPin == inputPin) return &link;
    }
    return nullptr;
}

const Link* Graph::linkFrom(int node, const std::string& outputPin) const {
    for (const Link& link : links) {
        if (link.fromNode == node && link.fromPin == outputPin) return &link;
    }
    return nullptr;
}

std::string Graph::toText() const {
    std::string out = "molten-graph 1\n";
    for (const Variable& v : variables) {
        out += "variable " + text::quote(v.name) + " " + pinTypeWord(v.type) + " " + text::quote(v.value) + " " + text::quote(v.comment);
        out += v.isShown ? "\n" : " hidden\n";
    }
    for (const std::string& member : code) out += "code " + text::quote(member) + "\n";
    for (const Node& node : nodes) {
        out += "node " + std::to_string(node.id) + " " + text::quote(node.type) + " " + floatText(node.x) + " " + floatText(node.y) + "\n";
        for (const auto& [pin, value] : node.values) out += "  value " + text::quote(pin) + " " + text::quote(value) + "\n";
        if (!node.comment.empty()) out += "  comment " + text::quote(node.comment) + "\n";
    }
    for (const Link& link : links) {
        out += "link " + std::to_string(link.fromNode) + " " + text::quote(link.fromPin) + " " + std::to_string(link.toNode) + " " +
               text::quote(link.toPin) + "\n";
    }
    return out;
}

std::optional<Graph> Graph::fromText(const std::string& contents) {
    std::vector<std::vector<std::string>> lines = text::parseLines(contents);
    if (lines.empty() || lines[0][0] != "molten-graph") return std::nullopt;
    if (!text::checkHeader(text::firstLine(contents), "molten-graph").empty()) return std::nullopt;
    Graph graph;
    Node* current = nullptr;
    for (size_t i = 1; i < lines.size(); i++) {
        const std::vector<std::string>& words = lines[i];
        if (words[0] == "node" && words.size() >= 5) {
            Node node;
            node.id = std::atoi(words[1].c_str());
            node.type = words[2];
            node.x = std::strtof(words[3].c_str(), nullptr);
            node.y = std::strtof(words[4].c_str(), nullptr);
            graph.nextId = std::max(graph.nextId, node.id + 1);
            graph.nodes.push_back(std::move(node));
            current = &graph.nodes.back();
        } else if (words[0] == "value" && words.size() >= 3 && current) {
            current->values[words[1]] = words[2];
        } else if (words[0] == "code" && words.size() >= 2) {
            graph.code.push_back(words[1]);
        } else if (words[0] == "comment" && words.size() >= 2 && current) {
            current->comment = words[1];
        } else if (words[0] == "variable" && words.size() >= 3) {
            Variable v;
            v.name = words[1];
            v.type = pinTypeOfWord(words[2]);
            if (words.size() > 3) v.value = words[3];
            if (words.size() > 4) v.comment = words[4];
            v.isShown = !(words.size() > 5 && words[5] == "hidden");
            if (!graph.variable(v.name)) graph.variables.push_back(std::move(v));
        } else if (words[0] == "link" && words.size() >= 5) {
            // Kept as written, even if a node's kind is unknown here: a script made with
            // nodes from a plugin must survive being opened without it.
            graph.links.push_back(Link{std::atoi(words[1].c_str()), words[2], std::atoi(words[3].c_str()), words[4]});
        }
    }
    return graph;
}

Graph Graph::starter() {
    Graph graph;
    graph.addNode("OnStart", 80, 80);
    graph.addNode("OnUpdate", 80, 220);
    return graph;
}

}  // namespace molten::script
