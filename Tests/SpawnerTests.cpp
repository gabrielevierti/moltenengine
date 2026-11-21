// Pools, spawners and lifetimes: things that come and go, reused.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Game/Component.hpp"
#include "Kit/Kit.hpp"

#include <filesystem>

using namespace molten;
using namespace molten::kit;

namespace {
/// Counts how often it starts: a reused one starts again.
struct Starts {
    int count = 0;
    void start(World&, Entity) { count++; }
};
}  // namespace

MOLTEN_COMPONENT(Starts)

namespace {
std::string makeProject() {
    std::string root = "/tmp/molten-test-spawner";
    std::filesystem::create_directories(root + "/Prefabs");
    text::writeFile(root + "/Prefabs/Spark.prefab", R"(molten-scene 1
entity "Spark"
  transform 0 0 0 0 0 0 1 1 1 1
  component "Starts"
  component "Lifetime"
    field seconds "0.5"
entity "Glow"
  transform 0 1 0 0 0 0 1 1 1 1
  parent 0
)");
    return root;
}
void play(World& world, double seconds) {
    for (int i = 0; i < int(seconds * 60 + 0.5); i++) world.advance(1.0 / 60.0);
}
int countNamed(World& world, const char* name, bool activeOnly) {
    int count = 0;
    world.scene.each<Name>([&](Entity entity, Name& n) {
        const Transform* t = world.scene.get<Transform>(entity);
        if (n.value == name && (!activeOnly || (t && t->isActive))) count++;
    });
    return count;
}
}  // namespace

TEST(pools_reuse_what_is_put_back) {
    World world;
    world.assetRoot = makeProject();
    kit::registerKit();
    Entity first = world.take("Prefabs/Spark.prefab", Vec3{1, 0, 0});
    CHECK(first.isValid() && world.waitingInPool("Prefabs/Spark.prefab") == 0);
    play(world, 0.1);
    CHECK(world.scene.get<Starts>(first)->count == 1);
    world.scene.get<Transform>(first)->rotation = Quat(1.0f, Vec3{0, 1, 0});
    world.release(first);
    CHECK(!world.scene.get<Transform>(first)->isActive && world.waitingInPool("Prefabs/Spark.prefab") == 1);
    // Taken again: the same one, elsewhere, standing as the prefab does, started again.
    Entity again = world.take("Prefabs/Spark.prefab", Vec3{5, 0, 0});
    CHECK(again == first && world.scene.get<Transform>(again)->isActive);
    CHECK(simd::length(world.scene.get<Transform>(again)->position - Vec3{5, 0, 0}) < 1e-5f);
    CHECK(std::abs(world.scene.get<Transform>(again)->rotation.vector.w - 1) < 1e-5f);
    play(world, 0.1);
    CHECK(world.scene.get<Starts>(again)->count == 2);
    // Its Lifetime puts it back by itself, and the next take reuses it.
    play(world, 0.6);
    CHECK(world.waitingInPool("Prefabs/Spark.prefab") == 1);
    CHECK(countNamed(world, "Spark", false) == 1);
    // What did not come from a pool is destroyed.
    Entity plain = world.scene.create("Plain");
    world.scene.add(plain, Transform{});
    world.release(plain);
    world.scene.flushDestroyed();
    CHECK(!world.scene.isAlive(plain));
}

TEST(spawners_make_waves) {
    World world;
    world.assetRoot = makeProject();
    kit::registerKit();
    Entity gate = world.scene.create("Gate");
    world.scene.add(gate, Transform{});
    Spawner spawner;
    spawner.prefab = "Prefabs/Spark.prefab";
    spawner.interval = 0.1f;
    spawner.firstDelay = 0;
    spawner.count = 3;
    spawner.waves = 2;
    spawner.waveSeconds = 1.0f;
    spawner.aliveValue = "sparks";
    world.scene.add(gate, spawner);
    int waves = 0, done = 0;
    world.listen("waveDone", [&](const Message&) { waves++; });
    world.listen("wavesDone", [&](const Message&) { done++; });
    play(world, 0.25);
    CHECK(world.value("sparks") == 3 && waves == 1);
    play(world, 0.5);                                       // their Lifetime: they went, back to the pool
    CHECK(world.value("sparks") == 0);
    play(world, 0.8);                                       // a second's pause, then the second wave reuses them
    CHECK(waves == 2);
    CHECK(countNamed(world, "Spark", false) == 3);          // three made in all
    play(world, 1.0);
    CHECK(done == 1);
    play(world, 1.0);
    CHECK(done == 1 && waves == 2);
}
