// Timelines: things moved along keys, cameras cut, values set, things shown, as time goes.
#include "Test.hpp"

#include "Animation/Timeline.hpp"
#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Scene/Components.hpp"

#include <cmath>
#include <filesystem>

using namespace molten;

namespace {
const char* kIntro = R"(molten-timeline 1
length 4
track move "Ship"
  key 0  0 0 0  0 0 0 1
  key 2  10 0 0  0 0 0 1
  key 4  10 0 10  0 0.7071068 0 0.7071068
track camera
  at 0 "Wide"
  at 3 "Close"
track value "doorsOpen"
  at 1 1
  at 3.5 2
track show "Title"
  at 0 0
  at 2 1
)";
}

TEST(a_timeline_is_read_and_written) {
    std::string error;
    std::optional<Timeline> read = Timeline::parse(kIntro, &error);
    CHECK(read && error.empty());
    CHECK(read->length == 4 && read->tracks.size() == 4);
    CHECK(read->tracks[0].keys.size() == 3 && read->tracks[1].marks[1].text == "Close");
    std::optional<Timeline> again = Timeline::parse(read->toText());
    CHECK(again && again->toText() == read->toText());
    // A kind of track from a newer engine is skipped, not fatal.
    std::optional<Timeline> later = Timeline::parse("molten-timeline 1\ntrack dance \"X\"\nat 1 \"step\"\n", &error);
    CHECK(later && later->tracks.empty());
    CHECK(!Timeline::parse("length 3\n"));
    // Through its keys: at them exactly, smoothly between.
    Vec3 position;
    Quat rotation;
    CHECK(read->tracks[0].sample(2, position, rotation) && simd::length(position - Vec3{10, 0, 0}) < 1e-4f);
    CHECK(read->tracks[0].sample(1, position, rotation) && position.x > 3 && position.x < 7);
    CHECK(read->tracks[0].sample(9, position, rotation) && simd::length(position - Vec3{10, 0, 10}) < 1e-4f);
}

TEST(a_timeline_plays) {
    std::string root = "/tmp/molten-test-timeline";
    std::filesystem::create_directories(root + "/Timelines");
    text::writeFile(root + "/Timelines/Intro.timeline", kIntro);
    World world;
    world.assetRoot = root;
    kit::registerKit();
    Entity ship = world.scene.create("Ship");
    world.scene.add(ship, Transform{});
    Entity title = world.scene.create("Title");
    world.scene.add(title, Transform{});
    Entity wide = world.scene.create("Wide"), close = world.scene.create("Close");
    world.scene.add(wide, Transform{});
    world.scene.add(wide, Camera{});
    world.scene.add(close, Transform{});
    world.scene.add(close, Camera{});
    Entity director = world.scene.create("Director");
    world.scene.add(director, Transform{});
    TimelinePlayer player;
    player.timeline = "Timelines/Intro.timeline";
    player.doneValue = "introDone";
    world.scene.add(director, player);

    auto play = [&](float seconds) {
        for (int i = 0; i < int(std::lround(seconds * 60)); i++) world.advance(1.0 / 60.0);
    };
    play(0.5f);
    CHECK(!world.scene.get<Transform>(title)->isActive);                   // hidden from the start
    CHECK(world.scene.get<Camera>(wide)->isActive && !world.scene.get<Camera>(close)->isActive);
    CHECK(!world.hasValue("doorsOpen"));
    play(1.0f);
    CHECK(world.value("doorsOpen") == 1);
    play(1.0f);
    CHECK(world.scene.get<Transform>(title)->isActive);                    // shown at 2
    CHECK(std::abs(world.scene.get<Transform>(ship)->position.x - 10) < 1.0f);     // round the corner, smoothly
    play(1.0f);
    CHECK(!world.scene.get<Camera>(wide)->isActive && world.scene.get<Camera>(close)->isActive);    // cut at 3
    CHECK(!world.hasValue("introDone"));
    play(1.0f);
    CHECK(world.value("doorsOpen") == 2 && world.value("introDone") == 1);
    CHECK(simd::length(world.scene.get<Transform>(ship)->position - Vec3{10, 0, 10}) < 1e-3f);
    // Dragged back to a moment (the editor's preview): as it is then, and nothing fired on the way.
    TimelinePlayer* placed = world.scene.get<TimelinePlayer>(director);
    world.setValue("doorsOpen", 0);
    placed->seek(world, 1.0f);
    CHECK(!world.scene.get<Transform>(title)->isActive && world.scene.get<Camera>(wide)->isActive);
    CHECK(world.value("doorsOpen") == 0);
    CHECK(world.scene.get<Transform>(ship)->position.x > 3 && world.scene.get<Transform>(ship)->position.x < 7);
}
