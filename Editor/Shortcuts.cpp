// Shortcuts.cpp
#include "Shortcuts.hpp"

#include <sstream>

namespace molten::editor {

KeyChord parseChord(const std::string& text) {
    KeyChord chord;
    std::string part;
    std::istringstream parts(text);
    std::vector<std::string> pieces;
    while (std::getline(parts, part, '+')) {
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty()) pieces.push_back(part);
    }
    // "Cmd++" is a plus: what is left after the last separator.
    if (!text.empty() && text.back() == '+' && (text.size() == 1 || text[text.size() - 2] == '+')) pieces.push_back("+");
    for (size_t i = 0; i < pieces.size(); i++) {
        const std::string& piece = pieces[i];
        bool isLast = i + 1 == pieces.size();
        if (!isLast && (piece == "Cmd" || piece == "Command" || piece == "Ctrl" || piece == "Control")) chord.command = true;
        else if (!isLast && piece == "Shift") chord.shift = true;
        else if (!isLast && (piece == "Alt" || piece == "Option" || piece == "Opt")) chord.alt = true;
        else if (isLast) chord.key = piece;
    }
    return chord;
}

std::string chordText(const KeyChord& chord) {
    if (chord.isEmpty()) return {};
    std::string out;
    if (chord.alt) out += "Alt+";
    if (chord.shift) out += "Shift+";
    if (chord.command) out += "Cmd+";
    return out + chord.key;
}

Shortcuts::Shortcuts() {
    // Each is a command of Editor/EditorCommands.cpp, by its id; the title and the group are what
    // Preferences > Shortcuts shows. Mostly Unity's keys, which most hands already know.
    const char* defaults[][4] = {
        {"newScene", "New Scene", "File", "Cmd+N"},
        {"openScene", "Open Scene", "File", "Cmd+O"},
        {"save", "Save", "File", "Cmd+S"},
        {"saveAs", "Save As", "File", "Shift+Cmd+S"},
        {"openProject", "Open Project", "File", "Shift+Cmd+O"},
        {"export", "Build Settings", "File", "Shift+Cmd+B"},
        {"quit", "Quit", "File", "Cmd+Q"},
        {"undo", "Undo", "Edit", "Cmd+Z"},
        {"redo", "Redo", "Edit", "Shift+Cmd+Z"},
        {"cut", "Cut", "Edit", "Cmd+X"},
        {"copy", "Copy", "Edit", "Cmd+C"},
        {"paste", "Paste", "Edit", "Cmd+V"},
        {"duplicate", "Duplicate", "Edit", "Cmd+D"},
        {"rename", "Rename", "Edit", "F2"},
        {"delete", "Delete", "Edit", "Cmd+Backspace"},
        {"selectAll", "Select All", "Edit", "Cmd+A"},
        {"deselectAll", "Deselect All", "Edit", "Shift+D"},
        {"selectChildren", "Select Children", "Edit", "Shift+C"},
        {"invertSelection", "Invert Selection", "Edit", "Cmd+I"},
        {"find", "Find in Hierarchy", "Edit", "Cmd+F"},
        {"findAnything", "Search Everything", "Edit", "Cmd+K"},
        {"play", "Play and Stop", "Edit", "Cmd+P"},
        {"pause", "Pause and Resume", "Edit", "Shift+Cmd+P"},
        {"step", "Step One Frame", "Edit", "Alt+Cmd+P"},
        {"settings", "Preferences", "Edit", "Cmd+Comma"},
        {"refresh", "Refresh Assets", "Assets", "Cmd+R"},
        {"buildScripts", "Compile Scripts", "Assets", "Cmd+B"},
        {"createEmpty", "Create Empty", "Entity", "Shift+Cmd+N"},
        {"createEmptyChild", "Create Empty Child", "Entity", "Alt+Shift+N"},
        {"group", "Group Selection", "Entity", "Cmd+G"},
        {"clearParent", "Clear Parent", "Entity", "Alt+Shift+P"},
        {"firstSibling", "Move to Top", "Entity", "Cmd+Equal"},
        {"lastSibling", "Move to Bottom", "Entity", "Cmd+Minus"},
        {"moveToView", "Move to View", "Entity", "Alt+Cmd+F"},
        {"alignWithView", "Align with View", "Entity", "Shift+Cmd+F"},
        {"toggleActive", "Toggle Active", "Entity", "Alt+Shift+A"},
        {"addComponent", "Add Component", "Component", "Shift+Cmd+A"},
        {"windowScene", "Scene", "Window", "Cmd+1"},
        {"windowGame", "Game", "Window", "Cmd+2"},
        {"windowInspector", "Inspector", "Window", "Cmd+3"},
        {"windowHierarchy", "Hierarchy", "Window", "Cmd+4"},
        {"windowProject", "Project", "Window", "Cmd+5"},
        {"windowConsole", "Console", "Window", "Shift+Cmd+C"},
        {"windowProfiler", "Profiler", "Window", "Cmd+7"},
    };
    for (const auto& line : defaults) {
        Action action{line[0], line[1], line[2], parseChord(line[3]), parseChord(line[3])};
        actions_.push_back(action);
    }
}

Shortcuts::Action* Shortcuts::find(const std::string& id) {
    for (Action& action : actions_) {
        if (action.id == id) return &action;
    }
    return nullptr;
}

bool Shortcuts::has(const std::string& id) const {
    for (const Action& action : actions_) {
        if (action.id == id) return true;
    }
    return false;
}

const KeyChord& Shortcuts::chord(const std::string& id) const {
    static const KeyChord none;
    for (const Action& action : actions_) {
        if (action.id == id) return action.chord;
    }
    return none;
}

std::string Shortcuts::text(const std::string& id) const {
    std::string shown = chordText(chord(id));
    // Read as a menu shows it: "Cmd+," rather than "Cmd+Comma".
    const std::pair<const char*, const char*> keys[] = {{"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Minus", "-"}, {"Equal", "="}};
    for (const auto& [name, sign] : keys) {
        std::string tail = std::string("+") + name;
        if (shown.size() >= tail.size() && shown.compare(shown.size() - tail.size(), tail.size(), tail) == 0) {
            shown = shown.substr(0, shown.size() - tail.size() + 1) + sign;
        }
    }
    return shown;
}

void Shortcuts::set(const std::string& id, const KeyChord& chord) {
    if (Action* action = find(id)) action->chord = chord;
}

void Shortcuts::reset(const std::string& id) {
    if (Action* action = find(id)) action->chord = action->original;
}

void Shortcuts::resetAll() {
    for (Action& action : actions_) action.chord = action.original;
}

std::vector<std::string> Shortcuts::sharing(const std::string& id) const {
    std::vector<std::string> others;
    const KeyChord& mine = chord(id);
    if (mine.isEmpty()) return others;
    for (const Action& action : actions_) {
        if (action.id != id && action.chord == mine) others.push_back(action.title);
    }
    return others;
}

std::string Shortcuts::toText() const {
    std::string out;
    for (const Action& action : actions_) {
        if (action.chord == action.original) continue;
        out += action.id + " " + (action.chord.isEmpty() ? std::string("none") : chordText(action.chord)) + "\n";
    }
    return out;
}

void Shortcuts::fromText(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string id = line.substr(0, space), keys = line.substr(space + 1);
        set(id, keys == "none" ? KeyChord{} : parseChord(keys));
    }
}

}  // namespace molten::editor
