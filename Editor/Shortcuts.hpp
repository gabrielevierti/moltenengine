// Shortcuts.hpp
// The editor's keyboard shortcuts, each one changeable (Molten Engine > Preferences > Shortcuts) and
// kept between runs. Written as people read them: "Shift+Cmd+S".
#pragma once

#include <string>
#include <vector>

namespace molten::editor {

struct KeyChord {
    bool command = false, shift = false, alt = false;
    std::string key;                    // as ImGui names it: "S", "F5", "Delete", "Comma"
    bool isEmpty() const { return key.empty(); }
    bool operator==(const KeyChord& other) const {
        return command == other.command && shift == other.shift && alt == other.alt && key == other.key;
    }
};

/// "Alt+Shift+Cmd+P" (in any order; "Option" and "Ctrl" are understood too); empty for none.
KeyChord parseChord(const std::string& text);
std::string chordText(const KeyChord& chord);

class Shortcuts {
public:
    struct Action {
        std::string id;                 // "save"
        std::string title;              // "Save Scene"
        std::string group;              // "File"
        KeyChord chord, original;
    };

    Shortcuts();
    const std::vector<Action>& actions() const { return actions_; }
    /// True if there is a shortcut by this name (set or not).
    bool has(const std::string& id) const;
    const KeyChord& chord(const std::string& id) const;
    /// For a menu: "Cmd+S", or "" for none.
    std::string text(const std::string& id) const;
    void set(const std::string& id, const KeyChord& chord);
    void reset(const std::string& id);
    void resetAll();
    /// The other actions on the same keys: a chord should do one thing.
    std::vector<std::string> sharing(const std::string& id) const;

    /// Only those changed from the original, a line each: "save Cmd+Alt+S".
    std::string toText() const;
    void fromText(const std::string& text);

private:
    Action* find(const std::string& id);
    std::vector<Action> actions_;
};

}  // namespace molten::editor
