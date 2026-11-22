// Manual.cpp
// Help > Molten Manual: the engine's own documentation (the Markdown files of Docs/), read
// inside the editor, beside the work. The documents at the left with the chapters of the one
// that is open; the page at the right, with a box to find words in it.
//
// It draws the Markdown the documents are written in: titles, paragraphs with **strong**
// words, `code` and [links](...), lists, tables and blocks of code. Each block's height is
// worked out once for the width it is shown at, and only the blocks in view are drawn, so a
// document of a thousand lines costs what a screenful does.
#include "Editor.hpp"

#include "Core/Paths.hpp"
#include "Core/TextFile.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"
#include "Theme.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace molten::editor {

namespace {

namespace fs = std::filesystem;

/// A stretch of a paragraph written one way.
struct Run {
    std::string text;
    bool isStrong = false, isCode = false;
    std::string link;                   // not empty: where a click goes
};

struct Block {
    enum class Kind { Heading, Paragraph, Item, Code, Table, Rule } kind = Kind::Paragraph;
    int level = 0;                      // of a heading (1 the document's title); of an item, how far it is indented
    std::vector<Run> runs;              // heading, paragraph, item
    std::string plain;                  // the same without marks, lower case: what Find looks in
    std::vector<std::string> lines;     // code
    std::vector<std::vector<std::string>> rows;     // table: the first is its header
    float height = -1, forWidth = -1;   // worked out for that width
    float top = 0;                      // where it was last drawn, in the page
};

struct Document {
    std::string file, title;            // "MakingGames.md", "Making Games"
    std::vector<Block> blocks;
    bool isRead = false;
};

std::string lowered(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// "Making **games**, with `code`" as runs.
std::vector<Run> runsOf(const std::string& text) {
    std::vector<Run> runs;
    Run current;
    auto flush = [&] {
        if (!current.text.empty()) runs.push_back(current);
        current.text.clear();
    };
    bool isStrong = false;
    for (size_t i = 0; i < text.size(); i++) {
        if (text.compare(i, 2, "**") == 0) {
            flush();
            isStrong = !isStrong;
            current.isStrong = isStrong;
            i++;
        } else if (text[i] == '`') {
            size_t end = text.find('`', i + 1);
            if (end == std::string::npos) {
                current.text += text[i];
                continue;
            }
            flush();
            Run code;
            code.isCode = true;
            code.text = text.substr(i + 1, end - i - 1);
            runs.push_back(code);
            i = end;
        } else if (text[i] == '[') {
            // [words](where)
            size_t close = text.find(']', i + 1);
            if (close != std::string::npos && close + 1 < text.size() && text[close + 1] == '(') {
                size_t end = text.find(')', close + 2);
                if (end != std::string::npos) {
                    flush();
                    Run link;
                    link.isStrong = isStrong;
                    link.text = text.substr(i + 1, close - i - 1);
                    link.link = text.substr(close + 2, end - close - 2);
                    runs.push_back(link);
                    i = end;
                    continue;
                }
            }
            current.text += text[i];
        } else {
            current.text += text[i];
        }
    }
    flush();
    return runs;
}

std::string plainOf(const std::vector<Run>& runs) {
    std::string out;
    for (const Run& run : runs) out += run.text;
    return lowered(out);
}

std::string withoutMarks(const std::string& text) {
    std::string out;
    for (const Run& run : runsOf(text)) out += run.text;
    return out;
}

std::string trimmed(const std::string& text) {
    size_t from = text.find_first_not_of(" \t"), to = text.find_last_not_of(" \t\r");
    return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
}

std::vector<Block> parse(const std::string& text) {
    std::vector<Block> blocks;
    std::istringstream lines(text);
    std::string line, paragraph;
    bool inCode = false;
    Block code;
    auto endParagraph = [&] {
        if (paragraph.empty()) return;
        Block block;
        block.kind = Block::Kind::Paragraph;
        block.runs = runsOf(paragraph);
        block.plain = plainOf(block.runs);
        blocks.push_back(std::move(block));
        paragraph.clear();
    };
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string bare = trimmed(line);
        if (bare.rfind("```", 0) == 0) {
            if (inCode) {
                blocks.push_back(std::move(code));
                code = Block{};
            } else {
                endParagraph();
                code.kind = Block::Kind::Code;
            }
            inCode = !inCode;
            continue;
        }
        if (inCode) {
            code.lines.push_back(line);
            code.plain += lowered(line) + "\n";
            continue;
        }
        if (bare.empty()) {
            endParagraph();
            continue;
        }
        if (bare[0] == '#') {
            endParagraph();
            Block block;
            block.kind = Block::Kind::Heading;
            size_t level = bare.find_first_not_of('#');
            block.level = int(level == std::string::npos ? 1 : level);
            block.runs = runsOf(trimmed(level == std::string::npos ? std::string() : bare.substr(level)));
            block.plain = plainOf(block.runs);
            blocks.push_back(std::move(block));
            continue;
        }
        if (bare == "---" || bare == "***") {
            endParagraph();
            Block block;
            block.kind = Block::Kind::Rule;
            blocks.push_back(std::move(block));
            continue;
        }
        if (bare[0] == '|') {
            endParagraph();
            std::vector<std::string> cells;
            std::string cell;
            for (size_t i = 1; i < bare.size(); i++) {
                if (bare[i] == '|') {
                    cells.push_back(withoutMarks(trimmed(cell)));
                    cell.clear();
                } else {
                    cell += bare[i];
                }
            }
            bool isSeparator = !cells.empty() && cells[0].find_first_not_of("-: ") == std::string::npos;
            if (blocks.empty() || blocks.back().kind != Block::Kind::Table) {
                Block block;
                block.kind = Block::Kind::Table;
                blocks.push_back(std::move(block));
            }
            if (!isSeparator && !cells.empty()) {
                blocks.back().rows.push_back(cells);
                for (const std::string& said : cells) blocks.back().plain += lowered(said) + " ";
            }
            continue;
        }
        bool isItem = bare.size() > 2 && (bare[0] == '-' || bare[0] == '*') && bare[1] == ' ';
        bool isNumbered = false;
        size_t digits = 0;
        while (digits < bare.size() && std::isdigit(static_cast<unsigned char>(bare[digits]))) digits++;
        if (digits > 0 && digits + 1 < bare.size() && bare[digits] == '.' && bare[digits + 1] == ' ') isNumbered = true;
        if (isItem || isNumbered) {
            endParagraph();
            Block block;
            block.kind = Block::Kind::Item;
            block.level = int(line.find_first_not_of(' ')) / 2;
            std::string words = isItem ? bare.substr(2) : bare;
            block.runs = runsOf(words);
            block.plain = plainOf(block.runs);
            blocks.push_back(std::move(block));
            continue;
        }
        // More of an item (its next line, indented) or of the paragraph.
        if (paragraph.empty() && !blocks.empty() && blocks.back().kind == Block::Kind::Item && line[0] == ' ') {
            std::vector<Run> more = runsOf(" " + bare);
            blocks.back().runs.insert(blocks.back().runs.end(), more.begin(), more.end());
            blocks.back().plain = plainOf(blocks.back().runs);
            continue;
        }
        paragraph += (paragraph.empty() ? "" : " ") + bare;
    }
    endParagraph();
    return blocks;
}

/// Lays words out in lines no wider than `width` and, with a draw list, draws them. Returns the height.
/// `clicked` receives the link under a click.
float layout(const std::vector<Run>& runs, float width, float fontSize, ImDrawList* draw, ImVec2 origin, bool isStrongAll, std::string* clicked) {
    ImFont* font = ImGui::GetFont();
    const float lineHeight = fontSize * 1.45f, spaceWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, " ").x;
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text, 0.88f), strong = ImGui::GetColorU32(ImGuiCol_Text);
    unsigned accent = themeAccent();
    const ImU32 linkColor = IM_COL32((accent >> 16) & 0xFF, (accent >> 8) & 0xFF, accent & 0xFF, 255), codeColor = IM_COL32(236, 196, 150, 255);
    float x = 0, y = 0;
    for (const Run& run : runs) {
        const char* at = run.text.c_str();
        const char* end = at + run.text.size();
        while (at < end) {
            // A word, and the spaces after it.
            const char* wordEnd = at;
            while (wordEnd < end && *wordEnd != ' ') wordEnd++;
            const char* next = wordEnd;
            while (next < end && *next == ' ') next++;
            float wordWidth = wordEnd > at ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, at, wordEnd).x : 0.0f;
            if (x > 0 && x + wordWidth > width) {
                x = 0;
                y += lineHeight;
            }
            if (draw && wordEnd > at) {
                ImVec2 position(origin.x + x, origin.y + y + (lineHeight - fontSize) * 0.5f);
                ImVec2 max(position.x + wordWidth, position.y + fontSize);
                bool isLink = !run.link.empty();
                if (run.isCode) draw->AddRectFilled(ImVec2(position.x - 2, position.y - 1), ImVec2(max.x + 2, max.y + 2), IM_COL32(255, 255, 255, 14), 3.0f);
                ImU32 color = isLink ? linkColor : run.isCode ? codeColor : (run.isStrong || isStrongAll) ? strong : ink;
                draw->AddText(font, fontSize, position, color, at, wordEnd);
                // No heavier weight of the font is loaded: strong words are drawn twice, a hair apart.
                if ((run.isStrong || isStrongAll) && !run.isCode) draw->AddText(font, fontSize, ImVec2(position.x + 0.55f, position.y), color, at, wordEnd);
                if (isLink && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(position, ImVec2(max.x + spaceWidth, max.y + 2))) {
                    draw->AddLine(ImVec2(position.x, max.y + 1), ImVec2(max.x, max.y + 1), linkColor, 1.0f);
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (clicked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) *clicked = run.link;
                }
            }
            x += wordWidth + float(next - wordEnd) * spaceWidth;
            at = next;
        }
    }
    return y + lineHeight;
}

class ManualPanel final : public Panel {
public:
    ManualPanel() {
        title = "Manual";
        isOpen = false;
        // The documents, in the order one reads them in.
        const char* const files[][2] = {{"GettingStarted.md", "Getting Started"}, {"MakingGames.md", "Making Games"},     {"VisualScripting.md", "Visual Scripting"},
                                        {"GameCode.md", "Writing Game Code"},     {"iOS.md", "iPhone and iPad"},           {"Plugins.md", "Editor Plugins"},
                                        {"../ARCHITECTURE.md", "How the Engine Works"}};
        for (const auto& file : files) documents_.push_back(Document{file[0], file[1], {}, false});
    }

    ImVec2 firstSize() const override { return ImVec2(980, 680); }
    ImGuiWindowFlags windowFlags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

    /// Shows a document by its file's name without the ending: "MakingGames".
    void show(const std::string& name) {
        for (size_t i = 0; i < documents_.size(); i++) {
            if (fs::path(documents_[i].file).stem().string() == name) open(int(i));
        }
    }

    void draw(EditorContext&) override {
        Document& document = documents_[size_t(current_)];
        read(document);

        // ---- at the left: the documents, and the chapters of the one that is open
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##contents", ImVec2(230, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        for (size_t i = 0; i < documents_.size(); i++) {
            bool isCurrent = int(i) == current_;
            if (ImGui::Selectable(documents_[i].title.c_str(), isCurrent)) open(int(i));
            if (!isCurrent) continue;
            ImGui::Indent(12);
            for (size_t b = 0; b < document.blocks.size(); b++) {
                const Block& block = document.blocks[b];
                if (block.kind != Block::Kind::Heading || block.level != 2) continue;
                std::string chapter;
                for (const Run& run : block.runs) chapter += run.text;
                ImGui::PushID(int(b));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Selectable(chapter.c_str(), false)) scrollTo_ = int(b);
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::Unindent(12);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // ---- at the right: a box to find words, and the page
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(260);
        bool entered = ImGui::InputTextWithHint("##find", "Find in this document", &find_, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Next") || entered) && !find_.empty()) findNext(document);
        if (found_ >= 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%d of %d", foundIndex_, foundCount_);
        } else if (!find_.empty() && searched_ == find_) {
            ImGui::SameLine();
            ImGui::TextDisabled("Not in this document");
        }
        ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (document.blocks.empty()) {
            ImGui::TextDisabled("%s is not where the engine's documents are (%s).", document.file.c_str(), (findRootDirectory() + "/Docs").c_str());
        }
        drawPage(document);
        ImGui::EndChild();
        ImGui::EndGroup();
    }

private:
    void open(int index) {
        if (index == current_) return;
        current_ = index;
        scrollTo_ = -2;                 // the top
        found_ = -1;
        searched_.clear();
    }

    void read(Document& document) {
        if (document.isRead) return;
        document.isRead = true;
        std::optional<std::string> text = text::readFile(findRootDirectory() + "/Docs/" + document.file);
        if (text) document.blocks = parse(*text);
    }

    void findNext(Document& document) {
        std::string wanted = lowered(find_);
        searched_ = find_;
        foundCount_ = 0;
        for (const Block& block : document.blocks) foundCount_ += block.plain.find(wanted) != std::string::npos;
        int count = int(document.blocks.size());
        for (int step = 1; step <= count; step++) {
            int index = (std::max(found_, -1) + step) % count;
            if (document.blocks[size_t(index)].plain.find(wanted) == std::string::npos) continue;
            found_ = index;
            scrollTo_ = index;
            foundIndex_ = 0;
            for (int i = 0; i <= index; i++) foundIndex_ += document.blocks[size_t(i)].plain.find(wanted) != std::string::npos;
            return;
        }
        found_ = -1;
    }

    void drawPage(Document& document) {
        const float fontSize = ImGui::GetFontSize();
        const float width = std::min(ImGui::GetContentRegionAvail().x - 8, 860.0f);        // a line longer than that is hard to read
        if (width < 80) return;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float viewTop = ImGui::GetScrollY(), viewBottom = viewTop + ImGui::GetWindowHeight();
        std::string clicked;
        for (size_t index = 0; index < document.blocks.size(); index++) {
            Block& block = document.blocks[index];
            block.top = ImGui::GetCursorPosY();
            if (scrollTo_ == int(index)) {
                ImGui::SetScrollY(std::max(block.top - 12.0f, 0.0f));
                scrollTo_ = -1;
            }
            if (block.kind == Block::Kind::Table) {
                drawTable(block, int(index), width);
                continue;
            }
            float scale = block.kind == Block::Kind::Heading ? (block.level <= 1 ? 1.7f : block.level == 2 ? 1.35f : 1.12f) : 1.0f;
            float indent = block.kind == Block::Kind::Item ? 18.0f + float(block.level) * 18.0f : 0.0f;
            float before = block.kind == Block::Kind::Heading ? (block.level <= 2 ? 18.0f : 10.0f) : 0.0f;
            float after = block.kind == Block::Kind::Heading ? 6.0f : block.kind == Block::Kind::Item ? 2.0f : 10.0f;
            if (block.forWidth != width) {
                block.forWidth = width;
                if (block.kind == Block::Kind::Code) block.height = float(block.lines.size()) * fontSize * 1.35f + 20.0f;
                else if (block.kind == Block::Kind::Rule) block.height = 9.0f;
                else block.height = layout(block.runs, width - indent, fontSize * scale, nullptr, ImVec2(0, 0), false, nullptr);
            }
            float total = before + block.height + after;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            bool isInView = block.top + total >= viewTop && block.top <= viewBottom;
            if (isInView) {
                if (int(index) == found_) {
                    draw->AddRectFilled(ImVec2(origin.x - 6, origin.y + before - 2), ImVec2(origin.x + width + 6, origin.y + before + block.height + 2), IM_COL32(255, 200, 80, 30), 4.0f);
                }
                if (block.kind == Block::Kind::Code) {
                    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + block.height), ImGui::GetColorU32(ImGuiCol_FrameBg), 6.0f);
                    draw->PushClipRect(origin, ImVec2(origin.x + width, origin.y + block.height), true);
                    for (size_t i = 0; i < block.lines.size(); i++) {
                        draw->AddText(ImVec2(origin.x + 12, origin.y + 10 + float(i) * fontSize * 1.35f), IM_COL32(214, 220, 230, 255), block.lines[i].c_str());
                    }
                    draw->PopClipRect();
                } else if (block.kind == Block::Kind::Rule) {
                    draw->AddLine(ImVec2(origin.x, origin.y + 4), ImVec2(origin.x + width, origin.y + 4), ImGui::GetColorU32(ImGuiCol_Separator));
                } else {
                    if (block.kind == Block::Kind::Item) {
                        draw->AddCircleFilled(ImVec2(origin.x + indent - 9, origin.y + fontSize * 0.72f), 2.2f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    }
                    layout(block.runs, width - indent, fontSize * scale, draw, ImVec2(origin.x + indent, origin.y + before), block.kind == Block::Kind::Heading, &clicked);
                    if (block.kind == Block::Kind::Heading && block.level <= 2) {
                        float under = origin.y + before + block.height + 1;
                        draw->AddLine(ImVec2(origin.x, under), ImVec2(origin.x + width, under), ImGui::GetColorU32(ImGuiCol_Separator));
                    }
                }
            }
            ImGui::Dummy(ImVec2(width, total));
        }
        if (scrollTo_ == -2) {
            ImGui::SetScrollY(0);
            scrollTo_ = -1;
        }
        if (!clicked.empty()) follow(clicked);
    }

    void drawTable(Block& block, int index, float width) {
        if (block.rows.empty()) return;
        int columns = int(block.rows.front().size());
        ImGui::PushID(index);
        if (columns > 0 && ImGui::BeginTable("##table", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width, 0))) {
            for (const std::string& name : block.rows.front()) ImGui::TableSetupColumn(name.c_str());
            ImGui::TableHeadersRow();
            for (size_t row = 1; row < block.rows.size(); row++) {
                ImGui::TableNextRow();
                for (int column = 0; column < columns; column++) {
                    ImGui::TableSetColumnIndex(column);
                    if (size_t(column) < block.rows[row].size()) ImGui::TextWrapped("%s", block.rows[row][size_t(column)].c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(1, 8));
    }

    /// A link: to the web, or to another of these documents.
    void follow(const std::string& link) {
        if (link.rfind("http", 0) == 0) {
            platform::openWebPage(link);
            return;
        }
        std::string file = link.substr(0, link.find('#'));
        std::string name = fs::path(file).stem().string();
        for (size_t i = 0; i < documents_.size(); i++) {
            if (fs::path(documents_[i].file).stem().string() == name) open(int(i));
        }
    }

    std::vector<Document> documents_;
    int current_ = 0;
    int scrollTo_ = -1;                 // a block to bring to the top; -2: the top of the page
    std::string find_, searched_;
    int found_ = -1, foundIndex_ = 0, foundCount_ = 0;
};

ManualPanel* theManual = nullptr;

}  // namespace

void registerManualPanel(EditorContext& editor) {
    auto panel = std::make_unique<ManualPanel>();
    theManual = panel.get();
    editor.registry.addPanel(nullptr, std::move(panel));
}

void showManual(EditorContext& editor, const std::string& page) {
    if (!theManual) return;
    theManual->show(page);
    editor.openPanel("Manual");
}

}  // namespace molten::editor
