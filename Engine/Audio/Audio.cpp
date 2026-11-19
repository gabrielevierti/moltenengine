#include "Audio/Audio.hpp"

#include "Core/Log.hpp"

#include <miniaudio.h>

namespace molten {

struct Audio::Device {
    ma_device device;
    bool isOpen = false;
};

namespace {

// Called by miniaudio on the audio thread whenever the sound card needs more samples.
void fillOutput(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
    static_cast<Mixer*>(device->pUserData)->render(static_cast<float*>(output), int(frameCount));
}

}  // namespace

Audio::Audio(bool openDevice) : device_(std::make_unique<Device>()) {
    if (!openDevice) return;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;             // samples as floats from -1 to 1
    config.playback.channels = 2;
    config.sampleRate = ma_uint32(mixer_.sampleRate());
    config.dataCallback = fillOutput;
    config.pUserData = &mixer_;

    if (ma_device_init(nullptr, &config, &device_->device) != MA_SUCCESS) {
        logWarning("no audio output device: the game will be silent");
        return;
    }
    if (ma_device_start(&device_->device) != MA_SUCCESS) {
        logWarning("the audio device did not start: the game will be silent");
        ma_device_uninit(&device_->device);
        return;
    }
    device_->isOpen = true;
}

Audio::~Audio() {
    if (device_->isOpen) ma_device_uninit(&device_->device);       // stops the audio thread first
}

bool Audio::isAvailable() const {
    return device_->isOpen;
}

std::shared_ptr<AudioClip> Audio::loadClip(const std::string& path) {
    // Ask for float samples; zero means "keep the file's own channel count and sample rate".
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frameCount = 0;
    void* frames = nullptr;
    if (ma_decode_file(path.c_str(), &config, &frameCount, &frames) != MA_SUCCESS || !frames) {
        logError("cannot load sound %s", path.c_str());
        return nullptr;
    }
    auto clip = std::make_shared<AudioClip>();
    clip->channels = int(config.channels);
    clip->sampleRate = int(config.sampleRate);
    const float* samples = static_cast<const float*>(frames);
    clip->samples.assign(samples, samples + frameCount * config.channels);
    ma_free(frames, nullptr);
    return clip;
}

}  // namespace molten
