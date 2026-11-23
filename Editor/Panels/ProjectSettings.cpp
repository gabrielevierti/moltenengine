// ProjectSettings.cpp
// Edit > Project Settings: what a project decides once for the whole game, a page for each
// kind of thing, as in Unity. Game (its name, version and first scene), Quality (how well it
// draws), Input (the actions and what moves them), Physics (gravity, the step, which layers
// collide) and Tags and Layers.
//
// Each page writes its own file as it is changed: the .molten file, Settings/Quality.settings,
// Assets/Input.actions, Settings/Game.settings.
#include "Editor.hpp"

#include "Core/Paths.hpp"
#include "Core/TextFile.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <iterator>

namespace molten::editor {

void saveGameSettings(EditorContext& editor, const GameSettings& settings) {
    editor.engine.applySettings(settings);
    if (!editor.hasProject) return;
    std::error_code code;
    std::filesystem::create_directories(editor.project.settingsDirectory(), code);
    if (!text::writeFile(editor.project.settingsDirectory() + "/Game.settings", settings.toText())) {
        editor.notify("Cannot write Settings/Game.settings", ConsoleMessage::Level::Error);
    }
}

namespace {
/// Under a value that something else makes do nothing: why, so that nothing set here is silently ignored.
static void ignoredNote(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.0f), "%s", text);
    ImGui::PopTextWrapPos();
}


class ProjectSettingsPanel final : public Panel {
public:
    ProjectSettingsPanel() {
        title = "Project Settings";
        isOpen = false;
    }

    ImGuiWindowFlags windowFlags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
    ImVec2 firstSize() const override { return ImVec2(860, 620); }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("No project is open.");
            return;
        }
        static const char* const pages[][3] = {
            {"Game", "gamepad-2", "What the game is: its name, version, icon and the scene it starts with. File > Build Settings makes it into an application."},
            {"Quality", "gauge", "How well the game draws as it ships: the same for every scene."},
            {"Input", "keyboard", "The actions the game asks about, and the keys, buttons and sticks that move them."},
            {"Physics", "atom", "Gravity, how often physics steps, and which layers collide with which."},
            {"Tags and Layers", "tag", "The tags and the layers that the things of this project can be given."},
        };
        const int count = int(std::size(pages));
        if (!editor.projectSettingsPage.empty()) {
            for (int i = 0; i < count; i++) {
                if (editor.projectSettingsPage == pages[i][0]) page_ = i;
            }
            editor.projectSettingsPage.clear();
        }

        // The pages at the left, as in Unity's own Project Settings.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
        ImGui::BeginChild("##pages", ImVec2(176, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        for (int i = 0; i < count; i++) {
            if (ui::iconSelectable(pages[i][0], pages[i][1], page_ == i)) page_ = i;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ui::title(pages[page_][0]);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", pages[page_][2]);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        switch (page_) {
            case 0: drawGame(editor); break;
            case 1: drawQuality(editor); break;
            case 2: drawInputSettings(editor); break;
            case 3: drawPhysics(editor); break;
            case 4: drawTagsAndLayers(editor); break;
        }
        ImGui::EndChild();
    }

private:
    static void drawGame(EditorContext& editor) {
        Project& project = editor.project;
        bool changed = ui::text("Name", project.name);
        changed |= ui::text("Version", project.version);
        changed |= ui::text("Identifier", project.identifier);
        ui::tooltip("The bundle identifier of the game, like com.yourname.game: what the App Store and the system know it by");

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
        ui::tooltip("The scene the exported game opens on");
        changed |= ui::checkbox("Splash Screen", project.showsSplash);
        ui::tooltip("The exported game opens with the engine's splash screen");
        {
            // Its icon: the engine's until the game has one of its own.
            std::string iconFile = project.iconFile(findRootDirectory());
            ImTextureID icon = editor.pictureAt ? editor.pictureAt(iconFile) : ImTextureID(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Icon");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f + ImGui::GetCursorPosX() - ImGui::GetStyle().ItemSpacing.x - ImGui::CalcTextSize("Icon").x);
            if (icon) ImGui::Image(icon, ImVec2(56, 56));
            else ImGui::Dummy(ImVec2(56, 56));
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Button("Choose a Picture...")) {
                std::vector<std::string> chosen = platform::chooseFiles("Choose a square picture for the game's icon (1024 x 1024 is best)", {"png", "jpg", "jpeg"}, false, {});
                if (!chosen.empty()) {
                    // Kept in the project, so that it travels with it.
                    std::string name = "Icon" + std::filesystem::path(chosen.front()).extension().string();
                    std::error_code code;
                    std::filesystem::copy_file(chosen.front(), std::filesystem::path(project.assetsDirectory()) / name, std::filesystem::copy_options::overwrite_existing, code);
                    if (code) {
                        editor.notify("Cannot copy the picture: " + code.message(), ConsoleMessage::Level::Error);
                    } else {
                        project.icon = name;
                        changed = true;
                        editor.assets.refresh();
                    }
                }
            }
            ImGui::BeginDisabled(project.icon.empty());
            if (ImGui::Button("Use the Engine's")) {
                project.icon.clear();
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::EndGroup();
        }
        ui::info("Folder", project.directory);
        ImGui::Spacing();
        if (ImGui::Button("Show in Finder")) platform::revealInFinder(project.directory);

        if (changed) {
            if (project.name.empty()) project.name = "Untitled";
            std::string error;
            if (!project.save(&error)) editor.notify(error, ConsoleMessage::Level::Error);
        }
    }

    /// How well the game draws, as it ships: the same for every scene. Settings/Quality.settings.
    ///
    /// The level of the whole at the top; under it the seven groups, each a row that says in a
    /// line what it comes to and can be put at a level of its own with the four small buttons
    /// at its right, or opened to set its values one by one.
    static void drawQuality(EditorContext& editor) {
        using Level = GameQuality::Level;
        using Group = GameQuality::Group;
        GameQuality quality = editor.engine.quality ? *editor.engine.quality : GameQuality{};
        bool changed = false;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 accent = ImGui::GetColorU32(ImGuiCol_ButtonActive);

        // ---- the level of the whole
        float width = (ImGui::GetContentRegionAvail().x - 3 * 6) / 4;
        for (int level = 0; level < 4; level++) {
            if (level > 0) ImGui::SameLine(0, 6);
            bool isOn = int(quality.level) == level;
            ImGui::PushStyleColor(ImGuiCol_Button, isOn ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button(GameQuality::levelName(Level(level)), ImVec2(width, 30))) {
                quality = GameQuality::preset(Level(level));
                changed = true;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ui::tooltip(GameQuality::levelPurpose(Level(level)));
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", GameQuality::levelPurpose(quality.level));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        // ---- the groups
        static const char* const icons[GameQuality::kGroupCount] = {"scaling", "grid-3x3", "sun-medium", "orbit", "sparkles", "cloud-sun", "trees"};
        static const char* const about[GameQuality::kGroupCount] = {
            "How many pixels the scene is drawn with before the picture is made the screen's size. Fewer is the surest way to a faster frame.",
            "How smooth the edges of things are.",
            "How far from the camera the sun's shadows reach, and how sharp they are. A scene may ask for less far.",
            "All of the light found with rays, where the GPU can: the sun's shadows, the light around things and what it bounces, reflections. Off, or how well. For every scene.",
            "The screen's ambient occlusion, bloom, and shafts of light through fog, where a scene has them.",
            "Clouds with depth, where a scene has them, and how finely they are walked through.",
            "How far the detail of far things lasts: the levels of LOD groups, painted foliage.",
        };
        static bool isOpen[GameQuality::kGroupCount] = {};
        const float rowHeight = 44, levelWidth = 30, levelsWidth = levelWidth * 4 + 3;
        for (int index = 0; index < GameQuality::kGroupCount; index++) {
            Group group = Group(index);
            Level at = quality.levelOf(group);
            ImGui::PushID(index);
            ImVec2 corner = ImGui::GetCursorScreenPos();
            float rowWidth = ImGui::GetContentRegionAvail().x;
            // Ray tracing has one more choice before its levels: none at all.
            const bool isRayTracing = group == Group::RayTracing;
            const float offWidth = isRayTracing ? 38.0f : 0.0f;
            // The row: a click opens it.
            if (ImGui::InvisibleButton("##row", ImVec2(rowWidth - levelsWidth - offWidth - 14, rowHeight))) isOpen[index] = !isOpen[index];
            bool isHovered = ImGui::IsItemHovered();
            if (isHovered) ui::tooltip(about[index]);
            draw->AddRectFilled(corner, ImVec2(corner.x + rowWidth, corner.y + rowHeight), ImGui::GetColorU32(isHovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 6);
            // An arrow, the group's icon, its name, and what it comes to.
            float middle = corner.y + rowHeight * 0.5f;
            ImVec2 arrow(corner.x + 14, middle);
            ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled), text = ImGui::GetColorU32(ImGuiCol_Text);
            if (isOpen[index]) draw->AddTriangleFilled(ImVec2(arrow.x - 4, arrow.y - 2.5f), ImVec2(arrow.x + 4, arrow.y - 2.5f), ImVec2(arrow.x, arrow.y + 3.5f), dim);
            else draw->AddTriangleFilled(ImVec2(arrow.x - 2.5f, arrow.y - 4), ImVec2(arrow.x + 3.5f, arrow.y), ImVec2(arrow.x - 2.5f, arrow.y + 4), dim);
            ui::drawIcon(draw, ImVec2(corner.x + 38, middle), 15, icons[index], at == Level::Custom ? accent : dim);
            float lineHeight = ImGui::GetTextLineHeight();
            draw->AddText(ImVec2(corner.x + 58, middle - lineHeight - 1), text, GameQuality::groupName(group));
            std::string summary = quality.summaryOf(group);
            ImGui::PushClipRect(corner, ImVec2(corner.x + rowWidth - levelsWidth - offWidth - 18, corner.y + rowHeight), true);
            draw->AddText(ImVec2(corner.x + 58, middle + 2), dim, summary.c_str());
            ImGui::PopClipRect();

            // Its level: four small buttons, joined, the one it is at lit (two, where two levels
            // have the same values for it). None lit: its values are no level's.
            static const char* const letters[4] = {"L", "M", "H", "U"};
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
            if (isRayTracing) {
                // Off, and then how well: each of the four traces all of the light.
                ImGui::SetCursorScreenPos(ImVec2(corner.x + rowWidth - levelsWidth - 8 - offWidth - 1, middle - 12));
                ImGui::PushStyleColor(ImGuiCol_Button, !quality.rayTraced ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
                if (ImGui::Button("Off", ImVec2(offWidth, 24))) {
                    quality.rayTraced = false;
                    quality.findLevel();
                    changed = true;
                }
                ImGui::PopStyleColor();
                ui::tooltip("No ray tracing: shadow maps (the Shadows group), the screen's ambient occlusion, reflections of the sky");
            }
            for (int level = 0; level < 4; level++) {
                ImGui::SetCursorScreenPos(ImVec2(corner.x + rowWidth - levelsWidth - 8 + float(level) * (levelWidth + 1), middle - 12));
                bool isOn = isRayTracing ? quality.rayTraced && quality.rayTracing == LightingQuality(level) : quality.isAt(group, Level(level));
                ImGui::PushStyleColor(ImGuiCol_Button, isOn ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
                ImGui::PushID(level);
                if (ImGui::Button(letters[level], ImVec2(levelWidth, 24))) {
                    if (isRayTracing) {
                        quality.rayTraced = true;
                        quality.setRayTracing(LightingQuality(level));
                        quality.findLevel();
                    } else {
                        quality.setGroup(group, Level(level));
                    }
                    changed = true;
                }
                ImGui::PopID();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    GameQuality shown = quality;
                    if (isRayTracing) shown.rayTraced = true, shown.setRayTracing(LightingQuality(level));
                    else shown.setGroup(group, Level(level));
                    ui::tooltip((isRayTracing ? shown.summaryOf(group) : std::string(GameQuality::levelName(Level(level))) + ": " + shown.summaryOf(group)).c_str());
                }
            }
            ImGui::PopStyleVar(2);
            ImGui::SetCursorScreenPos(ImVec2(corner.x, corner.y + rowHeight + 4));
            ImGui::Dummy(ImVec2(0, 0));

            // ---- opened: its values, one by one
            if (isOpen[index]) {
                ImGui::Indent(18);
                ImGui::Spacing();
                bool edited = false;
                switch (group) {
                    case Group::Resolution: {
                        edited |= ui::sliderFloat("Render Scale", quality.renderScale, 0.25f, 2.0f);
                        ui::tooltip("Below 1 the scene is drawn smaller and made the screen's size: faster, a little softer");
                        edited |= ui::checkbox("MetalFX Upscaling", quality.metalFX);
                        ui::tooltip("A smaller picture is made the screen's size by MetalFX, which keeps it sharp, instead of being stretched");
                        if (quality.metalFX && quality.renderScale >= 0.999f && !quality.dynamicResolution && !quality.followsDeviceHeat) {
                            ignoredNote("At a Render Scale of 1, never drawn smaller: nothing to make larger.");
                        }
                        edited |= ui::checkbox("Dynamic Resolution", quality.dynamicResolution);
                        ui::tooltip("While the GPU cannot keep up with the frame rate below, the scene is drawn smaller, and larger again when it can. A scene that traces rays is the first to need it");
                        if (quality.dynamicResolution) {
                            edited |= ui::dragFloat("Frame Rate to Keep", quality.targetFrameRate, 1.0f, 20.0f, 240.0f, "%.0f fps");
                            edited |= ui::sliderFloat("Smallest", quality.lowestScale, 0.25f, 1.0f);
                            ui::tooltip("Never smaller than this share of the render scale above");
                        }
                        edited |= ui::checkbox("Smaller When Hot", quality.followsDeviceHeat);
                        ui::tooltip("On a phone or a laptop that gets hot, the scene is drawn smaller until it cools");
                        break;
                    }
                    case Group::AntiAliasing: {
                        int aa = quality.antiAliasing >= 4 ? 2 : quality.antiAliasing >= 2 ? 1 : 0;
                        if (ui::combo("Multisampling", aa, {"Off", "2x", "4x"})) {
                            quality.antiAliasing = aa == 2 ? 4 : aa == 1 ? 2 : 1;
                            edited = true;
                        }
                        break;
                    }
                    case Group::Shadows: {
                        edited |= ui::checkbox("Shadows", quality.shadows);
                        {
                            float percent = quality.shadowReach * 100.0f;
                            if (ui::dragFloat("Reach##q", percent, 1.0f, 25.0f, 200.0f, "%.0f%%")) {
                                quality.shadowReach = percent / 100.0f;
                                edited = true;
                            }
                        }
                        ui::tooltip("How far the sun's shadows reach, as a share of how far each scene asks (Lighting > Shadows > Distance): "
                                    "50% half as far, which costs less; 150% farther, sharper far away");
                        int resolution = quality.shadowResolution >= 4096 ? 2 : quality.shadowResolution >= 2048 ? 1 : 0;
                        if (ui::combo("Resolution", resolution, {"1024", "2048", "4096"})) {
                            quality.shadowResolution = resolution == 2 ? 4096 : resolution == 1 ? 2048 : 1024;
                            edited = true;
                        }
                        edited |= ui::dragInt("Slices", quality.shadowCascades, 0.05f, 1, 4);
                        ui::tooltip("Slices of the view, each with its shadow map: more keeps shadows sharp both near and far");
                        if (quality.rayTraced && editor.engine.renderer().canTraceRays()) {
                            ignoredNote("Ray Tracing is on: the sun's shadow is traced. Its maps (Resolution, Slices) are drawn only for the light shafts in the fog.");
                        }
                        edited |= ui::dragInt("Lights", quality.shadowedLights, 0.05f, 0, 48);
                        ui::tooltip("Point and spot lights that cast shadows (those with Casts Shadows on): at most this many at once, "
                                    "the nearest and brightest first. A spot light draws one shadow map, a point light six");
                        int lightResolution = quality.lightShadowResolution >= 1024 ? 2 : quality.lightShadowResolution >= 512 ? 1 : 0;
                        if (ui::combo("Light Resolution", lightResolution, {"256", "512", "1024"})) {
                            quality.lightShadowResolution = lightResolution == 2 ? 1024 : lightResolution == 1 ? 512 : 256;
                            edited = true;
                        }
                        break;
                    }
                    case Group::RayTracing: {
                        // All of it here, for every scene: what is found with rays, and how many.
                        const bool canTrace = editor.engine.renderer().canTraceRays();
                        if (!canTrace) {
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextDisabled("%s", editor.engine.renderer().device().supportsRayTracing()
                                                          ? "The ray tracing shaders did not compile here (see the console): shadow maps are used."
                                                          : "This GPU does not trace rays: shadow maps are used here, whatever is chosen. Games keep the choice for the GPUs that can.");
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("%s", quality.rayTraced
                                                      ? "On, all of the light is found with rays: the sun's shadows, sharp where things touch and soft away from them; the light around things, hidden under a car or in a corner, and the light a sunlit wall throws on the floor; reflections of what is around. A higher level sends more rays along shadows' edges and for the light around things, and mirrors rougher things."
                                                      : "Off: shadow maps (the Shadows group), the screen's own ambient occlusion, and reflections of the sky.");
                        ImGui::PopTextWrapPos();
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("About a ray and a half a pixel. With Dynamic Resolution on (Resolution, above) a GPU that cannot keep up draws fewer pixels, not fewer frames.");
                        ImGui::PopTextWrapPos();
                        break;
                    }
                    case Group::Effects: {
                        edited |= ui::checkbox("Ambient Occlusion##q", quality.ambientOcclusion);
                        if (quality.rayTraced && editor.engine.renderer().canTraceRays()) {
                            ignoredNote("Ray Tracing is on: the rays find what hides the sky from each pixel. This, the screen's own, is not used.");
                        }
                        edited |= ui::checkbox("Bloom##q", quality.bloom);
                        edited |= ui::dragInt("Light Shaft Steps", quality.lightShafts, 0.2f, 0, 48);
                        ui::tooltip("The most steps a scene's shafts of light are walked in: 0 none");
                        break;
                    }
                    case Group::Sky: {
                        edited |= ui::checkbox("Clouds with Depth", quality.volumetricClouds);
                        ui::tooltip("Off: every scene's clouds are a flat layer");
                        if (quality.volumetricClouds) {
                            edited |= ui::dragInt("Steps", quality.cloudSteps, 0.2f, 16, 128);
                            ui::tooltip("How many steps each line of sight takes through the clouds: more is finer and slower");
                            edited |= ui::sliderFloat("Size", quality.cloudResolution, 0.25f, 1.0f);
                            ui::tooltip("The clouds are walked through into a picture this share of the scene's size. They are soft: half shows no loss and costs a quarter");
                        }
                        break;
                    }
                    case Group::Detail: {
                        edited |= ui::sliderFloat("Distance", quality.detailDistance, 0.25f, 3.0f);
                        ui::tooltip("0.5: detail lasts half as far; 2: twice");
                        break;
                    }
                }
                if (edited) {
                    quality.findLevel();
                    changed = true;
                }
                ImGui::Spacing();
                ImGui::Unindent(18);
            }
            ImGui::PopID();
        }
        if (quality.level == Level::Custom) {
            ImGui::Spacing();
            ImGui::TextDisabled("L M H U put one group at a level. A group with none lit has values of its own.");
        }
        if (changed) {
            editor.engine.setQuality(quality);
            std::error_code code;
            std::filesystem::create_directories(editor.project.settingsDirectory(), code);
            if (!text::writeFile(editor.project.settingsDirectory() + "/Quality.settings", quality.toText())) editor.notify("Cannot write Settings/Quality.settings", ConsoleMessage::Level::Error);
        }
    }

    static void drawPhysics(EditorContext& editor) {
        GameSettings settings = editor.engine.settings;
        bool changed = ui::dragVec3("Gravity", settings.gravity, 0.05f, "%.2f");
        ui::tooltip("The pull on every body, in meters per second each second. Earth's is 0 -9.81 0; a body's own Gravity Scale multiplies it");
        changed |= ui::dragInt("Steps per Second", settings.stepsPerSecond, 0.2f, 20, 240);
        ui::tooltip("How often physics moves things, and every script's fixedUpdate runs. 60 suits most games;\nmore is steadier for fast, small things and costs more");

        ui::subheading("Which Layers Collide");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("A tick where a row and a column cross: things on those two layers collide. Without it they pass through each other. "
                            "Name the layers in Tags and Layers; give a thing its layer at the top of the Inspector.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        std::vector<int> named;
        for (int i = 0; i < GameSettings::kLayerCount; i++) {
            if (!settings.layers[size_t(i)].empty()) named.push_back(i);
        }
        // A triangle: a pair is asked about once. The columns run the other way, as in Unity's.
        const float cell = ImGui::GetFrameHeight() + 4;
        float labelWidth = 0;
        for (int layer : named) labelWidth = std::max(labelWidth, ImGui::CalcTextSize(settings.layerName(layer).c_str()).x);
        labelWidth += 30 + 14;                  // the number before the name, and some air after it
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // The columns' names, written one under the other at a slant would need turned text: their numbers, and the name under the mouse.
        {
            ImVec2 at = ImGui::GetCursorScreenPos();
            for (size_t column = 0; column < named.size(); column++) {
                int layer = named[named.size() - 1 - column];
                std::string number = std::to_string(layer);
                float width = ImGui::CalcTextSize(number.c_str()).x;
                draw->AddText(ImVec2(at.x + labelWidth + float(column) * cell + (cell - 4 - width) * 0.5f, at.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), number.c_str());
            }
            ImGui::Dummy(ImVec2(labelWidth + float(named.size()) * cell, ImGui::GetTextLineHeight() + 4));
        }
        for (size_t row = 0; row < named.size(); row++) {
            int a = named[row];
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%2d", a);
            ImGui::SameLine(30);
            ImGui::TextUnformatted(settings.layerName(a).c_str());
            for (size_t column = 0; column + row < named.size(); column++) {
                int b = named[named.size() - 1 - column];
                ImGui::SameLine(labelWidth + float(column) * cell);
                bool meet = settings.layersMeet(a, b);
                ImGui::PushID(a * 64 + b);
                if (ImGui::Checkbox("##meet", &meet)) {
                    settings.setLayersMeet(a, b, meet);
                    changed = true;
                }
                ImGui::PopID();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s and %s %s", settings.layerName(a).c_str(), settings.layerName(b).c_str(), meet ? "collide" : "pass through each other");
                }
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("All Collide")) {
            settings.meets.fill(0xFFFFFFFFu);
            changed = true;
        }
        if (changed) saveGameSettings(editor, settings);
    }

    void drawTagsAndLayers(EditorContext& editor) {
        GameSettings settings = editor.engine.settings;
        bool changed = false;

        ui::subheading("Tags");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("A tag says what a thing is to the game: \"Player\", \"Enemy\". Scripts find things by it and ask about it when two things touch.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::BeginDisabled();
        std::string untagged = "Untagged";
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("##untagged", &untagged);
        ImGui::EndDisabled();
        int removed = -1;
        for (size_t i = 0; i < settings.tags.size(); i++) {
            ImGui::PushID(int(i));
            ImGui::SetNextItemWidth(260);
            ImGui::InputText("##tag", &settings.tags[i]);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (ImGui::Button("Remove")) removed = int(i);
            ui::tooltip("Things that have this tag keep it until they are given another");
            ImGui::PopID();
        }
        if (removed >= 0) {
            settings.tags.erase(settings.tags.begin() + removed);
            changed = true;
        }
        ImGui::SetNextItemWidth(260);
        bool entered = ImGui::InputTextWithHint("##new-tag", "A new tag", &newTag_, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        bool isNew = !newTag_.empty() && newTag_ != "Untagged" && std::find(settings.tags.begin(), settings.tags.end(), newTag_) == settings.tags.end();
        ImGui::BeginDisabled(!isNew);
        if ((ImGui::Button("Add Tag") || entered) && isNew) {
            settings.tags.push_back(newTag_);
            newTag_.clear();
            changed = true;
        }
        ImGui::EndDisabled();

        ui::subheading("Layers");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("A layer decides what a thing collides with (Physics, here) and what a ray or a camera's mask leaves out. There are 32; a layer is offered once it has a name.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        for (int i = 0; i < GameSettings::kLayerCount; i++) {
            ImGui::PushID(1000 + i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%2d", i);
            ImGui::SameLine(36);
            ImGui::SetNextItemWidth(260);
            // The first is every thing's layer until it is given another: its name stays.
            ImGui::BeginDisabled(i == 0);
            ImGui::InputTextWithHint("##layer", "No name: not offered", &settings.layers[size_t(i)]);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (changed) {
            // No two tags alike, and none empty.
            std::vector<std::string> tags;
            for (const std::string& tag : settings.tags) {
                if (!tag.empty() && tag != "Untagged" && std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
            }
            settings.tags = tags;
            saveGameSettings(editor, settings);
        }
    }

    int page_ = 0;
    std::string newTag_;
};

}  // namespace

void registerProjectPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<ProjectSettingsPanel>());
}

}  // namespace molten::editor
