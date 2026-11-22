// GameSettings.hpp
// What a project decides once, for all of its scenes, besides how they are drawn (that is
// Quality): the tags and the layers its things can be given, which layers collide with
// which, how hard gravity pulls and how often physics steps.
//
// Kept in the project's Settings/Game.settings, a text file; changed in Edit > Project
// Settings. `World::applySettings` hands it to physics and to the clock. (Unity keeps the
// same things in its Tags and Layers, Physics and Time settings.)
#pragma once

#include "Math/Math.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace molten {

struct GameSettings {
    static constexpr int kLayerCount = 32;

    std::vector<std::string> tags;                      // besides "Untagged", which every project has
    std::array<std::string, kLayerCount> layers;        // a layer's name; empty: it has none yet
    std::array<uint32_t, kLayerCount> meets;            // bit b of meets[a]: things on layer a collide with things on layer b
    Vec3 gravity{0, -9.81f, 0};
    int stepsPerSecond = 60;                            // of physics, and of every fixedUpdate

    /// What a new project starts with: the usual tags, eight named layers, and every layer meeting every other.
    GameSettings();

    /// "Default", "Water"; "Layer 12" for one that has no name.
    std::string layerName(int layer) const;
    /// The layer with that name; -1 if there is none.
    int layerNamed(const std::string& name) const;
    /// Every tag a thing can be given, "Untagged" first.
    std::vector<std::string> allTags() const;

    bool layersMeet(int a, int b) const;
    /// Both ways: a meets b exactly when b meets a.
    void setLayersMeet(int a, int b, bool meet);

    std::string toText() const;
    /// Lines it does not know are skipped, and what a text does not say stays as a new project has it.
    static GameSettings parse(const std::string& text);

    bool operator==(const GameSettings& other) const;
    bool operator!=(const GameSettings& other) const { return !(*this == other); }
};

}  // namespace molten
