// Scenes loaded in the background, and scenes added to the one open.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"

#include "Scene/SceneAhead.hpp"

#include <stb_image_write.h>

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

TEST(what_a_scene_needs_is_read_ahead_on_other_threads) {
    std::string root = "/tmp/molten-test-ahead";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root + "/Scenes");
    std::filesystem::create_directories(root + "/Models");
    std::filesystem::create_directories(root + "/Materials");
    std::filesystem::create_directories(root + "/Textures");
    text::writeFile(root + "/Models/box.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    text::writeFile(root + "/Models/wedge.obj", "v 0 0 0\nv 2 0 0\nv 0 2 0\nv 0 0 2\nf 1 2 3\nf 1 3 4\n");
    // Two pictures, three pixels by two and one by one.
    const unsigned char wall[3 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 9, 9, 9, 255, 8, 8, 8, 255, 7, 7, 7, 255};
    const unsigned char dot[4] = {10, 20, 30, 255};
    CHECK(stbi_write_png((root + "/Textures/wall.png").c_str(), 3, 2, 4, wall, 12) != 0);
    CHECK(stbi_write_png((root + "/Textures/dot.png").c_str(), 1, 1, 4, dot, 4) != 0);
    text::writeFile(root + "/Materials/Wall.material", "molten-material 1\nshader lit\nbaseColor 1 1 1 1\nbaseColorTexture \"Textures/wall.png\"\nnormalTexture \"Textures/missing.png\"\n");
    std::string scene = R"(molten-scene 1
entity "Rock"
  transform 0 0 0 0 0 0 1 1 1 1
  mesh "Models/box.obj" -1 1 1 1 1 1 1
  materialAsset "Materials/Wall.material"
entity "Another Rock"
  transform 2 0 0 0 0 0 1 1 1 1
  mesh "Models/box.obj" -1 1 1 1 1 1 1
  materialAsset "Materials/Wall.material"
entity "Wedge"
  transform 0 0 0 0 0 0 1 1 1 1
  mesh "Models/wedge.obj" -1 1 1 1 1 1 1
  material
    emissiveTexture "Textures/dot.png"
entity "Ball"
  mesh "sphere" -1 1 1 1 1 1 1
entity "Ghost"
  mesh "Models/nothing.obj" -1 1 1 1 1 1 1
)";
    text::writeFile(root + "/Scenes/Level.scene", scene);
    auto pathOf = [root](const std::string& name) { return root + "/" + name; };

    // What it names: each file once, the engine's own shapes left out.
    SceneNeeds needs = sceneNeeds(scene, [&](const std::string& name, std::string& out) {
        std::optional<std::string> read = text::readFile(pathOf(name));
        if (read) out = *read;
        return read.has_value();
    });
    CHECK(needs.models.size() == 3 && needs.models[0] == "Models/box.obj" && needs.models[1] == "Models/wedge.obj");
    CHECK(needs.materials.size() == 1 && needs.materials[0] == "Materials/Wall.material");
    CHECK(needs.pictures.size() == 3);         // dot.png in the scene's own material, wall.png and missing.png in the file's

    for (int threads : {1, 4}) {
        SceneAheadProgress progress;
        SceneAhead ahead = readSceneAhead("Scenes/Level.scene", pathOf("Scenes/Level.scene"), pathOf, &progress, threads);
        CHECK(ahead.isRead && ahead.text == scene);
        // What could not be read is simply not there: the scene says so itself when it is opened.
        CHECK(ahead.models.size() == 2);
        CHECK(ahead.pictures.size() == 2);
        bool sawWall = false, sawWedge = false;
        for (const SceneAhead::Picture& picture : ahead.pictures) {
            if (picture.name != "Textures/wall.png") continue;
            sawWall = picture.width == 3 && picture.height == 2 && picture.pixels.size() == 24 && picture.pixels[0] == 255 && picture.pixels[5] == 255 &&
                      picture.pixels[20] == 7;
        }
        for (const SceneAhead::Model& model : ahead.models) {
            if (model.name == "Models/wedge.obj") sawWedge = model.data.indices.size() == 6;
        }
        CHECK(sawWall && sawWedge);
        CHECK(progress.done.load() == progress.total.load() && progress.total.load() == 7);     // the scene, three models, three pictures
        CHECK(progress.fraction() == 1.0f);
    }
    // A scene that is not there is said to be so, and the bar is not left waiting.
    SceneAheadProgress progress;
    SceneAhead none = readSceneAhead("Scenes/None.scene", pathOf("Scenes/None.scene"), pathOf, &progress);
    CHECK(!none.isRead && progress.fraction() == 1.0f);
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
