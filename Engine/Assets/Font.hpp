// Font.hpp
// A font as a game sees it: one texture with every glyph in it (an atlas) and a table
// that says where each glyph is and how to place it.
//
// The atlas stores a signed distance field: each texel holds the distance to the
// glyph's outline instead of plain coverage. The fragment shader turns that back into
// a sharp edge at any size, so one small texture serves both tiny labels and huge
// titles. Tools/bake_font.py produces the atlas and the metrics file.
#pragma once

#include "Math/Math.hpp"

#include <optional>
#include <string_view>
#include <unordered_map>

namespace molten {

/// One glyph. All values are in atlas texels, at the size the font was baked at.
struct Glyph {
    float x = 0, y = 0, width = 0, height = 0;      // rectangle in the atlas
    float offsetX = 0, offsetY = 0;                 // from the pen position (top of the line) to the rectangle's top left
    float advance = 0;                              // how far the pen moves after this glyph
};

struct FontMetrics {
    float bakedSize = 48;           // the font size, in texels, the atlas was rendered at
    float lineHeight = 58;
    float distanceRange = 8;        // how many texels the distance field extends around an outline
    float atlasWidth = 512, atlasHeight = 512;
    std::unordered_map<uint32_t, Glyph> glyphs;

    /// Parses the text format written by Tools/bake_font.py.
    static std::optional<FontMetrics> parse(std::string_view text);

    /// The glyph for a Unicode code point, falling back to '?' and then to nothing.
    const Glyph* glyph(uint32_t codepoint) const;

    /// Width and height of `text` when drawn at `size`. Handles newlines.
    Vec2 measure(std::string_view text, float size) const;
};

/// Reads one Unicode code point from UTF-8 text and advances `index` past it.
uint32_t decodeUTF8(std::string_view text, size_t& index);

}  // namespace molten
