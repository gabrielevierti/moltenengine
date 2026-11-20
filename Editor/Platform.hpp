// Platform.hpp
// The few things the editor asks macOS itself: file dialogs, the Trash, the Finder.
// Implemented in Platform.mm, the editor's only Objective-C++.
#pragma once

#include <string>
#include <vector>

namespace molten::editor::platform {

/// The standard "choose a folder" sheet. Returns "" if the user cancels.
std::string chooseFolder(const std::string& title, const std::string& startDirectory = {});

/// The standard "open" sheet for files with one of `extensions` ("png", "obj"; none for
/// any file). Returns nothing if the user cancels.
std::vector<std::string> chooseFiles(const std::string& title, const std::vector<std::string>& extensions, bool allowMany,
                                     const std::string& startDirectory = {});

/// Moves a file or folder to the Trash, where it can be recovered.
bool moveToTrash(const std::string& path);

void revealInFinder(const std::string& path);
/// Opens a file with the program macOS uses for it: an image editor, Xcode.
void openWithDefaultApplication(const std::string& path);
/// Opens a web address in the browser.
void openWebPage(const std::string& address);

}  // namespace molten::editor::platform
