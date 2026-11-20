#include "Test.hpp"

#include "Game/Component.hpp"
#include "Scene/SceneFile.hpp"

using namespace molten;

namespace {

struct TestSpinner {
    float turnsPerSecond = 0.5f;
    int laps = 3;
    bool isReversed = false;
    Vec2 wobble{0, 0};
    Vec3 axis{0, 1, 0};
    Vec4 tint{1, 1, 1, 1};
    std::string label = "spin";
    float notSaved = 7;                 // not listed below: neither shown nor saved

    int updates = 0, starts = 0;
    void start(Engine&, Entity) { starts++; }
    void update(Engine&, Entity, float) { updates++; }
};

struct TestMarker {};

}  // namespace

MOLTEN_COMPONENT(TestSpinner, MOLTEN_FIELD(turnsPerSecond).range(0, 10), MOLTEN_FIELD(laps), MOLTEN_FIELD(isReversed),
                 MOLTEN_FIELD(wobble), MOLTEN_FIELD(axis), MOLTEN_FIELD(tint).color(), MOLTEN_FIELD(label))
MOLTEN_COMPONENT(TestMarker)

TEST(components_declare_their_fields_and_code) {
    const ComponentType* type = ComponentRegistry::instance().find("TestSpinner");
    CHECK(type != nullptr);
    CHECK(type->fields.size() == 7);
    CHECK(type->fields[0].name == "turnsPerSecond");
    CHECK(type->fields[0].type == FieldType::Float);
    CHECK(type->fields[0].maximum == 10);
    CHECK(type->fields[5].isColor);
    CHECK(type->fields[6].type == FieldType::String);
    CHECK(type->start != nullptr && type->update != nullptr);
    CHECK(type->fixedUpdate == nullptr);                    // the struct has no such function
    CHECK(type->hasCode());
    const ComponentType* marker = ComponentRegistry::instance().find("TestMarker");
    CHECK(marker != nullptr && marker->fields.empty() && !marker->hasCode());

    CHECK(type->fields[0].label() == "Turns Per Second");
    CHECK(type->fields[2].label() == "Is Reversed");
    Field odd;
    odd.name = "maxHP_bonus";
    CHECK(odd.label() == "Max HP bonus");
}

TEST(component_fields_are_read_and_written_as_text) {
    const ComponentType* type = ComponentRegistry::instance().find("TestSpinner");
    Scene scene;
    Entity entity = scene.create("A");
    CHECK(!type->has(scene, entity));
    TestSpinner* spinner = static_cast<TestSpinner*>(type->add(scene, entity));
    CHECK(type->has(scene, entity) && scene.has<TestSpinner>(entity));
    CHECK(type->findField("turnsPerSecond")->valueText(spinner) == "0.5");
    CHECK(type->findField("axis")->valueText(spinner) == "0 1 0");
    CHECK(type->findField("label")->valueText(spinner) == "spin");

    type->findField("turnsPerSecond")->setValueText(spinner, "2.25");
    type->findField("laps")->setValueText(spinner, "-4");
    type->findField("isReversed")->setValueText(spinner, "1");
    type->findField("axis")->setValueText(spinner, "1 0");             // the missing number keeps its value
    type->findField("label")->setValueText(spinner, "two words");
    type->findField("laps")->setValueText(spinner, "not a number");     // ignored
    CHECK_NEAR(spinner->turnsPerSecond, 2.25, 0);
    CHECK(spinner->laps == -4);
    CHECK(spinner->isReversed);
    CHECK(spinner->axis.x == 1 && spinner->axis.y == 0 && spinner->axis.z == 0);
    CHECK(spinner->label == "two words");

    Entity other = scene.create("B");
    type->copy(scene, entity, other);
    CHECK(scene.get<TestSpinner>(other)->laps == -4);
    std::vector<Entity> owners;
    type->collect(scene, owners);
    CHECK(owners.size() == 2);
    type->remove(scene, entity);
    CHECK(!type->has(scene, entity));
    CHECK(type->findField("nothing") == nullptr);
}

TEST(scene_files_keep_game_components) {
    Scene scene;
    Entity entity = scene.create("Fan");
    scene.add(entity, Transform{});
    TestSpinner spinner;
    spinner.turnsPerSecond = 1.75f;
    spinner.laps = 9;
    spinner.isReversed = true;
    spinner.wobble = Vec2{0.25f, -0.5f};
    spinner.axis = Vec3{0, 0, 1};
    spinner.tint = Vec4{0.1f, 0.2f, 0.3f, 0.4f};
    spinner.label = "a \"quoted\" name";
    spinner.notSaved = 99;
    scene.add(entity, spinner);
    scene.add(entity, TestMarker{});
    Entity plain = scene.create("Plain");
    scene.add(plain, Transform{});

    std::string text = writeScene(scene, {}, RenderSettings{});
    CHECK(text.find("component \"TestSpinner\"") != std::string::npos);
    CHECK(text.find("notSaved") == std::string::npos);

    Scene loaded;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    CHECK(readScene(text, loaded, materials, settings, SceneAssets{}));
    Entity fan = loaded.find("Fan");
    const TestSpinner* back = loaded.get<TestSpinner>(fan);
    CHECK(back != nullptr);
    if (back) {
        CHECK(back->turnsPerSecond == 1.75f && back->laps == 9 && back->isReversed);
        CHECK(back->wobble.x == 0.25f && back->wobble.y == -0.5f);
        CHECK(back->axis.z == 1 && back->tint.w == 0.4f);
        CHECK(back->label == "a \"quoted\" name");
        CHECK(back->notSaved == 7);                         // came back as the default
    }
    CHECK(loaded.has<TestMarker>(fan));
    CHECK(!loaded.has<TestSpinner>(loaded.find("Plain")));
    CHECK(writeScene(loaded, materials, settings) == text);  // saving an unchanged scene changes nothing
}

TEST(scene_files_keep_components_nobody_declared) {
    // As if the scene had been made with a game module that is not loaded now.
    std::string text =
        "molten-scene 1\n"
        "entity \"Door\"\n"
        "  transform 0 0 0  0 0 0 1  1 1 1\n"
        "  component \"Latch\"\n"
        "    field angle \"35\"\n"
        "    field label \"north door\"\n"
        "  component \"TestSpinner\"\n"
        "    field laps \"5\"\n"
        "    field removedLongAgo \"1\"\n";
    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    CHECK(readScene(text, scene, materials, settings, SceneAssets{}));
    Entity door = scene.find("Door");
    const UnknownComponents* unknown = scene.get<UnknownComponents>(door);
    CHECK(unknown != nullptr && unknown->lines.size() == 3);
    CHECK(scene.get<TestSpinner>(door) != nullptr && scene.get<TestSpinner>(door)->laps == 5);

    std::string written = writeScene(scene, materials, settings);
    CHECK(written.find("component \"Latch\"") != std::string::npos);
    CHECK(written.find("field angle \"35\"") != std::string::npos);
    CHECK(written.find("field label \"north door\"") != std::string::npos);
    CHECK(written.find("removedLongAgo") == std::string::npos);     // a field the code no longer has is let go
}

TEST(a_game_module_takes_its_types_with_it) {
    ComponentRegistry& registry = ComponentRegistry::instance();
    int token = 0;
    ComponentType type;
    type.name = "FromModule";
    type.module = &token;
    registry.add(type);
    ComponentType replacement = type;
    replacement.fields.push_back(Field{});
    registry.add(replacement);                              // the same name: replaced, not doubled
    CHECK(registry.find("FromModule") != nullptr && registry.find("FromModule")->fields.size() == 1);
    registry.removeModule(&token);
    CHECK(registry.find("FromModule") == nullptr);
    CHECK(registry.find("TestSpinner") != nullptr);         // types of the program itself stay
}
