// AudioClip.hpp
// Sound as numbers: a clip is an array of samples between -1 and 1.
// Clips can be decoded from files by the Audio class, or generated here from a few
// parameters, which is how the sample games get their sound effects without shipping
// a single audio file.
#pragma once

#include <cstdint>
#include <vector>

namespace molten {

struct AudioClip {
    std::vector<float> samples;         // interleaved if there is more than one channel
    int sampleRate = 44100;
    int channels = 1;

    float duration() const {
        return samples.empty() ? 0.0f : float(samples.size()) / float(sampleRate * channels);
    }
};

enum class Waveform : uint8_t { Sine, Square, Triangle, Sawtooth, Noise };

/// One synthesized note.
struct SynthParams {
    Waveform waveform = Waveform::Sine;
    float frequency = 440.0f;           // in hertz, at the start
    float frequencyEnd = 0.0f;          // if above zero, the pitch slides to this value
    float duration = 0.2f;              // seconds
    float attack = 0.005f;              // seconds to fade in: avoids a click
    float release = 0.1f;               // seconds to fade out at the end
    float volume = 0.5f;
    uint64_t seed = 1;                  // for Waveform::Noise
};

AudioClip synthesize(const SynthParams& params, int sampleRate = 44100);

/// Several notes played one after another, as one clip.
AudioClip synthesizeSequence(const std::vector<SynthParams>& notes, int sampleRate = 44100);

}  // namespace molten
