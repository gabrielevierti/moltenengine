// What the project window says about a project before it is opened.
#include "Test.hpp"

#include "../Editor/ProjectSummary.hpp"
#include "Assets/AssetDatabase.hpp"
#include "Core/TextFile.hpp"
#include "Project/Project.hpp"

#include <filesystem>

using namespace molten;
using namespace molten::editor;
namespace fs = std::filesystem;

namespace {

struct Scratch {
    fs::path path;
    explicit Scratch(const char* name) : path(fs::temp_directory_path() / (std::string("molten-test-") + name + "-" + AssetDatabase::makeGuid())) {
        fs::create_directories(path);
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void touch(const fs::path& path, const std::string& contents = "x") {
    fs::create_directories(path.parent_path());
    text::writeFile(path.string(), contents);
}

const ProjectSummary::Entry* entryAt(const ProjectSummary& summary, const std::string& path) {
    for (const ProjectSummary::Entry& entry : summary.entries) {
        if (entry.path == path) return &entry;
    }
    return nullptr;
}

}  // namespace

TEST(a_project_is_summed_up_without_opening_it) {
    Scratch scratch("summary");
    std::string error;
    std::optional<Project> made = Project::create(scratch.path.string(), "Sea Race", &error);
    CHECK(made.has_value());
    if (!made) return;
    fs::path root = made->directory;
    fs::path assets = root / "Assets";
    touch(assets / "Scenes/Harbour.scene", "molten-scene 1\nentity \"Boat\"\n  transform 0 0 0 0 0 0 1 1 1 1\nentity \"Sea\"\nentity \"Sun\"\n");
    touch(assets / "Scenes/Menu.scene", "molten-scene 1\nentity \"Camera\"\n");
    touch(assets / "Scenes/Menu.scene.meta", "molten-meta 1\n");
    touch(assets / "Scripts/Helm.cpp", "// a component");
    touch(assets / "Scripts/Helm.hpp", "// its header");
    touch(assets / "Models/boat.glb", std::string(4000, 'm'));
    touch(assets / "Textures/hull.png", std::string(1500, 't'));
    touch(assets / "Textures/sail.jpg", std::string(500, 't'));
    touch(assets / "Materials/Hull.material", "material");
    touch(assets / "Audio/engine.wav", "w");
    touch(assets / ".DS_Store", "junk");
    touch(root / "Library/Game/Game.dylib", std::string(9000, 'b'));
    made->startScene = "Scenes/Menu.scene";
    CHECK(made->save());

    ProjectSummary summary = summarizeProject(root.string());
    CHECK(summary.isValid);
    CHECK(summary.problem.empty());
    CHECK(summary.project.name == "Sea Race");
    // The scene the game starts with comes first; each knows how many entities it has.
    CHECK(summary.scenes.size() == 2);
    if (summary.scenes.size() == 2) {
        CHECK(summary.scenes[0].name == "Menu" && summary.scenes[0].isFirst && summary.scenes[0].entities == 1);
        CHECK(summary.scenes[1].path == "Scenes/Harbour.scene" && !summary.scenes[1].isFirst && summary.scenes[1].entities == 3);
        CHECK(summary.scenes[1].secondsAgo >= 0 && summary.scenes[1].secondsAgo < 600);
    }
    // A script is counted once, not once for its header too.
    CHECK(summary.scripts == 1);
    CHECK(summary.models == 1 && summary.textures == 2 && summary.materials == 1 && summary.sounds == 1);
    // Hidden files and the .meta beside each asset are nobody's business here.
    CHECK(entryAt(summary, "Assets/.DS_Store") == nullptr);
    CHECK(entryAt(summary, "Assets/Scenes/Menu.scene.meta") == nullptr);
    CHECK(summary.files == 9);
    CHECK(summary.secondsAgo >= 0 && summary.secondsAgo < 600);

    // The list: a folder, then what is in it, folders before files.
    const ProjectSummary::Entry* textures = entryAt(summary, "Assets/Textures");
    CHECK(textures && textures->isFolder && textures->depth == 1 && textures->files == 2 && textures->bytes == 2000);
    const ProjectSummary::Entry* boat = entryAt(summary, "Assets/Models/boat.glb");
    CHECK(boat && !boat->isFolder && boat->depth == 2 && boat->type == AssetType::Mesh && boat->bytes == 4000);
    const ProjectSummary::Entry* assetsEntry = entryAt(summary, "Assets");
    CHECK(assetsEntry && assetsEntry->depth == 0 && assetsEntry->files == 9);
    CHECK(!summary.entries.empty() && summary.entries.front().isFolder);
    // Library is named and left alone: what is in it is the editor's to rebuild.
    const ProjectSummary::Entry* library = entryAt(summary, "Library");
    CHECK(library && library->isFolder && !library->note.empty());
    CHECK(entryAt(summary, "Library/Game") == nullptr);
    // The project file itself is there, at the top.
    bool hasProjectFile = false;
    for (const ProjectSummary::Entry& entry : summary.entries) hasProjectFile = hasProjectFile || (entry.depth == 0 && entry.name.find(".molten") != std::string::npos);
    CHECK(hasProjectFile);
}

TEST(a_folder_that_is_no_project_says_so) {
    Scratch scratch("summary-none");
    touch(scratch.path / "notes.txt", "hello");
    ProjectSummary summary = summarizeProject(scratch.path.string());
    CHECK(!summary.isValid);
    CHECK(!summary.problem.empty());
    CHECK(summary.project.name == scratch.path.filename().string());
    ProjectSummary gone = summarizeProject((scratch.path / "not-here").string());
    CHECK(!gone.isValid && !gone.problem.empty() && gone.entries.empty());
}

TEST(a_huge_project_is_summed_up_but_not_listed_whole) {
    Scratch scratch("summary-many");
    std::string error;
    std::optional<Project> made = Project::create(scratch.path.string(), "Many", &error);
    CHECK(made.has_value());
    if (!made) return;
    for (int i = 0; i < 60; i++) touch(fs::path(made->directory) / "Assets/Textures" / ("t" + std::to_string(i) + ".png"), "p");
    ProjectSummary summary = summarizeProject(made->directory, 20);
    CHECK(summary.entries.size() == 20);
    CHECK(summary.isCutShort);
    CHECK(summary.textures == 60);
}

TEST(sizes_and_ages_read_the_way_people_say_them) {
    CHECK(sizeText(640) == "640 bytes");
    CHECK(sizeText(218000) == "218 KB");
    CHECK(sizeText(2400000) == "2.4 MB");
    CHECK(sizeText(22300000) == "22 MB");
    CHECK(sizeText(1400000000ull) == "1.4 GB");
    CHECK(agoText(5) == "just now");
    CHECK(agoText(12 * 60) == "12 min ago");
    CHECK(agoText(3600) == "1 hour ago");
    CHECK(agoText(3 * 3600) == "3 hours ago");
    CHECK(agoText(86400 + 5) == "yesterday");
    CHECK(agoText(9 * 86400) == "9 days ago");
    CHECK(agoText(120 * 86400) == "4 months ago");
    CHECK(agoText(-1).empty());
    CHECK(countText(1, "scene", "scenes") == "1 scene");
    CHECK(countText(13, "script", "scripts") == "13 scripts");
}
