#include "Audio/Mixer.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

SoundHandle Mixer::play(std::shared_ptr<const AudioClip> clip, const PlayOptions& options) {
    if (!clip || clip->samples.empty() || clip->channels < 1) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (int(voices_.size()) >= kMaxVoices) return 0;
    Voice voice;
    voice.handle = nextHandle_++;
    if (nextHandle_ == 0) nextHandle_ = 1;
    voice.clip = std::move(clip);
    voice.volume = options.volume;
    voice.pitch = options.pitch;
    voice.pan = options.pan;
    voice.loops = options.loops;
    voices_.push_back(std::move(voice));
    return voices_.back().handle;
}

Mixer::Voice* Mixer::find(SoundHandle handle) {
    for (Voice& voice : voices_) {
        if (voice.handle == handle) return &voice;
    }
    return nullptr;
}

void Mixer::stop(SoundHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    Voice* voice = find(handle);
    if (voice && voice->fadeOutFrames < 0) voice->fadeOutFrames = kFadeFrames;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& voice : voices_) {
        if (voice.fadeOutFrames < 0) voice.fadeOutFrames = kFadeFrames;
    }
}

void Mixer::setVolume(SoundHandle handle, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* voice = find(handle)) voice->volume = volume;
}

void Mixer::setPitch(SoundHandle handle, float pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* voice = find(handle)) voice->pitch = pitch;
}

void Mixer::setPan(SoundHandle handle, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* voice = find(handle)) voice->pan = pan;
}

bool Mixer::isPlaying(SoundHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(handle) != nullptr;
}

int Mixer::activeVoiceCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return int(voices_.size());
}

void Mixer::setMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    masterVolume_ = volume;
}

float Mixer::masterVolume() {
    std::lock_guard<std::mutex> lock(mutex_);
    return masterVolume_;
}

void Mixer::render(float* output, int frameCount) {
    std::fill(output, output + size_t(frameCount) * 2, 0.0f);
    std::lock_guard<std::mutex> lock(mutex_);

    for (size_t v = 0; v < voices_.size();) {
        Voice& voice = voices_[v];
        const AudioClip& clip = *voice.clip;
        const float* samples = clip.samples.data();
        int channels = clip.channels;
        double clipFrames = double(clip.samples.size() / size_t(channels));
        // How far to move through the clip for every frame of output.
        double step = double(std::max(voice.pitch, 0.01f)) * double(clip.sampleRate) / double(sampleRate_);
        // Balance: at the center both sides play at full volume; panning lowers the far side.
        float pan = std::clamp(voice.pan, -1.0f, 1.0f);
        float left = voice.volume * std::min(1.0f, 1.0f - pan);
        float right = voice.volume * std::min(1.0f, 1.0f + pan);

        bool isFinished = false;
        for (int frame = 0; frame < frameCount; frame++) {
            if (voice.position >= clipFrames) {
                if (!voice.loops) {
                    isFinished = true;
                    break;
                }
                voice.position = std::fmod(voice.position, clipFrames);
            }
            // The position falls between two samples of the clip: blend them.
            size_t index = size_t(voice.position);
            size_t next = index + 1;
            if (next >= size_t(clipFrames)) next = voice.loops ? 0 : index;
            float blend = float(voice.position - double(index));
            float sampleLeft = lerp(samples[index * size_t(channels)], samples[next * size_t(channels)], blend);
            float sampleRight = sampleLeft;
            if (channels > 1) {
                sampleRight = lerp(samples[index * size_t(channels) + 1], samples[next * size_t(channels) + 1], blend);
            }

            float fade = 1.0f;
            if (voice.fadeOutFrames >= 0) {
                fade = float(voice.fadeOutFrames) / float(kFadeFrames);
                if (--voice.fadeOutFrames < 0) {
                    isFinished = true;
                    break;
                }
            }
            output[frame * 2] += sampleLeft * left * fade;
            output[frame * 2 + 1] += sampleRight * right * fade;
            voice.position += step;
        }

        if (isFinished) {
            voices_[v] = std::move(voices_.back());     // order does not matter: fill the hole with the last voice
            voices_.pop_back();
        } else {
            v++;
        }
    }

    // Many loud sounds at once can add up to more than a sample can hold.
    for (int i = 0; i < frameCount * 2; i++) {
        output[i] = std::clamp(output[i] * masterVolume_, -1.0f, 1.0f);
    }
}

}  // namespace molten
