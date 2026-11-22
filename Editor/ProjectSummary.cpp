#include "ProjectSummary.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace molten::editor {

namespace {

namespace fs = std::filesystem;

std::string lowered(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

long long secondsSince(fs::file_time_type time) {
    return std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - time).count();
}

/// How many entities a scene file has: each starts a line with the word.
int countEntities(const fs::path& file) {
    std::ifstream in(file);
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (line.compare(0, 7, "entity ") == 0) count++;
    }
    return count;
}

struct Walk {
    ProjectSummary& summary;
    size_t maxEntries;
    fs::path root;
    fs::file_time_type newest{};
    bool hasNewest = false;

    /// Lists a folder into the summary and returns what it holds: files, bytes.
    void folder(const fs::path& directory, int depth, bool isInAssets, int& filesOut, uint64_t& bytesOut) {
        std::error_code code;
        std::vector<fs::directory_entry> inside;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, code)) {
            std::string name = entry.path().filename().string();
            // Hidden files, and the identifiers the editor keeps beside each asset.
            if (name.empty() || name[0] == '.' || entry.path().extension() == ".meta") continue;
            inside.push_back(entry);
        }
        // Folders first, then files, each by name whatever the capitals.
        std::sort(inside.begin(), inside.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            std::error_code ignored;
            bool aFolder = a.is_directory(ignored), bFolder = b.is_directory(ignored);
            if (aFolder != bFolder) return aFolder;
            return lowered(a.path().filename().string()) < lowered(b.path().filename().string());
        });
        for (const fs::directory_entry& entry : inside) {
            std::error_code ignored;
            ProjectSummary::Entry listed;
            listed.name = entry.path().filename().string();
            listed.path = fs::relative(entry.path(), root, ignored).generic_string();
            listed.depth = depth;
            listed.isFolder = entry.is_directory(ignored);
            size_t place = summary.entries.size();
            bool isListed = place < maxEntries;
            if (isListed) summary.entries.push_back(listed);
            else summary.isCutShort = true;

            if (listed.isFolder) {
                int files = 0;
                uint64_t bytes = 0;
                listed.type = AssetType::Folder;
                if (depth == 0 && listed.name == "Library") {
                    // What the editor can make again: named, not looked into.
                    listed.note = "rebuilt by the editor";
                } else {
                    folder(entry.path(), depth + 1, isInAssets || (depth == 0 && listed.name == "Assets"), files, bytes);
                }
                listed.files = files;
                listed.bytes = bytes;
                filesOut += files;
                bytesOut += bytes;
            } else {
                listed.type = AssetDatabase::typeOfExtension(entry.path().extension().string());
                listed.bytes = entry.file_size(ignored);
                if (ignored) listed.bytes = 0;
                filesOut++;
                bytesOut += listed.bytes;
                std::error_code timeCode;
                fs::file_time_type time = entry.last_write_time(timeCode);
                if (!timeCode && (!hasNewest || time > newest)) {
                    newest = time;
                    hasNewest = true;
                }
                if (isInAssets) file(entry, listed, timeCode ? fs::file_time_type{} : time, !timeCode);
            }
            if (isListed) summary.entries[place] = listed;
        }
    }

    /// A file of Assets: counted with its kind; a scene is kept by name too.
    void file(const fs::directory_entry& entry, const ProjectSummary::Entry& listed, fs::file_time_type time, bool hasTime) {
        summary.files++;
        summary.assetBytes += listed.bytes;
        switch (listed.type) {
            case AssetType::Mesh: summary.models++; break;
            case AssetType::Texture: summary.textures++; break;
            case AssetType::Material: summary.materials++; break;
            case AssetType::Audio: summary.sounds++; break;
            case AssetType::Graph: summary.graphs++; break;
            case AssetType::Prefab: summary.prefabs++; break;
            case AssetType::Animation: case AssetType::Controller: case AssetType::Timeline: summary.animations++; break;
            case AssetType::Script:
                // A component is its .cpp; the header beside it is the same script.
                if (entry.path().extension() == ".cpp" || entry.path().extension() == ".mm") summary.scripts++;
                break;
            case AssetType::Scene: {
                ProjectSummary::Scene scene;
                std::error_code ignored;
                scene.path = fs::relative(entry.path(), root / "Assets", ignored).generic_string();
                scene.name = entry.path().stem().string();
                scene.entities = countEntities(entry.path());
                scene.bytes = listed.bytes;
                scene.secondsAgo = hasTime ? secondsSince(time) : -1;
                summary.scenes.push_back(scene);
                break;
            }
            default: break;
        }
    }
};

}  // namespace

ProjectSummary summarizeProject(const std::string& directory, size_t maxEntries) {
    ProjectSummary summary;
    summary.directory = directory;
    std::error_code code;
    if (!fs::is_directory(directory, code)) {
        summary.problem = "This project's folder is not there anymore: it was moved or deleted.";
        return summary;
    }
    std::string error;
    if (std::optional<Project> project = Project::load(directory, &error)) {
        summary.project = *project;
        summary.isValid = true;
    } else {
        summary.problem = error.empty() ? std::string("This folder has no project file (.molten) in it.") : error;
        summary.project.name = fs::path(directory).filename().string();
    }

    Walk walk{summary, maxEntries, fs::path(directory)};
    int files = 0;
    uint64_t bytes = 0;
    walk.folder(walk.root, 0, false, files, bytes);
    if (walk.hasNewest) summary.secondsAgo = std::max(0LL, secondsSince(walk.newest));

    for (ProjectSummary::Scene& scene : summary.scenes) scene.isFirst = scene.path == summary.project.startScene;
    std::sort(summary.scenes.begin(), summary.scenes.end(), [](const ProjectSummary::Scene& a, const ProjectSummary::Scene& b) {
        if (a.isFirst != b.isFirst) return a.isFirst;
        return lowered(a.path) < lowered(b.path);
    });
    return summary;
}

std::string sizeText(uint64_t bytes) {
    char text[48];
    if (bytes < 1000) std::snprintf(text, sizeof(text), "%llu bytes", static_cast<unsigned long long>(bytes));
    else if (bytes < 1000ull * 1000) std::snprintf(text, sizeof(text), "%.0f KB", double(bytes) / 1e3);
    else if (bytes < 10ull * 1000 * 1000) std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / 1e6);
    else if (bytes < 1000ull * 1000 * 1000) std::snprintf(text, sizeof(text), "%.0f MB", double(bytes) / 1e6);
    else std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / 1e9);
    return text;
}

std::string agoText(long long seconds) {
    if (seconds < 0) return {};
    if (seconds < 60) return "just now";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min ago";
    if (seconds < 86400) {
        long long hours = seconds / 3600;
        return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
    }
    long long days = seconds / 86400;
    if (days == 1) return "yesterday";
    if (days < 60) return std::to_string(days) + " days ago";
    if (days < 730) return std::to_string(days / 30) + " months ago";
    return std::to_string(days / 365) + " years ago";
}

std::string countText(int count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

}  // namespace molten::editor
