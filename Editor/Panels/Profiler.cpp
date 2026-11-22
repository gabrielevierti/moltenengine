// The profiler: where each frame's time goes, frame after frame.
//
// At the top, the last five seconds as bars, one per frame, as tall as the frame took on the
// CPU, with what the GPU took as a line over them and the times of 60 and 30 frames a second
// marked. Below, the parts of a frame (events, update, physics, each kind of component,
// rendering, waiting) with their times: the average of the last second, or one frame's when a
// bar is clicked (click it again, or Live, to follow the frames again).
#include "Editor.hpp"
#include "Panels/Widgets.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Mesh.hpp"
#include "Rendering/Renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace molten::editor {

namespace {

struct Row {
    std::string name;
    int depth = 0;
    float ms = 0;
    int calls = 0;                      // the times it ran in a frame (fixed steps run several times)
};

/// A frame's sections as rows, one per place in the tree: repeated sections (fixed steps) added up.
std::vector<Row> rowsOf(const std::vector<const Profiler::Frame*>& frames) {
    std::vector<Row> rows;
    std::map<std::string, size_t> where;
    for (const Profiler::Frame* frame : frames) {
        std::vector<std::string> path;
        for (const Profiler::Section& section : frame->sections) {
            path.resize(size_t(section.depth));
            path.push_back(section.name);
            std::string key;
            for (const std::string& part : path) key += part + "/";
            auto found = where.find(key);
            if (found == where.end()) {
                // A new row goes after the last row under its parent, to keep the tree's order.
                where[key] = rows.size();
                rows.push_back(Row{section.name, section.depth, 0, 0});
                found = where.find(key);
            }
            rows[found->second].ms += section.ms;
            rows[found->second].calls++;
        }
    }
    float count = float(std::max<size_t>(frames.size(), 1));
    for (Row& row : rows) {
        row.ms /= count;
        row.calls = int(float(row.calls) / count + 0.5f);
    }
    return rows;
}

class ProfilerPanel final : public Panel {
public:
    ProfilerPanel() {
        title = "Profiler";
        isOpen = false;                 // Window > Profiler
    }

    void draw(EditorContext& editor) override {
        // Three pages: the frame's time, what the scene holds, what it weighs.
        if (ImGui::BeginTabBar("##profiler")) {
            if (ImGui::BeginTabItem("Frame")) {
                drawFrame(editor);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scene")) {
                drawStatistics(editor);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory")) {
                drawSceneWeightPage(editor);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

private:
    /// What the scene holds and what drawing it took, this frame.
    void drawStatistics(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        size_t triangles = 0;
        scene.each<MeshRenderer>([&](Entity, MeshRenderer& renderer) {
            if (renderer.mesh && renderer.isVisible) triangles += renderer.mesh->indexCount() / 3;
        });
        const RenderStats& stats = editor.engine.renderer().stats;
        auto row = [](const char* name, size_t value, const char* icon) {
            ImVec2 at = ImGui::GetCursorScreenPos();
            ui::drawIcon(ImGui::GetWindowDrawList(), ImVec2(at.x + 8, at.y + ImGui::GetTextLineHeight() * 0.5f), 12, icon, ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::SetCursorScreenPos(ImVec2(at.x + 26, at.y));
            ImGui::TextUnformatted(name);
            ImGui::SameLine(200);
            ImGui::Text("%zu", value);
        };
        ui::subheading("The Scene");
        row("Entities", editor.scene.entities().size(), "axes");
        row("Meshes", scene.count<MeshRenderer>(), "cube");
        row("Triangles", triangles, "cube");
        row("Lights", scene.count<Light>(), "light");
        row("Colliders", scene.count<Collider>(), "collider");
        row("Bodies", scene.count<RigidBody>(), "physics");
        row("Materials", editor.scene.materials.size(), "material");
        // Every picture drawn in the frame together: the Scene and the Game view, when both show.
        ui::subheading("The Last Frame");
        row("Draw calls", size_t(stats.drawCalls), "cube");
        row("Shadow draws", size_t(stats.shadowDraws), "light");
        row("Depth draws", size_t(stats.depthDraws), "cube");
        row("Full-screen passes", size_t(stats.passes), "eye");
        row("Interface and lines", size_t(stats.overlayDraws), "ui");
        row("Drawn", size_t(stats.visibleObjects), "eye");
        row("Culled", size_t(stats.culledObjects), "eye");
    }

    void drawFrame(EditorContext& editor) {
        Profiler& profiler = editor.engine.profiler;
        profiler.isEnabled = isRecording_;
        std::vector<const Profiler::Frame*> frames = profiler.frames();

        // ---- the bar: record or not, what the last second was
        if (ImGui::Button(isRecording_ ? "Pause" : "Record")) isRecording_ = !isRecording_;
        ui::tooltip(isRecording_ ? "Stop keeping frames, to look at the ones kept" : "Keep the time of every frame again");
        ImGui::SameLine();
        if (ImGui::Button("Live")) chosen_ = 0;
        ui::tooltip("Show the average of the last second, as it goes, instead of the frame chosen in the graph");
        const size_t recent = std::min<size_t>(frames.size(), 60);
        float cpu = 0, gpu = 0;
        int drawCalls = 0;
        for (size_t i = frames.size() - recent; i < frames.size(); i++) {
            cpu += frames[i]->cpuMs;
            gpu += frames[i]->gpuMs;
            drawCalls += frames[i]->drawCalls;
        }
        if (recent > 0) {
            cpu /= float(recent);
            gpu /= float(recent);
            drawCalls /= int(recent);
            ImGui::Text("CPU %.2f ms   GPU %.2f ms   %d draw calls   %.0f fps", double(cpu), double(gpu), drawCalls, double(editor.engine.time.framesPerSecond));
            ui::tooltip("Averages of the last second. The GPU's time is the frame's on the graphics chip: if it is longer than the CPU's, the GPU is the limit.");
        }
        if (frames.empty()) {
            ImGui::TextDisabled("No frames yet: press Record.");
            return;
        }

        // ---- the graph
        ImDrawList* ink = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetCursorScreenPos();
        float width = std::max(ImGui::GetContentRegionAvail().x, 100.0f), height = 110;
        ImGui::InvisibleButton("##graph", ImVec2(width, height));
        bool isHovered = ImGui::IsItemHovered();
        ink->AddRectFilled(at, ImVec2(at.x + width, at.y + height), IM_COL32(18, 20, 25, 255), 4);
        // The scale: at least 33 ms (30 frames a second), more if a frame took longer.
        float most = 33.4f;
        for (const Profiler::Frame* frame : frames) most = std::max({most, frame->cpuMs, frame->gpuMs});
        most = std::min(most * 1.1f, 250.0f);
        auto yOf = [&](float ms) { return at.y + height - std::min(ms / most, 1.0f) * (height - 4); };
        for (float guide : {16.67f, 33.33f}) {
            float y = yOf(guide);
            ink->AddLine(ImVec2(at.x, y), ImVec2(at.x + width, y), IM_COL32(255, 255, 255, 40));
            ink->AddText(ImVec2(at.x + 4, y + 1), IM_COL32(255, 255, 255, 90), guide < 20 ? "60 fps" : "30 fps");
        }
        float barWidth = width / float(Profiler::kHistory);
        float left = at.x + width - barWidth * float(frames.size());
        int hoveredIndex = -1;
        for (size_t i = 0; i < frames.size(); i++) {
            const Profiler::Frame& frame = *frames[i];
            float x = left + barWidth * float(i);
            bool isChosen = chosen_ != 0 && frame.number == chosen_;
            ImU32 color = isChosen ? IM_COL32(255, 122, 47, 255) : frame.cpuMs > 33.3f ? IM_COL32(230, 80, 70, 230)
                                                                  : frame.cpuMs > 16.7f ? IM_COL32(235, 190, 70, 230)
                                                                                        : IM_COL32(90, 170, 120, 230);
            ink->AddRectFilled(ImVec2(x, yOf(frame.cpuMs)), ImVec2(x + std::max(barWidth - 0.5f, 1.0f), at.y + height), color);
            if (i > 0 && frame.gpuMs > 0 && frames[i - 1]->gpuMs > 0) {
                ink->AddLine(ImVec2(x - barWidth * 0.5f, yOf(frames[i - 1]->gpuMs)), ImVec2(x + barWidth * 0.5f, yOf(frame.gpuMs)), IM_COL32(120, 190, 255, 230), 1.5f);
            }
            if (isHovered && ImGui::GetIO().MousePos.x >= x && ImGui::GetIO().MousePos.x < x + barWidth) hoveredIndex = int(i);
        }
        if (hoveredIndex >= 0) {
            const Profiler::Frame& frame = *frames[size_t(hoveredIndex)];
            ImGui::SetTooltip("CPU %.2f ms   GPU %.2f ms\n%d draw calls, %d triangles\nClick to look at this frame", double(frame.cpuMs), double(frame.gpuMs),
                              frame.drawCalls, frame.triangles);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                chosen_ = chosen_ == frame.number ? 0 : frame.number;
                if (chosen_ != 0) isRecording_ = false;           // held still, to read it
            }
        }
        ImGui::TextDisabled("Bars: the CPU's time for each frame.  Blue line: the GPU's.  Click a bar to look at that frame.");

        // ---- what each pass costs the GPU
        std::vector<GpuTimer::Pass> passes = editor.engine.renderer().gpuPasses();
        if (!passes.empty()) {
            float total = 0;
            for (const GpuTimer::Pass& pass : passes) total += pass.milliseconds;
            char heading[96];
            std::snprintf(heading, sizeof(heading), "The GPU, pass by pass: %.2f ms", double(total));
            ImGui::SeparatorText(heading);
            ui::tooltip("What each pass of the picture costs the graphics chip, averaged over the last frames, in the order they run. The longest are where to look first: Project Settings > Quality says how much each is asked for.");
            if (ImGui::BeginTable("##gpu", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("of the GPU's frame", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableHeadersRow();
                for (const GpuTimer::Pass& pass : passes) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(pass.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", double(pass.milliseconds));
                    ImGui::TableNextColumn();
                    float share = total > 0 ? pass.milliseconds / total : 0;
                    ImVec2 bar = ImGui::GetCursorScreenPos();
                    const float numbers = ImGui::CalcTextSize("100% ").x;
                    float room = std::max(ImGui::GetContentRegionAvail().x - numbers, 1.0f);
                    ImGui::Text("%3.0f%%", double(share * 100));
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bar.x + numbers, bar.y + 3),
                                                              ImVec2(bar.x + numbers + room * std::min(share, 1.0f), bar.y + ImGui::GetTextLineHeight() - 2),
                                                              IM_COL32(120, 190, 255, 170), 2);
                }
                ImGui::EndTable();
            }
        }

        // ---- the parts of the frame
        std::vector<const Profiler::Frame*> shown;
        for (const Profiler::Frame* frame : frames) {
            if (frame->number == chosen_) shown = {frame};
        }
        if (shown.empty()) {
            chosen_ = 0;
            shown.assign(frames.end() - long(recent), frames.end());
        }
        float whole = 0;
        for (const Profiler::Frame* frame : shown) whole += frame->cpuMs;
        whole /= float(shown.size());
        ImGui::SeparatorText(chosen_ != 0 ? "This frame" : "The last second, on average");
        if (ImGui::BeginTable("##sections", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Part of the frame", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("of the frame", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (const Row& row : rowsOf(shown)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(float(row.depth) * 14.0f + 0.01f);
                if (row.calls > 1) ImGui::Text("%s  x%d", row.name.c_str(), row.calls);
                else ImGui::TextUnformatted(row.name.c_str());
                ImGui::Unindent(float(row.depth) * 14.0f + 0.01f);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", double(row.ms));
                ImGui::TableNextColumn();
                float share = whole > 0 ? row.ms / whole : 0;
                ImVec2 bar = ImGui::GetCursorScreenPos();
                const float numbers = ImGui::CalcTextSize("100% ").x;
                float room = std::max(ImGui::GetContentRegionAvail().x - numbers, 1.0f);
                ImGui::Text("%3.0f%%", double(share * 100));
                // Waiting is drawn grey: time the CPU spent idle, for the GPU or the display.
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bar.x + numbers, bar.y + 3),
                                                          ImVec2(bar.x + numbers + room * std::min(share, 1.0f), bar.y + ImGui::GetTextLineHeight() - 2),
                                                          row.name.rfind("Waiting", 0) == 0 ? IM_COL32(120, 130, 150, 160) : IM_COL32(255, 122, 47, 170), 2);
            }
            ImGui::EndTable();
        }
    }

    bool isRecording_ = true;
    uint64_t chosen_ = 0;               // the frame looked at; 0 for the average of the last second
};

}  // namespace

void registerProfilerPanel(EditorContext& editor) {
    editor.engine.profiler.isEnabled = true;
    editor.registry.addPanel(nullptr, std::make_unique<ProfilerPanel>());
}

}  // namespace molten::editor
