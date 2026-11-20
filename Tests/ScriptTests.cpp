#include "Test.hpp"

#include "Rendering/Material.hpp"
#include "Script/Runtime.hpp"

using namespace molten;
using namespace molten::script;

namespace {

/// A scene with one entity, and what a script needs to run on it.
struct Stage {
    Scene scene;
    Input input;
    Physics physics;
    ScriptState state;
    Entity entity;

    Stage() {
        registerStandardNodes();
        entity = scene.create("Actor");
        scene.add(entity, Transform{});
    }

    void run(const Graph& graph, float dt = 0.5f) {
        ExecContext context;
        context.scene = &scene;
        context.input = &input;
        context.physics = &physics;
        context.self = entity;
        context.dt = dt;
        context.state = &state;
        runFrame(graph, context);
        input.endFrame();
    }

    Transform& transform() { return *scene.get<Transform>(entity); }
};

}  // namespace

TEST(script_values_convert_between_types) {
    CHECK(Value::number(2.5f).asFloat() == 2.5f);
    CHECK(Value::number(0).asBool() == false && Value::number(-1).asBool());
    CHECK(Value::boolean(true).asFloat() == 1.0f);
    CHECK(Value::string("3.5").asFloat() == 3.5f);
    float x, y, z;
    Value::number(2).asVector(x, y, z);
    CHECK(x == 2 && y == 2 && z == 2);
    CHECK(Value::vector(1, 2, 3).toText() == "1 2 3");
    Value back = Value::fromText(PinType::Vec3, "1 2.5 -3");
    CHECK(back.x == 1 && back.y == 2.5f && back.z == -3);
    CHECK(Value::fromText(PinType::Vec3, "4").x == 4 && Value::fromText(PinType::Vec3, "4").y == 0);
    CHECK(Value::fromText(PinType::Bool, "1").asBool() && !Value::fromText(PinType::Bool, "0").asBool());
    CHECK(Value::fromText(PinType::String, "two words").text == "two words");
}

TEST(script_wires_follow_the_rules) {
    registerStandardNodes();
    Graph graph;
    int update = graph.addNode("OnUpdate", 0, 0).id;
    int rotate = graph.addNode("Rotate", 200, 0).id;
    int move = graph.addNode("Translate", 200, 100).id;
    int number = graph.addNode("Number", 0, 100).id;

    CHECK(graph.connect(update, "then", rotate, "in"));
    CHECK(graph.connect(number, "value", rotate, "degrees"));
    CHECK(!graph.connect(update, "then", rotate, "degrees"));      // flow into a value
    CHECK(!graph.connect(number, "value", rotate, "in"));          // a value into flow
    CHECK(!graph.connect(update, "nothing", rotate, "in"));
    CHECK(!graph.connect(rotate, "then", rotate, "in"));           // a node to itself
    CHECK(!graph.connect(update, "then", 999, "in"));
    CHECK(graph.links.size() == 2);

    // A flow output leads to one place: wiring it elsewhere moves the wire.
    CHECK(graph.connect(update, "then", move, "in"));
    CHECK(graph.links.size() == 2);
    CHECK(graph.linkFrom(update, "then")->toNode == move);
    // A value output may feed many inputs; an input takes one.
    CHECK(graph.connect(update, "dt", rotate, "degrees"));
    CHECK(graph.linkTo(rotate, "degrees")->fromNode == update);
    CHECK(graph.connect(update, "dt", move, "offset"));            // a number where a vector is wanted is allowed
    CHECK(graph.links.size() == 3);

    graph.removeNode(update);
    CHECK(graph.links.empty());
    CHECK(graph.find(update) == nullptr && graph.find(rotate) != nullptr);
}

TEST(script_files_round_trip) {
    registerStandardNodes();
    Graph graph = Graph::starter();
    int update = graph.nodes[1].id;
    Node& rotate = graph.addNode("Rotate", 320.5f, 80);
    rotate.values["degrees"] = "45";
    rotate.values["axis"] = "1 0 0";
    Node& log = graph.addNode("Log", 520, 80);
    log.values["message"] = "a \"quoted\" word";
    int rotateId = rotate.id, logId = log.id;
    graph.connect(update, "then", rotateId, "in");
    graph.connect(rotateId, "then", logId, "in");

    std::string text = graph.toText();
    std::optional<Graph> back = Graph::fromText(text);
    CHECK(back.has_value());
    CHECK(back->nodes.size() == 4 && back->links.size() == 2);
    CHECK(back->find(rotateId)->values.at("degrees") == "45");
    CHECK(back->find(logId)->values.at("message") == "a \"quoted\" word");
    CHECK(back->find(rotateId)->x == 320.5f);
    CHECK(back->toText() == text);
    CHECK(back->addNode("Number", 0, 0).id > logId);                // new ids never collide with old ones
    CHECK(!Graph::fromText("molten-scene 1\n").has_value());

    // A node of a kind this program does not know is kept, wires and all.
    std::optional<Graph> foreign = Graph::fromText("molten-graph 1\nnode 1 \"OnUpdate\" 0 0\nnode 2 \"FromAPlugin\" 9 9\n  value \"power\" \"3\"\nlink 1 \"then\" 2 \"in\"\n");
    CHECK(foreign.has_value() && foreign->toText().find("FromAPlugin") != std::string::npos);
    CHECK(foreign->links.size() == 1);
}

TEST(a_script_turns_and_moves_its_entity) {
    Stage stage;
    Graph graph;
    int update = graph.addNode("OnUpdate", 0, 0).id;
    Node& rotate = graph.addNode("Rotate", 0, 0);
    rotate.values["degrees"] = "90";
    int rotateId = rotate.id;
    Node& move = graph.addNode("Translate", 0, 0);
    move.values["offset"] = "2 0 0";
    int moveId = move.id;
    graph.connect(update, "then", rotateId, "in");
    graph.connect(rotateId, "then", moveId, "in");

    stage.run(graph, 0.5f);                     // half a second: 45 degrees, one unit
    Vec3 forward = stage.transform().forward();
    CHECK_NEAR(forward.x, -std::sin(radians(45.0f)), 1e-4);
    CHECK_NEAR(forward.z, -std::cos(radians(45.0f)), 1e-4);
    CHECK_NEAR(stage.transform().position.x, 1.0, 1e-5);

    // The speed from another node instead of a typed number: dt times 4, as degrees per second... of nothing typed.
    int multiply = graph.addNode("Multiply", 0, 0).id;
    graph.find(multiply)->values["a"] = "4";
    graph.find(multiply)->values["b"] = "0.5";
    graph.connect(multiply, "result", moveId, "offset");                // 2 becomes (2, 2, 2): a number read as a vector
    stage.run(graph, 1.0f);
    CHECK_NEAR(stage.transform().position.y, 2.0, 1e-5);
    CHECK_NEAR(stage.transform().position.x, 3.0, 1e-5);
}

TEST(a_script_starts_once_branches_on_keys_and_remembers) {
    Stage stage;
    Graph graph;
    // On Start: count = 10.
    int start = graph.addNode("OnStart", 0, 0).id;
    Node& set = graph.addNode("SetVariable", 0, 0);
    set.values["name"] = "count";
    set.values["value"] = "10";
    graph.connect(start, "then", set.id, "in");
    // On Update: if Space is down, count = count + 1; otherwise move up by 1.
    int update = graph.addNode("OnUpdate", 0, 0).id;
    int branch = graph.addNode("Branch", 0, 0).id;
    int key = graph.addNode("KeyIsDown", 0, 0).id;
    int get = graph.addNode("GetVariable", 0, 0).id;
    graph.find(get)->values["name"] = "count";
    int add = graph.addNode("Add", 0, 0).id;
    graph.find(add)->values["b"] = "1";
    Node& increment = graph.addNode("SetVariable", 0, 0);
    increment.values["name"] = "count";
    int incrementId = increment.id;
    Node& up = graph.addNode("Translate", 0, 0);
    up.values["offset"] = "0 1 0";
    up.values["perSecond"] = "0";
    int upId = up.id;
    graph.connect(update, "then", branch, "in");
    graph.connect(key, "down", branch, "condition");
    graph.connect(get, "value", add, "a");
    graph.connect(add, "result", incrementId, "value");
    graph.connect(branch, "true", incrementId, "in");
    graph.connect(branch, "false", upId, "in");

    stage.run(graph);
    CHECK(stage.state.variables["count"].asFloat() == 10);
    CHECK(stage.transform().position.y == 1);
    stage.input.handleKey(Key::Space, true);
    stage.run(graph);
    stage.run(graph);
    CHECK(stage.state.variables["count"].asFloat() == 12);          // On Start did not run again
    CHECK(stage.transform().position.y == 1);
    stage.input.handleKey(Key::Space, false);
    stage.run(graph);
    CHECK(stage.transform().position.y == 2);

    // On Key Pressed fires on the frame the key goes down, once.
    Graph pressed;
    int onKey = pressed.addNode("OnKeyPressed", 0, 0).id;
    pressed.find(onKey)->values["key"] = "j";
    Node& jump = pressed.addNode("Translate", 0, 0);
    jump.values["offset"] = "0 0 5";
    jump.values["perSecond"] = "0";
    pressed.connect(onKey, "then", jump.id, "in");
    stage.input.handleKey(Key::J, true);
    stage.run(pressed);
    stage.run(pressed);
    CHECK(stage.transform().position.z == 5);

    CHECK(keyByName("Space") == Key::Space && keyByName("left") == Key::Left && keyByName("7") == Key::Num7);
    CHECK(keyByName("nonsense") == Key::Unknown);
}

TEST(a_script_survives_loops_and_missing_things) {
    Stage stage;
    // White wires in a circle: stopped after a fixed number of steps, not forever.
    Graph loop;
    int update = loop.addNode("OnUpdate", 0, 0).id;
    Node& a = loop.addNode("Translate", 0, 0);
    a.values["offset"] = "1 0 0";
    a.values["perSecond"] = "0";
    int aId = a.id;
    int b = loop.addNode("SetScale", 0, 0).id;
    loop.links.push_back(Link{update, "then", aId, "in"});
    loop.links.push_back(Link{aId, "then", b, "in"});
    loop.links.push_back(Link{b, "then", aId, "in"});
    stage.run(loop);
    CHECK(stage.transform().position.x > 100);              // it ran many times, and it came back

    // Colored wires in a circle: a value that depends on itself is zero, not a crash.
    Graph circle;
    int add1 = circle.addNode("Add", 0, 0).id, add2 = circle.addNode("Add", 0, 0).id;
    circle.links.push_back(Link{add1, "result", add2, "a"});
    circle.links.push_back(Link{add2, "result", add1, "a"});
    int onUpdate = circle.addNode("OnUpdate", 0, 0).id;
    Node& set = circle.addNode("SetVariable", 0, 0);
    int setId = set.id;
    circle.connect(onUpdate, "then", setId, "in");
    circle.connect(add1, "result", setId, "value");
    stage.run(circle);
    CHECK(stage.state.variables.count("score") == 1);

    // Nodes of unknown kinds, wires to nowhere, physics nodes without a body: skipped.
    Graph broken;
    int u = broken.addNode("OnUpdate", 0, 0).id;
    int unknown = broken.addNode("NoSuchNode", 0, 0).id;
    int velocity = broken.addNode("SetVelocity", 0, 0).id;
    broken.links.push_back(Link{u, "then", unknown, "in"});
    broken.links.push_back(Link{u, "then", 12345, "in"});
    stage.run(broken);
    Graph noBody;
    int u2 = noBody.addNode("OnUpdate", 0, 0).id;
    noBody.connect(u2, "then", noBody.addNode("SetVelocity", 0, 0).id, "in");
    stage.run(noBody);
    (void)velocity;

    // Destroy Self ends the script for good.
    Graph destroy;
    int u3 = destroy.addNode("OnUpdate", 0, 0).id;
    int gone = destroy.addNode("DestroySelf", 0, 0).id;
    Node& after = destroy.addNode("Translate", 0, 0);
    after.values["offset"] = "0 100 0";
    destroy.connect(u3, "then", gone, "in");
    destroy.connect(gone, "then", after.id, "in");
    float before = stage.transform().position.y;
    stage.run(destroy);
    stage.scene.flushDestroyed();
    CHECK(!stage.scene.isAlive(stage.entity));
    (void)before;
}

TEST(every_standard_node_is_complete) {
    registerStandardNodes();
    CHECK(NodeLibrary::instance().all().size() >= 35);
    for (const NodeDef& definition : NodeLibrary::instance().all()) {
        CHECK(!definition.type.empty() && !definition.title.empty() && !definition.category.empty() && !definition.help.empty());
        bool hasFlowInput = definition.findInput("in") != nullptr;
        bool hasValueOutput = false;
        for (const PinDef& pin : definition.outputs) hasValueOutput = hasValueOutput || pin.type != PinType::Flow;
        CHECK(hasFlowInput == bool(definition.execute));               // what can be reached by a white wire can run
        if (hasValueOutput) CHECK(bool(definition.evaluate));          // what offers a value can work it out
        if (definition.isEvent) CHECK(definition.findOutput("then") != nullptr);
    }
    CHECK(ComponentRegistry::instance().find("VisualScript") != nullptr);
}
