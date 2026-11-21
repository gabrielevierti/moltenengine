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
    const char* defaults[][4] = {
        {"newScene", "New Scene", "File", "Cmd+N"},
        {"openScene", "Open Scene", "File", "Cmd+O"},
        {"save", "Save Scene", "File", "Cmd+S"},
        {"saveAs", "Save Scene As New File", "File", "Shift+Cmd+S"},
        {"openProject", "Open Project", "File", "Shift+Cmd+O"},
        {"quit", "Quit", "File", "Cmd+Q"},
        {"undo", "Undo", "Edit", "Cmd+Z"},
        {"redo", "Redo", "Edit", "Shift+Cmd+Z"},
        {"duplicate", "Duplicate", "Edit", "Cmd+D"},
        {"selectAll", "Select All", "Edit", "Cmd+A"},
        {"alignWithView", "Align with View", "Edit", "Shift+Cmd+F"},
        {"findAnything", "Find Anything", "Edit", "Cmd+K"},
        {"settings", "Editor Settings", "Edit", "Cmd+Comma"},
        {"play", "Play and Stop", "Play", "Cmd+P"},
        {"pause", "Pause and Resume", "Play", "Shift+Cmd+P"},
        {"step", "Step One Frame", "Play", "Alt+Cmd+P"},
        {"buildScripts", "Build Scripts Now", "Game", "Cmd+B"},
        {"export", "Build and Export", "Game", "Shift+Cmd+B"},
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
