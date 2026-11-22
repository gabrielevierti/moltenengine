// Theme.hpp
#pragma once

#include <string>
#include <vector>

namespace molten::editor {

/// Sets Dear ImGui's style and colors. `scale` enlarges paddings and rounding with the interface.
void applyTheme(float scale);

/// The looks the editor can have (Edit > Preferences > Appearance): "Forge" (the one it is made
/// with: dark, the orange of hot iron), "Ocean", "Moss", "Midnight".
const std::vector<std::string>& themeNames();
/// Chooses one; applyTheme() then uses it. Unknown names are Forge.
void setTheme(const std::string& name);
const std::string& currentTheme();
/// The accent of the look chosen: what is selected, what has focus.
unsigned themeAccent();

}  // namespace molten::editor
