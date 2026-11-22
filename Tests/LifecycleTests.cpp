// A component's life: start once, update every frame, the last word when its entity goes.
// And the frame itself: there is one (World::simulateFrame), which an Engine runs too.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Game/Component.hpp"

using namespace molten;

namespace {

struct LifeLog {
    std::vector<std::string> lines;
    static LifeLog& instance() {
        static LifeLog log;
        return log;
    }
};

/// Says what happens to it, and proves that its entity is still whole when it hears onDestroy.
struct TestLife {
    std::string label = "life";
    bool destroysFriend = false;

    void start(World&, Entity) { LifeLog::instance().lines.push_back("start " + label); }
    void update(World&, Entity, float) { LifeLog::instance().lines.push_back("update " + label); }
    void lateUpdate(World&, Entity, float) { LifeLog::instance().lines.push_back("late " + label); }
    void onDestroy(World& world, Entity self) {
        const Transform* transform = world.scene.get<Transform>(self);
        LifeLog::instance().lines.push_back("destroy " + label + (transform ? " at " + std::to_string(int(transform->position.x)) : " with nothing left"));
        if (destroysFriend) world.scene.destroy(world.find("Friend"));
    }
};

Entity living(World& world, const char* name, float x, Entity parent = Entity{}) {
    Entity entity = world.scene.create(name);
    Transform transform;
    transform.position = Vec3{x, 0, 0};
    transform.parent = parent;
    world.scene.add(entity, transform);
    TestLife life;
    life.label = name;
    world.scene.add(entity, life);
    return entity;
}

size_t count(const std::string& line) {
    size_t found = 0;
    for (const std::string& said : LifeLog::instance().lines) found += said == line;
    return found;
}

}  // namespace

MOLTEN_COMPONENT(TestLife, MOLTEN_FIELD(label), MOLTEN_FIELD(destroysFriend))

TEST(a_component_starts_once_updates_then_has_the_last_word) {
    LifeLog::instance().lines.clear();
    World world;
    CHECK(ComponentRegistry::instance().find("TestLife")->onDestroy != nullptr);
    Entity parent = living(world, "Parent", 3);
    living(world, "Child", 5, parent);
    Entity other = living(world, "Other", 7);
    world.advance(1.0 / 60.0);
    world.advance(1.0 / 60.0);
    CHECK(count("start Parent") == 1 && count("update Parent") == 2 && count("late Parent") == 2);
    // The order within a frame: every update before any lateUpdate.
    std::vector<std::string>& lines = LifeLog::instance().lines;
    auto firstLate = std::find(lines.begin(), lines.end(), "late Parent");
    auto lastUpdateBefore = std::find(lines.begin(), lines.end(), "update Other");
    CHECK(lastUpdateBefore < firstLate);

    // Destroyed: it hears so before it is gone, with its Transform still there; what is under it goes with it and hears too.
    world.scene.destroy(parent);
    world.advance(1.0 / 60.0);
    CHECK(count("destroy Parent at 3") == 1 && count("destroy Child at 5") == 1 && count("destroy Other at 7") == 0);
    CHECK(!world.scene.isAlive(parent) && world.scene.isAlive(other));
    world.advance(1.0 / 60.0);
    CHECK(count("destroy Parent at 3") == 1);                   // once

    // What onDestroy itself destroys hears it in the same frame, and is gone with the rest.
    Entity friendly = living(world, "Friend", 9);
    world.advance(1.0 / 60.0);
    world.scene.get<TestLife>(other)->destroysFriend = true;
    world.scene.destroy(other);
    world.advance(1.0 / 60.0);
    CHECK(count("destroy Other at 7") == 1 && count("destroy Friend at 9") == 1);
    CHECK(!world.scene.isAlive(friendly));
}

TEST(a_scene_that_leaves_tells_its_components_and_one_that_never_started_hears_nothing) {
    LifeLog::instance().lines.clear();
    World world;
    living(world, "Stays", 1);
    world.advance(1.0 / 60.0);
    // Made after the last frame: it has not started, so it has nothing to undo.
    living(world, "Late", 2);
    CHECK(world.loadSceneText("Empty", "molten-scene 1\n"));
    CHECK(count("destroy Stays at 1") == 1 && count("destroy Late at 2") == 0);
    CHECK(world.scene.find("Stays") == Entity{});

    // While components do not run (an editor that is not playing), nothing is told.
    LifeLog::instance().lines.clear();
    World editing;
    editing.runsComponents = false;
    Entity placed = living(editing, "Placed", 4);
    editing.advance(1.0 / 60.0);
    editing.scene.destroy(placed);
    editing.advance(1.0 / 60.0);
    CHECK(LifeLog::instance().lines.empty() && !editing.scene.isAlive(placed));
}

TEST(what_was_waiting_runs_in_the_frame_and_ends_with_the_scene) {
    World world;
    int rang = 0, ticks = 0;
    world.after(0.05, [&] { rang++; });
    world.every(0.02, [&] { ticks++; });
    for (int i = 0; i < 6; i++) world.advance(1.0 / 60.0);     // a tenth of a second
    CHECK(rang == 1 && ticks >= 4);
    // The scene leaves: what was waiting in it does not fire in the next one.
    world.after(0.01, [&] { rang += 100; });
    world.endScene();
    for (int i = 0; i < 6; i++) world.advance(1.0 / 60.0);
    CHECK(rang == 1);
}
