// AssetDependencies.cpp
#include "Assets/AssetDependencies.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace molten {

namespace {

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

/// Files that are where everything starts: what is reached from them is in use.
bool isRoot(const std::string& path) {
    std::string extension = lowerExtension(path);
    return extension == ".scene" || extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".mm" || extension == ".molten" ||
           extension == ".graph";
}

/// Files that are things a game uses, and so may be unused.
bool canBeUnused(const std::string& path) {
    std::string extension = lowerExtension(path);
    for (const char* kind : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".obj", ".gltf", ".glb", ".wav", ".mp3", ".flac", ".ogg", ".material",
                             ".prefab", ".effect", ".animator", ".terrain", ".timeline", ".foliage"}) {
        if (extension == kind) return true;
    }
    return false;
}

const std::vector<std::string> kNone;

}  // namespace

bool AssetDependencies::canName(const std::string& path) {
    std::string extension = lowerExtension(path);
    for (const char* kind : {".scene", ".prefab", ".material", ".animator", ".effect", ".graph", ".molten", ".timeline", ".cpp", ".hpp", ".h", ".mm", ".gltf",
                             ".glb", ".mtl", ".obj", ".json", ".txt"}) {
        if (extension == kind) return true;
    }
    return false;
}

std::vector<std::string> AssetDependencies::namedIn(const std::string& text, const std::string& from,
                                                    const std::unordered_set<std::string>& known) {
    std::unordered_set<std::string> found;
    std::string folder = std::filesystem::path(from).parent_path().generic_string();
    auto consider = [&](std::string word) {
        size_t part = word.find('#');                   // "Models/car.glb#3": the model
        if (part != std::string::npos) word.resize(part);
        while (!word.empty() && (word.back() == ',' || word.back() == ';' || word.back() == ')')) word.pop_back();
        if (word.size() < 3 || word.find('.') == std::string::npos) return;
        if (word.rfind("Assets/", 0) == 0) word = word.substr(7);
        if (known.count(word) && word != from) found.insert(word);
        if (!folder.empty()) {
            std::string beside = folder + "/" + word;   // a picture a model names beside it
            if (known.count(beside) && beside != from) found.insert(beside);
        }
    };
    // In quotes, and as words: scene and material files name assets both ways.
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '"') continue;
        size_t end = text.find('"', i + 1);
        if (end == std::string::npos || end - i > 300) continue;
        consider(text.substr(i + 1, end - i - 1));
        i = end;
    }
    size_t start = std::string::npos;
    for (size_t i = 0; i <= text.size(); i++) {
        bool isWord = i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || std::string("._-/#").find(text[i]) != std::string::npos);
        if (isWord && start == std::string::npos) start = i;
        if (!isWord && start != std::string::npos) {
            if (i - start < 300) consider(text.substr(start, i - start));
            start = std::string::npos;
        }
    }
    std::vector<std::string> out(found.begin(), found.end());
    std::sort(out.begin(), out.end());
    return out;
}

void AssetDependencies::build(const std::unordered_map<std::string, std::string>& texts, const std::vector<std::string>& files) {
    files_ = files;
    uses_.clear();
    usedBy_.clear();
    std::unordered_set<std::string> known(files.begin(), files.end());
    for (const auto& [path, text] : texts) {
        std::vector<std::string> named = namedIn(text, path, known);
        for (const std::string& used : named) usedBy_[used].push_back(path);
        uses_[path] = std::move(named);
    }
    for (auto& [path, users] : usedBy_) std::sort(users.begin(), users.end());
}

void AssetDependencies::build(const std::string& root, const std::vector<std::string>& files) {
    std::unordered_map<std::string, std::string> texts;
    for (const std::string& path : files) {
        if (!canName(path)) continue;
        std::error_code code;
        std::filesystem::path full = std::filesystem::path(root) / path;
        if (std::filesystem::file_size(full, code) > (8u << 20) || code) continue;      // not a text one reads whole
        std::ifstream in(full, std::ios::binary);
        std::stringstream read;
        read << in.rdbuf();
        texts[path] = read.str();
    }
    build(texts, files);
}

const std::vector<std::string>& AssetDependencies::uses(const std::string& path) const {
    auto found = uses_.find(path);
    return found == uses_.end() ? kNone : found->second;
}

const std::vector<std::string>& AssetDependencies::usedBy(const std::string& path) const {
    auto found = usedBy_.find(path);
    return found == usedBy_.end() ? kNone : found->second;
}

std::vector<std::string> AssetDependencies::unused() const {
    // Everything reached from the roots, through what each uses.
    std::unordered_set<std::string> reached;
    std::vector<std::string> waiting;
    for (const std::string& path : files_) {
        if (isRoot(path)) waiting.push_back(path);
    }
    while (!waiting.empty()) {
        std::string path = waiting.back();
        waiting.pop_back();
        if (!reached.insert(path).second) continue;
        for (const std::string& used : uses(path)) waiting.push_back(used);
    }
    std::vector<std::string> out;
    for (const std::string& path : files_) {
        if (canBeUnused(path) && !reached.count(path)) out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace molten
