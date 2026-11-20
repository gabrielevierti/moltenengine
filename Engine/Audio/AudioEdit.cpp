// AudioEdit.cpp
#include "Audio/AudioEdit.hpp"

#include <algorithm>
#include <cmath>

namespace molten::audio {

namespace {

struct Stretch {
    size_t first = 0, last = 0, channels = 1;       // frames: [first, last)
    size_t frames() const { return last - first; }
};

Stretch stretchOf(const AudioClip& clip, float from, float to) {
    Stretch s;
    s.channels = size_t(std::max(clip.channels, 1));
    size_t total = clip.samples.size() / s.channels;
    auto frameAt = [&](float seconds) { return std::min(size_t(std::max(seconds, 0.0f) * float(clip.sampleRate) + 0.5f), total); };
    s.first = frameAt(std::min(from, to));
    s.last = frameAt(std::max(from, to));
    return s;
}

template <typename Change>
void eachSample(AudioClip& clip, float from, float to, Change change) {
    Stretch s = stretchOf(clip, from, to);
    for (size_t frame = s.first; frame < s.last; frame++) {
        float along = s.frames() > 1 ? float(frame - s.first) / float(s.frames() - 1) : 0.0f;
        for (size_t channel = 0; channel < s.channels; channel++) {
            float& sample = clip.samples[frame * s.channels + channel];
            sample = change(sample, along);
        }
    }
}

/// One pass of a filter that follows its input slowly: what it gives is the low part.
void follow(AudioClip& clip, const Stretch& s, float hertz, bool keepsLow) {
    float step = 1.0f - std::exp(-2.0f * 3.14159265f * std::max(hertz, 1.0f) / float(clip.sampleRate));
    for (size_t channel = 0; channel < s.channels; channel++) {
        float low = s.first < s.last ? clip.samples[s.first * s.channels + channel] : 0.0f;
        for (size_t frame = s.first; frame < s.last; frame++) {
            float& sample = clip.samples[frame * s.channels + channel];
            low += (sample - low) * step;
            sample = keepsLow ? low : sample - low;
        }
    }
}

}  // namespace

float decibels(float db) { return std::pow(10.0f, db / 20.0f); }

float peak(const AudioClip& clip, float from, float to) {
    Stretch s = stretchOf(clip, from, to);
    float loudest = 0;
    for (size_t i = s.first * s.channels; i < s.last * s.channels; i++) loudest = std::max(loudest, std::abs(clip.samples[i]));
    return loudest;
}

void gain(AudioClip& clip, float from, float to, float factor) {
    eachSample(clip, from, to, [&](float sample, float) { return sample * factor; });
}

void normalize(AudioClip& clip, float from, float to, float level) {
    float loudest = peak(clip, from, to);
    if (loudest > 1e-6f) gain(clip, from, to, level / loudest);
}

void fadeIn(AudioClip& clip, float from, float to) {
    // The curve of an equal-power fade: quick at first to the ear, never a step.
    eachSample(clip, from, to, [](float sample, float along) { return sample * std::sin(along * 1.5707963f); });
}

void fadeOut(AudioClip& clip, float from, float to) {
    eachSample(clip, from, to, [](float sample, float along) { return sample * std::cos(along * 1.5707963f); });
}

void reverse(AudioClip& clip, float from, float to) {
    Stretch s = stretchOf(clip, from, to);
    for (size_t i = 0; i < s.frames() / 2; i++) {
        for (size_t channel = 0; channel < s.channels; channel++) {
            std::swap(clip.samples[(s.first + i) * s.channels + channel], clip.samples[(s.last - 1 - i) * s.channels + channel]);
        }
    }
}

void silence(AudioClip& clip, float from, float to) {
    eachSample(clip, from, to, [](float, float) { return 0.0f; });
}

void trim(AudioClip& clip, float from, float to) {
    Stretch s = stretchOf(clip, from, to);
    clip.samples.erase(clip.samples.begin() + long(s.last * s.channels), clip.samples.end());
    clip.samples.erase(clip.samples.begin(), clip.samples.begin() + long(s.first * s.channels));
}

void remove(AudioClip& clip, float from, float to) {
    Stretch s = stretchOf(clip, from, to);
    clip.samples.erase(clip.samples.begin() + long(s.first * s.channels), clip.samples.begin() + long(s.last * s.channels));
}

void lowPass(AudioClip& clip, float from, float to, float hertz) {
    Stretch s = stretchOf(clip, from, to);
    follow(clip, s, hertz, true);
    follow(clip, s, hertz, true);
}

void highPass(AudioClip& clip, float from, float to, float hertz) {
    Stretch s = stretchOf(clip, from, to);
    follow(clip, s, hertz, false);
    follow(clip, s, hertz, false);
}

void toMono(AudioClip& clip) {
    if (clip.channels <= 1) return;
    size_t channels = size_t(clip.channels), frames = clip.samples.size() / channels;
    std::vector<float> mixed(frames);
    for (size_t frame = 0; frame < frames; frame++) {
        float sum = 0;
        for (size_t channel = 0; channel < channels; channel++) sum += clip.samples[frame * channels + channel];
        mixed[frame] = sum / float(channels);
    }
    clip.samples = std::move(mixed);
    clip.channels = 1;
}

void toStereo(AudioClip& clip) {
    if (clip.channels != 1) return;
    std::vector<float> both(clip.samples.size() * 2);
    for (size_t frame = 0; frame < clip.samples.size(); frame++) both[frame * 2] = both[frame * 2 + 1] = clip.samples[frame];
    clip.samples = std::move(both);
    clip.channels = 2;
}

void changeSpeed(AudioClip& clip, float speed) {
    speed = std::clamp(speed, 0.05f, 20.0f);
    size_t channels = size_t(std::max(clip.channels, 1)), frames = clip.samples.size() / channels;
    if (frames < 2 || std::abs(speed - 1) < 1e-4f) return;
    std::vector<float> bent;
    bent.reserve(size_t(double(frames) / double(speed) + 2) * channels);
    for (double at = 0; at + 1 < double(frames); at += double(speed)) {
        size_t frame = size_t(at);
        float share = float(at - double(frame));
        for (size_t channel = 0; channel < channels; channel++) {
            bent.push_back(clip.samples[frame * channels + channel] * (1 - share) + clip.samples[(frame + 1) * channels + channel] * share);
        }
    }
    clip.samples = std::move(bent);
}

void makeLoop(AudioClip& clip, float seconds) {
    size_t channels = size_t(std::max(clip.channels, 1)), frames = clip.samples.size() / channels;
    size_t blend = std::min(size_t(std::max(seconds, 0.0f) * float(clip.sampleRate)), frames / 2);
    if (blend == 0) return;
    // The tail fades out over the head fading in; then the tail is cut off.
    size_t kept = frames - blend;
    for (size_t i = 0; i < blend; i++) {
        float along = float(i) / float(blend);
        float in = std::sin(along * 1.5707963f), out = std::cos(along * 1.5707963f);
        for (size_t channel = 0; channel < channels; channel++) {
            float& head = clip.samples[i * channels + channel];
            head = head * in + clip.samples[(kept + i) * channels + channel] * out;
        }
    }
    clip.samples.resize(kept * channels);
}

}  // namespace molten::audio
