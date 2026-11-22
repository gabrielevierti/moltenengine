// C++ turned into boxes and wires: code made from a graph (changed or not), and code written by hand.
#include "Test.hpp"

#include "Script/Convert.hpp"
#include "Script/CppBoxes.hpp"
#include "Script/CppToGraph.hpp"
#include "Script/Runtime.hpp"

#include <algorithm>

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
    // The field is a variable, set to its value when the game starts.
    bool setsSpeed = false;
    for (const Node& node : graph.nodes) {
        if (node.type == "SetVariable" && node.values.count("name") && node.values.at("name") == "speed") setsSpeed = node.values.at("value") == "3";
    }
    CHECK(setsSpeed);
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
    size_t at = changed.find("script::Value::number(45)");
    CHECK(at != std::string::npos);
    changed.replace(at, std::string("script::Value::number(45)").size(), "script::Value::number(90)");
    CppToGraphResult read = graphFromCpp(changed);
    CHECK(read.graph && !read.isTheKeptGraph);
    if (!read.graph) return;
    const Node* turned = first(*read.graph, "Rotate");
    CHECK(turned && turned->values.count("degrees") && turned->values.at("degrees") == "90");
    CHECK(countOf(*read.graph, "OnUpdate") == 1);
}

TEST(cpp_with_lines_no_box_says_becomes_code_boxes_and_nothing_is_left_out) {
    registerStandardNodes();
    const char* code = R"(
struct Odd {
    void update(World& world, Entity self, float dt) {
        for (int i = 0; i < 3; i++) { }
        logInfo("still here");
    }
};
)";
    // A loop has no box of its own: the whole of it is the code itself as boxes, every line in one, and back the same.
    CppToGraphResult result = graphFromCpp(code);
    CHECK(result.graph.has_value() && result.isCodeAsBoxes && result.notConverted.empty());
    CHECK(result.graph && countOf(*result.graph, "CppBlock") == 1 && countOf(*result.graph, "CppCode") == 1 && countOf(*result.graph, "CppFunction") == 1);
    CHECK(result.graph && cppFromBoxes(*result.graph) == code);
}

TEST(cpp_without_a_component_is_still_its_code_as_boxes) {
    CppToGraphResult result = graphFromCpp("int main() { return 0; }");
    CHECK(result.graph.has_value() && result.error.empty() && result.isCodeAsBoxes);
    CHECK(result.graph && cppFromBoxes(*result.graph) == "int main() { return 0; }");
}
