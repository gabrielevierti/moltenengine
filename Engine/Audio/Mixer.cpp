#include "Audio/Mixer.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

Mixer::Mixer(int sampleRate) : sampleRate_(sampleRate) {
    for (const char* name : {"Effects", "Music", "Voices", "Interface"}) groups_.push_back(Group{name});
    groups_[1].reverb = 0;
    groups_[3].reverb = 0;
    groups_[1].under = 2;                   // the music goes down while someone speaks
    makeReverb();
}

void Mixer::makeReverb() {
    // Freeverb's lengths, at 44.1 kHz, the right a little longer than the left.
    const int combs[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    const int allPasses[] = {556, 441, 341, 225};
    double scale = double(sampleRate_) / 44100.0;
    for (int side = 0; side < 2; side++) {
        combs_[side].clear();
        allPasses_[side].clear();
        for (int length : combs) combs_[side].push_back(Comb{std::vector<float>(size_t(double(length + side * 23) * scale) + 1, 0.0f)});
        for (int length : allPasses) allPasses_[side].push_back(AllPass{std::vector<float>(size_t(double(length + side * 23) * scale) + 1, 0.0f)});
    }
}

int Mixer::groupLocked(const std::string& name) {
    for (size_t i = 0; i < groups_.size(); i++) {
        if (groups_[i].name == name) return int(i);
    }
    groups_.push_back(Group{name});
    return int(groups_.size() - 1);
}

int Mixer::group(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return name.empty() ? 0 : groupLocked(name);
}

std::vector<std::string> Mixer::groupNames() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const Group& group : groups_) names.push_back(group.name);
    return names;
}

void Mixer::setGroupVolume(int group, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (group >= 0 && size_t(group) < groups_.size()) groups_[size_t(group)].volume = std::max(volume, 0.0f);
}

float Mixer::groupVolume(int group) {
    std::lock_guard<std::mutex> lock(mutex_);
    return group >= 0 && size_t(group) < groups_.size() ? groups_[size_t(group)].volume : 1.0f;
}

void Mixer::setGroupMuted(int group, bool isMuted) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (group >= 0 && size_t(group) < groups_.size()) groups_[size_t(group)].isMuted = isMuted;
}

void Mixer::duck(int group, int under, float to, float attack, float release) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (group < 0 || size_t(group) >= groups_.size()) return;
    Group& g = groups_[size_t(group)];
    g.under = under >= 0 && size_t(under) < groups_.size() && under != group ? under : -1;
    g.duckTo = std::clamp(to, 0.0f, 1.0f);
    g.attack = std::max(attack, 0.001f);
    g.release = std::max(release, 0.001f);
}

void Mixer::setGroupReverb(int group, float send) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (group >= 0 && size_t(group) < groups_.size()) groups_[size_t(group)].reverb = std::clamp(send, 0.0f, 1.0f);
}

float Mixer::groupDucking(int group) {
    std::lock_guard<std::mutex> lock(mutex_);
    return group >= 0 && size_t(group) < groups_.size() ? groups_[size_t(group)].ducking : 1.0f;
}

void Mixer::setGroup(SoundHandle handle, int group) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* voice = find(handle)) voice->group = group >= 0 && size_t(group) < groups_.size() ? group : 0;
}

void Mixer::setMuffle(SoundHandle handle, float muffle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* voice = find(handle)) voice->muffle = std::clamp(muffle, 0.0f, 1.0f);
}

void Mixer::setReverb(float size, float damping, float wet) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_ = std::clamp(size, 0.0f, 1.0f);
    damping_ = std::clamp(damping, 0.0f, 1.0f);
    wet_ = std::clamp(wet, 0.0f, 1.0f);
}

float Mixer::reverbWet() {
    std::lock_guard<std::mutex> lock(mutex_);
    return wet_;
}

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
    voice.group = options.group >= 0 && size_t(options.group) < groups_.size() ? options.group : 0;
    voice.muffle = voice.muffleNow = std::clamp(options.muffle, 0.0f, 1.0f);
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
    float seconds = float(frameCount) / float(sampleRate_);

    // Each group's gain over this block: from where it was to where its volume and ducking say,
    // eased across the block so that a change does not click.
    std::vector<bool> isSounding(groups_.size(), false);
    for (const Voice& voice : voices_) {
        if (voice.fadeOutFrames < 0 && voice.volume > 0.001f) isSounding[size_t(voice.group)] = true;
    }
    std::vector<float> gainFrom(groups_.size()), gainTo(groups_.size()), sendOf(groups_.size());
    for (size_t g = 0; g < groups_.size(); g++) {
        Group& group = groups_[g];
        float target = group.under >= 0 && isSounding[size_t(group.under)] ? group.duckTo : 1.0f;
        float time = target < group.ducking ? group.attack : group.release;
        group.ducking += (target - group.ducking) * (1.0f - std::exp(-seconds / time));
        gainFrom[g] = group.gain;
        group.gain = group.isMuted ? 0.0f : group.volume * group.ducking;
        gainTo[g] = group.gain;
        sendOf[g] = group.reverb;
    }
    send_.assign(size_t(frameCount) * 2, 0.0f);
    bool anySend = false;
    // A muffled sound loses its highs: a low pass from 20 kHz (clear) down to about 400 Hz.
    auto lowPassFor = [&](float muffle) {
        float hertz = 20000.0f * std::pow(400.0f / 20000.0f, muffle);
        return 1.0f - std::exp(-2.0f * 3.14159265f * hertz / float(sampleRate_));
    };
    float muffleStep = 1.0f - std::exp(-seconds / 0.08f);

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
        size_t g = size_t(voice.group);
        float send = sendOf[g];
        float muffleFrom = voice.muffleNow;
        voice.muffleNow += (voice.muffle - voice.muffleNow) * muffleStep;
        bool filters = muffleFrom > 0.001f || voice.muffleNow > 0.001f;
        float alphaFrom = lowPassFor(muffleFrom), alphaTo = lowPassFor(voice.muffleNow);
        // Muffled is quieter too: a wall takes some of the sound, not only its highs.
        float quietFrom = 1.0f - 0.5f * muffleFrom, quietTo = 1.0f - 0.5f * voice.muffleNow;

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
            float through = float(frame) / float(frameCount);
            if (filters) {
                float alpha = alphaFrom + (alphaTo - alphaFrom) * through;
                voice.lowLeft += (sampleLeft - voice.lowLeft) * alpha;
                voice.lowRight += (sampleRight - voice.lowRight) * alpha;
                float quiet = quietFrom + (quietTo - quietFrom) * through;
                sampleLeft = voice.lowLeft * quiet;
                sampleRight = voice.lowRight * quiet;
            } else {
                voice.lowLeft = sampleLeft;
                voice.lowRight = sampleRight;
            }

            float fade = 1.0f;
            if (voice.fadeOutFrames >= 0) {
                fade = float(voice.fadeOutFrames) / float(kFadeFrames);
                if (--voice.fadeOutFrames < 0) {
                    isFinished = true;
                    break;
                }
            }
            fade *= gainFrom[g] + (gainTo[g] - gainFrom[g]) * through;
            float outLeft = sampleLeft * left * fade, outRight = sampleRight * right * fade;
            output[frame * 2] += outLeft;
            output[frame * 2 + 1] += outRight;
            if (send > 0) {
                send_[size_t(frame) * 2] += outLeft * send;
                send_[size_t(frame) * 2 + 1] += outRight * send;
                anySend = true;
            }
            voice.position += step;
        }

        if (isFinished) {
            voices_[v] = std::move(voices_.back());     // order does not matter: fill the hole with the last voice
            voices_.pop_back();
        } else {
            v++;
        }
    }

    // The room. Eased toward what it was asked to be; it keeps ringing after its sounds stop.
    float ease = 1.0f - std::exp(-seconds / 0.3f);
    float wetFrom = wetNow_;
    wetNow_ += (wet_ - wetNow_) * ease;
    sizeNow_ += (size_ - sizeNow_) * ease;
    dampingNow_ += (damping_ - dampingNow_) * ease;
    if (wetFrom > 0.0005f || wetNow_ > 0.0005f) {
        float feedback = 0.7f + 0.28f * sizeNow_;
        float damp = 0.05f + 0.6f * dampingNow_;
        for (int frame = 0; frame < frameCount; frame++) {
            float wet = (wetFrom + (wetNow_ - wetFrom) * float(frame) / float(frameCount)) * 0.6f;
            for (int side = 0; side < 2; side++) {
                float in = anySend ? (send_[size_t(frame) * 2] + send_[size_t(frame) * 2 + 1]) * 0.015f : 0.0f;
                float out = 0;
                for (Comb& comb : combs_[side]) {
                    float delayed = comb.buffer[comb.at];
                    comb.low = delayed * (1 - damp) + comb.low * damp;
                    comb.buffer[comb.at] = in + comb.low * feedback;
                    if (++comb.at >= comb.buffer.size()) comb.at = 0;
                    out += delayed;
                }
                for (AllPass& pass : allPasses_[side]) {
                    float delayed = pass.buffer[pass.at];
                    pass.buffer[pass.at] = out + delayed * 0.5f;
                    out = delayed - out;
                    if (++pass.at >= pass.buffer.size()) pass.at = 0;
                }
                output[frame * 2 + side] += out * wet;
            }
        }
    }

    // Many loud sounds at once can add up to more than a sample can hold.
    for (int i = 0; i < frameCount * 2; i++) {
        output[i] = std::clamp(output[i] * masterVolume_, -1.0f, 1.0f);
    }
}

}  // namespace molten
