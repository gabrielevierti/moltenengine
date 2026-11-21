// Scenes loaded in the background, and scenes added to the one open.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"

#include <filesystem>
#include <thread>

using namespace molten;

namespace {
/// A World that counts the models handed to it already read.
struct Loader : World {
    std::vector<std::string> preloaded;
    void preloadMesh(const std::string& name, MeshData) override { preloaded.push_back(name); }
};

std::string makeProject() {
    std::string root = "/tmp/molten-test-loading";
    std::filesystem::create_directories(root + "/Scenes");
    std::filesystem::create_directories(root + "/Models");
    text::writeFile(root + "/Models/box.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    text::writeFile(root + "/Scenes/Level.scene", R"(molten-scene 1
entity "Rock"
  transform 0 0 0 0 0 0 1 1 1 1
  mesh "Models/box.obj" -1 1 1 1 1 1 1
entity "Another Rock"
  transform 2 0 0 0 0 0 1 1 1 1
  mesh "Models/box.obj" -1 1 1 1 1 1 1
entity "Ball"
  transform 4 0 0 0 0 0 1 1 1 1
  mesh "sphere" -1 1 1 1 1 1 1
)");
    text::writeFile(root + "/Scenes/Menu.scene", R"(molten-scene 1
entity "Loading"
  transform 0 0 0 0 0 0 1 1 1 1
  component "LoadingScreen"
entity "Spinner"
  transform 0 0 0 0 0 0 1 1 1 1
  parent 0
)");
    text::writeFile(root + "/Scenes/Room.scene", R"(molten-scene 1
entity "Table"
  transform 1 0 0 0 0 0 1 1 1 1
entity "Lamp"
  transform 0 1 0 0 0 0 1 1 1 1
  parent 0
)");
    return root;
}
}  // namespace

TEST(a_scene_loads_in_the_background) {
    Loader world;
    world.assetRoot = makeProject();
    kit::registerKit();
    CHECK(world.loadSceneNow("Scenes/Menu.scene"));
    world.advance(1.0 / 60.0);
    CHECK(!world.scene.get<Transform>(world.find("Spinner"))->isActive);       // nothing loading: hidden
    world.loadSceneInBackground("Scenes/Level.scene");
    CHECK(world.isLoadingScene() && world.value("isLoading") == 1);
    world.advance(1.0 / 60.0);
    // The game goes on meanwhile: the loading screen shows.
    bool sawSpinner = world.scene.isAlive(world.find("Spinner")) && world.scene.get<Transform>(world.find("Spinner"))->isActive;
    for (int i = 0; i < 200 && world.isLoadingScene(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        world.advance(1.0 / 60.0);
    }
    CHECK(sawSpinner);
    CHECK(!world.isLoadingScene() && world.sceneName() == "Scenes/Level.scene");
    CHECK(world.find("Rock").isValid() && !world.find("Spinner").isValid());
    CHECK(world.value("loading") == 1 && world.value("isLoading") == 0);
    // Its model was read there, once, and handed over; the sphere is the engine's own.
    CHECK(world.preloaded.size() == 1 && world.preloaded[0] == "Models/box.obj");
}

TEST(a_scene_is_added_and_taken_away) {
    World world;
    world.assetRoot = makeProject();
    kit::registerKit();
    CHECK(world.loadSceneNow("Scenes/Level.scene"));
    Entity room = world.addScene("Scenes/Room.scene");
    CHECK(room.isValid() && world.find("Table").isValid() && world.find("Rock").isValid());
    const Transform* table = world.scene.get<Transform>(world.find("Table"));
    CHECK(table->parent == room);
    const Transform* lamp = world.scene.get<Transform>(world.find("Lamp"));
    CHECK(lamp->parent == world.find("Table"));                                  // its own parents kept
    CHECK(std::abs(lamp->worldPosition().x - 1) < 1e-5f && std::abs(lamp->worldPosition().y - 1) < 1e-5f);
    world.removeScene("Scenes/Room.scene");
    world.scene.flushDestroyed();
    CHECK(!world.find("Table").isValid() && !world.find("Lamp").isValid() && world.find("Rock").isValid());
    // Added in the background too.
    world.loadSceneInBackground("Scenes/Room.scene", true);
    for (int i = 0; i < 200 && world.isLoadingScene(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        world.advance(1.0 / 60.0);
    }
    CHECK(world.find("Table").isValid() && world.find("Rock").isValid());
}
