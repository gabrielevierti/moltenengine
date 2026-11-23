// TextHighlight.hpp
// A file of text shown with its words in color, as a code editor shows it: for C++ and Metal
// (keywords, types, strings, numbers, comments, the lines for the preprocessor), for the engine's
// own files (each line's first word, its quoted names, its numbers), for JSON and for Markdown.
// Read only; long files are drawn a screenful at a time.
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace molten::editor {

enum class TextKind { Plain, Code, Molten, Json, Markdown };

/// Which kind of text a file's extension says it is.
TextKind textKindOf(const std::string& extension);

/// The pieces of one line, each with its color.
struct TextSpan {
    int from = 0, to = 0;       // bytes of the line
    ImU32 color = 0;
};

/// Colors every line of `text`. Block comments carry from line to line.
std::vector<std::vector<TextSpan>> highlightText(const std::string& text, TextKind kind, std::vector<std::string>& lines);

/// Draws text in color, filling the room left (or `height` when above 0), with line numbers.
/// The work is kept between frames for as long as the text is the same.
class HighlightedText {
public:
    void draw(const std::string& text, TextKind kind, float height = 0);

private:
    size_t shownSize_ = size_t(-1);
    size_t shownHash_ = 0;
    TextKind shownKind_ = TextKind::Plain;
    std::vector<std::string> lines_;
    std::vector<std::vector<TextSpan>> spans_;
};

}  // namespace molten::editor
