#include "Core/SaveGame.hpp"

#include "Core/Log.hpp"
#include "Core/TextFile.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace molten {

namespace {

/// A name or a text in quotes, with \" and \\ for the quote and the backslash themselves.
std::string quoted(const std::string& value) {
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

/// Reads a quoted string from `at` in `line`, moving `at` past it.
bool readQuoted(const std::string& line, size_t& at, std::string& out) {
    while (at < line.size() && line[at] == ' ') at++;
    if (at >= line.size() || line[at] != '"') return false;
    out.clear();
    for (at++; at < line.size(); at++) {
        char c = line[at];
        if (c == '\\' && at + 1 < line.size()) {
            char next = line[++at];
            out += next == 'n' ? '\n' : next;
        } else if (c == '"') {
            at++;
            return true;
        } else {
            out += c;
        }
    }
    return false;
}

}  // namespace

void SaveGame::setNumber(const std::string& name, float value) {
    Value& slot = values_[name];
    slot = Value{};
    slot.kind = Value::Kind::Number;
    slot.number = value;
}

void SaveGame::setText(const std::string& name, const std::string& value) {
    Value& slot = values_[name];
    slot = Value{};
    slot.kind = Value::Kind::Text;
    slot.text = value;
}

void SaveGame::setVector(const std::string& name, Vec3 value) {
    Value& slot = values_[name];
    slot = Value{};
    slot.kind = Value::Kind::Vector;
    slot.vector = value;
}

float SaveGame::number(const std::string& name, float fallback) const {
    auto found = values_.find(name);
    return found != values_.end() && found->second.kind == Value::Kind::Number ? found->second.number : fallback;
}

std::string SaveGame::text(const std::string& name, const std::string& fallback) const {
    auto found = values_.find(name);
    return found != values_.end() && found->second.kind == Value::Kind::Text ? found->second.text : fallback;
}

Vec3 SaveGame::vector(const std::string& name, Vec3 fallback) const {
    auto found = values_.find(name);
    return found != values_.end() && found->second.kind == Value::Kind::Vector ? found->second.vector : fallback;
}

bool SaveGame::has(const std::string& name) const {
    return values_.count(name) > 0;
}

void SaveGame::remove(const std::string& name) {
    values_.erase(name);
}

std::vector<std::string> SaveGame::names() const {
    std::vector<std::string> out;
    for (const auto& entry : values_) out.push_back(entry.first);
    return out;
}

std::string SaveGame::toText() const {
    std::string out = "molten-save 1\n";
    char number[96];
    for (const auto& [name, value] : values_) {
        switch (value.kind) {
            case Value::Kind::Number:
                std::snprintf(number, sizeof(number), " %.9g\n", double(value.number));
                out += "number " + quoted(name) + number;
                break;
            case Value::Kind::Text:
                out += "text " + quoted(name) + " " + quoted(value.text) + "\n";
                break;
            case Value::Kind::Vector:
                std::snprintf(number, sizeof(number), " %.9g %.9g %.9g\n", double(value.vector.x), double(value.vector.y), double(value.vector.z));
                out += "vector " + quoted(name) + number;
                break;
        }
    }
    return out;
}

SaveGame SaveGame::fromText(const std::string& text) {
    SaveGame save;
    // A save from a newer version of the game's engine is not guessed at: it reads as empty.
    if (text.rfind("molten-save", 0) == 0) {
        if (std::string why = text::checkHeader(text::firstLine(text), "molten-save"); !why.empty()) {
            logWarning("the saved game can't be read: %s", why.c_str());
            return save;
        }
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string kind = line.substr(0, space), name;
        size_t at = space;
        if (!readQuoted(line, at, name)) continue;
        const char* rest = line.c_str() + at;
        if (kind == "number") {
            save.setNumber(name, std::strtof(rest, nullptr));
        } else if (kind == "vector") {
            char* end = nullptr;
            float x = std::strtof(rest, &end);
            float y = std::strtof(end, &end);
            float z = std::strtof(end, &end);
            save.setVector(name, Vec3{x, y, z});
        } else if (kind == "text") {
            std::string value;
            if (readQuoted(line, at, value)) save.setText(name, value);
        }
    }
    return save;
}

}  // namespace molten
