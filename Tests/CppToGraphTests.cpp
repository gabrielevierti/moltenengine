// C++ turned into boxes and wires: code made from a graph (changed or not), and code written by hand.
#include "Test.hpp"
#include "Core/TextFile.hpp"
#include <cstdlib>

#include "Script/Convert.hpp"
#include "Script/CppBoxes.hpp"
#include "Script/CppToGraph.hpp"
#include "Script/Runtime.hpp"

#include <algorithm>
#include <set>

using namespace molten;
using namespace molten::script;

namespace {

int countOf(const Graph& graph, const std::string& type) {
    return int(std::count_if(graph.nodes.begin(), graph.nodes.end(), [&](const Node& node) { return node.type == type; }));
}

const Node* first(const Graph& graph, const std::string& type) {
    for (const Node& node : graph.nodes) {
        if (node.type == type) return &node;
    }
    return nullptr;
}

const char* const kHandWritten = R"(
#include "MoltenMetal.hpp"

using namespace molten;

struct PlayerController {
    float speed = 3.0f;

    void start(Engine& engine, Entity self) {
        logInfo("ready");
    }

    void update(Engine& engine, Entity self, float dt) {
        if (Transform* transform = engine.scene.get<Transform>(self)) {
            float direction = 0;
            if (engine.input.isDown(Key::W)) direction += 1;
            if (engine.input.isDown(Key::S)) direction -= 1;
            transform->position += transform->forward() * (direction * speed * dt);
            if (engine.input.wasPressed(Key::Space)) {
                transform->position.y += 2;
            }
        }
    }
};

MOLTEN_COMPONENT(PlayerController, MOLTEN_FIELD(speed))
)";

}  // namespace

TEST(cpp_written_by_hand_becomes_a_graph) {
    registerStandardNodes();
    CppToGraphResult result = graphFromCpp(kHandWritten);
    CHECK(result.graph.has_value());
    if (!result.graph) return;
    const Graph& graph = *result.graph;
    CHECK(result.name == "PlayerController");
    CHECK(result.notConverted.empty());
    CHECK(countOf(graph, "OnStart") == 1);
    CHECK(countOf(graph, "OnUpdate") == 1);
    // Space, at the top of update, is an event of its own.
    const Node* space = first(graph, "OnKeyPressed");
    CHECK(space && space->values.count("key") && space->values.at("key") == "Space");
    CHECK(countOf(graph, "Branch") == 2);
    CHECK(countOf(graph, "KeyIsDown") == 2);
    CHECK(countOf(graph, "Translate") == 2);
    CHECK(countOf(graph, "GetForward") == 1);
    CHECK(countOf(graph, "Log") == 1);
    // The field is a variable of the script, declared with its first value, shown in the Inspector.
    const Variable* speed = graph.variable("speed");
    CHECK(speed && speed->type == PinType::Float && speed->value == "3" && speed->isShown);
    // The local is one too, not shown.
    const Variable* direction = graph.variable("direction");
    CHECK(direction && !direction->isShown);
    // After the two ifs, the move follows both ways out of the second one.
    const Node* branch = nullptr;
    for (const Node& node : graph.nodes) {
        if (node.type == "Branch") branch = &node;
    }
    CHECK(branch && graph.linkFrom(branch->id, "false"));
}

TEST(cpp_made_from_a_graph_and_changed_goes_back) {
    registerStandardNodes();
    Graph graph;
    Node& start = graph.addNode("OnUpdate", 0, 0);
    int startId = start.id;
    Node& rotate = graph.addNode("Rotate", 200, 0);
    rotate.values["degrees"] = "45";
    int rotateId = rotate.id;
    graph.connect(startId, "then", rotateId, "in");
    std::string code = graphToCppKeepingGraph(graph, "Spinner", "Scripts/Spinner.graph");

    // Unchanged: the very graph.
    CppToGraphResult same = graphFromCpp(code);
    CHECK(same.graph && same.isTheKeptGraph);

    // Changed by hand: read from the code, with the change in it.
    std::string changed = code;
    size_t at = changed.find("radians(");
    at = at == std::string::npos ? at : changed.find("45", at);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    changed.replace(at, 2, "90");
    CppToGraphResult read = graphFromCpp(changed);
    CHECK(read.graph && !read.isTheKeptGraph);
    if (!read.graph) return;
    const Node* turned = first(*read.graph, "Rotate");
    CHECK(turned && turned->values.count("degrees") && turned->values.at("degrees") == "90");
    CHECK(countOf(*read.graph, "OnUpdate") == 1);
}

TEST(cpp_with_lines_no_box_says_keeps_them_as_cpp_boxes_among_the_others) {
    registerStandardNodes();
    const char* code = R"(
#include <vector>

struct Odd {
    float total = 0;                // what has been counted
    std::vector<int> seen;          // no box holds a list

    void update(World& world, Entity self, float dt) {
        // Count to three.
        for (int i = 0; i < 3; i++) {
            total += i;
        }
        seen.push_back(int(total));
        while (total > 10) total -= 10;
        logInfo("still here");      // and say so
    }
};
)";
    CppToGraphResult result = graphFromCpp(code);
    CHECK(result.graph.has_value() && !result.isCodeAsBoxes);
    if (!result.graph) return;
    const Graph& graph = *result.graph;
    // The loops and the rest are the usual boxes; the one line no box says is a C++ box in its place.
    CHECK(countOf(graph, "For") == 1 && countOf(graph, "While") == 1 && countOf(graph, "Log") == 1);
    CHECK(countOf(graph, "Code") == 1 && !hasCppBoxes(graph));
    const Node* kept = first(graph, "Code");
    CHECK(kept && kept->values.count("code") && kept->values.at("code") == "seen.push_back(int(total));");
    // The comments are the boxes' own.
    const Node* loop = first(graph, "For");
    CHECK(loop && loop->comment == "Count to three.");
    const Node* said = first(graph, "Log");
    CHECK(said && said->comment == "and say so");
    // The fields: a variable with its comment; the list and the #include kept as C++.
    const Variable* total = graph.variable("total");
    CHECK(total && total->comment == "what has been counted");
    CHECK(std::find(graph.code.begin(), graph.code.end(), "#include <vector>") != graph.code.end());
    bool keepsList = false;
    for (const std::string& member : graph.code) keepsList |= member.find("std::vector<int> seen;") != std::string::npos;
    CHECK(keepsList);
    CHECK(!result.notConverted.empty());
}

TEST(cpp_without_a_component_is_still_its_code_as_boxes) {
    CppToGraphResult result = graphFromCpp("int main() { return 0; }");
    CHECK(result.graph.has_value() && result.error.empty() && result.isCodeAsBoxes);
    CHECK(result.graph && cppFromBoxes(*result.graph) == "int main() { return 0; }");
}

namespace {

std::multiset<std::string> kindsOf(const Graph& graph) {
    std::multiset<std::string> out;
    for (const Node& node : graph.nodes) out.insert(node.type);
    return out;
}

}  // namespace

TEST(a_graph_is_written_as_the_cpp_it_means_and_comes_back_the_same) {
    registerStandardNodes();
    Graph graph;
    Variable speed;
    speed.name = "speed";
    speed.value = "90";
    speed.comment = "Degrees a second";
    graph.variables.push_back(speed);
    Variable lives;
    lives.name = "lives";
    lives.value = "3";
    lives.isShown = false;
    graph.variables.push_back(lives);
    Variable aim;
    aim.name = "aim";
    aim.type = PinType::Vec3;
    aim.value = "0 1 0";
    graph.variables.push_back(aim);

    int update = graph.addNode("OnUpdate", 0, 0).id;
    Node& turn = graph.addNode("Rotate", 300, 0);
    turn.comment = "Turn as fast as it is set to";
    int turnId = turn.id;
    int speedValue = graph.addNode("GetVariable", 300, 150).id;
    graph.find(speedValue)->values["name"] = "speed";
    graph.connect(speedValue, "value", turnId, "degrees");
    int branch = graph.addNode("Branch", 600, 0).id;
    int down = graph.addNode("KeyIsDown", 600, 150).id;
    graph.find(down)->values["key"] = "Space";
    graph.connect(down, "down", branch, "condition");
    int sound = graph.addNode("PlaySound", 900, 0).id;
    graph.find(sound)->values["sound"] = "Audio/jump.wav";
    int lose = graph.addNode("AddToVariable", 900, 150).id;
    graph.find(lose)->values["name"] = "lives";
    graph.find(lose)->values["amount"] = "-1";
    int loop = graph.addNode("For", 1200, 0).id;
    graph.find(loop)->values["to"] = "3";
    int say = graph.addNode("Log", 1500, 0).id;
    graph.find(say)->values["message"] = "100% done";
    int line = graph.addNode("Code", 1800, 0).id;
    graph.find(line)->values["code"] = "seen.push_back(1);";
    graph.code.push_back("std::vector<int> seen;");
    graph.connect(update, "then", turnId, "in");
    graph.connect(turnId, "then", branch, "in");
    graph.connect(branch, "true", sound, "in");
    graph.connect(branch, "false", lose, "in");
    graph.connect(lose, "then", loop, "in");
    graph.connect(loop, "do", say, "in");
    graph.connect(loop, "done", line, "in");

    std::string code = graphToCpp(graph, "Turner", "Scripts/Turner.graph");
    if (std::getenv("SHOW_CPP")) text::writeFile("/tmp/turner_in.cpp", code);
    // The code a programmer would write.
    CHECK(code.find("    float speed = 90;") != std::string::npos && code.find("// Degrees a second") != std::string::npos);
    CHECK(code.find("Vec3 aim = Vec3{0, 1, 0};") != std::string::npos);
    CHECK(code.find("// Turn as fast as it is set to\n") != std::string::npos);
    CHECK(code.find("transform->rotation = transform->rotation * axisAngle(Vec3{0, 1, 0}, radians((speed) * dt));") != std::string::npos);
    CHECK(code.find("if (world.input.isDown(Key::Space)) {") != std::string::npos);
    CHECK(code.find("world.playSound(\"Audio/jump.wav\", 1);") != std::string::npos);
    CHECK(code.find("lives += -1;") != std::string::npos);
    CHECK(code.find("for (float i = 0; i < 3; i += 1) {") != std::string::npos && code.find("logInfo(\"100%% done\");") != std::string::npos);
    CHECK(code.find("seen.push_back(1);") != std::string::npos && code.find("std::vector<int> seen;") != std::string::npos);
    CHECK(code.find("MOLTEN_FIELD(speed).help(\"Degrees a second\")") != std::string::npos && code.find("MOLTEN_FIELD(lives)") == std::string::npos);
    CHECK(code.find("call(") == std::string::npos);         // every box here has a line of its own

    // And back: the same boxes, the same variables, the same comments.
    CppToGraphResult back = graphFromCpp(code);
    CHECK(back.graph && !back.isCodeAsBoxes && !back.isTheKeptGraph);
    if (!back.graph) return;
    if (std::getenv("SHOW_CPP")) text::writeFile("/tmp/turner.graph", back.graph->toText());
    std::multiset<std::string> before = kindsOf(graph), after = kindsOf(*back.graph);
    for (const char* kind : {"OnUpdate", "Rotate", "Branch", "KeyIsDown", "PlaySound", "AddToVariable", "For", "Log", "Code"}) {
        CHECK(after.count(kind) == before.count(kind));
    }
    const Variable* speedBack = back.graph->variable("speed");
    CHECK(speedBack && speedBack->value == "90" && speedBack->comment == "Degrees a second" && speedBack->isShown);
    const Variable* livesBack = back.graph->variable("lives");
    CHECK(livesBack && !livesBack->isShown);
    const Variable* aimBack = back.graph->variable("aim");
    CHECK(aimBack && aimBack->type == PinType::Vec3);
    const Node* turned = nullptr;
    for (const Node& node : back.graph->nodes) {
        if (node.type == "Rotate") turned = &node;
    }
    CHECK(turned && turned->comment == "Turn as fast as it is set to");
    // Written again, it is the same code.
    CHECK(graphToCpp(*back.graph, "Turner", "Scripts/Turner.graph") == code);
}

TEST(every_box_goes_to_cpp_and_back) {
    registerStandardNodes();
    // One of each box that does something, in a row under On Update; each box that gives a value
    // put in a variable of its type.
    Graph graph;
    const char* names[] = {"", "number", "flag", "point", "words"};
    for (PinType type : {PinType::Float, PinType::Bool, PinType::Vec3, PinType::String}) {
        Variable v;
        v.name = names[int(type)];
        v.type = type;
        v.value = Value::fromText(type, "").toText();
        graph.variables.push_back(v);
    }
    int last = graph.addNode("OnUpdate", 0, 0).id;
    std::string lastPin = "then";
    float x = 300;
    for (const NodeDef& kind : NodeLibrary::instance().all()) {
        if (kind.isEvent || kind.type.rfind("Cpp", 0) == 0 || kind.type == "Return" || kind.type == "DestroySelf" || kind.type == "Destroy" || kind.type == "Sequence" ||
            kind.type == "LoadScene" || kind.type == "ReloadScene" || kind.type == "Wait" || kind.type == "WaitUntil" || kind.type == "Code") {
            continue;
        }
        bool acts = kind.findInput("in") != nullptr;
        int id = graph.addNode(kind.type, x, 0).id;
        x += 280;
        if (acts) {
            graph.connect(last, lastPin, id, "in");
            last = id;
            lastPin.clear();
            for (const PinDef& pin : kind.outputs) {
                if (pin.type == PinType::Flow && (pin.name == "then" || pin.name == "done" || pin.name == "false")) lastPin = pin.name;
            }
            if (lastPin.empty()) break;
            continue;
        }
        const PinDef* out = nullptr;
        for (const PinDef& pin : kind.outputs) {
            if (pin.type != PinType::Flow) {
                out = &pin;
                break;
            }
        }
        if (!out) continue;
        // Its first input wired from a variable, so that it is not a value worked out once and for all.
        for (const PinDef& pin : kind.inputs) {
            if (pin.type == PinType::Flow) continue;
            int from = graph.addNode("GetVariable", x - 100, 300).id;
            graph.find(from)->values["name"] = names[int(pin.type == PinType::Flow ? PinType::Float : pin.type)];
            graph.connect(from, "value", id, pin.name);
            break;
        }
        // The others something that is not left as it is by adding or multiplying it (0 and 1 are folded away).
        for (const PinDef& pin : kind.inputs) {
            if (pin.type == PinType::Float && !graph.linkTo(id, pin.name)) graph.find(id)->values[pin.name] = "2";
            if (pin.type == PinType::Vec3 && !graph.linkTo(id, pin.name)) graph.find(id)->values[pin.name] = "1 2 3";
        }
        int set = graph.addNode("SetVariable", x, 150).id;
        graph.find(set)->values["name"] = names[int(out->type)];
        graph.connect(id, out->name, set, "value");
        graph.connect(last, lastPin, set, "in");
        last = set;
        lastPin = "then";
    }
    std::string code = graphToCpp(graph, "Everything", "Everything.graph");
    if (std::getenv("SHOW_CPP")) text::writeFile("/tmp/everything.cpp", code);
    CppToGraphResult back = graphFromCpp(code);
    CHECK(back.graph && !back.isCodeAsBoxes);
    if (!back.graph) return;
    // Every box comes back as itself: none becomes a C++ box.
    CHECK(countOf(*back.graph, "Code") == 0);
    for (const std::string& missed : back.notConverted) std::printf("    not a box again: %s\n", missed.c_str());
    std::multiset<std::string> before = kindsOf(graph), after = kindsOf(*back.graph);
    for (const std::string& kind : std::set<std::string>(before.begin(), before.end())) {
        if (kind == "Number" || kind == "GetVariable") continue;      // a number box is the number itself in code; a variable read where a value folds away
        if (after.count(kind) < before.count(kind)) std::printf("    %s: %zu then, %zu now\n", kind.c_str(), before.count(kind), after.count(kind));
        CHECK(after.count(kind) >= before.count(kind));
    }
    if (std::getenv("SHOW_CPP")) text::writeFile("/tmp/everything2.cpp", graphToCpp(*back.graph, "Everything", "Everything.graph"));
    CHECK(graphToCpp(*back.graph, "Everything", "Everything.graph") == code);
}
