#include "Panels/TextHighlight.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <unordered_set>

namespace molten::editor {

namespace {

// The colors, on the editor's dark background.
const ImU32 kText = IM_COL32(214, 218, 226, 255);
const ImU32 kComment = IM_COL32(118, 128, 142, 255);
const ImU32 kKeyword = IM_COL32(198, 146, 234, 255);
const ImU32 kType = IM_COL32(102, 196, 222, 255);
const ImU32 kString = IM_COL32(152, 206, 128, 255);
const ImU32 kNumber = IM_COL32(236, 176, 104, 255);
const ImU32 kPreprocessor = IM_COL32(232, 124, 124, 255);
const ImU32 kHeading = IM_COL32(250, 196, 100, 255);
const ImU32 kLineNumber = IM_COL32(92, 100, 112, 255);

const std::unordered_set<std::string>& keywords() {
    static const std::unordered_set<std::string> words = {
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto", "class", "struct", "enum",
        "union", "namespace", "using", "typedef", "template", "typename", "public", "private", "protected", "virtual", "override", "final",
        "static", "const", "constexpr", "consteval", "inline", "extern", "mutable", "volatile", "explicit", "friend", "operator", "new",
        "delete", "this", "true", "false", "nullptr", "sizeof", "alignof", "static_cast", "dynamic_cast", "reinterpret_cast",
        "const_cast", "try", "catch", "throw", "noexcept", "auto", "decltype", "static_assert", "co_await", "co_return", "co_yield",
        // Metal
        "kernel", "vertex", "fragment", "device", "constant", "thread", "threadgroup", "stage_in", "discard_fragment"};
    return words;
}

const std::unordered_set<std::string>& types() {
    static const std::unordered_set<std::string> words = {
        "void", "bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed", "size_t", "uint", "uint8_t", "uint16_t",
        "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "half", "float2", "float3", "float4", "float2x2", "float3x3",
        "float4x4", "half2", "half3", "half4", "int2", "int3", "int4", "uint2", "uint3", "uint4", "bool2", "bool3", "bool4", "texture2d",
        "texturecube", "sampler", "std", "string", "vector", "Vec2", "Vec3", "Vec4", "Quat", "Mat4", "World", "Entity", "Scene",
        "Transform", "Surface"};
    return words;
}

bool isWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

void add(std::vector<TextSpan>& spans, int from, int to, ImU32 color) {
    if (to <= from) return;
    if (!spans.empty() && spans.back().color == color && spans.back().to == from) {
        spans.back().to = to;
        return;
    }
    spans.push_back(TextSpan{from, to, color});
}

/// A number from `i`: digits, a point, an exponent, a suffix (1.5f, 0x2A, 3e-4).
int numberEnd(const std::string& line, int i) {
    int n = int(line.size());
    if (line[size_t(i)] == '0' && i + 1 < n && (line[size_t(i + 1)] == 'x' || line[size_t(i + 1)] == 'X')) {
        i += 2;
        while (i < n && std::isxdigit(static_cast<unsigned char>(line[size_t(i)]))) i++;
        return i;
    }
    while (i < n && (std::isdigit(static_cast<unsigned char>(line[size_t(i)])) || line[size_t(i)] == '.' || line[size_t(i)] == '\'')) i++;
    if (i < n && (line[size_t(i)] == 'e' || line[size_t(i)] == 'E')) {
        i++;
        if (i < n && (line[size_t(i)] == '-' || line[size_t(i)] == '+')) i++;
        while (i < n && std::isdigit(static_cast<unsigned char>(line[size_t(i)]))) i++;
    }
    while (i < n && std::isalpha(static_cast<unsigned char>(line[size_t(i)]))) i++;       // f, u, ull
    return i;
}

int stringEnd(const std::string& line, int i) {
    char quote = line[size_t(i)];
    int n = int(line.size());
    for (i++; i < n; i++) {
        if (line[size_t(i)] == '\\') i++;
        else if (line[size_t(i)] == quote) return i + 1;
    }
    return n;
}

void codeLine(const std::string& line, std::vector<TextSpan>& spans, bool& inComment) {
    int n = int(line.size());
    int i = 0;
    if (!inComment) {
        int first = 0;
        while (first < n && (line[size_t(first)] == ' ' || line[size_t(first)] == '\t')) first++;
        if (first < n && line[size_t(first)] == '#') {
            // The preprocessor's line, and the file it includes.
            int end = first + 1;
            while (end < n && isWordChar(line[size_t(end)])) end++;
            add(spans, 0, end, kPreprocessor);
            i = end;
        }
    }
    while (i < n) {
        if (inComment) {
            size_t close = line.find("*/", size_t(i));
            int end = close == std::string::npos ? n : int(close) + 2;
            add(spans, i, end, kComment);
            if (close != std::string::npos) inComment = false;
            i = end;
            continue;
        }
        char c = line[size_t(i)];
        if (c == '/' && i + 1 < n && line[size_t(i + 1)] == '/') {
            add(spans, i, n, kComment);
            break;
        }
        if (c == '/' && i + 1 < n && line[size_t(i + 1)] == '*') {
            inComment = true;
            add(spans, i, i + 2, kComment);
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            int end = stringEnd(line, i);
            add(spans, i, end, kString);
            i = end;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            int end = numberEnd(line, i);
            add(spans, i, end, kNumber);
            i = end;
        } else if (isWordStart(c)) {
            int end = i;
            while (end < n && isWordChar(line[size_t(end)])) end++;
            std::string word = line.substr(size_t(i), size_t(end - i));
            ImU32 color = keywords().count(word) ? kKeyword : types().count(word) ? kType : (word.rfind("MOLTEN_", 0) == 0 ? kPreprocessor : kText);
            add(spans, i, end, color);
            i = end;
        } else if (c == '[' && i + 1 < n && line[size_t(i + 1)] == '[') {
            // Metal's attributes, [[buffer(0)]]
            size_t close = line.find("]]", size_t(i));
            int end = close == std::string::npos ? n : int(close) + 2;
            add(spans, i, end, kType);
            i = end;
        } else {
            add(spans, i, i + 1, kText);
            i++;
        }
    }
}

/// The engine's own files: "keyword values...", with # comments, "names" and numbers.
void moltenLine(const std::string& line, std::vector<TextSpan>& spans) {
    int n = int(line.size());
    int i = 0;
    while (i < n && (line[size_t(i)] == ' ' || line[size_t(i)] == '\t')) i++;
    if (i < n && line[size_t(i)] == '#') {
        add(spans, i, n, kComment);
        return;
    }
    int end = i;
    while (end < n && line[size_t(end)] != ' ' && line[size_t(end)] != '\t') end++;
    add(spans, i, end, line.rfind("molten-", size_t(i)) == size_t(i) ? kPreprocessor : kKeyword);
    i = end;
    while (i < n) {
        char c = line[size_t(i)];
        if (c == '"') {
            int stop = stringEnd(line, i);
            add(spans, i, stop, kString);
            i = stop;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || ((c == '-' || c == '.') && i + 1 < n && std::isdigit(static_cast<unsigned char>(line[size_t(i + 1)])))) {
            int stop = numberEnd(line, i + (c == '-' ? 1 : 0));
            add(spans, i, stop, kNumber);
            i = stop;
        } else if (isWordStart(c)) {
            int stop = i;
            while (stop < n && isWordChar(line[size_t(stop)])) stop++;
            add(spans, i, stop, kText);
            i = stop;
        } else {
            add(spans, i, i + 1, kText);
            i++;
        }
    }
}

void jsonLine(const std::string& line, std::vector<TextSpan>& spans) {
    int n = int(line.size());
    for (int i = 0; i < n;) {
        char c = line[size_t(i)];
        if (c == '"') {
            int end = stringEnd(line, i);
            int after = end;
            while (after < n && line[size_t(after)] == ' ') after++;
            add(spans, i, end, after < n && line[size_t(after)] == ':' ? kType : kString);       // a key, or a value
            i = end;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            int end = numberEnd(line, c == '-' ? i + 1 : i);
            add(spans, i, std::max(end, i + 1), kNumber);
            i = std::max(end, i + 1);
        } else if (isWordStart(c)) {
            int end = i;
            while (end < n && isWordChar(line[size_t(end)])) end++;
            add(spans, i, end, kKeyword);       // true, false, null
            i = end;
        } else {
            add(spans, i, i + 1, kText);
            i++;
        }
    }
}

void markdownLine(const std::string& line, std::vector<TextSpan>& spans, bool& inCode) {
    int n = int(line.size());
    if (line.rfind("```", 0) == 0) {
        inCode = !inCode;
        add(spans, 0, n, kComment);
        return;
    }
    if (inCode) {
        add(spans, 0, n, kString);
        return;
    }
    if (n > 0 && line[0] == '#') {
        add(spans, 0, n, kHeading);
        return;
    }
    for (int i = 0; i < n;) {
        if (line[size_t(i)] == '`') {
            size_t close = line.find('`', size_t(i + 1));
            int end = close == std::string::npos ? n : int(close) + 1;
            add(spans, i, end, kString);
            i = end;
        } else if (line.compare(size_t(i), 2, "**") == 0) {
            size_t close = line.find("**", size_t(i + 2));
            int end = close == std::string::npos ? n : int(close) + 2;
            add(spans, i, end, kKeyword);
            i = end;
        } else {
            add(spans, i, i + 1, kText);
            i++;
        }
    }
}

}  // namespace

TextKind textKindOf(const std::string& extension) {
    std::string e = extension;
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (e == ".cpp" || e == ".hpp" || e == ".h" || e == ".mm" || e == ".c" || e == ".metal" || e == ".shader") return TextKind::Code;
    if (e == ".json") return TextKind::Json;
    if (e == ".md") return TextKind::Markdown;
    for (const char* own : {".scene", ".prefab", ".material", ".meta", ".animator", ".timeline", ".graph", ".shadergraph", ".molten",
                            ".settings", ".actions", ".lighting", ".effect", ".anim"}) {
        if (e == own) return TextKind::Molten;
    }
    return TextKind::Plain;
}

std::vector<std::vector<TextSpan>> highlightText(const std::string& text, TextKind kind, std::vector<std::string>& lines) {
    lines.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Tabs as four spaces: the columns then line up as they do in an editor.
        std::string expanded;
        for (char c : line) {
            if (c == '\t') expanded += "    ";
            else expanded += c;
        }
        lines.push_back(std::move(expanded));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::vector<std::vector<TextSpan>> spans(lines.size());
    bool inComment = false, inCode = false;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& line = lines[i];
        switch (kind) {
            case TextKind::Code: codeLine(line, spans[i], inComment); break;
            case TextKind::Molten: moltenLine(line, spans[i]); break;
            case TextKind::Json: jsonLine(line, spans[i]); break;
            case TextKind::Markdown: markdownLine(line, spans[i], inCode); break;
            case TextKind::Plain: add(spans[i], 0, int(line.size()), kText); break;
        }
    }
    return spans;
}

void HighlightedText::draw(const std::string& text, TextKind kind, float height) {
    size_t hash = std::hash<std::string>{}(text);
    if (text.size() != shownSize_ || hash != shownHash_ || kind != shownKind_) {
        spans_ = highlightText(text, kind, lines_);
        shownSize_ = text.size();
        shownHash_ = hash;
        shownKind_ = kind;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(22, 24, 29, 255));
    ImGui::BeginChild("##highlighted", ImVec2(-FLT_MIN, height > 0 ? height : -FLT_MIN), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    char number[16];
    std::snprintf(number, sizeof number, "%zu", lines_.size());
    const float gutter = ImGui::CalcTextSize(number).x + 14.0f;
    // As wide as the longest line, so that it scrolls sideways.
    float widest = 0;
    for (const std::string& line : lines_) widest = std::max(widest, float(line.size()));
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(gutter + widest * ImGui::CalcTextSize("M").x + 20, lineHeight * float(lines_.size())));
    // Only the lines in sight are drawn.
    int first = std::max(0, int(ImGui::GetScrollY() / lineHeight) - 1);
    int last = std::min(int(lines_.size()), first + int(ImGui::GetWindowHeight() / lineHeight) + 3);
    {
        for (int row = first; row < last; row++) {
            float y = origin.y + float(row) * lineHeight;
            std::snprintf(number, sizeof number, "%d", row + 1);
            float numberWidth = ImGui::CalcTextSize(number).x;
            drawList->AddText(ImVec2(origin.x + gutter - 10 - numberWidth, y), kLineNumber, number);
            const std::string& line = lines_[size_t(row)];
            float x = origin.x + gutter;
            for (const TextSpan& span : spans_[size_t(row)]) {
                const char* begin = line.c_str() + span.from;
                const char* end = line.c_str() + span.to;
                drawList->AddText(font, size, ImVec2(x, y), span.color, begin, end);
                x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace molten::editor
