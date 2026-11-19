#include "Audio/AudioClip.hpp"

#include "Math/Math.hpp"

namespace molten {

AudioClip synthesize(const SynthParams& params, int sampleRate) {
    AudioClip clip;
    clip.sampleRate = sampleRate;
    int count = std::max(int(params.duration * float(sampleRate)), 1);
    clip.samples.resize(size_t(count));

    Random random(params.seed);
    float phase = 0.0f;                                 // position inside one cycle, 0..1
    float startFrequency = std::max(params.frequency, 1.0f);
    float endFrequency = params.frequencyEnd > 0.0f ? params.frequencyEnd : startFrequency;

    for (int i = 0; i < count; i++) {
        float time = float(i) / float(sampleRate);
        float progress = float(i) / float(count);

        // Slide the pitch along a musical (exponential) curve and advance the phase by
        // one sample's worth of the current frequency.
        float frequency = startFrequency * std::pow(endFrequency / startFrequency, progress);
        phase += frequency / float(sampleRate);
        phase -= std::floor(phase);

        float value = 0.0f;
        switch (params.waveform) {
        case Waveform::Sine:     value = std::sin(phase * 2.0f * kPi); break;
        case Waveform::Square:   value = phase < 0.5f ? 1.0f : -1.0f; break;
        case Waveform::Triangle: value = 1.0f - 4.0f * std::abs(phase - 0.5f); break;
        case Waveform::Sawtooth: value = 2.0f * phase - 1.0f; break;
        case Waveform::Noise:    value = random.range(-1.0f, 1.0f); break;
        }

        // Envelope: fade in over `attack`, fade out over the last `release` seconds.
        float envelope = 1.0f;
        if (params.attack > 0.0f) envelope = std::min(envelope, time / params.attack);
        if (params.release > 0.0f) envelope = std::min(envelope, (params.duration - time) / params.release);
        clip.samples[size_t(i)] = value * saturate(envelope) * params.volume;
    }
    return clip;
}

AudioClip synthesizeSequence(const std::vector<SynthParams>& notes, int sampleRate) {
    AudioClip clip;
    clip.sampleRate = sampleRate;
    for (const SynthParams& note : notes) {
        AudioClip part = synthesize(note, sampleRate);
        clip.samples.insert(clip.samples.end(), part.samples.begin(), part.samples.end());
    }
    return clip;
}

}  // namespace molten
