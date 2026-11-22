// Two panels of settings: how the open scene is rendered, and the facts of the project.
#include "Editor.hpp"
#include "Core/TextFile.hpp"
#include "Rendering/Quality.hpp"
#include "Panels/Widgets.hpp"
#include "Panels/Pickers.hpp"

#include <algorithm>
#include <filesystem>

namespace molten::editor {

namespace {

/// Lighting: what lights the scene besides its lights. The sky, the air, shadows.
class LightingPanel final : public Panel {
public:
    LightingPanel() { title = "Lighting"; }

    void draw(EditorContext& editor) override {
        RenderSettings& settings = editor.engine.renderer().settings;
        bool changed = false;

        // The sun is an entity (the directional light): say where, since people look for it here.
        Entity sun;
        editor.engine.scene.each<Light>([&](Entity entity, Light& light) {
            if (!sun.isValid() && light.type == LightType::Directional) sun = entity;
        });
        // One page: what the scene is lit by, how its light is worked out (shadows, rays, ambient
        // occlusion), and its lights in a list that opens when wanted. How well the game draws all
        // this, as it ships, is the project's: Project Settings > Quality.
        drawEnvironment(editor, settings, sun, changed);
        drawQuality(editor, settings, changed);
        if (ImGui::CollapsingHeader("Lights in the Scene")) drawLights(editor, changed);
        // These settings are saved with the scene, so changing them is an edit like any other.
        if (changed) editor.scene.markChanged();
    }

private:
    void drawEnvironment(EditorContext& editor, RenderSettings& settings, Entity sun, bool& changed) {
        if (ui::section("Sun")) {
            if (sun.isValid()) {
                Light* light = editor.engine.scene.get<Light>(sun);
                changed |= ui::color("Color", light->color, false);
                changed |= ui::dragFloat("Intensity", light->intensity, 0.02f, 0.0f, 100.0f);
                changed |= ui::checkbox("Casts Shadows", light->castsShadows);
                changed |= ui::dragDegrees("Size in the Sky", settings.sky.sunAngularRadius, 0.1f, 20.0f);
                ui::tooltip("A larger sun gives softer ray traced shadows, and a larger disk in the sky");
                changed |= ui::dragFloat("Disk Brightness", settings.sky.sunDiskIntensity, 0.1f, 0.0f, 1000.0f);
                if (ImGui::Button("Select the Sun")) {
                    editor.scene.select(sun);
                    editor.selectedAsset.clear();
                }
                ui::tooltip("Its direction is its rotation: turn it with the rotate tool");
            } else {
                ImGui::TextDisabled("The scene has no directional light.");
                ImGui::TextDisabled("Entity > Light > Directional Light makes one.");
            }
        }
        if (ui::section("Sky")) {
            changed |= ui::checkbox("Sunset Colors", settings.sky.sunsetColors);
            ui::tooltip("As the sun nears the horizon: its light goes gold, orange and red, the sky glows round it, the clouds are lit from below; "
                        "when it has set, the sky darkens into night");
            changed |= ui::color("Zenith", settings.sky.zenithColor, true);
            changed |= ui::color("Horizon", settings.sky.horizonColor, true);
            if (settings.sky.sunsetColors) {
                changed |= ui::sliderFloat("Moon", settings.sky.moon, 0.0f, 2.0f);
                ui::tooltip("At night: the moon across the sky from the sun, which lights the scene with a cold, dim light and casts shadows. 0 a night with no moon, 1 a full moon");
                changed |= ui::sliderFloat("Stars", settings.sky.stars, 0.0f, 3.0f);
                ui::tooltip("At night, when the sun is down: 0 none, 1 a clear night far from any town");
            }
            changed |= ui::color("Ground", settings.sky.groundColor, true);
            changed |= ui::dragFloat("Brightness", settings.sky.intensity, 0.01f, 0.0f, 100.0f);
            changed |= ui::dragFloat("Light from the Sky", settings.environmentIntensity, 0.01f, 0.0f, 10.0f);
            ui::tooltip("How much the sky lights the scene: the soft light in the shade, and what shiny things mirror");
        }
        if (ui::section("Clouds")) {
            changed |= ui::sliderFloat("Coverage", settings.sky.cloudCoverage, 0.0f, 1.0f);
            changed |= ui::dragFloat("Scale", settings.sky.cloudScale, 0.01f, 0.05f, 20.0f);
            changed |= ui::dragVec2("Drift", settings.sky.cloudSpeed, 0.001f);
            changed |= ui::checkbox("With Depth", settings.sky.cloudsHaveDepth);
            ui::tooltip("Clouds as volumes, thick and thin, lit by the sun through them: a camera can fly in, through and above them. Costs more than the flat layer");
            if (settings.sky.cloudsHaveDepth) {
                changed |= ui::dragFloat("Height", settings.sky.cloudHeight, 5.0f, 50.0f, 12000.0f, "%.0f m");
                ui::tooltip("Meters above the ground where their base is");
                changed |= ui::dragFloat("Thickness", settings.sky.cloudThickness, 5.0f, 50.0f, 8000.0f, "%.0f m");
            }
        }
        if (ui::section("Fog")) {
            changed |= ui::color("Color##fog", settings.fogColor, true);
            changed |= ui::dragFloat("Density", settings.fogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
            changed |= ui::dragFloat("Thins With Height", settings.fogHeightFalloff, 0.001f, 0.0f, 2.0f, "%.3f");
            ui::tooltip("Above zero the fog lies low, as mist in a valley, and thins at this rate per meter of height");
            changed |= ui::dragFloat("Glow Toward Sun", settings.fogSunScatter, 0.02f, 0.0f, 20.0f);
            changed |= ui::dragInt("Light Shafts", settings.fogShaftSteps, 0.2f, 0, 48);
            ui::tooltip("Above zero the fog glows only where the sun reaches it: what stands in the sun's way casts beams of shadow through the mist. The number is how finely the fog is walked through; 12 to 24 is usual, and each step costs. Needs Glow Toward Sun above zero");
        }
        if (ui::section("Wind")) {
            changed |= ui::dragVec3("Velocity", settings.wind, 0.05f, "%.2f m/s");
            changed |= ui::sliderFloat("Gusts", settings.windGustiness, 0.0f, 1.0f);
            ImGui::TextDisabled("Carries particles with Wind Influence above zero.");
        }
    }

    void drawQuality(EditorContext& editor, RenderSettings& settings, bool& changed) {
        bool canTrace = editor.engine.renderer().canTraceRays();
        bool hasExtras = editor.engine.renderer().hasExtras();
        // One choice for the whole: four levels, from what any Mac runs at full speed to the best there is.
        if (ui::section("Ray Tracing")) {
            static const char* names[] = {"Low", "Medium", "High", "Ultra"};
            static const char* what[] = {"Shadow maps only. Fast on every Mac, iPhone and iPad.",
                                         "The sun's shadows ray traced: exact at any distance, soft far from what casts them. Less than a ray a pixel.",
                                         "And the sky's light hidden where it really is (under a car, in a corner), and reflections on smooth things. About a ray a pixel.",
                                         "And light bounced off what is lit, reflections on rougher things, smoother shadow edges. About a ray and a half a pixel."};
            LightingQuality current = lightingQualityOf(settings);
            // What the project's quality lets through of it.
            const GameQuality* quality = editor.engine.quality ? &*editor.engine.quality : nullptr;
            LightingQuality most = quality ? quality->rayTracing : LightingQuality::Ultra;
            float width = (ImGui::GetContentRegionAvail().x - 3 * 4) / 4;
            for (int level = 0; level < 4; level++) {
                if (level > 0) ImGui::SameLine(0, 4);
                bool isOn = int(current) == level;
                ImGui::BeginDisabled(level > 0 && !canTrace);
                ImGui::PushStyleColor(ImGuiCol_Button, isOn ? ImVec4(0.95f, 0.47f, 0.18f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
                if (ImGui::Button(names[level], ImVec2(width, 0))) {
                    applyLightingQuality(settings, LightingQuality(level));
                    if (level > 1 && !hasExtras) settings.rayTracedOcclusion = settings.rayTracedReflections = settings.rayTracedBounce = false;
                    changed = true;
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", what[level]);
            }
            ImGui::TextWrapped("%s", current == LightingQuality::Custom ? "Custom: set one by one below." : what[int(current)]);
            // The scene asks for more than the project's quality gives: said, with the way there.
            RenderSettings given = settings;
            limitLightingQuality(given, most);
            bool isLimited = canTrace && (given.rayTracedShadows != settings.rayTracedShadows || given.rayTracedOcclusion != settings.rayTracedOcclusion ||
                                          given.rayTracedReflections != settings.rayTracedReflections || given.rayTracedBounce != settings.rayTracedBounce);
            if (isLimited) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.72f, 0.35f, 1));
                ImGui::TextWrapped("The project's quality is at %s for ray tracing: this scene is drawn with that much of it.", names[int(most)]);
                ImGui::PopStyleColor();
                if (ImGui::SmallButton("Project Settings > Quality")) editor.showProjectSettings("Quality");
            }
            if (!canTrace) {
                ImGui::TextDisabled("%s", editor.engine.renderer().device().supportsRayTracing()
                                                ? "The ray tracing shaders did not compile here (see the console): Low only."
                                                : "This GPU cannot trace rays: Low only.");
            }
        }
        if (ui::section("Shadow Maps")) {
            changed |= ui::checkbox("Enabled", settings.shadowsEnabled);
            changed |= ui::dragFloat("Distance", settings.cascades.shadowDistance, 0.5f, 5.0f, 2000.0f, "%.0f m");
            ui::tooltip("How far from the camera this scene's shadows should reach. A racing circuit wants more than a room");
            if (const GameQuality* quality = editor.engine.quality ? &*editor.engine.quality : nullptr; quality && quality->shadowDistance < settings.cascades.shadowDistance) {
                ImGui::TextDisabled("Up to %.0f m at the project's quality.", quality->shadowDistance);
            }
            ImGui::TextDisabled("How sharp: Project Settings > Quality.");
            changed |= ui::dragFloat("Bias", settings.shadowBias, 0.00001f, 0.0f, 0.01f, "%.5f");
            changed |= ui::dragFloat("Normal Offset", settings.shadowNormalOffset, 0.05f, 0.0f, 10.0f);
            if (settings.rayTracedShadows && canTrace) ImGui::TextDisabled("Ray traced shadows are on: the maps are not used.");
        }
        if (ui::section("Ray Traced Shadows")) {
            ImGui::BeginDisabled(!canTrace);
            changed |= ui::checkbox("Enabled##rt", settings.rayTracedShadows);
            ImGui::BeginDisabled(!settings.rayTracedShadows);
            changed |= ui::dragInt("Rays at an Edge", settings.shadowRayCount, 0.1f, 1, 16);
            ui::tooltip("One ray answers for a pixel well inside the light or a shadow. Along a shadow's edge this many are sent, to find how much of the sun shows: more is a smoother edge, and costs little");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        // The rest uses the rays the shadows already send for: off with them.
        bool canRest = canTrace && hasExtras && settings.rayTracedShadows;
        if (ui::section("Ray Traced Ambient Light")) {
            ImGui::BeginDisabled(!canRest);
            changed |= ui::checkbox("Enabled##ao-rt", settings.rayTracedOcclusion);
            ui::tooltip("The sky's light is hidden by what is really in the way: under a car, in a corner");
            ImGui::BeginDisabled(!settings.rayTracedOcclusion);
            changed |= ui::dragInt("Rays##ao", settings.occlusionRayCount, 0.1f, 1, 8);
            ui::tooltip("Sent for one pixel in four, before the scene is drawn, and smoothed: 1 is enough for the shade of corners, 2 for bounced light");
            changed |= ui::checkbox("Bounced Light", settings.rayTracedBounce);
            ui::tooltip("Global illumination, one bounce: a red wall in the sun tints the floor beside it");
            ImGui::BeginDisabled(!settings.rayTracedBounce);
            changed |= ui::dragFloat("Bounce Strength", settings.bounceIntensity, 0.02f, 0.0f, 8.0f);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        if (ui::section("Ray Traced Reflections")) {
            ImGui::BeginDisabled(!canRest);
            changed |= ui::checkbox("Enabled##refl", settings.rayTracedReflections);
            ui::tooltip("Smooth surfaces show what is around them. Each is weighed by the surface's own shine: rough things barely reflect");
            ImGui::BeginDisabled(!settings.rayTracedReflections);
            changed |= ui::sliderFloat("Up to Roughness", settings.reflectionMaxRoughness, 0.05f, 1.0f);
            ui::tooltip("Only surfaces smoother than this send a reflection ray, fading out toward it; rougher ones mirror the sky. Lower is faster");
            changed |= ui::sliderFloat("Strength", settings.reflectionStrength, 0.0f, 1.5f);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        if (!canRest && canTrace) ImGui::TextDisabled(hasExtras ? "Ambient light and reflections need Ray Traced Shadows on." : "The extra shaders did not compile (see the console).");
        if (ui::section("Screen Ambient Occlusion")) {
            changed |= ui::checkbox("Enabled##ao", settings.ambientOcclusionEnabled);
            ui::tooltip("A cheap darkening of creases from the picture itself. Ray traced ambient light does it better where it is on");
            changed |= ui::dragFloat("Radius", settings.ambientOcclusionRadius, 0.01f, 0.01f, 10.0f);
            changed |= ui::dragFloat("Strength", settings.ambientOcclusionIntensity, 0.01f, 0.0f, 4.0f);
            changed |= ui::dragFloat("Contrast", settings.ambientOcclusionPower, 0.01f, 0.1f, 8.0f);
        }
    }

    /// Every light of the scene in one table: turn each on and off, set it, go to it.
    void drawLights(EditorContext& editor, bool& changed) {
        Scene& scene = editor.engine.scene;
        int count = 0;
        if (ImGui::BeginTable("##lights", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 28);
            ImGui::TableSetupColumn("Light", ImGuiTableColumnFlags_WidthStretch, 3);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 34);
            ImGui::TableSetupColumn("Intensity", ImGuiTableColumnFlags_WidthStretch, 2);
            ImGui::TableHeadersRow();
            for (Entity entity : editor.scene.entities()) {
                Light* light = scene.get<Light>(entity);
                if (!light) continue;
                count++;
                ImGui::PushID(int(entity.index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                changed |= ImGui::Checkbox("##on", &light->isEnabled);
                ImGui::TableNextColumn();
                const Name* name = scene.get<Name>(entity);
                if (ImGui::Selectable(name ? name->value.c_str() : "Light", editor.scene.isSelected(entity))) {
                    editor.scene.select(entity);
                    editor.selectedAsset.clear();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(light->type == LightType::Directional ? "Directional" : light->type == LightType::Spot ? "Spot" : "Point");
                ImGui::TableNextColumn();
                float color[3] = {light->color.x, light->color.y, light->color.z};
                if (ImGui::ColorEdit3("##color", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float)) {
                    light->color = Vec3{color[0], color[1], color[2]};
                    changed = true;
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::DragFloat("##intensity", &light->intensity, 0.02f, 0.0f, 1000.0f, "%.2f");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (count == 0) ImGui::TextDisabled("The scene has no lights. Entity > Light makes one.");
        else ImGui::TextDisabled("%d lights. Click a name to select it; its direction and place are its Transform.", count);
    }

};

/// Rendering: how the picture is made from the lit scene. The camera's film, effects, quality.
class RenderingPanel final : public Panel {
public:
    RenderingPanel() { title = "Rendering"; }

    void draw(EditorContext& editor) override {
        RenderSettings& settings = editor.engine.renderer().settings;
        bool changed = false;

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
        if (ui::section("Exposure and Color")) {
            changed |= ui::checkbox("Adapts", settings.autoExposure);
            ui::tooltip("The exposure follows how bright the scene is, as an eye does: brighter in a tunnel, darker in the sun");
            if (settings.autoExposure) {
                changed |= ui::dragFloat("Compensation", settings.exposureCompensation, 0.01f, -6.0f, 6.0f, "%.2f stops");
                changed |= ui::dragFloat("Speed", settings.exposureAdaptSpeed, 0.01f, 0.05f, 20.0f);
                changed |= ui::dragFloat("Darkest", settings.darkestExposure, 0.01f, 0.01f, 10.0f);
                changed |= ui::dragFloat("Brightest", settings.brightestExposure, 0.05f, 0.1f, 100.0f);
            }
            changed |= pickValue(editor, Entity{}, "picture", "Grading Table", settings.colorGrading);
            ui::tooltip("A picture in Assets of 32 squares of 32 pixels side by side (red across, green down, blue square to square). "
                        "Assets/Grading/Neutral 32.png changes nothing: grade a copy of it in a photo editor. Empty: none");
            if (!settings.colorGrading.empty()) changed |= ui::sliderFloat("Grading Amount", settings.colorGradingAmount, 0.0f, 1.0f);
        }
        if (ui::section("Depth of Field and Motion Blur")) {
            changed |= ui::checkbox("Depth of Field", settings.depthOfField);
            if (settings.depthOfField) {
                changed |= ui::dragFloat("Focus Distance", settings.focusDistance, 0.05f, 0.1f, 1000.0f, "%.2f m");
                changed |= ui::dragFloat("In Focus", settings.focusRange, 0.05f, 0.0f, 500.0f, "%.2f m");
                ui::tooltip("How many meters around the focus distance stay sharp");
                changed |= ui::sliderFloat("Blur", settings.focusBlur, 0.0f, 32.0f);
            }
            changed |= ui::checkbox("Motion Blur", settings.motionBlur);
            ui::tooltip("What streaks past as the camera turns or speeds along");
            if (settings.motionBlur) changed |= ui::sliderFloat("Shutter", settings.motionBlurAmount, 0.0f, 1.0f);
        }
        if (ui::section("Color")) {
            changed |= ui::sliderFloat("Contrast", settings.contrast, 0.5f, 1.5f);
            changed |= ui::sliderFloat("Saturation", settings.saturation, 0.0f, 2.0f);
            changed |= ui::sliderFloat("Temperature", settings.temperature, -1.0f, 1.0f);
            ui::tooltip("Below zero bluer, as at dusk; above, warmer, as by a fire");
            changed |= ui::sliderFloat("Tint", settings.tint, -1.0f, 1.0f);
            ui::tooltip("Below zero greener, above more magenta");
            changed |= ui::sliderFloat("Sharpen", settings.sharpen, 0.0f, 1.0f);
            ui::tooltip("Crisper edges: good with a render scale below 1");
        }
        if (ui::section("Lens")) {
            changed |= ui::sliderFloat("Flare##flare", settings.lensFlare, 0.0f, 2.0f);
            ui::tooltip("Light of the sun scattered in the camera's glass: a glow round it, a star of streaks, a halo, and colored ghosts across the picture, "
                        "while the sun is in view and not hidden. 0: none");
            changed |= pickValue(editor, Entity{}, "picture", "Dirt", settings.lensDirt);
            ui::tooltip("Smudges and specks on the glass (a mostly black picture: Lens/Dirt.png comes with the engine), lit up where the sun, "
                        "the lamps and other bright light fall on them, through the bloom and the flare. None: a clean lens");
            if (!settings.lensDirt.empty()) changed |= ui::sliderFloat("Dirt Amount", settings.lensDirtAmount, 0.0f, 3.0f);
            changed |= ui::sliderFloat("Signal Noise", settings.interference, 0.0f, 1.0f);
            ui::tooltip("An analog video link: 0 clean, 1 about to drop (a drone's camera)");
        }
        if (changed) editor.scene.markChanged();
    }
};

/// Input: the actions of the game and what triggers each. Saved in Assets/Input.actions,
/// which the game ships with; code and components ask for actions by name.
class InputPanel final : public Panel {
public:
    InputPanel() {
        title = "Input";
        isOpen = false;                 // shown as the Input page of Edit > Project Settings
    }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("No project is open.");
            return;
        }
        if (loadedFor_ != editor.project.directory) {
            loadedFor_ = editor.project.directory;
            std::optional<std::string> contents = text::readFile(editor.assets.absolutePath("Input.actions"));
            map_ = contents ? InputMap::fromText(*contents) : InputMap::standard();
            editor.engine.input.apply(map_);
        }
        drawDevices(editor);
        ui::subheading("Actions");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("What the player can do. Code and components ask for them by name, so the keys can change here without touching the game.");
        ImGui::PopTextWrapPos();
        bool changed = false;
        int removed = -1;
        for (int i = 0; i < int(map_.actions.size()); i++) {
            InputAction& action = map_.actions[size_t(i)];
            ImGui::PushID(i);
            // A row each: its name, what it is bound to, and what it reads now; the chosen one opens.
            float value = action.isAxis ? editor.engine.input.axis(action.name) : (editor.engine.input.isDown(action.name) ? 1.0f : 0.0f);
            ImVec2 rowAt = ImGui::GetCursorScreenPos();
            float rowWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable("##row", chosen_ == i, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 24))) chosen_ = chosen_ == i ? -1 : i;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ui::drawIcon(draw, ImVec2(rowAt.x + 10, rowAt.y + 12), 13, action.isAxis ? "logic" : "ui", ImGui::GetColorU32(ImGuiCol_Text));
            draw->AddText(ImVec2(rowAt.x + 24, rowAt.y + 4), ImGui::GetColorU32(ImGuiCol_Text), action.name.empty() ? "(no name)" : action.name.c_str());
            std::string bound = summaryOf(action);
            draw->AddText(ImVec2(rowAt.x + 160, rowAt.y + 4), ImGui::GetColorU32(ImGuiCol_TextDisabled), bound.c_str());
            // The meter: lit while pressed; an axis from the middle.
            float meterX = rowAt.x + rowWidth - 110;
            draw->AddRectFilled(ImVec2(meterX, rowAt.y + 9), ImVec2(meterX + 70, rowAt.y + 15), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
            ImU32 lit = ImGui::GetColorU32(ImGuiCol_ButtonActive);
            if (action.isAxis) {
                float middle = meterX + 35;
                draw->AddRectFilled(ImVec2(std::min(middle, middle + value * 35), rowAt.y + 9), ImVec2(std::max(middle, middle + value * 35), rowAt.y + 15), lit, 3.0f);
            } else if (value > 0) {
                draw->AddRectFilled(ImVec2(meterX, rowAt.y + 9), ImVec2(meterX + 70, rowAt.y + 15), lit, 3.0f);
            }
            ImGui::SameLine(rowWidth - 24);
            if (ImGui::SmallButton("x")) removed = i;
            ui::tooltip("Remove this action");
            if (chosen_ == i) {
                ImGui::Indent(12);
                changed |= ui::text("Name", action.name);
                int kind = action.isAxis ? 1 : 0;
                if (ui::combo("Kind", kind, {"Button: on or off", "Axis: from -1 to 1"})) {
                    action.isAxis = kind == 1;
                    changed = true;
                }
                if (action.isAxis) {
                    changed |= keyChoice("Negative Key", action.negative);
                    changed |= keyChoice("Positive Key", action.positive);
                    changed |= keyChoice("Negative, Also", action.negativeAlt);
                    changed |= keyChoice("Positive, Also", action.positiveAlt);
                    std::vector<std::string> axes = {"None"};
                    for (int a = 0; a < int(GamepadAxis::Count); a++) axes.push_back(gamepadAxisName(GamepadAxis(a)));
                    int axis = action.gamepadAxis == GamepadAxis::None ? 0 : int(action.gamepadAxis) + 1;
                    if (ui::combo("Controller Stick", axis, axes)) {
                        action.gamepadAxis = axis == 0 ? GamepadAxis::None : GamepadAxis(axis - 1);
                        changed = true;
                    }
                    if (action.gamepadAxis != GamepadAxis::None) changed |= ui::checkbox("Stick Reversed", action.invert);
                    std::vector<std::string> numbers = {"None"};
                    for (int a = 0; a < 12; a++) numbers.push_back("Axis " + std::to_string(a));
                    int number = action.joystickAxis + 1;
                    if (ui::combo("Joystick Axis", number, numbers)) {
                        action.joystickAxis = number - 1;
                        changed = true;
                    }
                    ui::tooltip("For a joystick that is not a game controller: a radio for models on USB, a flight stick, a wheel");
                    if (action.joystickAxis >= 0) changed |= ui::checkbox("Joystick Reversed", action.joystickInvert);
                } else {
                    changed |= keyList("Keys", action.keys);
                    changed |= buttonList("Controller", action.buttons);
                    changed |= mouseList("Mouse", action.mouseButtons);
                }
                if (!action.isAxis) {
                    if (ImGui::Button(listening_ == i ? "Press a key or a controller button..." : "Bind by Pressing")) listening_ = listening_ == i ? -1 : i;
                    ui::tooltip("Then press what should do it: a key, or a button of the controller");
                    if (listening_ == i) changed |= listen(editor, action);
                }
                ImGui::Unindent(12);
                ImGui::Spacing();
            }
            ImGui::PopID();
        }
        if (removed >= 0) {
            map_.actions.erase(map_.actions.begin() + removed);
            changed = true;
        }
        ImGui::Spacing();
        if (ImGui::Button("+ Button")) {
            InputAction action;
            action.name = "action" + std::to_string(map_.actions.size() + 1);
            map_.actions.push_back(action);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Axis")) {
            InputAction action;
            action.name = "axis" + std::to_string(map_.actions.size() + 1);
            action.isAxis = true;
            map_.actions.push_back(action);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Back to the Standard Ones")) {
            map_ = InputMap::standard();
            changed = true;
        }
        ImGui::SameLine();
        // Only what a game needs is in the list: the actions of things that fly come on asking.
        bool hasFlying = false;
        for (const InputAction& action : map_.actions) hasFlying = hasFlying || action.name == "flaps";
        ImGui::BeginDisabled(hasFlying);
        if (ImGui::Button("+ Flying Actions")) {
            for (const InputAction& action : InputMap::flying().actions) {
                bool has = false;
                for (const InputAction& mine : map_.actions) has = has || mine.name == action.name;
                if (!has) map_.actions.push_back(action);
            }
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Adds yaw, throttle, throttleLever, flaps and brake: what Flight Controls reads");
        }
        if (changed) {
            editor.engine.input.apply(map_);
            if (!text::writeFile(editor.assets.absolutePath("Input.actions"), map_.toText())) {
                editor.notify("Cannot write Input.actions", ConsoleMessage::Level::Error);
            }
        }
    }

private:
    /// What an action is bound to, in a few words: "Space, A" or "A D, LeftX".
    static std::string summaryOf(const InputAction& action) {
        std::string out;
        auto add = [&](const std::string& word) {
            if (!word.empty()) out += (out.empty() ? "" : ", ") + word;
        };
        if (action.isAxis) {
            if (action.negative != Key::Unknown || action.positive != Key::Unknown) add(std::string(keyName(action.negative)) + " " + keyName(action.positive));
            if (action.gamepadAxis != GamepadAxis::None) add(gamepadAxisName(action.gamepadAxis));
        } else {
            for (Key key : action.keys) add(keyName(key));
            for (GamepadButton button : action.buttons) add(std::string("pad ") + gamepadButtonName(button));
            static const char* const kMouse[] = {"mouse left", "mouse right", "mouse middle"};
            for (MouseButton button : action.mouseButtons) add(kMouse[size_t(button)]);
        }
        return out.empty() ? std::string("nothing") : out;
    }

    /// The key of the editor's own keyboard that a key of the game is.
    static ImGuiKey imguiKeyOf(Key key) {
        int k = int(key);
        if (key >= Key::A && key <= Key::Z) return ImGuiKey(ImGuiKey_A + (k - int(Key::A)));
        if (key >= Key::Num0 && key <= Key::Num9) return ImGuiKey(ImGuiKey_0 + (k - int(Key::Num0)));
        if (key >= Key::F1 && key <= Key::F12) return ImGuiKey(ImGuiKey_F1 + (k - int(Key::F1)));
        switch (key) {
            case Key::Space: return ImGuiKey_Space;
            case Key::Enter: return ImGuiKey_Enter;
            case Key::Escape: return ImGuiKey_Escape;
            case Key::Tab: return ImGuiKey_Tab;
            case Key::Backspace: return ImGuiKey_Backspace;
            case Key::Left: return ImGuiKey_LeftArrow;
            case Key::Right: return ImGuiKey_RightArrow;
            case Key::Up: return ImGuiKey_UpArrow;
            case Key::Down: return ImGuiKey_DownArrow;
            case Key::LeftShift: return ImGuiKey_LeftShift;
            case Key::RightShift: return ImGuiKey_RightShift;
            case Key::LeftControl: return ImGuiKey_LeftCtrl;
            case Key::RightControl: return ImGuiKey_RightCtrl;
            case Key::LeftAlt: return ImGuiKey_LeftAlt;
            case Key::RightAlt: return ImGuiKey_RightAlt;
            default: return ImGuiKey_None;
        }
    }

    bool isKeyDown(EditorContext& editor, Key key) {
        ImGuiKey k = imguiKeyOf(key);
        return editor.engine.input.isDown(key) || (k != ImGuiKey_None && ImGui::IsKeyDown(k));
    }

    /// Bind by pressing: the next key or controller button pressed joins the action.
    bool listen(EditorContext& editor, InputAction& action) {
        for (int k = 1; k < int(Key::Count); k++) {
            ImGuiKey key = imguiKeyOf(Key(k));
            if (key == ImGuiKey_None || !ImGui::IsKeyPressed(key, false)) continue;
            if (Key(k) == Key::Escape) {
                listening_ = -1;
                return false;
            }
            if (std::find(action.keys.begin(), action.keys.end(), Key(k)) == action.keys.end()) action.keys.push_back(Key(k));
            listening_ = -1;
            return true;
        }
        for (int b = 0; b < int(GamepadButton::Count); b++) {
            if (!editor.engine.input.wasPressed(GamepadButton(b))) continue;
            if (std::find(action.buttons.begin(), action.buttons.end(), GamepadButton(b)) == action.buttons.end()) action.buttons.push_back(GamepadButton(b));
            listening_ = -1;
            return true;
        }
        return false;
    }

    /// The controller and the keyboard as pictures, lit as they are pressed, and the screen's touches.
    void drawDevices(EditorContext& editor) {
        Input& input = editor.engine.input;
        const InputAction* chosen = chosen_ >= 0 && chosen_ < int(map_.actions.size()) ? &map_.actions[size_t(chosen_)] : nullptr;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImU32 body = ImGui::GetColorU32(ImGuiCol_FrameBg), edge = ImGui::GetColorU32(ImGuiCol_Border), text = ImGui::GetColorU32(ImGuiCol_Text);
        ImU32 lit = ImGui::GetColorU32(ImGuiCol_ButtonActive), bound = IM_COL32(255, 160, 60, 255);
        float room = ImGui::GetContentRegionAvail().x;
        bool sideBySide = room > 720;

        // ---- the controller
        ImGui::BeginGroup();
        bool connected = input.isGamepadConnected();
        ImGui::TextUnformatted("Controller");
        ImGui::SameLine();
        ImGui::TextColored(connected ? ImVec4(0.45f, 0.85f, 0.45f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), connected ? "connected" : "not connected");
        ImVec2 o = ImGui::GetCursorScreenPos();
        float w = 300, h = 170;
        ImGui::Dummy(ImVec2(w, h));
        auto isBound = [&](GamepadButton button) { return chosen && std::find(chosen->buttons.begin(), chosen->buttons.end(), button) != chosen->buttons.end(); };
        auto button = [&](ImVec2 c, float r, GamepadButton which, const char* label) {
            bool down = input.isDown(which);
            draw->AddCircleFilled(c, r, down ? lit : ImGui::GetColorU32(ImGuiCol_Button), 16);
            draw->AddCircle(c, r, isBound(which) ? bound : edge, 16, isBound(which) ? 2.0f : 1.0f);
            if (label) {
                ImVec2 size = ImGui::CalcTextSize(label);
                draw->AddText(ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), text, label);
            }
        };
        // Shoulders and triggers.
        float lt = input.axis(GamepadAxis::LeftTrigger), rt = input.axis(GamepadAxis::RightTrigger);
        draw->AddRectFilled(ImVec2(o.x + 40, o.y + 2), ImVec2(o.x + 100, o.y + 12), body, 4.0f);
        draw->AddRectFilled(ImVec2(o.x + 40, o.y + 2), ImVec2(o.x + 40 + 60 * lt, o.y + 12), lit, 4.0f);
        draw->AddRectFilled(ImVec2(o.x + w - 100, o.y + 2), ImVec2(o.x + w - 40, o.y + 12), body, 4.0f);
        draw->AddRectFilled(ImVec2(o.x + w - 100, o.y + 2), ImVec2(o.x + w - 100 + 60 * rt, o.y + 12), lit, 4.0f);
        for (int side = 0; side < 2; side++) {
            GamepadButton which = side ? GamepadButton::RightShoulder : GamepadButton::LeftShoulder;
            ImVec2 a(o.x + (side ? w - 104 : 36), o.y + 16), b(a.x + 68, a.y + 10);
            draw->AddRectFilled(a, b, input.isDown(which) ? lit : ImGui::GetColorU32(ImGuiCol_Button), 4.0f);
            draw->AddRect(a, b, isBound(which) ? bound : edge, 4.0f, 0, isBound(which) ? 2.0f : 1.0f);
        }
        // The body, with its grips.
        draw->AddRectFilled(ImVec2(o.x + 20, o.y + 30), ImVec2(o.x + w - 20, o.y + 120), body, 40.0f);
        draw->AddCircleFilled(ImVec2(o.x + 62, o.y + 120), 38, body, 24);
        draw->AddCircleFilled(ImVec2(o.x + w - 62, o.y + 120), 38, body, 24);
        // Sticks: where they are pushed.
        auto stick = [&](ImVec2 c, GamepadAxis x, GamepadAxis y, GamepadButton press) {
            draw->AddCircleFilled(c, 20, ImGui::GetColorU32(ImGuiCol_WindowBg), 20);
            bool isAxisBound = chosen && chosen->isAxis && (chosen->gamepadAxis == x || chosen->gamepadAxis == y);
            draw->AddCircle(c, 20, isAxisBound ? bound : edge, 20, isAxisBound ? 2.0f : 1.0f);
            ImVec2 knob(c.x + input.axis(x) * 12, c.y - input.axis(y) * 12);
            draw->AddCircleFilled(knob, 11, input.isDown(press) ? lit : ImGui::GetColorU32(ImGuiCol_Button), 16);
        };
        stick(ImVec2(o.x + 75, o.y + 68), GamepadAxis::LeftX, GamepadAxis::LeftY, GamepadButton::LeftStick);
        stick(ImVec2(o.x + w - 115, o.y + 108), GamepadAxis::RightX, GamepadAxis::RightY, GamepadButton::RightStick);
        // The d-pad.
        ImVec2 pad(o.x + 115, o.y + 108);
        struct Arm { GamepadButton button; float x, y; };
        for (Arm arm : {Arm{GamepadButton::DpadUp, 0, -1}, Arm{GamepadButton::DpadDown, 0, 1}, Arm{GamepadButton::DpadLeft, -1, 0}, Arm{GamepadButton::DpadRight, 1, 0}}) {
            ImVec2 c(pad.x + arm.x * 12, pad.y + arm.y * 12);
            ImVec2 a(c.x - 6, c.y - 6), b(c.x + 6, c.y + 6);
            draw->AddRectFilled(a, b, input.isDown(arm.button) ? lit : ImGui::GetColorU32(ImGuiCol_Button), 2.0f);
            draw->AddRect(a, b, isBound(arm.button) ? bound : edge, 2.0f, 0, isBound(arm.button) ? 2.0f : 1.0f);
        }
        // A B X Y.
        ImVec2 face(o.x + w - 75, o.y + 66);
        button(ImVec2(face.x, face.y + 16), 9, GamepadButton::A, "A");
        button(ImVec2(face.x + 16, face.y), 9, GamepadButton::B, "B");
        button(ImVec2(face.x - 16, face.y), 9, GamepadButton::X, "X");
        button(ImVec2(face.x, face.y - 16), 9, GamepadButton::Y, "Y");
        button(ImVec2(o.x + w * 0.5f - 18, o.y + 62), 6, GamepadButton::Back, nullptr);
        button(ImVec2(o.x + w * 0.5f + 18, o.y + 62), 6, GamepadButton::Start, nullptr);
        ImGui::EndGroup();

        if (sideBySide) ImGui::SameLine(0, 24);

        // ---- the keyboard
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Keyboard");
        ImVec2 k0 = ImGui::GetCursorScreenPos();
        const float unit = 26, gap = 3;
        struct Cap { const char* label; Key key; float width; };
        std::vector<std::vector<Cap>> rows = {
            {{"Esc", Key::Escape, 1.2f}, {"F1", Key::F1, 1}, {"F2", Key::F2, 1}, {"F3", Key::F3, 1}, {"F4", Key::F4, 1}, {"F5", Key::F5, 1}, {"F6", Key::F6, 1},
             {"F7", Key::F7, 1}, {"F8", Key::F8, 1}, {"F9", Key::F9, 1}, {"F10", Key::F10, 1}, {"F11", Key::F11, 1}, {"F12", Key::F12, 1}},
            {{"1", Key::Num1, 1}, {"2", Key::Num2, 1}, {"3", Key::Num3, 1}, {"4", Key::Num4, 1}, {"5", Key::Num5, 1}, {"6", Key::Num6, 1}, {"7", Key::Num7, 1},
             {"8", Key::Num8, 1}, {"9", Key::Num9, 1}, {"0", Key::Num0, 1}, {"Del", Key::Backspace, 1.6f}},
            {{"Tab", Key::Tab, 1.5f}, {"Q", Key::Q, 1}, {"W", Key::W, 1}, {"E", Key::E, 1}, {"R", Key::R, 1}, {"T", Key::T, 1}, {"Y", Key::Y, 1}, {"U", Key::U, 1},
             {"I", Key::I, 1}, {"O", Key::O, 1}, {"P", Key::P, 1}},
            {{"", Key::Unknown, 1.8f}, {"A", Key::A, 1}, {"S", Key::S, 1}, {"D", Key::D, 1}, {"F", Key::F, 1}, {"G", Key::G, 1}, {"H", Key::H, 1}, {"J", Key::J, 1},
             {"K", Key::K, 1}, {"L", Key::L, 1}, {"Ret", Key::Enter, 1.8f}},
            {{"Shift", Key::LeftShift, 2.3f}, {"Z", Key::Z, 1}, {"X", Key::X, 1}, {"C", Key::C, 1}, {"V", Key::V, 1}, {"B", Key::B, 1}, {"N", Key::N, 1}, {"M", Key::M, 1},
             {"Shift", Key::RightShift, 2.3f}},
            {{"Ctrl", Key::LeftControl, 1.4f}, {"Alt", Key::LeftAlt, 1.4f}, {"Space", Key::Space, 6.0f}, {"Alt", Key::RightAlt, 1.4f}, {"<", Key::Left, 1},
             {"^", Key::Up, 1}, {"v", Key::Down, 1}, {">", Key::Right, 1}},
        };
        auto keyBound = [&](Key key) {
            if (!chosen || key == Key::Unknown) return false;
            if (chosen->isAxis) return key == chosen->negative || key == chosen->positive || key == chosen->negativeAlt || key == chosen->positiveAlt;
            return std::find(chosen->keys.begin(), chosen->keys.end(), key) != chosen->keys.end();
        };
        float y = k0.y, widest = 0;
        for (const auto& row : rows) {
            float x = k0.x;
            for (const Cap& cap : row) {
                float cw = unit * cap.width + gap * (cap.width - 1);
                ImVec2 a(x, y), b(x + cw, y + unit);
                bool down = cap.key != Key::Unknown && isKeyDown(editor, cap.key);
                if (cap.label[0]) {
                    draw->AddRectFilled(a, b, down ? lit : ImGui::GetColorU32(ImGuiCol_Button), 4.0f);
                    draw->AddRect(a, b, keyBound(cap.key) ? bound : edge, 4.0f, 0, keyBound(cap.key) ? 2.0f : 1.0f);
                    ImVec2 size = ImGui::CalcTextSize(cap.label);
                    draw->AddText(ImVec2(a.x + (cw - size.x) * 0.5f, a.y + (unit - size.y) * 0.5f), text, cap.label);
                }
                x += cw + gap;
            }
            widest = std::max(widest, x - k0.x);
            y += unit + gap;
        }
        ImGui::Dummy(ImVec2(widest, y - k0.y));
        ImGui::EndGroup();

        // ---- touches: on a phone, or the touch controls of the scene
        const std::vector<Input::Touch>& touches = input.touches();
        if (!touches.empty()) {
            ImGui::TextDisabled("Touches");
            ImVec2 t0 = ImGui::GetCursorScreenPos();
            ImVec2 screen = ImGui::GetIO().DisplaySize;
            float scale = 200.0f / std::max(screen.x, 1.0f);
            draw->AddRect(t0, ImVec2(t0.x + 200, t0.y + screen.y * scale), edge, 8.0f);
            for (const Input::Touch& touch : touches) draw->AddCircleFilled(ImVec2(t0.x + touch.position.x * scale, t0.y + touch.position.y * scale), 7, lit);
            ImGui::Dummy(ImVec2(200, screen.y * scale));
        }
        // A joystick that is not a game controller (a radio, a flight stick): its axes by number.
        const JoystickState& joystick = input.joystick();
        if (joystick.connected) {
            ui::subheading(("Joystick: " + joystick.name).c_str());
            for (size_t a = 0; a < joystick.axes.size(); a++) {
                ImGui::Text("Axis %d", int(a));
                ImGui::SameLine(70);
                ImGui::ProgressBar(joystick.axes[a] * 0.5f + 0.5f, ImVec2(-FLT_MIN, 8), "");
            }
        }
        ImGui::TextDisabled(chosen ? "In orange: what the chosen action is bound to." : "Choose an action below to see what it is bound to, in orange.");
    }

    static std::vector<std::string> keyNames() {
        std::vector<std::string> names = {"None"};
        for (int k = 1; k < int(Key::Count); k++) names.push_back(keyName(Key(k)));
        return names;
    }

    static bool keyChoice(const char* label, Key& key) {
        int index = int(key);               // Key::Unknown is 0, and so is "None"
        if (!ui::combo(label, index, keyNames())) return false;
        key = Key(index);
        return true;
    }

    /// A list shown as small buttons: click one to remove it, "+" to add.
    template <class T, class NameOf> static bool chips(const char* label, std::vector<T>& values, const std::vector<std::string>& choices, NameOf nameOf, int firstChoice) {
        bool changed = false;
        ui::label(label);
        ImGui::PushID(label);
        int removed = -1;
        for (int i = 0; i < int(values.size()); i++) {
            ImGui::PushID(i);
            if (ImGui::SmallButton(nameOf(values[size_t(i)]))) removed = i;
            ui::tooltip("Click to remove");
            ImGui::PopID();
            ImGui::SameLine(0, 4);
        }
        if (removed >= 0) {
            values.erase(values.begin() + removed);
            changed = true;
        }
        if (ImGui::SmallButton("+")) ImGui::OpenPopup("##add");
        if (ImGui::BeginPopup("##add")) {
            for (int c = 0; c < int(choices.size()); c++) {
                if (ImGui::MenuItem(choices[size_t(c)].c_str())) {
                    T value = T(c + firstChoice);
                    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    static bool keyList(const char* label, std::vector<Key>& keys) {
        std::vector<std::string> names;
        for (int k = 1; k < int(Key::Count); k++) names.push_back(keyName(Key(k)));
        return chips(label, keys, names, [](Key key) { return keyName(key); }, 1);
    }
    static bool buttonList(const char* label, std::vector<GamepadButton>& buttons) {
        std::vector<std::string> names;
        for (int b = 0; b < int(GamepadButton::Count); b++) names.push_back(gamepadButtonName(GamepadButton(b)));
        return chips(label, buttons, names, [](GamepadButton button) { return gamepadButtonName(button); }, 0);
    }
    static bool mouseList(const char* label, std::vector<MouseButton>& buttons) {
        static const char* const kNames[] = {"Left", "Right", "Middle"};
        return chips(label, buttons, {"Left", "Right", "Middle"}, [](MouseButton button) { return kNames[size_t(button)]; }, 0);
    }

    InputMap map_;
    std::string loadedFor_;
    int chosen_ = -1, listening_ = -1;
};

}  // namespace

void registerRenderingPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<LightingPanel>());
    editor.registry.addPanel(nullptr, std::make_unique<RenderingPanel>());
}

void drawInputSettings(EditorContext& editor) {
    static InputPanel panel;            // what it has open is kept while the editor runs
    panel.draw(editor);
}

}  // namespace molten::editor
