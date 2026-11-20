#include "Game/Component.hpp"

#include <map>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace molten {

namespace {

/// The shortest text that reads back as exactly the same float: 0.05, not 0.0500000007.
std::string floatText(float value) {
    char buffer[32];
    for (int digits = 6; digits <= 9; digits++) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", digits, double(value));
        if (std::strtof(buffer, nullptr) == value) break;
    }
    return buffer;
}

/// Up to `count` numbers separated by spaces; the ones that are missing stay as they were.
void readFloats(const std::string& text, float* values, int count) {
    const char* cursor = text.c_str();
    for (int i = 0; i < count; i++) {
        char* end = nullptr;
        float value = std::strtof(cursor, &end);
        if (end == cursor) return;
        values[i] = value;
        cursor = end;
    }
}

}  // namespace

std::string Field::label() const {
    std::string out;
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c == '_') {
            out += ' ';
            continue;
        }
        bool isUpper = c >= 'A' && c <= 'Z';
        // A space where a new word starts: before a capital that follows a small letter or a digit.
        if (isUpper && i > 0 && !(name[i - 1] >= 'A' && name[i - 1] <= 'Z') && name[i - 1] != '_') out += ' ';
        out += i == 0 && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
    }
    return out;
}

std::string Field::valueText(void* component) const {
    void* at = address(component);
    switch (type) {
        case FieldType::Float: return floatText(*static_cast<float*>(at));
        case FieldType::Int: return std::to_string(*static_cast<int*>(at));
        case FieldType::Bool: return *static_cast<bool*>(at) ? "1" : "0";
        case FieldType::Vec2: {
            Vec2 v = *static_cast<Vec2*>(at);
            return floatText(v.x) + " " + floatText(v.y);
        }
        case FieldType::Vec3: {
            Vec3 v = *static_cast<Vec3*>(at);
            return floatText(v.x) + " " + floatText(v.y) + " " + floatText(v.z);
        }
        case FieldType::Vec4: {
            Vec4 v = *static_cast<Vec4*>(at);
            return floatText(v.x) + " " + floatText(v.y) + " " + floatText(v.z) + " " + floatText(v.w);
        }
        case FieldType::String: return *static_cast<std::string*>(at);
    }
    return {};
}

void Field::setValueText(void* component, const std::string& text) const {
    void* at = address(component);
    switch (type) {
        case FieldType::Float: readFloats(text, static_cast<float*>(at), 1); break;
        case FieldType::Int: {
            char* end = nullptr;
            long value = std::strtol(text.c_str(), &end, 10);
            if (end != text.c_str()) *static_cast<int*>(at) = int(value);
            break;
        }
        case FieldType::Bool: {
            // "1" and "0" are what is written; a hand-edited "true" is understood too.
            if (!text.empty()) *static_cast<bool*>(at) = text[0] == '1' || text[0] == 't' || text[0] == 'T';
            break;
        }
        case FieldType::Vec2: {
            Vec2& v = *static_cast<Vec2*>(at);
            float values[2] = {v.x, v.y};
            readFloats(text, values, 2);
            v = Vec2{values[0], values[1]};
            break;
        }
        case FieldType::Vec3: {
            Vec3& v = *static_cast<Vec3*>(at);
            float values[3] = {v.x, v.y, v.z};
            readFloats(text, values, 3);
            v = Vec3{values[0], values[1], values[2]};
            break;
        }
        case FieldType::Vec4: {
            Vec4& v = *static_cast<Vec4*>(at);
            float values[4] = {v.x, v.y, v.z, v.w};
            readFloats(text, values, 4);
            v = Vec4{values[0], values[1], values[2], values[3]};
            break;
        }
        case FieldType::String: *static_cast<std::string*>(at) = text; break;
    }
}

const Field* ComponentType::findField(const std::string& fieldName) const {
    for (const Field& field : fields) {
        if (field.name == fieldName) return &field;
    }
    return nullptr;
}

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry registry;
    return registry;
}

void ComponentRegistry::add(ComponentType type) {
    for (ComponentType& existing : types_) {
        if (existing.name == type.name) {
            // The engine's own, declared after the game's (in a program the game's code is
            // linked into): the game's stays, and the engine's waits behind it.
            if (existing.module != nullptr && type.module == nullptr) {
                // Filed where the engine files its own, unless the game's says otherwise.
                if (existing.category.empty() || existing.category == "Scripts") existing.category = type.category;
                auto waiting = std::find_if(replaced.begin(), replaced.end(), [&](const ComponentType& r) { return r.name == type.name; });
                if (waiting != replaced.end()) *waiting = std::move(type);
                else replaced.push_back(std::move(type));
                return;
            }
            // A game's own Follow takes the place of the one that comes with the engine,
            // which is kept for when the game's code is not there.
            if (existing.module == nullptr && type.module != nullptr) {
                if (type.category.empty() || type.category == "Scripts") type.category = existing.category;
                replaced.push_back(existing);
            }
            existing = std::move(type);
            return;
        }
    }
    types_.push_back(std::move(type));
}

void ComponentRegistry::remove(const std::string& name) {
    types_.erase(std::remove_if(types_.begin(), types_.end(), [&](const ComponentType& type) { return type.name == name; }), types_.end());
}

void ComponentRegistry::removeModule(const void* module) {
    types_.erase(std::remove_if(types_.begin(), types_.end(), [module](const ComponentType& type) { return type.module == module; }),
                 types_.end());
    for (ComponentType& type : replaced) {
        if (!find(type.name)) types_.push_back(type);
    }
    replaced.clear();
}

void ComponentRegistry::setCategory(const std::string& name, const std::string& category) {
    for (ComponentType& type : types_) {
        if (type.name == name) type.category = category;
    }
}

namespace {
std::map<std::string, std::string>& displayNames() {
    static std::map<std::string, std::string> names;
    return names;
}
}  // namespace

void ComponentRegistry::setDisplayName(const std::string& typeName, const std::string& shown) { displayNames()[typeName] = shown; }

std::string ComponentRegistry::displayName(const std::string& typeName) {
    auto named = displayNames().find(typeName);
    if (named != displayNames().end()) return named->second;
    std::string out;
    for (size_t i = 0; i < typeName.size(); i++) {
        char c = typeName[i];
        bool isUpper = c >= 'A' && c <= 'Z';
        // A space before a capital that starts a word: "PlayerMover", but not inside "HUD".
        if (i > 0 && isUpper) {
            char before = typeName[i - 1];
            bool beforeIsLower = before >= 'a' && before <= 'z';
            bool nextIsLower = i + 1 < typeName.size() && typeName[i + 1] >= 'a' && typeName[i + 1] <= 'z';
            if (beforeIsLower || (before >= 'A' && before <= 'Z' && nextIsLower)) out += ' ';
        }
        out += c;
    }
    return out;
}

const ComponentType* ComponentRegistry::find(const std::string& name) const {
    for (const ComponentType& type : types_) {
        if (type.name == name) return &type;
    }
    return nullptr;
}

}  // namespace molten
