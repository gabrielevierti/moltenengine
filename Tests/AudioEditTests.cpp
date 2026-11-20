// AudioEditTests.cpp
// The changes the sound viewer makes to a clip.
#include "Test.hpp"

#include "Audio/AudioEdit.hpp"

#include <cmath>

using namespace molten;

namespace {

AudioClip tone(float hertz, float seconds, int channels = 1, int rate = 8000) {
    AudioClip clip;
    clip.sampleRate = rate;
    clip.channels = channels;
    size_t frames = size_t(seconds * float(rate));
    for (size_t i = 0; i < frames; i++) {
        float sample = 0.5f * std::sin(2 * 3.14159265f * hertz * float(i) / float(rate));
        for (int c = 0; c < channels; c++) clip.samples.push_back(sample);
    }
    return clip;
}

}  // namespace

TEST(sound_edits_change_only_their_stretch) {
    AudioClip clip = tone(200, 1.0f);
    audio::gain(clip, 0.5f, 1.0f, 0.5f);
    CHECK_NEAR(audio::peak(clip, 0.0f, 0.5f), 0.5f, 0.01f);
    CHECK_NEAR(audio::peak(clip, 0.5f, 1.0f), 0.25f, 0.01f);
    audio::normalize(clip, 0.0f, 1.0f, 0.9f);
    CHECK_NEAR(audio::peak(clip, 0.0f, 1.0f), 0.9f, 0.001f);
    audio::silence(clip, 0.25f, 0.5f);
    CHECK_NEAR(audio::peak(clip, 0.26f, 0.49f), 0.0f, 1e-6f);
    CHECK(audio::peak(clip, 0.0f, 0.2f) > 0.5f);
    CHECK_NEAR(audio::decibels(-6.0f), 0.501f, 0.002f);
}

TEST(sound_fades_start_and_end_in_silence) {
    AudioClip clip = tone(200, 1.0f);
    audio::fadeIn(clip, 0.0f, 0.5f);
    audio::fadeOut(clip, 0.5f, 1.0f);
    CHECK(audio::peak(clip, 0.0f, 0.02f) < 0.05f);
    CHECK(audio::peak(clip, 0.98f, 1.0f) < 0.05f);
    CHECK(audio::peak(clip, 0.45f, 0.55f) > 0.45f);
}

TEST(sound_cuts_keep_or_take_out_a_stretch) {
    AudioClip clip = tone(200, 1.0f, 2);
    audio::trim(clip, 0.25f, 0.75f);
    CHECK_NEAR(clip.duration(), 0.5f, 0.001f);
    audio::remove(clip, 0.1f, 0.2f);
    CHECK_NEAR(clip.duration(), 0.4f, 0.001f);
    CHECK(clip.samples.size() % 2 == 0);
    audio::toMono(clip);
    CHECK(clip.channels == 1);
    CHECK_NEAR(clip.duration(), 0.4f, 0.001f);
    audio::toStereo(clip);
    CHECK(clip.channels == 2);
    CHECK_NEAR(clip.samples[10], clip.samples[11], 1e-6f);
}

TEST(sound_reversed_twice_is_itself) {
    AudioClip clip = tone(130, 0.3f, 2);
    clip.samples[0] = 0.9f;
    AudioClip before = clip;
    audio::reverse(clip, 0.0f, 0.3f);
    CHECK_NEAR(clip.samples[clip.samples.size() - 2], 0.9f, 1e-6f);
    audio::reverse(clip, 0.0f, 0.3f);
    bool isSame = true;
    for (size_t i = 0; i < clip.samples.size(); i++) isSame = isSame && std::abs(clip.samples[i] - before.samples[i]) < 1e-6f;
    CHECK(isSame);
}

TEST(sound_filters_let_through_their_side) {
    AudioClip low = tone(100, 0.5f, 1, 44100), high = tone(6000, 0.5f, 1, 44100);
    AudioClip a = low, b = high;
    audio::lowPass(a, 0, 0.5f, 600);
    audio::lowPass(b, 0, 0.5f, 600);
    CHECK(audio::peak(a, 0.1f, 0.5f) > 0.4f);
    CHECK(audio::peak(b, 0.1f, 0.5f) < 0.06f);
    a = low, b = high;
    audio::highPass(a, 0, 0.5f, 1200);
    audio::highPass(b, 0, 0.5f, 1200);
    CHECK(audio::peak(a, 0.1f, 0.5f) < 0.06f);
    CHECK(audio::peak(b, 0.1f, 0.5f) > 0.35f);
}

TEST(sound_speed_and_loop_change_its_length) {
    AudioClip clip = tone(200, 1.0f);
    audio::changeSpeed(clip, 2.0f);
    CHECK_NEAR(clip.duration(), 0.5f, 0.002f);
    audio::makeLoop(clip, 0.1f);
    CHECK_NEAR(clip.duration(), 0.4f, 0.002f);
    // The end now leads into the beginning: no step between the last sample and the first.
    CHECK(std::abs(clip.samples.back() - clip.samples.front()) < 0.2f);
}
