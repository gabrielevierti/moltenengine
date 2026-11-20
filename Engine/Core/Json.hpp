// Json.hpp
// Reading JSON: the text format glTF models (and much else) describe themselves in.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace molten {

/// A JSON value: nothing, a yes or no, a number, a text, a list, or named values.
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    bool isNull() const { return kind == Kind::Null; }
    /// The value of a name; a null value if this is not an object or has no such name.
    const Json& operator[](const std::string& name) const;
    /// An element of a list; a null value if out of range.
    const Json& operator[](size_t index) const;
    size_t size() const { return kind == Kind::Array ? array.size() : 0; }
    bool has(const std::string& name) const { return kind == Kind::Object && object.count(name) > 0; }

    double asNumber(double fallback = 0) const { return kind == Kind::Number ? number : fallback; }
    int asInt(int fallback = 0) const { return kind == Kind::Number ? int(number) : fallback; }
    std::string asString(const std::string& fallback = "") const { return kind == Kind::String ? string : fallback; }

    /// The value a text describes, or nothing if it is not JSON.
    static std::optional<Json> parse(const std::string& text);
};

}  // namespace molten
