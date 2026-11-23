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
    if (!showsSplash) out += "splash 0\n";
    if (!icon.empty()) out += "icon " + text::quote(icon) + "\n";
    return out;
}

std::optional<Project> Project::fromText(const std::string& contents) {
    std::vector<std::vector<std::string>> lines = text::parseLines(contents);
    if (lines.empty() || lines[0][0] != "molten-project") return std::nullopt;
    // Any version is read: a project only names things, and an older editor opening a newer
    // project should still find its name and scenes (the rest it skips).
    Project project;
    for (size_t i = 1; i < lines.size(); i++) {
        const std::vector<std::string>& words = lines[i];
        if (words.size() < 2) continue;             // a statement this version does not know: skipped
        if (words[0] == "name") project.name = words[1];
        else if (words[0] == "startScene") project.startScene = words[1];
        else if (words[0] == "version") project.version = words[1];
        else if (words[0] == "identifier") project.identifier = words[1];
        else if (words[0] == "splash") project.showsSplash = words[1] != "0";
        else if (words[0] == "icon") project.icon = words[1];
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

std::optional<Project> Project::create(const std::string& parentDirectory, const std::string& name, std::string* error,
                                       const std::string& templateDirectory) {
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
    for (const char* folder : {"Assets/Scenes", "Assets/Models", "Assets/Textures", "Assets/Audio", "Assets/Scripts", "Plugins", "Settings", "Library"}) {
        fs::create_directories(directory / folder, code);
        if (code) {
            fail(error, "Cannot create " + (directory / folder).string() + ": " + code.message());
            return std::nullopt;
        }
    }
    text::writeFile((directory / ".gitignore").string(), kGitIgnore);
    // An empty project has no scripts: its Scripts folder is for the ones its maker writes
    // (Assets > New C++ Script, or + New > Script in the Explorer).
    if (!templateDirectory.empty()) {
        // The template's own files: what it is made of, not what the editor keeps about it.
        std::optional<Project> source = load(templateDirectory, error);
        if (!source) return std::nullopt;
        for (const char* folder : {"Assets", "Settings", "Source", "Plugins"}) {
            fs::path from = fs::path(source->directory) / folder;
            if (!fs::is_directory(from, code)) continue;
            fs::copy(from, directory / folder, fs::copy_options::recursive | fs::copy_options::overwrite_existing, code);
            if (code) {
                fail(error, "Cannot copy " + from.string() + ": " + code.message());
                return std::nullopt;
            }
        }
        project.startScene = source->startScene;
        project.showsSplash = source->showsSplash;
        // And its picture, so that the new project is shown with one from the start (the editor
        // takes its own the first time the project is saved).
        fs::copy_file(fs::path(source->directory) / "preview.png", directory / "preview.png", fs::copy_options::overwrite_existing, code);
        code.clear();
    }
    project.file = project.filePath();
    if (!project.save(error)) return std::nullopt;
    return project;
}

bool Project::save(std::string* error) const {
    if (text::writeFile(filePath(), toText())) return true;
    fail(error, "Cannot write " + filePath());
    return false;
}

// ---------------------------------------------------------------- templates

std::vector<ProjectTemplate> projectTemplates(const std::string& templatesDirectory) {
    std::vector<ProjectTemplate> found;
    std::error_code code;
    for (const fs::directory_entry& entry : fs::directory_iterator(templatesDirectory, code)) {
        if (!entry.is_directory(code)) continue;
        std::optional<Project> project = Project::load(entry.path().string());
        if (!project) continue;
        ProjectTemplate item;
        item.name = project->name;
        item.directory = project->directory;
        if (std::optional<std::string> description = text::readFile((entry.path() / "template.txt").string())) {
            item.description = *description;
            while (!item.description.empty() && (item.description.back() == '\n' || item.description.back() == ' ')) item.description.pop_back();
        }
        found.push_back(std::move(item));
    }
    std::sort(found.begin(), found.end(), [](const ProjectTemplate& a, const ProjectTemplate& b) { return a.name < b.name; });
    return found;
}

std::vector<ProjectTemplate> projectTemplates(const std::string& enginesDirectory, const std::string& yoursDirectory) {
    std::vector<ProjectTemplate> found = projectTemplates(enginesDirectory);
    for (ProjectTemplate& yours : projectTemplates(yoursDirectory)) {
        yours.isYours = true;
        auto same = std::find_if(found.begin(), found.end(), [&](const ProjectTemplate& item) { return !item.isYours && item.name == yours.name; });
        if (same != found.end()) {
            yours.original = same->directory;
            *same = std::move(yours);
        } else {
            found.push_back(std::move(yours));
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const ProjectTemplate& a, const ProjectTemplate& b) { return a.name < b.name; });
    return found;
}

std::string yourTemplatesDirectory() {
    const char* home = std::getenv("HOME");
    return (fs::path(home ? home : ".") / "Documents" / "Molten Templates").string();
}

std::string copyAsTemplate(const std::string& projectDirectory, const std::string& templatesDirectory, const std::string& name, std::string* error) {
    std::optional<Project> source = Project::load(projectDirectory, error);
    if (!source) return {};
    std::error_code code;
    fs::path destination = fs::path(templatesDirectory) / Project::fileNameFor(name.empty() ? source->name : name);
    if (fs::exists(destination, code) && !fs::is_empty(destination, code)) {
        fail(error, destination.string() + " already exists");
        return {};
    }
    fs::create_directories(destination, code);
    for (const char* folder : {"Assets", "Settings", "Source", "Plugins"}) {
        fs::path from = fs::path(source->directory) / folder;
        if (!fs::is_directory(from, code)) continue;
        fs::copy(from, destination / folder, fs::copy_options::recursive | fs::copy_options::overwrite_existing, code);
        if (code) {
            fail(error, "Cannot copy " + from.string() + ": " + code.message());
            return {};
        }
    }
    for (const char* file : {"template.txt", "preview.png", ".gitignore"}) {
        fs::path from = fs::path(source->directory) / file;
        if (fs::exists(from, code)) fs::copy_file(from, destination / file, fs::copy_options::overwrite_existing, code);
        code.clear();
    }
    Project copy = *source;
    if (!name.empty()) copy.name = name;
    copy.directory = destination.lexically_normal().string();
    copy.file.clear();                          // (else it is the original's file, and that one would be written over)
    copy.file = copy.filePath();
    if (!copy.save(error)) return {};
    fs::create_directories(destination / "Library", code);
    return copy.directory;
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
