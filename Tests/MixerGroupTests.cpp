// The mixer's groups, ducking, rooms and walls.
#include "Test.hpp"

#include "Audio/Mixer.hpp"
#include "Core/World.hpp"
#include "Game/Component.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Sound.hpp"
#include "Physics/Physics.hpp"

#include <cmath>

using namespace molten;

namespace {
std::shared_ptr<AudioClip> steady(float value, float seconds = 5) {
    auto clip = std::make_shared<AudioClip>();
    clip->samples.assign(size_t(44100 * seconds), value);
    return clip;
}
/// The loudness of what the mixer gives over `seconds`, the last block's average size.
float listen(Mixer& mixer, float seconds) {
    std::vector<float> out(512 * 2);
    float level = 0;
    for (int block = 0; block < int(seconds * 44100 / 512); block++) {
        mixer.render(out.data(), 512);
        level = 0;
        for (float v : out) level += std::abs(v);
        level /= float(out.size());
    }
    return level;
}
}  // namespace

TEST(sounds_play_in_groups_with_their_volumes) {
    Mixer mixer;
    std::vector<std::string> names = mixer.groupNames();
    CHECK(names.size() == 4 && names[0] == "Effects" && names[1] == "Music" && mixer.group("Music") == 1);
    int ambience = mixer.group("Ambience");
    CHECK(ambience == 4 && mixer.group("Ambience") == 4);
    PlayOptions music;
    music.group = mixer.group("Music");
    music.loops = true;
    mixer.play(steady(0.5f), music);
    CHECK(std::abs(listen(mixer, 0.1f) - 0.5f) < 0.01f);
    mixer.setGroupVolume(1, 0.5f);
    CHECK(std::abs(listen(mixer, 0.05f) - 0.25f) < 0.01f);
    mixer.setGroupMuted(1, true);
    CHECK(listen(mixer, 0.05f) < 1e-4f);
    mixer.setGroupMuted(1, false);
    mixer.setGroupVolume(1, 1);
    // Someone speaks: the music goes down to under a third, and back up when they stop.
    PlayOptions voice;
    voice.group = mixer.group("Voices");
    SoundHandle speaking = mixer.play(steady(0.0f, 1), voice);
    CHECK(std::abs(listen(mixer, 0.6f) - 0.15f) < 0.02f);
    CHECK(mixer.groupDucking(1) < 0.32f);
    mixer.stop(speaking);
    CHECK(std::abs(listen(mixer, 4) - 0.5f) < 0.02f);
    // The effects are not ducked, and ducking can be stopped.
    mixer.duck(1, -1);
    mixer.play(steady(0.0f, 1), voice);
    CHECK(std::abs(listen(mixer, 0.6f) - 0.5f) < 0.02f);
}

TEST(a_muffled_sound_loses_its_highs_and_a_room_rings_after_it) {
    Mixer mixer;
    // A high whistle: every other sample up and down.
    auto whistle = std::make_shared<AudioClip>();
    for (int i = 0; i < 44100 * 2; i++) whistle->samples.push_back(i % 2 ? 0.5f : -0.5f);
    PlayOptions options;
    options.loops = true;
    SoundHandle sound = mixer.play(whistle, options);
    CHECK(std::abs(listen(mixer, 0.1f) - 0.5f) < 0.01f);
    mixer.setMuffle(sound, 1);
    CHECK(listen(mixer, 0.5f) < 0.02f);                       // behind a wall: hardly a whisper of it
    mixer.stopAll();
    listen(mixer, 0.1f);

    // A clap in a cave rings on; in no room it is over at once; music does not ring.
    auto clap = std::make_shared<AudioClip>();
    clap->samples.assign(4410, 0.0f);
    for (int i = 0; i < 200; i++) clap->samples[size_t(i)] = i % 2 ? 0.8f : -0.8f;
    mixer.play(clap);
    CHECK(listen(mixer, 0.4f) < 1e-6f);                       // dry: silence after it
    mixer.setReverb(0.85f, 0.2f, 0.5f);
    listen(mixer, 1.5f);                                      // eased in
    CHECK(std::abs(mixer.reverbWet() - 0.5f) < 1e-6f);
    mixer.play(clap);
    float ringing = listen(mixer, 0.4f);
    CHECK(ringing > 1e-4f);
    CHECK(listen(mixer, 8) < ringing * 0.05f);                // and dies away
    PlayOptions music;
    music.group = mixer.group("Music");
    mixer.play(clap, music);
    CHECK(listen(mixer, 0.4f) < 1e-5f);
}

namespace {
/// A World that listens from a point, and keeps what it would tell a mixer.
struct Hearing : World {
    Mat4 at = identity();
    float muffled = -1, wet = -1;
    std::string group;
    bool listener(Mat4& world) override {
        world = at;
        return true;
    }
    uint32_t beginSound(const std::string&, float, float, bool) override { return 9; }
    void groupSound(uint32_t, const std::string& name) override { group = name; }
    void muffleSound(uint32_t, float muffle) override { muffled = muffle; }
    void setRoom(float, float, float w) override { wet = w; }
};
}  // namespace

TEST(reverb_zones_and_walls_change_what_is_heard) {
    kit::registerKit();
    Hearing world;
    world.setVolume("Music", 0.4f);
    CHECK(std::abs(world.volumeOf("Music") - 0.4f) < 1e-6f && world.volumeOf("Effects") == 1);

    Entity cave = world.scene.create("Cave");
    world.scene.add(cave, Transform{});
    kit::ReverbZone zone;
    zone.room = "Cave";
    world.scene.add(cave, zone);
    world.advance(1.0 / 60.0);
    CHECK(world.wet > 0.4f && std::abs(world.roomWet() - 0.5f) < 1e-5f);       // Cave, and the listener in its middle
    world.at = translation(Vec3{8.5f, 0, 0});                                  // in the edge of it: some
    world.advance(1.0 / 60.0);
    CHECK(world.roomWet() > 0.05f && world.roomWet() < 0.45f);
    world.at = translation(Vec3{30, 0, 0});                                    // outside: dry
    world.advance(1.0 / 60.0);
    CHECK(world.wet == 0 && world.roomWet() == 0);

    // A sound behind a wall is muffled; in the open, not.
    world.at = identity();
    Entity wall = world.scene.create("Wall");
    Transform place;
    place.position = Vec3{0, 0, -5};
    world.scene.add(wall, place);
    Collider collider;
    collider.halfExtents = Vec3{4, 4, 0.2f};
    world.scene.add(wall, collider);
    world.advance(1.0 / 60.0);
    uint32_t sound = world.startSound("Audio/radio.wav", 1, 1, true, "Voices");
    CHECK(world.group == "Voices");
    world.placeSound(sound, Vec3{0, 0, -10});
    world.changeSound(sound, 1, 1);
    CHECK(world.muffled > 0.5f);
    world.placeSound(sound, Vec3{10, 0, 0});
    CHECK(world.muffled == 0);
}
