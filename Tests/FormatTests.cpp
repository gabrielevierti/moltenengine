// The promise the file formats make (Docs/FileFormats.md): a file from a newer engine of the
// same version still opens, without what is new; one of a newer version is refused, not guessed at.
#include "Test.hpp"

#include "Animation/AnimatorController.hpp"
#include "Animation/Timeline.hpp"
#include "Core/SaveGame.hpp"
#include "Core/TextFile.hpp"
#include "Project/Project.hpp"
#include "Rendering/Material.hpp"
#include "Scene/SceneFile.hpp"
#include "Script/Graph.hpp"

using namespace molten;

TEST(a_header_says_which_versions_can_be_read) {
    CHECK(text::checkHeader("molten-scene 1", "molten-scene").empty());
    CHECK(text::checkHeader("molten-scene", "molten-scene").empty());            // no number: version 1
    CHECK(text::checkHeader("molten-quality 3", "molten-quality").empty());
    CHECK(!text::checkHeader("molten-scene 2", "molten-scene").empty());
    CHECK(!text::checkHeader("molten-scene 0", "molten-scene").empty());
    CHECK(!text::checkHeader("molten-material 1", "molten-scene").empty());
    CHECK(text::checkHeader("molten-scene 9", "molten-scene").find("newer") != std::string::npos);
    for (const text::FileFormat& format : text::kFileFormats) CHECK(text::newestVersion(format.name) == format.version);
}

TEST(a_scene_with_lines_from_a_newer_engine_still_opens) {
    std::string text =
        "molten-scene 1\n"
        "settings\n"
        "  aurora 0.5 green\n"                     // not something this engine has
        "entity \"Lamp\"\n"
        "  transform 1 2 3 0 0 0 1 1 1 1\n"
        "  hologram \"blue\" 3\n"                  // nor this
        "entity \"Floor\"\n"
        "  transform 0 0 0 0 0 0 1 1 1 1\n";
    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    CHECK(readScene(text, scene, materials, settings, SceneAssets{}));
    Entity lamp = scene.find("Lamp");
    CHECK(lamp.isValid() && scene.find("Floor").isValid());
    CHECK(lamp.isValid() && scene.get<Transform>(lamp)->position.y == 2.0f);
}

TEST(a_file_of_a_newer_version_is_refused) {
    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    CHECK(!readScene("molten-scene 2\nentity \"A\"\n", scene, materials, settings, SceneAssets{}));
    Material material;
    CHECK(!readMaterial("molten-material 7\nshader lit\n", material));
    CHECK(Project::fromText("molten-project 3\nname \"Later\"\n")->name == "Later");     // the one exception: see Project.cpp
    CHECK(!script::Graph::fromText("molten-graph 2\n"));
    std::string why;
    CHECK(!Timeline::parse("molten-timeline 2\nlength 4\n", &why) && why.find("newer") != std::string::npos);
    CHECK(!AnimatorController::parse("molten-animator 5\n", &why) && why.find("newer") != std::string::npos);
    CHECK(SaveGame::fromText("molten-save 4\nnumber \"score\" 10\n").isEmpty());
    CHECK(SaveGame::fromText("molten-save 1\nnumber \"score\" 10\n").number("score") == 10);
}

TEST(timelines_and_animators_skip_what_they_do_not_know) {
    std::string why;
    std::optional<Timeline> timeline = Timeline::parse(
        "molten-timeline 1\nlength 4\nfog 0.3\n"
        "track hologram \"Lamp\"\nat 1 \"on\"\n"            // a kind of track from later: skipped with its lines
        "track camera \"Cam\"\nat 2 \"Wide\"\n",
        &why);
    CHECK(timeline.has_value());
    CHECK(timeline && timeline->tracks.size() == 1 && timeline->tracks[0].marks.size() == 1);
    std::optional<AnimatorController> animator = AnimatorController::parse(
        "molten-animator 1\nparameter float speed\nstate \"Idle\" \"idle\"\nmood calm\nstate \"Run\" \"run\"\n"
        "transition Idle Run when speed > 1 smoothly\n",
        &why);
    CHECK(animator.has_value());
}
