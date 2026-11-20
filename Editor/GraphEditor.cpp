#include "GraphEditor.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace molten::editor {

using namespace script;

namespace {

const float kNodeWidth = 210;
const float kTitleHeight = 26;
const float kRowHeight = 26;
const float kPinRadius = 5.5f;

/// "perSecond" as the label of a pin: "per second".
std::string pinLabel(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') {
            out += ' ';
            out += char(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

ImU32 categoryColor(const std::string& category) {
    if (category == "Events") return IM_COL32(190, 60, 50, 255);
    if (category == "Flow") return IM_COL32(110, 110, 120, 255);
    if (category == "Transform") return IM_COL32(200, 110, 40, 255);
    if (category == "Physics") return IM_COL32(60, 140, 90, 255);
    if (category == "Input") return IM_COL32(60, 110, 190, 255);
    if (category == "Math") return IM_COL32(90, 100, 120, 255);
    if (category == "Logic") return IM_COL32(140, 80, 170, 255);
    if (category == "Variables") return IM_COL32(40, 140, 150, 255);
    return IM_COL32(120, 105, 70, 255);
}

}  // namespace

ImU32 pinColor(PinType type) {
    switch (type) {
        case PinType::Flow: return IM_COL32(235, 235, 235, 255);
        case PinType::Float: return IM_COL32(120, 200, 120, 255);
        case PinType::Bool: return IM_COL32(220, 90, 90, 255);
        case PinType::Vec3: return IM_COL32(240, 200, 80, 255);
        case PinType::String: return IM_COL32(210, 120, 220, 255);
    }
    return IM_COL32_WHITE;
}

std::string GraphEditor::key(int node, const std::string& pin, bool isOutput) {
    return std::to_string(node) + (isOutput ? ">" : "<") + pin;
}

/// The control for an input nothing is wired to: a number, a checkbox, three numbers, a text.
bool GraphEditor::drawValueEditor(Node& node, const PinDef& pin, float width) {
    auto stored = node.values.find(pin.name);
    Value value = stored != node.values.end() ? Value::fromText(pin.type, stored->second) : pin.defaultValue;
    bool changed = false;
    ImGui::PushID(pin.name.c_str());
    ImGui::SetNextItemWidth(width);
    switch (pin.type) {
        case PinType::Float: changed = ImGui::DragFloat("##value", &value.x, 0.05f, 0.0f, 0.0f, "%.3g"); break;
        case PinType::Bool: {
            bool flag = value.x != 0;
            changed = ImGui::Checkbox("##value", &flag);
            value.x = flag ? 1.0f : 0.0f;
            break;
        }
        case PinType::Vec3: {
            float v[3] = {value.x, value.y, value.z};
            changed = ImGui::DragFloat3("##value", v, 0.05f, 0.0f, 0.0f, "%.3g");
            value.x = v[0];
            value.y = v[1];
            value.z = v[2];
            break;
        }
        case PinType::String: changed = ImGui::InputText("##value", &value.text); break;
        case PinType::Flow: break;
    }
    ImGui::PopID();
    if (changed) {
        value.type = pin.type;
        node.values[pin.name] = value.toText();
    }
    return changed;
}

bool GraphEditor::drawNode(Graph& graph, Node& node, const NodeDef* definition, ImVec2 origin, ImDrawList* drawList) {
    bool changed = false;
    const float z = zoom_;
    const float nodeWidth = kNodeWidth * z, titleHeight = kTitleHeight * z, rowHeight = kRowHeight * z;
    ImGui::PushID(node.id);
    size_t inputCount = definition ? definition->inputs.size() : 0, outputCount = definition ? definition->outputs.size() : 0;
    float rows = float(std::max<size_t>(std::max(inputCount, outputCount), 1));
    ImVec2 min(origin.x + node.x * z, origin.y + node.y * z);
    ImVec2 max(min.x + nodeWidth, min.y + titleHeight + rows * rowHeight + 8 * z);

    // The body: a shadow, the box, and the title in the color of the node's family.
    bool selected = isSelected(node.id);
    float round = 7.0f * z;
    drawList->AddRectFilled(ImVec2(min.x + 3, min.y + 4), ImVec2(max.x + 3, max.y + 4), IM_COL32(0, 0, 0, 90), round);
    drawList->AddRectFilled(min, max, IM_COL32(38, 41, 48, 245), round);
    ImU32 titleColor = definition ? categoryColor(definition->category) : IM_COL32(150, 40, 40, 255);
    drawList->AddRectFilled(min, ImVec2(max.x, min.y + titleHeight), titleColor, round, ImDrawFlags_RoundCornersTop);
    drawList->AddRect(min, max, selected ? IM_COL32(255, 160, 80, 255) : IM_COL32(14, 15, 18, 255), round, 0, selected ? 2.0f : 1.0f);
    std::string title = definition ? definition->title : node.type + " (unknown)";
    drawList->AddText(ImVec2(min.x + 10 * z, min.y + (titleHeight - ImGui::GetFontSize()) * 0.5f), IM_COL32(255, 255, 255, 255), title.c_str());

    // The title is the handle the node is dragged by: with the others that are selected.
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##title", ImVec2(nodeWidth, titleHeight));
    if (ImGui::IsItemActivated()) {
        if (ImGui::GetIO().KeyShift) {
            if (selected) selection_.erase(node.id);
            else selection_.insert(node.id);
        } else if (!selected) {
            selection_ = {node.id};
        }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        if ((delta.x != 0 || delta.y != 0) && isSelected(node.id)) {
            for (int id : selection_) {
                if (Node* moved = graph.find(id)) {
                    moved->x += delta.x / z;
                    moved->y += delta.y / z;
                }
            }
            changed = true;
        }
    }
    if (definition && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", definition->help.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (!selected) selection_ = {node.id};
        menuNode_ = node.id;
        ImGui::OpenPopup("##node-menu");
    }
    if (ImGui::BeginPopup("##node-menu")) {
        if (ImGui::MenuItem(selection_.size() > 1 ? "Delete Nodes" : "Delete Node")) {
            std::set<int> doomed = selection_;
            doomed.insert(node.id);
            ImGui::EndPopup();
            ImGui::PopID();
            for (int id : doomed) graph.removeNode(id);           // `node` is gone after this line
            selection_.clear();
            return true;
        }
        ImGui::EndPopup();
    }

    if (!definition) {
        ImGui::PopID();
        return changed;
    }

    // One pin: the dot on the edge, the wire that can be pulled from it, its menu.
    auto drawPin = [&](const PinDef& pin, bool isOutput, int row) {
        ImVec2 center(isOutput ? max.x : min.x, min.y + titleHeight + rowHeight * (float(row) + 0.5f) + 2 * z);
        pins_[key(node.id, pin.name, isOutput)] = PinPlace{center, pin.type};
        ImGui::PushID(isOutput ? 1 : 0);
        ImGui::PushID(pin.name.c_str());
        float hit = std::max(18.0f * z, 12.0f);
        ImGui::SetCursorScreenPos(ImVec2(center.x - hit * 0.5f, center.y - hit * 0.5f));
        ImGui::InvisibleButton("##pin", ImVec2(hit, hit));
        bool isHot = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (isHot) hovered_ = PinRef{node.id, pin.name, isOutput};
        if (ImGui::IsItemActivated()) dragFrom_ = PinRef{node.id, pin.name, isOutput};
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            menuPin_ = PinRef{node.id, pin.name, isOutput};
            ImGui::OpenPopup("##pin-menu");
        }
        if (ImGui::BeginPopup("##pin-menu")) {
            if (ImGui::MenuItem("Unplug")) {
                if (isOutput) graph.disconnectOutput(node.id, pin.name);
                else graph.disconnectInput(node.id, pin.name);
                changed = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();

        ImU32 color = pinColor(pin.type);
        bool isWired = isOutput ? graph.linkFrom(node.id, pin.name) != nullptr : graph.linkTo(node.id, pin.name) != nullptr;
        if (!isWired && isOutput) {
            for (const Link& link : graph.links) isWired = isWired || (link.fromNode == node.id && link.fromPin == pin.name);
        }
        float radius = (isHot ? kPinRadius + 2 : kPinRadius) * z;
        if (pin.type == PinType::Flow) {
            // Flow pins are arrows: they say "next".
            ImVec2 a(center.x - radius, center.y - radius), b(center.x + radius, center.y), c(center.x - radius, center.y + radius);
            if (isWired) drawList->AddTriangleFilled(a, b, c, color);
            else {
                drawList->AddTriangleFilled(a, b, c, IM_COL32(38, 41, 48, 255));
                drawList->AddTriangle(a, b, c, color, 1.5f);
            }
        } else {
            drawList->AddCircleFilled(center, radius, isWired ? color : IM_COL32(38, 41, 48, 255), 16);
            drawList->AddCircle(center, radius, color, 16, 1.5f);
        }
        return isWired;
    };

    float textOffset = (rowHeight - ImGui::GetFontSize()) * 0.5f;
    // Far out, the values are too small to change: they are only read.
    bool showsEditors = z >= 0.6f;
    for (size_t i = 0; i < inputCount; i++) {
        const PinDef& pin = definition->inputs[i];
        bool isWired = drawPin(pin, false, int(i));
        float rowTop = min.y + titleHeight + rowHeight * float(i) + 2 * z;
        std::string label = pin.type == PinType::Flow ? std::string() : pinLabel(pin.name);
        drawList->AddText(ImVec2(min.x + 12 * z, rowTop + textOffset), IM_COL32(200, 204, 212, 255), label.c_str());
        // An input nothing is wired to shows the value it will use, ready to be changed.
        if (pin.type != PinType::Flow && !isWired) {
            float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            float left = min.x + 12 * z + labelWidth + 8 * z;
            float width = std::max(max.x - 14 * z - left - (i < outputCount ? 50.0f * z : 0.0f), 36.0f * z);
            if (showsEditors) {
                ImGui::SetCursorScreenPos(ImVec2(left, rowTop + 2 * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * z, 2 * z));
                if (drawValueEditor(node, pin, width)) changed = true;
                ImGui::PopStyleVar();
            } else {
                auto stored = node.values.find(pin.name);
                std::string shown = stored != node.values.end() ? stored->second : pin.defaultValue.toText();
                drawList->AddText(ImVec2(left, rowTop + textOffset), IM_COL32(150, 156, 168, 255), shown.c_str());
            }
        }
    }
    for (size_t i = 0; i < outputCount; i++) {
        const PinDef& pin = definition->outputs[i];
        drawPin(pin, true, int(i));
        std::string label = pin.type == PinType::Flow && pin.name == "then" ? std::string() : pinLabel(pin.name);
        float rowTop = min.y + titleHeight + rowHeight * float(i) + 2 * z;
        float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
        drawList->AddText(ImVec2(max.x - 12 * z - labelWidth, rowTop + textOffset), IM_COL32(200, 204, 212, 255), label.c_str());
    }
    ImGui::PopID();
    return changed;
}

/// The menu that adds a node: a search over every kind there is, and the kinds by family
/// below it. Returns true if one was added.
bool GraphEditor::drawAddMenu(Graph& graph, ImVec2 at) {
    bool added = false;
    auto add = [&](const NodeDef& definition) {
        selection_ = {graph.addNode(definition.type, at.x, at.y).id};
        added = true;
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::IsWindowAppearing()) {
        search_.clear();
        searchChoice_ = 0;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##search", "Search: rotate, key, distance...", &search_);
    auto lowered = [](std::string text) {
        for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::string wanted = lowered(search_);
    if (!wanted.empty()) {
        // What matches, best first: by the title, then by the family or what it does.
        std::vector<const NodeDef*> found;
        for (const NodeDef& definition : NodeLibrary::instance().all()) {
            if (lowered(definition.title).find(wanted) != std::string::npos) found.push_back(&definition);
        }
        for (const NodeDef& definition : NodeLibrary::instance().all()) {
            if (lowered(definition.title).find(wanted) == std::string::npos &&
                (lowered(definition.category).find(wanted) != std::string::npos || lowered(definition.help).find(wanted) != std::string::npos)) {
                found.push_back(&definition);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) searchChoice_ = std::min(searchChoice_ + 1, int(found.size()) - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) searchChoice_ = std::max(searchChoice_ - 1, 0);
        searchChoice_ = std::clamp(searchChoice_, 0, std::max(int(found.size()) - 1, 0));
        ImGui::Separator();
        for (size_t i = 0; i < found.size() && i < 14; i++) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(categoryColor(found[i]->category) | IM_COL32(60, 60, 60, 0)));
            ImGui::Bullet();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Selectable((found[i]->title + "##" + found[i]->type).c_str(), int(i) == searchChoice_)) add(*found[i]);
            ImGui::SameLine(200);
            ImGui::TextDisabled("%s", found[i]->category.c_str());
        }
        if (found.empty()) ImGui::TextDisabled("No node is called that.");
        if (!found.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter)) add(*found[size_t(searchChoice_)]);
        return added;
    }
    ImGui::Separator();
    std::vector<std::string> categories;
    for (const NodeDef& definition : NodeLibrary::instance().all()) {
        if (std::find(categories.begin(), categories.end(), definition.category) == categories.end()) categories.push_back(definition.category);
    }
    for (const std::string& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (const NodeDef& definition : NodeLibrary::instance().all()) {
            if (definition.category != category) continue;
            if (ImGui::MenuItem(definition.title.c_str())) add(definition);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", definition.help.c_str());
        }
        ImGui::EndMenu();
    }
    return added;
}

bool GraphEditor::draw(Graph& graph, ImVec2 size) {
    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.08f, 0.095f, 1.0f));
    ImGui::BeginChild("##graph-board", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 boardMin = ImGui::GetWindowPos(), boardSize = ImGui::GetWindowSize();
    ImGuiIO& io = ImGui::GetIO();
    bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    bool isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput;

    // ---- the view: zoom about the pointer (Cmd or Ctrl and the wheel), or the middle for the buttons
    auto zoomAbout = [&](ImVec2 at, float factor) {
        float next = std::clamp(zoom_ * factor, 0.3f, 2.0f);
        ImVec2 onBoard((at.x - boardMin.x - scroll_.x) / zoom_, (at.y - boardMin.y - scroll_.y) / zoom_);
        zoom_ = next;
        scroll_ = ImVec2(at.x - boardMin.x - onBoard.x * zoom_, at.y - boardMin.y - onBoard.y * zoom_);
    };
    if (pendingZoom_ != 1.0f) {
        zoomAbout(ImVec2(boardMin.x + boardSize.x * 0.5f, boardMin.y + boardSize.y * 0.5f), pendingZoom_);
        pendingZoom_ = 1.0f;
    }
    if (isHovered && io.MouseWheel != 0 && (io.KeySuper || io.KeyCtrl)) {
        zoomAbout(io.MousePos, std::pow(1.12f, io.MouseWheel));
    } else if (isHovered && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
        scroll_.x += io.MouseWheelH * 30;
        scroll_.y += io.MouseWheel * 30;
    }
    if (isFocused && ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeySuper && !io.KeyCtrl) needsCentering_ = true;

    if (needsCentering_ && !graph.nodes.empty()) {
        // Every node in view, as large as fits, but never larger than life.
        float left = graph.nodes[0].x, top = graph.nodes[0].y, right = left, bottom = top;
        for (const Node& node : graph.nodes) {
            left = std::min(left, node.x);
            top = std::min(top, node.y);
            right = std::max(right, node.x + kNodeWidth);
            bottom = std::max(bottom, node.y + 140);
        }
        zoom_ = std::clamp(std::min((boardSize.x - 80) / std::max(right - left, 1.0f), (boardSize.y - 80) / std::max(bottom - top, 1.0f)), 0.3f, 1.0f);
        scroll_ = ImVec2(40 - left * zoom_, 40 - top * zoom_);
    }
    needsCentering_ = false;
    ImGui::SetWindowFontScale(zoom_);
    ImVec2 origin(boardMin.x + scroll_.x, boardMin.y + scroll_.y);

    // The grid: it moves with the nodes, which is what tells the eye the board is being dragged.
    const float step = 32 * zoom_;
    for (float x = std::fmod(scroll_.x, step); x < boardSize.x; x += step) {
        drawList->AddLine(ImVec2(boardMin.x + x, boardMin.y), ImVec2(boardMin.x + x, boardMin.y + boardSize.y), IM_COL32(255, 255, 255, 10));
    }
    for (float y = std::fmod(scroll_.y, step); y < boardSize.y; y += step) {
        drawList->AddLine(ImVec2(boardMin.x, boardMin.y + y), ImVec2(boardMin.x + boardSize.x, boardMin.y + y), IM_COL32(255, 255, 255, 10));
    }

    // The background takes what no node takes: a drag draws a box that selects, the middle
    // button moves the board, a click deselects.
    ImGui::SetCursorScreenPos(boardMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##background", ImVec2(std::max(boardSize.x, 1.0f), std::max(boardSize.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    bool isOverBackground = ImGui::IsItemHovered();
    ImVec2 pointerOnBoard((io.MousePos.x - origin.x) / zoom_, (io.MousePos.y - origin.y) / zoom_);
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        isBoxSelecting_ = true;
        boxStart_ = pointerOnBoard;
        if (!io.KeyShift) selection_.clear();
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        scroll_.x += io.MouseDelta.x;
        scroll_.y += io.MouseDelta.y;
    }
    bool wantsAddMenu = isOverBackground && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f)) {
        scroll_.x += io.MouseDelta.x;                   // the right button drags the board too
        scroll_.y += io.MouseDelta.y;
    }
    // Space opens the search where the pointer is.
    if (isHovered && isFocused && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsPopupOpen("##add-node")) wantsAddMenu = true;

    pins_.clear();
    hovered_ = PinRef{};
    // By id, not by reference: deleting a node while drawing the list would move the others.
    std::vector<int> ids;
    for (const Node& node : graph.nodes) ids.push_back(node.id);
    for (int id : ids) {
        Node* node = graph.find(id);
        if (!node) continue;
        if (drawNode(graph, *node, NodeLibrary::instance().find(node->type), origin, drawList)) changed = true;
    }

    // The wires, as curves that leave and arrive horizontally. Along each, dots run the way
    // it goes: brighter and quicker on the flow wires, which say what happens next.
    auto bezierAt = [](ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, float t) {
        float u = 1 - t;
        return ImVec2(u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                      u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y);
    };
    double now = ImGui::GetTime();
    auto drawWire = [&](ImVec2 from, ImVec2 to, ImU32 color, float thickness, bool flows) {
        float bend = std::max(std::abs(to.x - from.x) * 0.5f, 40.0f * zoom_);
        ImVec2 b(from.x + bend, from.y), c(to.x - bend, to.y);
        drawList->AddBezierCubic(from, b, c, to, color, thickness * std::max(zoom_, 0.6f));
        if (flows) {
            float length = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y)) + bend;
            int dots = std::max(1, int(length / (46.0f * zoom_)));
            float shift = float(std::fmod(now * (color == pinColor(PinType::Flow) ? 0.9 : 0.45), 1.0));
            for (int k = 0; k < dots; k++) {
                float t = std::fmod((float(k) + shift) / float(dots), 1.0f);
                drawList->AddCircleFilled(bezierAt(from, b, c, to, t), 2.6f * std::max(zoom_, 0.6f), (color & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 230), 8);
            }
        }
    };
    for (const Link& link : graph.links) {
        auto from = pins_.find(key(link.fromNode, link.fromPin, true));
        auto to = pins_.find(key(link.toNode, link.toPin, false));
        if (from == pins_.end() || to == pins_.end()) continue;
        bool isFlow = from->second.type == PinType::Flow;
        ImU32 color = pinColor(from->second.type);
        drawWire(from->second.position, to->second.position, isFlow ? color : (color & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 200), isFlow ? 3.0f : 2.2f, true);
    }

    // A wire being pulled follows the pointer; released over a pin of the other kind, it is plugged in.
    if (dragFrom_.isValid()) {
        auto start = pins_.find(key(dragFrom_.node, dragFrom_.pin, dragFrom_.isOutput));
        if (start == pins_.end()) {
            dragFrom_ = PinRef{};
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (dragFrom_.isOutput) drawWire(start->second.position, io.MousePos, pinColor(start->second.type), 2.5f, false);
            else drawWire(io.MousePos, start->second.position, pinColor(start->second.type), 2.5f, false);
        } else {
            if (hovered_.isValid() && hovered_.isOutput != dragFrom_.isOutput) {
                const PinRef& output = dragFrom_.isOutput ? dragFrom_ : hovered_;
                const PinRef& input = dragFrom_.isOutput ? hovered_ : dragFrom_;
                if (graph.connect(output.node, output.pin, input.node, input.pin)) changed = true;
            }
            dragFrom_ = PinRef{};
        }
    }

    // The box being drawn: what it touches is selected when it is let go.
    if (isBoxSelecting_) {
        ImVec2 a(origin.x + boxStart_.x * zoom_, origin.y + boxStart_.y * zoom_), b = io.MousePos;
        ImVec2 low(std::min(a.x, b.x), std::min(a.y, b.y)), high(std::max(a.x, b.x), std::max(a.y, b.y));
        drawList->AddRectFilled(low, high, IM_COL32(255, 160, 80, 30));
        drawList->AddRect(low, high, IM_COL32(255, 160, 80, 200));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            isBoxSelecting_ = false;
            if (high.x - low.x > 3 || high.y - low.y > 3) {
                for (const Node& node : graph.nodes) {
                    const NodeDef* definition = NodeLibrary::instance().find(node.type);
                    size_t rows = definition ? std::max<size_t>(std::max(definition->inputs.size(), definition->outputs.size()), 1) : 1;
                    ImVec2 nodeMin(origin.x + node.x * zoom_, origin.y + node.y * zoom_);
                    ImVec2 nodeMax(nodeMin.x + kNodeWidth * zoom_, nodeMin.y + (kTitleHeight + float(rows) * kRowHeight + 8) * zoom_);
                    if (nodeMax.x >= low.x && nodeMin.x <= high.x && nodeMax.y >= low.y && nodeMin.y <= high.y) selection_.insert(node.id);
                }
            }
        }
    }

    if (wantsAddMenu) {
        addAt_ = pointerOnBoard;
        ImGui::OpenPopup("##add-node");
    }
    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::BeginPopup("##add-node")) {
        if (drawAddMenu(graph, addAt_)) changed = true;
        ImGui::EndPopup();
    }

    if (isFocused) {
        if (!selection_.empty() && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            for (int id : selection_) graph.removeNode(id);
            selection_.clear();
            changed = true;
        }
        if ((io.KeySuper || io.KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            for (const Node& node : graph.nodes) selection_.insert(node.id);
        }
    }

    if (graph.nodes.empty()) {
        const char* hint = "Press Space, or right-click, to add a node. Start with an event: On Update.";
        ImVec2 hintSize = ImGui::CalcTextSize(hint);
        drawList->AddText(ImVec2(boardMin.x + (boardSize.x - hintSize.x) * 0.5f, boardMin.y + boardSize.y * 0.5f), IM_COL32(150, 156, 168, 255), hint);
    }
    ImGui::EndChild();
    return changed;
}

}  // namespace molten::editor
