// TextFile.hpp
// Small helpers for the line-based text files Molten keeps next to a game's assets
// (projects, asset metadata): `keyword value`, one per line, strings in quotes.
#pragma once

#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace molten::text {

inline std::optional<std::string> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

inline bool writeFile(const std::string& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << contents;
    return bool(file);
}

/// "a \"quoted\" word" with the quotes and backslashes a reader needs.
inline std::string quote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

/// One line split into words; a quoted string is one word, without its quotes.
inline std::vector<std::string> splitWords(const std::string& line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < line.size()) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
        } else if (c == '"') {
            std::string word;
            i++;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    i++;
                    word += line[i] == 'n' ? '\n' : line[i];
                } else {
                    word += line[i];
                }
                i++;
            }
            i++;                        // the closing quote
            words.push_back(std::move(word));
        } else {
            size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') i++;
            words.push_back(line.substr(start, i - start));
        }
    }
    return words;
}

/// Every non-empty line of a text, as words.
inline std::vector<std::vector<std::string>> parseLines(const std::string& contents) {
    std::vector<std::vector<std::string>> lines;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        std::vector<std::string> words = splitWords(line);
        if (!words.empty()) lines.push_back(std::move(words));
    }
    return lines;
}


// ---- the formats, and their versions
//
// Every file Molten writes starts with its format's name and a version: "molten-scene 1".
// The promise (Docs/FileFormats.md): within one version a reader skips the lines it does not
// know, so a file written by a newer engine of the same version still opens, minus what is
// new. A version goes up only when old readers would read the file wrong rather than read
// less of it; and every engine still reads all the versions before its own. A file of a
// newer version is refused with a message, not guessed at (a project file is the exception:
// it is always read, see Project.cpp).

struct FileFormat {
    const char* name;       // the first word of the file
    int version;            // the newest version this engine reads and writes
    const char* what;       // for the reference and the messages
};

inline constexpr FileFormat kFileFormats[] = {
    {"molten-project", 1, "a project (project.molten)"},
    {"molten-scene", 1, "a scene or a prefab"},
    {"molten-variant", 1, "a prefab variant"},
    {"molten-material", 1, "a material"},
    {"molten-meta", 1, "an asset's .meta: its id and import settings"},
    {"molten-graph", 1, "a visual script or a shader graph"},
    {"molten-animator", 1, "an animator controller"},
    {"molten-timeline", 1, "a timeline"},
    {"molten-input", 1, "the input actions"},
    {"molten-quality", 3, "the quality levels"},
    {"molten-game-settings", 1, "the game's settings"},
    {"molten-save", 1, "a saved game"},
    {"molten-lighting", 1, "a scene's baked light probes"},
    {"molten-recent", 1, "the editor's recent projects"},
};

/// The newest version of a format this engine knows; 0 for a name that is not one.
inline int newestVersion(const std::string& format) {
    for (const FileFormat& f : kFileFormats)
        if (format == f.name) return f.version;
    return 0;
}

/// Whether a file whose first line is `firstLine` can be read as `format`. Empty when it can;
/// otherwise the reason, for the console. A header without a number is version 1.
inline std::string checkHeader(const std::string& firstLine, const std::string& format) {
    std::vector<std::string> words = splitWords(firstLine);
    if (words.empty() || words[0] != format) return "it does not start with \"" + format + "\"";
    int version = words.size() > 1 ? std::atoi(words[1].c_str()) : 1;
    int newest = newestVersion(format);
    if (version < 1) return "\"" + words[0] + "\" has no version this engine knows";
    if (newest > 0 && version > newest)
        return "it was written by a newer Molten (" + format + " " + std::to_string(version) + "; this one reads up to " +
               std::to_string(newest) + ")";
    return {};
}

/// The first line of a text.
inline std::string firstLine(const std::string& contents) {
    size_t end = contents.find('\n');
    return contents.substr(0, end == std::string::npos ? contents.size() : end);
}

}  // namespace molten::text
