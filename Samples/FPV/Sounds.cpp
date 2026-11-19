#include "Sounds.hpp"

#include <cmath>

using namespace molten;

namespace fpv {

namespace {

SynthParams note(Waveform waveform, float frequency, float duration, float volume) {
    SynthParams params;
    params.waveform = waveform;
    params.frequency = frequency;
    params.duration = duration;
    params.volume = volume;
    params.release = duration * 0.5f;
    return params;
}

std::shared_ptr<AudioClip> share(AudioClip clip) {
    return std::make_shared<AudioClip>(std::move(clip));
}

/// The whine of one motor and its propeller, turning 100 times per second. Half a
/// second holds exactly 50 turns, so the clip loops without a click. The game plays
/// four of these and changes the pitch of each to follow its motor.
AudioClip motorLoop() {
    AudioClip clip;
    clip.samples.resize(size_t(clip.sampleRate / 2));
    // The turn itself, then its multiples. The second and third are the loudest: that is
    // the blades going by, the note people recognize as "a drone".
    const float strengths[8] = {0.45f, 1.0f, 0.8f, 0.4f, 0.32f, 0.2f, 0.14f, 0.09f};
    const float phases[8] = {0.0f, 1.3f, 2.9f, 0.7f, 4.1f, 2.2f, 5.0f, 3.3f};
    for (size_t i = 0; i < clip.samples.size(); i++) {
        float t = float(i) / float(clip.sampleRate), sample = 0;
        for (int n = 0; n < 8; n++) sample += strengths[n] * std::sin(2.0f * kPi * 100.0f * float(n + 1) * t + phases[n]);
        clip.samples[i] = sample * 0.11f;
    }
    return clip;
}

/// Air rushing past: noise with the hiss taken out. The end is faded into the beginning,
/// so that it loops without a seam.
AudioClip windLoop() {
    AudioClip clip;
    size_t count = size_t(clip.sampleRate) * 2, overlap = size_t(clip.sampleRate) / 5;
    std::vector<float> noise(count + overlap);
    Random random(17);
    float slow = 0, slower = 0;
    for (float& sample : noise) {
        // Two simple low-pass filters in a row.
        slow += (random.range(-1.0f, 1.0f) - slow) * 0.12f;
        slower += (slow - slower) * 0.2f;
        sample = slower * 2.4f;
    }
    clip.samples.resize(count);
    for (size_t i = 0; i < count; i++) {
        float fade = i < overlap ? float(i) / float(overlap) : 1.0f;
        clip.samples[i] = noise[i] * fade + (i < overlap ? noise[count + i] * (1.0f - fade) : 0.0f);
    }
    return clip;
}

}  // namespace

Sounds Sounds::make() {
    Sounds sounds;
    sounds.motor = share(motorLoop());
    sounds.wind = share(windLoop());
    // The start lights: three low beeps and a long high one.
    sounds.beep = share(synthesize(note(Waveform::Sine, 660.0f, 0.14f, 0.35f)));
    sounds.go = share(synthesize(note(Waveform::Sine, 1320.0f, 0.5f, 0.35f)));
    sounds.gate = share(synthesize(note(Waveform::Triangle, 1320.0f, 0.09f, 0.3f)));
    sounds.lap = share(synthesizeSequence({note(Waveform::Triangle, 1320.0f, 0.08f, 0.3f), note(Waveform::Triangle, 1760.0f, 0.16f, 0.3f)}));
    sounds.finish = share(synthesizeSequence({note(Waveform::Triangle, 523.25f, 0.13f, 0.3f), note(Waveform::Triangle, 659.25f, 0.13f, 0.3f),
                                              note(Waveform::Triangle, 783.99f, 0.13f, 0.3f), note(Waveform::Triangle, 1046.5f, 0.4f, 0.3f)}));
    // A crash: a crack of noise.
    SynthParams crack = note(Waveform::Noise, 0.0f, 0.4f, 0.6f);
    crack.attack = 0.001f;
    crack.release = 0.35f;
    crack.seed = 3;
    sounds.crash = share(synthesize(crack));
    // The battery alarm of a flight controller's buzzer.
    sounds.alarm = share(synthesizeSequence({note(Waveform::Square, 2700.0f, 0.09f, 0.12f), note(Waveform::Square, 2700.0f, 0.09f, 0.0f),
                                             note(Waveform::Square, 2700.0f, 0.09f, 0.12f)}));
    return sounds;
}

}  // namespace fpv
