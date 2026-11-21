// The editor's shortcuts, written as people read them and changed by them.
#include "Test.hpp"

#include "../Editor/Shortcuts.hpp"

using namespace molten::editor;

TEST(shortcuts_are_read_written_and_changed) {
    KeyChord chord = parseChord("Shift+Cmd+S");
    CHECK(chord.command && chord.shift && !chord.alt && chord.key == "S");
    CHECK(chordText(parseChord("Cmd+Option+Shift+P")) == "Alt+Shift+Cmd+P");     // in the order menus show
    CHECK(parseChord("Ctrl+K") == parseChord("Cmd+K"));
    CHECK(parseChord("F5").key == "F5" && !parseChord("F5").command);
    CHECK(parseChord("").isEmpty() && chordText(KeyChord{}).empty());

    Shortcuts shortcuts;
    CHECK(shortcuts.text("save") == "Cmd+S" && shortcuts.text("settings") == "Cmd+,");
    CHECK(shortcuts.toText().empty());                                         // nothing changed: nothing kept
    shortcuts.set("save", parseChord("Alt+Cmd+S"));
    shortcuts.set("quit", KeyChord{});
    shortcuts.set("duplicate", parseChord("Cmd+Z"));
    CHECK(shortcuts.sharing("duplicate").size() == 1 && shortcuts.sharing("duplicate")[0] == "Undo");
    std::string kept = shortcuts.toText();
    Shortcuts again;
    again.fromText(kept);
    CHECK(again.text("save") == "Alt+Cmd+S" && again.text("quit").empty() && again.text("duplicate") == "Cmd+Z");
    again.reset("duplicate");
    CHECK(again.text("duplicate") == "Cmd+D" && again.sharing("duplicate").empty());
    again.resetAll();
    CHECK(again.toText().empty());
}
