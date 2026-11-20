// AudioEdit.hpp
// Changes to a sound: louder, faded, reversed, cut, filtered. Each works on a stretch of
// the clip, from one moment to another in seconds, and changes the clip it is given.
// These are what the editor's sound viewer offers, and a game can use them on clips it
// makes or loads.
#pragma once

#include "Audio/AudioClip.hpp"

namespace molten::audio {

/// The loudest sample of a stretch, 0 to 1 (and above, if it clips).
float peak(const AudioClip& clip, float from, float to);

/// Multiplies a stretch by `factor`. In decibels: `gain(clip, from, to, decibels(-6))`.
void gain(AudioClip& clip, float from, float to, float factor);
float decibels(float db);

/// Makes the stretch as loud as it can be without clipping (its peak becomes `level`).
void normalize(AudioClip& clip, float from, float to, float level = 0.98f);

/// From silence up to what it is, and from what it is down to silence, over the stretch.
void fadeIn(AudioClip& clip, float from, float to);
void fadeOut(AudioClip& clip, float from, float to);

/// The stretch played backward.
void reverse(AudioClip& clip, float from, float to);

/// The stretch made silent, its length kept.
void silence(AudioClip& clip, float from, float to);

/// Keeps only the stretch, or takes it out and joins what is left.
void trim(AudioClip& clip, float from, float to);
void remove(AudioClip& clip, float from, float to);

/// Lets through what is below, or above, a pitch in hertz, 12 decibels an octave.
void lowPass(AudioClip& clip, float from, float to, float hertz);
void highPass(AudioClip& clip, float from, float to, float hertz);

/// Two channels made one (their average); one made two (the same on both).
void toMono(AudioClip& clip);
void toStereo(AudioClip& clip);

/// Read through faster or slower: 2 is an octave up and half as long.
void changeSpeed(AudioClip& clip, float speed);

/// Makes its end meet its beginning without a click, by blending the last `seconds` into
/// the first: the clip gets that much shorter and can be played around and around.
void makeLoop(AudioClip& clip, float seconds);

}  // namespace molten::audio
