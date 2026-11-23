#include "Assets/AssetDatabase.hpp"

#include "Core/TextFile.hpp"

#include <sstream>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <random>

namespace molten {

namespace fs = std::filesystem;

namespace {

const char* const kMetaExtension = ".meta";

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::string lower(std::string value) {
    for (char& c : value) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return value;
}

bool isMetaFile(const fs::path& path) { return path.extension() == kMetaExtension; }

/// The guid in a .meta file, or "" if the file is missing or is not one of ours.
std::string readGuid(const fs::path& metaPath) {
    std::optional<std::string> contents = text::readFile(metaPath.string());
    if (!contents) return {};
    std::vector<std::vector<std::string>> lines = text::parseLines(*contents);
    if (lines.empty() || lines[0][0] != "molten-meta") return {};
    for (const std::vector<std::string>& words : lines) {
        if (words.size() >= 2 && words[0] == "guid") return words[1];
    }
    return {};
}

bool writeGuid(const fs::path& metaPath, const std::string& guid) {
    // A file's import settings live in its .meta too (Assets/ImportSettings.hpp): kept.
    std::string settings;
    if (std::optional<std::string> contents = text::readFile(metaPath.string())) {
        std::istringstream lines(*contents);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.rfind("import ", 0) == 0) settings += line + "\n";
        }
    }
    return text::writeFile(metaPath.string(), "molten-meta 1\nguid " + guid + "\n" + settings);
}

std::string joinPath(const std::string& folder, const std::string& name) {
    return folder.empty() ? name : folder + "/" + name;
}

}  // namespace

std::string AssetInfo::parent() const {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string AssetInfo::stem() const {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

AssetType AssetDatabase::typeOfExtension(const std::string& extension) {
    std::string e = lower(extension);
    if (e == ".scene") return AssetType::Scene;
    if (e == ".graph") return AssetType::Graph;
    if (e == ".prefab") return AssetType::Prefab;
    if (e == ".material") return AssetType::Material;
    if (e == ".obj" || e == ".gltf" || e == ".glb" || e == ".fbx" || e == ".dae" || e == ".stl" || e == ".ply") return AssetType::Mesh;
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".hdr" || e == ".exr" || e == ".ktx2") return AssetType::Texture;
    if (e == ".wav" || e == ".mp3" || e == ".flac" || e == ".ogg" || e == ".m4a") return AssetType::Audio;
    if (e == ".ttf" || e == ".otf") return AssetType::Font;
    if (e == ".metal" || e == ".shader" || e == ".shadergraph") return AssetType::Shader;
    if (e == ".effect") return AssetType::Effect;
    if (e == ".anim") return AssetType::Animation;
    if (e == ".animator") return AssetType::Controller;
    if (e == ".timeline") return AssetType::Timeline;
    if (e == ".cpp" || e == ".hpp" || e == ".h" || e == ".mm") return AssetType::Script;
    if (e == ".txt" || e == ".md" || e == ".json") return AssetType::Text;
    return AssetType::Other;
}

const char* AssetDatabase::typeName(AssetType type) {
    switch (type) {
        case AssetType::Folder: return "Folder";
        case AssetType::Scene: return "Scene";
        case AssetType::Mesh: return "Model";
        case AssetType::Texture: return "Texture";
        case AssetType::Audio: return "Audio";
        case AssetType::Font: return "Font";
        case AssetType::Shader: return "Shader";
        case AssetType::Script: return "Code";
        case AssetType::Text: return "Text";
        case AssetType::Other: return "File";
        case AssetType::Graph: return "Visual Script";
        case AssetType::Prefab: return "Prefab";
        case AssetType::Effect: return "Effect";
        case AssetType::Animation: return "Animation Clip";
        case AssetType::Controller: return "Animator Controller";
        case AssetType::Timeline: return "Timeline";
        case AssetType::Material: return "Material";
    }
    return "File";
}

bool AssetDatabase::isValidName(const std::string& name) {
    if (name.empty() || name.size() > 200 || name[0] == '.' || name.back() == ' ') return false;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '\n' || c == '\r' || c == '\0') return false;
    }
    return lower(fs::path(name).extension().string()) != kMetaExtension;
}

std::string AssetDatabase::makeGuid() {
    // 128 random bits, as 32 hexadecimal digits.
    static std::mt19937_64 generator = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(),
                           uint32_t(std::chrono::steady_clock::now().time_since_epoch().count())};
        return std::mt19937_64(seed);
    }();
    static const char digits[] = "0123456789abcdef";
    std::string guid;
    for (int half = 0; half < 2; half++) {
        uint64_t bits = generator();
        for (int i = 0; i < 16; i++) {
            guid += digits[bits & 15];
            bits >>= 4;
        }
    }
    return guid;
}

void AssetDatabase::open(const std::string& assetsDirectory) {
    directory_ = assetsDirectory;
    assets_.clear();
    byPath_.clear();
    byGuid_.clear();
    revision_++;
    if (!directory_.empty()) refresh();
}

bool AssetDatabase::refresh() {
    if (directory_.empty()) return false;
    std::vector<AssetInfo> found;
    std::vector<fs::path> metaFiles;
    std::error_code code;
    fs::path root(directory_);
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, code), end;
    for (; !code && it != end; it.increment(code)) {
        const fs::directory_entry& entry = *it;
        std::string fileName = entry.path().filename().string();
        if (fileName.empty() || fileName[0] == '.') {          // .DS_Store, .git and friends
            if (entry.is_directory(code)) it.disable_recursion_pending();
            continue;
        }
        if (isMetaFile(entry.path())) {
            metaFiles.push_back(entry.path());
            continue;
        }
        AssetInfo info;
        info.path = fs::relative(entry.path(), root, code).generic_string();
        info.name = fileName;
        if (entry.is_directory(code)) {
            info.type = AssetType::Folder;
        } else if (entry.is_regular_file(code)) {
            info.type = typeOfExtension(entry.path().extension().string());
            info.size = uint64_t(entry.file_size(code));
            info.modified = int64_t(entry.last_write_time(code).time_since_epoch().count());
            fs::path metaPath = entry.path();
            metaPath += kMetaExtension;
            info.guid = readGuid(metaPath);
            if (info.guid.empty()) {
                info.guid = makeGuid();
                writeGuid(metaPath, info.guid);
            }
        } else {
            continue;
        }
        found.push_back(std::move(info));
    }

    // A .meta whose asset is gone is only clutter. Only files that are really ours are removed.
    for (const fs::path& metaPath : metaFiles) {
        fs::path asset = metaPath;
        asset.replace_extension();                              // "wall.png.meta" -> "wall.png"
        if (!fs::exists(asset, code) && !readGuid(metaPath).empty()) fs::remove(metaPath, code);
    }

    std::sort(found.begin(), found.end(), [](const AssetInfo& a, const AssetInfo& b) { return a.path < b.path; });

    bool changed = found.size() != assets_.size();
    for (size_t i = 0; !changed && i < found.size(); i++) {
        const AssetInfo& a = found[i];
        const AssetInfo& b = assets_[i];
        changed = a.path != b.path || a.guid != b.guid || a.size != b.size || a.modified != b.modified;
    }
    if (!changed) return false;

    assets_ = std::move(found);
    byPath_.clear();
    byGuid_.clear();
    for (size_t i = 0; i < assets_.size(); i++) {
        byPath_[assets_[i].path] = i;
        if (!assets_[i].guid.empty()) byGuid_[assets_[i].guid] = i;
    }
    revision_++;
    return true;
}

std::vector<const AssetInfo*> AssetDatabase::children(const std::string& folder) const {
    std::vector<const AssetInfo*> result;
    for (const AssetInfo& info : assets_) {
        if (info.parent() == folder) result.push_back(&info);
    }
    std::sort(result.begin(), result.end(), [](const AssetInfo* a, const AssetInfo* b) {
        if (a->isFolder() != b->isFolder()) return a->isFolder();
        return lower(a->name) < lower(b->name);
    });
    return result;
}

const AssetInfo* AssetDatabase::find(const std::string& path) const {
    auto found = byPath_.find(path);
    return found == byPath_.end() ? nullptr : &assets_[found->second];
}

const AssetInfo* AssetDatabase::findByGuid(const std::string& guid) const {
    auto found = byGuid_.find(guid);
    return found == byGuid_.end() ? nullptr : &assets_[found->second];
}

std::string AssetDatabase::uniquePath(const std::string& folder, const std::string& stem, const std::string& extension) const {
    std::error_code code;
    for (int number = 1; number < 10000; number++) {
        std::string name = number == 1 ? stem + extension : stem + " " + std::to_string(number) + extension;
        std::string candidate = joinPath(folder, name);
        if (!fs::exists(fs::path(absolutePath(candidate)), code)) return candidate;
    }
    return joinPath(folder, stem + " " + makeGuid().substr(0, 8) + extension);
}

std::string AssetDatabase::createFolder(const std::string& parent, const std::string& name, std::string* error) {
    if (!isValidName(name)) {
        fail(error, "\"" + name + "\" cannot be used as a name");
        return {};
    }
    std::string path = uniquePath(parent, name, "");
    std::error_code code;
    fs::create_directories(fs::path(absolutePath(path)), code);
    if (code) {
        fail(error, "Cannot create the folder: " + code.message());
        return {};
    }
    refresh();
    return path;
}

std::string AssetDatabase::rename(const std::string& path, const std::string& newName, std::string* error) {
    const AssetInfo* info = find(path);
    if (!info) {
        fail(error, path + " is not in the project");
        return {};
    }
    if (!isValidName(newName)) {
        fail(error, "\"" + newName + "\" cannot be used as a name");
        return {};
    }
    std::string newPath = joinPath(info->parent(), newName);
    if (newPath == path) return path;
    std::error_code code;
    fs::path from(absolutePath(path)), to(absolutePath(newPath));
    // On a file system that ignores case, "Wall.png" exists when asked about "wall.png":
    // changing only the case is still allowed.
    if (fs::exists(to, code) && lower(newPath) != lower(path)) {
        fail(error, newName + " already exists");
        return {};
    }
    fs::rename(from, to, code);
    if (code) {
        fail(error, "Cannot rename: " + code.message());
        return {};
    }
    if (!info->isFolder()) {
        fs::path metaFrom = from, metaTo = to;
        metaFrom += kMetaExtension;
        metaTo += kMetaExtension;
        fs::rename(metaFrom, metaTo, code);         // without it the file would get a new guid
    }
    refresh();
    return newPath;
}

std::string AssetDatabase::move(const std::string& path, const std::string& newParent, std::string* error) {
    const AssetInfo* info = find(path);
    if (!info) {
        fail(error, path + " is not in the project");
        return {};
    }
    if (!newParent.empty() && (!find(newParent) || !find(newParent)->isFolder())) {
        fail(error, newParent + " is not a folder of the project");
        return {};
    }
    if (info->parent() == newParent) return path;
    // A folder cannot go inside itself.
    if (info->isFolder() && (newParent == path || newParent.compare(0, path.size() + 1, path + "/") == 0)) {
        fail(error, "A folder cannot be moved into itself");
        return {};
    }
    std::string newPath = joinPath(newParent, info->name);
    std::error_code code;
    fs::path from(absolutePath(path)), to(absolutePath(newPath));
    if (fs::exists(to, code)) {
        fail(error, info->name + " already exists in that folder");
        return {};
    }
    fs::rename(from, to, code);
    if (code) {
        fail(error, "Cannot move: " + code.message());
        return {};
    }
    if (!info->isFolder()) {
        fs::path metaFrom = from, metaTo = to;
        metaFrom += kMetaExtension;
        metaTo += kMetaExtension;
        fs::rename(metaFrom, metaTo, code);
    }
    refresh();
    return newPath;
}

std::string AssetDatabase::replaceReferences(const std::string& text, const std::string& from, const std::string& to) {
    if (from.empty() || from == to) return text;
    std::string out;
    out.reserve(text.size());
    size_t at = 0;
    while (true) {
        size_t found = text.find(from, at);
        if (found == std::string::npos) break;
        size_t end = found + from.size();
        char before = found > 0 ? text[found - 1] : '\0';
        char after = end < text.size() ? text[end] : '\0';
        // A whole path: it starts a quoted value or an item of a comma list, and ends one, or
        // goes on into the folder ("Models/" in "Models/car.obj").
        bool isWhole = (before == '"' || before == ',') && (after == '"' || after == ',' || after == '/');
        out.append(text, at, found - at);
        out += isWhole ? to : from;
        at = end;
    }
    out.append(text, at, std::string::npos);
    return out;
}

int AssetDatabase::retargetReferences(const std::string& from, const std::string& to) {
    int written = 0;
    for (const AssetInfo& asset : assets_) {
        switch (asset.type) {
            case AssetType::Scene: case AssetType::Prefab: case AssetType::Material: case AssetType::Effect: case AssetType::Animation:
            case AssetType::Controller: case AssetType::Timeline: case AssetType::Graph: break;
            default: continue;
        }
        std::string file = absolutePath(asset.path);
        std::optional<std::string> text = text::readFile(file);
        if (!text || text->find(from) == std::string::npos) continue;
        std::string changed = replaceReferences(*text, from, to);
        if (changed != *text && text::writeFile(file, changed)) written++;
    }
    return written;
}

std::string AssetDatabase::importFile(const std::string& sourcePath, const std::string& folder, std::string* error) {
    std::error_code code;
    fs::path source(sourcePath);
    if (!fs::is_regular_file(source, code)) {
        fail(error, sourcePath + " is not a file");
        return {};
    }
    std::string fileName = source.filename().string();
    if (!isValidName(fileName)) {
        fail(error, "\"" + fileName + "\" cannot be used as a name");
        return {};
    }
    std::string path = uniquePath(folder, source.stem().string(), source.extension().string());
    fs::create_directories(fs::path(absolutePath(folder)), code);
    fs::copy_file(source, fs::path(absolutePath(path)), code);
    if (code) {
        fail(error, "Cannot copy " + fileName + ": " + code.message());
        return {};
    }
    refresh();
    return path;
}

}  // namespace molten
