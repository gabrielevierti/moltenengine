// Widgets.hpp
// The rows the editor's panels are made of: a label on the left, a control on the right.
// Every function returns true while the value is being changed.
#pragma once

#include "API/EditorAPI.hpp"
#include "ImGuiLayer.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace molten::editor::ui {

/// The name of an asset being dragged from the asset browser: the payload is its path
/// relative to Assets, with the terminating zero.
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

inline bool dragFloat(const char* name, float& value, float speed = 0.01f, float minimum = 0.0f, float maximum = 0.0f, const char* format = "%.3f") {
    label(name);
    ImGui::PushID(name);
    bool changed = ImGui::DragFloat("##value", &value, speed, minimum, maximum, format, minimum < maximum ? ImGuiSliderFlags_AlwaysClamp : 0);
    ImGui::PopID();
    return changed;
}

inline bool sliderFloat(const char* name, float& value, float minimum, float maximum, const char* format = "%.2f") {
    label(name);
    ImGui::PushID(name);
    bool changed = ImGui::SliderFloat("##value", &value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
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
    bool changed = ImGui::DragFloat2("##value", v, speed);
    if (changed) value = Vec2{v[0], v[1]};
    ImGui::PopID();
    return changed;
}

/// Three numbers with the colors of the axes: red x, green y, blue z.
inline bool dragVec3(const char* name, Vec3& value, float speed = 0.01f, const char* format = "%.3f") {
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

/// "1.2 MB"
inline std::string byteSize(uint64_t bytes) {
    char buffer[32];
    if (bytes < 1024) std::snprintf(buffer, sizeof(buffer), "%llu bytes", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024 * 1024) std::snprintf(buffer, sizeof(buffer), "%.1f KB", double(bytes) / 1024.0);
    else std::snprintf(buffer, sizeof(buffer), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buffer;
}

}  // namespace molten::editor::ui
