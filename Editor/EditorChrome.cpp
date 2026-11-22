// EditorChrome.cpp
// What frames the editor's windows: the bar under the menus (what the scene is doing at the
// left; Run, Hold and Frame and the layouts at the right) and the status bar along the
// bottom (the last thing the console heard, and the numbers).
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
        // Molten's own controls: three labelled keys at the right, and what the scene is doing at the left.
        const float keyHeight = 26;
        enum class Picture { Run, Stop, Hold, Frame };
        auto control = [&](const char* id, const char* label, Picture picture, bool isLit, bool isEnabled, ImU32 litColor, const std::string& tooltip) {
            float width = 34 + ImGui::CalcTextSize(label).x + 12;
            ImGui::BeginDisabled(!isEnabled);
            ImVec2 min = ImGui::GetCursorScreenPos();
            bool pressed = ImGui::InvisibleButton(id, ImVec2(width, keyHeight));
            bool isHovered = ImGui::IsItemHovered();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip.c_str());
            ImVec2 max(min.x + width, min.y + keyHeight), c(min.x + 18, (min.y + max.y) * 0.5f);
            ImU32 ink = !isEnabled ? IM_COL32(96, 102, 114, 255) : isLit ? litColor : IM_COL32(223, 227, 234, 255);
            drawList->AddRectFilled(min, max, isHovered ? IM_COL32(58, 64, 76, 255) : IM_COL32(40, 44, 52, 255), keyHeight * 0.5f);
            if (isLit) drawList->AddRect(min, max, litColor, keyHeight * 0.5f, ImDrawFlags_None, 1.5f);
            switch (picture) {
                case Picture::Run:      // a spark: the forge is lit
                    drawList->AddCircle(c, 7.0f, ink, 20, 1.6f);
                    drawList->AddTriangleFilled(ImVec2(c.x - 2.5f, c.y - 4), ImVec2(c.x - 2.5f, c.y + 4), ImVec2(c.x + 4.5f, c.y), ink);
                    break;
                case Picture::Stop:
                    drawList->AddCircle(c, 7.0f, ink, 20, 1.6f);
                    drawList->AddRectFilled(ImVec2(c.x - 3, c.y - 3), ImVec2(c.x + 3, c.y + 3), ink, 1.0f);
                    break;
                case Picture::Hold:     // two bars of a held breath
                    drawList->AddCircle(c, 7.0f, ink, 20, 1.6f);
                    drawList->AddLine(ImVec2(c.x - 2.2f, c.y - 3.5f), ImVec2(c.x - 2.2f, c.y + 3.5f), ink, 1.8f);
                    drawList->AddLine(ImVec2(c.x + 2.2f, c.y - 3.5f), ImVec2(c.x + 2.2f, c.y + 3.5f), ink, 1.8f);
                    break;
                case Picture::Frame:    // one frame of film, and an arrow past it
                    drawList->AddRect(ImVec2(c.x - 7, c.y - 5), ImVec2(c.x + 1, c.y + 5), ink, 1.5f, ImDrawFlags_None, 1.5f);
                    drawList->AddLine(ImVec2(c.x + 3, c.y), ImVec2(c.x + 8, c.y), ink, 1.6f);
                    drawList->AddTriangleFilled(ImVec2(c.x + 6, c.y - 3), ImVec2(c.x + 6, c.y + 3), ImVec2(c.x + 9.5f, c.y), ink);
                    break;
            }
            drawList->AddText(ImVec2(min.x + 32, c.y - ImGui::GetTextLineHeight() * 0.5f), ink, label);
            return pressed;
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 0));
        // What the scene is doing, at the left.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (keyHeight - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", !isPlaying ? "Editing" : isPaused ? "Held: changes are kept until Stop" : "Running: what you change now is undone at Stop");
        // The keys at the right end, beside the layout: their widths first, to know where they start.
        const float layoutWidth = 170;
        auto keyWidth = [](const char* label) { return 34 + ImGui::CalcTextSize(label).x + 12; };
        float keysWidth = keyWidth(isPlaying ? "Stop" : "Run") + keyWidth(isPaused ? "Resume" : "Hold") + keyWidth("Frame") + 12;
        // Each key placed on its own, all at the same height in the middle of the bar.
        const float barTop = ImGui::GetWindowPos().y, keysTop = barTop + (height - keyHeight) * 0.5f;
        float keyX = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - layoutWidth - keysWidth - 22;
        auto placeKey = [&](const char* label) {
            ImGui::SetCursorScreenPos(ImVec2(keyX, keysTop));
            keyX += keyWidth(label) + 6;
        };
        placeKey(isPlaying ? "Stop" : "Run");
        if (control("##play", isPlaying ? "Stop" : "Run", isPlaying ? Picture::Stop : Picture::Run, isPlaying, true, IM_COL32(255, 122, 47, 255),
                    isPlaying ? "Stop: back to editing, the scene returns to how it was (" + editor.keysOf("play") + ")" : "Run the scene (" + editor.keysOf("play") + ")")) {
            editor.commands.run("play");
        }
        placeKey(isPaused ? "Resume" : "Hold");
        if (control("##pause", isPaused ? "Resume" : "Hold", Picture::Hold, isPaused, isPlaying, IM_COL32(120, 190, 255, 255),
                    isPaused ? "Resume (" + editor.keysOf("pause") + ")"
                             : "Hold: freeze the game where it is; change things, then resume (" + editor.keysOf("pause") + ")")) {
            editor.commands.run("pause");
        }
        placeKey("Frame");
        if (control("##step", "Frame", Picture::Frame, false, isPlaying, 0, "Advance one frame and hold (" + editor.keysOf("step") + ")")) editor.commands.run("step");
        ImGui::PopStyleVar();

        // The layout of the windows, at the right end.
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - layoutWidth - 10, barTop + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(layoutWidth);
        if (ImGui::BeginCombo("##layout", ("Layout: " + layoutName_).c_str(), ImGuiComboFlags_HeightLarge)) {
            drawLayoutItems();
            ImGui::EndCombo();
        }
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
