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

    /// How well the game draws: the same for every scene. Settings/Quality.settings.
    ///
    /// The levels are the game's own: a row of them at the top (choose one to set it, + to add
    /// one), what is done with the one chosen under it (its name, whether the game starts at it,
    /// copy, move, delete), and then every value it has, in its seven groups. The editor draws at
    /// the level chosen here, so a change is seen at once.
    static void drawQuality(EditorContext& editor) {
        using Group = GameQuality::Group;
        QualityLevels levels = editor.engine.qualityLevelsAll;
        if (levels.levels.empty()) levels = QualityLevels::standard();
        static std::string chosenName;
        if (!levels.find(chosenName)) chosenName = editor.engine.quality && levels.find(editor.engine.quality->name) ? editor.engine.quality->name : levels.start;
        int chosen = std::max(levels.indexOf(chosenName), 0);
        bool changed = false, rechosen = false;

        // ---- the levels
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
        float room = ImGui::GetContentRegionAvail().x - 36;
        float width = std::clamp(room / float(levels.levels.size()) - 6, 70.0f, 170.0f);
        for (size_t i = 0; i < levels.levels.size(); i++) {
            const GameQuality& level = levels.levels[i];
            if (i > 0) {
                if (ImGui::GetCursorPosX() + width > room + 30) ImGui::NewLine();
                else ImGui::SameLine(0, 6);
            }
            ImGui::PushID(int(i));
            bool isOn = int(i) == chosen;
            ImGui::PushStyleColor(ImGuiCol_Button, isOn ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            std::string label = level.name == levels.start ? level.name + "  " + icon::Star : level.name;
            if (ImGui::Button(label.c_str(), ImVec2(width, 30))) {
                chosenName = level.name;
                rechosen = true;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                std::string about = level.name == levels.start ? "The game starts at this level. " : "";
                for (GameQuality::Level known : {GameQuality::Level::Low, GameQuality::Level::Medium, GameQuality::Level::High, GameQuality::Level::Ultra}) {
                    if (level.name == GameQuality::levelName(known)) about += GameQuality::levelPurpose(known);
                }
                if (!about.empty()) ui::tooltip(about.c_str());
            }
            ImGui::PopID();
        }
        ImGui::SameLine(0, 6);
        if (ImGui::Button("+", ImVec2(30, 30))) {
            GameQuality made = levels.levels[size_t(chosen)];
            made.name = levels.freeName("New Level");
            levels.levels.insert(levels.levels.begin() + chosen + 1, made);
            chosenName = made.name;
            changed = rechosen = true;
        }
        ui::tooltip("A new level, a copy of the one chosen: then set its values");
        ImGui::PopStyleVar();
        chosen = std::max(levels.indexOf(chosenName), 0);
        GameQuality& level = levels.levels[size_t(chosen)];

        // ---- what is done with the one chosen
        ImGui::Spacing();
        {
            static char name[64];
            static std::string nameFor;
            if (nameFor != level.name) {
                std::snprintf(name, sizeof name, "%s", level.name.c_str());
                nameFor = level.name;
            }
            ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.4f, 240.0f));
            if (ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
                std::string wanted = name;
                if (!wanted.empty() && wanted != level.name) {
                    std::string unique = levels.freeName(wanted);
                    if (levels.start == level.name) levels.start = unique;
                    level.name = unique;
                    chosenName = unique;
                    nameFor.clear();
                    changed = true;
                }
            }
            ui::tooltip("Its name, as the game's settings menu shows it");
            ImGui::SameLine();
            bool isStart = levels.start == level.name;
            ImGui::BeginDisabled(isStart);
            if (ImGui::Button(isStart ? "The Game Starts Here" : "Start Here")) {
                levels.start = level.name;
                changed = true;
            }
            ImGui::EndDisabled();
            ui::tooltip("The level a game starts at, until its player chooses another in the game's settings (World::setQualityLevel)");
            ImGui::SameLine();
            ImGui::BeginDisabled(chosen == 0);
            if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
                std::swap(levels.levels[size_t(chosen)], levels.levels[size_t(chosen - 1)]);
                changed = true;
            }
            ImGui::EndDisabled();
            ui::tooltip("Earlier in the list: the game's menu shows them in this order, usually from the lightest");
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(chosen + 1 >= int(levels.levels.size()));
            if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
                std::swap(levels.levels[size_t(chosen)], levels.levels[size_t(chosen + 1)]);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(levels.levels.size() <= 1);
            if (ImGui::Button("Delete")) {
                std::string gone = level.name;
                levels.levels.erase(levels.levels.begin() + chosen);
                if (levels.start == gone) levels.start = levels.levels.front().name;
                chosenName = levels.levels[size_t(std::min(chosen, int(levels.levels.size()) - 1))].name;
                changed = rechosen = true;
            }
            ImGui::EndDisabled();
        }
        if (changed) {
            // (The level chosen may be another now: drawn again next frame.)
            saveQuality(editor, levels, chosenName);
            return;
        }
        GameQuality& q = levels.levels[size_t(std::max(levels.indexOf(chosenName), 0))];
        ImGui::Spacing();

        // ---- its values, group by group
        static const char* const icons[GameQuality::kGroupCount] = {"scaling", "grid-3x3", "sun-medium", "orbit", "sparkles", "cloud-sun", "trees"};
        bool edited = false;
        for (int index = 0; index < GameQuality::kGroupCount; index++) {
            Group group = Group(index);
            ImGui::PushID(index);
            std::string header = std::string(GameQuality::groupName(group));
            bool open = ImGui::CollapsingHeader(("##group" + std::to_string(index)).c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            float middle = (min.y + max.y) * 0.5f;
            ui::drawIcon(draw, ImVec2(min.x + 34, middle), 14, icons[index], ImGui::GetColorU32(ImGuiCol_Text));
            draw->AddText(ImVec2(min.x + 50, middle - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), header.c_str());
            std::string summary = q.summaryOf(group);
            float summaryX = min.x + 60 + ImGui::CalcTextSize(header.c_str()).x;
            ImGui::PushClipRect(ImVec2(summaryX, min.y), ImVec2(max.x - 110, max.y), true);
            draw->AddText(ImVec2(summaryX, middle - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), summary.c_str());
            ImGui::PopClipRect();
            // Its values copied from another level, or from one of the four a new project has.
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 100);
            if (ImGui::SmallButton("Copy From...")) ImGui::OpenPopup("copy");
            if (ImGui::BeginPopup("copy")) {
                ImGui::TextDisabled("%s from:", GameQuality::groupName(group));
                for (const GameQuality& other : levels.levels) {
                    if (other.name == q.name) continue;
                    if (ImGui::MenuItem(other.name.c_str(), nullptr, q.sameGroup(group, other))) {
                        q.copyGroup(group, other);
                        edited = true;
                    }
                }
                ImGui::Separator();
                for (GameQuality::Level known : {GameQuality::Level::Low, GameQuality::Level::Medium, GameQuality::Level::High, GameQuality::Level::Ultra}) {
                    GameQuality standard = GameQuality::preset(known);
                    std::string item = std::string("Molten's ") + GameQuality::levelName(known);
                    if (ImGui::MenuItem(item.c_str(), nullptr, q.sameGroup(group, standard))) {
                        q.copyGroup(group, standard);
                        edited = true;
                    }
                }
                ImGui::EndPopup();
            }
            if (open) {
                ImGui::Indent(14);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", GameQuality::groupPurpose(group));
                ImGui::PopTextWrapPos();
                edited |= drawQualityGroup(editor, q, group);
                ImGui::Spacing();
                ImGui::Unindent(14);
            }
            ImGui::PopID();
        }
        if (edited) saveQuality(editor, levels, chosenName);
        else if (rechosen) editor.engine.setQualityLevels(levels, chosenName);      // only drawn at another: nothing to write
    }

    /// The values of one group of a level. True if one was changed.
    static bool drawQualityGroup(EditorContext& editor, GameQuality& q, GameQuality::Group group) {
        using Group = GameQuality::Group;
        bool edited = false;
        const bool canTrace = editor.engine.renderer().canTraceRays();
        switch (group) {
            case Group::Resolution: {
                edited |= ui::sliderFloat("Render Scale", q.renderScale, 0.25f, 2.0f);
                ui::tooltip("Below 1 the scene is drawn smaller and made the screen's size: faster, a little softer. Above 1, drawn larger and made smaller: the finest edges");
                edited |= ui::checkbox("MetalFX Upscaling", q.metalFX);
                ui::tooltip("A smaller picture is made the screen's size by MetalFX, which keeps it sharp, instead of being stretched");
                edited |= ui::checkbox("Dynamic Resolution", q.dynamicResolution);
                ui::tooltip("While the GPU cannot keep up with the frame rate below, the scene is drawn smaller, and larger again when it can");
                ImGui::BeginDisabled(!q.dynamicResolution);
                edited |= ui::dragFloat("Frame Rate to Keep", q.targetFrameRate, 1.0f, 20.0f, 240.0f, "%.0f fps");
                edited |= ui::sliderFloat("Smallest", q.lowestScale, 0.25f, 1.0f);
                ui::tooltip("Never smaller than this share of the render scale above");
                ImGui::EndDisabled();
                edited |= ui::checkbox("Smaller When Hot", q.followsDeviceHeat);
                ui::tooltip("On a phone or a laptop that gets hot, the scene is drawn smaller until it cools");
                break;
            }
            case Group::AntiAliasing: {
                int aa = q.antiAliasing >= 4 ? 2 : q.antiAliasing >= 2 ? 1 : 0;
                if (ui::combo("Multisampling", aa, {"Off", "2x", "4x"})) {
                    q.antiAliasing = aa == 2 ? 4 : aa == 1 ? 2 : 1;
                    edited = true;
                }
                ui::tooltip("Each pixel's edges looked at 2 or 4 times. 2x costs little on Apple's GPUs");
                break;
            }
            case Group::Shadows: {
                edited |= ui::checkbox("Shadows", q.shadows);
                ImGui::BeginDisabled(!q.shadows);
                float percent = q.shadowReach * 100.0f;
                if (ui::dragFloat("Reach##q", percent, 1.0f, 10.0f, 400.0f, "%.0f%%")) {
                    q.shadowReach = percent / 100.0f;
                    edited = true;
                }
                ui::tooltip("How far the sun's shadows reach, as a share of how far each scene asks (Lighting > Shadows > Distance)");
                int resolution = q.shadowResolution >= 4096 ? 3 : q.shadowResolution >= 2048 ? 2 : q.shadowResolution >= 1024 ? 1 : 0;
                if (ui::combo("Resolution", resolution, {"512", "1024", "2048", "4096"})) {
                    q.shadowResolution = 512 << resolution;
                    edited = true;
                }
                edited |= ui::dragInt("Slices", q.shadowCascades, 0.05f, 1, 4);
                ui::tooltip("Slices of the view, each with its shadow map: more keeps shadows sharp both near and far");
                edited |= ui::dragInt("Lights", q.shadowedLights, 0.05f, 0, 48);
                ui::tooltip("Point and spot lights that cast shadows: at most this many at once, the nearest and brightest first");
                int lightResolution = q.lightShadowResolution >= 2048 ? 3 : q.lightShadowResolution >= 1024 ? 2 : q.lightShadowResolution >= 512 ? 1 : 0;
                if (ui::combo("Light Resolution", lightResolution, {"256", "512", "1024", "2048"})) {
                    q.lightShadowResolution = 256 << lightResolution;
                    edited = true;
                }
                ImGui::EndDisabled();
                if (q.rayTraced && q.tracedShadows && canTrace) ignoredNote("Ray Tracing traces the sun's shadow: its maps are drawn only for the light shafts in the fog.");
                break;
            }
            case Group::RayTracing: {
                if (!canTrace) {
                    ignoredNote(editor.engine.renderer().device().supportsRayTracing()
                                    ? "The ray tracing shaders did not compile here (see the console): shadow maps are used."
                                    : "This GPU does not trace rays: shadow maps are used here, whatever is chosen. A game keeps the choice for the GPUs that can.");
                }
                edited |= ui::checkbox("Ray Tracing", q.rayTraced);
                ui::tooltip("Off: shadow maps (Shadows, above), the screen's own ambient occlusion, reflections of the sky");
                ImGui::BeginDisabled(!q.rayTraced);
                edited |= ui::checkbox("Sun's Shadows", q.tracedShadows);
                ImGui::BeginDisabled(!q.tracedShadows);
                edited |= ui::dragInt("Rays at the Edges", q.shadowRays, 0.1f, 1, 32);
                ui::tooltip("Rays toward the sun where part of its disk shows: soft edges. One is enough everywhere else");
                ImGui::EndDisabled();
                edited |= ui::checkbox("Light Around Things", q.tracedOcclusion);
                ui::tooltip("How much of the sky each pixel sees: dark under a car, in a corner");
                ImGui::BeginDisabled(!q.tracedOcclusion);
                edited |= ui::dragInt("Rays a Pixel", q.occlusionRays, 0.05f, 1, 16);
                ImGui::EndDisabled();
                edited |= ui::checkbox("Bounced Light", q.tracedBounce);
                ui::tooltip("And the light those rays find: a sunlit wall lights the floor beside it");
                edited |= ui::checkbox("Reflections", q.tracedReflections);
                ImGui::BeginDisabled(!q.tracedReflections);
                edited |= ui::sliderFloat("Roughest Mirrored", q.reflectionRoughness, 0.0f, 1.0f);
                ui::tooltip("Surfaces smoother than this mirror what is around them; rougher ones the sky's blurred light");
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                break;
            }
            case Group::Effects: {
                edited |= ui::checkbox("Ambient Occlusion##q", q.ambientOcclusion);
                ui::tooltip("The screen's own darkening of corners and creases (traced light, where it is on, does it better)");
                edited |= ui::checkbox("Bloom##q", q.bloom);
                edited |= ui::dragInt("Light Shaft Steps", q.lightShafts, 0.2f, 0, 48);
                ui::tooltip("The most steps a scene's shafts of light are walked in: 0 none");
                edited |= ui::checkbox("Lights in the Air", q.lightsInAir);
                ui::tooltip("Point and spot lights that light the fog they pass through (Light > Lights the Air)");
                edited |= ui::checkbox("Motion Blur##q", q.motionBlur);
                edited |= ui::checkbox("Depth of Field##q", q.depthOfField);
                ui::tooltip("Both only where a scene has them (Post Processing)");
                break;
            }
            case Group::Sky: {
                edited |= ui::checkbox("Clouds with Depth", q.volumetricClouds);
                ui::tooltip("Off: every scene's clouds are a flat layer");
                ImGui::BeginDisabled(!q.volumetricClouds);
                edited |= ui::dragInt("Steps", q.cloudSteps, 0.2f, 16, 128);
                ui::tooltip("How many steps each line of sight takes through the clouds: more is finer and slower");
                edited |= ui::sliderFloat("Size", q.cloudResolution, 0.35f, 1.0f);
                ui::tooltip("The clouds are walked through into a picture this share of the screen's size, however small the scene is drawn. "
                            "They are soft: half shows no loss and costs a quarter");
                ImGui::EndDisabled();
                break;
            }
            case Group::Detail: {
                edited |= ui::sliderFloat("Distance", q.detailDistance, 0.25f, 3.0f);
                ui::tooltip("How far the detail of far things lasts: 0.5 half as far, 2 twice");
                edited |= ui::checkbox("Light Probes", q.lightProbes);
                ui::tooltip("The light baked into a scene's Light Probe Volumes (Lighting > Baked Light)");
                break;
            }
        }
        return edited;
    }

    /// The levels, now: drawn by the editor at `shown`, and written to Settings/Quality.settings.
    static void saveQuality(EditorContext& editor, const QualityLevels& levels, const std::string& shown) {
        editor.engine.setQualityLevels(levels, shown);
        std::error_code code;
        std::filesystem::create_directories(editor.project.settingsDirectory(), code);
        if (!text::writeFile(editor.project.settingsDirectory() + "/Quality.settings", levels.toText())) {
            editor.notify("Cannot write Settings/Quality.settings", ConsoleMessage::Level::Error);
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
