// Editing many entities at once from the Inspector.
#include "Test.hpp"

#include "../Editor/MultiEdit.hpp"
#include "Game/Component.hpp"
#include "Kit/Kit.hpp"

#include <cmath>

using namespace molten;

TEST(what_is_changed_on_one_selected_is_changed_on_all) {
    kit::registerKit();
    Scene scene;
    std::vector<NamedMaterial> materials;
    SceneAssets assets;
    auto make = [&](const char* name, Vec3 at, Vec3 color, const char* clip, bool lit) {
        Entity entity = scene.create(name);
        Transform place;
        place.position = at;
        scene.add(entity, place);
        if (lit) {
            Light light;
            light.color = color;
            scene.add(entity, light);
        }
        kit::AudioSource source;
        source.clip = clip;
        scene.add(entity, source);
        return entity;
    };
    Entity a = make("A", Vec3{0, 1, 2}, Vec3{1, 0, 0}, "Audio/a.wav", true);
    Entity b = make("B", Vec3{3, 4, 5}, Vec3{0, 1, 0}, "Audio/b.wav", true);
    Entity c = make("C", Vec3{6, 7, 8}, Vec3{0, 0, 1}, "Audio/c.wav", false);
    std::vector<Entity> selected{b, c, a};

    EntityPicture before = pictureOf(scene, a, materials);
    scene.get<Transform>(a)->position.x = 10;                       // x only
    scene.get<Light>(a)->intensity = 3;                             // one value of the light
    scene.get<kit::AudioSource>(a)->volume = 0.25f;                 // one field of a declared component
    Collider collider;
    collider.halfExtents = Vec3{2, 2, 2};
    scene.add(a, collider);                                         // a built-in component added
    scene.add(a, kit::Health{});                                    // and a declared one
    EntityPicture after = pictureOf(scene, a, materials);
    CHECK(spreadChanges(scene, before, after, a, selected, materials, assets) > 0);

    for (Entity other : {b, c}) {
        const Transform* place = scene.get<Transform>(other);
        CHECK(place->position.x == 10);
        CHECK(scene.get<kit::AudioSource>(other)->volume == 0.25f);
        CHECK(scene.has<Collider>(other) && scene.get<Collider>(other)->halfExtents.x == 2);
        CHECK(scene.has<kit::Health>(other));
    }
    CHECK(scene.get<Transform>(b)->position.y == 4 && scene.get<Transform>(c)->position.z == 8);       // their own y and z
    CHECK(scene.get<kit::AudioSource>(b)->clip == "Audio/b.wav");                                       // their own clips
    CHECK(scene.get<Light>(b)->intensity == 3 && scene.get<Light>(b)->color.y == 1 && scene.get<Light>(b)->color.x == 0);   // their own color
    CHECK(!scene.has<Light>(c));                                    // a value of a light: only for those that have one

    // Taken away from one, taken away from all.
    before = pictureOf(scene, a, materials);
    scene.remove<Collider>(a);
    scene.remove<kit::Health>(a);
    after = pictureOf(scene, a, materials);
    spreadChanges(scene, before, after, a, selected, materials, assets);
    CHECK(!scene.has<Collider>(b) && !scene.has<Collider>(c) && !scene.has<kit::Health>(b));
    // Nothing changed: nothing done.
    CHECK(spreadChanges(scene, after, after, a, selected, materials, assets) == 0);
    CHECK(splitWords("mesh \"Models/a b.obj\" 2 1 1") == std::vector<std::string>({"mesh", "\"Models/a b.obj\"", "2", "1", "1"}));
}
