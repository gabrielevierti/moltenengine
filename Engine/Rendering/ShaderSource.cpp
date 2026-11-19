#include "Rendering/ShaderSource.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>

namespace molten {

namespace fs = std::filesystem;

namespace {

void appendFile(const fs::path& root, const fs::path& file, std::set<std::string>& included, ShaderSource& out) {
    std::string key = fs::weakly_canonical(file).string();
    if (!included.insert(key).second) return;                   // already part of the text

    std::ifstream stream(file);
    if (!stream) {
        out.error += "cannot read " + file.string() + "\n";
        return;
    }
    out.files.push_back(file.string());
    std::string name = fs::relative(file, root).generic_string();
    out.text += "#line 1 \"" + name + "\"\n";

    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        lineNumber++;
        std::string_view trimmed(line);
        size_t first = trimmed.find_first_not_of(" \t");
        trimmed = first == std::string_view::npos ? std::string_view{} : trimmed.substr(first);

        const std::string_view directive = "#include \"";
        if (trimmed.substr(0, directive.size()) != directive) {
            out.text += line;
            out.text += '\n';
            continue;
        }
        size_t close = trimmed.find('"', directive.size());
        std::string includeName(trimmed.substr(directive.size(), close == std::string_view::npos ? std::string_view::npos : close - directive.size()));
        fs::path target = file.parent_path() / includeName;      // next to the including file first,
        if (!fs::exists(target)) target = root / includeName;    // then relative to the shader folder
        if (!fs::exists(target)) {
            out.error += name + ":" + std::to_string(lineNumber) + ": cannot find \"" + includeName + "\"\n";
            out.text += '\n';
            continue;
        }
        appendFile(root, target, included, out);
        out.text += "#line " + std::to_string(lineNumber + 1) + " \"" + name + "\"\n";
    }
}

}  // namespace

ShaderSource ShaderSource::load(const std::string& directory) {
    ShaderSource out;
    fs::path root(directory);
    std::error_code code;
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, code)) {
        if (entry.is_regular_file() && entry.path().extension() == ".metal") files.push_back(entry.path());
    }
    if (code) {
        out.error = "cannot open shader directory " + directory + "\n";
        return out;
    }
    std::sort(files.begin(), files.end());                       // a stable order, whatever the file system returns

    std::set<std::string> included;
    for (const fs::path& file : files) appendFile(root, file, included, out);
    return out;
}

}  // namespace molten
