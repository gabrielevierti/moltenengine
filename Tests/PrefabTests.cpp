// Prefabs by reference (a copy that follows its prefab) and prefab variants.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Navigation.hpp"
#include "Scene/SceneFile.hpp"

#include <filesystem>

using namespace molten;

namespace {

const char* kWheel = R"(molten-scene 1
entity "Wheel"
  transform 0 0 0 0 0 0 1 1 1 1
entity "Rim"
  transform 0 0.1 0 0 0 0 1 1 1 1
  parent 0
)";

const char* kCart = R"(molten-scene 1
entity "Cart"
  transform 0 0 0 0 0 0 1 1 1 1
  component "NavAgent"
    field speed "2"
entity "Left Wheel"
  transform -1 0 0 0 0 0 1 1 1 1
  parent 0
  prefab "Prefabs/Wheel.prefab"
entity "Right Wheel"
  transform 1 0 0 0 0 0 1 1 1 1
  parent 0
  prefab "Prefabs/Wheel.prefab"
)";

std::string folder() {
    std::string root = "/tmp/molten-test-prefabs";
    std::filesystem::create_directories(root + "/Prefabs");
    text::writeFile(root + "/Prefabs/Wheel.prefab", kWheel);
    text::writeFile(root + "/Prefabs/Cart.prefab", kCart);
    return root;
}

int countNamed(Scene& scene, const std::string& name) {
    int count = 0;
    scene.each<Name>([&](Entity, Name& named) { count += named.value == name; });
    return count;
}

}  // namespace

TEST(a_prefab_inside_a_prefab_is_put_there_each_time_and_not_saved_twice) {
    kit::registerKit();
    World world;
    world.assetRoot = folder();
    Entity cart = world.spawn("Prefabs/Cart.prefab", Vec3{5, 0, 0});
    CHECK(cart.isValid() && countNamed(world.scene, "Rim") == 2 && countNamed(world.scene, "Wheel") == 2);
    // The wheels' parts are under their copies, where the copies are.
    world.scene.updateTransforms();
    Entity rim;
    world.scene.each<Name>([&](Entity entity, Name& named) {
        if (named.value == "Rim" && !rim.isValid()) rim = entity;
    });
    Vec3 at = world.scene.get<Transform>(rim)->worldPosition();
    CHECK(std::abs(std::abs(at.x - 5) - 1) < 1e-4f && std::abs(at.y - 0.1f) < 1e-4f);
    CHECK(world.scene.has<FromPrefab>(rim));
    // Written out, a copy is its own line, not its parts: the scene reads back the same.
    std::string written = writeScene(world.scene, {}, RenderSettings{});
    CHECK(written.find("prefab \"Prefabs/Wheel.prefab\"") != std::string::npos);
    CHECK(written.find("\"Rim\"") == std::string::npos);
    // And a change to the prefab shows in the copies the next time they are read.
    text::writeFile(world.assetRoot + "/Prefabs/Wheel.prefab", std::string(kWheel) + "entity \"Hubcap\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  parent 0\n");
    World again;
    again.assetRoot = world.assetRoot;
    again.loadSceneText("Test", written);
    CHECK(countNamed(again.scene, "Hubcap") == 2 && countNamed(again.scene, "Rim") == 2);
    text::writeFile(world.assetRoot + "/Prefabs/Wheel.prefab", kWheel);
}

TEST(a_prefab_variant_is_its_base_with_values_changed) {
    kit::registerKit();
    World world;
    world.assetRoot = folder();
    text::writeFile(world.assetRoot + "/Prefabs/Fast Cart.prefab", R"(molten-variant 1
base "Prefabs/Cart.prefab"
name "Fast Cart"
set "" NavAgent speed "7.5"
set "Left Wheel" Transform scale "2 2 2"
set "Left Wheel/Wheel/Rim" Transform position "0 0.5 0"
)");
    Entity cart = world.spawn("Prefabs/Fast Cart.prefab", Vec3{0, 0, 0});
    CHECK(cart.isValid());
    CHECK(world.scene.get<Name>(cart)->value == "Fast Cart");
    const kit::NavAgent* agent = world.scene.get<kit::NavAgent>(cart);
    CHECK(agent && agent->speed == 7.5f);
    CHECK(countNamed(world.scene, "Rim") == 2);
    bool scaled = false, moved = false;
    world.scene.each<Name>([&](Entity entity, Name& named) {
        if (named.value == "Left Wheel") scaled = world.scene.get<Transform>(entity)->scale.x == 2;
        if (named.value == "Rim" && world.scene.get<Transform>(entity)->position.y == 0.5f) moved = true;
    });
    CHECK(scaled && moved);
    CHECK(isPrefabVariant("molten-variant 1\nbase \"x\"\n") && !isPrefabVariant(kWheel));
}
