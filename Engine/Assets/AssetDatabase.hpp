// AssetDatabase.hpp
// What is in a project's Assets folder: every file, what kind of asset it is, and an
// identifier that stays the same when the file is renamed or moved.
//
// The identifier lives in a small text file beside the asset, `name.ext.meta`:
//
//     molten-meta 1
//     guid 3f2a9c0e5b7d41a8b6c2d9e0f1a2b3c4
//
// Put the .meta files under version control together with the assets. The database
// itself is only what a scan of the folder finds: nothing else is stored.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

enum class AssetType : uint8_t { Folder, Scene, Mesh, Texture, Audio, Font, Shader, Script, Text, Other, Graph, Prefab, Material, Effect, Animation, Controller, Timeline };

struct AssetInfo {
    std::string path;                   // relative to Assets, with '/': "Textures/wall.png"
    std::string name;                   // the last part: "wall.png"
    std::string guid;                   // empty for folders
    AssetType type = AssetType::Other;
    uint64_t size = 0;                  // in bytes
    int64_t modified = 0;               // file time, for noticing changes

    bool isFolder() const { return type == AssetType::Folder; }
    /// The folder this is in: "Textures", or "" at the top.
    std::string parent() const;
    /// "wall" for "Textures/wall.png".
    std::string stem() const;
};

class AssetDatabase {
public:
    /// Points the database at an Assets folder and scans it.
    void open(const std::string& assetsDirectory);
    const std::string& directory() const { return directory_; }
    bool isOpen() const { return !directory_.empty(); }

    /// Scans again. Gives every new file a .meta and removes the .meta of files that are
    /// gone. Returns true if anything differs from the last scan.
    bool refresh();
    /// A number that changes whenever a scan finds a difference: lets a panel cache.
    uint64_t revision() const { return revision_; }

    /// Everything, folders included, sorted by path.
    const std::vector<AssetInfo>& assets() const { return assets_; }
    /// What is directly inside a folder ("" for the top): folders first, then files, by name.
    std::vector<const AssetInfo*> children(const std::string& folder) const;
    const AssetInfo* find(const std::string& path) const;
    const AssetInfo* findByGuid(const std::string& guid) const;
    std::string absolutePath(const std::string& path) const { return path.empty() ? directory_ : directory_ + "/" + path; }

    // ---- changes. Each one rescans; `error` says why a change was refused.

    /// Makes a folder inside `parent`. If the name is taken, a number is added.
    /// Returns the new folder's path, or "" on failure.
    std::string createFolder(const std::string& parent, const std::string& name, std::string* error = nullptr);
    /// Gives a file or folder another name in the same folder. The .meta follows.
    /// Returns the new path, or "" on failure.
    std::string rename(const std::string& path, const std::string& newName, std::string* error = nullptr);
    /// Moves a file or folder into another folder. Returns the new path, or "".
    std::string move(const std::string& path, const std::string& newParent, std::string* error = nullptr);
    /// Copies a file from anywhere on disk into a folder of the project.
    std::string importFile(const std::string& sourcePath, const std::string& folder, std::string* error = nullptr);
    /// A path in `folder` that nothing uses yet: "Scenes/New Scene 2.scene".
    std::string uniquePath(const std::string& folder, const std::string& stem, const std::string& extension) const;

    static AssetType typeOfExtension(const std::string& extension);
    static const char* typeName(AssetType type);
    /// False for names the file system or the database cannot take.
    static bool isValidName(const std::string& name);
    static std::string makeGuid();

private:
    std::string directory_;
    std::vector<AssetInfo> assets_;
    std::unordered_map<std::string, size_t> byPath_, byGuid_;
    uint64_t revision_ = 0;
};

}  // namespace molten
