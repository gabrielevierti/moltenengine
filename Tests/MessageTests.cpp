// Messages between scripts, and things done later: in seconds, when something holds, as coroutines.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Game/Component.hpp"
#include "Kit/Kit.hpp"
#include "Script/Runtime.hpp"

#include <filesystem>

using namespace molten;

namespace {
/// A component that counts the messages it hears.
struct Ear {
    int heard = 0;
    float last = 0;
    std::string from;
    void onMessage(World& world, Entity self, const Message& message) {
        heard++;
        last = message.number;
        const Name* name = world.scene.get<Name>(message.from);
        from = name ? name->value : "";
        if (message.name == "echo") world.send("echo", self, message.number + 1);       // forever: stopped by the engine
    }
};
}  // namespace

MOLTEN_COMPONENT(Ear)

TEST(messages_reach_who_listens) {
    World world;
    Entity a = world.scene.create("A"), b = world.scene.create("B"), sender = world.scene.create("Sender");
    for (Entity e : {a, b, sender}) world.scene.add(e, Transform{});
    world.scene.add(a, Ear{});
    world.scene.add(b, Ear{});
    int heardByCode = 0;
    float total = 0;
    int id = world.listen("doorOpened", [&](const Message& message) {
        heardByCode++;
        total += message.number;
    });
    int any = 0;
    world.listen("*", [&](const Message&) { any++; }, sender);
    // To everyone.
    world.send("doorOpened", Entity{}, 3, "", sender);
    CHECK(world.scene.get<Ear>(a)->heard == 1 && world.scene.get<Ear>(b)->heard == 1);
    CHECK(world.scene.get<Ear>(a)->from == "Sender" && world.scene.get<Ear>(a)->last == 3);
    CHECK(heardByCode == 1 && total == 3 && any == 1);
    // To one: the other does not hear it; code that listens does.
    world.send("doorOpened", b, 4);
    CHECK(world.scene.get<Ear>(a)->heard == 1 && world.scene.get<Ear>(b)->heard == 2 && heardByCode == 2);
    world.send("somethingElse");
    CHECK(heardByCode == 2 && any == 3);
    world.stopListening(id);
    world.send("doorOpened");
    CHECK(heardByCode == 2);
    // A listener whose owner is gone hears nothing.
    world.scene.destroy(sender);
    world.scene.flushDestroyed();
    world.send("somethingElse");
    CHECK(any == 4);                                        // the four before, not this one
    // A message that sends itself again and again is stopped.
    int before = world.scene.get<Ear>(a)->heard;
    world.send("echo", a);
    CHECK(world.scene.get<Ear>(a)->heard - before == 32);
    // While the editor edits, components do not hear.
    World editing;
    editing.runsComponents = false;
    Entity c = editing.scene.create("C");
    editing.scene.add(c, Transform{});
    editing.scene.add(c, Ear{});
    editing.send("doorOpened");
    CHECK(editing.scene.get<Ear>(c)->heard == 0);
}

namespace {
Task countdown(World& world, std::vector<std::string>& log) {
    log.push_back("three");
    co_await world.seconds(1);
    log.push_back("two");
    co_await world.seconds(1);
    log.push_back("one");
    co_await world.until([&world] { return world.value("go") > 0; });
    log.push_back("go");
}
}  // namespace

TEST(things_happen_later) {
    World world;
    auto play = [&](double seconds) {
        for (int i = 0; i < int(seconds * 60 + 0.5); i++) world.advance(1.0 / 60.0);
    };
    int once = 0, again = 0, whenDone = 0;
    world.after(0.5, [&] { once++; });
    int ticking = world.every(0.25, [&] { again++; });
    world.when([&] { return world.value("coins") >= 3; }, [&] { whenDone++; });
    play(0.4);
    CHECK(once == 0 && again == 1);
    play(0.2);
    CHECK(once == 1 && again == 2);
    world.cancel(ticking);
    play(1);
    CHECK(once == 1 && again == 2 && whenDone == 0);
    world.setValue("coins", 3);
    play(0.1);
    play(0.1);
    CHECK(whenDone == 1);
    // In game time: paused, nothing happens.
    int paused = 0;
    world.after(0.1, [&] { paused++; });
    world.time.timeScale = 0;
    play(1);
    CHECK(paused == 0);
    world.time.timeScale = 1;
    play(0.2);
    CHECK(paused == 1);
    // A task whose owner is destroyed does not happen.
    Entity owner = world.scene.create("Owner");
    world.scene.add(owner, Transform{});
    int owned = 0;
    world.after(0.2, [&] { owned++; }, owner);
    world.scene.destroy(owner);
    play(0.5);
    CHECK(owned == 0);

    // A coroutine: one function that waits.
    std::vector<std::string> log;
    world.start(countdown(world, log));
    CHECK(log.size() == 1);                                 // it ran until its first wait
    play(1.05);
    CHECK(log.size() == 2 && log[1] == "two");
    play(1.0);
    CHECK(log.size() == 3);
    play(1.0);
    CHECK(log.size() == 3);                                  // waiting for "go"
    world.setValue("go", 1);
    play(0.1);
    CHECK(log.size() == 4 && log[3] == "go");
    // Its owner gone, it stops where it waits.
    Entity speaker = world.scene.create("Speaker");
    world.scene.add(speaker, Transform{});
    std::vector<std::string> cut;
    world.setValue("go", 0);
    world.start(countdown(world, cut), speaker);
    world.scene.destroy(speaker);
    play(3);
    CHECK(cut.size() == 1);
    // A new scene: nothing waits any more.
    world.after(1, [&] { once = 100; });
    world.loadSceneText("Empty", "molten-scene 1\n");
    play(2);
    CHECK(once == 1);
}

TEST(scripts_send_hear_and_wait) {
    using namespace molten::script;
    registerStandardNodes();
    kit::registerKit();
    std::string root = "/tmp/molten-test-messages";
    std::filesystem::create_directories(root);
    // The sender: on start, waits half a second, then tells "Door" it opened, with 7.
    Graph sender;
    int start = sender.addNode("OnStart", 0, 0).id;
    Node& wait = sender.addNode("Wait", 100, 0);
    wait.values["seconds"] = "0.5";
    int waitId = wait.id;
    Node& send = sender.addNode("SendMessage", 200, 0);
    send.values["message"] = "opened";
    send.values["to"] = "Door";
    send.values["number"] = "7";
    int sendId = send.id;
    sender.connect(start, "then", waitId, "in");
    sender.connect(waitId, "then", sendId, "in");
    text::writeFile(root + "/Sender.graph", sender.toText());
    // The door: on the message, adds its number to "opened".
    Graph door;
    Node& hear = door.addNode("OnMessage", 0, 0);
    hear.values["message"] = "opened";
    int hearId = hear.id;
    Node& add = door.addNode("AddValue", 200, 0);
    add.values["name"] = "opened";
    int addId = add.id;
    door.connect(hearId, "then", addId, "in");
    door.connect(hearId, "number", addId, "amount");
    text::writeFile(root + "/Door.graph", door.toText());

    World world;
    world.assetRoot = root;
    Entity a = world.scene.create("Button"), b = world.scene.create("Door");
    world.scene.add(a, Transform{});
    world.scene.add(b, Transform{});
    world.scene.add(a, VisualScript{"Sender.graph"});
    world.scene.add(b, VisualScript{"Door.graph"});
    for (int i = 0; i < 20; i++) world.advance(1.0 / 60.0);
    CHECK(world.value("opened") == 0);                      // still waiting
    for (int i = 0; i < 25; i++) world.advance(1.0 / 60.0);
    CHECK(world.value("opened") == 7);                      // sent once, heard once
    for (int i = 0; i < 60; i++) world.advance(1.0 / 60.0);
    CHECK(world.value("opened") == 7);
}
