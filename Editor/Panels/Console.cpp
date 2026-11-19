// The console: what the engine, the editor and the plugins have said.
#include "Editor.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>

namespace molten::editor {

namespace {

class ConsolePanel final : public Panel {
public:
    ConsolePanel() { title = "Console"; }

    void draw(EditorContext& editor) override {
        if (ImGui::Button("Clear")) editor.console.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Info", &showInfo_);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &showWarnings_);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &showErrors_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Filter", &filter_);
        ImGui::Separator();

        ImGui::BeginChild("##messages", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        static const ImVec4 colors[] = {ImVec4(0.78f, 0.81f, 0.86f, 1), ImVec4(1.0f, 0.78f, 0.3f, 1), ImVec4(1.0f, 0.42f, 0.42f, 1)};
        for (const ConsoleMessage& message : editor.console) {
            bool isShown = message.level == ConsoleMessage::Level::Info ? showInfo_
                         : message.level == ConsoleMessage::Level::Warning ? showWarnings_ : showErrors_;
            if (!isShown) continue;
            if (!filter_.empty() && message.text.find(filter_) == std::string::npos) continue;
            int minutes = int(message.time) / 60, seconds = int(message.time) % 60;
            ImGui::TextDisabled("%02d:%02d", minutes, seconds);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, colors[int(message.level)]);
            ImGui::TextUnformatted(message.text.c_str());
            ImGui::PopStyleColor();
            if (message.count > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d times)", message.count);
            }
        }
        // Stay at the bottom while new messages arrive, unless the user scrolled up to read.
        if (editor.console.size() != lastCount_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40) ImGui::SetScrollHereY(1.0f);
        lastCount_ = editor.console.size();
        ImGui::EndChild();
    }

private:
    bool showInfo_ = true, showWarnings_ = true, showErrors_ = true;
    std::string filter_;
    size_t lastCount_ = 0;
};

}  // namespace

void registerConsolePanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<ConsolePanel>());
}

}  // namespace molten::editor
