// The visual script panel: one script (a .graph file of the project) on the drawing board.
// What is drawn is saved as soon as the hand is off, and a game that is playing picks
// the change up at once: scripts can be rewired while they run.
#include "Editor.hpp"
#include "Script/CppBoxes.hpp"
#include "Script/CppToGraph.hpp"
#include "Platform.hpp"
#include "Script/Convert.hpp"
#include "GraphEditor.hpp"
#include "Panels/Widgets.hpp"
#include "Panels/Pickers.hpp"

#include <algorithm>
#include <cmath>
#include <set>

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
        // Following the selection: choose an entity with a Visual Script, and its script is shown,
        // and while the game runs, what that entity's own copy of it is doing (several entities
        // can run the same script; each has its own values).
        Entity chosen = editor.scene.primary();
        if (followsSelection_ && chosen != followed_) {
            followed_ = chosen;
            if (const VisualScript* component = editor.engine.scene.get<VisualScript>(chosen); component && !component->graph.empty()) {
                if (editor.assets.find(component->graph)) open(editor, component->graph);
                watchWanted_ = chosen;
            }
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
        drawWatchedEntity(editor);
        board_.pickText = [this, &editor](const script::PinDef& pin, std::string& value, float width) {
            if (pin.pick == "variable") {
                // The variables are those of this graph: the names its Set and Get Variable nodes use.
                std::set<std::string> names;
                for (const script::Node& node : graph_.nodes) {
                    if (node.type != "SetVariable" && node.type != "GetVariable") continue;
                    auto name = node.values.find(pin.name);
                    if (name != node.values.end() && !name->second.empty()) names.insert(name->second);
                }
                return pickFromList("variable", value, std::vector<std::string>(names.begin(), names.end()), true, width);
            }
            return pickValueInline(editor, Entity{}, pin.pick, value, width);
        };
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
        ImGui::SameLine();
        ImGui::Checkbox("Follow Selection", &followsSelection_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Choosing an entity with a Visual Script shows its script, and while the game runs, what that entity's copy of it is doing");
        }
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
        // The entity chosen in the scene, if it runs this script: its values, not another's.
        if (watchWanted_.isValid()) {
            const VisualScript* component = editor.engine.scene.get<VisualScript>(watchWanted_);
            if (component && component->graph == path_ && debugger.entity != watchWanted_) {
                debugger.entity = watchWanted_;
                debugger.values.clear();
                debugger.ran.clear();
            }
            watchWanted_ = Entity{};
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

    /// Whose script this is, when it was opened by choosing an entity: its name, and while the game
    /// runs, its own variables as they are now.
    void drawWatchedEntity(EditorContext& editor) {
        script::Debugger& debugger = script::Debugger::instance();
        Scene& world = editor.engine.scene;
        Entity shown = world.isAlive(debugger.entity) ? debugger.entity : world.isAlive(followed_) ? followed_ : Entity{};
        const VisualScript* component = world.isAlive(shown) ? world.get<VisualScript>(shown) : nullptr;
        if (!component || component->graph != path_) return;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetCursorScreenPos();
        std::string name = editor.scene.nameOf(shown);
        ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        // A chip with the entity's name; the live dot says the values below are its own, now.
        bool isLive = editor.scene.isPlaying();
        ImVec2 chipMax(at.x + nameSize.x + (isLive ? 34 : 22), at.y + nameSize.y + 8);
        drawList->AddRectFilled(at, chipMax, IM_COL32(255, 140, 60, 40), 10);
        drawList->AddRect(at, chipMax, IM_COL32(255, 140, 60, 150), 10);
        float textX = at.x + 11;
        if (isLive) {
            float pulse = 0.6f + 0.4f * float(std::sin(ImGui::GetTime() * 5.0));
            drawList->AddCircleFilled(ImVec2(at.x + 13, (at.y + chipMax.y) * 0.5f), 3.5f, IM_COL32(120, 255, 160, int(255 * pulse)), 12);
            textX += 12;
        }
        drawList->AddText(ImVec2(textX, at.y + 4), IM_COL32(255, 220, 190, 255), name.c_str());
        ImGui::Dummy(ImVec2(chipMax.x - at.x, chipMax.y - at.y));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(isLive ? "The values on the wires and the variables are this entity's, as they are now"
                                                             : "The script of the entity chosen in the scene");
        if (isLive && !component->state.variables.empty()) {
            // Its variables, as they are now, sorted by name.
            std::vector<std::pair<std::string, std::string>> variables;
            for (const auto& [variable, value] : component->state.variables) variables.push_back({variable, value.toText()});
            std::sort(variables.begin(), variables.end());
            for (const auto& [variable, value] : variables) {
                ImGui::SameLine(0, 10);
                ImGui::TextDisabled("%s", variable.c_str());
                ImGui::SameLine(0, 4);
                ImGui::Text("%s", value.size() > 18 ? (value.substr(0, 17) + "\xE2\x80\xA6").c_str() : value.c_str());
            }
        }
    }

    bool followsSelection_ = true;      // the script shown follows the entity chosen in the scene
    Entity followed_{};                 // the entity last followed
    Entity watchWanted_{};              // to watch once the script is open
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
    if (script::hasCppBoxes(*graph)) {
        // C++ laid out as boxes is the file it was made from: written back beside the graph, under the same
        // name. What the file said before is kept in the project's Library, in case the boxes were the older of the two.
        namespace fs = std::filesystem;
        std::string relative = (fs::path(graphPath).parent_path() / (fs::path(graphPath).stem().string() + ".cpp")).generic_string();
        std::string file = editor.assets.absolutePath(relative);
        std::string written = script::cppFromBoxes(*graph);
        std::optional<std::string> before = text::readFile(file);
        if (before && *before == written) {
            editor.notify("Assets/" + relative + " already says what the boxes say");
            return relative;
        }
        std::error_code code;
        if (before) {
            fs::path kept = fs::path(editor.project.libraryDirectory()) / "Before Conversion" / fs::path(relative).filename();
            fs::create_directories(kept.parent_path(), code);
            text::writeFile(kept.string(), *before);
        }
        fs::create_directories(fs::path(file).parent_path(), code);
        if (!text::writeFile(file, written)) {
            editor.notify("Cannot write Assets/" + relative, ConsoleMessage::Level::Error);
            return {};
        }
        editor.assets.refresh();
        editor.notify("Wrote Assets/" + relative + " from its boxes" + (before ? ". What it said before is in Library/Before Conversion" : ""));
        return relative;
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
    if (!code) {
        editor.notify("Cannot read Assets/" + cppPath, ConsoleMessage::Level::Error);
        return {};
    }
    script::CppToGraphResult converted = script::graphFromCpp(*code);
    if (!converted.graph) {
        editor.notify("Assets/" + cppPath + ": " + converted.error, ConsoleMessage::Level::Warning);
        return {};
    }
    std::string folder = std::filesystem::path(cppPath).parent_path().string();
    std::string relative = editor.assets.uniquePath(folder, std::filesystem::path(cppPath).stem().string(), ".graph");
    if (converted.isCodeAsBoxes) {
        // The code itself as boxes goes by the code's own name: Motor.cpp is Motor.graph, and is made again over
        // the one made before (but never over a visual script of that name that is something else).
        std::string same = (std::filesystem::path(cppPath).parent_path() / (std::filesystem::path(cppPath).stem().string() + ".graph")).generic_string();
        std::optional<std::string> there = text::readFile(editor.assets.absolutePath(same));
        std::optional<script::Graph> old = there ? script::Graph::fromText(*there) : std::nullopt;
        if (!there || (old && script::hasCppBoxes(*old))) relative = same;
    }
    if (!text::writeFile(editor.assets.absolutePath(relative), converted.graph->toText())) {
        editor.notify("Cannot write Assets/" + relative, ConsoleMessage::Level::Error);
        return {};
    }
    editor.assets.refresh();
    if (converted.isCodeAsBoxes) {
        int functions = 0, statements = 0;
        for (const script::Node& node : converted.graph->nodes) {
            functions += node.type == "CppFunction";
            statements += node.type == "CppCode" || node.type == "CppIf" || node.type == "CppBlock";
        }
        editor.notify("Wrote Assets/" + relative + ": the code itself as boxes, " + std::to_string(functions) + (functions == 1 ? " function, " : " functions, ") +
                      std::to_string(statements) + " statements, nothing left out. Convert to C++ Script writes it back");
    } else if (converted.notConverted.empty()) {
        editor.notify("Wrote Assets/" + relative + ": " + converted.name + " as boxes and wires");
    } else {
        // What has no box is said line by line, so nothing goes missing without a word.
        for (const std::string& line : converted.notConverted) editor.notify("Not in the graph, " + line, ConsoleMessage::Level::Warning);
        editor.notify("Wrote Assets/" + relative + ". " + std::to_string(converted.notConverted.size()) +
                          (converted.notConverted.size() == 1 ? " line has" : " lines have") + " no box: they are listed in the Console",
                      ConsoleMessage::Level::Warning);
    }
    return relative;
}

void registerVisualScriptPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<VisualScriptPanel>());
}

}  // namespace molten::editor
