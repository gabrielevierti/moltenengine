// The console: what the engine, the editor, the plugins and the game have said. As in Unity:
// a row per message with its kind, counts of each kind that also turn them on and off,
// identical messages folded into one with a number, and the whole of the chosen one below.
#include "Editor.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace molten::editor {

namespace {

using Level = ConsoleMessage::Level;

ImU32 colorOf(Level level) {
    switch (level) {
        case Level::Error: return IM_COL32(255, 104, 104, 255);
        case Level::Warning: return IM_COL32(255, 198, 76, 255);
        case Level::Debug: return IM_COL32(140, 196, 255, 255);
        case Level::Info: break;
    }
    return IM_COL32(200, 206, 216, 255);
}

/// The mark at the start of a row: a stop sign, a warning triangle, a bubble, a bug's dot.
void drawMark(ImDrawList* drawList, ImVec2 c, Level level, float r) {
    ImU32 color = colorOf(level);
    switch (level) {
        case Level::Error:
            drawList->AddCircleFilled(c, r, color, 16);
            drawList->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 0.4f), ImVec2(c.x + r * 0.4f, c.y + r * 0.4f), IM_COL32(40, 20, 20, 255), 1.6f);
            drawList->AddLine(ImVec2(c.x + r * 0.4f, c.y - r * 0.4f), ImVec2(c.x - r * 0.4f, c.y + r * 0.4f), IM_COL32(40, 20, 20, 255), 1.6f);
            break;
        case Level::Warning:
            drawList->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 1.05f, c.y + r * 0.8f), ImVec2(c.x - r * 1.05f, c.y + r * 0.8f), color);
            drawList->AddLine(ImVec2(c.x, c.y - r * 0.35f), ImVec2(c.x, c.y + r * 0.25f), IM_COL32(50, 36, 10, 255), 1.6f);
            break;
        case Level::Debug:
            drawList->AddCircle(c, r * 0.85f, color, 16, 1.6f);
            drawList->AddCircleFilled(c, r * 0.35f, color, 12);
            break;
        case Level::Info:
            drawList->AddCircle(c, r * 0.9f, color, 16, 1.4f);
            drawList->AddLine(ImVec2(c.x, c.y - r * 0.1f), ImVec2(c.x, c.y + r * 0.5f), color, 1.6f);
            drawList->AddCircleFilled(ImVec2(c.x, c.y - r * 0.45f), 1.3f, color, 6);
            break;
    }
}

class ConsolePanel final : public Panel {
public:
    ConsolePanel() { title = "Console"; }

    void draw(EditorContext& editor) override {
        bool isPlaying = editor.scene.isPlaying();
        if (isPlaying && !wasPlaying_ && clearsOnPlay_) {
            editor.console.clear();
            selected_ = -1;
        }
        wasPlaying_ = isPlaying;

        int counts[4] = {0, 0, 0, 0};
        for (const ConsoleMessage& message : editor.console) counts[int(message.level)] += message.count;

        // ---- the bar: what to do with the list on the left, what to show of it on the right
        if (ImGui::Button("Clear")) {
            editor.console.clear();
            selected_ = -1;
        }
        ImGui::SameLine();
        toggle("Collapse", collapses_, "The same message, however many times it came, on one row with a number");
        ImGui::SameLine();
        toggle("Clear on Play", clearsOnPlay_, "Start each run with an empty console");
        ImGui::SameLine();
        float right = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        float kinds = 4 * 64 + 12;
        ImGui::SetNextItemWidth(std::max(80.0f, right - ImGui::GetCursorPosX() - kinds - 8));
        ImGui::InputTextWithHint("##filter", "Search", &filter_);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - kinds));
        kindToggle(Level::Error, counts, "Errors");
        ImGui::SameLine(0, 4);
        kindToggle(Level::Warning, counts, "Warnings");
        ImGui::SameLine(0, 4);
        kindToggle(Level::Info, counts, "Messages");
        ImGui::SameLine(0, 4);
        kindToggle(Level::Debug, counts, "Debug: what code says with logDebug");

        // ---- the rows
        struct Row {
            int index;          // into the console
            int count;
        };
        std::vector<Row> rows;
        std::map<std::pair<int, std::string>, size_t> seen;     // collapsed: where each message's row is
        for (size_t i = 0; i < editor.console.size(); i++) {
            const ConsoleMessage& message = editor.console[i];
            if (!shows_[int(message.level)]) continue;
            if (!filter_.empty() && !containsIgnoringCase(message.text, filter_)) continue;
            if (collapses_) {
                auto key = std::make_pair(int(message.level), message.text);
                auto found = seen.find(key);
                if (found != seen.end()) {
                    rows[found->second].count += message.count;
                    continue;
                }
                seen[key] = rows.size();
            }
            rows.push_back({int(i), message.count});
        }

        float detailHeight = selected_ >= 0 && selected_ < int(editor.console.size()) ? std::clamp(detailHeight_, 50.0f, ImGui::GetContentRegionAvail().y - 60) : 0;
        ImGui::BeginChild("##messages", ImVec2(0, -detailHeight - (detailHeight > 0 ? 6 : 0)), ImGuiChildFlags_Borders);
        const float rowHeight = std::max(ImGui::GetTextLineHeight() + 10, 26.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiListClipper clipper;
        clipper.Begin(int(rows.size()), rowHeight);
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++) {
                const ConsoleMessage& message = editor.console[size_t(rows[size_t(r)].index)];
                ImVec2 min = ImGui::GetCursorScreenPos();
                float width = ImGui::GetContentRegionAvail().x;
                ImGui::PushID(r);
                bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, rowHeight));
                bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                if (clicked) selected_ = rows[size_t(r)].index;
                bool isSelected = selected_ == rows[size_t(r)].index;
                ImVec2 max(min.x + width, min.y + rowHeight);
                ImU32 background = isSelected ? IM_COL32(64, 82, 112, 255) : hovered ? IM_COL32(44, 48, 56, 255)
                                 : (r % 2 ? IM_COL32(30, 32, 37, 255) : IM_COL32(34, 36, 42, 255));
                drawList->AddRectFilled(min, max, background);
                float middle = min.y + rowHeight * 0.5f;
                drawMark(drawList, ImVec2(min.x + 14, middle), message.level, 6.5f);
                char time[16];
                std::snprintf(time, sizeof(time), "%02d:%02d", int(message.time) / 60, int(message.time) % 60);
                drawList->AddText(ImVec2(min.x + 28, middle - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(120, 126, 138, 255), time);
                // The first line only: the rest is below when chosen.
                std::string line = message.text.substr(0, message.text.find('\n'));
                drawList->PushClipRect(ImVec2(min.x + 72, min.y), ImVec2(max.x - 50, max.y), true);
                drawList->AddText(ImVec2(min.x + 72, middle - ImGui::GetTextLineHeight() * 0.5f),
                                  message.level == Level::Info ? IM_COL32(222, 226, 232, 255) : colorOf(message.level), line.c_str());
                drawList->PopClipRect();
                int count = rows[size_t(r)].count;
                if (count > 1) {
                    std::string number = count > 999 ? "999+" : std::to_string(count);
                    ImVec2 size = ImGui::CalcTextSize(number.c_str());
                    ImVec2 pillMin(max.x - size.x - 22, middle - 9), pillMax(max.x - 8, middle + 9);
                    drawList->AddRectFilled(pillMin, pillMax, IM_COL32(70, 76, 90, 255), 9);
                    drawList->AddText(ImVec2(pillMin.x + 7, middle - size.y * 0.5f), IM_COL32(230, 232, 238, 255), number.c_str());
                }
            }
        }
        // Stay at the bottom while new messages arrive, unless the user scrolled up to read.
        if (editor.console.size() != lastCount_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - rowHeight * 2) ImGui::SetScrollHereY(1.0f);
        lastCount_ = editor.console.size();
        if (rows.empty()) ImGui::TextDisabled(editor.console.empty() ? "Nothing yet." : "Nothing that matches.");
        ImGui::EndChild();

        // ---- the chosen message, whole
        if (detailHeight > 0) {
            // A bar to drag between the two.
            ImGui::InvisibleButton("##split", ImVec2(-FLT_MIN, 6));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActive()) detailHeight_ -= ImGui::GetIO().MouseDelta.y;
            const ConsoleMessage& message = editor.console[size_t(selected_)];
            ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(message.level == Level::Info ? IM_COL32(222, 226, 232, 255) : colorOf(message.level)));
            ImGui::TextWrapped("%s", message.text.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextDisabled("%s at %02d:%02d%s", levelName(message.level), int(message.time) / 60, int(message.time) % 60,
                                message.count > 1 ? (", " + std::to_string(message.count) + " times in a row").c_str() : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(message.text.c_str());
            ImGui::EndChild();
        }
    }

private:
    static const char* levelName(Level level) {
        switch (level) {
            case Level::Error: return "Error";
            case Level::Warning: return "Warning";
            case Level::Debug: return "Debug";
            case Level::Info: break;
        }
        return "Message";
    }

    static bool containsIgnoringCase(const std::string& text, const std::string& part) {
        auto found = std::search(text.begin(), text.end(), part.begin(), part.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
        return found != text.end();
    }

    void toggle(const char* label, bool& value, const char* tip) {
        ImGui::PushStyleColor(ImGuiCol_Button, value ? ImVec4(0.30f, 0.36f, 0.46f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(label)) value = !value;
        ImGui::PopStyleColor();
        ui::tooltip(tip);
    }

    /// A kind of message: its mark and how many there are; clicked, it is shown or hidden.
    void kindToggle(Level level, const int* counts, const char* tip) {
        ImGui::PushID(int(level));
        ImVec2 min = ImGui::GetCursorScreenPos();
        float height = ImGui::GetFrameHeight();
        if (ImGui::InvisibleButton("##kind", ImVec2(60, height))) shows_[int(level)] = !shows_[int(level)];
        ui::tooltip(tip);
        bool on = shows_[int(level)];
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, ImVec2(min.x + 60, min.y + height), on ? IM_COL32(52, 58, 70, 255) : IM_COL32(32, 34, 40, 255), 5);
        drawMark(drawList, ImVec2(min.x + 14, min.y + height * 0.5f), level, 6);
        int count = counts[int(level)];
        std::string number = count > 999 ? "999+" : std::to_string(count);
        drawList->AddText(ImVec2(min.x + 26, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f), on ? IM_COL32(230, 232, 238, 255) : IM_COL32(110, 116, 128, 255),
                          number.c_str());
        ImGui::PopID();
    }

    bool shows_[4] = {true, true, true, true};
    bool collapses_ = false, clearsOnPlay_ = true, wasPlaying_ = false;
    std::string filter_;
    size_t lastCount_ = 0;
    int selected_ = -1;
    float detailHeight_ = 90;
};

}  // namespace

void registerConsolePanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<ConsolePanel>());
}

}  // namespace molten::editor
