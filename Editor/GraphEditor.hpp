// GraphEditor.hpp
// The drawing board of a visual script: nodes to drag, pins to wire, values to type in.
// It knows nothing about the rest of the editor: it edits a script::Graph with Dear ImGui.
//
//   drag a node by its title          right-click the background    add a node
//   drag from a pin to another        right-click a node            delete it
//   scroll, or drag the background    right-click a pin             unplug it
//   Delete or Backspace               removes the selected node
#pragma once

#include "Script/Graph.hpp"

#include <imgui.h>

#include <map>
#include <string>

namespace molten::editor {

class GraphEditor {
public:
    /// Draws the script in a region of the current window and lets the user edit it.
    /// Returns true if the script changed (as opposed to the view of it).
    bool draw(script::Graph& graph, ImVec2 size);

    /// Brings the nodes into view: after opening another script.
    void resetView() { needsCentering_ = true; }
    int selectedNode() const { return selected_; }

private:
    struct PinPlace {
        ImVec2 position;
        script::PinType type = script::PinType::Float;
    };
    struct PinRef {
        int node = 0;
        std::string pin;
        bool isOutput = false;
        bool isValid() const { return node != 0; }
    };

    bool drawNode(script::Graph& graph, script::Node& node, const script::NodeDef* definition, ImVec2 origin, ImDrawList* drawList);
    bool drawValueEditor(script::Node& node, const script::PinDef& pin, float width);
    bool drawAddMenu(script::Graph& graph, ImVec2 at);
    static std::string key(int node, const std::string& pin, bool isOutput);

    ImVec2 scroll_{0, 0};                       // how far the board has been dragged
    int selected_ = 0;
    PinRef dragFrom_;                           // the pin a wire is being pulled from
    PinRef hovered_;                            // the pin under the pointer this frame
    PinRef menuPin_;
    int menuNode_ = 0;
    ImVec2 addAt_{0, 0};                        // where on the board a new node goes
    std::map<std::string, PinPlace> pins_;      // where every pin was drawn this frame
    bool needsCentering_ = true;
};

/// The color of a kind of wire.
ImU32 pinColor(script::PinType type);

}  // namespace molten::editor
