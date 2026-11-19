// Audio.hpp
// Sound output: connects the Mixer to the computer's speakers, and decodes audio files.
//
// The device work is done by miniaudio, a single-file library; on a Mac it talks to
// Core Audio. All this class does is open the default output and hand miniaudio a
// function that asks the mixer for the next block of samples.
#pragma once

#include "Audio/Mixer.hpp"

#include <memory>
#include <string>

namespace molten {

class Audio {
public:
    /// With `openDevice` false nothing is played: used when rendering without a window.
    explicit Audio(bool openDevice = true);
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    /// False if no output device could be opened. Playing sounds is then harmless and silent.
    bool isAvailable() const;

    SoundHandle play(const std::shared_ptr<const AudioClip>& clip, const PlayOptions& options = {}) {
        return mixer_.play(clip, options);
    }
    void stop(SoundHandle handle) { mixer_.stop(handle); }

    /// Everything else: volume, pitch and pan of a playing sound, the master volume.
    Mixer& mixer() { return mixer_; }

    /// Decodes a WAV, FLAC or MP3 file into memory. Returns nullptr and logs if it cannot.
    static std::shared_ptr<AudioClip> loadClip(const std::string& path);

private:
    struct Device;                      // keeps miniaudio out of this header
    Mixer mixer_;
    std::unique_ptr<Device> device_;
};

}  // namespace molten
