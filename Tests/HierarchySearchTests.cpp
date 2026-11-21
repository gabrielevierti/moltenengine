// The Hierarchy's search: names, components, tags, layers, prefabs.
#include "Test.hpp"

#include "../Editor/HierarchySearch.hpp"
#include "Kit/Kit.hpp"
#include "Physics/Physics.hpp"
#include "Scene/Components.hpp"
#include "Scene/SceneFile.hpp"

using namespace molten;

TEST(the_hierarchy_finds_by_name_component_tag_and_layer) {
    kit::registerKit();
    Scene scene;
    auto make = [&](const char* name) {
        Entity entity = scene.create(name);
        scene.add(entity, Transform{});
        scene.add(entity, Name{name});
        return entity;
    };
    Entity lamp = make("Street Lamp");
    scene.add(lamp, Light{});
    Entity sun = make("Sun");
    Light directional;
    directional.type = LightType::Directional;
    scene.add(sun, directional);
    Entity goblin = make("Goblin");
    scene.add(goblin, Tag{"Enemy", 6});
    scene.add(goblin, Collider{});
    scene.add(goblin, kit::Health{});
    Entity orc = make("Orc");
    scene.add(orc, Tag{"Enemy", 6});
    scene.get<Transform>(orc)->isActive = false;
    Entity wheel = make("Wheel (1)");
    scene.add(wheel, PrefabInstance{"Prefabs/Wheel.prefab", {}});

    auto find = [&](const std::string& query) {
        std::vector<SearchWord> words = parseSearch(query);
        std::vector<std::string> names;
        for (Entity entity : {lamp, sun, goblin, orc, wheel}) {
            if (matchesSearch(scene, entity, scene.get<Name>(entity)->value, words)) names.push_back(scene.get<Name>(entity)->value);
        }
        return names;
    };
    using List = std::vector<std::string>;
    CHECK(find("lamp") == List({"Street Lamp"}));
    CHECK(find("c:light") == List({"Street Lamp", "Sun"}));
    CHECK(find("t:directional") == List({"Sun"}));
    CHECK(find("c:pointlight") == List({"Street Lamp"}));
    CHECK(find("tag:enemy") == List({"Goblin", "Orc"}));
    CHECK(find("tag:enemy c:health") == List({"Goblin"}));
    CHECK(find("tag:enemy -c:collider") == List({"Orc"}));
    CHECK(find("layer:enemy") == List({"Goblin", "Orc"}) && find("layer:6") == find("layer:enemy"));
    CHECK(find("is:off") == List({"Orc"}));
    CHECK(find("prefab:wheel") == List({"Wheel (1)"}));
    CHECK(find("c:transform").size() == 5 && find("").size() == 5);
    CHECK(find("nothing-like-it").empty());
}
