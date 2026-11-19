// Project.hpp
// A game project on disk: a folder with a `.molten` file in it.
//
//     MyGame/
//       MyGame.molten      this file: the name, the scene the game starts with
//       Assets/            everything the game ships: scenes, models, textures, sounds
//       Source/            the game's C++ code: components declared with MOLTEN_COMPONENT
//       Plugins/           editor plugins built for this project (.dylib)
//       Settings/          how the editor was left: window layout, snapping
//       Library/           what the editor can rebuild; never put under version control
//
// The project file is text, one statement per line, like scene files:
//
//     molten-project 1
//     name "My Game"
//     startScene "Scenes/Main.scene"
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace molten {

struct Project {
    std::string name = "Untitled";
    std::string directory;              // the project's folder, absolute
    std::string file;                   // the .molten file it was read from; kept when the project is renamed
    std::string startScene;             // relative to Assets: "Scenes/Main.scene"
    std::string version = "0.1.0";      // of the game, as the developer numbers it
    std::string identifier;             // the bundle identifier: "com.example.mygame"

    std::string filePath() const;
    std::string assetsDirectory() const { return directory + "/Assets"; }
    /// The game's own C++ code. The editor builds everything in it into one library.
    std::string sourceDirectory() const { return directory + "/Source"; }
    std::string pluginsDirectory() const { return directory + "/Plugins"; }
    std::string settingsDirectory() const { return directory + "/Settings"; }
    std::string libraryDirectory() const { return directory + "/Library"; }

    /// Opens a project from its `.molten` file or from the folder that contains one.
    static std::optional<Project> load(const std::string& path, std::string* error = nullptr);

    /// Makes `parentDirectory/name` with the folders above and an empty project file.
    /// Fails if that folder exists and is not empty.
    static std::optional<Project> create(const std::string& parentDirectory, const std::string& name, std::string* error = nullptr);

    bool save(std::string* error = nullptr) const;

    /// The project as the text of its file, and back.
    std::string toText() const;
    static std::optional<Project> fromText(const std::string& text);

    /// "My Game!" -> "MyGame": what the folder and the project file are called.
    static std::string fileNameFor(const std::string& name);
};

/// The projects opened lately, most recent first, kept in a text file of the user's.
struct RecentProjects {
    std::vector<std::string> paths;     // project folders
    static constexpr size_t kMaximum = 12;

    void add(const std::string& projectDirectory);
    void remove(const std::string& projectDirectory);
    bool load(const std::string& file);
    bool save(const std::string& file) const;
};

/// Where Molten keeps what belongs to the user and not to a project:
/// ~/Library/Application Support/MoltenMetalEngine on a Mac. Created if missing.
std::string userDataDirectory();

}  // namespace molten
