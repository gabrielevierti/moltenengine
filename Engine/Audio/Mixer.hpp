// Mixer.hpp
// Turns "play this clip" into the stream of samples a sound card wants.
//
// The operating system asks for audio in small blocks, a few hundred times per second,
// on a thread of its own. Each time, the mixer walks its list of playing sounds
// ("voices"), reads the next piece of each clip at the right speed, scales it by its
// volume and pan, and adds everything together. That is all a software mixer is.
//
// Every sound plays in a group (Effects, Music, Voices, Interface, or one of the game's own),
// each with its volume, so that a settings menu can lower the music and keep the effects. A
// group can duck under another: the music goes down while someone speaks, and back up after.
//
// Rooms: the sounds of the groups that send to it go through a reverb too, set by the room the
// listener is in (a cave, a hall, a bathroom), and a sound behind a wall can be muffled.
//
// The mixer has no idea what a sound card is: Audio (in Audio.hpp) connects it to one.
// That keeps this file plain C++ that can be tested by calling render() directly.
#pragma once

#include "Audio/AudioClip.hpp"
#include "Math/Math.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace molten {

struct PlayOptions {
    float volume = 1.0f;
    float pitch = 1.0f;         // 2 is an octave higher and twice as fast, 0.5 an octave lower
    float pan = 0.0f;           // -1 left, 0 center, 1 right
    bool loops = false;
    int group = 0;              // Mixer::group("Music"); 0 is Effects
    float muffle = 0;           // 0 clear, 1 as from behind a thick wall
};

/// Identifies one playing sound. 0 means "no sound".
using SoundHandle = uint32_t;

class Mixer {
public:
    explicit Mixer(int sampleRate = 44100);

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

    // ---- groups
    /// The group of this name, made if there is none yet. Effects (0), Music, Voices and
    /// Interface are there from the start; Music ducks under Voices.
    int group(const std::string& name);
    std::vector<std::string> groupNames();
    void setGroupVolume(int group, float volume);
    float groupVolume(int group);
    void setGroupMuted(int group, bool isMuted);
    /// While anything plays in `under`, `group` goes down to `to` of its volume (0.3: a third),
    /// in `attack` seconds, and back up in `release` once it is quiet. A negative `under` stops it.
    void duck(int group, int under, float to = 0.3f, float attack = 0.15f, float release = 0.8f);
    /// How much of a group's sounds goes into the room's reverb: 0 none (music, interface), 1 all.
    void setGroupReverb(int group, float send);
    /// How far the group is ducked now: 1 not at all.
    float groupDucking(int group);
    void setGroup(SoundHandle handle, int group);
    void setMuffle(SoundHandle handle, float muffle);

    // ---- the room
    /// `size` 0 a small room to 1 a cathedral; `damping` 0 bright (stone, tiles) to 1 dull
    /// (curtains, a forest); `wet` how much of it is heard, 0 none. Changes are eased in.
    void setReverb(float size, float damping, float wet);
    float reverbWet();

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
        int group = 0;
        float muffle = 0, muffleNow = 0;   // asked for, and where it is on its way there
        float lowLeft = 0, lowRight = 0;   // the muffling filter's memory
        int fadeOutFrames = -1;     // while stopping: output frames left before silence
    };

    Voice* find(SoundHandle handle);

    struct Group {
        std::string name;
        float volume = 1;
        bool isMuted = false;
        int under = -1;                     // the group it ducks under; -1 none
        float duckTo = 0.3f, attack = 0.15f, release = 0.8f;
        float ducking = 1;                  // now: 1 not ducked
        float gain = 1;                     // volume x ducking, as of the end of the last block
        float reverb = 1;
    };
    std::vector<Group> groups_;
    int groupLocked(const std::string& name);

    // A reverb in the manner of Freeverb: combs feeding back through a low pass, then all-passes.
    struct Comb {
        std::vector<float> buffer;
        size_t at = 0;
        float low = 0;
    };
    struct AllPass {
        std::vector<float> buffer;
        size_t at = 0;
    };
    std::vector<Comb> combs_[2];
    std::vector<AllPass> allPasses_[2];
    float size_ = 0.5f, damping_ = 0.5f, wet_ = 0, wetNow_ = 0, sizeNow_ = 0.5f, dampingNow_ = 0.5f;
    std::vector<float> send_;               // what goes into the reverb, this block
    void makeReverb();

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
