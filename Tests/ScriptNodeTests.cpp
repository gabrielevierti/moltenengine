// The nodes a whole game is made of without code: acting on other entities, loops and gates,
// finding things, numbers, words.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Script/Runtime.hpp"

using namespace molten;
using namespace molten::script;

namespace {

/// A world with one entity, and what a script needs to run on it.
struct Set {
    World world;
    ScriptState state;
    Entity actor;
    double time = 0;

    Set() {
        registerStandardNodes();
        actor = world.scene.create("Actor");
        world.scene.add(actor, Transform{});
    }

    Entity thing(const char* name, Vec3 position, const char* tag = nullptr) {
        Entity entity = world.scene.create(name);
        Transform transform;
        transform.position = position;
        world.scene.add(entity, transform);
        if (tag) world.scene.add(entity, Tag{tag, 0});
        world.scene.updateTransforms();
        return entity;
    }

    void run(const Graph& graph, float dt = 0.1f, Entity other = Entity{}) {
        ExecContext context;
        context.world = &world;
        context.scene = &world.scene;
        context.input = &world.input;
        context.physics = &world.physics;
        context.self = actor;
        context.other = other;
        context.dt = dt;
        time += dt;
        context.time = time;
        context.state = &state;
        runFrame(graph, context);
        world.input.endFrame();
        world.scene.flushDestroyed();
        world.scene.updateTransforms();
    }

    Vec3 at(Entity entity) { return world.scene.get<Transform>(entity)->position; }
};

/// On Update wired to a node; returns that node's id.
int onUpdate(Graph& graph, const char* type) {
    int update = -1;
    for (const Node& node : graph.nodes) {
        if (node.type == "OnUpdate") update = node.id;
    }
    if (update < 0) update = graph.addNode("OnUpdate", 0, 0).id;
    int id = graph.addNode(type, 0, 0).id;
    graph.connect(update, "then", id, "in");
    return id;
}

/// What a value node gives now, read through a Set Variable.
Value read(Set& set, Graph& graph, int node, const char* pin, PinType type) {
    Graph probe = graph;
    int keep = onUpdate(probe, type == PinType::String ? "SetText" : "SetVariable");
    if (type == PinType::String) {
        probe.find(keep)->values["name"] = "probe";
        probe.connect(node, pin, keep, "text");
        set.run(probe, 0.0f);
        return Value::string(set.world.text("probe"));
    }
    if (type == PinType::Vec3) {
        // A point is read by putting something there.
        Entity marker = set.world.scene.find("Probe");
        if (!set.world.scene.isAlive(marker)) marker = set.thing("Probe", Vec3{0, 0, 0});
        probe.find(keep)->type = "SetPosition";
        probe.find(keep)->values["on"] = "Probe";
        probe.connect(node, pin, keep, "position");
        set.run(probe, 0.0f);
        Vec3 at = set.at(marker);
        return Value::vector(at.x, at.y, at.z);
    }
    probe.find(keep)->values["name"] = "probe";
    probe.connect(node, pin, keep, "value");
    set.run(probe, 0.0f);
    return set.state.variables["probe"];
}

}  // namespace

TEST(a_node_acts_on_the_entity_its_on_pin_names) {
    Set set;
    Entity door = set.thing("Door", Vec3{5, 0, 0});
    Graph graph;
    int move = onUpdate(graph, "Translate");
    graph.find(move)->values["offset"] = "0 2 0";
    graph.find(move)->values["on"] = "Door";
    set.run(graph, 0.5f);
    CHECK_NEAR(set.at(door).y, 1.0, 1e-5);                      // the door moved
    CHECK_NEAR(set.at(set.actor).y, 0.0, 1e-5);                 // the one with the script did not

    // Nothing typed, or "self": the script's own entity, as it always was.
    graph.find(move)->values["on"] = "self";
    set.run(graph, 0.5f);
    CHECK_NEAR(set.at(set.actor).y, 1.0, 1e-5);
    // An entity that is not there: nothing happens, nothing breaks.
    graph.find(move)->values["on"] = "Nobody";
    set.run(graph, 0.5f);
    CHECK_NEAR(set.at(door).y, 1.0, 1e-5);
}

TEST(the_other_of_a_collision_can_be_destroyed_or_switched_off) {
    Set set;
    Entity coin = set.thing("Coin", Vec3{1, 0, 0}, "Pickup");
    Graph graph;
    int destroy = onUpdate(graph, "Destroy");                   // "on" is "other" unless something else is typed
    (void)destroy;
    set.run(graph, 0.1f, coin);
    CHECK(!set.world.scene.isAlive(coin));
    CHECK(set.world.scene.isAlive(set.actor));

    Entity lamp = set.thing("Lamp", Vec3{0, 3, 0});
    Graph switches;
    int off = onUpdate(switches, "SetActive");
    switches.find(off)->values["active"] = "0";
    switches.find(off)->values["on"] = "Lamp";
    set.run(switches);
    CHECK(!set.world.scene.get<Transform>(lamp)->isActive);
}

TEST(loops_and_gates_run_what_follows_as_often_as_they_say) {
    Set set;
    // Repeat 4: the variable counts the turns, and index says which.
    Graph graph;
    int repeat = onUpdate(graph, "Repeat");
    graph.find(repeat)->values["times"] = "4";
    int add = graph.addNode("AddToVariable", 0, 0).id;
    graph.find(add)->values["name"] = "turns";
    graph.find(add)->values["amount"] = "1";
    graph.connect(repeat, "loop", add, "in");
    int sum = graph.addNode("AddToVariable", 0, 0).id;
    graph.find(sum)->values["name"] = "indices";
    graph.connect(add, "then", sum, "in");
    graph.connect(repeat, "index", sum, "amount");
    int done = graph.addNode("SetVariable", 0, 0).id;
    graph.find(done)->values["name"] = "done";
    graph.find(done)->values["value"] = "1";
    graph.connect(repeat, "done", done, "in");
    set.run(graph);
    CHECK_NEAR(set.state.variables["turns"].asFloat(), 4.0, 1e-6);
    CHECK_NEAR(set.state.variables["indices"].asFloat(), 0 + 1 + 2 + 3, 1e-6);
    CHECK(set.state.variables["done"].asBool());

    // Do Once lets the first frame through and no other.
    Set once;
    Graph gate;
    int doOnce = onUpdate(gate, "DoOnce");
    int count = gate.addNode("AddToVariable", 0, 0).id;
    gate.find(count)->values["name"] = "times";
    gate.find(count)->values["amount"] = "1";
    gate.connect(doOnce, "then", count, "in");
    for (int i = 0; i < 5; i++) once.run(gate);
    CHECK_NEAR(once.state.variables["times"].asFloat(), 1.0, 1e-6);

    // A cooldown of half a second, asked ten times a second for two seconds: 4 or 5 times through.
    Set cool;
    Graph timed;
    int cooldown = onUpdate(timed, "Cooldown");
    timed.find(cooldown)->values["seconds"] = "0.5";
    int shots = timed.addNode("AddToVariable", 0, 0).id;
    timed.find(shots)->values["name"] = "shots";
    timed.find(shots)->values["amount"] = "1";
    timed.connect(cooldown, "then", shots, "in");
    for (int i = 0; i < 20; i++) cool.run(timed, 0.1f);
    float fired = cool.state.variables["shots"].asFloat();
    CHECK(fired >= 4 && fired <= 5);

    // Flip Flop: one way, then the other.
    Set flip;
    Graph sides;
    int flipFlop = onUpdate(sides, "FlipFlop");
    int a = sides.addNode("AddToVariable", 0, 0).id, b = sides.addNode("AddToVariable", 0, 0).id;
    sides.find(a)->values["name"] = "a";
    sides.find(a)->values["amount"] = "1";
    sides.find(b)->values["name"] = "b";
    sides.find(b)->values["amount"] = "1";
    sides.connect(flipFlop, "a", a, "in");
    sides.connect(flipFlop, "b", b, "in");
    for (int i = 0; i < 5; i++) flip.run(sides);
    CHECK_NEAR(flip.state.variables["a"].asFloat(), 3.0, 1e-6);
    CHECK_NEAR(flip.state.variables["b"].asFloat(), 2.0, 1e-6);

    // Sequence: all three, in order.
    Set ordered;
    Graph sequence;
    int seq = onUpdate(sequence, "Sequence");
    const char* pins[] = {"first", "second", "third"};
    for (int i = 0; i < 3; i++) {
        int step = sequence.addNode("SetVariable", 0, 0).id;
        sequence.find(step)->values["name"] = "last";
        sequence.find(step)->values["value"] = std::to_string(i + 1);
        sequence.connect(seq, pins[i], step, "in");
        int tally = sequence.addNode("AddToVariable", 0, 0).id;
        sequence.find(tally)->values["name"] = "all";
        sequence.find(tally)->values["amount"] = "1";
        sequence.connect(step, "then", tally, "in");
    }
    ordered.run(sequence);
    CHECK_NEAR(ordered.state.variables["all"].asFloat(), 3.0, 1e-6);
    CHECK_NEAR(ordered.state.variables["last"].asFloat(), 3.0, 1e-6);
}

TEST(things_are_found_by_tag_and_what_is_found_can_be_acted_on) {
    Set set;
    Entity near = set.thing("Near", Vec3{2, 0, 0}, "Enemy");
    Entity far = set.thing("Far", Vec3{9, 0, 0}, "Enemy");
    set.thing("Coin", Vec3{1, 0, 0}, "Pickup");

    Graph graph;
    int nearest = graph.addNode("Nearest", 0, 0).id;
    graph.find(nearest)->values["tag"] = "Enemy";
    CHECK(read(set, graph, nearest, "exists", PinType::Bool).asBool());
    CHECK_NEAR(read(set, graph, nearest, "distance", PinType::Float).asFloat(), 2.0, 1e-5);
    int count = graph.addNode("CountWithTag", 0, 0).id;
    graph.find(count)->values["tag"] = "Enemy";
    CHECK_NEAR(read(set, graph, count, "count", PinType::Float).asFloat(), 2.0, 1e-6);

    // What Nearest found, wired into another node's "on": two with the same tag are told apart.
    int destroy = onUpdate(graph, "Destroy");
    graph.connect(nearest, "entity", destroy, "on");
    set.run(graph);
    CHECK(!set.world.scene.isAlive(near) && set.world.scene.isAlive(far));

    // For Each With Tag: every one of them, each in its turn.
    Entity third = set.thing("Third", Vec3{4, 0, 0}, "Enemy");
    Graph all;
    int each = onUpdate(all, "ForEachWithTag");
    all.find(each)->values["tag"] = "Enemy";
    int lift = all.addNode("SetPosition", 0, 0).id;
    all.find(lift)->values["position"] = "0 7 0";
    all.connect(each, "loop", lift, "in");
    all.connect(each, "entity", lift, "on");
    set.run(all);
    CHECK_NEAR(set.at(far).y, 7.0, 1e-5);
    CHECK_NEAR(set.at(third).y, 7.0, 1e-5);
    CHECK_NEAR(set.at(set.actor).y, 0.0, 1e-5);

    // Nothing with that tag: "exists" says no, and acting on what was not found does nothing.
    graph.find(nearest)->values["tag"] = "Dragon";
    CHECK(!read(set, graph, nearest, "exists", PinType::Bool).asBool());
    set.run(graph);
    CHECK(set.world.scene.isAlive(set.actor));
}

TEST(numbers_points_and_words_are_worked_out) {
    Set set;
    Graph graph;
    auto node = [&](const char* type, std::initializer_list<std::pair<const char*, const char*>> values) {
        Node& made = graph.addNode(type, 0, 0);
        for (auto& [pin, value] : values) made.values[pin] = value;
        return made.id;
    };
    auto number = [&](int id, const char* pin = "result") { return read(set, graph, id, pin, PinType::Float).asFloat(); };

    CHECK_NEAR(number(node("Clamp", {{"value", "7"}, {"least", "0"}, {"most", "5"}})), 5.0, 1e-6);
    CHECK_NEAR(number(node("Clamp", {{"value", "-2"}, {"least", "5"}, {"most", "0"}})), 0.0, 1e-6);         // limits the wrong way round
    CHECK_NEAR(number(node("Lerp", {{"a", "10"}, {"b", "20"}, {"t", "0.25"}})), 12.5, 1e-5);
    CHECK_NEAR(number(node("Min", {{"a", "3"}, {"b", "-1"}})), -1.0, 1e-6);
    CHECK_NEAR(number(node("Max", {{"a", "3"}, {"b", "-1"}})), 3.0, 1e-6);
    CHECK_NEAR(number(node("Abs", {{"value", "-4.5"}})), 4.5, 1e-6);
    CHECK_NEAR(number(node("Round", {{"value", "2.6"}})), 3.0, 1e-6);
    CHECK_NEAR(number(node("Modulo", {{"a", "7"}, {"b", "3"}})), 1.0, 1e-6);
    CHECK_NEAR(number(node("Modulo", {{"a", "7"}, {"b", "0"}})), 0.0, 1e-6);                                 // nothing divides by nothing
    CHECK_NEAR(number(node("Power", {{"a", "3"}, {"b", "2"}})), 9.0, 1e-5);
    CHECK_NEAR(number(node("Sqrt", {{"value", "16"}})), 4.0, 1e-6);
    CHECK_NEAR(number(node("Sqrt", {{"value", "-16"}})), 0.0, 1e-6);
    CHECK_NEAR(number(node("Cos", {{"degrees", "60"}})), 0.5, 1e-5);
    CHECK(read(set, graph, node("Equal", {{"a", "2"}, {"b", "2"}}), "result", PinType::Bool).asBool());
    CHECK_NEAR(number(node("Length", {{"vector", "3 4 0"}}), "length"), 5.0, 1e-5);
    CHECK_NEAR(number(node("Dot", {{"a", "1 0 0"}, {"b", "0 1 0"}})), 0.0, 1e-6);
    Value across = read(set, graph, node("Cross", {{"a", "1 0 0"}, {"b", "0 1 0"}}), "result", PinType::Vec3);
    CHECK_NEAR(across.z, 1.0, 1e-6);
    Value unit = read(set, graph, node("Normalize", {{"vector", "0 0 -8"}}), "result", PinType::Vec3);
    CHECK_NEAR(unit.z, -1.0, 1e-6);
    Value zero = read(set, graph, node("Normalize", {{"vector", "0 0 0"}}), "result", PinType::Vec3);      // no direction: no nonsense
    CHECK(zero.x == 0 && zero.y == 0 && zero.z == 0);
    Value way = read(set, graph, node("SubtractVectors", {{"a", "5 1 0"}, {"b", "2 1 0"}}), "result", PinType::Vec3);
    CHECK_NEAR(way.x, 3.0, 1e-6);

    CHECK(read(set, graph, node("JoinText", {{"a", "Lap "}, {"b", "3"}}), "text", PinType::String).asString() == "Lap 3");
    CHECK(read(set, graph, node("NumberToText", {{"number", "3.14159"}, {"decimals", "2"}}), "text", PinType::String).asString() == "3.14");
    CHECK(read(set, graph, node("NumberToText", {{"number", "41.7"}, {"decimals", "0"}}), "text", PinType::String).asString() == "42");
    CHECK(read(set, graph, node("TextEquals", {{"a", "red"}, {"b", "red"}}), "result", PinType::Bool).asBool());

    // Set Text and Get Text go through the world's shared texts, which the interface shows.
    Graph words;
    int say = onUpdate(words, "SetText");
    words.find(say)->values["name"] = "message";
    words.find(say)->values["text"] = "Game over";
    set.run(words);
    CHECK(set.world.text("message") == "Game over");
}

TEST(a_script_turns_moves_and_looks_for_things) {
    Set set;
    // Move Toward stops at the point instead of going past it.
    Graph graph;
    int move = onUpdate(graph, "MoveToward");
    graph.find(move)->values["point"] = "3 0 0";
    graph.find(move)->values["speed"] = "2";
    set.run(graph, 1.0f);
    CHECK_NEAR(set.at(set.actor).x, 2.0, 1e-5);
    set.run(graph, 1.0f);
    CHECK_NEAR(set.at(set.actor).x, 3.0, 1e-5);
    set.run(graph, 1.0f);
    CHECK_NEAR(set.at(set.actor).x, 3.0, 1e-5);

    // Look At turns the entity so that its forward points at the point.
    Graph looks;
    int look = onUpdate(looks, "LookAt");
    looks.find(look)->values["point"] = "3 5 -10";
    set.run(looks);
    Vec3 forward = set.world.scene.get<Transform>(set.actor)->forward();
    CHECK_NEAR(forward.z, -1.0, 1e-4);                          // upright: it only turns round, the height is ignored
    CHECK_NEAR(forward.y, 0.0, 1e-4);

    // A ray finds the wall in front, and says how far; the script's own collider is not in its way.
    Entity wall = set.thing("Wall", Vec3{3, 0, -6});
    set.world.scene.add(wall, Collider::box(Vec3{4, 4, 0.5f}));
    set.world.scene.add(set.actor, Collider::sphere(0.5f));
    set.world.physics.step(set.world.scene, 1.0f / 60.0f);      // the colliders are known to physics after a step
    Graph ray;
    int cast = ray.addNode("Raycast", 0, 0).id;
    ray.find(cast)->values["from"] = "3 0 0";
    ray.find(cast)->values["direction"] = "0 0 -1";
    CHECK(read(set, ray, cast, "hit", PinType::Bool).asBool());
    CHECK_NEAR(read(set, ray, cast, "distance", PinType::Float).asFloat(), 5.5, 1e-3);
    ray.find(cast)->values["direction"] = "0 0 1";
    CHECK(!read(set, ray, cast, "hit", PinType::Bool).asBool());

    // Distance To another entity, by name.
    Graph far;
    int distance = far.addNode("DistanceTo", 0, 0).id;
    far.find(distance)->values["to"] = "Wall";
    CHECK_NEAR(read(set, far, distance, "distance", PinType::Float).asFloat(), 6.0, 1e-4);
}

TEST(a_script_hears_values_change_and_hurts_what_has_health) {
    Set set;
    Graph graph;
    int changed = graph.addNode("OnValueChanged", 0, 0).id;
    graph.find(changed)->values["value"] = "score";
    int count = graph.addNode("AddToVariable", 0, 0).id;
    graph.find(count)->values["name"] = "heard";
    graph.find(count)->values["amount"] = "1";
    graph.connect(changed, "then", count, "in");
    set.world.setValue("score", 5);
    set.run(graph);                                             // the first frame only notes what it is
    set.run(graph);
    CHECK_NEAR(set.state.variables["heard"].asFloat(), 0.0, 1e-6);
    set.world.addValue("score", 1);
    set.run(graph);
    set.run(graph);
    CHECK_NEAR(set.state.variables["heard"].asFloat(), 1.0, 1e-6);

    // Damage takes health from what has a Health; Health reads it.
    Entity enemy = set.thing("Enemy", Vec3{0, 0, 0}, "Enemy");
    kit::Health health;
    health.amount = 3;
    set.world.scene.add(enemy, health);
    Graph hurts;
    int damage = onUpdate(hurts, "Damage");
    hurts.find(damage)->values["amount"] = "2";
    hurts.find(damage)->values["on"] = "Enemy";
    set.run(hurts);
    CHECK_NEAR(set.world.scene.get<kit::Health>(enemy)->amount, 1.0, 1e-6);
    Graph asks;
    int left = asks.addNode("GetHealth", 0, 0).id;
    asks.find(left)->values["on"] = "Enemy";
    CHECK_NEAR(read(set, asks, left, "health", PinType::Float).asFloat(), 1.0, 1e-6);

    // Slow motion, and back.
    Graph slow;
    int speed = onUpdate(slow, "SetTimeScale");
    slow.find(speed)->values["speed"] = "0.25";
    set.run(slow);
    CHECK_NEAR(set.world.time.timeScale, 0.25, 1e-6);
}
