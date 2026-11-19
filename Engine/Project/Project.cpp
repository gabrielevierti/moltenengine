#include "Project/Project.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace molten {

namespace fs = std::filesystem;

namespace {

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

const char* const kGitIgnore =
    "# What the Molten editor can rebuild.\n"
    "Library/\n"
    "# How each person left the editor.\n"
    "Settings/\n"
    ".DS_Store\n";

/// What a new project's Source folder starts with: one component that does something
/// visible, with every part of a component in it.
const char* const kFirstSourceFile = R"(// The game's code. Every .cpp file in this folder is built into the game: in the editor
// with Game > Build Game Code (Cmd+B), and again when the game is exported.
#include "MoltenMetal.hpp"

using namespace molten;

// A component is a plain struct. Add it to an entity in the editor with Add Component.
struct Spinner {
    float turnsPerSecond = 0.25f;
    Vec3 axis{0, 1, 0};

    // Called every frame while the game plays. `self` is the entity this component is on.
    void update(Engine& engine, Entity self, float dt) {
        if (Transform* transform = engine.scene.get<Transform>(self)) {
            Quat turn = axisAngle(safeNormalize(axis), turnsPerSecond * 2.0f * kPi * dt);
            transform->rotation = simd::normalize(turn * transform->rotation);
        }
    }

    // Also available, with the same arguments:
    //   void start(Engine& engine, Entity self);                    once, when play begins
    //   void fixedUpdate(Engine& engine, Entity self, float dt);    every physics step
};

// This line makes the component known to the editor and to scene files. The fields
// listed here are the ones shown in the inspector and saved.
MOLTEN_COMPONENT(Spinner, MOLTEN_FIELD(turnsPerSecond).range(-4, 4), MOLTEN_FIELD(axis))
)";

}  // namespace

std::string Project::fileNameFor(const std::string& name) {
    std::string out;
    for (char c : name) {
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (keep) out += c;
    }
    return out.empty() ? "Project" : out;
}

std::string Project::filePath() const {
    return !file.empty() ? file : directory + "/" + fileNameFor(name) + ".molten";
}

std::string Project::toText() const {
    std::string out = "molten-project 1\n";
    out += "name " + text::quote(name) + "\n";
    if (!startScene.empty()) out += "startScene " + text::quote(startScene) + "\n";
    out += "version " + text::quote(version) + "\n";
    if (!identifier.empty()) out += "identifier " + text::quote(identifier) + "\n";
    return out;
}

std::optional<Project> Project::fromText(const std::string& contents) {
    std::vector<std::vector<std::string>> lines = text::parseLines(contents);
    if (lines.empty() || lines[0][0] != "molten-project") return std::nullopt;
    Project project;
    for (size_t i = 1; i < lines.size(); i++) {
        const std::vector<std::string>& words = lines[i];
        if (words.size() < 2) continue;             // a statement this version does not know: skipped
        if (words[0] == "name") project.name = words[1];
        else if (words[0] == "startScene") project.startScene = words[1];
        else if (words[0] == "version") project.version = words[1];
        else if (words[0] == "identifier") project.identifier = words[1];
    }
    return project;
}

std::optional<Project> Project::load(const std::string& path, std::string* error) {
    std::error_code code;
    fs::path file = fs::absolute(fs::path(path), code);
    if (fs::is_directory(file, code)) {
        // The folder was given: the project file is the one `.molten` inside it.
        fs::path found;
        for (const auto& entry : fs::directory_iterator(file, code)) {
            if (entry.path().extension() == ".molten") found = entry.path();
        }
        if (found.empty()) {
            fail(error, "There is no .molten file in " + file.string());
            return std::nullopt;
        }
        file = found;
    }
    std::optional<std::string> contents = text::readFile(file.string());
    if (!contents) {
        fail(error, "Cannot read " + file.string());
        return std::nullopt;
    }
    std::optional<Project> project = fromText(*contents);
    if (!project) {
        fail(error, file.filename().string() + " is not a Molten Metal Engine project file");
        return std::nullopt;
    }
    project->directory = file.parent_path().lexically_normal().string();
    project->file = file.lexically_normal().string();
    // An older or hand-made project may lack folders the editor expects.
    for (const std::string& folder : {project->assetsDirectory(), project->settingsDirectory(), project->libraryDirectory()}) {
        fs::create_directories(folder, code);
    }
    return project;
}

std::optional<Project> Project::create(const std::string& parentDirectory, const std::string& name, std::string* error) {
    std::string trimmed = name;
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    if (trimmed.empty()) {
        fail(error, "The project needs a name");
        return std::nullopt;
    }
    std::error_code code;
    fs::path parent = fs::absolute(fs::path(parentDirectory), code);
    fs::path directory = parent / fileNameFor(trimmed);
    if (fs::exists(directory, code) && !fs::is_empty(directory, code)) {
        fail(error, directory.string() + " already exists and is not empty");
        return std::nullopt;
    }
    Project project;
    project.name = trimmed;
    project.directory = directory.lexically_normal().string();
    for (const char* folder : {"Assets/Scenes", "Assets/Models", "Assets/Textures", "Assets/Audio", "Source", "Plugins", "Settings", "Library"}) {
        fs::create_directories(directory / folder, code);
        if (code) {
            fail(error, "Cannot create " + (directory / folder).string() + ": " + code.message());
            return std::nullopt;
        }
    }
    text::writeFile((directory / ".gitignore").string(), kGitIgnore);
    text::writeFile((directory / "Source" / "Game.cpp").string(), kFirstSourceFile);
    project.file = project.filePath();
    if (!project.save(error)) return std::nullopt;
    return project;
}

bool Project::save(std::string* error) const {
    if (text::writeFile(filePath(), toText())) return true;
    fail(error, "Cannot write " + filePath());
    return false;
}

// ---------------------------------------------------------------- recent projects

void RecentProjects::add(const std::string& projectDirectory) {
    remove(projectDirectory);
    paths.insert(paths.begin(), projectDirectory);
    if (paths.size() > kMaximum) paths.resize(kMaximum);
}

void RecentProjects::remove(const std::string& projectDirectory) {
    paths.erase(std::remove(paths.begin(), paths.end(), projectDirectory), paths.end());
}

bool RecentProjects::load(const std::string& file) {
    paths.clear();
    std::optional<std::string> contents = text::readFile(file);
    if (!contents) return false;
    for (const std::vector<std::string>& words : text::parseLines(*contents)) {
        if (words.size() >= 2 && words[0] == "project") paths.push_back(words[1]);
    }
    return true;
}

bool RecentProjects::save(const std::string& file) const {
    std::string out = "molten-recent 1\n";
    for (const std::string& path : paths) out += "project " + text::quote(path) + "\n";
    return text::writeFile(file, out);
}

std::string userDataDirectory() {
    const char* home = std::getenv("HOME");
    fs::path directory = fs::path(home ? home : ".") / "Library" / "Application Support" / "MoltenMetalEngine";
    std::error_code code;
    fs::create_directories(directory, code);
    return directory.string();
}

}  // namespace molten
