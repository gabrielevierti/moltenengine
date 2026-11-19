#include "Test.hpp"

#include "Assets/AssetDatabase.hpp"
#include "Core/TextFile.hpp"
#include "Project/Project.hpp"

#include <filesystem>

using namespace molten;
namespace fs = std::filesystem;

namespace {

/// A folder of its own for one test, removed when the test ends.
struct Scratch {
    fs::path path;
    explicit Scratch(const char* name) : path(fs::temp_directory_path() / (std::string("molten-test-") + name + "-" + AssetDatabase::makeGuid())) {
        fs::create_directories(path);
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    std::string str() const { return path.string(); }
};

void touch(const fs::path& path, const std::string& contents = "x") {
    fs::create_directories(path.parent_path());
    text::writeFile(path.string(), contents);
}

}  // namespace

TEST(text_words_keep_quoted_strings_together) {
    std::vector<std::string> words = text::splitWords("  name \"My \\\"Great\\\" Game\"  3 ");
    CHECK(words.size() == 3);
    CHECK(words[0] == "name");
    CHECK(words[1] == "My \"Great\" Game");
    CHECK(words[2] == "3");
    CHECK(text::splitWords(text::quote("a \\ b \"c\"\nd"))[0] == "a \\ b \"c\"\nd");
}

TEST(project_text_round_trips) {
    Project project;
    project.name = "Space \"Miner\" 2";
    project.startScene = "Scenes/Main Menu.scene";
    project.version = "1.2.0";
    project.identifier = "com.example.miner";
    std::optional<Project> back = Project::fromText(project.toText());
    CHECK(back.has_value());
    CHECK(back->name == project.name);
    CHECK(back->startScene == project.startScene);
    CHECK(back->version == project.version);
    CHECK(back->identifier == project.identifier);
    CHECK(back->toText() == project.toText());
    CHECK(!Project::fromText("molten-scene 1\n").has_value());
    CHECK(!Project::fromText("").has_value());
    // Statements from a newer version are skipped, not fatal.
    CHECK(Project::fromText("molten-project 2\nname \"A\"\nrenderer \"future\" 3\n")->name == "A");
}

TEST(project_is_created_saved_and_loaded) {
    Scratch scratch("project");
    std::string error;
    std::optional<Project> created = Project::create(scratch.str(), "  My Game! ", &error);
    CHECK(created.has_value());
    CHECK(created->name == "My Game!");
    CHECK(fs::path(created->directory).filename() == "MyGame");
    CHECK(fs::exists(created->filePath()));
    CHECK(fs::is_directory(created->assetsDirectory() + "/Scenes"));
    CHECK(fs::exists(created->directory + "/.gitignore"));

    created->startScene = "Scenes/Main.scene";
    CHECK(created->save());
    // From the folder and from the file.
    std::optional<Project> fromFolder = Project::load(created->directory, &error);
    std::optional<Project> fromFile = Project::load(created->filePath(), &error);
    CHECK(fromFolder.has_value() && fromFile.has_value());
    CHECK(fromFolder->startScene == "Scenes/Main.scene");
    CHECK(fromFile->directory == fromFolder->directory);

    // A folder that already holds something is not taken over.
    CHECK(!Project::create(scratch.str(), "My Game!", &error).has_value());
    CHECK(!error.empty());
    CHECK(!Project::create(scratch.str(), "   ", &error).has_value());
    CHECK(!Project::load(scratch.str(), &error).has_value());       // no .molten directly in there
}

TEST(recent_projects_keep_order_and_a_limit) {
    Scratch scratch("recent");
    RecentProjects recent;
    recent.add("/a");
    recent.add("/b with space");
    recent.add("/a");                           // moves to the front, not duplicated
    CHECK(recent.paths.size() == 2);
    CHECK(recent.paths[0] == "/a");
    for (int i = 0; i < 30; i++) recent.add("/p" + std::to_string(i));
    CHECK(recent.paths.size() == RecentProjects::kMaximum);
    std::string file = scratch.str() + "/recent.txt";
    CHECK(recent.save(file));
    RecentProjects loaded;
    CHECK(loaded.load(file));
    CHECK(loaded.paths == recent.paths);
}

TEST(asset_database_scans_types_and_gives_guids) {
    Scratch scratch("assets");
    touch(scratch.path / "Textures/wall.PNG");
    touch(scratch.path / "Models/ship.obj");
    touch(scratch.path / "Scenes/Main.scene");
    touch(scratch.path / ".DS_Store");
    touch(scratch.path / ".hidden/secret.png");

    AssetDatabase database;
    database.open(scratch.str());
    CHECK(database.assets().size() == 6);       // three folders, three files; hidden things are not assets
    const AssetInfo* wall = database.find("Textures/wall.PNG");
    CHECK(wall != nullptr);
    CHECK(wall->type == AssetType::Texture);
    CHECK(wall->guid.size() == 32);
    CHECK(wall->parent() == "Textures");
    CHECK(wall->stem() == "wall");
    CHECK(database.find("Models/ship.obj")->type == AssetType::Mesh);
    CHECK(database.find("Scenes/Main.scene")->type == AssetType::Scene);
    CHECK(database.find("Scenes")->isFolder());
    CHECK(fs::exists(scratch.path / "Textures/wall.PNG.meta"));
    CHECK(database.findByGuid(wall->guid) == wall);

    // Scanning again changes nothing and keeps the identifiers.
    std::string guid = wall->guid;
    uint64_t revision = database.revision();
    CHECK(!database.refresh());
    CHECK(database.revision() == revision);
    AssetDatabase again;
    again.open(scratch.str());
    CHECK(again.find("Textures/wall.PNG")->guid == guid);

    std::vector<const AssetInfo*> top = database.children("");
    CHECK(top.size() == 3);
    CHECK(top[0]->name == "Models");
}

TEST(asset_database_notices_new_changed_and_deleted_files) {
    Scratch scratch("changes");
    touch(scratch.path / "a.png");
    AssetDatabase database;
    database.open(scratch.str());
    touch(scratch.path / "b.png");
    CHECK(database.refresh());
    CHECK(database.find("b.png") != nullptr);
    touch(scratch.path / "a.png", "longer contents");
    CHECK(database.refresh());
    CHECK(database.find("a.png")->size == 15);
    fs::remove(scratch.path / "b.png");
    CHECK(database.refresh());
    CHECK(database.find("b.png") == nullptr);
    CHECK(!fs::exists(scratch.path / "b.png.meta"));           // the orphan was cleaned up
    // A .meta that is not ours is never touched.
    touch(scratch.path / "foreign.meta", "something else");
    database.refresh();
    CHECK(fs::exists(scratch.path / "foreign.meta"));
}

TEST(asset_database_renames_and_moves_keeping_the_guid) {
    Scratch scratch("moves");
    touch(scratch.path / "Textures/wall.png");
    touch(scratch.path / "Models/ship.obj");
    AssetDatabase database;
    database.open(scratch.str());
    std::string guid = database.find("Textures/wall.png")->guid;
    std::string error;

    CHECK(database.rename("Textures/wall.png", "brick.png", &error) == "Textures/brick.png");
    CHECK(database.find("Textures/wall.png") == nullptr);
    CHECK(database.find("Textures/brick.png")->guid == guid);

    CHECK(database.move("Textures/brick.png", "Models", &error) == "Models/brick.png");
    CHECK(database.find("Models/brick.png")->guid == guid);
    CHECK(database.move("Models/brick.png", "", &error) == "brick.png");
    CHECK(database.findByGuid(guid)->path == "brick.png");

    // Refused: a taken name, a bad name, a folder into itself, a target that is not a folder.
    CHECK(database.rename("Models/ship.obj", "a/b.obj", &error).empty());
    CHECK(database.rename("Models/ship.obj", ".hidden", &error).empty());
    CHECK(database.rename("Models/ship.obj", "ship.obj.meta", &error).empty());
    touch(scratch.path / "Models/other.obj");
    database.refresh();
    CHECK(database.rename("Models/ship.obj", "other.obj", &error).empty());
    CHECK(database.createFolder("Models", "Sub", &error) == "Models/Sub");
    CHECK(database.move("Models", "Models/Sub", &error).empty());
    CHECK(database.move("Models/ship.obj", "brick.png", &error).empty());
    CHECK(fs::exists(scratch.path / "Models/ship.obj"));

    // A folder moves with everything in it, identifiers included.
    std::string shipGuid = database.find("Models/ship.obj")->guid;
    CHECK(database.move("Models", "Textures", &error) == "Textures/Models");
    CHECK(database.find("Textures/Models/ship.obj")->guid == shipGuid);
}

TEST(asset_database_makes_unique_names_and_imports) {
    Scratch scratch("unique"), outside("outside");
    AssetDatabase database;
    database.open(scratch.str());
    std::string error;
    CHECK(database.createFolder("", "New Folder", &error) == "New Folder");
    CHECK(database.createFolder("", "New Folder", &error) == "New Folder 2");
    CHECK(database.uniquePath("New Folder", "New Scene", ".scene") == "New Folder/New Scene.scene");

    touch(outside.path / "rock.png", "pixels");
    CHECK(database.importFile((outside.path / "rock.png").string(), "New Folder", &error) == "New Folder/rock.png");
    CHECK(database.importFile((outside.path / "rock.png").string(), "New Folder", &error) == "New Folder/rock 2.png");
    CHECK(database.find("New Folder/rock.png")->size == 6);
    CHECK(fs::exists(outside.path / "rock.png"));               // copied, not moved
    CHECK(database.importFile((outside.path / "missing.png").string(), "", &error).empty());

    std::string a = AssetDatabase::makeGuid(), b = AssetDatabase::makeGuid();
    CHECK(a.size() == 32 && a != b);
}

TEST(project_keeps_its_file_when_renamed) {
    Scratch scratch("rename");
    std::optional<Project> project = Project::create(scratch.str(), "First Name");
    CHECK(project.has_value());
    std::string file = project->filePath();
    project->name = "Second Name";
    CHECK(project->save());
    CHECK(project->filePath() == file);         // one project file, whatever the game is called now
    std::optional<Project> loaded = Project::load(project->directory);
    CHECK(loaded.has_value() && loaded->name == "Second Name");
}
