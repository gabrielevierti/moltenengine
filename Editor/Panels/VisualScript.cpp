// The visual script panel: one script (a .graph file of the project) on the drawing board.
// What is drawn is saved as soon as the hand is off, and a game that is playing picks
// the change up at once: scripts can be rewired while they run.
#include "Editor.hpp"
#include "Platform.hpp"
#include "Script/Convert.hpp"
#include "GraphEditor.hpp"
#include "Panels/Widgets.hpp"

#include "Core/TextFile.hpp"

#include <filesystem>

namespace molten::editor {

namespace {

class VisualScriptPanel final : public Panel {
public:
    VisualScriptPanel() {
        title = "Visual Script";
        isOpen = false;                 // opens with a script (Edit Script, a double-click), or from the Window menu
        isOpen = false;                 // opens when a script is opened
    }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("Open a project to make visual scripts.");
            return;
        }
        // Someone asked for a script: double-click in the assets, Edit in the inspector.
        if (!editor.graphToEdit.empty()) {
            open(editor, editor.graphToEdit);
            editor.graphToEdit.clear();
            ImGui::SetWindowFocus();
        }
        // The file may have been renamed, moved or thrown away.
        if (!path_.empty() && !editor.assets.find(path_)) {
            save(editor);               // nothing is lost: written where it was, it shows up again
            path_.clear();
        }

        drawToolbar(editor);
        ImGui::Separator();
        if (path_.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("A visual script is a behavior drawn as boxes and wires instead of written as code. "
                               "Make one with New, wire an event to what should happen, and give it to an entity.");
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) editor.panelOwnsEditKeys = true;
        drawDebugging(editor);
        if (board_.draw(graph_, ImGui::GetContentRegionAvail())) isUnsaved_ = true;
        // Saved once the drag or the typing is over, not sixty times a second during it.
        if (isUnsaved_ && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) save(editor);
    }

private:
    void open(EditorContext& editor, const std::string& path) {
        if (path == path_) return;
        save(editor);
        std::optional<std::string> contents = text::readFile(editor.assets.absolutePath(path));
        std::optional<script::Graph> graph = contents ? script::Graph::fromText(*contents) : std::nullopt;
        if (!graph) {
            editor.notify("Cannot open " + path + " as a visual script", ConsoleMessage::Level::Error);
            return;
        }
        path_ = path;
        graph_ = std::move(*graph);
        isUnsaved_ = false;
        board_.resetView();
    }

    void save(EditorContext& editor) {
        if (!isUnsaved_ || path_.empty()) return;
        isUnsaved_ = false;
        if (!text::writeFile(editor.assets.absolutePath(path_), graph_.toText())) {
            editor.notify("Cannot write " + path_, ConsoleMessage::Level::Error);
        }
    }

    void createNew(EditorContext& editor) {
        std::string folder = editor.assets.find("Scripts") ? "Scripts" : editor.assets.createFolder("", "Scripts");
        std::string path = editor.assets.uniquePath(folder, "New Script", ".graph");
        if (!text::writeFile(editor.assets.absolutePath(path), script::Graph::starter().toText())) {
            editor.notify("Cannot create " + path, ConsoleMessage::Level::Error);
            return;
        }
        editor.assets.refresh();
        editor.selectedAsset = path;
        editor.currentFolder = folder;
        open(editor, path);
        editor.notify("Created Assets/" + path + ". Rename it in the asset browser.");
    }

    void drawToolbar(EditorContext& editor) {
        if (ImGui::Button("+ New")) createNew(editor);
        ImGui::SameLine();
        // Every script of the project, to go from one to another.
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##script", path_.empty() ? "Choose a script" : path_.c_str())) {
            bool any = false;
            for (const AssetInfo& asset : editor.assets.assets()) {
                if (asset.type != AssetType::Graph) continue;
                any = true;
                std::string candidate = asset.path;        // opening one may rescan the assets
                if (ImGui::Selectable(candidate.c_str(), candidate == path_)) {
                    open(editor, candidate);
                    break;
                }
            }
            if (!any) ImGui::TextDisabled("The project has no visual scripts yet.");
            ImGui::EndCombo();
        }
        if (path_.empty()) return;

        // Giving the script to what is selected: adds the component, or points it here.
        ImGui::SameLine();
        SceneDocument& scene = editor.scene;
        ImGui::BeginDisabled(scene.selection().empty() || scene.isPlaying());
        if (ImGui::Button("Use on Selection")) {
            for (Entity entity : scene.selection()) editor.engine.scene.add(entity, VisualScript{path_, {}, {}});
            scene.markChanged();
            editor.notify("The selection now runs " + path_);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Adds a Visual Script component with this script to the selected entities");
        }

        ImGui::SameLine();
        if (ImGui::Button("To C++")) {
            if (isUnsaved_) save(editor);
            std::string made = convertGraphToCpp(editor, path_);
            if (!made.empty()) platform::openWithDefaultApplication(editor.assets.absolutePath(made));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Writes the same behavior as a C++ component in Assets/Scripts, to go on from in code");
        // The view of the board.
        ImGui::SameLine(0, 16);
        if (ImGui::SmallButton("-")) board_.zoomBy(1 / 1.25f);
        ImGui::SameLine(0, 2);
        ImGui::TextDisabled("%3d%%", int(board_.zoom() * 100 + 0.5f));
        ImGui::SameLine(0, 2);
        if (ImGui::SmallButton("+")) board_.zoomBy(1.25f);
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("Fit")) board_.fitView();
        ImGui::SameLine();
        int users = 0;
        editor.engine.scene.each<VisualScript>([&](Entity, VisualScript& component) { users += component.graph == path_ ? 1 : 0; });
        ImGui::TextDisabled("%zu nodes, used by %d %s.  Space: add a node   drag: select   Cmd+scroll: zoom", graph_.nodes.size(), users, users == 1 ? "entity" : "entities");
    }

    /// While the game runs: the values on the wires and what ran; always: breakpoints; when the
    /// game is held at one, what to do next.
    void drawDebugging(EditorContext& editor) {
        script::Debugger& debugger = script::Debugger::instance();
        SceneDocument& scene = editor.scene;
        bool isPlaying = scene.isPlaying();
        if (isPlaying && !wasPlaying_) debugger.clearSeen();     // a new play: nothing seen yet
        wasPlaying_ = isPlaying;
        if (debugger.graph != path_) {
            debugger.graph = path_;
            debugger.values.clear();
            debugger.ran.clear();
            debugger.entity = Entity{};
        }
        debugger.isOn = isPlaying;
        // Stopped at a breakpoint: the game is held, and the script shown is the one stopped in.
        if (debugger.wantsHold) {
            debugger.wantsHold = false;
            scene.setPaused(true);
            if (debugger.stopped.graph != path_) editor.graphToEdit = debugger.stopped.graph;
        }
        debugView_.values = &debugger.values;
        debugView_.ran = &debugger.ran;
        debugView_.now = debugger.now;
        debugView_.stoppedAt = debugger.stopped.graph == path_ ? debugger.stopped.node : 0;
        std::string path = path_;
        debugView_.hasBreakpoint = [path](int node) { return script::Debugger::instance().hasBreakpoint(path, node); };
        debugView_.toggleBreakpoint = [path](int node) { script::Debugger::instance().toggleBreakpoint(path, node); };
        board_.debug = &debugView_;
        if (!isPlaying) {
            debugger.stopped = script::Debugger::Stop{};
            return;
        }
        // Which entity's values are shown, of those running this script.
        std::vector<Entity> users;
        editor.engine.scene.each<VisualScript>([&](Entity entity, VisualScript& component) {
            if (component.graph == path_) users.push_back(entity);
        });
        if (users.size() > 1) {
            ImGui::SetNextItemWidth(200);
            std::string shown = editor.engine.scene.isAlive(debugger.entity) ? scene.nameOf(debugger.entity) : "the first";
            if (ImGui::BeginCombo("##watched", ("Watching " + shown).c_str())) {
                for (Entity user : users) {
                    if (ImGui::Selectable((scene.nameOf(user) + "##" + std::to_string(user.index)).c_str(), user == debugger.entity)) {
                        debugger.entity = user;
                        debugger.values.clear();
                        debugger.ran.clear();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        }
        if (debugger.stopped.node != 0) {
            const script::Node* node = graph_.find(debugger.stopped.node);
            const script::NodeDef* definition = node ? script::NodeLibrary::instance().find(node->type) : nullptr;
            ImGui::TextColored(ImVec4(1, 0.86f, 0.25f, 1), "Held before %s, on %s", definition ? definition->title.c_str() : "a node",
                               scene.nameOf(debugger.stopped.entity).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Continue")) {
                debugger.stopped = script::Debugger::Stop{};
                scene.setPaused(false);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The node runs, and the game goes on until the next breakpoint");
            ImGui::SameLine();
            if (ImGui::SmallButton("Step")) {
                debugger.stopped = script::Debugger::Stop{};
                debugger.stepsOnce = true;
                scene.setPaused(false);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The node runs, and the game is held again before the next node that runs");
        } else if (!debugger.breakpoints.empty()) {
            ImGui::TextDisabled("Running. Breakpoints hold the game when the flow comes to them.");
        } else {
            ImGui::TextDisabled("Running: the last value on each wire; the nodes that ran light up. Right-click a node for a breakpoint.");
        }
    }

    GraphDebugView debugView_;
    bool wasPlaying_ = false;
    std::string path_;                  // relative to Assets; empty for none
    script::Graph graph_;
    GraphEditor board_;
    bool isUnsaved_ = false;
};

}  // namespace

std::string convertGraphToCpp(EditorContext& editor, const std::string& graphPath) {
    std::optional<std::string> text = text::readFile(editor.assets.absolutePath(graphPath));
    std::optional<script::Graph> graph = text ? script::Graph::fromText(*text) : std::nullopt;
    if (!graph) {
        editor.notify("Assets/" + graphPath + " could not be read as a visual script", ConsoleMessage::Level::Error);
        return {};
    }
    std::string name = script::typeNameFor(std::filesystem::path(graphPath).stem().string());
    std::string relative = editor.assets.uniquePath("Scripts", name, ".cpp");
    std::string file = editor.assets.absolutePath(relative);
    // The component is called after the file: a second copy gets a name of its own too.
    name = std::filesystem::path(relative).stem().string();
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), code);
    if (!text::writeFile(file, script::graphToCppKeepingGraph(*graph, name, "Assets/" + graphPath))) {
        editor.notify("Cannot write Assets/" + relative, ConsoleMessage::Level::Error);
        return {};
    }
    editor.assets.refresh();
    editor.notify("Wrote Assets/" + relative + ": a component called " + name + ", built with the game's code. It keeps the graph, to go back");
    return relative;
}

std::string convertCppToGraph(EditorContext& editor, const std::string& cppPath) {
    std::optional<std::string> code = text::readFile(editor.assets.absolutePath(cppPath));
    bool wasChanged = false;
    std::string name;
    std::optional<script::Graph> graph = code ? script::graphKeptIn(*code, &wasChanged, &name) : std::nullopt;
    if (!graph) {
        editor.notify("Assets/" + cppPath + " was not made from a visual script: code written by hand has no boxes to go back to",
                      ConsoleMessage::Level::Warning);
        return {};
    }
    std::string folder = std::filesystem::path(cppPath).parent_path().string();
    std::string relative = editor.assets.uniquePath(folder, std::filesystem::path(cppPath).stem().string(), ".graph");
    if (!text::writeFile(editor.assets.absolutePath(relative), graph->toText())) {
        editor.notify("Cannot write Assets/" + relative, ConsoleMessage::Level::Error);
        return {};
    }
    editor.assets.refresh();
    if (wasChanged) {
        editor.notify("Wrote Assets/" + relative + " from the graph Assets/" + cppPath + " was made from. The code was changed since: those changes are not in it",
                      ConsoleMessage::Level::Warning);
    } else {
        editor.notify("Wrote Assets/" + relative + ": the same behavior as boxes and wires");
    }
    return relative;
}

void registerVisualScriptPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<VisualScriptPanel>());
}

}  // namespace molten::editor
