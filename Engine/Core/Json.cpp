#include "Core/Json.hpp"

#include <cstdlib>

namespace molten {

const Json& Json::operator[](const std::string& name) const {
    static const Json null;
    if (kind != Kind::Object) return null;
    auto found = object.find(name);
    return found == object.end() ? null : found->second;
}

const Json& Json::operator[](size_t index) const {
    static const Json null;
    return kind == Kind::Array && index < array.size() ? array[index] : null;
}

namespace {

struct Reader {
    const std::string& text;
    size_t at = 0;
    bool failed = false;

    void skipSpace() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) at++;
    }
    bool take(char c) {
        skipSpace();
        if (at < text.size() && text[at] == c) {
            at++;
            return true;
        }
        return false;
    }
    bool takeWord(const char* word) {
        size_t length = std::char_traits<char>::length(word);
        if (text.compare(at, length, word) != 0) return false;
        at += length;
        return true;
    }

    std::string readString() {
        std::string out;
        while (at < text.size() && text[at] != '"') {
            char c = text[at++];
            if (c != '\\' || at >= text.size()) {
                out += c;
                continue;
            }
            char escaped = text[at++];
            switch (escaped) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // A character by its number: written out as UTF-8.
                    unsigned code = unsigned(std::strtoul(text.substr(at, 4).c_str(), nullptr, 16));
                    at = std::min(at + 4, text.size());
                    if (code < 0x80) out += char(code);
                    else if (code < 0x800) {
                        out += char(0xC0 | (code >> 6));
                        out += char(0x80 | (code & 0x3F));
                    } else {
                        out += char(0xE0 | (code >> 12));
                        out += char(0x80 | ((code >> 6) & 0x3F));
                        out += char(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: out += escaped;
            }
        }
        if (at >= text.size()) failed = true;
        else at++;                                              // the closing quote
        return out;
    }

    Json read(int depth) {
        Json value;
        skipSpace();
        if (at >= text.size() || depth > 200) {
            failed = true;
            return value;
        }
        char c = text[at];
        if (c == '{') {
            at++;
            value.kind = Json::Kind::Object;
            if (take('}')) return value;
            while (!failed) {
                skipSpace();
                if (at >= text.size() || text[at] != '"') {
                    failed = true;
                    break;
                }
                at++;
                std::string name = readString();
                if (!take(':')) {
                    failed = true;
                    break;
                }
                value.object[name] = read(depth + 1);
                if (take(',')) continue;
                if (!take('}')) failed = true;
                break;
            }
        } else if (c == '[') {
            at++;
            value.kind = Json::Kind::Array;
            if (take(']')) return value;
            while (!failed) {
                value.array.push_back(read(depth + 1));
                if (take(',')) continue;
                if (!take(']')) failed = true;
                break;
            }
        } else if (c == '"') {
            at++;
            value.kind = Json::Kind::String;
            value.string = readString();
        } else if (takeWord("true")) {
            value.kind = Json::Kind::Bool;
            value.boolean = true;
        } else if (takeWord("false")) {
            value.kind = Json::Kind::Bool;
        } else if (takeWord("null")) {
            // nothing
        } else {
            char* end = nullptr;
            value.number = std::strtod(text.c_str() + at, &end);
            size_t read = size_t(end - (text.c_str() + at));
            if (read == 0) failed = true;
            at += read;
            value.kind = Json::Kind::Number;
        }
        return value;
    }
};

}  // namespace

std::optional<Json> Json::parse(const std::string& text) {
    Reader reader{text};
    Json value = reader.read(0);
    reader.skipSpace();
    if (reader.failed || reader.at != text.size()) return std::nullopt;
    return value;
}

}  // namespace molten
