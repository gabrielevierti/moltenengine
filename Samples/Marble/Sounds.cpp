#include "Sounds.hpp"

using namespace molten;

namespace {

SynthParams note(Waveform waveform, float frequency, float duration, float volume) {
    SynthParams params;
    params.waveform = waveform;
    params.frequency = frequency;
    params.duration = duration;
    params.volume = volume;
    params.release = duration * 0.6f;
    return params;
}

std::shared_ptr<AudioClip> share(AudioClip clip) {
    return std::make_shared<AudioClip>(std::move(clip));
}

}  // namespace

Sounds Sounds::make() {
    Sounds sounds;

    // Two quick notes a fifth apart: the classic "got it" chime.
    sounds.collect = share(synthesizeSequence({note(Waveform::Triangle, 880.0f, 0.07f, 0.35f),
                                               note(Waveform::Triangle, 1318.5f, 0.22f, 0.35f)}));

    // A pitch that slides up reads as "going up"; down, as falling.
    SynthParams jump = note(Waveform::Sine, 260.0f, 0.16f, 0.4f);
    jump.frequencyEnd = 520.0f;
    sounds.jump = share(synthesize(jump));

    SynthParams fall = note(Waveform::Sawtooth, 440.0f, 0.6f, 0.25f);
    fall.frequencyEnd = 70.0f;
    sounds.fall = share(synthesize(fall));

    // A very short low thump.
    SynthParams land = note(Waveform::Sine, 110.0f, 0.09f, 0.6f);
    land.frequencyEnd = 55.0f;
    land.attack = 0.002f;
    sounds.land = share(synthesize(land));

    // A rising major arpeggio: C, E, G, C.
    sounds.win = share(synthesizeSequence({note(Waveform::Triangle, 523.25f, 0.12f, 0.35f), note(Waveform::Triangle, 659.25f, 0.12f, 0.35f),
                                           note(Waveform::Triangle, 783.99f, 0.12f, 0.35f), note(Waveform::Triangle, 1046.5f, 0.5f, 0.35f)}));

    // One second of noise. Played far below its recorded speed it becomes a low rumble.
    SynthParams roll = note(Waveform::Noise, 0.0f, 1.0f, 0.5f);
    roll.attack = 0.0f;
    roll.release = 0.0f;
    roll.seed = 5;
    sounds.roll = share(synthesize(roll));

    return sounds;
}
