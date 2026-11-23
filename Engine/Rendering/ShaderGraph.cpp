#include "Rendering/ShaderGraph.hpp"

#include <filesystem>
#include <fstream>

#include "Math/Math.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace molten {

using script::Graph;
using script::Link;
using script::Node;
using script::NodeDef;
using script::NodeLibrary;
using script::PinDef;
using script::PinType;
using script::Value;

namespace {

PinDef number(const char* name, float value = 0.0f) {
    return PinDef{name, PinType::Float, Value::number(value), ""};
}
PinDef vector(const char* name, Vec3 value = Vec3{0, 0, 0}) {
    return PinDef{name, PinType::Vec3, Value::vector(value.x, value.y, value.z), ""};
}
PinDef words(const char* name, const char* value) {
    return PinDef{name, PinType::String, Value::string(value), ""};
}

NodeDef kind(const char* type, const char* category, const char* title, const char* help, std::vector<PinDef> inputs, std::vector<PinDef> outputs) {
    NodeDef node;
    node.type = type;
    node.category = category;
    node.title = title;
    node.help = help;
    node.inputs = std::move(inputs);
    node.outputs = std::move(outputs);
    return node;
}

NodeLibrary makeShaderNodes() {
    NodeLibrary library;
    // ---- what the surface is: where, which way, when
    library.add(kind("SG.UV", "Input", "UV", "Where on the mesh's pictures the pixel is (x and y; z is 0)", {}, {vector("uv")}));
    library.add(kind("SG.Position", "Input", "Position", "Where the pixel is in the world, in meters", {}, {vector("position")}));
    library.add(kind("SG.Normal", "Input", "Normal", "Which way the mesh faces there (its own, before the material's normal map)", {}, {vector("normal")}));
    library.add(kind("SG.View", "Input", "View Direction", "From the pixel toward the eye", {}, {vector("view")}));
    library.add(kind("SG.Time", "Input", "Time", "Seconds since the game started: what makes a shader move", {}, {number("time")}));
    library.add(kind("SG.Tint", "Input", "Entity Tint", "The tint the entity's Mesh Renderer gives it", {}, {vector("color"), number("alpha")}));
    library.add(kind("SG.Wind", "Input", "Wind", "The scene's wind, with its weather, in meters a second", {}, {vector("wind")}));
    library.add(kind("SG.Material", "Input", "Material", "What the material's own settings and pictures made of the surface, before the graph",
                     {}, {vector("color"), number("alpha"), number("metallic"), number("roughness"), vector("emissive"), number("occlusion"), vector("normal")}));
    // ---- numbers and colors, and the properties each material sets for itself
    library.add(kind("SG.Number", "Value", "Number", "A number typed in", {number("value", 1)}, {number("value")}));
    library.add(kind("SG.Color", "Value", "Color", "A color typed in (red, green, blue)", {vector("color", Vec3{1, 1, 1})}, {vector("color")}));
    library.add(kind("SG.Vector", "Value", "Vector", "Three numbers typed in", {vector("vector")}, {vector("vector")}));
    library.add(kind("SG.FloatProperty", "Property", "Number Property", "A number each material using the shader sets for itself, by this name",
                     {words("name", "amount"), number("default", 1), number("min", 0), number("max", 1)}, {number("value")}));
    library.add(kind("SG.ColorProperty", "Property", "Color Property", "A color each material using the shader sets for itself, by this name",
                     {words("name", "color"), vector("default", Vec3{1, 1, 1})}, {vector("color")}));
    // ---- arithmetic: on three numbers at once (a number is three of itself)
    for (const char* type : {"Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max"}) {
        library.add(kind((std::string("SG.") + type).c_str(), "Math", type, "", {vector("a"), vector("b", Vec3{1, 1, 1})}, {vector("result")}));
    }
    library.add(kind("SG.Mix", "Math", "Mix", "From a (t at 0) to b (t at 1)", {vector("a"), vector("b", Vec3{1, 1, 1}), number("t", 0.5f)}, {vector("result")}));
    for (const char* type : {"One Minus", "Absolute", "Fraction", "Sine", "Cosine", "Saturate", "Normalize", "Negate"}) {
        std::string id = std::string("SG.") + type;
        id.erase(std::remove(id.begin(), id.end(), ' '), id.end());
        library.add(kind(id.c_str(), "Math", type, "", {vector("in")}, {vector("result")}));
    }
    library.add(kind("SG.Step", "Math", "Step", "0 below the edge, 1 from it on", {vector("edge", Vec3{0.5f, 0.5f, 0.5f}), vector("in")}, {vector("result")}));
    library.add(kind("SG.Smoothstep", "Math", "Smoothstep", "0 below the first edge, 1 above the second, smoothly between",
                     {vector("from"), vector("to", Vec3{1, 1, 1}), vector("in")}, {vector("result")}));
    library.add(kind("SG.Remap", "Math", "Remap", "From one range to another (clamped)",
                     {vector("in"), number("from min", 0), number("from max", 1), number("to min", 0), number("to max", 1)}, {vector("result")}));
    library.add(kind("SG.Dot", "Math", "Dot", "", {vector("a"), vector("b")}, {number("result")}));
    library.add(kind("SG.Length", "Math", "Length", "", {vector("in")}, {number("result")}));
    library.add(kind("SG.Distance", "Math", "Distance", "", {vector("a"), vector("b")}, {number("result")}));
    library.add(kind("SG.Split", "Math", "Split", "Its three numbers apart", {vector("in")}, {number("x"), number("y"), number("z")}));
    library.add(kind("SG.Combine", "Math", "Combine", "Three numbers as one vector or color", {number("x"), number("y"), number("z")}, {vector("result")}));
    // ---- patterns
    library.add(kind("SG.Noise", "Pattern", "Noise", "Smooth noise between 0 and 1 (with nothing wired to uv: the mesh's)",
                     {vector("uv"), number("scale", 4)}, {number("value")}));
    library.add(kind("SG.ScrollUV", "Pattern", "Scroll UV", "UV moving with time: lava, water, a conveyor (with nothing wired to uv: the mesh's)",
                     {vector("uv"), vector("speed", Vec3{0.1f, 0, 0})}, {vector("uv")}));
    library.add(kind("SG.Checker", "Pattern", "Checker", "Squares of 0 and 1 (with nothing wired to uv: the mesh's)", {vector("uv"), number("scale", 8)}, {number("value")}));
    library.add(kind("SG.Fresnel", "Pattern", "Fresnel", "1 where the surface is seen edge on, 0 where it faces the eye: rims, glass, force fields",
                     {number("power", 5)}, {number("value")}));
    // ---- what the surface becomes
    library.add(kind("SG.Surface", "Output", "Surface",
                     "What the surface is: what is wired or typed in here replaces what the material made; the rest stays",
                     {vector("color", Vec3{1, 1, 1}), number("alpha", 1), number("metallic", 0), number("roughness", 0.5f), vector("emissive"),
                      number("occlusion", 1), vector("normal", Vec3{0, 1, 0})},
                     {}));
    return library;
}

std::string literal(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", double(value));
    std::string out = text;
    if (out.find_first_of(".en") == std::string::npos) out += ".0";
    return out;
}

std::string literal(const Value& value, PinType type) {
    if (type == PinType::Vec3) {
        float x, y, z;
        value.asVector(x, y, z);
        return "float3(" + literal(x) + ", " + literal(y) + ", " + literal(z) + ")";
    }
    return literal(value.asFloat());
}

/// A property's name as the code can use it: letters, digits and _, not starting with a digit.
std::string identifier(const std::string& text) {
    std::string out;
    for (char c : text) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "p_" + out;
    return out;
}

/// Writes the surface function, node by node, from the Surface node back along its wires.
class Writer {
public:
    Writer(const Graph& graph, const NodeLibrary& library) : graph_(graph), library_(library) {}

    std::string body, error;
    std::vector<std::string> properties;

    /// The value at an input, as `want`: its wire's, else what is typed there, else `unwired`
    /// (or the pin's own default).
    std::string input(const Node& node, const NodeDef& definition, const std::string& pin, PinType want, const char* unwired = nullptr) {
        if (const Link* link = graph_.linkTo(node.id, pin)) return output(link->fromNode, link->fromPin, want);
        const PinDef* defined = definition.findInput(pin);
        auto typed = node.values.find(pin);
        if (typed != node.values.end() && defined) return convert(literal(Value::fromText(defined->type, typed->second), defined->type), defined->type, want);
        if (unwired) return unwired;
        return defined ? convert(literal(defined->defaultValue, defined->type), defined->type, want) : literal(0.0f);
    }
    bool isSet(const Node& node, const std::string& pin) const { return graph_.linkTo(node.id, pin) || node.values.count(pin); }
    std::string typedText(const Node& node, const NodeDef& definition, const std::string& pin) const {
        auto typed = node.values.find(pin);
        if (typed != node.values.end()) return typed->second;
        const PinDef* defined = definition.findInput(pin);
        return defined ? defined->defaultValue.toText() : std::string();
    }

    /// The variable holding an output of a node, written first if it is not yet.
    std::string output(int nodeId, const std::string& pin, PinType want) {
        const Node* node = graph_.find(nodeId);
        const NodeDef* definition = node ? library_.find(node->type) : nullptr;
        const PinDef* out = definition ? definition->findOutput(pin) : nullptr;
        if (!out) {
            error = "a wire comes from something that is not a shader graph's node";
            return want == PinType::Vec3 ? "float3(0.0)" : "0.0";
        }
        if (!written_.count(nodeId)) {
            if (visiting_.count(nodeId)) {
                error = "wires go round in a loop through \"" + definition->title + "\"";
                return want == PinType::Vec3 ? "float3(0.0)" : "0.0";
            }
            visiting_.insert(nodeId);
            write(*node, *definition);
            visiting_.erase(nodeId);
            written_.insert(nodeId);
        }
        return convert(variable(nodeId, pin), out->type, want);
    }

private:
    static std::string variable(int node, const std::string& pin) {
        std::string name = "n" + std::to_string(node) + "_";
        for (char c : pin) name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        return name;
    }
    static std::string convert(const std::string& expression, PinType have, PinType want) {
        if (have == want) return expression;
        if (want == PinType::Vec3) return "float3(" + expression + ")";
        return "(" + expression + ").x";
    }
    void assign(const Node& node, const NodeDef& definition, const std::string& pin, const std::string& expression) {
        const PinDef* out = definition.findOutput(pin);
        body += std::string("    ") + (out && out->type == PinType::Vec3 ? "float3 " : "float ") + variable(node.id, pin) + " = " + expression + ";\n";
    }

    void write(const Node& node, const NodeDef& d) {
        const std::string type = node.type.substr(3);          // without "SG."
        auto v = [&](const char* pin, const char* unwired = nullptr) { return input(node, d, pin, PinType::Vec3, unwired); };
        auto f = [&](const char* pin) { return input(node, d, pin, PinType::Float); };
        const char* meshUV = "float3(in.uv, 0.0)";
        if (type == "UV") assign(node, d, "uv", meshUV);
        else if (type == "Position") assign(node, d, "position", "in.position");
        else if (type == "Normal") assign(node, d, "normal", "in.normal");
        else if (type == "View") assign(node, d, "view", "in.view");
        else if (type == "Time") assign(node, d, "time", "in.time");
        else if (type == "Tint") {
            assign(node, d, "color", "in.tint.rgb");
            assign(node, d, "alpha", "in.tint.a");
        } else if (type == "Wind") assign(node, d, "wind", "in.wind");
        else if (type == "Material") {
            for (const char* field : {"color", "alpha", "metallic", "roughness", "emissive", "occlusion", "normal"}) assign(node, d, field, std::string("s.") + field);
        } else if (type == "Number") assign(node, d, "value", f("value"));
        else if (type == "Color") assign(node, d, "color", v("color"));
        else if (type == "Vector") assign(node, d, "vector", v("vector"));
        else if (type == "FloatProperty" || type == "ColorProperty") {
            const std::string name = identifier(typedText(node, d, "name"));
            if (!declared_.count(name)) {
                declared_.insert(name);
                if (type == "FloatProperty") {
                    const float low = Value::fromText(PinType::Float, typedText(node, d, "min")).asFloat();
                    const float high = Value::fromText(PinType::Float, typedText(node, d, "max")).asFloat();
                    properties.push_back("float " + name + " = " + literal(Value::fromText(PinType::Float, typedText(node, d, "default")).asFloat()) +
                                         (high > low ? " range " + literal(low) + " " + literal(high) : std::string()));
                } else {
                    float x, y, z;
                    Value::fromText(PinType::Vec3, typedText(node, d, "default")).asVector(x, y, z);
                    properties.push_back("color " + name + " = " + literal(x) + " " + literal(y) + " " + literal(z) + " 1");
                }
            }
            if (type == "FloatProperty") assign(node, d, "value", "props." + name);
            else assign(node, d, "color", "props." + name + ".rgb");
        } else if (type == "Add") assign(node, d, "result", "(" + v("a") + " + " + v("b") + ")");
        else if (type == "Subtract") assign(node, d, "result", "(" + v("a") + " - " + v("b") + ")");
        else if (type == "Multiply") assign(node, d, "result", "(" + v("a") + " * " + v("b") + ")");
        else if (type == "Divide") assign(node, d, "result", "(" + v("a") + " / " + v("b") + ")");
        else if (type == "Power") assign(node, d, "result", "pow(max(" + v("a") + ", float3(0.0)), " + v("b") + ")");
        else if (type == "Min") assign(node, d, "result", "min(" + v("a") + ", " + v("b") + ")");
        else if (type == "Max") assign(node, d, "result", "max(" + v("a") + ", " + v("b") + ")");
        else if (type == "Mix") assign(node, d, "result", "mix(" + v("a") + ", " + v("b") + ", " + f("t") + ")");
        else if (type == "OneMinus") assign(node, d, "result", "(float3(1.0) - " + v("in") + ")");
        else if (type == "Absolute") assign(node, d, "result", "abs(" + v("in") + ")");
        else if (type == "Fraction") assign(node, d, "result", "fract(" + v("in") + ")");
        else if (type == "Sine") assign(node, d, "result", "sin(" + v("in") + ")");
        else if (type == "Cosine") assign(node, d, "result", "cos(" + v("in") + ")");
        else if (type == "Saturate") assign(node, d, "result", "saturate(" + v("in") + ")");
        else if (type == "Normalize") assign(node, d, "result", "normalize(" + v("in") + ")");
        else if (type == "Negate") assign(node, d, "result", "(-" + v("in") + ")");
        else if (type == "Step") assign(node, d, "result", "step(" + v("edge") + ", " + v("in") + ")");
        else if (type == "Smoothstep") assign(node, d, "result", "smoothstep(" + v("from") + ", " + v("to") + ", " + v("in") + ")");
        else if (type == "Remap") {
            assign(node, d, "result", "mix(float3(" + f("to min") + "), float3(" + f("to max") + "), saturate((" + v("in") + " - float3(" + f("from min") +
                                          ")) / max(" + f("from max") + " - " + f("from min") + ", 1e-6)))");
        } else if (type == "Dot") assign(node, d, "result", "dot(" + v("a") + ", " + v("b") + ")");
        else if (type == "Length") assign(node, d, "result", "length(" + v("in") + ")");
        else if (type == "Distance") assign(node, d, "result", "distance(" + v("a") + ", " + v("b") + ")");
        else if (type == "Split") {
            const std::string in = v("in");
            assign(node, d, "x", "(" + in + ").x");
            assign(node, d, "y", "(" + in + ").y");
            assign(node, d, "z", "(" + in + ").z");
        } else if (type == "Combine") assign(node, d, "result", "float3(" + f("x") + ", " + f("y") + ", " + f("z") + ")");
        else if (type == "Noise") assign(node, d, "value", "noise((" + v("uv", meshUV) + ").xy * " + f("scale") + ")");
        else if (type == "ScrollUV") assign(node, d, "uv", "(" + v("uv", meshUV) + " + " + v("speed") + " * in.time)");
        else if (type == "Checker") {
            const std::string uv = v("uv", meshUV), scale = f("scale");
            assign(node, d, "value", "fract((floor((" + uv + ").x * " + scale + ") + floor((" + uv + ").y * " + scale + ")) * 0.5) * 2.0");
        } else if (type == "Fresnel") {
            assign(node, d, "value", "pow(1.0 - saturate(dot(normalize(s.normal), normalize(in.view))), " + f("power") + ")");
        } else {
            error = "\"" + d.title + "\" is not a node a shader graph knows";
        }
    }

    const Graph& graph_;
    const NodeLibrary& library_;
    std::set<int> written_, visiting_;
    std::set<std::string> declared_;
};

}  // namespace

const NodeLibrary& shaderNodes() {
    static const NodeLibrary library = makeShaderNodes();
    return library;
}

std::string shaderTextFromGraph(const Graph& graph, const std::string& name, std::string* error) {
    const NodeLibrary& library = shaderNodes();
    const Node* surface = nullptr;
    for (const Node& node : graph.nodes) {
        if (node.type == "SG.Surface") surface = &node;
    }
    if (!surface) {
        if (error) *error = "a shader graph needs a Surface node: what the surface becomes";
        return {};
    }
    const NodeDef& definition = *library.find("SG.Surface");
    Writer writer(graph, library);
    std::string assignments;
    for (const PinDef& pin : definition.inputs) {
        if (!writer.isSet(*surface, pin.name)) continue;
        const std::string value = writer.input(*surface, definition, pin.name, pin.type);
        assignments += "    s." + pin.name + " = " + value + ";\n";
    }
    if (!writer.error.empty()) {
        if (error) *error = writer.error;
        return {};
    }
    std::string text = "// " + name + ": made from its shader graph by the engine. Change the graph, not this.\n";
    if (!writer.properties.empty()) {
        text += "properties {\n";
        for (const std::string& line : writer.properties) text += "    " + line + "\n";
        text += "}\n";
    }
    text += "\nSurface surface(Surface s, Input in, Props props) {\n" + writer.body + assignments + "    return s;\n}\n";
    return text;
}

std::string newShaderGraphText() {
    Graph graph;
    graph.library = &shaderNodes();
    // By number: adding a node may move the others in memory.
    const int surface = graph.addNode("SG.Surface", 520, 120).id;
    const int color = graph.addNode("SG.ColorProperty", 160, 80).id;
    const int rough = graph.addNode("SG.Number", 200, 260).id;
    graph.find(color)->values["name"] = "color";
    graph.find(color)->values["default"] = "1 0.45 0.2";
    graph.find(rough)->values["value"] = "0.4";
    graph.connect(color, "color", surface, "color");
    graph.connect(rough, "value", surface, "roughness");
    return graph.toText();
}

bool isShaderGraphText(const std::string& text) {
    return text.compare(0, 12, "molten-graph") == 0;
}

bool isShaderGraphFile(const std::string& absolutePath) {
    std::filesystem::path path(absolutePath);
    std::string extension = path.extension().string();
    if (extension == ".shadergraph") return true;
    if (extension != ".shader") return false;
    std::ifstream file(absolutePath, std::ios::binary);
    char start[12] = {};
    file.read(start, sizeof start);
    return file.gcount() == 12 && std::string(start, 12) == "molten-graph";
}

}  // namespace molten
