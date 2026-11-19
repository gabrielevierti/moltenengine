#include "Assets/Font.hpp"

#include <cstdlib>
#include <string>
#include <vector>

namespace molten {

uint32_t decodeUTF8(std::string_view text, size_t& index) {
    auto byte = [&](size_t i) { return i < text.size() ? uint32_t(uint8_t(text[i])) : 0u; };
    uint32_t first = byte(index);
    // The number of leading 1 bits in the first byte says how many bytes the character uses.
    int extra = first < 0x80 ? 0 : (first >> 5) == 0x6 ? 1 : (first >> 4) == 0xE ? 2 : (first >> 3) == 0x1E ? 3 : -1;
    if (extra < 0) {
        index++;
        return 0xFFFD;                              // not valid UTF-8: the replacement character
    }
    uint32_t codepoint = extra == 0 ? first : first & (0x3Fu >> extra);
    for (int i = 1; i <= extra; i++) codepoint = (codepoint << 6) | (byte(index + size_t(i)) & 0x3Fu);
    index += size_t(extra) + 1;
    return codepoint;
}

std::optional<FontMetrics> FontMetrics::parse(std::string_view text) {
    // The file is lines of numbers separated by spaces:
    //   info <bakedSize> <lineHeight> <distanceRange> <atlasWidth> <atlasHeight>
    //   <codepoint> <x> <y> <width> <height> <offsetX> <offsetY> <advance>
    FontMetrics metrics;
    bool hasInfo = false;
    while (!text.empty()) {
        size_t newline = text.find('\n');
        std::string line(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        std::vector<std::string> fields;
        size_t position = 0;
        while (position < line.size()) {
            size_t start = line.find_first_not_of(" \t\r", position);
            if (start == std::string::npos) break;
            size_t end = line.find_first_of(" \t\r", start);
            fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
            position = end == std::string::npos ? line.size() : end;
        }
        if (fields.empty()) continue;

        auto number = [&](size_t i) { return i < fields.size() ? std::strtof(fields[i].c_str(), nullptr) : 0.0f; };
        if (fields[0] == "info" && fields.size() >= 6) {
            metrics.bakedSize = number(1);
            metrics.lineHeight = number(2);
            metrics.distanceRange = number(3);
            metrics.atlasWidth = number(4);
            metrics.atlasHeight = number(5);
            hasInfo = true;
        } else if (fields.size() >= 8) {
            Glyph glyph{number(1), number(2), number(3), number(4), number(5), number(6), number(7)};
            metrics.glyphs[uint32_t(std::strtoul(fields[0].c_str(), nullptr, 10))] = glyph;
        }
    }
    if (!hasInfo || metrics.glyphs.empty()) return std::nullopt;
    return metrics;
}

const Glyph* FontMetrics::glyph(uint32_t codepoint) const {
    auto found = glyphs.find(codepoint);
    if (found == glyphs.end()) found = glyphs.find(uint32_t('?'));
    return found == glyphs.end() ? nullptr : &found->second;
}

Vec2 FontMetrics::measure(std::string_view text, float size) const {
    float scale = size / bakedSize;
    float width = 0, lineWidth = 0;
    int lines = 1;
    for (size_t i = 0; i < text.size();) {
        uint32_t codepoint = decodeUTF8(text, i);
        if (codepoint == '\n') {
            width = std::max(width, lineWidth);
            lineWidth = 0;
            lines++;
            continue;
        }
        if (const Glyph* g = glyph(codepoint)) lineWidth += g->advance * scale;
    }
    return Vec2{std::max(width, lineWidth), float(lines) * lineHeight * scale};
}

}  // namespace molten
