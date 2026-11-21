// What uses what among a project's assets.
#include "Test.hpp"

#include "Assets/AssetDependencies.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

using namespace molten;

namespace {
bool has(const std::vector<std::string>& list, const std::string& item) { return std::find(list.begin(), list.end(), item) != list.end(); }
}  // namespace

TEST(assets_know_what_uses_them) {
    std::vector<std::string> files = {"Scenes/Main.scene", "Materials/Wood.material", "Textures/wood.png", "Textures/old.png", "Models/car.glb",
                                      "Models/wheel.png", "Audio/horn.wav", "Audio/unused.wav", "Scripts/Horn.cpp", "Prefabs/Crate.prefab",
                                      "Prefabs/Lost.prefab"};
    std::unordered_map<std::string, std::string> texts = {
        {"Scenes/Main.scene", "entity \"Car\"\n  mesh \"Models/car.glb#3\" -1 1 1 1 1 1 1\n  materialAsset \"Materials/Wood.material\"\nentity \"Box\"\n  prefab \"Prefabs/Crate.prefab\"\n"},
        {"Materials/Wood.material", "molten-material 1\nbaseMap Textures/wood.png\n"},
        {"Models/car.glb", std::string("glTF\x02\0\0\0", 8) + "{\"images\":[{\"uri\":\"wheel.png\"}]}"},
        {"Scripts/Horn.cpp", "world.playSound(\"Audio/horn.wav\");"},
        {"Prefabs/Crate.prefab", "entity \"Crate\"\n  materialAsset \"Materials/Wood.material\"\n"},
        {"Prefabs/Lost.prefab", "entity \"Lost\"\n  mesh \"Textures/old.png\"\n"}};
    AssetDependencies dependencies;
    dependencies.build(texts, files);
    CHECK(has(dependencies.uses("Scenes/Main.scene"), "Models/car.glb"));         // the model of one of its parts
    CHECK(has(dependencies.uses("Materials/Wood.material"), "Textures/wood.png")); // named without quotes
    CHECK(has(dependencies.uses("Models/car.glb"), "Models/wheel.png"));          // a picture beside the model
    CHECK(dependencies.usedBy("Materials/Wood.material").size() == 2);
    CHECK(has(dependencies.usedBy("Audio/horn.wav"), "Scripts/Horn.cpp"));
    // Nothing reaches the lost prefab, nor what only it uses, nor the sound nothing plays.
    std::vector<std::string> unused = dependencies.unused();
    CHECK(unused.size() == 3 && has(unused, "Prefabs/Lost.prefab") && has(unused, "Textures/old.png") && has(unused, "Audio/unused.wav"));
}

TEST(the_driving_project_uses_its_assets) {
    const char* projects = std::getenv("MOLTEN_PROJECTS");
    if (!projects) return;
    std::string root = std::string(projects) + "/Driving/Assets";
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() != ".meta") files.push_back(std::filesystem::relative(entry.path(), root).generic_string());
    }
    AssetDependencies dependencies;
    dependencies.build(root, files);
    CHECK(has(dependencies.uses("Scenes/Main.scene"), "Materials/Asphalt.material"));
    CHECK(!dependencies.usedBy("Models/circuit_road.obj").empty());
    for (const std::string& path : dependencies.unused()) printf("      unused: %s\n", path.c_str());
}
