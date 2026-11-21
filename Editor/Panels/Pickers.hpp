// Pickers.hpp
// The control for a text value that names something (a sound, a scene, an entity, a bone, a
// shared value...): a list of what it can be, never a box to type it into, where one wrong
// letter breaks the game without a word. Free text is only for words people read: a button's
// label, a text on screen. The kinds are those of Field::pick.
#pragma once

#include "Editor.hpp"

#include <string>
#include <vector>

namespace molten::editor {

/// Draws the control for `value`, a `kind` of thing, labelled. `owner` is the entity whose
/// component it is (for its bones, its children, its controller); none where there is none.
/// Kind "" is free words; "none" draws a dim dash (nothing to choose for it).
bool pickValue(EditorContext& editor, Entity owner, const std::string& kind, const char* label, std::string& value);

/// The same without a label, as wide as the space given: for the boxes of a visual script.
bool pickValueInline(EditorContext& editor, Entity owner, const std::string& kind, std::string& value, float width);

/// A list the caller makes (with "New…" when a new name may be given): for what only it knows,
/// like the variables of the graph being edited.
bool pickFromList(const char* id, std::string& value, const std::vector<std::string>& choices, bool allowsNew, float width);

/// What a kind can be, in the scene and the project as they are now.
std::vector<std::string> choicesFor(EditorContext& editor, Entity owner, const std::string& kind);

/// The file extensions of an asset kind ("sound": .wav .mp3 ...); empty if it is not one.
std::vector<std::string> extensionsOf(const std::string& kind);

}  // namespace molten::editor
