// SaveGame.hpp
// A saved game: numbers, words and positions by name, written to a file of its own in the
// game's folder (beside its preferences) and read back the next time it is played.
//
//   SaveGame save;
//   save.setNumber("level", 3);
//   save.setVector("player", position);
//   world.saveGame(save);                       // the slot "Save"; or world.saveGame(save, "Slot 2")
//
//   if (std::optional<SaveGame> saved = world.loadGame()) {
//       int level = int(saved->number("level", 1));
//   }
//
// Preferences are for settings and records; a save game is a game in progress, of which a
// player may keep several.
#pragma once

#include "Math/Math.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace molten {

class SaveGame {
public:
    void setNumber(const std::string& name, float value);
    void setText(const std::string& name, const std::string& value);
    void setVector(const std::string& name, Vec3 value);
    float number(const std::string& name, float fallback = 0.0f) const;
    std::string text(const std::string& name, const std::string& fallback = "") const;
    Vec3 vector(const std::string& name, Vec3 fallback = Vec3{0, 0, 0}) const;
    bool has(const std::string& name) const;
    void remove(const std::string& name);
    std::vector<std::string> names() const;
    bool isEmpty() const { return values_.empty(); }

    /// As the file has it: a line per value ("number level 3", "text name \"Ann\"", "vector player 1 2 3").
    std::string toText() const;
    /// Reads what toText() wrote; lines it does not understand are passed over.
    static SaveGame fromText(const std::string& text);

private:
    struct Value {
        enum class Kind { Number, Text, Vector } kind = Kind::Number;
        float number = 0;
        std::string text;
        Vec3 vector{0, 0, 0};
    };
    std::map<std::string, Value> values_;
};

}  // namespace molten
