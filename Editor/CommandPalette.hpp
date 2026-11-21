// CommandPalette.hpp
// Cmd+K: one field that finds anything the editor has by name and does it. The menus' actions,
// the windows, the Create menu, the entities of the scene, the project's assets.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace molten::editor {

struct PaletteCommand {
    std::string title;                  // what is searched and shown: "Save Scene", "Car/Wheel FL"
    std::string kind;                   // shown at the right: "Edit", "Window", "Entity", "Asset"
    std::string shortcut;               // shown only
    bool isEnabled = true;
    std::function<void()> run;
    std::string context;                // also searched, a little less: "Rival 2 / Model" for an entity
};

/// How well `query` names `text`: -1 if its letters are not all in it, in order (case is not
/// looked at). Higher for letters together, at the start of words, and at the start of the text.
int paletteScore(const std::string& query, const std::string& text);

/// The commands that match, best first (in their own order when as good), at most `limit`.
std::vector<size_t> rankPalette(const std::string& query, const std::vector<PaletteCommand>& commands, size_t limit);

class CommandPalette {
public:
    void open();
    bool isOpen() const { return isOpen_; }
    /// Draws it over everything, when open. `commands` is everything it can find this frame.
    void draw(const std::vector<PaletteCommand>& commands);

private:
    bool isOpen_ = false;
    bool justOpened_ = false;
    char query_[256] = {};
    int chosen_ = 0;
    std::string lastQuery_;
};

} // namespace molten::editor
