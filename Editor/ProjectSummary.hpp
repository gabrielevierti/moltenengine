// ProjectSummary.hpp
// What the project window says about a project before it is opened: what its file says,
// its scenes, what is in its folders and how much of it there is. Read from the disk
// without opening anything, so that looking at a project costs nothing.
#pragma once

#include "Assets/AssetDatabase.hpp"
#include "Project/Project.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace molten::editor {

struct ProjectSummary {
    std::string directory;
    bool isValid = false;               // it has a .molten file that could be read
    std::string problem;                // if not: why it cannot be opened
    Project project;                    // what its file says: name, first scene, the game's version

    struct Scene {
        std::string path;               // relative to Assets: "Scenes/Main.scene"
        std::string name;               // "Main"
        int entities = 0;
        uint64_t bytes = 0;
        long long secondsAgo = -1;      // since it was last saved; -1 unknown
        bool isFirst = false;           // the one the game starts with
    };
    std::vector<Scene> scenes;          // the first one at the top, then by name

    // How many of each there are in Assets.
    int models = 0, textures = 0, materials = 0, sounds = 0, scripts = 0, graphs = 0, prefabs = 0, animations = 0;
    int files = 0;                      // everything in Assets, without the .meta files beside them
    uint64_t assetBytes = 0;            // what Assets weighs
    long long secondsAgo = -1;          // since anything of the project was last saved; -1 unknown

    /// The project's folders and files, depth first: a folder, then what is in it (folders
    /// before files, each by name). Hidden files and .meta files are left out; Library, which
    /// the editor rebuilds, is named but not looked into.
    struct Entry {
        std::string name;
        std::string path;               // relative to the project's folder
        int depth = 0;
        bool isFolder = false;
        AssetType type = AssetType::Other;
        int files = 0;                  // a folder: how many files are in it, at any depth
        uint64_t bytes = 0;             // a file's size; a folder's whole weight
        std::string note;               // said beside it: "rebuilt by the editor"
    };
    std::vector<Entry> entries;
    bool isCutShort = false;            // the project has more files than are listed
};

/// Reads a project folder. `maxEntries` bounds the list of files (a project of a hundred
/// thousand textures is still summed up, but not listed one by one).
ProjectSummary summarizeProject(const std::string& directory, size_t maxEntries = 6000);

/// "640 bytes", "218 KB", "22 MB", "1.4 GB".
std::string sizeText(uint64_t bytes);
/// "just now", "12 min ago", "3 hours ago", "yesterday", "9 days ago", "4 months ago".
std::string agoText(long long seconds);
/// "1 scene", "13 scripts": the number with the word in the singular or the plural.
std::string countText(int count, const char* one, const char* many);

}  // namespace molten::editor
