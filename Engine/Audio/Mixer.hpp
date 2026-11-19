// Mixer.hpp
// Turns "play this clip" into the stream of samples a sound card wants.
//
// The operating system asks for audio in small blocks, a few hundred times per second,
// on a thread of its own. Each time, the mixer walks its list of playing sounds
// ("voices"), reads the next piece of each clip at the right speed, scales it by its
// volume and pan, and adds everything together. That is all a software mixer is.
//
// The mixer has no idea what a sound card is: Audio (in Audio.hpp) connects it to one.
// That keeps this file plain C++ that can be tested by calling render() directly.
#pragma once

#include "Audio/AudioClip.hpp"
#include "Math/Math.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace molten {

struct PlayOptions {
    float volume = 1.0f;
    float pitch = 1.0f;         // 2 is an octave higher and twice as fast, 0.5 an octave lower
    float pan = 0.0f;           // -1 left, 0 center, 1 right
    bool loops = false;
};

/// Identifies one playing sound. 0 means "no sound".
using SoundHandle = uint32_t;

class Mixer {
public:
    explicit Mixer(int sampleRate = 44100) : sampleRate_(sampleRate) {}

    int sampleRate() const { return sampleRate_; }

    /// Starts playing a clip. Returns 0 if the clip is empty or too many sounds are playing.
    SoundHandle play(std::shared_ptr<const AudioClip> clip, const PlayOptions& options = {});

    /// Stops a sound with a very short fade, so that cutting it does not click.
    void stop(SoundHandle handle);
    void stopAll();

    void setVolume(SoundHandle handle, float volume);
    void setPitch(SoundHandle handle, float pitch);
    void setPan(SoundHandle handle, float pan);
    bool isPlaying(SoundHandle handle);
    int activeVoiceCount();

    void setMasterVolume(float volume);
    float masterVolume();

    /// Writes `frameCount` stereo frames to `output` (left, right, left, right, ...).
    /// This is what the audio thread calls.
    void render(float* output, int frameCount);

    static constexpr int kMaxVoices = 64;

private:
    struct Voice {
        SoundHandle handle = 0;
        std::shared_ptr<const AudioClip> clip;
        double position = 0;        // in frames of the clip; fractional, because of pitch
        float volume = 1, pitch = 1, pan = 0;
        bool loops = false;
        int fadeOutFrames = -1;     // while stopping: output frames left before silence
    };

    Voice* find(SoundHandle handle);

    static constexpr int kFadeFrames = 256;     // about 6 milliseconds

    int sampleRate_;
    float masterVolume_ = 1.0f;
    SoundHandle nextHandle_ = 1;
    std::vector<Voice> voices_;
    std::mutex mutex_;              // the game thread and the audio thread both touch voices_
};

/// The volume and pan that make a sound seem to come from a place in the world.
struct SpatialGain {
    float volume = 1;
    float pan = 0;
};

/// `listenerWorld` is the world matrix of whoever is listening, usually the camera.
/// The sound plays at full volume within `fullVolumeDistance` and is silent at `maxDistance`.
inline SpatialGain spatialize(const Mat4& listenerWorld, Vec3 position, float fullVolumeDistance = 2.0f, float maxDistance = 40.0f) {
    Vec3 toSound = position - xyz(listenerWorld.columns[3]);
    float distance = simd::length(toSound);
    float t = saturate((distance - fullVolumeDistance) / std::max(maxDistance - fullVolumeDistance, 0.001f));
    SpatialGain gain;
    gain.volume = (1.0f - t) * (1.0f - t);
    if (distance > 0.0001f) {
        Vec3 right = safeNormalize(xyz(listenerWorld.columns[0]), Vec3{1, 0, 0});
        gain.pan = simd::dot(toSound / distance, right) * 0.8f;     // never entirely in one ear
    }
    return gain;
}

}  // namespace molten
