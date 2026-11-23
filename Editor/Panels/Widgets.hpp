// Widgets.hpp
// The rows the editor's panels are made of: a label on the left, a control on the right.
// Every function returns true while the value is being changed.
#pragma once

#include "API/EditorAPI.hpp"
#include "ImGuiLayer.hpp"
#include "Icons.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace molten::editor::ui {

/// The name of an asset being dragged from the asset browser: the payload is its path
/// relative to Assets, with the terminating zero.
/// Shows `text` beside the pointer while it rests on the item just drawn.
inline void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}

inline constexpr const char* kAssetPayload = "MOLTEN_ASSET";
/// An entity being dragged in the hierarchy: the payload is an molten::Entity.
inline constexpr const char* kEntityPayload = "MOLTEN_ENTITY";

/// Starts a row: draws the label and leaves the cursor where the control goes, as wide as what is left.
inline void label(const char* text) {
    ImGui::AlignTextToFramePadding();
    // As in ImGui, what follows "##" tells two rows with the same label apart and is not shown.
    const char* end = std::strstr(text, "##");
    const float column = std::max(110.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    // From where the label starts (inside a folded section it is indented) to the control's column.
    const float room = column - ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x;
    if (ImGui::CalcTextSize(text, end).x <= room) {
        ImGui::TextUnformatted(text, end);
    } else {
        // Too long for its column: cut with an ellipsis rather than under the control, and said
        // whole when the pointer rests on it.
        ImVec2 at = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetTextLineHeight();
        ImGui::Dummy(ImVec2(room, height));
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), at, ImVec2(at.x + room, at.y + height), at.x + room, text, end, nullptr);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%.*s", int(end ? end - text : std::strlen(text)), text);
    }
    ImGui::SameLine(column);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/// The default way a number is written: as short as says it (820, 0.35, -27.9), not with a tail of zeros.
inline constexpr const char* kNumberFormat = "%g";

inline bool dragFloat(const char* name, float& value, float speed = 0.01f, float minimum = 0.0f, float maximum = 0.0f, const char* format = kNumberFormat) {
    label(name);
    ImGui::PushID(name);
    bool changed = ImGui::DragFloat("##value", &value, speed, minimum, maximum, format, minimum < maximum ? ImGuiSliderFlags_AlwaysClamp : 0);
    ImGui::PopID();
    return changed;
}

/// A slider as a bar that fills from the left up to its value, with the number written over
/// it (Cmd and a click types one): how much of the range is used is read at a glance.
inline bool sliderBar(const char* id, float& value, float minimum, float maximum, const char* format = "%.2f") {
    const float width = ImGui::CalcItemWidth(), height = ImGui::GetFrameHeight();
    const float rounding = ImGui::GetStyle().FrameRounding;
    ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + width, min.y + height);
    float filled = maximum > minimum ? std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f) : 0.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
    ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
    if (filled > 0.0f) {
        float end = std::max(min.x + width * filled, min.x + 2.0f);
        draw->AddRectFilled(min, ImVec2(end, max.y), ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.30f)), rounding,
                            filled > 0.985f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
        draw->AddLine(ImVec2(end, min.y + 3), ImVec2(end, max.y - 3), ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.95f)), 2.0f);
    }
    // The control itself is drawn over that with nothing of its own but the number.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
    bool changed = ImGui::SliderFloat(id, &value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopStyleColor(5);
    return changed;
}

inline bool sliderFloat(const char* name, float& value, float minimum, float maximum, const char* format = "%.2f") {
    label(name);
    ImGui::PushID(name);
    bool changed = sliderBar("##value", value, minimum, maximum, format);
    ImGui::PopID();
    return changed;
}

/// An angle kept in radians and shown in degrees.
inline bool dragDegrees(const char* name, float& radiansValue, float minimum, float maximum) {
    float shown = degrees(radiansValue);
    if (!dragFloat(name, shown, 0.5f, minimum, maximum, "%.1f\xC2\xB0")) return false;
    radiansValue = radians(shown);
    return true;
}

inline bool dragInt(const char* name, int& value, float speed, int minimum, int maximum) {
    label(name);
    ImGui::PushID(name);
    bool changed = ImGui::DragInt("##value", &value, speed, minimum, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopID();
    return changed;
}

inline bool dragVec2(const char* name, Vec2& value, float speed = 0.01f) {
    label(name);
    ImGui::PushID(name);
    float v[2] = {value.x, value.y};
    bool changed = ImGui::DragFloat2("##value", v, speed, 0.0f, 0.0f, kNumberFormat);
    if (changed) value = Vec2{v[0], v[1]};
    ImGui::PopID();
    return changed;
}

/// Three numbers with the colors of the axes: red x, green y, blue z.
inline bool dragVec3(const char* name, Vec3& value, float speed = 0.01f, const char* format = kNumberFormat) {
    label(name);
    ImGui::PushID(name);
    static const ImU32 colors[3] = {IM_COL32(229, 72, 77, 255), IM_COL32(70, 167, 88, 255), IM_COL32(62, 123, 250, 255)};
    float v[3] = {value.x, value.y, value.z};
    bool changed = false;
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float width = (ImGui::CalcItemWidth() - spacing * 2) / 3.0f;
    for (int i = 0; i < 3; i++) {
        ImGui::PushID(i);
        if (i > 0) ImGui::SameLine(0, spacing);
        ImGui::SetNextItemWidth(width);
        changed |= ImGui::DragFloat("##axis", &v[i], speed, 0.0f, 0.0f, format);
        // A thin bar of the axis' color on the left edge of the field.
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + 3, max.y), colors[i], 2.0f, ImDrawFlags_RoundCornersLeft);
        ImGui::PopID();
    }
    if (changed) value = Vec3{v[0], v[1], v[2]};
    ImGui::PopID();
    return changed;
}

inline bool checkbox(const char* name, bool& value) {
    label(name);
    ImGui::PushID(name);
    bool changed = ImGui::Checkbox("##value", &value);
    ImGui::PopID();
    return changed;
}

/// A color without alpha. `hdr` lets the values go above 1: light.
inline bool color(const char* name, Vec3& value, bool hdr = false) {
    label(name);
    ImGui::PushID(name);
    float v[3] = {value.x, value.y, value.z};
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | (hdr ? ImGuiColorEditFlags_HDR : 0);
    bool changed = ImGui::ColorEdit3("##value", v, flags);
    if (changed) value = Vec3{v[0], v[1], v[2]};
    ImGui::PopID();
    return changed;
}

inline bool color(const char* name, Vec4& value) {
    label(name);
    ImGui::PushID(name);
    float v[4] = {value.x, value.y, value.z, value.w};
    bool changed = ImGui::ColorEdit4("##value", v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
    if (changed) value = Vec4{v[0], v[1], v[2], v[3]};
    ImGui::PopID();
    return changed;
}

/// A choice among names. `index` may be -1 for "none of them".
inline bool combo(const char* name, int& index, const std::vector<std::string>& choices) {
    label(name);
    ImGui::PushID(name);
    bool changed = false;
    const char* shown = index >= 0 && index < int(choices.size()) ? choices[size_t(index)].c_str() : "";
    if (ImGui::BeginCombo("##value", shown)) {
        for (int i = 0; i < int(choices.size()); i++) {
            bool isSelected = i == index;
            if (ImGui::Selectable(choices[size_t(i)].c_str(), isSelected) && !isSelected) {
                index = i;
                changed = true;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

inline bool text(const char* name, std::string& value) {
    label(name);
    ImGui::PushID(name);
    bool changed = ImGui::InputText("##value", &value);
    ImGui::PopID();
    return changed;
}

/// A line of dim text: for facts that cannot be edited.
inline void info(const char* name, const std::string& value) {
    label(name);
    ImGui::TextDisabled("%s", value.c_str());
}

/// If an asset of one of the given types is dropped on the last item, returns its path.
inline bool acceptAsset(const AssetDatabase& assets, std::initializer_list<AssetType> types, std::string& path) {
    bool accepted = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload, ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::string dropped(static_cast<const char*>(payload->Data));
            const AssetInfo* info = assets.find(dropped);
            bool fits = false;
            for (AssetType type : types) fits = fits || (info && info->type == type);
            if (fits && payload->IsDelivery()) {
                path = dropped;
                accepted = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return accepted;
}

/// A header that folds, open by default, with room for a menu button at its right end.
inline bool section(const char* name) {
    return ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
}

/// The kind of icon a component gets, from its name (the built-in ones) or its category.
inline std::string iconKindOf(const std::string& name, const std::string& category = {}) {
    auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
    // The built-in ones, each its own; anything else by its category. A name names a Lucide icon
    // (Icons.hpp) or one of iconLookOf's kinds, which also give it its color.
    if (name == "Transform") return "axes";
    if (has("Mesh Renderer") || has("MeshRenderer")) return "cube";
    if (has("LOD")) return "layers";
    if (has("Light") && !has("Lights")) return "light";
    if (has("Reflection") || has("Probe")) return "aperture";
    if (has("Camera")) return "camera";
    if (has("Collider")) return "collider";
    if (has("Cloth")) return "flag";
    if (has("Buoyancy")) return "anchor";
    if (has("Rigid") || has("Joint") || has("Force")) return "physics";
    if (has("Particle") || has("Explosion") || has("Flicker")) return "spark";
    if (has("Trail")) return "wind";
    if (has("Audio") || has("Sound") || has("Music")) return "audio";
    if (has("Animator") || has("Animation") || has("IK")) return "anim";
    if (has("Character") || has("Ragdoll") || has("Player")) return "person";
    if (has("Boat") || has("Ship") || has("Sail")) return "sailboat";
    if (has("Plane") || has("Aircraft") || has("Flight")) return "plane";
    if (has("Vehicle") || has("Wheel") || has("Motor") || has("Car")) return "car";
    if (has("Spline") || has("Road") || has("Path")) return "spline";
    if (has("Terrain")) return "mountain";
    if (has("Foliage") || has("Tree") || has("Grass")) return "trees";
    if (has("Water") || has("Sea") || has("Ocean")) return "waves";
    if (has("Weather") || has("Wind")) return "cloud-sun";
    if (has("Visual Script") || has("VisualScript")) return "graph";
    if (has("Network")) return "wifi";
    if (has("Weapon") || has("Health") || has("Damage")) return "target";
    if (has("Pickup") || has("Inventory")) return "package";
    if (has("Trigger")) return "square-dashed";
    if (has("Text") || has("Label")) return "type";
    if (category == "UI" || has("UI") || has("Hud") || has("Touch") || has("Button")) return "ui";
    if (category == "Environment") return "leaf";
    if (category == "Logic") return "logic";
    if (category == "Effects") return "spark";
    if (category == "Rendering") return "eye";
    if (category == "Physics") return "physics";
    if (category == "Audio") return "audio";
    if (category == "Animation") return "anim";
    if (category == "Vehicles") return "car";
    return "code";                      // a game's own script
}

/// The icon that stands for a kind of thing (iconKindOf's words, or any Lucide name: "folder"),
/// and the color it is drawn in when it has one of its own (0: the color it is given).
struct IconLook {
    const char* glyph = nullptr;
    ImU32 tint = 0;
};
inline IconLook iconLookOf(const std::string& kind) {
    struct Entry {
        const char* kind;
        const char* glyph;
        ImU32 tint;
    };
    static const Entry kinds[] = {
        {"axes", icon::Move3d, 0},
        {"cube", icon::Box, IM_COL32(110, 160, 235, 255)},
        {"light", icon::Lightbulb, IM_COL32(255, 205, 90, 255)},
        {"sun", icon::Sun, IM_COL32(255, 205, 90, 255)},
        {"camera", icon::Video, IM_COL32(120, 205, 140, 255)},
        {"collider", icon::SquareDashed, IM_COL32(120, 215, 130, 255)},
        {"physics", icon::Atom, 0},
        {"spark", icon::Sparkles, IM_COL32(255, 160, 70, 255)},
        {"audio", icon::Volume2, 0},
        {"anim", icon::Clapperboard, IM_COL32(255, 170, 90, 255)},
        {"person", icon::PersonStanding, 0},
        {"wheel", icon::Car, 0},
        {"spline", icon::Spline, 0},
        {"graph", icon::Workflow, IM_COL32(255, 140, 60, 255)},
        {"ui", icon::LayoutTemplate, 0},
        {"leaf", icon::Sprout, IM_COL32(120, 205, 120, 255)},
        {"logic", icon::GitBranch, 0},
        {"eye", icon::Eye, 0},
        {"material", icon::Palette, IM_COL32(110, 165, 225, 255)},
        {"code", icon::Braces, IM_COL32(110, 200, 210, 255)},
        {"move", icon::Move, 0},
        {"rotate", icon::Rotate3d, 0},
        {"scale", icon::Scaling, 0},
        {"all", icon::Move3d, 0},
        {"magnet", icon::Magnet, 0},
        {"grid", icon::Grid3x3, 0},
        {"folder", icon::Folder, IM_COL32(230, 180, 90, 255)},
    };
    for (const Entry& entry : kinds) {
        if (kind == entry.kind) return {entry.glyph, entry.tint};
    }
    if (const char* glyph = icon::named(kind)) return {glyph, 0};
    return {icon::Braces, IM_COL32(110, 200, 210, 255)};      // a game's own script
}

/// Draws an icon (see iconLookOf), centred at `c`, `s` points across: in its own color if it has
/// one, else in `color`. Pass `ownColor` false to always draw it in `color`.
inline void drawIcon(ImDrawList* d, ImVec2 c, float s, const std::string& kind, ImU32 color, bool ownColor = true) {
    IconLook look = iconLookOf(kind);
    ImFont* font = ImGuiLayer::iconFont();
    if (!font || !look.glyph) return;
    ImU32 ink = ownColor && look.tint ? look.tint : color;
    // Keep the color's own transparency (a faded row fades its icon too).
    if (ownColor && look.tint) ink = (ink & 0x00FFFFFFu) | (color & 0xFF000000u);
    ImVec2 size = font->CalcTextSizeA(s, FLT_MAX, 0.0f, look.glyph);
    d->AddText(font, s, ImVec2(std::round(c.x - size.x * 0.5f), std::round(c.y - size.y * 0.5f)), ink, look.glyph);
}

// ---- the editor's windows: an icon on each tab, the same window whatever its tab shows

/// The icon on the tab of a window of the editor, by its title; nullptr for none.
inline const char* windowIcon(const std::string& title) {
    static const std::pair<const char*, const char*> icons[] = {
        {"Scene", icon::Box},          {"Game", icon::Gamepad2},        {"Hierarchy", icon::ListTree},
        {"Inspector", icon::SlidersHorizontal}, {"Explorer", icon::FolderTree}, {"Console", icon::TerminalSquare},
        {"Lighting", icon::Lightbulb},  {"Environment", icon::CloudSun}, {"Rendering", icon::Aperture},
        {"Post Processing", icon::WandSparkles}, {"Animation", icon::Film}, {"Timeline", icon::GanttChart}, {"Animator", icon::Clapperboard},
        {"Visual Script", icon::Workflow}, {"Shader Graph", icon::Blocks}, {"Profiler", icon::Activity}, {"Source Control", icon::GitBranch},
        {"Scene Weight", icon::Weight},   {"Manual", icon::BookOpen},      {"Project Settings", icon::Settings},
        {"Input", icon::Gamepad2},      {"Preferences", icon::Settings2}, {"Build Settings", icon::Hammer},
    };
    for (const auto& entry : icons) {
        if (title == entry.first) return entry.second;
    }
    return nullptr;
}

/// What ImGui knows an editor window by: its title alone ("###Hierarchy"), whatever its tab shows.
/// Look a window up, dock it or focus it by this.
inline std::string windowId(const std::string& title) {
    return "###" + title;
}

/// What a window's tab shows: its icon and its title. Known by windowId(title), so what it shows
/// may change. In a row of tabs too narrow for every title, the tabs behind show their icon alone
/// (windowTabTip puts the title over them) and the one in front keeps its title.
/// Whether a window's tab shows its icon alone: it is behind others in a row too narrow for all
/// their titles. (Such a tab has no close button either: the one in front has, and the tab's menu.)
inline bool isCompactTab(const std::string& title) {
    if (!windowIcon(title)) return false;
    ImGuiWindow* window = ImGui::FindWindowByName(windowId(title).c_str());
    ImGuiDockNode* node = window ? window->DockNode : nullptr;
    return node && node->TabBar && node->Windows.Size > 1 && node->TabBar->SelectedTabId != window->TabId &&
           node->Size.x / float(node->Windows.Size) < 120.0f;
}

inline std::string windowName(const std::string& title) {
    const char* glyph = windowIcon(title);
    const std::string id = windowId(title);
    if (!glyph) return title + id;
    if (isCompactTab(title)) return std::string(glyph) + id;
    return std::string(glyph) + "  " + title + id;
}

/// Between a window's Begin and End: its title over its tab while the mouse rests there (a tab
/// that shows its icon alone says what it is).
inline void windowTabTip(const std::string& title) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->DockIsActive && window->TabId != 0 && g.HoveredId == window->TabId && g.HoveredIdTimer > 0.45f) ImGui::SetTooltip("%s", title.c_str());
}

/// Saved window settings (imgui.ini, a saved layout) from before the windows had icons: each
/// "[Window][Hierarchy]" made "[Window][###Hierarchy]", the name the window has now, so that it
/// keeps its place. Windows whose names have "##" in them already are left as they are.
inline std::string withWindowIds(std::string ini) {
    const std::string tag = "[Window][";
    for (size_t at = ini.find(tag); at != std::string::npos; at = ini.find(tag, at + tag.size())) {
        size_t start = at + tag.size(), end = ini.find(']', start);
        if (end == std::string::npos) break;
        std::string name = ini.substr(start, end - start);
        // Only the editor's own windows: not ImGui's (its dock space's host, its Debug window).
        if (name.empty() || name.find("##") != std::string::npos || name.rfind("WindowOverViewport", 0) == 0 || name.rfind("Debug", 0) == 0) continue;
        ini.insert(start, "###");
    }
    return ini;
}

/// Text with an icon before it, for a label or a menu: "<icon>  Lighting".
inline std::string withIcon(const char* glyph, const char* text) {
    return std::string(glyph) + "  " + text;
}

/// A row of a list with an icon before its words (a page of settings, a kind of thing to show): the
/// icon in a column of its own and the words clear of it, as the Mac's own lists have them.
inline bool iconSelectable(const char* label, const std::string& icon, bool isSelected, float height = 26.0f, float iconSize = 14.0f) {
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    // As wide as the list, and never narrower than its words (a menu that fits itself to what it holds).
    float needed = 6 + iconSize + 12 + ImGui::CalcTextSize(label).x + 10;
    bool clicked = ImGui::Selectable("##row", isSelected, 0, ImVec2(std::max(needed, ImGui::GetContentRegionAvail().x), height));
    ImGui::PopID();
    ImVec2 size = ImGui::GetItemRectSize();
    float middle = at.y + size.y * 0.5f;
    ImU32 color = ImGui::GetColorU32(isSelected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    if (ImGui::IsItemHovered()) color = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawIcon(drawList, ImVec2(at.x + 6 + iconSize * 0.5f, middle), iconSize, icon, color);
    drawList->AddText(ImVec2(at.x + 6 + iconSize + 12, middle - ImGui::GetFontSize() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label);
    return clicked;
}

/// A component's header: an icon of what kind it is, its name, folding; open by default.
inline bool componentSection(const char* label, const char* id, const std::string& iconKind) {
    std::string text = std::string("      ") + label + "###" + id;
    bool open = ImGui::CollapsingHeader(text.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    float size = ImGui::GetFontSize() * 0.95f;
    ImVec2 center(min.x + ImGui::GetFontSize() + style.FramePadding.x * 3 + size * 0.5f, (min.y + max.y) * 0.5f);
    drawIcon(ImGui::GetWindowDrawList(), center, size, iconKind, ImGui::GetColorU32(ImGuiCol_Text));
    return open;
}

/// The small button at the right end of a component's header that opens what can be done to it:
/// three dots, one over the other.
inline bool moreButton(const char* id) {
    const float side = ImGui::GetFontSize() + 4;
    ImVec2 min = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    bool isHovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (isHovered) draw->AddRectFilled(min, ImVec2(min.x + side, min.y + side), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 4.0f);
    ImU32 color = ImGui::GetColorU32(isHovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    for (int i = -1; i <= 1; i++) draw->AddCircleFilled(ImVec2(min.x + side * 0.5f, min.y + side * 0.5f + float(i) * 4.5f), 1.4f, color);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Reset, copy, remove...");
    return pressed;
}

/// The name of a page of settings: a little larger than the text under it.
inline void title(const char* text) {
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
}

/// A title inside a section: words and a line, not something that folds (a material's Surface, Textures).
inline void subheading(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
}

/// "1.2 MB"
inline std::string byteSize(uint64_t bytes) {
    char buffer[32];
    if (bytes < 1024) std::snprintf(buffer, sizeof(buffer), "%llu bytes", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024 * 1024) std::snprintf(buffer, sizeof(buffer), "%.1f KB", double(bytes) / 1024.0);
    else std::snprintf(buffer, sizeof(buffer), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buffer;
}

}  // namespace molten::editor::ui
