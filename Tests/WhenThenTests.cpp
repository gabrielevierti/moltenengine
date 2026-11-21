// When / Then: game logic set in the Inspector, without code.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/WhenThen.hpp"
#include "Physics/Physics.hpp"

using namespace molten;
using namespace molten::kit;

namespace {
Entity thing(World& world, const char* name, Vec3 at = {0, 0, 0}) {
    Entity entity = world.scene.create(name);
    Transform transform;
    transform.position = at;
    world.scene.add(entity, transform);
    return entity;
}
void play(World& world, double seconds) {
    for (int i = 0; i < int(seconds * 60 + 0.5); i++) world.advance(1.0 / 60.0);
}
}  // namespace

TEST(when_something_enters_then_things_happen) {
    World world;
    kit::registerKit();
    Entity door = thing(world, "Door");
    Entity zone = thing(world, "Zone", Vec3{0, 0, 0});
    Collider sensor;
    sensor.shape = ColliderShape::Box;
    sensor.halfExtents = Vec3{2, 2, 2};
    sensor.isTrigger = true;
    world.scene.add(zone, sensor);
    WhenThen rule;
    rule.when = "Something enters";
    rule.who = "Ball";
    rule.action1 = "Hide";
    rule.target1 = "Door";
    rule.action2 = "Add to value";
    rule.target2 = "score";
    rule.amount2 = 5;
    world.scene.add(zone, rule);
    // Something else falls through first: nothing happens.
    Entity stone = thing(world, "Stone", Vec3{0, 4, 0});
    Collider small;
    small.shape = ColliderShape::Sphere;
    small.radius = 0.3f;
    world.scene.add(stone, small);
    world.scene.add(stone, RigidBody{});
    play(world, 1.0);
    CHECK(world.scene.get<Transform>(door)->isActive && world.value("score") == 0);
    // The ball: the door goes, the score goes up, once.
    Entity ball = thing(world, "Ball", Vec3{0, 4, 0});
    world.scene.add(ball, small);
    world.scene.add(ball, RigidBody{});
    play(world, 1.0);
    CHECK(!world.scene.get<Transform>(door)->isActive);
    CHECK(world.value("score") == 5);
}

TEST(when_a_message_a_value_or_time_then_things_happen) {
    World world;
    kit::registerKit();
    Entity light = thing(world, "Light");
    world.scene.get<Transform>(light)->isActive = false;
    Entity switcher = thing(world, "Switcher");
    WhenThen onMessage;
    onMessage.when = "A message";
    onMessage.message = "power";
    onMessage.once = false;
    onMessage.action1 = "Switch";
    onMessage.target1 = "Light";
    world.scene.add(switcher, onMessage);
    play(world, 0.1);
    world.send("power");
    CHECK(world.scene.get<Transform>(light)->isActive);
    world.send("power");
    CHECK(!world.scene.get<Transform>(light)->isActive);          // again: not only once

    Entity winner = thing(world, "Winner");
    WhenThen onValue;
    onValue.when = "A value reaches";
    onValue.value = "coins";
    onValue.atLeast = 3;
    onValue.seconds = 0.5f;                                       // and then waits half a second
    onValue.action1 = "Send message";
    onValue.target1 = "won";
    onValue.action2 = "Set value";
    onValue.target2 = "level";
    onValue.amount2 = 2;
    world.scene.add(winner, onValue);
    int won = 0;
    world.listen("won", [&](const Message&) { won++; });
    world.setValue("coins", 2);
    play(world, 0.2);
    CHECK(won == 0);
    world.setValue("coins", 3);
    play(world, 0.3);
    CHECK(won == 0 && world.value("level") == 0);               // still waiting
    play(world, 0.4);
    CHECK(won == 1 && world.value("level") == 2);

    Entity clock = thing(world, "Clock");
    WhenThen ticking;
    ticking.when = "Every few seconds";
    ticking.seconds = 0.5f;
    ticking.once = false;
    ticking.action1 = "Add to value";
    ticking.target1 = "ticks";
    world.scene.add(clock, ticking);
    play(world, 2.05);
    CHECK(world.value("ticks") == 4);

    Entity starter = thing(world, "Starter");
    WhenThen atStart;
    atStart.when = "It starts";
    atStart.action1 = "Destroy";
    atStart.target1 = "self";
    world.scene.add(starter, atStart);
    play(world, 0.1);
    CHECK(!world.scene.isAlive(starter));
}
