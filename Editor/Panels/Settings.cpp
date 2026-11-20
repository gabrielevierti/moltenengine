// Two panels of settings: how the open scene is rendered, and the facts of the project.
#include "Editor.hpp"
#include "Core/TextFile.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>

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
        // Three pages, as lighting windows have: what the scene is lit by, the lights in it, and how shadows are made.
        if (ImGui::BeginTabBar("##pages")) {
            if (ImGui::BeginTabItem("Environment")) { page_ = 0; ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Lights")) { page_ = 1; ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Shadows")) { page_ = 2; ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        if (page_ == 1) {
            drawLights(editor, changed);
            if (changed) editor.scene.markChanged();
            return;
        }
        if (page_ == 0 && ui::section("Sun")) {
            if (sun.isValid()) {
                Light* light = editor.engine.scene.get<Light>(sun);
                changed |= ui::color("Color", light->color, false);
                changed |= ui::dragFloat("Intensity", light->intensity, 0.02f, 0.0f, 100.0f);
                changed |= ui::checkbox("Casts Shadows", light->castsShadows);
                if (ImGui::Button("Select the Sun")) {
                    editor.scene.select(sun);
                    editor.selectedAsset.clear();
                }
                ui::tooltip("Its direction is its rotation: turn it with the rotate tool");
            } else {
                ImGui::TextDisabled("The scene has no directional light.");
                ImGui::TextDisabled("Create > Light > Directional Light makes one.");
            }
        }
        if (page_ == 0 && ui::section("Sky")) {
            changed |= ui::color("Zenith", settings.sky.zenithColor, true);
            changed |= ui::color("Horizon", settings.sky.horizonColor, true);
            changed |= ui::color("Ground", settings.sky.groundColor, true);
            changed |= ui::dragFloat("Brightness", settings.sky.intensity, 0.01f, 0.0f, 100.0f);
            changed |= ui::dragFloat("Sun Disk", settings.sky.sunDiskIntensity, 0.1f, 0.0f, 1000.0f);
            changed |= ui::dragDegrees("Sun Size", settings.sky.sunAngularRadius, 0.1f, 20.0f);
            changed |= ui::sliderFloat("Clouds", settings.sky.cloudCoverage, 0.0f, 1.0f);
            changed |= ui::checkbox("Clouds Have Depth", settings.sky.cloudsHaveDepth);
            ui::tooltip("Clouds as volumes one looks through, thick and thin, lit from the sun's side. Costs more than the flat layer");
            changed |= ui::dragFloat("Cloud Scale", settings.sky.cloudScale, 0.01f, 0.05f, 20.0f);
            changed |= ui::dragVec2("Cloud Wind", settings.sky.cloudSpeed, 0.001f);
            changed |= ui::dragFloat("Sky Lighting", settings.environmentIntensity, 0.01f, 0.0f, 10.0f);
        }
        if (page_ == 0 && ui::section("Wind")) {
            changed |= ui::dragVec3("Velocity", settings.wind, 0.05f, "%.2f m/s");
            changed |= ui::sliderFloat("Gusts", settings.windGustiness, 0.0f, 1.0f);
            ImGui::TextDisabled("Carries particles with Wind Influence above zero.");
        }
        if (page_ == 0 && ui::section("Fog")) {
            changed |= ui::color("Color", settings.fogColor, true);
            changed |= ui::dragFloat("Density", settings.fogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
            changed |= ui::dragFloat("Thins With Height", settings.fogHeightFalloff, 0.001f, 0.0f, 2.0f, "%.3f");
            ui::tooltip("Above zero the fog lies low, as mist in a valley, and thins at this rate per meter of height");
            changed |= ui::dragFloat("Glow Toward Sun", settings.fogSunScatter, 0.02f, 0.0f, 20.0f);
            changed |= ui::dragInt("Light Shafts", settings.fogShaftSteps, 0.2f, 0, 48);
            ui::tooltip("Above zero the fog glows only where the sun reaches it: what stands in the sun's way casts beams of shadow through the mist. The number is how finely the fog is walked through; 12 to 24 is usual. Needs Glow Toward Sun above zero");
        }
        if (page_ == 2 && ui::section("Shadows")) {
            changed |= ui::checkbox("Enabled", settings.shadowsEnabled);
            changed |= ui::dragFloat("Distance", settings.cascades.shadowDistance, 0.5f, 1.0f, 5000.0f);
            changed |= ui::dragFloat("Bias", settings.shadowBias, 0.00001f, 0.0f, 0.01f, "%.5f");
            changed |= ui::dragFloat("Normal Offset", settings.shadowNormalOffset, 0.05f, 0.0f, 10.0f);
        }
        if (page_ == 2 && ui::section("Ray Tracing")) {
            bool canTrace = editor.engine.renderer().canTraceRays();
            ImGui::BeginDisabled(!canTrace);
            changed |= ui::checkbox("Sun Shadows", settings.rayTracedShadows);
            if (settings.rayTracedShadows) {
                changed |= ui::dragInt("Rays per Pixel", settings.shadowRayCount, 0.1f, 1, 16);
                ImGui::TextDisabled("Softness follows Sky > Sun Size.");
                // What else the same rays can answer, once the scene is there to be asked.
                bool hasExtras = editor.engine.renderer().hasExtras();
                ImGui::BeginDisabled(!hasExtras);
                changed |= ui::checkbox("Ambient Light", settings.rayTracedOcclusion);
                ui::tooltip("The sky's light is hidden by what is really in the way: under a car, in a corner");
                if (settings.rayTracedOcclusion) changed |= ui::dragInt("Ambient Rays", settings.occlusionRayCount, 0.1f, 1, 16);
                if (settings.rayTracedOcclusion) {
                    changed |= ui::checkbox("Bounced Light", settings.rayTracedBounce);
                    ui::tooltip("Global illumination, one bounce: the same rays bring back the light of what they hit, so a red wall in the sun tints the floor beside it");
                    if (settings.rayTracedBounce) changed |= ui::dragFloat("Bounce Strength", settings.bounceIntensity, 0.02f, 0.0f, 8.0f);
                }
                changed |= ui::checkbox("Reflections", settings.rayTracedReflections);
                ui::tooltip("Smooth surfaces mirror what is around them: each sends a ray and shows what it hits, in its color and light");
                ImGui::EndDisabled();
                if (!hasExtras) ImGui::TextDisabled("Not available: the extra shaders did not compile (see the console).");
            }
            ImGui::EndDisabled();
            if (!canTrace) {
                ImGui::TextWrapped(editor.engine.renderer().device().supportsRayTracing()
                                       ? "The ray tracing shaders did not compile on this system (see the console). Shadow maps are used."
                                       : "This GPU cannot trace rays. Shadow maps are used.");
            }
        }
        if (page_ == 2 && ui::section("Ambient Occlusion")) {
            changed |= ui::checkbox("Enabled##ao", settings.ambientOcclusionEnabled);
            changed |= ui::dragFloat("Radius", settings.ambientOcclusionRadius, 0.01f, 0.01f, 10.0f);
            changed |= ui::dragFloat("Strength", settings.ambientOcclusionIntensity, 0.01f, 0.0f, 4.0f);
            changed |= ui::dragFloat("Contrast", settings.ambientOcclusionPower, 0.01f, 0.1f, 8.0f);
        }
        // These settings are saved with the scene, so changing them is an edit like any other.
        if (changed) editor.scene.markChanged();
    }

private:
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
        if (count == 0) ImGui::TextDisabled("The scene has no lights. Create > Light makes one.");
        else ImGui::TextDisabled("%d lights. Click a name to select it; its direction and place are its Transform.", count);
    }

    int page_ = 0;
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
        if (ui::section("Quality")) {
            static const int sampleCounts[] = {1, 2, 4};
            int index = settings.sampleCount >= 4 ? 2 : settings.sampleCount >= 2 ? 1 : 0;
            if (ui::combo("Anti-Aliasing", index, {"Off", "2x MSAA", "4x MSAA"})) {
                settings.sampleCount = sampleCounts[index];
                changed = true;
            }
            changed |= ui::sliderFloat("Render Scale", settings.renderScale, 0.25f, 2.0f);
        }
        if (ui::section("Statistics")) {
            const auto& stats = editor.engine.renderer().stats;
            ui::info("Draw Calls", std::to_string(stats.drawCalls));
            ui::info("Frames per Second", std::to_string(int(editor.engine.time.framesPerSecond + 0.5f)));
        }
        if (changed) editor.scene.markChanged();
    }
};

/// Input: the actions of the game and what triggers each. Saved in Assets/Input.actions,
/// which the game ships with; code and components ask for actions by name.
class InputPanel final : public Panel {
public:
    InputPanel() { title = "Input"; }

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
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Actions are what the player can do. Code and components ask for them by name, so the keys can change here without touching the game.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        // A joystick that is plugged in: what it is, and what each of its axes reads, to find which is which.
        const JoystickState& joystick = editor.engine.input.joystick();
        if (joystick.connected) {
            ImGui::SeparatorText(("Joystick: " + joystick.name).c_str());
            for (size_t a = 0; a < joystick.axes.size(); a++) {
                ImGui::Text("Axis %d", int(a));
                ImGui::SameLine(70);
                ImGui::ProgressBar(joystick.axes[a] * 0.5f + 0.5f, ImVec2(-FLT_MIN, 8), "");
            }
            int held = 0;
            for (bool isDown : joystick.buttons) held += isDown ? 1 : 0;
            ImGui::TextDisabled("%d buttons, %d held. Move a stick to see which axis it is, then set it on an action below.", int(joystick.buttons.size()), held);
            ImGui::Spacing();
        } else {
            ImGui::TextDisabled("No joystick. A radio or a flight stick plugged in by USB appears here with its axes.");
            ImGui::Spacing();
        }

        bool changed = false;
        int removed = -1;
        for (int i = 0; i < int(map_.actions.size()); i++) {
            InputAction& action = map_.actions[size_t(i)];
            ImGui::PushID(i);
            std::string header = (action.name.empty() ? std::string("(no name)") : action.name) + (action.isAxis ? "   axis" : "   button") + "###action";
            bool isOpen = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24);
            if (ImGui::SmallButton("x")) removed = i;
            ui::tooltip("Remove this action");
            if (isOpen) {
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
                // What it reads right now: press the key and see.
                float value = action.isAxis ? editor.engine.input.axis(action.name) : (editor.engine.input.isDown(action.name) ? 1.0f : 0.0f);
                ui::label("Now");
                ImGui::ProgressBar(action.isAxis ? value * 0.5f + 0.5f : value, ImVec2(-FLT_MIN, 6), "");
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
        if (changed) {
            editor.engine.input.apply(map_);
            if (!text::writeFile(editor.assets.absolutePath("Input.actions"), map_.toText())) {
                editor.notify("Cannot write Input.actions", ConsoleMessage::Level::Error);
            }
        }
    }

private:
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
        changed |= ui::checkbox("Splash Screen", project.showsSplash);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The exported game opens with the engine's splash screen");
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
    editor.registry.addPanel(nullptr, std::make_unique<LightingPanel>());
    editor.registry.addPanel(nullptr, std::make_unique<RenderingPanel>());
    editor.registry.addPanel(nullptr, std::make_unique<InputPanel>());
}

void registerProjectPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<ProjectPanel>());
}

}  // namespace molten::editor
