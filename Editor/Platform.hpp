// Platform.hpp
// The few things the editor asks macOS itself: file dialogs, the Trash, the Finder.
// Implemented in Platform.mm, the editor's only Objective-C++.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace molten::editor::platform {

/// The standard "choose a folder" sheet. Returns "" if the user cancels.
std::string chooseFolder(const std::string& title, const std::string& startDirectory = {});

/// The standard "open" sheet for files with one of `extensions` ("png", "obj"; none for
/// any file). Returns nothing if the user cancels.
std::vector<std::string> chooseFiles(const std::string& title, const std::vector<std::string>& extensions, bool allowMany,
                                     const std::string& startDirectory = {});

/// The standard "save" sheet: where to write a new file, starting from `suggestedName`.
/// Returns an empty string if the user cancels.
std::string chooseSaveFile(const std::string& title, const std::string& suggestedName, const std::string& extension);

/// Moves a file or folder to the Trash, where it can be recovered.
bool moveToTrash(const std::string& path);

void revealInFinder(const std::string& path);
/// Opens a file with the program macOS uses for it: an image editor, Xcode.
void openWithDefaultApplication(const std::string& path);
/// Opens a web address in the browser.
void openWebPage(const std::string& address);

// ---------------------------------------------------------------- the Mac's own menu bar

/// A row of a menu in the menu bar at the top of the screen.
struct NativeMenuRow {
    std::string title;                  // empty: a line across the menu
    std::string keys;                   // "Shift+Cmd+S": shown beside it. The editor's own shortcuts do what they say.
    bool isEnabled = true, isChecked = false;
    std::function<void()> run;          // done at the editor's next frame (see takeChosenMenuActions)
    std::vector<NativeMenuRow> children;    // not empty: a submenu
};

/// True where the menus are the Mac's own, at the top of the screen, and not drawn in the window.
bool usesNativeMenuBar();
/// Puts the editor's menus in the Mac's menu bar, by name and in order: the first is the program's
/// own (About, Preferences, Quit; the Mac adds Hide and Show All to it). Each time a menu is
/// opened, `rowsOf` is asked what it holds now, so that what can be done is what is enabled.
void setNativeMenuBar(const std::vector<std::string>& menuTitles, std::function<std::vector<NativeMenuRow>(const std::string& menu)> rowsOf);
/// What was chosen in the menu bar since the last time this was asked, in order. The menus are
/// used between the editor's frames: what they do is done in the next one, as a click in it would be.
std::vector<std::function<void()>> takeChosenMenuActions();
/// The dot in the window's close button that says there are changes not saved.
void setWindowEdited(void* glfwWindow, bool isEdited);

/// Full screen that keeps the Mac's menu bar, where the menus are: the window covers the whole
/// screen but the menu bar, without its title bar, and the Dock hides until the pointer goes
/// down to it. (The Mac's own full screen hides the menu bar.) The window's green button does
/// it once this is called for the window; Window > Full Screen does it and undoes it.
void takeOverFullScreen(void* glfwWindow);
void toggleFullScreen(void* glfwWindow);
bool isFullScreen(void* glfwWindow);

}  // namespace molten::editor::platform
