#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Script/Convert.hpp"
#include "Script/CppBoxes.hpp"
#include "Script/CppToGraph.hpp"
#include "Script/Runtime.hpp"

#include <cstdlib>
#include <filesystem>

using namespace molten;
using namespace molten::script;

namespace {

const char* const kMotor = R"cpp(// Motor.cpp
// An engine behind a gearbox.
#include "Motor.hpp"

#include <algorithm>

using namespace molten;

namespace {

const float kGears[] = {3.58f, 2.05f, 1.42f};

/// The torque with the pedal down, at a speed.
float torqueAt(const EngineKind& kind, float rpm) {
    if (rpm > kind.redline) return 0;                               // the limiter
    if (rpm <= kind.peakRpm) {
        float t = saturate((rpm - kind.idle) / std::max(kind.peakRpm - kind.idle, 1.0f));
        t = t * t * (3 - 2 * t);
        return kind.peakTorque * t;
    } else if (rpm > 9000) {
        return -1;
    } else
        rpm += 1;
    for (int i = 0; i < 3; i++) rpm *= kGears[i];
    while (rpm > 100) {
        rpm -= 100;
    }
    do {
        rpm++;
    } while (rpm < 10);
    switch (int(rpm)) {
        case 1: return 2;
        default: break;
    }
    return kind.peakTorque;
}

}  // namespace

void Motor::fixedUpdate(World& world, Entity self, float dt) {
    Scene& scene = world.scene;
    Entity car = kit::vehicleOf(scene, self);
    if (!scene.isAlive(car)) return;

    // The wheels it turns, and how fast they are turning.
    int driven = 0;
    float spin = 0;
    scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&) {
        if (kit::vehicleOf(scene, entity) != car || !wheel.isDriven) return;
        driven++;
        spin += wheel.state.spin;
    });
    auto half = [](float x) { return x * 0.5f; };
    Vec3 up{0, 1, 0};
    const char* text = "a } brace { in a text; and a // comment that is none";
    char brace = '{';
    rpm = half(spin) + float(driven) + up.y + float(text[0]) + float(brace);
}

struct Spinner {
    float speed = 90;                   // degrees a second
    Vec3 axis{0, 1, 0};

    void update(World& world, Entity self, float dt) {
        if (Transform* transform = world.scene.get<Transform>(self)) transform->rotation = Quat(radians(speed * dt), axis) * transform->rotation;
    }
};

MOLTEN_COMPONENT(Spinner, MOLTEN_FIELD(speed).range(0, 720),
                 MOLTEN_FIELD(axis))
)cpp";

int countOf(const Graph& graph, const char* type) {
    int count = 0;
    for (const Node& node : graph.nodes) count += node.type == type;
    return count;
}

const Node* boxSaying(const Graph& graph, const char* type, const std::string& words) {
    for (const Node& node : graph.nodes) {
        auto code = node.values.find("code");
        if (node.type == type && code != node.values.end() && code->second.find(words) != std::string::npos) return &node;
    }
    return nullptr;
}

const Node* after(const Graph& graph, const Node* node, const char* pin) {
    const Link* link = node ? graph.linkFrom(node->id, pin) : nullptr;
    return link ? graph.find(link->toNode) : nullptr;
}

}  // namespace

TEST(any_cpp_is_laid_out_as_boxes_in_the_shape_it_has) {
    Graph graph = boxesFromCpp(kMotor);
    // Three functions (one of them in a struct), each with its first line on its box.
    CHECK(countOf(graph, "CppFunction") == 3);
    const Node* torque = boxSaying(graph, "CppFunction", "float torqueAt(const EngineKind& kind, float rpm)");
    const Node* fixed = boxSaying(graph, "CppFunction", "void Motor::fixedUpdate(World& world, Entity self, float dt)");
    const Node* update = boxSaying(graph, "CppFunction", "void update(World& world, Entity self, float dt)");
    CHECK(torque && fixed && update);
    // What is outside them is file boxes: the includes, the table, the struct's opening and its fields, the macro.
    CHECK(boxSaying(graph, "CppFile", "#include") == nullptr);                  // (preprocessor lines come before a box, with the comments)
    CHECK(boxSaying(graph, "CppFile", "using namespace molten;"));
    CHECK(boxSaying(graph, "CppFile", "const float kGears[] = {3.58f, 2.05f, 1.42f};"));
    CHECK(boxSaying(graph, "CppFile", "struct Spinner {") && boxSaying(graph, "CppFile", "float speed = 90;") && boxSaying(graph, "CppFile", "Vec3 axis{0, 1, 0};"));
    const Node* macro = boxSaying(graph, "CppFile", "MOLTEN_COMPONENT(Spinner");
    CHECK(macro && macro->values.at("code").find("MOLTEN_FIELD(axis))") != std::string::npos);

    // torqueAt: an if with one statement, an if with braces and an else-if chain, the loops, the switch.
    const Node* first = after(graph, torque, "then");
    CHECK(first && first->type == "CppIf" && first->values.at("code") == "rpm > kind.redline");
    const Node* limiter = after(graph, first, "true");
    CHECK(limiter && limiter->type == "CppCode" && limiter->values.at("code") == "return 0;" && !after(graph, limiter, "then"));
    const Node* second = after(graph, first, "then");
    CHECK(second && second->type == "CppIf" && second->values.at("code") == "rpm <= kind.peakRpm" && second->values.count("hasElse"));
    const Node* inBraces = after(graph, second, "true");
    CHECK(inBraces && inBraces->values.at("code").find("float t = saturate(") == 0);
    CHECK(after(graph, after(graph, inBraces, "then"), "then")->values.at("code") == "return kind.peakTorque * t;");
    const Node* elseIf = after(graph, second, "false");
    CHECK(elseIf && elseIf->type == "CppIf" && elseIf->values.at("code") == "rpm > 9000");
    CHECK(after(graph, elseIf, "false") && after(graph, elseIf, "false")->values.at("code") == "rpm += 1;");
    const Node* loop = after(graph, second, "then");
    CHECK(loop && loop->type == "CppBlock" && loop->values.at("code") == "for (int i = 0; i < 3; i++)");
    CHECK(after(graph, loop, "body") && after(graph, loop, "body")->values.at("code") == "rpm *= kGears[i];");
    const Node* during = after(graph, loop, "then");
    CHECK(during && during->type == "CppBlock" && during->values.at("code") == "while (rpm > 100)");
    const Node* doing = after(graph, during, "then");
    CHECK(doing && doing->type == "CppBlock" && doing->values.at("code") == "do" && doing->values.at("tail") == " while (rpm < 10);");
    const Node* choice = after(graph, doing, "then");
    CHECK(choice && choice->type == "CppBlock" && choice->values.at("code") == "switch (int(rpm))");
    const Node* label = after(graph, choice, "body");
    CHECK(label && label->values.at("code") == "case 1:" && after(graph, label, "then")->values.at("code") == "return 2;");
    CHECK(after(graph, choice, "then") && after(graph, choice, "then")->values.at("code") == "return kind.peakTorque;");

    // fixedUpdate: a function written in place as the last argument is a block, with its statements; one kept in a
    // variable too; braces and semicolons inside texts and letters are not taken for the code's.
    const Node* each = boxSaying(graph, "CppBlock", "scene.each<kit::Wheel, Transform>(");
    CHECK(each && each->values.at("code") == "scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel& wheel, Transform&)" && each->values.at("tail") == ");");
    const Node* inEach = after(graph, each, "body");
    CHECK(inEach && inEach->type == "CppIf" && after(graph, inEach, "then")->values.at("code") == "driven++;");
    const Node* kept = boxSaying(graph, "CppBlock", "auto half = [](float x)");
    CHECK(kept && after(graph, kept, "body")->values.at("code") == "return x * 0.5f;");
    CHECK(boxSaying(graph, "CppCode", "const char* text = \"a } brace { in a text; and a // comment that is none\";"));
    CHECK(boxSaying(graph, "CppCode", "char brace = '{';"));
    // The comment above a statement is its box's.
    const Node* driven = boxSaying(graph, "CppCode", "int driven = 0;");
    CHECK(driven && cppBoxComment(*driven) == "The wheels it turns, and how fast they are turning.");
    CHECK(cppBoxComment(*torque) == "The torque with the pedal down, at a speed.");
    // Every box has a place of its own on the board, and a size its code fits in.
    for (const Node& a : graph.nodes) {
        float width = 0, height = 0;
        CHECK(cppBoxSize(a, width, height) && width >= 224 && height > 50);
        for (const Node& b : graph.nodes) {
            if (&a != &b) CHECK(!(a.x == b.x && a.y == b.y));
        }
    }

    // And written out, it is the file: to the character, also after a trip through a .graph file.
    CHECK(cppFromBoxes(graph) == kMotor);
    std::optional<Graph> read = Graph::fromText(graph.toText());
    CHECK(read && cppFromBoxes(*read) == kMotor);
    CHECK(hasCppBoxes(graph) && graphToCpp(graph, "Motor", "Motor.graph") == kMotor);
}

TEST(boxes_changed_on_the_board_write_the_code_that_says_so) {
    Graph graph = boxesFromCpp("void Lamp::update(World& world, Entity self, float dt) {\n    if (isOn) {\n        glow += dt;\n    }\n    count++;\n}\n");
    Node* condition = const_cast<Node*>(boxSaying(graph, "CppIf", "isOn"));
    Node* counted = const_cast<Node*>(boxSaying(graph, "CppCode", "count++;"));
    const Node* function = boxSaying(graph, "CppFunction", "Lamp::update");
    CHECK(condition && counted && function);
    // A condition edited, a statement added after one (as the board adds it: no semicolon, nothing about where it was in a file).
    condition->values["code"] = "isOn && glow < 2";
    Node& added = graph.addNode("CppCode", 0, 0);
    added.values["code"] = "world.log(\"lit\")";
    int addedId = added.id, countedId = boxSaying(graph, "CppCode", "count++;")->id;
    CHECK(graph.connect(countedId, "then", addedId, "in"));
    // An else, wired on the board to a new box.
    Node& otherwise = graph.addNode("CppCode", 0, 0);
    otherwise.values["code"] = "glow = 0;";
    CHECK(graph.connect(boxSaying(graph, "CppIf", "isOn")->id, "false", otherwise.id, "in"));
    CHECK(cppFromBoxes(graph) == "void Lamp::update(World& world, Entity self, float dt) {\n    if (isOn && glow < 2) {\n        glow += dt;\n    } else {\n        glow = 0;\n    }\n"
                                 "    count++;\n    world.log(\"lit\");\n}\n");
    // A function and a file box made on the board from nothing.
    Graph made;
    registerStandardNodes();
    Node& include = made.addNode("CppFile", 80, 80);
    include.values["code"] = "#include \"MoltenMetal.hpp\"";
    Node& start = made.addNode("CppFunction", 80, 200);
    start.values["code"] = "void Door::start(World& world, Entity self)";
    Node& step = made.addNode("CppBlock", 400, 200);
    step.values["code"] = "for (int i = 0; i < 3; i++)";
    Node& inside = made.addNode("CppCode", 450, 320);
    inside.values["code"] = "open(i)";
    Node& box = made.addNode("Rotate", 700, 200);
    box.values["degrees"] = "90";
    int startId = made.nodes[1].id, stepId = made.nodes[2].id, insideId = made.nodes[3].id, boxId = made.nodes[4].id;
    CHECK(made.connect(startId, "then", stepId, "in") && made.connect(stepId, "body", insideId, "in") && made.connect(stepId, "then", boxId, "in"));
    std::string written = cppFromBoxes(made);
    CHECK(written.find("#include \"MoltenMetal.hpp\"\n\nvoid Door::start(World& world, Entity self) {\n    for (int i = 0; i < 3; i++) {\n        open(i);\n    }\n") == 0);
    // A box of the other kind among them is the call that runs it.
    CHECK(written.find("call(\"Rotate\", {{\"degrees\", script::Value::number(90)}});") != std::string::npos && written.find("// the box Rotate") != std::string::npos);
    CHECK(written.rfind("}\n") == written.size() - 2);
}

TEST(what_cannot_be_followed_is_one_box_and_still_the_file) {
    for (const char* broken : {"void f() {\n    if (x) {\n}\n", "int a = 1; }\nint b;\n", "const char* s = \"never closed;\n", "/* never closed", "#if A\nvoid f() {\n#else\nvoid g() {\n#endif\n}\n",
                               "", "\n\n", "// only a comment\n", "x"}) {
        Graph graph = boxesFromCpp(broken);
        CHECK(cppFromBoxes(graph) == broken);
        CHECK(countOf(graph, "CppFunction") <= 1);
    }
}

TEST(a_script_the_boxes_cannot_say_becomes_code_boxes_and_one_they_can_stays_boxes) {
    registerStandardNodes();
    CppToGraphResult motor = graphFromCpp(kMotor);
    CHECK(motor.graph && motor.isCodeAsBoxes && motor.error.empty() && motor.notConverted.empty());
    CHECK(hasCppBoxes(*motor.graph) && cppFromBoxes(*motor.graph) == kMotor);
    // The other way: what is written in the style the boxes know is still those boxes.
    Graph simple;
    Node& update = simple.addNode("OnUpdate", 80, 80);
    Node& rotate = simple.addNode("Rotate", 320, 80);
    rotate.values["degrees"] = "90";
    int updateId = simple.nodes[0].id, rotateId = simple.nodes[1].id;
    CHECK(simple.connect(updateId, "then", rotateId, "in"));
    (void)update;
    CppToGraphResult back = graphFromCpp(graphToCppKeepingGraph(simple, "Turner", "Turner.graph"));
    CHECK(back.graph && !back.isCodeAsBoxes && !hasCppBoxes(*back.graph) && back.isTheKeptGraph);
}

namespace {
/// A graph of C++ boxes as an outline, for looking at what a file became (MOLTEN_BOXES_OUTLINE=<a file> prints it).
void writeOutline(const Graph& graph, const Node* node, int depth, std::string& out) {
    for (; node; node = after(graph, node, "then")) {
        std::string text = cppBoxText(*node);
        if (size_t line = text.find('\n'); line != std::string::npos) text = text.substr(0, line) + " ...";
        out += std::string(size_t(depth) * 4, ' ') + (node->type == "CppIf" ? "? " : node->type == "CppBlock" ? "@ " : "- ") + text + "\n";
        if (node->type == "CppIf") {
            writeOutline(graph, after(graph, node, "true"), depth + 1, out);
            if (after(graph, node, "false")) {
                out += std::string(size_t(depth) * 4, ' ') + "  else\n";
                writeOutline(graph, after(graph, node, "false"), depth + 1, out);
            }
        }
        if (node->type == "CppBlock") writeOutline(graph, after(graph, node, "body"), depth + 1, out);
    }
}
}  // namespace

TEST(every_source_file_of_the_engine_goes_to_boxes_and_back_to_the_same_bytes) {
    if (const char* shown = std::getenv("MOLTEN_BOXES_OUTLINE")) {
        if (std::optional<std::string> text = text::readFile(shown)) {
            Graph graph = boxesFromCpp(*text);
            std::vector<const Node*> top;
            for (const Node& node : graph.nodes) {
                if (node.type == "CppFile" || node.type == "CppFunction") top.push_back(&node);
            }
            std::string out;
            for (const Node* node : top) {
                std::string text = cppBoxText(*node);
                if (size_t line = text.find('\n'); line != std::string::npos) text = text.substr(0, line) + " ...";
                out += (node->type == "CppFunction" ? "FUNCTION " : "file     ") + text + "\n";
                if (node->type == "CppFunction") writeOutline(graph, after(graph, node, "then"), 1, out);
            }
            std::printf("%s", out.c_str());
        }
    }
    // The engine's own code, the editor's, the tests' and the Driving game's scripts: some hundreds of files of
    // every kind of C++ the project has. Each must come back from its boxes as it went in.
    namespace fs = std::filesystem;
    std::error_code code;
    fs::path root = fs::path(__FILE__).parent_path().parent_path();
    std::vector<fs::path> folders;
    for (const char* name : {"Engine", "Editor", "Tests", "Player", "Plugins", "Samples"}) {
        if (fs::is_directory(root / name, code)) folders.push_back(root / name);
    }
    if (const char* projects = std::getenv("MOLTEN_PROJECTS")) folders.push_back(fs::path(projects));
    else if (fs::is_directory(root.parent_path() / "MoltenProjects", code)) folders.push_back(root.parent_path() / "MoltenProjects");
    int files = 0, functions = 0, statements = 0, whole = 0, different = 0;
    std::string firstDifferent, firstWhole;
    for (const fs::path& folder : folders) {
        for (fs::recursive_directory_iterator file(folder, fs::directory_options::skip_permission_denied, code), end; !code && file != end; file.increment(code)) {
            std::string extension = file->path().extension().string();
            if (extension != ".cpp" && extension != ".hpp" && extension != ".h" && extension != ".mm") continue;
            if (file->path().string().find("/Library/") != std::string::npos || file->path().string().find("/build/") != std::string::npos) continue;
            std::optional<std::string> text = text::readFile(file->path().string());
            if (!text) continue;
            files++;
            Graph graph = boxesFromCpp(*text);
            std::optional<Graph> read = Graph::fromText(graph.toText());
            if (cppFromBoxes(graph) != *text || !read || cppFromBoxes(*read) != *text) {
                if (different++ == 0) firstDifferent = file->path().string();
            }
            int inIt = countOf(graph, "CppFunction");
            functions += inIt;
            statements += countOf(graph, "CppCode") + countOf(graph, "CppIf") + countOf(graph, "CppBlock");
            if (inIt == 0 && graph.nodes.size() == 1 && text->size() > 400) {
                if (whole++ == 0) firstWhole = file->path().string();
            }
        }
    }
    std::printf("    (%d files as boxes: %d functions, %d statements; %d files kept whole%s%s)\n", files, functions, statements, whole, whole ? ", the first " : "", firstWhole.c_str());
    if (different) std::printf("    not the same again: %s\n", firstDifferent.c_str());
    CHECK(different == 0);
    // Where the sources are (they are where the tests were built, unless the program was carried elsewhere): nearly all of them are followed.
    if (files > 0) CHECK(files > 50 && functions > 1000 && whole * 20 < files);
}
