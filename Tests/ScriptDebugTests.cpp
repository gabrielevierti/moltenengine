// Watching a visual script run: values on the wires, what ran, and breakpoints.
#include "Test.hpp"

#include "Script/Runtime.hpp"

using namespace molten;
using namespace molten::script;

TEST(the_debugger_shows_values_and_holds_at_breakpoints) {
    registerStandardNodes();
    Scene scene;
    Input input;
    Physics physics;
    ScriptState state;
    Entity actor = scene.create("Actor");
    scene.add(actor, Transform{});
    // Every frame: move by 2 x 0.5 along x, then turn.
    Graph graph;
    int update = graph.addNode("OnUpdate", 0, 0).id;
    int multiply = graph.addNode("Multiply", 0, 0).id;
    graph.find(multiply)->values["a"] = "2";
    graph.find(multiply)->values["b"] = "0.5";
    int move = graph.addNode("Translate", 0, 0).id;
    int turn = graph.addNode("Rotate", 0, 0).id;
    graph.find(turn)->values["degrees"] = "0";
    graph.connect(update, "then", move, "in");
    graph.connect(multiply, "result", move, "offset");
    graph.connect(move, "then", turn, "in");

    script::Debugger& debugger = script::Debugger::instance();
    debugger.clearSeen();
    debugger.breakpoints.clear();
    debugger.graph = "Scripts/Mover.graph";
    debugger.isOn = true;
    double time = 0;
    auto run = [&] {
        ExecContext context;
        context.scene = &scene;
        context.input = &input;
        context.physics = &physics;
        context.self = actor;
        context.dt = 1;
        context.time = time;
        context.state = &state;
        context.graphName = "Scripts/Mover.graph";
        debugger.now = time;
        runFrame(graph, context);
        time += 1;
    };
    run();
    CHECK(debugger.entity == actor);
    auto result = std::make_pair(multiply, std::string("result"));
    CHECK(debugger.values.count(result) && debugger.values[result] == "1");
    CHECK(debugger.ran.count(move) && debugger.ran.count(turn) && debugger.ran[move] == 0);
    CHECK(std::abs(scene.get<Transform>(actor)->position.x - 1) < 1e-5f);

    // A breakpoint on Translate: the flow stops before it, and asks for the game to be held.
    debugger.toggleBreakpoint("Scripts/Mover.graph", move);
    CHECK(debugger.hasBreakpoint("Scripts/Mover.graph", move));
    run();
    CHECK(debugger.stopped.node == move && debugger.stopped.entity == actor && debugger.wantsHold);
    CHECK(std::abs(scene.get<Transform>(actor)->position.x - 1) < 1e-5f);       // not moved: held before it
    // Going on: the node held at runs, and the flow after it.
    debugger.wantsHold = false;
    debugger.stopped = script::Debugger::Stop{};
    debugger.breakpoints.clear();
    double ranTurn = debugger.ran[turn];
    run();
    CHECK(std::abs(scene.get<Transform>(actor)->position.x - 3) < 1e-5f);       // the held move, and this frame's
    CHECK(debugger.ran[turn] > ranTurn);
    // One step: held before the next node that runs.
    debugger.stepsOnce = true;
    run();
    CHECK(debugger.stopped.node == move && !debugger.stepsOnce);
    debugger.stopped = script::Debugger::Stop{};
    debugger.stepsOnce = true;
    run();                                                                      // the held move runs; held again before Rotate
    CHECK(debugger.stopped.node == turn);
    // Another script is not watched; off, nothing is noted.
    debugger.clearSeen();
    debugger.graph = "Scripts/Other.graph";
    run();
    CHECK(debugger.values.empty() && debugger.ran.empty());
    debugger.isOn = false;
    debugger.graph.clear();
}
