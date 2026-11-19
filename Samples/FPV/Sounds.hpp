// Sounds.hpp
// Every sound of the game, computed when it starts: no audio files.
#pragma once

#include "MoltenMetal.hpp"

#include <memory>

namespace fpv {

struct Sounds {
    std::shared_ptr<molten::AudioClip> motor;       // one motor at 100 turns per second, to loop and re-pitch
    std::shared_ptr<molten::AudioClip> wind;        // rushing air, to loop
    std::shared_ptr<molten::AudioClip> beep, go, gate, lap, finish, crash, alarm;

    static Sounds make();
};

}  // namespace fpv
