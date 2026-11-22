// What a project decides for all of its scenes: tags, layers and which of them collide, gravity, the step.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Project/GameSettings.hpp"

using namespace molten;

TEST(a_new_project_has_the_usual_tags_and_layers_and_everything_collides) {
    GameSettings settings;
    CHECK(settings.allTags().front() == "Untagged" && settings.allTags().size() == settings.tags.size() + 1);
    CHECK(settings.layerName(0) == "Default" && settings.layerName(3) == "Water" && settings.layerName(12) == "Layer 12");
    CHECK(settings.layerNamed("Enemy") == 6 && settings.layerNamed("Nothing") == -1);
    for (int a = 0; a < GameSettings::kLayerCount; a++) {
        for (int b = 0; b < GameSettings::kLayerCount; b++) CHECK(settings.layersMeet(a, b));
    }
    CHECK(settings.gravity.y < -9.8f && settings.stepsPerSecond == 60);
}

TEST(game_settings_are_written_and_read_back) {
    GameSettings settings;
    settings.tags = {"Player", "Coin", "Lava Pit"};
    settings.layers[9] = "Ghosts";
    settings.setLayersMeet(5, 9, false);
    settings.setLayersMeet(9, 9, false);
    settings.gravity = Vec3{0, -3.7f, 0.5f};
    settings.stepsPerSecond = 120;
    CHECK(!settings.layersMeet(9, 5) && !settings.layersMeet(5, 9) && settings.layersMeet(5, 6));       // both ways

    GameSettings back = GameSettings::parse(settings.toText());
    CHECK(back == settings);
    CHECK(back.tags.size() == 3 && back.tags[2] == "Lava Pit" && back.layerName(9) == "Ghosts");
    // Saving what was read changes nothing in the file.
    CHECK(back.toText() == settings.toText());

    // A file that says only some things leaves the rest as a new project has it; rubbish is skipped.
    GameSettings partial = GameSettings::parse("molten-game-settings 1\ngravity 0 -1.6 0\nsomething else\nstepsPerSecond 9000\n");
    CHECK_NEAR(partial.gravity.y, -1.6, 1e-6);
    CHECK(partial.stepsPerSecond == 240);                               // kept within what physics can do
    CHECK(partial.tags == GameSettings().tags && partial.layerName(1) == "Transparent");
    CHECK(GameSettings::parse("") == GameSettings());
}

TEST(the_world_makes_its_settings_so) {
    World world;
    GameSettings settings;
    settings.gravity = Vec3{0, -1.6f, 0};
    settings.stepsPerSecond = 120;
    settings.setLayersMeet(1, 2, false);
    world.applySettings(settings);
    CHECK_NEAR(world.physics.gravity.y, -1.6, 1e-6);
    CHECK_NEAR(world.time.fixedDelta, 1.0 / 120.0, 1e-7);
    CHECK(!(world.physics.layerMeets[1] & (1u << 2)) && (world.physics.layerMeets[1] & (1u << 3)));
    // A folder with no Game.settings: as a new project.
    world.loadSettings("/nowhere/at/all");
    CHECK(world.settings == GameSettings() && world.physics.gravity.y < -9.8f);
}

namespace {

/// A ball dropped onto a floor; returns where it is after two seconds.
float dropOntoFloor(bool layersMeet) {
    World world;
    GameSettings settings;
    settings.setLayersMeet(5, 7, layersMeet);
    world.applySettings(settings);
    Entity floor = world.scene.create("Floor");
    world.scene.add(floor, Transform{});
    Collider slab = Collider::box(Vec3{10, 0.5f, 10});
    slab.layer = 1u << 7;
    world.scene.add(floor, slab);
    Entity ball = world.scene.create("Ball");
    Transform high;
    high.position = Vec3{0, 3, 0};
    world.scene.add(ball, high);
    Collider round = Collider::sphere(0.5f);
    round.layer = 1u << 5;
    world.scene.add(ball, round);
    world.scene.add(ball, RigidBody{});
    for (int i = 0; i < 120; i++) world.advance(1.0 / 60.0);
    return world.scene.get<Transform>(ball)->position.y;
}

}  // namespace

TEST(layers_that_do_not_meet_pass_through_each_other) {
    // On the floor: the middle of the ball is half a meter above the slab's top.
    CHECK_NEAR(dropOntoFloor(true), 1.0, 0.05);
    // Through it, and still falling.
    CHECK(dropOntoFloor(false) < -5.0f);
}
