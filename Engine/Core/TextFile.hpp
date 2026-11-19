// TextFile.hpp
// Small helpers for the line-based text files Molten keeps next to a game's assets
// (projects, asset metadata): `keyword value`, one per line, strings in quotes.
#pragma once

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

}  // namespace molten::text
