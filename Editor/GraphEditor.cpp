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
    ImGui::PushID(node.id);
    size_t inputCount = definition ? definition->inputs.size() : 0, outputCount = definition ? definition->outputs.size() : 0;
    float rows = float(std::max<size_t>(std::max(inputCount, outputCount), 1));
    ImVec2 min(origin.x + node.x, origin.y + node.y);
    ImVec2 max(min.x + kNodeWidth, min.y + kTitleHeight + rows * kRowHeight + 8);

    // The body: a shadow, the box, and the title in the color of the node's family.
    bool isSelected = selected_ == node.id;
    drawList->AddRectFilled(ImVec2(min.x + 3, min.y + 4), ImVec2(max.x + 3, max.y + 4), IM_COL32(0, 0, 0, 90), 7.0f);
    drawList->AddRectFilled(min, max, IM_COL32(38, 41, 48, 245), 7.0f);
    ImU32 titleColor = definition ? categoryColor(definition->category) : IM_COL32(150, 40, 40, 255);
    drawList->AddRectFilled(min, ImVec2(max.x, min.y + kTitleHeight), titleColor, 7.0f, ImDrawFlags_RoundCornersTop);
    drawList->AddRect(min, max, isSelected ? IM_COL32(255, 160, 80, 255) : IM_COL32(14, 15, 18, 255), 7.0f, 0, isSelected ? 2.0f : 1.0f);
    std::string title = definition ? definition->title : node.type + " (unknown)";
    drawList->AddText(ImVec2(min.x + 10, min.y + (kTitleHeight - ImGui::GetFontSize()) * 0.5f), IM_COL32(255, 255, 255, 255), title.c_str());

    // The title is the handle the node is dragged by.
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##title", ImVec2(kNodeWidth, kTitleHeight));
    if (ImGui::IsItemActivated()) selected_ = node.id;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (delta.x != 0 || delta.y != 0) {
            node.x += delta.x;
            node.y += delta.y;
            changed = true;
        }
    }
    if (definition && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", definition->help.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        selected_ = node.id;
        menuNode_ = node.id;
        ImGui::OpenPopup("##node-menu");
    }
    if (ImGui::BeginPopup("##node-menu")) {
        if (ImGui::MenuItem("Delete Node")) {
            int id = node.id;
            ImGui::EndPopup();
            ImGui::PopID();
            graph.removeNode(id);           // `node` is gone after this line
            if (selected_ == id) selected_ = 0;
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
        ImVec2 center(isOutput ? max.x : min.x, min.y + kTitleHeight + kRowHeight * (float(row) + 0.5f) + 2);
        pins_[key(node.id, pin.name, isOutput)] = PinPlace{center, pin.type};
        ImGui::PushID(isOutput ? 1 : 0);
        ImGui::PushID(pin.name.c_str());
        ImGui::SetCursorScreenPos(ImVec2(center.x - 9, center.y - 9));
        ImGui::InvisibleButton("##pin", ImVec2(18, 18));
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
        float radius = isHot ? kPinRadius + 2 : kPinRadius;
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

    float textOffset = (kRowHeight - ImGui::GetFontSize()) * 0.5f;
    for (size_t i = 0; i < inputCount; i++) {
        const PinDef& pin = definition->inputs[i];
        bool isWired = drawPin(pin, false, int(i));
        float rowTop = min.y + kTitleHeight + kRowHeight * float(i) + 2;
        std::string label = pin.type == PinType::Flow ? std::string() : pinLabel(pin.name);
        drawList->AddText(ImVec2(min.x + 12, rowTop + textOffset), IM_COL32(200, 204, 212, 255), label.c_str());
        // An input nothing is wired to shows the value it will use, ready to be changed.
        if (pin.type != PinType::Flow && !isWired) {
            float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            float left = min.x + 12 + labelWidth + 8;
            float width = std::max(max.x - 14 - left - (i < outputCount ? 50.0f : 0.0f), 36.0f);
            ImGui::SetCursorScreenPos(ImVec2(left, rowTop + 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
            if (drawValueEditor(node, pin, width)) changed = true;
            ImGui::PopStyleVar();
        }
    }
    for (size_t i = 0; i < outputCount; i++) {
        const PinDef& pin = definition->outputs[i];
        drawPin(pin, true, int(i));
        std::string label = pin.type == PinType::Flow && pin.name == "then" ? std::string() : pinLabel(pin.name);
        float rowTop = min.y + kTitleHeight + kRowHeight * float(i) + 2;
        float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
        drawList->AddText(ImVec2(max.x - 12 - labelWidth, rowTop + textOffset), IM_COL32(200, 204, 212, 255), label.c_str());
    }
    ImGui::PopID();
    return changed;
}

/// The menu that adds a node: every kind there is, by family. Returns true if one was added.
bool GraphEditor::drawAddMenu(Graph& graph, ImVec2 at) {
    bool added = false;
    std::vector<std::string> categories;
    for (const NodeDef& definition : NodeLibrary::instance().all()) {
        if (std::find(categories.begin(), categories.end(), definition.category) == categories.end()) categories.push_back(definition.category);
    }
    for (const std::string& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (const NodeDef& definition : NodeLibrary::instance().all()) {
            if (definition.category != category) continue;
            if (ImGui::MenuItem(definition.title.c_str())) {
                selected_ = graph.addNode(definition.type, at.x, at.y).id;
                added = true;
            }
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

    if (needsCentering_ && !graph.nodes.empty()) {
        // Put the top-left node a little inside the board.
        float left = graph.nodes[0].x, top = graph.nodes[0].y;
        for (const Node& node : graph.nodes) {
            left = std::min(left, node.x);
            top = std::min(top, node.y);
        }
        scroll_ = ImVec2(40 - left, 40 - top);
    }
    needsCentering_ = false;
    ImVec2 origin(boardMin.x + scroll_.x, boardMin.y + scroll_.y);

    // The grid: it moves with the nodes, which is what tells the eye the board is being dragged.
    const float step = 32;
    for (float x = std::fmod(scroll_.x, step); x < boardSize.x; x += step) {
        drawList->AddLine(ImVec2(boardMin.x + x, boardMin.y), ImVec2(boardMin.x + x, boardMin.y + boardSize.y), IM_COL32(255, 255, 255, 10));
    }
    for (float y = std::fmod(scroll_.y, step); y < boardSize.y; y += step) {
        drawList->AddLine(ImVec2(boardMin.x, boardMin.y + y), ImVec2(boardMin.x + boardSize.x, boardMin.y + y), IM_COL32(255, 255, 255, 10));
    }

    // The background takes what no node takes: dragging moves the board, a click deselects.
    ImGui::SetCursorScreenPos(boardMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##background", ImVec2(std::max(boardSize.x, 1.0f), std::max(boardSize.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    bool isOverBackground = ImGui::IsItemHovered();
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        scroll_.x += io.MouseDelta.x;
        scroll_.y += io.MouseDelta.y;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) selected_ = 0;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
        scroll_.x += io.MouseWheelH * 30;
        scroll_.y += io.MouseWheel * 30;
    }
    bool wantsAddMenu = isOverBackground && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);

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

    // The wires, as curves that leave and arrive horizontally.
    auto drawWire = [&](ImVec2 from, ImVec2 to, ImU32 color, float thickness) {
        float bend = std::max(std::abs(to.x - from.x) * 0.5f, 40.0f);
        drawList->AddBezierCubic(from, ImVec2(from.x + bend, from.y), ImVec2(to.x - bend, to.y), to, color, thickness);
    };
    for (const Link& link : graph.links) {
        auto from = pins_.find(key(link.fromNode, link.fromPin, true));
        auto to = pins_.find(key(link.toNode, link.toPin, false));
        if (from == pins_.end() || to == pins_.end()) continue;
        drawWire(from->second.position, to->second.position, pinColor(from->second.type), from->second.type == PinType::Flow ? 3.0f : 2.2f);
    }

    // A wire being pulled follows the pointer; released over a pin of the other kind, it is plugged in.
    if (dragFrom_.isValid()) {
        auto start = pins_.find(key(dragFrom_.node, dragFrom_.pin, dragFrom_.isOutput));
        if (start == pins_.end()) {
            dragFrom_ = PinRef{};
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (dragFrom_.isOutput) drawWire(start->second.position, io.MousePos, pinColor(start->second.type), 2.5f);
            else drawWire(io.MousePos, start->second.position, pinColor(start->second.type), 2.5f);
        } else {
            if (hovered_.isValid() && hovered_.isOutput != dragFrom_.isOutput) {
                const PinRef& output = dragFrom_.isOutput ? dragFrom_ : hovered_;
                const PinRef& input = dragFrom_.isOutput ? hovered_ : dragFrom_;
                if (graph.connect(output.node, output.pin, input.node, input.pin)) changed = true;
            }
            dragFrom_ = PinRef{};
        }
    }

    if (wantsAddMenu) {
        addAt_ = ImVec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
        ImGui::OpenPopup("##add-node");
    }
    if (ImGui::BeginPopup("##add-node")) {
        ImGui::TextDisabled("Add a node");
        ImGui::Separator();
        if (drawAddMenu(graph, addAt_)) changed = true;
        ImGui::EndPopup();
    }

    if (selected_ != 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        graph.removeNode(selected_);
        selected_ = 0;
        changed = true;
    }

    if (graph.nodes.empty()) {
        const char* hint = "Right-click to add a node. Start with an event: On Update.";
        ImVec2 hintSize = ImGui::CalcTextSize(hint);
        drawList->AddText(ImVec2(boardMin.x + (boardSize.x - hintSize.x) * 0.5f, boardMin.y + boardSize.y * 0.5f), IM_COL32(150, 156, 168, 255), hint);
    }
    ImGui::EndChild();
    return changed;
}

}  // namespace molten::editor
