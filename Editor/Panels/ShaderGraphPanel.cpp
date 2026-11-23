// ShaderGraphPanel.cpp
// The Shader Graph window: a shader (a `.shader` that is a graph) on the same drawing board as the
// visual scripts, made of the shader nodes (Rendering/ShaderGraph.hpp). Saved as soon as the hand
// is off; the engine makes the shader's code from it, and every material that uses it changes at
// once. Under the board, what is wrong (if anything) and the code it makes, to read, or to keep
// as a shader written as code.
#include "Editor.hpp"
#include "GraphEditor.hpp"
#include "Panels/Pickers.hpp"
#include "Panels/Widgets.hpp"
#include "Panels/TextHighlight.hpp"

#include "Core/TextFile.hpp"
#include "Rendering/ShaderGraph.hpp"

#include <filesystem>
#include <unordered_map>

namespace molten::editor {

namespace {

/// Whether an asset is a shader drawn as a graph. Its first line is read once for each time the
/// file is written.
bool isShaderGraph(EditorContext& editor, const std::string& path) {
    const AssetInfo* asset = path.empty() ? nullptr : editor.assets.find(path);
    if (!asset || asset->type != AssetType::Shader) return false;
    static std::unordered_map<std::string, std::pair<int64_t, bool>> known;
    auto found = known.find(path);
    if (found != known.end() && found->second.first == int64_t(asset->modified)) return found->second.second;
    bool isGraph = isShaderGraphFile(editor.assets.absolutePath(path));
    known[path] = {int64_t(asset->modified), isGraph};
    return isGraph;
}

class ShaderGraphPanel final : public Panel {
public:
    ShaderGraphPanel() {
        title = "Shader Graph";
        isOpen = false;                 // opens with a shader graph (a double-click in the Explorer), or from the Window menu
    }

    ImVec2 firstSize() const override { return ImVec2(960, 560); }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("Open a project to make shader graphs.");
            return;
        }
        // A shader drawn as a graph, chosen in the Explorer, is the one shown.
        if (editor.selectedAsset != path_ && isShaderGraph(editor, editor.selectedAsset)) open(editor, editor.selectedAsset);
        if (!path_.empty() && !editor.assets.find(path_)) {
            save(editor);
            path_.clear();
        }
        drawToolbar(editor);
        ImGui::Separator();
        if (path_.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("A shader graph is a shader drawn as boxes and wires: what the surface is at each pixel (its color, "
                               "how rough, how much metal, the light it gives off), from the mesh's UV, the time, noise, the "
                               "material's own pictures and properties each material sets. Make one with New; a material uses it "
                               "by choosing it as its Shader.");
            return;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) editor.panelOwnsEditKeys = true;
        // The board above, what it makes below (a strip that can be opened).
        const float codeHeight = showsCode_ ? std::min(220.0f, ImGui::GetContentRegionAvail().y * 0.4f) : 0.0f;
        const float statusHeight = ImGui::GetTextLineHeightWithSpacing() + 6;
        ImVec2 board = ImGui::GetContentRegionAvail();
        board.y = std::max(board.y - codeHeight - statusHeight, 120.0f);
        if (board_.draw(graph_, board)) {
            isUnsaved_ = true;
            remake();
        }
        if (isUnsaved_ && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) save(editor);
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1), "%s  %s", icon::CircleX, error_.c_str());
        } else {
            ImGui::TextDisabled("%s  The shader is made: materials using it show it now.", icon::Check);
        }
        if (showsCode_) {
            codeView_.draw(code_, TextKind::Code, codeHeight);
        }
    }

private:
    void drawToolbar(EditorContext& editor) {
        if (ImGui::Button("+ New")) createNew(editor);
        ui::tooltip("A new shader in Assets/Shaders, ready to change");
        ImGui::SameLine();
        std::vector<std::string> graphs;
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (!asset.isFolder() && isShaderGraph(editor, asset.path)) graphs.push_back(asset.path);
        }
        std::string chosen = path_;
        if (pickFromList("##graph", chosen, graphs, false, 260) && chosen != path_) {
            editor.selectedAsset = chosen;
            open(editor, chosen);
        }
        if (path_.empty()) return;
        ImGui::SameLine(0, 16);
        if (ImGui::Button(showsCode_ ? "Hide Code" : "Show Code")) showsCode_ = !showsCode_;
        ui::tooltip("The surface shader the graph makes: what the engine compiles");
        ImGui::SameLine();
        if (ImGui::Button("Save as Code")) saveAsCode(editor);
        ui::tooltip("Writes the code this graph makes as a shader of its own, to go on from by hand. The graph stays as it is");
        ImGui::SameLine();
        if (ImGui::Button("Fit")) board_.fitView();
        ImGui::SameLine();
        ImGui::TextDisabled("Space or right-click adds a node; drag from a pin to wire it.");
    }

    void open(EditorContext& editor, const std::string& path) {
        if (path == path_) return;
        save(editor);
        std::optional<std::string> contents = text::readFile(editor.assets.absolutePath(path));
        std::optional<script::Graph> graph = contents ? script::Graph::fromText(*contents) : std::nullopt;
        if (!graph) {
            editor.notify("Cannot open " + path + " as a shader graph", ConsoleMessage::Level::Error);
            return;
        }
        path_ = path;
        graph_ = std::move(*graph);
        graph_.library = &shaderNodes();
        isUnsaved_ = false;
        board_.resetView();
        remake();
    }

    void remake() {
        error_.clear();
        code_ = shaderTextFromGraph(graph_, path_, &error_);
    }

    void save(EditorContext& editor) {
        if (!isUnsaved_ || path_.empty()) return;
        isUnsaved_ = false;
        if (!text::writeFile(editor.assets.absolutePath(path_), graph_.toText())) {
            editor.notify("Cannot write " + path_, ConsoleMessage::Level::Error);
        }
    }

    void createNew(EditorContext& editor) {
        std::string folder = editor.assets.find("Shaders") ? "Shaders" : editor.assets.createFolder("", "Shaders");
        std::string path = editor.assets.uniquePath(folder, "New Shader", ".shader");
        if (!text::writeFile(editor.assets.absolutePath(path), newShaderGraphText())) {
            editor.notify("Cannot create " + path, ConsoleMessage::Level::Error);
            return;
        }
        editor.assets.refresh();
        editor.selectedAsset = path;
        open(editor, path);
    }

    void saveAsCode(EditorContext& editor) {
        if (code_.empty()) {
            editor.notify("The graph makes no code yet: " + error_, ConsoleMessage::Level::Warning);
            return;
        }
        std::filesystem::path made(path_);
        std::string folder = made.parent_path().generic_string();
        std::string path = editor.assets.uniquePath(folder, made.stem().string() + " (Code)", ".shader");
        if (!text::writeFile(editor.assets.absolutePath(path), code_)) {
            editor.notify("Cannot write " + path, ConsoleMessage::Level::Error);
            return;
        }
        editor.assets.refresh();
        editor.notify("Wrote " + path + ": a material can use it as it uses the graph", ConsoleMessage::Level::Info);
    }

    std::string path_, error_, code_;
    script::Graph graph_;
    GraphEditor board_;
    HighlightedText codeView_;
    bool isUnsaved_ = false, showsCode_ = false;
};

}  // namespace

void registerShaderGraphPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<ShaderGraphPanel>());
}

}  // namespace molten::editor
