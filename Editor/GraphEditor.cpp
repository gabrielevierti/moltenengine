#include "GraphEditor.hpp"

#include "Script/CppBoxes.hpp"

#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace molten::editor {

using namespace script;

namespace {

const float kNodeWidth = 224;
const float kTitleHeight = 30;
const float kRowHeight = 26;
const float kPinRadius = 5.0f;
const float kGrid = 24;                         // the board's dots, and the step nodes settle on

const ImU32 kBoard = IM_COL32(21, 23, 28, 255);
const ImU32 kNodeBody = IM_COL32(32, 35, 42, 250);
const ImU32 kNodeEdge = IM_COL32(12, 13, 16, 255);
const ImU32 kAccent = IM_COL32(255, 140, 60, 255);
const ImU32 kLabel = IM_COL32(196, 201, 210, 255);

/// A color with its channels scaled, alpha kept.
ImU32 scaled(ImU32 color, float by) {
    auto channel = [&](int shift) { return ImU32(std::clamp(float((color >> shift) & 0xFF) * by, 0.0f, 255.0f)) << shift; };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | (color & IM_COL32_A_MASK);
}

ImU32 withAlpha(ImU32 color, int alpha) { return (color & ~IM_COL32_A_MASK) | (ImU32(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT); }

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

/// How large a node is on the board (before the zoom): the usual card with a row for each pin, or,
/// for a box that holds C++, as large as its code needs.
ImVec2 sizeOf(const Node& node, const NodeDef* definition) {
    size_t rows = definition ? std::max<size_t>(std::max(definition->inputs.size(), definition->outputs.size()), 1) : 1;
    ImVec2 size(kNodeWidth, kTitleHeight + float(rows) * kRowHeight + 8);
    float width = 0, height = 0;
    if (cppBoxSize(node, width, height)) {
        // Its ways out to the right ("true", "false", "body") have a row each and room for their names; "then" leaves below.
        size_t exits = 0;
        for (size_t i = 0; definition && i < definition->outputs.size(); i++) exits += definition->outputs[i].name != "then";
        size.x = std::max(size.x, width + (exits > 0 ? 54.0f : 0.0f));
        size.y = std::max(kTitleHeight + float(exits) * kRowHeight + 22, height);
    }
    return size;
}

ImU32 categoryColor(const std::string& category) {
    if (category == "C++") return IM_COL32(86, 156, 214, 255);
    if (category == "Events") return IM_COL32(222, 78, 64, 255);
    if (category == "Flow") return IM_COL32(150, 156, 170, 255);
    if (category == "Transform") return IM_COL32(236, 136, 52, 255);
    if (category == "Physics") return IM_COL32(72, 176, 112, 255);
    if (category == "Input") return IM_COL32(74, 140, 230, 255);
    if (category == "Math") return IM_COL32(112, 126, 150, 255);
    if (category == "Logic") return IM_COL32(168, 104, 214, 255);
    if (category == "Variables") return IM_COL32(52, 176, 186, 255);
    if (category == "Animation") return IM_COL32(230, 176, 60, 255);
    if (category == "Game") return IM_COL32(204, 98, 150, 255);
    return IM_COL32(150, 132, 92, 255);
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
        case PinType::String:
            // What it names, from a list (a sound, a scene, an entity, a key...); words only for words.
            if (!pin.pick.empty() && pickText) changed = pickText(pin, value.text, width);
            else changed = ImGui::InputText("##value", &value.text);
            break;
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
    const ImVec2 size = sizeOf(node, definition);
    const bool holdsCode = isCppBox(node.type) || node.type == "Code";
    const float nodeWidth = size.x * z, titleHeight = kTitleHeight * z, rowHeight = kRowHeight * z;
    ImGui::PushID(node.id);
    size_t inputCount = definition ? definition->inputs.size() : 0, outputCount = definition ? definition->outputs.size() : 0;
    ImVec2 min(origin.x + node.x * z, origin.y + node.y * z);
    ImVec2 max(min.x + nodeWidth, min.y + size.y * z);

    // The body: a soft shadow, the card, its header tinted with the color of the node's family
    // under a bright band of it, and the title. Selected: an outline of light round it.
    bool selected = isSelected(node.id);
    bool hovered = ImGui::IsMouseHoveringRect(min, max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    float round = 9.0f * z;
    for (int i = 3; i >= 1; i--) {
        float spread = float(i) * 3.0f * z;
        drawList->AddRectFilled(ImVec2(min.x - spread * 0.5f, min.y + spread * 0.6f), ImVec2(max.x + spread * 0.5f, max.y + spread * 1.3f),
                                IM_COL32(0, 0, 0, 34), round + spread);
    }
    ImU32 family = definition ? categoryColor(definition->category) : IM_COL32(180, 50, 50, 255);
    const bool isEvent = definition && definition->isEvent;
    drawList->AddRectFilled(min, max, kNodeBody, round);
    drawList->AddRectFilled(min, ImVec2(max.x, min.y + titleHeight), isEvent ? scaled(family, 0.62f) : scaled(family, 0.34f), round, ImDrawFlags_RoundCornersTop);
    drawList->AddRectFilled(min, ImVec2(max.x, min.y + 3.0f * z), family, round, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2(min.x, min.y + titleHeight), ImVec2(max.x, min.y + titleHeight), withAlpha(family, 90), 1.0f);
    if (selected) {
        drawList->AddRect(ImVec2(min.x - 3, min.y - 3), ImVec2(max.x + 3, max.y + 3), withAlpha(kAccent, 70), round + 3, 0, 4.0f);
        drawList->AddRect(min, max, kAccent, round, 0, 1.8f);
    } else {
        drawList->AddRect(min, max, hovered ? IM_COL32(92, 98, 112, 255) : kNodeEdge, round, 0, 1.0f);
    }
    // Its family, as a dot before the title; an event, as a little bolt: it starts things.
    ImVec2 mark(min.x + 14 * z, min.y + titleHeight * 0.5f + 1.0f * z);
    if (isEvent) {
        const ImVec2 bolt[6] = {ImVec2(mark.x + 1.5f * z, mark.y - 6 * z), ImVec2(mark.x - 3.5f * z, mark.y + 1 * z), ImVec2(mark.x, mark.y + 1 * z),
                                ImVec2(mark.x - 1.5f * z, mark.y + 6 * z), ImVec2(mark.x + 3.5f * z, mark.y - 1 * z), ImVec2(mark.x, mark.y - 1 * z)};
        drawList->AddConvexPolyFilled(bolt, 3, IM_COL32(255, 255, 255, 235));
        drawList->AddConvexPolyFilled(bolt + 3, 3, IM_COL32(255, 255, 255, 235));
    } else {
        drawList->AddCircleFilled(mark, 3.5f * z, family, 12);
    }
    std::string title = definition ? definition->title : node.type + " (unknown)";
    drawList->AddText(ImVec2(min.x + 25 * z, min.y + (titleHeight - ImGui::GetFontSize()) * 0.5f + 1.0f * z), IM_COL32(244, 245, 248, 255), title.c_str());
    // It has a comment: a small mark at the end of its title (the comment itself on the title's tooltip).
    if (!node.comment.empty() && node.type != "Code") {
        ImVec2 at(max.x - 30 * z, min.y + titleHeight * 0.5f);
        drawList->AddRectFilled(ImVec2(at.x - 6 * z, at.y - 5 * z), ImVec2(at.x + 6 * z, at.y + 3 * z), IM_COL32(150, 200, 140, 230), 2.5f * z);
        drawList->AddTriangleFilled(ImVec2(at.x - 3 * z, at.y + 3 * z), ImVec2(at.x + 1 * z, at.y + 3 * z), ImVec2(at.x - 4 * z, at.y + 7 * z), IM_COL32(150, 200, 140, 230));
    }
    // What went wrong at it: a red ring, and the words under it (with the pin, if it was one).
    if (problems) {
        float below = max.y + 6 * z;
        for (const script::ScriptProblem& problem : *problems) {
            if (problem.node != node.id) continue;
            drawList->AddRect(ImVec2(min.x - 3, min.y - 3), ImVec2(max.x + 3, max.y + 3), IM_COL32(240, 80, 70, 230), round + 3, 0, 2.5f);
            std::string words = (problem.pin.empty() ? std::string() : pinLabel(problem.pin) + ": ") + problem.message;
            drawList->AddText(nullptr, ImGui::GetFontSize(), ImVec2(min.x, below), IM_COL32(255, 120, 110, 255), words.c_str(), nullptr, std::max(nodeWidth, 260.0f * z));
            below += ImGui::CalcTextSize(words.c_str(), nullptr, false, std::max(nodeWidth, 260.0f * z)).y + 2 * z;
            problemPins_.insert(key(node.id, problem.pin, false));
        }
    }
    if (debug) {
        // Ran just now: lit, fading over half a second. Held before it: a thick yellow ring.
        if (debug->ran) {
            auto ran = debug->ran->find(node.id);
            if (ran != debug->ran->end()) {
                float fade = 1.0f - float(std::clamp((debug->now - ran->second) / 0.6, 0.0, 1.0));
                if (fade > 0) {
                    drawList->AddRect(ImVec2(min.x - 3, min.y - 3), ImVec2(max.x + 3, max.y + 3), IM_COL32(110, 255, 160, int(90 * fade)), round + 3, 0, 6.0f);
                    drawList->AddRect(min, max, IM_COL32(120, 255, 170, int(230 * fade)), round, 0, 2.0f);
                }
            }
        }
        if (debug->stoppedAt == node.id) drawList->AddRect(ImVec2(min.x - 4, min.y - 4), ImVec2(max.x + 4, max.y + 4), IM_COL32(255, 220, 60, 255), round + 4, 0, 4.0f);
        if (debug->hasBreakpoint && debug->hasBreakpoint(node.id)) {
            drawList->AddCircleFilled(ImVec2(max.x - 12 * z, min.y + titleHeight * 0.5f), 5.5f * z, IM_COL32(235, 60, 60, 255), 16);
            drawList->AddCircle(ImVec2(max.x - 12 * z, min.y + titleHeight * 0.5f), 5.5f * z, IM_COL32(20, 0, 0, 255), 16, 1.2f);
        }
    }
    if (ImGui::IsMouseHoveringRect(min, max)) hoveredNode_ = node.id;

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
    // Let go: the moved nodes settle on the board's dots, so that rows of them line up.
    if (ImGui::IsItemDeactivated() && isSelected(node.id)) {
        for (int id : selection_) {
            if (Node* moved = graph.find(id)) {
                moved->x = std::round(moved->x / (kGrid * 0.5f)) * (kGrid * 0.5f);
                moved->y = std::round(moved->y / (kGrid * 0.5f)) * (kGrid * 0.5f);
            }
        }
        changed = true;
    }
    // On the title: its comment (what it is for, written by whoever made it) above what the box does.
    if (definition && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        if (!node.comment.empty()) {
            ImGui::TextUnformatted(node.comment.c_str());
            ImGui::Separator();
        }
        ImGui::TextDisabled("%s", definition->help.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (!selected) selection_ = {node.id};
        menuNode_ = node.id;
        ImGui::OpenPopup("##node-menu");
    }
    // F9 on the selected node: a breakpoint there, or none.
    if (debug && debug->toggleBreakpoint && selection_.size() == 1 && *selection_.begin() == node.id && ImGui::IsKeyPressed(ImGuiKey_F9, false) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        debug->toggleBreakpoint(node.id);
    }
    if (ImGui::BeginPopup("##node-menu")) {
        if (debug && debug->toggleBreakpoint && definition && definition->execute) {
            bool has = debug->hasBreakpoint && debug->hasBreakpoint(node.id);
            if (ImGui::MenuItem(has ? "Remove Breakpoint" : "Add Breakpoint", "F9")) debug->toggleBreakpoint(node.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("While the game runs, it is held when the flow comes to this node, before it runs");
            ImGui::Separator();
        }
        if (ImGui::MenuItem(node.comment.empty() ? "Add Comment..." : "Edit Comment...")) commentFor_ = node.id;
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
        // A box of code is read downward: it is come to at its top left, and goes on ("then") from its bottom left,
        // to the box under it. Its branches leave to the right, like any node's.
        bool leavesLeft = false;
        if (holdsCode && pin.type == PinType::Flow) {
            if (!isOutput) center = ImVec2(min.x, min.y + titleHeight * 0.5f);
            else if (pin.name == "then") center = ImVec2(min.x, max.y - 10 * z), leavesLeft = true;
        }
        pins_[key(node.id, pin.name, isOutput)] = PinPlace{center, pin.type, leavesLeft};
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
        // While a wire is pulled: the pins it can be plugged into light up, the others fade.
        bool takesDrag = false, refusesDrag = false;
        if (dragFrom_.isValid() && dragFrom_.node != node.id) {
            auto start = pins_.find(key(dragFrom_.node, dragFrom_.pin, dragFrom_.isOutput));
            bool fits = start != pins_.end() && dragFrom_.isOutput != isOutput &&
                        (start->second.type == PinType::Flow) == (pin.type == PinType::Flow);
            takesDrag = fits;
            refusesDrag = !fits;
        }
        float radius = (isHot || takesDrag ? kPinRadius + 1.5f : kPinRadius) * z;
        if (refusesDrag) color = withAlpha(color, 70);
        if (takesDrag) drawList->AddCircleFilled(center, radius + 4.0f * z, withAlpha(color, 60), 20);
        if (pin.type == PinType::Flow) {
            // Flow pins are arrows: they say "next". (Downward, where next is the box below.)
            ImVec2 a(center.x - radius * 0.8f, center.y - radius), b(center.x + radius, center.y), c(center.x - radius * 0.8f, center.y + radius);
            if (leavesLeft) a = ImVec2(center.x - radius, center.y - radius * 0.8f), b = ImVec2(center.x + radius, center.y - radius * 0.8f), c = ImVec2(center.x, center.y + radius);
            if (isWired) drawList->AddTriangleFilled(a, b, c, color);
            else {
                drawList->AddTriangleFilled(a, b, c, kNodeBody);
                drawList->AddTriangle(a, b, c, color, 1.5f);
            }
        } else {
            drawList->AddCircleFilled(center, radius, isWired ? color : kNodeBody, 16);
            drawList->AddCircle(center, radius, color, 16, 1.6f);
        }
        return isWired;
    };

    float textOffset = (rowHeight - ImGui::GetFontSize()) * 0.5f;
    // Far out, the values are too small to change: they are only read.
    bool showsEditors = z >= 0.6f;
    if (holdsCode && ImGui::IsRectVisible(min, max)) {
        // The code it holds, as it is read, under the comment written above it in the file.
        const float lineHeight = 17.0f * z, right = max.x - (outputCount > 1 ? 54.0f : 12.0f) * z;
        ImVec2 at(min.x + 14 * z, min.y + titleHeight + 7 * z);
        drawList->PushClipRect(ImVec2(min.x, min.y + titleHeight), ImVec2(right, max.y - 2 * z), true);
        int shown = 0;
        auto lines = [&](const std::string& words, ImU32 color, const char* before) {
            size_t start = 0;
            while (start <= words.size() && shown < 40) {
                size_t end = words.find('\n', start);
                if (end == std::string::npos) end = words.size();
                std::string line = std::string(before) + words.substr(start, end - start);
                drawList->AddText(ImVec2(at.x, at.y + lineHeight * float(shown)), color, shown == 39 && end < words.size() ? "..." : line.c_str());
                shown++;
                start = end + 1;
            }
        };
        std::string comment = cppBoxComment(node), code = cppBoxText(node);
        if (!comment.empty()) lines(comment, IM_COL32(118, 162, 116, 255), "// ");
        if (!code.empty()) lines(code, node.type == "Code" ? IM_COL32(244, 204, 140, 255) : IM_COL32(226, 229, 236, 255), "");
        else if (comment.empty()) lines(node.type == "CppFile" ? "(a line end)" : "(nothing yet: double-click to write it)", IM_COL32(120, 126, 138, 255), "");
        drawList->PopClipRect();
        // Double-clicked: its code in a window of its own, to change.
        ImGui::SetCursorScreenPos(ImVec2(min.x + 10 * z, min.y + titleHeight));
        ImGui::InvisibleButton("##code", ImVec2(std::max(right - min.x - 10 * z, 8.0f), std::max(max.y - min.y - titleHeight, 8.0f)));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selection_ = {node.id};
            ImGui::OpenPopup("##code-editor");
        } else if (ImGui::IsItemActivated() && !ImGui::GetIO().KeyShift && !selected) {
            selection_ = {node.id};
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsPopupOpen("##code-editor")) {
            ImGui::SetTooltip("%s", node.type == "Code" ? "A line of C++ no box says yet: it runs when the script is converted to C++, not as a visual script.\n"
                                                         "Double-click to change it, or put boxes in its place."
                                                       : "Double-click to change the code");
        }
        ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##code-editor")) {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled("%s", node.type == "CppIf" ? "if ( ... ): what has to hold" : node.type == "CppBlock" ? "The line that opens the block: for (...), while (...), each([&](Thing& thing)"
                                      : node.type == "CppFunction" ? "The function's first line" : "C++, as it is written in the file");
            std::string& text = node.values["code"];
            int rows = 1;
            for (char c : text) rows += c == '\n';
            if (ImGui::InputTextMultiline("##text", &text, ImVec2(700, float(std::clamp(rows + 2, 4, 30)) * ImGui::GetTextLineHeight() + 8), ImGuiInputTextFlags_AllowTabInput)) changed = true;
            if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    for (size_t i = 0; i < inputCount; i++) {
        const PinDef& pin = definition->inputs[i];
        if (holdsCode && pin.type != PinType::Flow) continue;          // its code is drawn above, not as a row
        bool isWired = drawPin(pin, false, int(i));
        float rowTop = min.y + titleHeight + rowHeight * float(i) + 2 * z;
        std::string label = pin.type == PinType::Flow ? std::string() : pinLabel(pin.name);
        bool isWrong = problemPins_.count(key(node.id, pin.name, false)) > 0;
        drawList->AddText(ImVec2(min.x + 14 * z, rowTop + textOffset), isWrong ? IM_COL32(255, 110, 100, 255) : kLabel, label.c_str());
        // An input nothing is wired to shows the value it will use, ready to be changed.
        if (pin.type != PinType::Flow && !isWired) {
            float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            float left = min.x + 14 * z + labelWidth + 8 * z;
            float width = std::max(max.x - 14 * z - left - (i < outputCount ? 50.0f * z : 0.0f), 36.0f * z);
            if (showsEditors) {
                ImGui::SetCursorScreenPos(ImVec2(left, rowTop + 2 * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * z, 2 * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 22, 27, 255));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(28, 31, 38, 255));
                if (drawValueEditor(node, pin, width)) changed = true;
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            } else {
                auto stored = node.values.find(pin.name);
                std::string shown = stored != node.values.end() ? stored->second : pin.defaultValue.toText();
                drawList->AddText(ImVec2(left, rowTop + textOffset), IM_COL32(150, 156, 168, 255), shown.c_str());
            }
        }
    }
    int outputRow = 0;
    for (size_t i = 0; i < outputCount; i++) {
        const PinDef& pin = definition->outputs[i];
        // (A box of code's "then" is at its foot, not in a row.)
        int row = holdsCode && pin.name == "then" ? 0 : outputRow++;
        drawPin(pin, true, row);
        std::string label = pin.type == PinType::Flow && pin.name == "then" ? std::string() : pinLabel(pin.name);
        float rowTop = min.y + titleHeight + rowHeight * float(row) + 2 * z;
        float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
        drawList->AddText(ImVec2(max.x - 14 * z - labelWidth, rowTop + textOffset), kLabel, label.c_str());
    }
    ImGui::PopID();
    return changed;
}

/// The menu that adds a node: a search over every kind there is, and the kinds by family
/// below it. Returns true if one was added.
bool GraphEditor::drawAddMenu(Graph& graph, ImVec2 at) {
    bool added = false;
    auto add = [&](const NodeDef& definition) {
        int id = graph.addNode(definition.type, at.x, at.y).id;
        selection_ = {id};
        // Made at the end of a wire let go on the board: plugged into it, by the first pin that fits.
        if (pendingConnect_.isValid()) {
            const PinDef* fromPin = nullptr;
            if (const Node* from = graph.find(pendingConnect_.node)) {
                if (const NodeDef* fromKind = graph.kinds().find(from->type)) {
                    fromPin = pendingConnect_.isOutput ? fromKind->findOutput(pendingConnect_.pin) : fromKind->findInput(pendingConnect_.pin);
                }
            }
            if (fromPin) {
                const std::vector<PinDef>& others = pendingConnect_.isOutput ? definition.inputs : definition.outputs;
                // The same kind first, then any that fits (flow with flow, value with value).
                const PinDef* match = nullptr;
                for (const PinDef& pin : others) {
                    if (pin.type == fromPin->type) {
                        match = &pin;
                        break;
                    }
                }
                for (const PinDef& pin : others) {
                    if (match) break;
                    if ((pin.type == PinType::Flow) == (fromPin->type == PinType::Flow)) match = &pin;
                }
                if (match) {
                    if (pendingConnect_.isOutput) graph.connect(pendingConnect_.node, pendingConnect_.pin, id, match->name);
                    else graph.connect(id, match->name, pendingConnect_.node, pendingConnect_.pin);
                }
            }
            pendingConnect_ = PinRef{};
        }
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
        for (const NodeDef& definition : graph.kinds().all()) {
            if (lowered(definition.title).find(wanted) != std::string::npos) found.push_back(&definition);
        }
        for (const NodeDef& definition : graph.kinds().all()) {
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
    for (const NodeDef& definition : graph.kinds().all()) {
        if (std::find(categories.begin(), categories.end(), definition.category) == categories.end()) categories.push_back(definition.category);
    }
    for (const std::string& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (const NodeDef& definition : graph.kinds().all()) {
            if (definition.category != category) continue;
            if (ImGui::MenuItem(definition.title.c_str())) add(definition);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", definition.help.c_str());
        }
        ImGui::EndMenu();
    }
    return added;
}

bool GraphEditor::draw(Graph& graph, ImVec2 size) {
    problemPins_.clear();
    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kBoard));
    ImGui::BeginChild("##graph-board", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 boardMin = ImGui::GetWindowPos(), boardSize = ImGui::GetWindowSize();
    ImGuiIO& io = ImGui::GetIO();
    bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    bool isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput;

    // ---- the view. Zoom about the pointer (Cmd or Ctrl and the wheel, or the - and + buttons about
    // the middle) and the wheel's scrolling are asked for; the view then glides there.
    auto zoomAbout = [&](ImVec2 at, float factor) {
        float next = std::clamp(targetZoom_ * factor, 0.25f, 2.0f);
        zoomScreen_ = at;
        zoomBoard_ = ImVec2((at.x - boardMin.x - scroll_.x) / zoom_, (at.y - boardMin.y - scroll_.y) / zoom_);
        zoomsAboutPoint_ = true;
        targetZoom_ = next;
    };
    if (pendingZoom_ != 1.0f) {
        zoomAbout(ImVec2(boardMin.x + boardSize.x * 0.5f, boardMin.y + boardSize.y * 0.5f), pendingZoom_);
        pendingZoom_ = 1.0f;
    }
    if (isHovered && io.MouseWheel != 0 && (io.KeySuper || io.KeyCtrl)) {
        zoomAbout(io.MousePos, std::pow(1.15f, io.MouseWheel));
    } else if (isHovered && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
        zoomsAboutPoint_ = false;
        targetScroll_.x += io.MouseWheelH * 40;
        targetScroll_.y += io.MouseWheel * 40;
    }
    if (isFocused && ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeySuper && !io.KeyCtrl) needsCentering_ = true;

    if (needsCentering_ && !graph.nodes.empty()) {
        // Every node in view, as large as fits, but never larger than life.
        float left = graph.nodes[0].x, top = graph.nodes[0].y, right = left, bottom = top;
        for (const Node& node : graph.nodes) {
            left = std::min(left, node.x);
            top = std::min(top, node.y);
            ImVec2 size = sizeOf(node, graph.kinds().find(node.type));
            right = std::max(right, node.x + size.x);
            bottom = std::max(bottom, node.y + size.y + 40);
        }
        targetZoom_ = std::clamp(std::min((boardSize.x - 100) / std::max(right - left, 1.0f), (boardSize.y - 100) / std::max(bottom - top, 1.0f)), 0.25f, 1.0f);
        targetScroll_ = ImVec2((boardSize.x - (right - left) * targetZoom_) * 0.5f - left * targetZoom_,
                               (boardSize.y - (bottom - top) * targetZoom_) * 0.5f - top * targetZoom_);
        zoomsAboutPoint_ = false;
    }
    needsCentering_ = false;
    if (snapsView_) {
        zoom_ = targetZoom_;
        scroll_ = targetScroll_;
        snapsView_ = false;
    }
    {
        float ease = 1.0f - std::exp(-std::min(io.DeltaTime, 0.1f) * 16.0f);
        zoom_ += (targetZoom_ - zoom_) * ease;
        if (std::abs(targetZoom_ - zoom_) < 0.0005f) zoom_ = targetZoom_;
        if (zoomsAboutPoint_) {
            // The point under the pointer stays under it all the way.
            scroll_ = ImVec2(zoomScreen_.x - boardMin.x - zoomBoard_.x * zoom_, zoomScreen_.y - boardMin.y - zoomBoard_.y * zoom_);
            targetScroll_ = scroll_;
            if (zoom_ == targetZoom_) zoomsAboutPoint_ = false;
        } else {
            scroll_.x += (targetScroll_.x - scroll_.x) * ease;
            scroll_.y += (targetScroll_.y - scroll_.y) * ease;
        }
    }
    ImGui::SetWindowFontScale(zoom_);
    ImVec2 origin(boardMin.x + scroll_.x, boardMin.y + scroll_.y);

    // The board: dots on a grid, and every fifth line faintly, moving with the nodes, which is what
    // tells the eye the board is being dragged.
    {
        const float step = kGrid * zoom_;
        if (step >= 7.0f) {
            float dot = std::max(1.0f, 1.1f * zoom_);
            for (float x = std::fmod(scroll_.x, step); x < boardSize.x; x += step) {
                for (float y = std::fmod(scroll_.y, step); y < boardSize.y; y += step) {
                    drawList->AddRectFilled(ImVec2(boardMin.x + x - dot * 0.5f, boardMin.y + y - dot * 0.5f), ImVec2(boardMin.x + x + dot * 0.5f, boardMin.y + y + dot * 0.5f),
                                            IM_COL32(255, 255, 255, 22));
                }
            }
        }
        const float major = step * 5;
        for (float x = std::fmod(scroll_.x, major); x < boardSize.x; x += major) {
            drawList->AddLine(ImVec2(boardMin.x + x, boardMin.y), ImVec2(boardMin.x + x, boardMin.y + boardSize.y), IM_COL32(255, 255, 255, 9));
        }
        for (float y = std::fmod(scroll_.y, major); y < boardSize.y; y += major) {
            drawList->AddLine(ImVec2(boardMin.x, boardMin.y + y), ImVec2(boardMin.x + boardSize.x, boardMin.y + y), IM_COL32(255, 255, 255, 9));
        }
    }

    // The background takes what no node takes: a drag draws a box that selects, the middle
    // and right buttons move the board, a click deselects.
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
    auto dragBoard = [&]() {
        scroll_.x += io.MouseDelta.x;
        scroll_.y += io.MouseDelta.y;
        targetScroll_ = scroll_;
        zoomsAboutPoint_ = false;
    };
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) dragBoard();
    bool wantsAddMenu = isOverBackground && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f)) dragBoard();
    // Space opens the search where the pointer is.
    if (isHovered && isFocused && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsPopupOpen("##add-node")) wantsAddMenu = true;

    hoveredNode_ = 0;
    pins_.clear();
    hovered_ = PinRef{};
    // By id, not by reference: deleting a node while drawing the list would move the others.
    std::vector<int> ids;
    for (const Node& node : graph.nodes) ids.push_back(node.id);
    for (int id : ids) {
        Node* node = graph.find(id);
        if (!node) continue;
        if (drawNode(graph, *node, graph.kinds().find(node->type), origin, drawList)) changed = true;
    }

    // The wires, as curves that leave and arrive horizontally. Along each, dots run the way
    // it goes: brighter and quicker on the flow wires, which say what happens next.
    auto bezierAt = [](ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, float t) {
        float u = 1 - t;
        return ImVec2(u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                      u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y);
    };
    double now = ImGui::GetTime();
    // A wire: a soft glow under a clean curve. Light runs along it only while the script runs and
    // the node it comes from has just run: it shows what is happening, not decoration.
    auto drawWire = [&](ImVec2 from, ImVec2 to, ImU32 color, float thickness, bool flows, bool leavesLeft = false) {
        float bend = std::max(std::abs(to.x - from.x) * 0.5f, 50.0f * zoom_);
        ImVec2 b(from.x + bend, from.y), c(to.x - bend, to.y);
        if (leavesLeft) {
            // From the foot of one box of code to the head of the next: a bracket down their left side.
            bend = std::clamp(std::abs(to.y - from.y) * 0.5f, 14.0f * zoom_, 46.0f * zoom_) + std::max(from.x - to.x, 0.0f) * 0.5f;
            b = ImVec2(from.x - bend, from.y);
        }
        float width = thickness * std::max(zoom_, 0.6f);
        drawList->AddBezierCubic(from, b, c, to, withAlpha(color, 38), width * 3.2f);
        drawList->AddBezierCubic(from, b, c, to, color, width);
        if (flows) {
            float length = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y)) + bend;
            int dots = std::max(1, int(length / (60.0f * zoom_)));
            float shift = float(std::fmod(now * 1.1, 1.0));
            for (int k = 0; k < dots; k++) {
                float t = std::fmod((float(k) + shift) / float(dots), 1.0f);
                ImVec2 at = bezierAt(from, b, c, to, t);
                drawList->AddCircleFilled(at, 4.5f * std::max(zoom_, 0.6f), withAlpha(color, 60), 10);
                drawList->AddCircleFilled(at, 2.4f * std::max(zoom_, 0.6f), IM_COL32(255, 255, 255, 235), 8);
            }
        }
    };
    auto hasJustRun = [&](int node) {
        if (!debug || !debug->ran) return false;
        auto ran = debug->ran->find(node);
        return ran != debug->ran->end() && debug->now - ran->second < 0.6;
    };
    for (const Link& link : graph.links) {
        auto from = pins_.find(key(link.fromNode, link.fromPin, true));
        auto to = pins_.find(key(link.toNode, link.toPin, false));
        if (from == pins_.end() || to == pins_.end()) continue;
        bool isFlow = from->second.type == PinType::Flow;
        ImU32 color = pinColor(from->second.type);
        bool touchesSelection = isSelected(link.fromNode) || isSelected(link.toNode);
        if (!isFlow) color = withAlpha(color, touchesSelection ? 255 : 200);
        drawWire(from->second.position, to->second.position, color, isFlow ? 2.8f : (touchesSelection ? 2.6f : 2.0f), hasJustRun(link.fromNode), from->second.leavesLeft);
        // What went along it last, while the game runs: a label half way.
        if (!isFlow && debug && debug->values) {
            auto seen = debug->values->find({link.fromNode, link.fromPin});
            if (seen != debug->values->end()) {
                std::string text = seen->second;
                // Numbers shortened: "3.14159274" is "3.142".
                std::string shortened, word;
                std::istringstream words(text);
                while (words >> word) {
                    char* end = nullptr;
                    double number = std::strtod(word.c_str(), &end);
                    if (end && *end == 0 && end != word.c_str()) {
                        char buffer[32];
                        std::snprintf(buffer, sizeof(buffer), "%.3g", number);
                        word = buffer;
                    }
                    shortened += (shortened.empty() ? "" : " ") + word;
                }
                if (!shortened.empty() || text.empty()) text = shortened;
                if (text.size() > 28) text = text.substr(0, 27) + "\xE2\x80\xA6";
                if (text.empty()) text = "\"\"";
                ImVec2 a = from->second.position, d = to->second.position;
                float bend = std::max(std::abs(d.x - a.x) * 0.5f, 40.0f * zoom_);
                ImVec2 middle = bezierAt(a, ImVec2(a.x + bend, a.y), ImVec2(d.x - bend, d.y), d, 0.5f);
                ImVec2 size = ImGui::CalcTextSize(text.c_str());
                ImVec2 low(middle.x - size.x * 0.5f - 4, middle.y - size.y * 0.5f - 2), high(middle.x + size.x * 0.5f + 4, middle.y + size.y * 0.5f + 2);
                drawList->AddRectFilled(low, high, IM_COL32(18, 20, 24, 235), 4);
                drawList->AddRect(low, high, color, 4);
                drawList->AddText(ImVec2(low.x + 4, low.y + 2), IM_COL32(235, 235, 235, 255), text.c_str());
            }
        }
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
            } else if (!hovered_.isValid() && hoveredNode_ == 0 && isHovered) {
                // Let go on the board: what to put there, plugged into this wire.
                pendingConnect_ = dragFrom_;
                wantsAddMenu = true;
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
                    ImVec2 size = sizeOf(node, graph.kinds().find(node.type));
                    ImVec2 nodeMin(origin.x + node.x * zoom_, origin.y + node.y * zoom_);
                    ImVec2 nodeMax(nodeMin.x + size.x * zoom_, nodeMin.y + size.y * zoom_);
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
    } else if (!wantsAddMenu) {
        pendingConnect_ = PinRef{};                 // closed without a node: the wire it was for is gone
    }

    // A node's comment, written: shown on its title, and above its line in C++.
    if (commentFor_ != 0) {
        ImGui::OpenPopup("##comment-editor");
        commentId_ = commentFor_;
        commentFor_ = 0;
    }
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##comment-editor")) {
        if (Node* commented = graph.find(commentId_)) {
            ImGui::TextDisabled("What this box is for: shown when the pointer is on it, and a comment above its line in C++.");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputTextMultiline("##comment", &commented->comment, ImVec2(440, ImGui::GetTextLineHeight() * 5 + 8))) changed = true;
            if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        } else {
            ImGui::CloseCurrentPopup();
        }
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

    // The whole script, small, in a corner: where the view is in it, and a click goes there.
    if (graph.nodes.size() >= 4 && boardSize.x > 420 && boardSize.y > 300) {
        float left = graph.nodes[0].x, top = graph.nodes[0].y, right = left, bottom = top;
        for (const Node& node : graph.nodes) {
            left = std::min(left, node.x);
            top = std::min(top, node.y);
            ImVec2 size = sizeOf(node, graph.kinds().find(node.type));
            right = std::max(right, node.x + size.x);
            bottom = std::max(bottom, node.y + size.y);
        }
        // The view's own rectangle on the board counts too, so that it is always on the map.
        float viewLeft = -scroll_.x / zoom_, viewTop = -scroll_.y / zoom_;
        float viewRight = viewLeft + boardSize.x / zoom_, viewBottom = viewTop + boardSize.y / zoom_;
        float spanLeft = std::min(left, viewLeft) - 40, spanTop = std::min(top, viewTop) - 40;
        float spanRight = std::max(right, viewRight) + 40, spanBottom = std::max(bottom, viewBottom) + 40;
        const ImVec2 mapSize(180, 118);
        ImVec2 mapMin(boardMin.x + boardSize.x - mapSize.x - 12, boardMin.y + boardSize.y - mapSize.y - 12);
        ImVec2 mapMax(mapMin.x + mapSize.x, mapMin.y + mapSize.y);
        float scale = std::min(mapSize.x / (spanRight - spanLeft), mapSize.y / (spanBottom - spanTop));
        auto onMap = [&](float x, float y) { return ImVec2(mapMin.x + (x - spanLeft) * scale, mapMin.y + (y - spanTop) * scale); };
        drawList->AddRectFilled(mapMin, mapMax, IM_COL32(14, 15, 19, 225), 6);
        drawList->AddRect(mapMin, mapMax, IM_COL32(255, 255, 255, 30), 6);
        for (const Node& node : graph.nodes) {
            const NodeDef* kind = graph.kinds().find(node.type);
            ImU32 family = kind ? categoryColor(kind->category) : IM_COL32(150, 50, 50, 255);
            ImVec2 size = sizeOf(node, kind);
            drawList->AddRectFilled(onMap(node.x, node.y), onMap(node.x + size.x, node.y + size.y), isSelected(node.id) ? kAccent : withAlpha(family, 200), 2);
        }
        drawList->AddRect(onMap(viewLeft, viewTop), onMap(viewRight, viewBottom), IM_COL32(255, 255, 255, 170), 2, 0, 1.2f);
        ImGui::SetCursorScreenPos(mapMin);
        ImGui::InvisibleButton("##minimap", mapSize);
        if (ImGui::IsItemActive()) {
            // Where it is clicked comes to the middle of the view.
            float x = spanLeft + (io.MousePos.x - mapMin.x) / scale, y = spanTop + (io.MousePos.y - mapMin.y) / scale;
            targetScroll_ = ImVec2(boardSize.x * 0.5f - x * zoom_, boardSize.y * 0.5f - y * zoom_);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) scroll_ = targetScroll_;
            zoomsAboutPoint_ = false;
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
