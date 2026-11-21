// CommandPalette.cpp
#include "CommandPalette.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>

namespace molten::editor {

void CommandPalette::open() {
    isOpen_ = true;
    justOpened_ = true;
    query_[0] = 0;
    chosen_ = 0;
    lastQuery_.clear();
}

void CommandPalette::draw(const std::vector<PaletteCommand>& commands) {
    if (!isOpen_) return;
    const ImGuiViewport* view = ImGui::GetMainViewport();
    float width = std::min(640.0f, view->WorkSize.x - 40);
    ImGui::SetNextWindowPos(ImVec2(view->WorkPos.x + (view->WorkSize.x - width) * 0.5f, view->WorkPos.y + view->WorkSize.y * 0.12f));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    if (justOpened_) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8);
    bool visible = ImGui::Begin("##Command Palette", nullptr,
                                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                    ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(2);
    if (!visible) {
        ImGui::End();
        return;
    }
    if (justOpened_) ImGui::SetKeyboardFocusHere();
    justOpened_ = false;
    ImGui::SetNextItemWidth(-1);
    bool entered = ImGui::InputTextWithHint("##query", "Search actions, windows, entities, assets...", query_, sizeof(query_),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    if (lastQuery_ != query_) {
        lastQuery_ = query_;
        chosen_ = 0;
    }
    std::vector<size_t> shown = rankPalette(query_, commands, 14);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) chosen_++;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) chosen_--;
    chosen_ = shown.empty() ? 0 : std::clamp(chosen_, 0, int(shown.size()) - 1);

    const PaletteCommand* run = nullptr;
    ImGui::Spacing();
    if (shown.empty()) ImGui::TextDisabled("Nothing has that name.");
    for (size_t row = 0; row < shown.size(); row++) {
        const PaletteCommand& command = commands[shown[row]];
        ImGui::PushID(int(row));
        if (!command.isEnabled) ImGui::BeginDisabled();
        if (ImGui::Selectable(command.title.c_str(), int(row) == chosen_)) run = &command;
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x + ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0) {
            chosen_ = int(row);
        }
        std::string right = command.shortcut.empty() ? command.kind : command.shortcut + "   " + command.kind;
        float rightWidth = ImGui::CalcTextSize(right.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - rightWidth);
        ImGui::TextDisabled("%s", right.c_str());
        if (!command.isEnabled) ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (entered && !shown.empty()) run = &commands[shown[size_t(chosen_)]];
    bool leaves = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                  (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsWindowAppearing());
    ImGui::End();
    if (run && run->isEnabled) {
        isOpen_ = false;
        if (run->run) run->run();
    } else if (leaves) {
        isOpen_ = false;
    }
}

} // namespace molten::editor
