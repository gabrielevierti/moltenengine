// Clips made in the editor (.anim): curves on properties, played by an Animator by themselves or as
// the motions of an Animator Controller's states, from code or a visual script, and on a Timeline.
#include "Test.hpp"

#include "Animation/AnimationClip.hpp"
#include "Animation/Animator.hpp"
#include "Animation/Timeline.hpp"
#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Scene/Components.hpp"
#include "Script/Convert.hpp"
#include "Script/Runtime.hpp"

#include <cmath>
#include <filesystem>

using namespace molten;

namespace {

const char* kDoor = R"(molten-clip 1
length 2
loops 0
track "" Transform position
  key 0  0 0 0
  key 2  0 3 0 linear
track "Hinge" Transform rotation
  key 0  0 0 0
  key 1  0 -90 0
track "Lamp" Light intensity
  key 0 1
  key 1 0 step
event 1 "doorOpened"
)";

const char* kBob = R"(molten-clip 1
length 1
track "" Transform position
  key 0 0 0 0
  key 0.5 0 1 0
  key 1 0 0 0
)";

const char* kController = R"(molten-animator 1
parameter trigger open
parameter float speed 0
state Closed
  motion Animations/Bob.anim
  place 40 80
state Open loops 0
  motion Animations/Door.anim
start Closed
transition Closed Open when open blend 0
)";

std::string makeProject() {
    std::string root = "/tmp/molten-test-clips";
    std::filesystem::create_directories(root + "/Animations");
    text::writeFile(root + "/Animations/Door.anim", kDoor);
    text::writeFile(root + "/Animations/Bob.anim", kBob);
    text::writeFile(root + "/Door.animator", kController);
    return root;
}

struct Door {
    World world;
    Entity door, hinge, lamp;
    Door() {
        world.assetRoot = makeProject();
        kit::registerKit();
        door = world.scene.create("Door");
        world.scene.add(door, Transform{});
        hinge = world.scene.create("Hinge");
        Transform under;
        under.parent = door;
        world.scene.add(hinge, under);
        lamp = world.scene.create("Lamp");
        world.scene.add(lamp, under);
        world.scene.add(lamp, Light{});
    }
    void play(float seconds) {
        for (int i = 0; i < int(std::lround(seconds * 60)); i++) world.advance(1.0 / 60.0);
    }
};

}  // namespace

TEST(a_clip_is_read_and_written) {
    std::string error;
    std::optional<AnimationClip> clip = AnimationClip::parse(kDoor, &error);
    CHECK(clip && error.empty());
    CHECK(clip->length == 2 && !clip->loops && clip->tracks.size() == 3 && clip->events.size() == 1);
    CHECK(clip->tracks[0].width == 3 && clip->tracks[0].keys[1].ease == AnimationKey::Ease::Linear);
    CHECK(clip->tracks[1].path == "Hinge" && clip->find("Lamp", "Light", "intensity"));
    std::optional<AnimationClip> again = AnimationClip::parse(clip->toText());
    CHECK(again && again->toText() == clip->toText());
    CHECK(!AnimationClip::parse("length 2\n"));
    CHECK(!AnimationClip::parse("molten-clip 1\nkey 0 1\n", &error) && !error.empty());
    // Through the keys: linear halfway, smooth (eased at both ends) between, a step at once.
    CHECK(std::abs(clip->tracks[0].sample(1)[1] - 1.5f) < 1e-4f);
    float early = clip->tracks[1].sample(0.1f)[1], middle = clip->tracks[1].sample(0.5f)[1];
    CHECK(early > -9 && early < 0 && std::abs(middle + 45) < 1e-3f);
    CHECK(clip->tracks[2].sample(0.99f)[0] == 1 && clip->tracks[2].sample(1)[0] == 0);
    CHECK(clip->tracks[0].sample(-1)[1] == 0 && clip->tracks[0].sample(9)[1] == 3);
    // A key put near one there replaces it.
    AnimationTrack track = clip->tracks[0];
    track.setKey(1.002f, {5, 5, 5, 0});
    track.setKey(1.0f, {6, 6, 6, 0});
    CHECK(track.keys.size() == 3 && track.keys[1].value[0] == 6);
}

TEST(properties_are_read_and_written_by_name) {
    Door door;
    Scene& scene = door.world.scene;
    CHECK(entityAtPath(scene, door.door, "") == door.door && entityAtPath(scene, door.door, "Hinge") == door.hinge);
    CHECK(!entityAtPath(scene, door.door, "Nothing").isValid());
    CHECK(pathFromRoot(scene, door.door, door.lamp) == std::optional<std::string>("Lamp"));
    CHECK(!pathFromRoot(scene, door.lamp, door.door));
    std::array<float, 4> value{};
    int width = 0;
    CHECK(writeProperty(scene, door.hinge, "Transform", "rotation", {0, 30, 0, 0}));
    CHECK(readProperty(scene, door.hinge, "Transform", "rotation", value, &width) && width == 3 && std::abs(value[1] - 30) < 1e-3f);
    CHECK(writeProperty(scene, door.lamp, "Light", "intensity", {4, 0, 0, 0}) && scene.get<Light>(door.lamp)->intensity == 4);
    CHECK(!writeProperty(scene, door.door, "Light", "intensity", {4, 0, 0, 0}));
    bool listsLight = false;
    for (const AnimatableProperty& property : animatableProperties(scene, door.lamp)) listsLight |= property.component == "Light" && property.field == "intensity";
    CHECK(listsLight);
}

TEST(an_animator_plays_a_clip_made_in_the_editor) {
    Door door;
    Animator animator;
    animator.clip = "Animations/Door.anim";
    animator.loops = false;
    door.world.scene.add(door.door, animator);
    int heard = 0;
    door.world.listen("doorOpened", [&](const Message& message) { heard += message.to == door.door; });
    door.play(0.5f);
    Scene& scene = door.world.scene;
    CHECK(scene.get<Light>(door.lamp)->intensity == 1 && heard == 0);
    door.play(0.6f);
    CHECK(scene.get<Light>(door.lamp)->intensity == 0 && heard == 1);           // its event: a message to the door
    door.play(1.5f);
    CHECK(std::abs(scene.get<Transform>(door.door)->position.y - 3) < 1e-3f);
    CHECK(scene.get<Animator>(door.door)->isFinished() && heard == 1);
    std::array<float, 4> angles{};
    readProperty(scene, door.hinge, "Transform", "rotation", angles);
    CHECK(std::abs(angles[1] + 90) < 0.1f);
}

TEST(a_controller_plays_clips_made_in_the_editor_as_its_states) {
    Door door;
    Animator animator;
    animator.controller = "Door.animator";
    door.world.scene.add(door.door, animator);
    door.play(0.25f);
    Scene& scene = door.world.scene;
    Animator* playing = scene.get<Animator>(door.door);
    CHECK(playing->currentState() == "Closed");
    CHECK(scene.get<Transform>(door.door)->position.y > 0.3f);                     // bobbing
    playing->setTrigger("open");
    door.play(2.5f);
    CHECK(playing->currentState() == "Open" && playing->isFinished());
    CHECK(std::abs(scene.get<Transform>(door.door)->position.y - 3) < 1e-3f);
    // Asked for by the game: back to Closed at once, whatever the transitions say.
    playing->playState("Closed");
    door.play(0.1f);
    CHECK(playing->currentState() == "Closed");
    // The box's place is kept in the file.
    std::optional<AnimatorController> controller = AnimatorController::parse(kController);
    CHECK(controller && controller->states[0].x == 40 && controller->states[0].y == 80);
    CHECK(AnimatorController::parse(controller->toText())->states[0].y == 80);
}

TEST(visual_scripts_drive_the_animator) {
    Door door;
    Animator animator;
    animator.controller = "Door.animator";
    door.world.scene.add(door.door, animator);
    door.play(0.1f);
    script::ScriptState state;
    script::Call call(door.world, door.door, 1.0f / 60, state);
    call("SetAnimatorFloat", {{"parameter", script::Value::string("speed")}, {"value", script::Value::number(2.5f)}});
    CHECK(door.world.scene.get<Animator>(door.door)->parameter("speed") == 2.5f);
    CHECK(call.value("GetAnimatorFloat", {{"parameter", script::Value::string("speed")}}).asFloat() == 2.5f);
    call("SetAnimatorTrigger", {{"parameter", script::Value::string("open")}});
    door.play(0.1f);
    CHECK(call.value("AnimatorState", {}, "current").asString() == "Open");
    CHECK(call.value("AnimatorState", {{"state", script::Value::string("Open")}}, "isIn").asBool());
    call("CrossFade", {{"state", script::Value::string("Closed")}, {"seconds", script::Value::number(0)}});
    door.play(0.1f);
    CHECK(door.world.scene.get<Animator>(door.door)->currentState() == "Closed");
}

TEST(a_timeline_plays_a_clip_on_an_entity) {
    Door door;
    std::string root = door.world.assetRoot;
    text::writeFile(root + "/Scene.timeline", "molten-timeline 1\nlength 5\ntrack clip \"Door\"\n  at 1 \"Animations/Door.anim\" 1\n");
    Entity director = door.world.scene.create("Director");
    door.world.scene.add(director, Transform{});
    TimelinePlayer player;
    player.timeline = "Scene.timeline";
    door.world.scene.add(director, player);
    door.play(0.5f);
    Scene& scene = door.world.scene;
    CHECK(scene.get<Transform>(door.door)->position.y == 0);
    door.play(1.5f);                                                               // a second into the clip
    CHECK(std::abs(scene.get<Transform>(door.door)->position.y - 1.5f) < 0.1f);
    TimelinePlayer* placed = scene.get<TimelinePlayer>(director);
    placed->seek(door.world, 2.0f);                                                // dragged: shown as it is then
    CHECK(std::abs(scene.get<Transform>(door.door)->position.y - 1.5f) < 1e-3f);
    std::optional<Timeline> read = Timeline::parse(text::readFile(root + "/Scene.timeline").value());
    CHECK(read && read->tracks[0].kind == TimelineTrackKind::Clip && Timeline::parse(read->toText())->toText() == read->toText());
}

TEST(an_animator_plays_its_own_animations_by_name) {
    Door door;
    text::writeFile(door.world.assetRoot + "/Animations/Spin.anim",
                    "molten-clip 1\nlength 1\nowner \"Door\"\ntrack \"\" Transform position\n  key 0 0 0 0\n  procedural wave 2 1 0.25 axes 0 1 0\n");
    Animator animator;
    animator.animations = "Animations/Door.anim, Animations/Spin.anim";
    door.world.scene.add(door.door, animator);
    Animator* playing = door.world.scene.get<Animator>(door.door);
    CHECK(playing->animationNames() == std::vector<std::string>({"Door", "Spin"}));
    playing->play("Spin");
    door.play(0.02f);
    CHECK(playing->isPlayingAnimation("Spin") && !playing->isPlayingAnimation("Door"));
    // A wave of 2 up and down, a quarter turn in: at its top.
    CHECK(std::abs(door.world.scene.get<Transform>(door.door)->position.y - 2) < 0.1f);
    playing->stop();
    CHECK(!playing->isPlaying && playing->time == 0);
    std::optional<AnimationClip> read = AnimationClip::parse(text::readFile(door.world.assetRoot + "/Animations/Spin.anim").value());
    CHECK(read && read->owner == "Door" && read->tracks[0].procedural.kind == Procedural::Kind::Wave && read->tracks[0].procedural.axes[0] == 0);
    CHECK(AnimationClip::parse(read->toText())->toText() == read->toText());
}

TEST(a_parameter_says_how_far_through_an_animation_is) {
    AnimationClip clip;
    clip.length = 2;
    clip.timeBy = "open";
    AnimationTrack track;
    track.component = "Transform";
    track.field = "position";
    track.width = 3;
    track.setKey(0, {0, 0, 0, 0});
    track.setKey(2, {0, 4, 0, 0});
    track.keys[1].ease = AnimationKey::Ease::Linear;
    clip.tracks.push_back(track);
    AnimationTrack lever = track;
    lever.field = "scale";
    lever.keys.clear();
    lever.procedural.kind = Procedural::Kind::Parameter;
    lever.procedural.amplitude = 3;
    lever.procedural.parameter = "open";
    clip.tracks.push_back(lever);
    std::unordered_map<std::string, float> parameters{{"open", 0.25f}};
    ClipPose pose = ClipPose::of(clip, 99, &parameters);
    CHECK(std::abs(pose.values.at(track.id()).value[1] - 1) < 1e-4f);
    CHECK(std::abs(pose.values.at(lever.id()).value[0] - 0.75f) < 1e-4f);
    CHECK(AnimationClip::parse(clip.toText())->timeBy == "open");
}
