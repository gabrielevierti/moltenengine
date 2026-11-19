// Sounds.hpp
// The game's sound effects. None of them is a file: each is computed when the game
// starts from a few numbers (a waveform, a pitch that may slide, a duration).
#pragma once

#include "MoltenMetal.hpp"

struct Sounds {
    std::shared_ptr<molten::AudioClip> collect;     // a gem was picked up
    std::shared_ptr<molten::AudioClip> jump;
    std::shared_ptr<molten::AudioClip> land;        // the marble hit something
    std::shared_ptr<molten::AudioClip> fall;        // the marble fell off the level
    std::shared_ptr<molten::AudioClip> win;
    std::shared_ptr<molten::AudioClip> roll;        // loops while the marble rolls

    static Sounds make();
};
