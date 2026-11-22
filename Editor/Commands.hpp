// Commands.hpp
// Everything the editor can be told to do, said once.
//
// The menu bar, the keyboard shortcuts, the search (Cmd+K) and the list in Preferences >
// Shortcuts are all read from this one list. A command therefore cannot be in a menu and
// missing from the search, or answer to keys its menu does not show: there is nowhere else
// to write it. (Unity does this with [MenuItem], Unreal with its UI command lists.)
//
// Nothing here draws or reads keys, so it can be tested without a window.
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace molten::editor {

struct Command {
    std::string id;                     // "save": what the shortcut settings and plugins call it; never shown
    std::string path;                   // "File/Save": the menu, any submenus, then its name there
    std::function<void()> run;
    std::function<bool()> enabled;      // empty: always
    std::function<bool()> checked;      // not empty: a tick beside it while this says yes
    std::function<std::string()> nameNow;   // not empty: its name changes ("Play", then "Stop")
    std::function<void()> items;        // not empty: it is a list drawn by this (recent files, the layouts)
    bool itemsAreInline = false;        // that list is drawn in the menu itself, not in a submenu of its own
    bool usesEditKeys = false;          // its keys are left to a text field being typed in (Undo, Select All)
    bool actsOnSelection = false;       // ...and to the Project window while it has the keyboard (Delete, Copy)
    bool worksInGame = false;           // its keys work while the playing game has the keyboard (Play, Pause)
    std::string keys;                   // keys shown that the settings do not hold: "F", handled by the Scene view

    // Said after add(), one after the other: add(...).when(...).editKeys()
    Command& when(std::function<bool()> test) { enabled = std::move(test); return *this; }
    Command& ticked(std::function<bool()> test) { checked = std::move(test); return *this; }
    Command& named(std::function<std::string()> name) { nameNow = std::move(name); return *this; }
    Command& lists(std::function<void()> draw, bool inMenuItself = false) {
        items = std::move(draw);
        itemsAreInline = inMenuItself;
        return *this;
    }
    Command& editKeys() { usesEditKeys = true; return *this; }
    Command& onSelection() { usesEditKeys = actsOnSelection = true; return *this; }
    Command& inGame() { worksInGame = true; return *this; }
    Command& shows(std::string shown) { keys = std::move(shown); return *this; }

    bool isLine() const { return path.size() >= 2 && path.compare(path.size() - 2, 2, "/-") == 0; }
    bool isEnabled() const { return !enabled || enabled(); }
    bool isChecked() const { return checked && checked(); }
    /// "File": the menu of the bar it is in.
    std::string menu() const;
    /// "Save": what it is called, now.
    std::string name() const;
    /// "File", "Entity > Light": where it is found, for the search.
    std::string where() const;
};

class Commands {
public:
    /// Adds a command at the end of its menu. `id` is also the name of its shortcut, if it has one.
    Command& add(std::string id, std::string path, std::function<void()> run = {});
    /// A line across a menu, after what was added so far: line("File"), line("Entity/Light").
    void line(const std::string& menuPath);

    Command* find(const std::string& id);
    const Command* find(const std::string& id) const;
    const std::deque<Command>& all() const { return commands_; }
    /// Does it, if it exists and may be done now.
    bool run(const std::string& id) const;

    /// One row of a menu: a command (or a line), or a submenu by name.
    struct Row {
        const Command* command = nullptr;
        std::string submenu;
    };
    /// What a menu holds, in the order it was said: its own commands and lines, and each submenu
    /// once, where its first command is. Lines at either end, and two in a row, are left out.
    std::vector<Row> rowsOf(const std::string& menuPath) const;
    /// The menus of the bar, in the order they were first mentioned.
    std::vector<std::string> menus() const;

private:
    std::deque<Command> commands_;      // a deque: add() hands out references that must stay good
};

}  // namespace molten::editor
