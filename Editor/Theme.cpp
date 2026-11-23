// The look of the editor: one dark theme, with the color of hot iron for what is selected.
#include "Theme.hpp"

#include <imgui.h>

#include <ImGuizmo.h>

namespace molten::editor {

namespace {

struct Look {
    std::string name;
    unsigned background, panel, raised, hover, active, border, text, dim, accent, accentLight;
};

const std::vector<Look>& looks() {
    static const std::vector<Look> all = {
        {"Forge", 0x17191d, 0x1e2126, 0x282c33, 0x333842, 0x3d434f, 0x0e0f12, 0xdfe3ea, 0x8b93a1, 0xff7a2f, 0xff9455},
        {"Ocean", 0x151a21, 0x1b222b, 0x243040, 0x2e3c4f, 0x38485e, 0x0c1015, 0xdde6f0, 0x8796a8, 0x3e9bff, 0x6db3ff},
        {"Moss", 0x161b18, 0x1c231f, 0x26302a, 0x313d35, 0x3b4a40, 0x0d110e, 0xdfe8e2, 0x8a9a8f, 0x4cc27e, 0x74d79c},
        {"Midnight", 0x0d0e11, 0x131418, 0x1c1e24, 0x262931, 0x30343e, 0x050608, 0xd8dce4, 0x7f8796, 0xff7a2f, 0xff9455},
    };
    return all;
}

std::string gTheme = "Forge";

const Look& lookNow() {
    for (const Look& look : looks()) {
        if (look.name == gTheme) return look;
    }
    return looks().front();
}

}  // namespace

const std::vector<std::string>& themeNames() {
    static std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Look& look : looks()) out.push_back(look.name);
        return out;
    }();
    return names;
}

void setTheme(const std::string& name) { gTheme = name; }
const std::string& currentTheme() { return lookNow().name; }
unsigned themeAccent() { return lookNow().accent; }

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowRounding = 6;
    style.ChildRounding = 5;
    style.FrameRounding = 4;
    style.PopupRounding = 6;
    style.GrabRounding = 4;
    style.TabRounding = 5;
    style.ScrollbarRounding = 8;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 16;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;
    style.TabBarBorderSize = 1;
    // Tabs: a crowded row shrinks them before it scrolls them (the ones behind show their icon
    // alone then: ui::windowName), and a tab behind shows its close button only under the pointer.
    style.TabMinWidthShrink = 34;
    style.TabCloseButtonMinWidthUnselected = 0;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1;
    style.DockingSeparatorSize = 2;

    auto rgb = [](unsigned hex, float alpha = 1.0f) {
        return ImVec4(float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, alpha);
    };
    const Look& look = lookNow();
    const ImVec4 background = rgb(look.background), panel = rgb(look.panel), raised = rgb(look.raised), hover = rgb(look.hover);
    const ImVec4 active = rgb(look.active), border = rgb(look.border), text = rgb(look.text), dim = rgb(look.dim);
    const ImVec4 accent = rgb(look.accent);         // in Forge, the color of hot iron: selection and focus only

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = dim;
    colors[ImGuiCol_WindowBg] = panel;
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = rgb(0x1a1c21, 0.98f);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = background;
    colors[ImGuiCol_FrameBgHovered] = raised;
    colors[ImGuiCol_FrameBgActive] = hover;
    colors[ImGuiCol_TitleBg] = background;
    colors[ImGuiCol_TitleBgActive] = background;
    colors[ImGuiCol_TitleBgCollapsed] = background;
    colors[ImGuiCol_MenuBarBg] = background;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = hover;
    colors[ImGuiCol_ScrollbarGrabActive] = active;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = rgb(look.accentLight);
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = hover;
    colors[ImGuiCol_ButtonActive] = active;
    colors[ImGuiCol_Header] = raised;
    colors[ImGuiCol_HeaderHovered] = hover;
    colors[ImGuiCol_HeaderActive] = active;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = rgb(look.accent, 0.6f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = background;
    colors[ImGuiCol_TabHovered] = hover;
    colors[ImGuiCol_TabSelected] = panel;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = background;
    colors[ImGuiCol_TabDimmedSelected] = panel;
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_DockingPreview] = rgb(look.accent, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableHeaderBg] = background;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = rgb(0x24272d);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = rgb(0xffffff, 0.02f);
    colors[ImGuiCol_TextSelectedBg] = rgb(look.accent, 0.35f);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.55f);
    style.ScaleAllSizes(scale);

    ImGuizmo::Style& gizmo = ImGuizmo::GetStyle();
    gizmo.TranslationLineThickness = 4.0f;
    gizmo.TranslationLineArrowSize = 8.0f;
    gizmo.RotationLineThickness = 3.5f;
    gizmo.RotationOuterLineThickness = 2.5f;
    gizmo.ScaleLineThickness = 4.0f;
    gizmo.ScaleLineCircleSize = 7.0f;
    gizmo.CenterCircleSize = 6.0f;
    gizmo.Colors[ImGuizmo::DIRECTION_X] = rgb(0xe5484d);
    gizmo.Colors[ImGuizmo::DIRECTION_Y] = rgb(0x46a758);
    gizmo.Colors[ImGuizmo::DIRECTION_Z] = rgb(0x3e7bfa);
    gizmo.Colors[ImGuizmo::PLANE_X] = rgb(0xe5484d, 0.45f);
    gizmo.Colors[ImGuizmo::PLANE_Y] = rgb(0x46a758, 0.45f);
    gizmo.Colors[ImGuizmo::PLANE_Z] = rgb(0x3e7bfa, 0.45f);
    gizmo.Colors[ImGuizmo::SELECTION] = rgb(0xffc04d);
    gizmo.Colors[ImGuizmo::ROTATION_USING_BORDER] = accent;
    gizmo.Colors[ImGuizmo::ROTATION_USING_FILL] = rgb(look.accent, 0.35f);
    gizmo.Colors[ImGuizmo::TEXT] = text;
}

}  // namespace molten::editor
