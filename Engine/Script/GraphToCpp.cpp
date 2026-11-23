// GraphToCpp.cpp
// A visual script written out as the C++ a programmer would write: the script's variables are
// the component's fields (with their comments, and those shown in the Inspector named in
// MOLTEN_COMPONENT), each box the line of code it means (transform->position += ..., if, while,
// for, world.playSound(...)), each box's comment a // comment above its line. What has no line of
// its own in C++ (a box a game or a plugin added, one that finds an entity by its name) is written
// as the call that runs that very box: call("Type", {...}). Script/CppToGraph reads all of it
// back to the same boxes, so a script goes from boxes to C++ and back without turning into
// something else.
#include "Script/Convert.hpp"
#include "Script/CppBoxes.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace molten::script {

namespace {

std::string cppText(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') out += "\\n";
        else out += c;
    }
    return out + "\"";
}

std::string number(float value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%g", double(value));
    std::string text = buffer;
    if (text.find_first_of(".einf") == std::string::npos) return text;
    return text + "f";
}

/// A C++ name for a variable: letters, digits and _, not starting with a digit.
std::string identifier(const std::string& name) {
    std::string out;
    for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "v" + out;
    return out;
}

/// A key as code says it: Key::W, or keyByName("0") for the ones whose name is not a C++ name.
std::string keyCode(const std::string& name) {
    bool plain = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
    for (char c : name) plain = plain && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    return plain ? "Key::" + name : "keyByName(" + cppText(name) + ")";
}

const char* cppType(PinType type) {
    switch (type) {
        case PinType::Bool: return "bool";
        case PinType::Vec3: return "Vec3";
        case PinType::String: return "std::string";
        default: return "float";
    }
}

struct Code {
    std::string text;
    PinType type = PinType::Float;
};

std::string literalOf(PinType type, const Value& value) {
    switch (type) {
        case PinType::Bool: return value.asBool() ? "true" : "false";
        case PinType::String: return cppText(value.asString());
        case PinType::Vec3: {
            float x, y, z;
            value.asVector(x, y, z);
            return "Vec3{" + number(x) + ", " + number(y) + ", " + number(z) + "}";
        }
        default: return number(value.asFloat());
    }
}

/// A value as a script::Value, for the call of a box that has no line of its own.
std::string asScriptValue(const Code& code) {
    switch (code.type) {
        case PinType::Bool: return "script::Value::boolean(" + code.text + ")";
        case PinType::String: return "script::Value::string(" + code.text + ")";
        case PinType::Vec3: return "script::asValue(" + code.text + ")";
        default: return "script::Value::number(" + code.text + ")";
    }
}

std::string fromScriptValue(const std::string& value, PinType type) {
    switch (type) {
        case PinType::Bool: return value + ".asBool()";
        case PinType::String: return value + ".asString()";
        case PinType::Vec3: return "script::asVec3(" + value + ")";
        default: return value + ".asFloat()";
    }
}

class Writer {
public:
    explicit Writer(const Graph& graph) : graph_(graph) {}

    // What each function needs at its top, found as its body is written.
    bool usesCall = false, usesTransform = false, usesBody = false, isCollision = false;

    std::string body(const Node& event, int indent) {
        out_.clear();
        flow(event, "then", indent, 0);
        return out_;
    }
    void line(int indent, const std::string& text) { out_ += std::string(size_t(indent) * 4, ' ') + text + "\n"; }
    std::string take() {
        std::string out = std::move(out_);
        out_.clear();
        return out;
    }

private:
    const NodeDef* definitionOf(const Node& node) const { return NodeLibrary::instance().find(node.type); }

    bool onSelf(const Node& node) const {
        auto on = node.values.find("on");
        return (on == node.values.end() || on->second == "self" || on->second.empty()) && !graph_.linkTo(node.id, "on");
    }

    /// What goes into an input: the code of what is wired to it, what was typed there, or the box's own default.
    Code input(const Node& node, const char* pin, int depth) {
        const NodeDef* definition = definitionOf(node);
        const PinDef* defined = definition ? definition->findInput(pin) : nullptr;
        PinType type = defined ? defined->type : PinType::Float;
        if (const Link* link = graph_.linkTo(node.id, pin)) {
            if (const Node* source = graph_.find(link->fromNode)) {
                Code code = expression(*source, link->fromPin, depth + 1);
                if (code.type != type && type == PinType::Float && code.type == PinType::Bool) code.text = "float(" + code.text + ")";
                return code;
            }
        }
        auto typed = node.values.find(pin);
        Value value = typed != node.values.end() ? Value::fromText(type, typed->second) : defined ? defined->defaultValue : Value{};
        return Code{literalOf(type, value), type};
    }

    /// What was typed into a text input, or the box's own default.
    std::string text(const Node& node, const char* pin) {
        auto typed = node.values.find(pin);
        if (typed != node.values.end()) return typed->second;
        const NodeDef* definition = definitionOf(node);
        const PinDef* defined = definition ? definition->findInput(pin) : nullptr;
        return defined ? defined->defaultValue.asString() : std::string();
    }

    /// The value on an output of a box, as an expression.
    Code expression(const Node& node, const std::string& output, int depth) {
        const NodeDef* definition = definitionOf(node);
        if (!definition || depth > 40) return Code{"0", PinType::Float};
        const PinDef* out = definition->findOutput(output);
        PinType type = out ? out->type : PinType::Float;
        auto in = [&](const char* pin) { return input(node, pin, depth).text; };
        auto both = [&](const char* sign) { return Code{"(" + in("a") + " " + sign + " " + in("b") + ")", type}; };
        const std::string& t = node.type;
        if (definition->isEvent) {
            if (output == "dt") return Code{"dt", PinType::Float};
            if (output == "other") {
                usesCall = true;
                return Code{"call.otherName().asString()", PinType::String};
            }
            usesCall = true;
            return Code{"0", type};
        }
        if (t == "Number") return Code{in("value"), PinType::Float};
        if (t == "Text") return Code{in("value"), PinType::String};
        if (t == "Vector") return Code{"Vec3{" + in("x") + ", " + in("y") + ", " + in("z") + "}", PinType::Vec3};
        if (t == "Add" || t == "AddVectors") return both("+");
        if (t == "Subtract" || t == "SubtractVectors") return both("-");
        if (t == "Multiply") return both("*");
        if (t == "Divide") return both("/");
        if (t == "ScaleVector") return Code{"(" + in("vector") + " * " + in("by") + ")", PinType::Vec3};
        if (t == "Greater") return both(">");
        if (t == "Less") return both("<");
        if (t == "GreaterOrEqual") return both(">=");
        if (t == "LessOrEqual") return both("<=");
        if (t == "Equal") return both("==");
        if (t == "NotEqual") return both("!=");
        if (t == "And") return both("&&");
        if (t == "Or") return both("||");
        if (t == "Not") return Code{"!" + in("value"), PinType::Bool};
        if (t == "Negate") return Code{"-" + in("value"), PinType::Float};
        if (t == "Select") return Code{"(" + in("condition") + " ? " + in("ifYes") + " : " + in("ifNo") + ")", PinType::Float};
        if (t == "Sin") return Code{"std::sin(radians(" + in("degrees") + "))", PinType::Float};
        if (t == "Cos") return Code{"std::cos(radians(" + in("degrees") + "))", PinType::Float};
        if (t == "Abs") return Code{"std::abs(" + in("value") + ")", PinType::Float};
        if (t == "Floor") return Code{"std::floor(" + in("value") + ")", PinType::Float};
        if (t == "Round") return Code{"std::round(" + in("value") + ")", PinType::Float};
        if (t == "Sqrt") return Code{"std::sqrt(" + in("value") + ")", PinType::Float};
        if (t == "Min") return Code{"std::min(" + in("a") + ", " + in("b") + ")", PinType::Float};
        if (t == "Max") return Code{"std::max(" + in("a") + ", " + in("b") + ")", PinType::Float};
        if (t == "Power") return Code{"std::pow(" + in("a") + ", " + in("b") + ")", PinType::Float};
        if (t == "Modulo") return Code{"std::fmod(" + in("a") + ", " + in("b") + ")", PinType::Float};
        if (t == "Clamp") return Code{"std::clamp(" + in("value") + ", " + in("least") + ", " + in("most") + ")", PinType::Float};
        if (t == "Lerp") return Code{"lerp(" + in("a") + ", " + in("b") + ", " + in("t") + ")", PinType::Float};
        if (t == "LerpVectors") return Code{"lerp(" + in("a") + ", " + in("b") + ", " + in("t") + ")", PinType::Vec3};
        if (t == "Distance") return Code{"simd::distance(" + in("a") + ", " + in("b") + ")", PinType::Float};
        if (t == "Length") return Code{"simd::length(" + in("vector") + ")", PinType::Float};
        if (t == "Normalize") return Code{"simd::normalize(" + in("vector") + ")", PinType::Vec3};
        if (t == "Dot") return Code{"simd::dot(" + in("a") + ", " + in("b") + ")", PinType::Float};
        if (t == "Cross") return Code{"simd::cross(" + in("a") + ", " + in("b") + ")", PinType::Vec3};
        if (t == "Direction") return Code{"simd::normalize(" + in("to") + " - " + in("from") + ")", PinType::Vec3};
        if (t == "Split") return Code{in("vector") + "." + output, PinType::Float};
        if (t == "GetVariable") {
            std::string name = text(node, "name");
            if (!graph_.linkTo(node.id, "name") && !name.empty()) {
                const Variable* declared = graph_.variable(name);
                return Code{identifier(name), declared ? declared->type : PinType::Float};
            }
        }
        if (t == "Time") {
            if (output == "dt") return Code{"dt", PinType::Float};
            return Code{"float(world.time.elapsed)", PinType::Float};
        }
        if (t == "GetValue" && !graph_.linkTo(node.id, "name")) return Code{"world.value(" + cppText(text(node, "name")) + ")", PinType::Float};
        if (t == "KeyIsDown" && !graph_.linkTo(node.id, "key")) return Code{"world.input.isDown(" + keyCode(text(node, "key")) + ")", PinType::Bool};
        if (t == "ActionIsDown") return Code{"world.input.isDown(" + in("action") + ")", PinType::Bool};
        if (t == "Axis") return Code{"world.input.axis(" + in("axis") + ")", PinType::Float};
        if (t == "GetPosition" && onSelf(node)) {
            usesTransform = true;
            return Code{"transform->position", PinType::Vec3};
        }
        if (t == "GetForward" && onSelf(node)) {
            usesTransform = true;
            return Code{"transform->forward()", PinType::Vec3};
        }
        if (t == "GetVelocity" && onSelf(node)) {
            usesBody = true;
            return Code{"body->velocity", PinType::Vec3};
        }
        if (t == "For" && output == "index") return Code{loopName(node.id), PinType::Float};
        // Any other: the box itself, asked for its value.
        usesCall = true;
        std::string value = "call.value(" + cppText(t) + ", " + inputs(node, *definition, depth) + ", " + cppText(output) + ")";
        return Code{fromScriptValue(value, type), type};
    }

    /// The inputs of a box, for call(...): what is wired, or typed in.
    std::string inputs(const Node& node, const NodeDef& definition, int depth) {
        std::string list;
        for (const PinDef& pin : definition.inputs) {
            if (pin.type == PinType::Flow) continue;
            if (!graph_.linkTo(node.id, pin.name) && !node.values.count(pin.name)) continue;
            Code code = input(node, pin.name.c_str(), depth);
            list += std::string(list.empty() ? "" : ", ") + "{" + cppText(pin.name) + ", " + asScriptValue(code) + "}";
        }
        return "{" + list + "}";
    }

    void comment(const Node& node, int indent) {
        if (node.comment.empty()) return;
        std::istringstream lines(node.comment);
        std::string each;
        while (std::getline(lines, each)) line(indent, "// " + each);
    }

    /// The boxes that follow a flow output, one after another.
    void flow(const Node& from, const std::string& output, int indent, int depth) {
        const Link* link = graph_.linkFrom(from.id, output);
        const Node* node = link ? graph_.find(link->toNode) : nullptr;
        if (!node || depth > 200) return;
        const NodeDef* definition = definitionOf(*node);
        if (!definition) {
            line(indent, "// A box of a kind this program does not know: " + node->type);
            return;
        }
        comment(*node, indent);
        auto in = [&](const char* pin) { return input(*node, pin, 0).text; };
        const std::string& t = node->type;
        auto next = [&](const char* exit) { flow(*node, exit, indent, depth + 1); };

        if (t == "Branch") {
            line(indent, "if (" + in("condition") + ") {");
            flow(*node, "true", indent + 1, depth + 1);
            if (graph_.linkFrom(node->id, "false")) {
                line(indent, "} else {");
                flow(*node, "false", indent + 1, depth + 1);
            }
            line(indent, "}");
            return;
        }
        if (t == "While") {
            line(indent, "while (" + in("condition") + ") {");
            flow(*node, "do", indent + 1, depth + 1);
            line(indent, "}");
            next("done");
            return;
        }
        if (t == "For") {
            std::string index = loopName(node->id);
            std::string step = in("step");
            bool down = !step.empty() && step[0] == '-';
            line(indent, "for (float " + index + " = " + in("from") + "; " + index + (down ? " > " : " < ") + in("to") + "; " + index + " += " + step + ") {");
            flow(*node, "do", indent + 1, depth + 1);
            line(indent, "}");
            next("done");
            return;
        }
        if (t == "Sequence") {
            for (const char* part : {"first", "second", "third"}) flow(*node, part, indent, depth + 1);
            return;
        }
        if (t == "Return") {
            line(indent, "return;");
            return;
        }
        if (t == "Code") {
            // As it was written: its first line indented to here, the others as they were.
            std::istringstream lines(text(*node, "code"));
            std::string each;
            bool first = true;
            while (std::getline(lines, each)) {
                if (first) line(indent, each);
                else out_ += each + "\n";
                first = false;
            }
            next("then");
            return;
        }
        if (t == "SetVariable" && !graph_.linkTo(node->id, "name") && !text(*node, "name").empty()) {
            // As the variable is declared: a yes-no set from a yes-no stays one.
            std::string value = in("value");
            const Variable* declared = graph_.variable(text(*node, "name"));
            if (declared && declared->type != PinType::Float && value.rfind("float(", 0) == 0 && value.back() == ')') value = value.substr(6, value.size() - 7);
            line(indent, identifier(text(*node, "name")) + " = " + value + ";");
            next("then");
            return;
        }
        if (t == "AddToVariable" && !graph_.linkTo(node->id, "name") && !text(*node, "name").empty()) {
            line(indent, identifier(text(*node, "name")) + " += " + in("amount") + ";");
            next("then");
            return;
        }
        auto perSecond = [&](const std::string& amount) {
            Code flag = input(*node, "perSecond", 0);
            return flag.text == "true" ? "(" + amount + ") * dt" : flag.text == "false" ? amount : amount + " * (" + flag.text + " ? dt : 1.0f)";
        };
        if (onSelf(*node)) {
            if (t == "Translate" && input(*node, "local", 0).text == "false") {
                usesTransform = true;
                line(indent, "transform->position += " + perSecond(in("offset")) + ";");
                next("then");
                return;
            }
            if (t == "Rotate") {
                usesTransform = true;
                line(indent, "transform->rotation = transform->rotation * axisAngle(" + in("axis") + ", radians(" + perSecond(in("degrees")) + "));");
                next("then");
                return;
            }
            if (t == "SetPosition" || t == "SetScale") {
                usesTransform = true;
                line(indent, std::string("transform->") + (t == "SetPosition" ? "position = " + in("position") : "scale = " + in("scale")) + ";");
                next("then");
                return;
            }
            if (t == "SetVelocity" || t == "AddVelocity") {
                usesBody = true;
                line(indent, std::string("body->velocity ") + (t == "SetVelocity" ? "= " : "+= ") + in("velocity") + ";");
                next("then");
                return;
            }
        }
        if (t == "PlaySound") {
            line(indent, "world.playSound(" + in("sound") + ", " + in("volume") + ");");
            next("then");
            return;
        }
        if (t == "LoadScene") {
            line(indent, "world.loadScene(" + in("scene") + ");");
            next("then");
            return;
        }
        if (t == "ReloadScene") {
            line(indent, "world.reloadScene();");
            next("then");
            return;
        }
        if (t == "SetValue") {
            line(indent, "world.setValue(" + in("name") + ", " + in("value") + ");");
            next("then");
            return;
        }
        if (t == "DestroySelf") {
            line(indent, "world.scene.destroy(self);");
            return;
        }
        if (t == "Log" && !graph_.linkTo(node->id, "message")) {
            std::string message = text(*node, "message");
            std::string safe;
            for (char c : message) safe += c == '%' ? std::string("%%") : std::string(1, c);
            line(indent, "logInfo(" + cppText(safe) + ");");
            next("then");
            return;
        }
        // Any other box: the call that runs it.
        usesCall = true;
        std::vector<std::string> exits;
        for (const PinDef& pin : definition->outputs) {
            if (pin.type == PinType::Flow) exits.push_back(pin.name);
        }
        std::string call = "call(" + cppText(t) + ", " + inputs(*node, *definition, 0) + ")";
        if (exits.size() <= 1) {
            line(indent, call + ";");
            if (!exits.empty()) next(exits.front().c_str());
            return;
        }
        std::string taken = "way" + (++ways_ > 1 ? std::to_string(ways_) : std::string());
        line(indent, "std::string " + taken + " = " + call + ";");
        for (const std::string& exit : exits) {
            if (!graph_.linkFrom(node->id, exit)) continue;
            line(indent, "if (" + taken + " == " + cppText(exit) + ") {");
            flow(*node, exit, indent + 1, depth + 1);
            line(indent, "}");
        }
    }

    /// A loop's counter: i, then j, k, then i4, i5... in the order the loops are written.
    std::string loopName(int node) {
        auto found = loops_.find(node);
        if (found != loops_.end()) return found->second;
        size_t n = loops_.size();
        std::string name = n < 3 ? std::string(1, char('i' + n)) : "i" + std::to_string(n + 1);
        loops_[node] = name;
        return name;
    }

    const Graph& graph_;
    std::string out_;
    std::map<int, std::string> loops_;
    int ways_ = 0;
};

}  // namespace

std::string graphToNaturalCpp(const Graph& given, const std::string& name, const std::string& source) {
    registerStandardNodes();
    // A variable a box names that was never declared (a script of before 1.3) is declared here: a number.
    Graph graph = given;
    for (const Node& node : given.nodes) {
        if (node.type != "SetVariable" && node.type != "GetVariable" && node.type != "AddToVariable") continue;
        if (given.linkTo(node.id, "name")) continue;
        auto named = node.values.find("name");
        std::string variable = named != node.values.end() ? named->second : "score";
        if (variable.empty() || graph.variable(variable)) continue;
        Variable declared;
        declared.name = variable;
        declared.value = "0";
        declared.isShown = false;
        graph.variables.push_back(declared);
    }
    std::string start, update, collision, trigger, members;
    bool callsIn[4] = {}, transformIn[4] = {}, bodyIn[4] = {};
    for (const Node& node : graph.nodes) {
        const NodeDef* definition = NodeLibrary::instance().find(node.type);
        if (!definition || !definition->isEvent || !graph.linkFrom(node.id, "then")) continue;
        auto typed = [&](const char* pin, const std::string& fallback) {
            auto found = node.values.find(pin);
            return found != node.values.end() ? found->second : fallback;
        };
        Writer writer(graph);
        int where = 1;
        std::string body;
        if (!node.comment.empty()) {
            std::istringstream lines(node.comment);
            std::string each;
            while (std::getline(lines, each)) body += "        // " + each + "\n";
        }
        if (node.type == "OnStart") {
            where = 0;
            body += writer.body(node, 2);
        } else if (node.type == "OnUpdate") {
            body += writer.body(node, 2);
        } else if (node.type == "OnKeyPressed" || node.type == "OnKeyReleased") {
            std::string inside = writer.body(node, 3);
            body += std::string("        if (world.input.") + (node.type == "OnKeyPressed" ? "wasPressed" : "wasReleased") + "(" + keyCode(typed("key", "Space")) +
                    ")) {\n" + inside + "        }\n";
        } else if (node.type == "OnActionPressed") {
            std::string inside = writer.body(node, 3);
            body += "        if (world.input.wasPressed(" + cppText(typed("action", "jump")) + ")) {\n" + inside + "        }\n";
        } else if (node.type == "OnPointerPressed") {
            std::string inside = writer.body(node, 3);
            body += "        if (world.input.wasPressed(MouseButton::Left)) {\n" + inside + "        }\n";
        } else if (node.type == "OnTimer") {
            std::string timer = "timer" + std::to_string(node.id);
            float seconds = std::max(Value::fromText(PinType::Float, typed("seconds", "1")).asFloat(), 0.001f);
            char text[32];
            std::snprintf(text, sizeof text, "%g", double(seconds));
            members += "    float " + timer + " = 0;                 // seconds since it last rang\n";
            std::string inside = writer.body(node, 3);
            body += "        " + timer + " += dt;\n        if (" + timer + " >= " + text + "f) {\n            " + timer + " -= " + text + "f;\n" + inside + "        }\n";
        } else if (node.type == "OnCollision") {
            where = 2;
            body += writer.body(node, 2);
        } else if (node.type == "OnTriggerEnter") {
            where = 3;
            body += writer.body(node, 2);
        } else {
            // An event that has no function of a component: run as the box it is, every frame.
            writer.usesCall = true;
            body += "        // " + definition->title + ": an event with no function of its own in C++.\n";
        }
        callsIn[where] |= writer.usesCall;
        transformIn[where] |= writer.usesTransform;
        bodyIn[where] |= writer.usesBody;
        (where == 0 ? start : where == 1 ? update : where == 2 ? collision : trigger) += body;
    }
    bool usesCall = callsIn[0] || callsIn[1] || callsIn[2] || callsIn[3];

    std::string out;
    out += "// " + name + ".cpp\n";
    out += "// Made from the visual script " + source + ". Each line is what a box of it does; a box with no line of\n";
    out += "// its own is run as that box. Convert it back to boxes whenever you like.\n";
    out += "#include \"MoltenMetal.hpp\"\n";
    if (usesCall) out += "#include \"Script/Convert.hpp\"\n";
    for (const std::string& kept : graph.code) {
        if (kept.rfind("#include", 0) == 0) out += kept + "\n";
    }
    out += "\n#include <algorithm>\n#include <cmath>\n#include <string>\n\nusing namespace molten;\n\n";
    out += "struct " + name + " {\n";
    for (const Variable& v : graph.variables) {
        std::string declaration = "    " + std::string(cppType(v.type)) + " " + identifier(v.name) + " = " + literalOf(v.type, Value::fromText(v.type, v.value)) + ";";
        if (!v.comment.empty()) {
            std::string first = v.comment.substr(0, v.comment.find('\n'));
            declaration += std::string(std::max<size_t>(1, 40 > declaration.size() ? 40 - declaration.size() : 1), ' ') + "// " + first;
        }
        out += declaration + "\n";
    }
    if (usesCall) out += "    script::ScriptState state;          // what the boxes called below remember\n";
    out += members;
    for (const std::string& kept : graph.code) {
        if (kept.rfind("#include", 0) == 0) continue;
        out += "\n    " + kept + "\n";
    }
    auto function = [&](int where, const std::string& signature, const std::string& callLine, const std::string& body) {
        if (body.empty()) return;
        out += "\n    " + signature + " {\n";
        if (callsIn[where]) out += "        " + callLine + "\n";
        if (transformIn[where]) out += "        Transform* transform = world.scene.get<Transform>(self);\n        if (!transform) return;\n";
        if (bodyIn[where]) out += "        RigidBody* body = world.scene.get<RigidBody>(self);\n        if (!body) return;\n";
        out += body + "    }\n";
    };
    function(0, "void start(World& world, Entity self)", "script::Call call(world, self, 0, state);", start);
    function(1, "void update(World& world, Entity self, float dt)", "script::Call call(world, self, dt, state);", update);
    function(2, "void onCollision(World& world, Entity self, Entity other)", "script::Call call(world, self, 0, state, other);", collision);
    function(3, "void onTriggerEnter(World& world, Entity self, Entity other)", "script::Call call(world, self, 0, state, other);", trigger);
    out += "};\n\nMOLTEN_COMPONENT(" + name;
    for (const Variable& v : graph.variables) {
        if (!v.isShown) continue;
        out += ",\n                 MOLTEN_FIELD(" + identifier(v.name) + ")";
        if (!v.comment.empty()) out += ".help(" + cppText(v.comment.substr(0, v.comment.find('\n'))) + ")";
    }
    out += ")\n";
    return out;
}

}  // namespace molten::script
