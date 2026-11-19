#include "Audio/Mixer.hpp"
#include "Test.hpp"

using namespace molten;

namespace {

std::shared_ptr<AudioClip> constantClip(float value, int frames, int sampleRate = 44100, int channels = 1) {
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = sampleRate;
    clip->channels = channels;
    clip->samples.assign(size_t(frames * channels), value);
    return clip;
}

}  // namespace

TEST(mixer_plays_a_clip_once_then_goes_quiet) {
    Mixer mixer(44100);
    SoundHandle handle = mixer.play(constantClip(0.5f, 100));
    CHECK(handle != 0);
    CHECK(mixer.isPlaying(handle));

    std::vector<float> out(300 * 2, 9.0f);
    mixer.render(out.data(), 300);
    CHECK_NEAR(out[0], 0.5f, 1e-6f);                    // first frame, left
    CHECK_NEAR(out[1], 0.5f, 1e-6f);                    // first frame, right
    CHECK_NEAR(out[99 * 2], 0.5f, 1e-6f);               // last frame of the clip
    CHECK_NEAR(out[100 * 2], 0.0f, 1e-6f);              // silence after it
    CHECK_NEAR(out[299 * 2 + 1], 0.0f, 1e-6f);
    CHECK(!mixer.isPlaying(handle));
    CHECK(mixer.activeVoiceCount() == 0);
}

TEST(mixer_volume_pan_and_master_volume) {
    Mixer mixer(44100);
    PlayOptions options;
    options.volume = 0.5f;
    options.pan = -1.0f;                                // all the way left
    mixer.play(constantClip(0.8f, 64), options);
    std::vector<float> out(64 * 2);
    mixer.render(out.data(), 64);
    CHECK_NEAR(out[10], 0.4f, 1e-6f);
    CHECK_NEAR(out[11], 0.0f, 1e-6f);

    options.pan = 0.5f;                                 // half right: the left side drops to half
    mixer.play(constantClip(0.8f, 64), options);
    mixer.setMasterVolume(0.5f);
    mixer.render(out.data(), 64);
    CHECK_NEAR(out[10], 0.8f * 0.5f * 0.5f * 0.5f, 1e-6f);
    CHECK_NEAR(out[11], 0.8f * 0.5f * 0.5f, 1e-6f);
}

TEST(mixer_adds_voices_and_clamps) {
    Mixer mixer(44100);
    mixer.play(constantClip(0.3f, 32));
    mixer.play(constantClip(0.4f, 32));
    std::vector<float> out(32 * 2);
    mixer.render(out.data(), 32);
    CHECK_NEAR(out[0], 0.7f, 1e-6f);

    mixer.play(constantClip(0.9f, 32));
    mixer.play(constantClip(0.9f, 32));
    mixer.render(out.data(), 32);
    CHECK_NEAR(out[0], 1.0f, 1e-6f);                    // 1.8 does not fit: clamped
}

TEST(mixer_pitch_and_sample_rate_change_the_speed) {
    Mixer mixer(44100);
    PlayOptions twice;
    twice.pitch = 2.0f;
    SoundHandle fast = mixer.play(constantClip(0.5f, 100), twice);
    std::vector<float> out(60 * 2);
    mixer.render(out.data(), 60);
    CHECK_NEAR(out[49 * 2], 0.5f, 1e-6f);               // 100 frames at double speed last 50
    CHECK_NEAR(out[50 * 2], 0.0f, 1e-6f);
    CHECK(!mixer.isPlaying(fast));

    // A clip recorded at half the mixer's rate must last twice as many output frames.
    SoundHandle slow = mixer.play(constantClip(0.5f, 100, 22050));
    std::vector<float> longer(220 * 2);
    mixer.render(longer.data(), 220);
    CHECK_NEAR(longer[199 * 2], 0.5f, 1e-6f);
    CHECK_NEAR(longer[200 * 2], 0.0f, 1e-6f);
    CHECK(!mixer.isPlaying(slow));
}

TEST(mixer_interpolates_between_samples) {
    auto ramp = std::make_shared<AudioClip>();
    ramp->samples = {0.0f, 1.0f, 0.0f, 1.0f};
    Mixer mixer(44100);
    PlayOptions half;
    half.pitch = 0.5f;
    mixer.play(ramp, half);
    std::vector<float> out(4 * 2);
    mixer.render(out.data(), 4);
    CHECK_NEAR(out[0], 0.0f, 1e-6f);
    CHECK_NEAR(out[2], 0.5f, 1e-6f);                    // halfway between the first two samples
    CHECK_NEAR(out[4], 1.0f, 1e-6f);
    CHECK_NEAR(out[6], 0.5f, 1e-6f);
}

TEST(mixer_loops_and_stops_with_a_fade) {
    Mixer mixer(44100);
    PlayOptions looping;
    looping.loops = true;
    SoundHandle handle = mixer.play(constantClip(0.5f, 10), looping);
    std::vector<float> out(1000 * 2);
    mixer.render(out.data(), 1000);
    CHECK_NEAR(out[999 * 2], 0.5f, 1e-6f);              // still going, 100 times around
    CHECK(mixer.isPlaying(handle));

    mixer.stop(handle);
    mixer.render(out.data(), 1000);
    CHECK_NEAR(out[0], 0.5f, 1e-6f);                    // the fade starts at full volume
    CHECK(out[100 * 2] < 0.5f && out[100 * 2] > 0.0f);  // and is on its way down
    CHECK_NEAR(out[600 * 2], 0.0f, 1e-6f);
    CHECK(!mixer.isPlaying(handle));
}

TEST(mixer_stereo_clips_keep_their_sides) {
    auto clip = std::make_shared<AudioClip>();
    clip->channels = 2;
    clip->samples = {0.2f, 0.6f, 0.2f, 0.6f, 0.2f, 0.6f};
    Mixer mixer(44100);
    mixer.play(clip);
    std::vector<float> out(3 * 2);
    mixer.render(out.data(), 3);
    CHECK_NEAR(out[2], 0.2f, 1e-6f);
    CHECK_NEAR(out[3], 0.6f, 1e-6f);
}

TEST(mixer_refuses_empty_clips_and_too_many_voices) {
    Mixer mixer(44100);
    CHECK(mixer.play(nullptr) == 0);
    CHECK(mixer.play(std::make_shared<AudioClip>()) == 0);
    auto clip = constantClip(0.01f, 1000);
    for (int i = 0; i < Mixer::kMaxVoices; i++) CHECK(mixer.play(clip) != 0);
    CHECK(mixer.play(clip) == 0);
    mixer.stopAll();
    std::vector<float> out(1000 * 2);
    mixer.render(out.data(), 1000);
    CHECK(mixer.activeVoiceCount() == 0);
}

TEST(spatialize_gives_volume_by_distance_and_pan_by_side) {
    Mat4 listener = identity();                         // at the origin, looking down -Z, +X to the right
    SpatialGain near = spatialize(listener, Vec3{0, 0, -1});
    CHECK_NEAR(near.volume, 1.0f, 1e-6f);
    CHECK_NEAR(near.pan, 0.0f, 1e-6f);

    SpatialGain right = spatialize(listener, Vec3{10, 0, 0});
    CHECK_NEAR(right.pan, 0.8f, 1e-5f);
    CHECK(right.volume < 1.0f && right.volume > 0.0f);

    SpatialGain left = spatialize(listener, Vec3{-10, 0, 0});
    CHECK_NEAR(left.pan, -0.8f, 1e-5f);

    SpatialGain far = spatialize(listener, Vec3{0, 0, -100});
    CHECK_NEAR(far.volume, 0.0f, 1e-6f);

    SpatialGain half = spatialize(listener, Vec3{0, 0, -21}, 2.0f, 40.0f);
    CHECK_NEAR(half.volume, 0.25f, 1e-5f);              // halfway out: (1 - 0.5) squared
}
