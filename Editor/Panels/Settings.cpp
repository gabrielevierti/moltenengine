// Two panels of settings: how the open scene is rendered, and the facts of the project.
#include "Editor.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>

namespace molten::editor {

namespace {

class RenderingPanel final : public Panel {
public:
    RenderingPanel() { title = "Rendering"; }

    void draw(EditorContext& editor) override {
        RenderSettings& settings = editor.engine.renderer().settings;
        bool changed = false;

        if (ui::section("Sky")) {
            changed |= ui::color("Zenith", settings.sky.zenithColor, true);
            changed |= ui::color("Horizon", settings.sky.horizonColor, true);
            changed |= ui::color("Ground", settings.sky.groundColor, true);
            changed |= ui::dragFloat("Brightness", settings.sky.intensity, 0.01f, 0.0f, 100.0f);
            changed |= ui::dragFloat("Sun Disk", settings.sky.sunDiskIntensity, 0.1f, 0.0f, 1000.0f);
            changed |= ui::dragDegrees("Sun Size", settings.sky.sunAngularRadius, 0.1f, 20.0f);
            changed |= ui::sliderFloat("Clouds", settings.sky.cloudCoverage, 0.0f, 1.0f);
            changed |= ui::dragFloat("Cloud Scale", settings.sky.cloudScale, 0.01f, 0.05f, 20.0f);
            changed |= ui::dragVec2("Cloud Wind", settings.sky.cloudSpeed, 0.001f);
            changed |= ui::dragFloat("Sky Lighting", settings.environmentIntensity, 0.01f, 0.0f, 10.0f);
        }
        if (ui::section("Fog")) {
            changed |= ui::color("Color", settings.fogColor, true);
            changed |= ui::dragFloat("Density", settings.fogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
        }
        if (ui::section("Shadows")) {
            changed |= ui::checkbox("Enabled", settings.shadowsEnabled);
            changed |= ui::dragFloat("Distance", settings.cascades.shadowDistance, 0.5f, 1.0f, 5000.0f);
            changed |= ui::dragFloat("Bias", settings.shadowBias, 0.00001f, 0.0f, 0.01f, "%.5f");
            changed |= ui::dragFloat("Normal Offset", settings.shadowNormalOffset, 0.05f, 0.0f, 10.0f);
        }
        if (ui::section("Ray Tracing")) {
            bool canTrace = editor.engine.renderer().canTraceRays();
            ImGui::BeginDisabled(!canTrace);
            changed |= ui::checkbox("Sun Shadows", settings.rayTracedShadows);
            if (settings.rayTracedShadows) {
                changed |= ui::dragInt("Rays per Pixel", settings.shadowRayCount, 0.1f, 1, 16);
                ImGui::TextDisabled("Softness follows Sky > Sun Size.");
            }
            ImGui::EndDisabled();
            if (!canTrace) {
                ImGui::TextWrapped(editor.engine.renderer().device().supportsRayTracing()
                                       ? "The ray tracing shaders did not compile on this system (see the console). Shadow maps are used."
                                       : "This GPU cannot trace rays. Shadow maps are used.");
            }
        }
        if (ui::section("Ambient Occlusion")) {
            changed |= ui::checkbox("Enabled##ao", settings.ambientOcclusionEnabled);
            changed |= ui::dragFloat("Radius", settings.ambientOcclusionRadius, 0.01f, 0.01f, 10.0f);
            changed |= ui::dragFloat("Strength", settings.ambientOcclusionIntensity, 0.01f, 0.0f, 4.0f);
            changed |= ui::dragFloat("Contrast", settings.ambientOcclusionPower, 0.01f, 0.1f, 8.0f);
        }
        if (ui::section("Bloom")) {
            changed |= ui::checkbox("Enabled##bloom", settings.bloomEnabled);
            changed |= ui::dragFloat("Amount", settings.bloomIntensity, 0.002f, 0.0f, 2.0f);
            changed |= ui::dragFloat("Threshold", settings.bloomThreshold, 0.01f, 0.0f, 50.0f);
            changed |= ui::dragFloat("Knee", settings.bloomKnee, 0.01f, 0.0f, 1.0f);
        }
        if (ui::section("Camera and Film")) {
            changed |= ui::dragFloat("Exposure", settings.exposure, 0.01f, 0.01f, 50.0f);
            changed |= ui::combo("Tone Mapping", settings.toneMapper, {"ACES", "Reinhard", "None"});
            changed |= ui::sliderFloat("Vignette", settings.vignette, 0.0f, 1.0f);
            changed |= ui::sliderFloat("Lens Distortion", settings.lensDistortion, 0.0f, 1.0f);
            changed |= ui::dragFloat("Color Fringes", settings.chromaticAberration, 0.0005f, 0.0f, 0.1f, "%.4f");
            changed |= ui::dragFloat("Film Grain", settings.filmGrain, 0.001f, 0.0f, 0.5f);
        }
        if (ui::section("Quality")) {
            static const int sampleCounts[] = {1, 2, 4};
            int index = settings.sampleCount >= 4 ? 2 : settings.sampleCount >= 2 ? 1 : 0;
            if (ui::combo("Anti-Aliasing", index, {"Off", "2x MSAA", "4x MSAA"})) {
                settings.sampleCount = sampleCounts[index];
                changed = true;
            }
            changed |= ui::sliderFloat("Render Scale", settings.renderScale, 0.25f, 2.0f);
        }
        // These settings are saved with the scene, so changing them is an edit like any other.
        if (changed) editor.scene.markChanged();
    }
};

class ProjectPanel final : public Panel {
public:
    ProjectPanel() {
        title = "Project Settings";
        isOpen = false;
    }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("No project is open.");
            return;
        }
        Project& project = editor.project;
        bool changed = ui::text("Name", project.name);
        changed |= ui::text("Version", project.version);
        changed |= ui::text("Identifier", project.identifier);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The bundle identifier of the game, like com.yourname.game");

        // The scene the game starts with: one of the project's scenes.
        std::vector<std::string> scenes;
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.type == AssetType::Scene) scenes.push_back(asset.path);
        }
        int index = -1;
        for (size_t i = 0; i < scenes.size(); i++) {
            if (scenes[i] == project.startScene) index = int(i);
        }
        if (ui::combo("First Scene", index, scenes)) {
            project.startScene = scenes[size_t(index)];
            changed = true;
        }
        ui::info("Folder", project.directory);

        if (changed) {
            if (project.name.empty()) project.name = "Untitled";
            std::string error;
            if (!project.save(&error)) editor.notify(error, ConsoleMessage::Level::Error);
        }
    }
};

}  // namespace

void registerRenderingPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<RenderingPanel>());
}

void registerProjectPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<ProjectPanel>());
}

}  // namespace molten::editor
