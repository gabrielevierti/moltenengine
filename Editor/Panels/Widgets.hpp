// Widgets.hpp
// The rows the editor's panels are made of: a label on the left, a control on the right.
// Every function returns true while the value is being changed.
#pragma once

#include "API/EditorAPI.hpp"
#include "ImGuiLayer.hpp"

#include <imgui.h>
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
    ImGui::TextUnformatted(text, end);
    ImGui::SameLine(std::max(110.0f, ImGui::GetContentRegionAvail().x * 0.36f));
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
    if (name == "Transform") return "axes";
    if (has("Mesh Renderer") || has("MeshRenderer")) return "cube";
    if (has("Light") && !has("Lights")) return "light";
    if (has("Camera")) return "camera";
    if (has("Collider")) return "collider";
    if (has("Rigid") || has("Joint") || has("Buoyancy")) return "physics";
    if (has("Particle") || has("Explosion") || has("Flicker")) return "spark";
    if (has("Audio") || has("Sound")) return "audio";
    if (has("Animator") || has("Animation") || has("IK")) return "anim";
    if (has("Character") || has("Ragdoll")) return "person";
    if (has("Vehicle") || has("Wheel") || has("Motor") || has("Boat")) return "wheel";
    if (has("Spline") || has("Road") || has("Path")) return "spline";
    if (has("Visual Script") || has("VisualScript")) return "graph";
    if (category == "UI" || has("UI") || has("Hud") || has("Touch")) return "ui";
    if (category == "Environment") return "leaf";
    if (category == "Logic") return "logic";
    if (category == "Effects") return "spark";
    if (category == "Rendering") return "eye";
    if (category == "Physics") return "physics";
    if (category == "Audio") return "audio";
    if (category == "Animation") return "anim";
    if (category == "Vehicles") return "wheel";
    return "code";                      // a game's own script
}

/// Draws a small line icon of a kind (see iconKindOf), centred at `c`, `s` points across.
inline void drawIcon(ImDrawList* d, ImVec2 c, float s, const std::string& kind, ImU32 color) {
    float r = s * 0.5f, t = std::max(1.2f, s / 11.0f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * r, c.y + y * r); };
    if (kind == "axes") {
        d->AddLine(P(-0.7f, 0.7f), P(0.8f, 0.7f), IM_COL32(230, 90, 80, 255), t);
        d->AddLine(P(-0.7f, 0.7f), P(-0.7f, -0.85f), IM_COL32(120, 210, 110, 255), t);
        d->AddLine(P(-0.7f, 0.7f), P(0.2f, -0.1f), IM_COL32(90, 150, 255, 255), t);
    } else if (kind == "cube") {
        ImVec2 top[4] = {P(0, -0.9f), P(0.85f, -0.45f), P(0, 0), P(-0.85f, -0.45f)};
        d->AddPolyline(top, 4, color, ImDrawFlags_Closed, t);
        d->AddLine(P(-0.85f, -0.45f), P(-0.85f, 0.45f), color, t);
        d->AddLine(P(0.85f, -0.45f), P(0.85f, 0.45f), color, t);
        d->AddLine(P(0, 0), P(0, 0.9f), color, t);
        d->AddLine(P(-0.85f, 0.45f), P(0, 0.9f), color, t);
        d->AddLine(P(0.85f, 0.45f), P(0, 0.9f), color, t);
    } else if (kind == "light") {
        d->AddCircle(P(0, -0.15f), r * 0.5f, IM_COL32(255, 210, 90, 255), 12, t);
        d->AddLine(P(-0.25f, 0.55f), P(0.25f, 0.55f), color, t);
        d->AddLine(P(-0.18f, 0.8f), P(0.18f, 0.8f), color, t);
    } else if (kind == "camera") {
        d->AddRect(P(-0.9f, -0.45f), P(0.35f, 0.55f), color, 1.5f, 0, t);
        ImVec2 lens[3] = {P(0.35f, 0.05f), P(0.9f, -0.4f), P(0.9f, 0.5f)};
        d->AddPolyline(lens, 3, color, ImDrawFlags_Closed, t);
    } else if (kind == "collider") {
        d->AddRect(P(-0.8f, -0.8f), P(0.8f, 0.8f), IM_COL32(120, 220, 120, 255), 0.0f, 0, t);
        d->AddLine(P(-0.8f, -0.8f), P(0.8f, 0.8f), IM_COL32(120, 220, 120, 140), t * 0.8f);
    } else if (kind == "physics") {
        d->AddCircle(P(0, 0.1f), r * 0.75f, color, 14, t);
        d->AddLine(P(0, -0.65f), P(0, -1.0f), color, t);
        d->AddLine(P(-0.35f, -0.95f), P(0.35f, -0.95f), color, t);
    } else if (kind == "spark") {
        for (int i = 0; i < 4; i++) {
            float a = float(i) * 0.785398f;
            d->AddLine(P(std::cos(a) * 0.9f, std::sin(a) * 0.9f), P(-std::cos(a) * 0.9f, -std::sin(a) * 0.9f), IM_COL32(255, 170, 70, 255), t);
        }
    } else if (kind == "audio") {
        ImVec2 cone[4] = {P(-0.9f, -0.3f), P(-0.4f, -0.3f), P(0.15f, -0.8f), P(0.15f, 0.8f)};
        d->AddPolyline(cone, 4, color, 0, t);
        d->AddLine(P(0.15f, 0.8f), P(-0.4f, 0.3f), color, t);
        d->AddLine(P(-0.4f, 0.3f), P(-0.9f, 0.3f), color, t);
        d->AddLine(P(-0.9f, 0.3f), P(-0.9f, -0.3f), color, t);
        d->PathArcTo(P(0.15f, 0), r * 0.55f, -0.9f, 0.9f, 8);
        d->PathStroke(color, 0, t);
    } else if (kind == "anim") {
        ImVec2 dia[4] = {P(0, -0.85f), P(0.85f, 0), P(0, 0.85f), P(-0.85f, 0)};
        d->AddConvexPolyFilled(dia, 4, IM_COL32(255, 175, 90, 255));
    } else if (kind == "person") {
        d->AddCircle(P(0, -0.55f), r * 0.32f, color, 10, t);
        d->PathArcTo(P(0, 0.9f), r * 0.75f, 3.4f, 6.02f, 10);
        d->PathStroke(color, 0, t);
    } else if (kind == "wheel") {
        d->AddCircle(c, r * 0.85f, color, 14, t);
        d->AddCircle(c, r * 0.3f, color, 8, t);
    } else if (kind == "spline") {
        d->AddBezierCubic(P(-0.9f, 0.7f), P(-0.3f, -1.3f), P(0.3f, 1.3f), P(0.9f, -0.7f), color, t);
    } else if (kind == "graph") {
        d->AddRect(P(-0.95f, -0.75f), P(-0.25f, -0.15f), color, 1.0f, 0, t);
        d->AddRect(P(0.25f, 0.15f), P(0.95f, 0.75f), color, 1.0f, 0, t);
        d->AddBezierCubic(P(-0.25f, -0.45f), P(0.1f, -0.45f), P(-0.1f, 0.45f), P(0.25f, 0.45f), IM_COL32(255, 140, 60, 255), t);
    } else if (kind == "ui") {
        d->AddRect(P(-0.9f, -0.7f), P(0.9f, 0.7f), color, 2.0f, 0, t);
        d->AddLine(P(-0.55f, -0.2f), P(0.55f, -0.2f), color, t);
        d->AddLine(P(-0.55f, 0.2f), P(0.15f, 0.2f), color, t);
    } else if (kind == "leaf") {
        d->PathArcTo(P(0.5f, 0.5f), r * 1.2f, 3.4f, 4.9f, 10);
        d->PathArcTo(P(-0.5f, -0.5f), r * 1.2f, 0.25f, 1.75f, 10);
        d->PathStroke(IM_COL32(120, 210, 120, 255), ImDrawFlags_Closed, t);
    } else if (kind == "logic") {
        d->AddLine(P(-0.9f, 0), P(-0.2f, 0), color, t);
        d->AddLine(P(-0.2f, 0), P(0.6f, -0.65f), color, t);
        d->AddLine(P(-0.2f, 0), P(0.6f, 0.65f), color, t);
        d->AddCircleFilled(P(0.65f, -0.65f), r * 0.18f, color);
        d->AddCircleFilled(P(0.65f, 0.65f), r * 0.18f, color);
    } else if (kind == "eye") {
        d->PathArcTo(P(0, 0.9f), r * 1.25f, 3.85f, 5.57f, 10);
        d->PathArcTo(P(0, -0.9f), r * 1.25f, 0.71f, 2.43f, 10);
        d->PathStroke(color, ImDrawFlags_Closed, t);
        d->AddCircleFilled(c, r * 0.25f, color);
    } else if (kind == "material") {
        d->AddCircleFilled(c, r * 0.85f, IM_COL32(90, 140, 190, 255), 16);
        d->AddCircleFilled(P(-0.3f, -0.3f), r * 0.28f, IM_COL32(220, 235, 255, 200), 10);
    } else {
        // Code: { }
        d->AddText(ImVec2(c.x - r * 0.95f, c.y - ImGui::GetFontSize() * 0.5f), IM_COL32(110, 200, 210, 255), "{}");
    }
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
