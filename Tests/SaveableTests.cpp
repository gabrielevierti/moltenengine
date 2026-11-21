// Saving a game in progress by itself: what has a Saveable, the shared values, the scene.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Navigation.hpp"
#include "Kit/Saveable.hpp"

#include <cmath>
#include <filesystem>

using namespace molten;
using namespace molten::kit;

TEST(the_state_of_a_game_is_kept_and_put_back) {
    std::string root = "/tmp/molten-test-saveable";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root + "/Scenes");
    text::writeFile(root + "/Scenes/Level.scene", R"(molten-scene 1
entity "Chest"
  transform 1 0 1 0 0 0 1 1 1 1
  component "Saveable"
  component "NavAgent"
    field speed "2"
entity "Coin"
  transform 5 0 0 0 0 0 1 1 1 1
  component "Saveable"
entity "Coin"
  transform 6 0 0 0 0 0 1 1 1 1
  component "Saveable"
entity "Lamp"
  transform 0 3 0 0 0 0 1 1 1 1
  component "Saveable"
    field keepsPlace "0"
entity "Scenery"
  transform 9 9 9 0 0 0 1 1 1 1
)");
    kit::registerKit();
    World world;
    world.assetRoot = root;
    world.preferencesFile = root + "/Preferences/preferences.txt";
    CHECK(world.loadSceneNow("Scenes/Level.scene"));
    // Playing: the chest moved, its agent slowed, a coin picked up, the lamp switched off.
    Entity chest = world.find("Chest");
    world.scene.get<Transform>(chest)->position = Vec3{3, 0, 4};
    world.scene.get<NavAgent>(chest)->speed = 7;
    std::vector<Entity> coins;
    world.scene.each<Name>([&](Entity e, Name& n) {
        if (n.value == "Coin") coins.push_back(e);
    });
    CHECK(coins.size() == 2);
    world.scene.destroy(coins[0]);
    world.scene.flushDestroyed();
    Entity lamp = world.find("Lamp");
    world.scene.get<Transform>(lamp)->isActive = false;
    world.scene.get<Transform>(lamp)->position = Vec3{0, 9, 0};
    world.setValue("coins", 1);
    world.setText("playerName", "Gabriele");
    world.scene.get<Transform>(world.find("Scenery"))->position = Vec3{0, 0, 0};
    CHECK(saveState(world, "Slot 1"));

    // Later things changed again: loading puts back what was saved.
    world.setValue("coins", 50);
    world.scene.get<Transform>(chest)->position = Vec3{-10, 0, 0};
    CHECK(loadState(world, "Slot 1"));
    world.advance(1.0 / 60.0);                              // the scene is loaded at the end of the frame
    Entity chestAgain = world.find("Chest");
    CHECK(simd::length(world.scene.get<Transform>(chestAgain)->position - Vec3{3, 0, 4}) < 1e-4f);
    CHECK(world.scene.get<NavAgent>(chestAgain)->speed == 7);
    int coinsLeft = 0;
    world.scene.each<Name>([&](Entity, Name& n) { coinsLeft += n.value == "Coin"; });
    CHECK(coinsLeft == 1);                                  // the one picked up stays picked up
    Entity lampAgain = world.find("Lamp");
    CHECK(!world.scene.get<Transform>(lampAgain)->isActive);
    CHECK(std::abs(world.scene.get<Transform>(lampAgain)->position.y - 3) < 1e-4f);     // its place is not kept
    CHECK(world.value("coins") == 1 && world.text("playerName") == "Gabriele");
    CHECK(simd::length(world.scene.get<Transform>(world.find("Scenery"))->position - Vec3{9, 9, 9}) < 1e-4f);   // not saveable: as the scene has it
    // A slot that is not there: nothing happens.
    CHECK(!loadState(world, "Nothing"));
}
