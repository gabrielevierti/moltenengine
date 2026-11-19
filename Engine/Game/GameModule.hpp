// GameModule.hpp
// A game's own code, built as a dynamic library and loaded while the program runs.
//
// The library needs no entry point. Loading it runs its MOLTEN_COMPONENT lines, and
// each declares a component type; that is all the engine needs to show the game's
// components in the editor, read them from scenes and run them.
//
// A module can be replaced by a newer build without restarting (the editor does this
// when the code is rebuilt). The old library is never unloaded: objects it made may
// still be alive somewhere, and their code must stay where it is. A few megabytes of
// address space per rebuild is the price.
#pragma once

#include <string>
#include <vector>

namespace molten {

class GameModule {
public:
    /// Loads a library and takes its component types. If a module was loaded before, its
    /// types are withdrawn first. Returns false, with the reason, if it cannot be loaded;
    /// the previous module's types are then gone too.
    bool load(const std::string& libraryPath, std::string* error = nullptr);
    /// Withdraws the module's component types. Entities must not hold components of
    /// those types any more: clear the scene first (see Scene::dropStorages).
    void retire();

    bool isLoaded() const { return isLoaded_; }
    const std::string& path() const { return path_; }
    /// The component types the module declared, by name.
    std::vector<std::string> componentNames() const;

private:
    bool isLoaded_ = false;
    std::string path_;
    int generation_ = 0;        // its address is the token types are tagged with
};

}  // namespace molten
