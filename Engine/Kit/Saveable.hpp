// Saveable.hpp
// Saving a game in progress without writing what to save: everything with a Saveable is kept
// as it is (where it is, whether it is on, its components' values, whether it was destroyed),
// with the shared values and texts and the scene it is in.
//
//   kit::saveState(world);                 // the slot "Save"
//   kit::loadState(world);                 // the scene again, as it was saved
//
// Or from the When / Then actions "Save game" and "Load game", or the Save Game and Load Game
// nodes of visual scripts. What was spawned while playing is not made again.
#pragma once

#include "Core/SaveGame.hpp"
#include "Core/World.hpp"

#include <string>

namespace molten::kit {

struct Saveable {
    std::string id;                     // what it is known by in the save; empty: its path of names ("Level/Chest")
    bool keepsPlace = true;             // where it is, how it is turned and sized
    bool keepsValues = true;            // the values of its other components (a door's state, a chest's coins)
};

/// Everything saveable, as it is now.
SaveGame captureState(World& world);
/// Puts back what `save` kept, in the scene loaded now. False if it is not a state.
bool restoreState(World& world, const SaveGame& save);
/// Captures and writes the state to a slot.
bool saveState(World& world, const std::string& slot = "Save");
/// Reads a slot, loads its scene again from its file at the end of the frame, and puts back the
/// state. False if there is no such save.
bool loadState(World& world, const std::string& slot = "Save");

void linkSaveable();

}  // namespace molten::kit
