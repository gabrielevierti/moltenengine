// EditorChrome.cpp
// What frames the editor's windows: the bar under the menus (what the scene is doing at the
// left; Run, Hold and Frame in the middle; the layouts at the right) and the status bar along
// the bottom (the last thing the console heard, and the numbers).
#include "EditorApp.hpp"

#include "Panels/Widgets.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace molten::editor {

namespace {

/// The color the console gives each kind of message.
ImU32 levelColor(ConsoleMessage::Level level) {
    switch (level) {
        case ConsoleMessage::Level::Error: return IM_COL32(229, 72, 77, 255);
        case ConsoleMessage::Level::Warning: return IM_COL32(245, 181, 61, 255);
        case ConsoleMessage::Level::Debug: return IM_COL32(110, 168, 254, 255);
        default: return IM_COL32(139, 147, 161, 255);
    }
}

}  // namespace

void EditorApp::drawPlayBar() {
    EditorContext& editor = *context_;
    SceneDocument& scene = editor.scene;
    const float height = 38;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    bool isOpen = ImGui::BeginViewportSideBar("##playbar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (isOpen) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool isPlaying = scene.isPlaying(), isPaused = scene.isPaused();
        // Molten's own controls: Run, Hold and Frame as one control in the middle of the bar (where
        // the eye looks for them), what the scene is doing at the left, the layouts at the right.
        const float keyHeight = 28;
        struct Key {
            const char* id;
            const char* label;
            const char* glyph;
            bool isLit, isEnabled;
            ImU32 litColor;
            std::string tooltip;
            const char* command;
        };
        const Key keys[] = {
            {"##play", isPlaying ? "Stop" : "Run", isPlaying ? icon::Square : icon::Play, isPlaying, true, IM_COL32(255, 122, 47, 255),
             isPlaying ? "Stop: back to editing, the scene returns to how it was (" + editor.keysOf("play") + ")" : "Run the scene (" + editor.keysOf("play") + ")", "play"},
            {"##pause", isPaused ? "Resume" : "Hold", isPaused ? icon::Play : icon::Pause, isPaused, isPlaying, IM_COL32(110, 180, 255, 255),
             isPaused ? "Resume (" + editor.keysOf("pause") + ")" : "Hold: freeze the game where it is; change things, then resume (" + editor.keysOf("pause") + ")",
             "pause"},
            {"##step", "Frame", icon::StepForward, false, isPlaying, 0, "Advance one frame and hold (" + editor.keysOf("step") + ")", "step"},
        };
        auto keyWidth = [](const Key& key) { return 14 + 16 + 7 + ImGui::CalcTextSize(key.label).x + 14; };
        float groupWidth = 0;
        for (const Key& key : keys) groupWidth += keyWidth(key);
        const float barTop = ImGui::GetWindowPos().y, keysTop = barTop + (height - keyHeight) * 0.5f;
        float keyX = std::round(ImGui::GetWindowPos().x + (ImGui::GetWindowWidth() - groupWidth) * 0.5f);
        // The group's ground, then each key on it; a hairline between two keys.
        drawList->AddRectFilled(ImVec2(keyX, keysTop), ImVec2(keyX + groupWidth, keysTop + keyHeight), IM_COL32(36, 40, 48, 255), keyHeight * 0.5f);
        drawList->AddRect(ImVec2(keyX, keysTop), ImVec2(keyX + groupWidth, keysTop + keyHeight), IM_COL32(255, 255, 255, 18), keyHeight * 0.5f);
        ImFont* icons = ImGuiLayer::iconFont();
        for (size_t i = 0; i < std::size(keys); i++) {
            const Key& key = keys[i];
            const float width = keyWidth(key);
            ImGui::SetCursorScreenPos(ImVec2(keyX, keysTop));
            ImGui::BeginDisabled(!key.isEnabled);
            bool pressed = ImGui::InvisibleButton(key.id, ImVec2(width, keyHeight));
            bool isHovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", key.tooltip.c_str());
            const ImVec2 min(keyX, keysTop), max(keyX + width, keysTop + keyHeight);
            const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft : i + 1 == std::size(keys) ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
            if (key.isLit) {
                drawList->AddRectFilled(min, max, (key.litColor & 0x00FFFFFFu) | IM_COL32(0, 0, 0, 52), keyHeight * 0.5f, corners);
            } else if (isHovered && key.isEnabled) {
                drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 16), keyHeight * 0.5f, corners);
            }
            if (i > 0) drawList->AddLine(ImVec2(min.x, min.y + 7), ImVec2(min.x, max.y - 7), IM_COL32(255, 255, 255, 22));
            const ImU32 ink = !key.isEnabled ? IM_COL32(96, 102, 114, 255) : key.isLit ? key.litColor : IM_COL32(223, 227, 234, 255);
            const float middle = (min.y + max.y) * 0.5f;
            if (icons) {
                ImVec2 extent = icons->CalcTextSizeA(16.0f, FLT_MAX, 0.0f, key.glyph);
                drawList->AddText(icons, 16.0f, ImVec2(min.x + 14 + std::round((16 - extent.x) * 0.5f), std::round(middle - extent.y * 0.5f)), ink, key.glyph);
            }
            drawList->AddText(ImVec2(min.x + 14 + 16 + 7, std::round(middle - ImGui::GetTextLineHeight() * 0.5f)), ink, key.label);
            if (pressed) editor.commands.run(key.command);
            keyX += width;
        }

        // (Whether the scene runs is said by the Run button itself, lit while it does.)
        const float layoutWidth = 170;

        // The layout of the windows, at the right end.
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - layoutWidth - 10, barTop + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(layoutWidth);
        if (ImGui::BeginCombo("##layout", (std::string(icon::LayoutDashboard) + "  " + layoutName_).c_str(), ImGuiComboFlags_HeightLarge)) {
            drawLayoutItems();
            ImGui::EndCombo();
        }
        ui::tooltip("The layout of the windows: choose one, save this one, or go back to the default");
    }
    ImGui::End();
}

void EditorApp::drawStatusBar() {
    EditorContext& editor = *context_;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    const float height = ImGui::GetFrameHeight() + 2;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    bool isOpen = ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (isOpen) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float width = ImGui::GetWindowWidth(), middle = origin.y + height * 0.5f, textY = middle - ImGui::GetTextLineHeight() * 0.5f;
        const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        // The numbers, at the right: what is selected, how much there is, how fast it is drawn.
        // "Draw calls" are the scene's own: its meshes, particles and decals. What else the GPU
        // is asked for (their shadows, the depth before the picture, the full-screen passes) is
        // in the tip, each kind on its own line.
        char right[200];
        const RenderStats stats = shownRenderStats(editor);
        size_t selected = editor.scene.selection().size();
        const char* calls = stats.drawCalls == 1 ? "draw call" : "draw calls";
        if (selected > 0) {
            std::snprintf(right, sizeof(right), "%zu selected     %zu entities     %d %s     %.0f fps", selected, editor.scene.entities().size(), stats.drawCalls, calls,
                          double(editor.engine.time.framesPerSecond));
        } else {
            std::snprintf(right, sizeof(right), "%zu entities     %d %s     %.0f fps", editor.scene.entities().size(), stats.drawCalls, calls,
                          double(editor.engine.time.framesPerSecond));
        }
        float rightWidth = ImGui::CalcTextSize(right).x;
        draw->AddText(ImVec2(origin.x + width - rightWidth - 12, textY), dim, right);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + width - rightWidth - 16, origin.y));
        ImGui::InvisibleButton("##numbers", ImVec2(rightWidth + 16, height));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("What drawing this picture took, last frame");
            ImGui::Separator();
            for (const std::string& line : renderStatsLines(stats)) ImGui::TextUnformatted(line.c_str());
            ImGui::TextDisabled("GPU %.1f ms a frame", double(editor.engine.renderer().lastGpuMilliseconds()));
            ImGui::EndTooltip();
        }
        // The engine's own shaders, only when something is wrong with them: the fullest did not compile
        // on this Mac and the scene is drawn with less. Said for as long as it is so, not once in a log.
        const std::string& shaderProblem = editor.engine.renderer().shaderProblem();
        if (!shaderProblem.empty()) {
            const char* label = "Shaders reduced";
            const float labelWidth = ImGui::CalcTextSize(label).x;
            ImVec2 min(origin.x + width - rightWidth - 12 - labelWidth - 30, origin.y);
            ImGui::SetCursorScreenPos(min);
            bool pressed = ImGui::InvisibleButton("##shaders", ImVec2(labelWidth + 20, height));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                std::string firstLines = shaderProblem.substr(0, 600);
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(560.0f);
                ImGui::TextUnformatted("The engine's full shaders did not compile on this Mac, so the scene is drawn with simpler ones: "
                                       "some effects are missing (water, clouds with depth, fog on the ground, ray tracing). "
                                       "This is a fault in the engine's shaders, not in your project. What the compiler said:");
                ImGui::Spacing();
                ImGui::TextDisabled("%s", firstLines.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (pressed) editor.openPanel("Console");
            draw->AddCircleFilled(ImVec2(min.x + 6, middle), 4.0f, levelColor(ConsoleMessage::Level::Error));
            draw->AddText(ImVec2(min.x + 16, textY), levelColor(ConsoleMessage::Level::Error), label);
            rightWidth += labelWidth + 40;
        }

        // The last thing the console heard, at the left: a click opens the Console on it.
        if (!editor.console.empty()) {
            const ConsoleMessage& last = editor.console.back();
            std::string line = last.text.substr(0, last.text.find('\n'));
            const float room = width - rightWidth - 70;
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
            bool pressed = ImGui::InvisibleButton("##last", ImVec2(std::max(room, 40.0f), height));
            bool isHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Open the Console");
            if (pressed) editor.openPanel("Console");
            draw->AddCircleFilled(ImVec2(origin.x + 16, middle), 4.0f, levelColor(last.level));
            ImU32 color = last.level == ConsoleMessage::Level::Error ? levelColor(last.level) : isHovered ? ImGui::GetColorU32(ImGuiCol_Text) : dim;
            draw->PushClipRect(ImVec2(origin.x + 26, origin.y), ImVec2(origin.x + room, origin.y + height), true);
            draw->AddText(ImVec2(origin.x + 28, textY), color, line.c_str());
            draw->PopClipRect();
        }
    }
    ImGui::End();
}

}  // namespace molten::editor
