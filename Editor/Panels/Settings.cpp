// The panels of the scene's settings (Lighting, Environment, Rendering, Post Processing) and of the project's.
#include "Editor.hpp"
#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Kit/LightProbeVolume.hpp"
#include "Rendering/Quality.hpp"
#include "Panels/Widgets.hpp"
#include "Panels/Pickers.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>

namespace molten::editor {

namespace {

/// Lighting: what lights the scene. The sun, the sky's light, shadows, the lights. The world they
/// light (the sky, its clouds, the fog, the weather and its wind, the sea) is in Environment; how
/// the picture is drawn is in Rendering, and what is done to the finished picture in Post Processing.
/// What the project's quality (Project Settings > Quality) does to a setting of the scene, said
/// under it, with the way there: a setting the quality turns off does nothing, and must say so.
void qualityNote(EditorContext& editor, const char* text) {
    ImGui::PushID(text);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.35f, 1.0f), "%s", text);
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton("Project Settings > Quality")) editor.showProjectSettings("Quality");
    ImGui::PopID();
}

/// The project's quality, if it has one (an editor without a project has none: all is as the scene says).
const GameQuality* projectQuality(EditorContext& editor) {
    return editor.engine.quality ? &*editor.engine.quality : nullptr;
}

/// The sun's shadow is traced, not drawn into shadow maps (the project's quality, on a GPU that can trace rays).
bool sunIsTraced(EditorContext& editor) {
    const GameQuality* quality = projectQuality(editor);
    return quality && quality->rayTraced && editor.engine.renderer().canTraceRays();
}

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
        // One page: what the scene is lit by, how its shadows and shade look, and its lights in a
        // list that opens when wanted. How it is all drawn (shadow maps or rays, how sharp) is the
        // project's, the same for every scene: Project Settings > Quality.
        drawLight(editor, settings, sun, changed);
        drawShading(editor, settings, changed);
        drawBaked(editor, settings, changed);
        if (ImGui::CollapsingHeader("Lights in the Scene")) drawLights(editor, changed);
        // These settings are saved with the scene, so changing them is an edit like any other.
        if (changed) editor.scene.markChanged();
    }

private:
    void drawLight(EditorContext& editor, RenderSettings& settings, Entity sun, bool& changed) {
        if (ui::section("Sun")) {
            if (sun.isValid()) {
                Light* light = editor.engine.scene.get<Light>(sun);
                changed |= ui::color("Color", light->color, false);
                changed |= ui::dragFloat("Intensity", light->intensity, 0.02f, 0.0f, 100.0f);
                changed |= ui::checkbox("Casts Shadows", light->castsShadows);
                if (const GameQuality* quality = projectQuality(editor); quality && !quality->shadows && light->castsShadows) {
                    qualityNote(editor, "No shadows at the project's quality: the scene has none, whatever it asks.");
                }
                changed |= ui::dragDegrees("Size in the Sky", settings.sky.sunAngularRadius, 0.1f, 20.0f);
                ui::tooltip(sunIsTraced(editor) ? "How large the sun's disk is in the sky, and how soft its shadows' edges (they are traced: a larger sun, softer edges)"
                                                : "How large the sun's disk is in the sky. (With ray tracing, at the project's quality, it also softens its shadows' edges)");
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
        // The sky's light: how much it lights the scene. (The sky itself is the world's: Environment.)
        if (ui::section("Sky Light")) {
            changed |= ui::dragFloat("Light from the Sky", settings.environmentIntensity, 0.01f, 0.0f, 10.0f);
            ui::tooltip("How much the sky lights the scene: the soft light in the shade, and what shiny things mirror");
        }
    }

    /// How this scene's shadows and shade look. How they are worked out (shadow maps or rays,
    /// how sharp, how many rays) is the project's, the same for every scene: Project Settings > Quality.
    void drawShading(EditorContext& editor, RenderSettings& settings, bool& changed) {
        if (ui::section("Shadows")) {
            const GameQuality* quality = projectQuality(editor);
            changed |= ui::dragFloat("Distance", settings.cascades.shadowDistance, 0.5f, 5.0f, 2000.0f, "%.0f m");
            ui::tooltip("How far from the camera this scene's sun shadows reach. A racing circuit wants more than a room. "
                        "The project's quality reaches a share of it: less at Low, more at Ultra");
            if (quality && quality->shadows && std::abs(quality->shadowReach - 1.0f) > 0.001f) {
                ImGui::TextDisabled("At the project's quality: %.0f m (%.0f%%).", settings.cascades.shadowDistance * quality->shadowReach, quality->shadowReach * 100.0f);
            }
            // The two below are for shadow maps: with the sun's shadow traced, only the lights' maps use them.
            const bool traced = sunIsTraced(editor);
            changed |= ui::dragFloat("Bias", settings.shadowBias, 0.00001f, 0.0f, 0.01f, "%.5f");
            ui::tooltip(traced ? "The sun's shadow maps: how far a surface's own shadow is pushed back. (The sun's shadow is traced at the project's quality: no maps)"
                               : "The sun's shadow maps: how far a surface's own shadow is pushed back. Raise it if surfaces are striped by their own shadow");
            changed |= ui::dragFloat("Normal Offset", settings.shadowNormalOffset, 0.05f, 0.0f, 10.0f);
            ui::tooltip("Shadow maps, the sun's and the point and spot lights': how far along its facing a surface looks for its shadow. Raise it for stripes on slopes");
            if (traced) ImGui::TextDisabled("The sun's shadow is traced: Bias is not used.");
        }
        // Said once, with the way there: how all of it is drawn is not set here.
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Shadow maps or ray tracing, and how sharp, are the project's: the same for every scene.");
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Project Settings > Quality")) editor.showProjectSettings("Quality");
    }

    // ---- baked light: the probes of the scene's Light Probe Volumes (Kit/LightProbeVolume.hpp)

    std::future<BakedLighting> baking_;
    std::atomic<float> bakeProgress_{0.0f};
    std::atomic<bool> bakeCancelled_{false};
    std::string bakeFile_, bakeName_;           // where the result goes: on disk, and as Assets names it
    std::chrono::steady_clock::time_point bakeStarted_;

    void drawBaked(EditorContext& editor, RenderSettings& settings, bool& changed) {
        if (!ui::section("Baked Light")) {
            finishBake(editor, settings, changed);      // a bake goes on with the section closed
            return;
        }
        int volumes = 0;
        size_t probes = 0;
        editor.engine.scene.each<kit::LightProbeVolume, Transform>([&](Entity, kit::LightProbeVolume& volume, Transform& transform) {
            if (!transform.isActiveInHierarchy) return;
            volumes++;
            probes += gridFor(volume.desc(transform).box, volume.spacing).probeCount();
        });
        ImGui::PushTextWrapPos(0.0f);
        if (volumes == 0) {
            ImGui::TextDisabled("No Light Probe Volume in the scene: everything is lit by the whole sky, indoors as out.");
            ImGui::TextDisabled("Entity > Light > Light Probe Volume puts one round a room or a building.");
        } else {
            ImGui::Text("%d %s, %zu probes.", volumes, volumes == 1 ? "volume" : "volumes", probes);
        }
        if (baking_.valid()) {
            float done = bakeProgress_.load();
            char label[48];
            std::snprintf(label, sizeof label, "Baking... %.0f%%", double(done * 100.0f));
            ImGui::ProgressBar(done, ImVec2(-FLT_MIN, 0), label);
            if (ImGui::Button("Cancel")) bakeCancelled_ = true;
        } else if (volumes > 0) {
            const std::string& scenePath = editor.scene.filePath;
            const std::string root = editor.engine.assetRoot + "/";
            bool canSave = !scenePath.empty() && scenePath.compare(0, root.size(), root) == 0;
            ImGui::BeginDisabled(!canSave);
            if (ImGui::Button("Bake Light Probes")) startBake(editor, settings, scenePath.substr(root.size()));
            ImGui::EndDisabled();
            if (!canSave) ImGui::TextDisabled("Save the scene in the project first: the light is kept beside it.");
            ui::tooltip("Sends rays from every probe into the scene: how much of the sky each sees, and the light the walls and the floor throw back. "
                        "What moves and what is seen through is left out. Bake again when the scene's walls or lights change");
        }
        if (!settings.bakedLighting.empty()) {
            size_t loaded = editor.engine.renderer().lightProbeCount();
            ImGui::TextDisabled("%s: %zu probes in use.", settings.bakedLighting.c_str(), loaded);
            if (ImGui::SmallButton("Stop Using It")) {
                settings.bakedLighting.clear();
                changed = true;
            }
            ui::tooltip("The scene is lit by the whole sky again. The file stays among the assets");
        }
        ImGui::PopTextWrapPos();
        finishBake(editor, settings, changed);
    }

    void startBake(EditorContext& editor, RenderSettings& settings, const std::string& sceneName) {
        auto job = std::make_shared<kit::LightingBakeJob>(kit::prepareLightingBake(editor.engine, settings, 256));
        bakeName_ = kit::lightingFileFor(sceneName);
        bakeFile_ = editor.engine.assetRoot + "/" + bakeName_;
        bakeProgress_ = 0.0f;
        bakeCancelled_ = false;
        bakeStarted_ = std::chrono::steady_clock::now();
        logInfo("baking the light probes: %zu triangles, %zu lights", job->triangleCount(), job->lights.size());
        // On another thread: the editor goes on meanwhile. The job holds copies of all it needs.
        baking_ = std::async(std::launch::async, [this, job] {
            return bakeLighting(job->scene, job->lights, job->volumes, job->settings, [this](float done) {
                bakeProgress_ = done;
                return !bakeCancelled_.load();
            });
        });
    }

    void finishBake(EditorContext& editor, RenderSettings& settings, bool& changed) {
        if (!baking_.valid() || baking_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        BakedLighting baked = baking_.get();
        if (bakeCancelled_ || baked.grids.empty()) {
            logInfo("the bake was stopped: the scene's light is as it was");
            return;
        }
        if (!text::writeFile(bakeFile_, baked.toText())) {
            logError("could not write %s", bakeFile_.c_str());
            return;
        }
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - bakeStarted_).count();
        logInfo("baked the light probes into %s in %.1f s", bakeName_.c_str(), seconds);
        settings.bakedLighting = bakeName_;
        editor.engine.bakedLightingRevision++;      // the same file, read again
        changed = true;
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

/// Environment: the world of the game. Its sky and clouds, the air (fog, light shafts), the
/// weather and the wind it brings, the sea. Saved with the scene.
class EnvironmentPanel final : public Panel {
public:
    EnvironmentPanel() {
        title = "Environment";
        firstBeside = "Lighting";
    }

    void draw(EditorContext& editor) override {
        RenderSettings& settings = editor.engine.renderer().settings;
        bool changed = false;
        if (ui::section("Sky")) {
            // Automatic: the sky follows the sun by itself, day, sunset, night with its moon and
            // stars, and lights the fog and the clouds to match. Or a sky of fixed colors.
            int kind = settings.sky.sunsetColors ? 0 : 1;
            if (ui::combo("Sky", kind, {"Automatic: day, sunset, night", "Fixed colors"})) {
                settings.sky.sunsetColors = kind == 0;
                if (settings.sky.sunsetColors && settings.sky.stars <= 0.0f) settings.sky.stars = 1.0f;
                changed = true;
            }
            ui::tooltip(settings.sky.sunsetColors ? "Turn the sun (its rotation) and the sky follows: gold, orange and red as it sets, the clouds lit from below, then night with the moon across the sky from it and the stars. The colors below are the clear day's"
                                                  : "The sky is the colors below, whatever the sun does");
            changed |= ui::color(settings.sky.sunsetColors ? "Day Zenith" : "Zenith", settings.sky.zenithColor, true);
            changed |= ui::color(settings.sky.sunsetColors ? "Day Horizon" : "Horizon", settings.sky.horizonColor, true);
            if (settings.sky.sunsetColors) {
                changed |= ui::sliderFloat("Moon", settings.sky.moon, 0.0f, 2.0f);
                ui::tooltip("At night: the moon across the sky from the sun, which lights the scene with a cold, dim light and casts shadows. 0 a night with no moon, 1 a full moon");
                changed |= ui::sliderFloat("Stars", settings.sky.stars, 0.0f, 3.0f);
                ui::tooltip("At night, when the sun is down: 0 none, 1 a clear night far from any town");
            }
            changed |= ui::color("Ground", settings.sky.groundColor, true);
            changed |= ui::dragFloat("Brightness", settings.sky.intensity, 0.01f, 0.0f, 100.0f);
        }
        if (ui::section("Fog")) {
            changed |= ui::color("Color##fog", settings.fogColor, true);
            if (settings.sky.sunsetColors) {
                changed |= ui::checkbox("Lit by the Sun", settings.fogLitBySun);
                ui::tooltip("The color above is the fog's at noon: it goes gold as the sun sets and dark at night, lit then by the moon. Off: always this color");
            }
            changed |= ui::dragFloat("Density", settings.fogDensity, 0.0005f, 0.0f, 1.0f, "%.4f");
            changed |= ui::dragFloat("Thins With Height", settings.fogHeightFalloff, 0.001f, 0.0f, 2.0f, "%.3f");
            ui::tooltip("Above zero the fog lies low, as mist in a valley, and thins at this rate per meter of height");
            changed |= ui::dragFloat("Glow Toward Sun", settings.fogSunScatter, 0.02f, 0.0f, 20.0f);
            bool shafts = settings.fogShaftSteps > 0;
            if (ui::checkbox("Light Shafts", shafts)) {
                settings.fogShaftSteps = shafts ? 16 : 0;
                if (shafts && settings.fogSunScatter <= 0.0f) settings.fogSunScatter = 2.0f;
                if (shafts && settings.fogDensity <= 0.0f) settings.fogDensity = 0.002f;
                changed = true;
            }
            ui::tooltip("The air glows only where the sun reaches it: what stands in its way (a tree, a building, a cloud's edge) casts beams of light and shadow through it. How finely: Project Settings > Quality > Effects");
            if (shafts) {
                const GameQuality* quality = projectQuality(editor);
                bool sunShades = false;
                editor.engine.scene.each<Light>([&](Entity, Light& light) {
                    if (light.type == LightType::Directional && light.isEnabled && light.castsShadows) sunShades = true;
                });
                if (quality && quality->lightShafts <= 0) qualityNote(editor, "No light shafts at the project's quality.");
                else if (quality && !quality->shadows) qualityNote(editor, "Light shafts are the sun's shadows in the air: there are none at the project's quality.");
                else if (!sunShades) ImGui::TextDisabled("They are the sun's shadows in the air: turn on its Casts Shadows (Lighting > Sun).");
                else if (settings.fogDensity <= 0.0f) ImGui::TextDisabled("They need some fog: raise the Density.");
            }
        }
        if (ui::section("Weather")) {
            // Built in, none by default: a game that wants its own leaves these at 0.
            changed |= ui::sliderFloat("Rain", settings.weather.rain, 0.0f, 1.0f);
            ui::tooltip("Rain round the camera, with the wind: drops that splash on whatever they hit and leave rings on water. The sky clouds over and the sun dims as it rains. 0 none, 1 a downpour");
            changed |= ui::sliderFloat("Storm", settings.weather.storm, 0.0f, 1.0f);
            ui::tooltip("Lightning now and then, a few kilometers away or nearer: bolts down from the clouds, and flashes inside them that light them from within. 1 every few seconds. The wind rises with it, to a gale. With rain or without");
            changed |= ui::sliderFloat("Snow", settings.weather.snow, 0.0f, 1.0f);
            ui::tooltip("Snow round the camera: flakes that drift down slowly, sway and go with the wind. The sky clouds over. 0 none, 1 a heavy fall");
            changed |= ui::checkbox("Snow Settles", settings.weather.snowSettles);
            ui::tooltip("What falls lies: what faces up and is out in it (not under a roof) turns white, unevenly, in drifts, more the longer it snows "
                        "(a full cover in a minute and a half of a heavy fall, a thin one in a light fall); it melts in some minutes once it stops. Off: it melts as it lands");
            if (settings.weather.rain > 0 || settings.weather.storm > 0 || settings.weather.snow > 0) ImGui::TextDisabled("It moves while the game runs, or with Simulate on.");
            // The clouds are the weather's: rain, a storm and snow cloud the sky over on top of these.
            ui::subheading("Clouds");
            changed |= ui::sliderFloat("Coverage", settings.sky.cloudCoverage, 0.0f, 1.0f);
            changed |= ui::dragFloat("Scale", settings.sky.cloudScale, 0.01f, 0.05f, 20.0f);
            changed |= ui::dragVec2("Drift", settings.sky.cloudSpeed, 0.001f);
            ui::tooltip("How they move by themselves, besides the wind (which carries them too): along x and z");
            changed |= ui::checkbox("With Depth", settings.sky.cloudsHaveDepth);
            ui::tooltip("Clouds as volumes, thick and thin, lit by the sun through them: a camera can fly in, through and above them. Costs more than the flat layer");
            if (settings.sky.cloudsHaveDepth) {
                const GameQuality* quality = projectQuality(editor);
                const bool flat = quality && !quality->volumetricClouds;
                ImGui::BeginDisabled(flat);
                changed |= ui::dragFloat("Height", settings.sky.cloudHeight, 5.0f, 50.0f, 12000.0f, "%.0f m");
                ui::tooltip("Meters above the ground where their base is");
                changed |= ui::dragFloat("Thickness", settings.sky.cloudThickness, 5.0f, 50.0f, 8000.0f, "%.0f m");
                ImGui::EndDisabled();
                if (flat) qualityNote(editor, "Flat clouds at the project's quality (Sky): no depth, so Height and Thickness are not used.");
            }
            // The wind is the weather's too: rain freshens it, a storm makes a gale of it.
            ui::subheading("Wind");
            // One wind for the whole scene, set as how hard it blows and which way (what is kept is its velocity along x and z).
            static float lastHeading = 73.0f;
            float speed = simd::length(Vec2{settings.wind.x, settings.wind.z});
            float heading = speed > 1e-4f ? std::atan2(settings.wind.x, -settings.wind.z) * (180.0f / kPi) : lastHeading;
            if (heading < 0) heading += 360.0f;
            bool isMoved = ui::dragFloat("Speed", speed, 0.05f, 0.0f, 40.0f, "%.1f m/s");
            ui::tooltip("The scene's own, before the weather adds to it (rain a little, a storm a gale): 2 a light air, 6 a breeze that raises small waves, 12 a strong wind and white crests, 20 a gale");
            isMoved |= ui::dragFloat("Blows Toward", heading, 0.5f, 0.0f, 360.0f, "%.0f deg");
            ui::tooltip("0 toward -Z, 90 toward +X: the way waves run, grass leans and smoke drifts");
            if (isMoved) {
                lastHeading = heading;
                float turn = heading * (kPi / 180.0f);
                settings.wind = Vec3{std::sin(turn) * speed, 0, -std::cos(turn) * speed};
                changed = true;
            }
            changed |= ui::sliderFloat("Gusts", settings.windGustiness, 0.0f, 1.0f);
        }
        if (ui::section("Water")) {
            // The scene's sea: everywhere below its level. Its waves are the wind's and the weather's.
            SeaSettings& sea = settings.sea;
            changed |= ui::checkbox("Sea", sea.enabled);
            ui::tooltip("Water everywhere below a level, out to the horizon: things float on it (Buoyancy), its waves rise with the wind and the weather, the sun's light crawls in lines over its bottom, and under its level the camera is under water. Off: no sea. (Water of the game's own, a pond or a pool, is a shader: Explorer > + New > Water Shader as Code)");
            if (sea.enabled) {
                changed |= ui::dragFloat("Level", sea.level, 0.05f, -10000.0f, 10000.0f, "%.2f m");
                ui::tooltip("The height of its surface at rest. Below it all is under water");
                ui::subheading("Color");
                changed |= ui::color("Deep##sea", sea.color, false);
                ui::tooltip("Its color where it is deep: what is left of white light after a long way through it. Red goes first, then green");
                changed |= ui::color("Shallow##sea", sea.shallowColor, false);
                ui::tooltip("The color it takes over a light bottom a meter or two down: turquoise over sand in the tropics, green in a northern sea");
                changed |= ui::dragFloat("Clarity", sea.clarity, 0.1f, 0.2f, 80.0f, "%.1f m");
                ui::tooltip("How far one sees through it, down from above and across under it: 3 a murky harbor, 10 a clear sea, 40 a lagoon");
                changed |= ui::sliderFloat("Scattering", sea.scattering, 0.0f, 3.0f);
                ui::tooltip("How much light its body sends back up, in its deep color: 0 dark ink, 1 a real sea, 2 and more a glowing cartoon lagoon");
                ui::subheading("Surface");
                changed |= ui::sliderFloat("Reflections", sea.reflections, 0.0f, 1.0f);
                ui::tooltip("How much it mirrors the sky and what stands over it: 1 as real water does, less for a stylized look");
                changed |= ui::sliderFloat("Ripples", sea.ripples, 0.0f, 2.0f);
                ui::tooltip("The small waves on it, over the swell: 0 a glassy surface, 1 a real sea's, 2 choppy");
                changed |= ui::sliderFloat("Caustics", sea.caustics, 0.0f, 3.0f);
                ui::tooltip("The lines of light its waves gather on its bottom, moving with them: 0 none, 1 as real, more to draw them strong");
                ui::subheading("Foam");
                changed |= ui::sliderFloat("Shore", sea.foam, 0.0f, 2.0f);
                ui::tooltip("White where it meets the shore and what stands in it: a beach, a rock, a hull");
                changed |= ui::sliderFloat("Whitecaps", sea.whitecaps, 0.0f, 2.0f);
                ui::tooltip("White on the crests of waves that break: none below a fresh breeze (5 m/s), more the harder it blows");
                ui::subheading("Camera");
                changed |= ui::checkbox("Through the Surface", sea.lensThroughSurface);
                ui::tooltip("As the camera goes through its surface: the water's edge across the picture, the world on its other side blurred; a shake as it goes in; drops on the glass that slide off as it comes out. Off: the picture just cuts from above to under");
                ImGui::TextDisabled("Its waves: the wind, in Weather.");
            }
        }
        if (changed) editor.scene.markChanged();
    }
};

/// Rendering: how the lit scene is drawn. The shade in corners, what is out of focus, what moves
/// too fast to be sharp. How many pixels, how smooth the edges and whether light is traced with
/// rays are the project's: Project Settings > Quality.
class RenderingPanel final : public Panel {
public:
    RenderingPanel() { title = "Rendering"; }

    void draw(EditorContext& editor) override {
        RenderSettings& settings = editor.engine.renderer().settings;
        bool changed = false;

        if (ui::section("Ambient Occlusion")) {
            const GameQuality* quality = projectQuality(editor);
            // With the light ray traced, the rays find what hides the sky from each pixel: none of this is used.
            const bool traced = sunIsTraced(editor);
            ImGui::BeginDisabled(traced);
            changed |= ui::checkbox("Enabled##ao", settings.ambientOcclusionEnabled);
            ui::tooltip("The sky's light kept out of creases and corners, worked out from the picture");
            if (settings.ambientOcclusionEnabled) {
                changed |= ui::dragFloat("Radius", settings.ambientOcclusionRadius, 0.01f, 0.01f, 10.0f);
                ui::tooltip("How wide the shade in creases and corners is, in meters");
                changed |= ui::dragFloat("Strength", settings.ambientOcclusionIntensity, 0.01f, 0.0f, 4.0f);
                changed |= ui::dragFloat("Contrast", settings.ambientOcclusionPower, 0.01f, 0.1f, 8.0f);
            }
            ImGui::EndDisabled();
            if (traced) qualityNote(editor, "The light around things is ray traced at the project's quality: the rays find the shade in creases and corners, and none of this is used.");
            else if (quality && !quality->ambientOcclusion && settings.ambientOcclusionEnabled) qualityNote(editor, "No ambient occlusion at the project's quality.");
        }
        if (ui::section("Depth of Field")) {
            changed |= ui::checkbox("Enabled##dof", settings.depthOfField);
            if (settings.depthOfField) {
                changed |= ui::dragFloat("Focus Distance", settings.focusDistance, 0.05f, 0.1f, 1000.0f, "%.2f m");
                changed |= ui::dragFloat("In Focus", settings.focusRange, 0.05f, 0.0f, 500.0f, "%.2f m");
                ui::tooltip("How many meters around the focus distance stay sharp");
                changed |= ui::sliderFloat("Blur", settings.focusBlur, 0.0f, 32.0f);
            }
        }
        if (ui::section("Motion Blur")) {
            changed |= ui::checkbox("Enabled##motion", settings.motionBlur);
            ui::tooltip("What streaks past as the camera turns or speeds along");
            if (settings.motionBlur) changed |= ui::sliderFloat("Shutter", settings.motionBlurAmount, 0.0f, 1.0f);
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("How many pixels, how smooth the edges, the shadows' sharpness and ray tracing are the project's: the same for every scene.");
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Project Settings > Quality")) editor.showProjectSettings("Quality");
        if (changed) editor.scene.markChanged();
    }
};

/// Post Processing: what is done to the finished picture, as a camera and its film do: how it is
/// exposed, its glow, its colors, the lens it was taken through.
class PostProcessingPanel final : public Panel {
public:
    PostProcessingPanel() {
        title = "Post Processing";
        firstBeside = "Rendering";
    }

    void draw(EditorContext& editor) override {
        RenderSettings& settings = editor.engine.renderer().settings;
        bool changed = false;

        if (ui::section("Exposure and Tone Mapping")) {
            changed |= ui::dragFloat("Exposure", settings.exposure, 0.01f, 0.01f, 50.0f);
            changed |= ui::combo("Tone Mapping", settings.toneMapper, {"ACES", "Reinhard", "None"});
            ui::tooltip("How the light of the scene, brighter than any screen, is brought within it. ACES as film does; None cuts what is too bright");
            changed |= ui::checkbox("Adapts", settings.autoExposure);
            ui::tooltip("The exposure follows how bright the scene is, as an eye does: brighter in a tunnel, darker in the sun");
            if (settings.autoExposure) {
                changed |= ui::dragFloat("Compensation", settings.exposureCompensation, 0.01f, -6.0f, 6.0f, "%.2f stops");
                changed |= ui::dragFloat("Speed", settings.exposureAdaptSpeed, 0.01f, 0.05f, 20.0f);
                changed |= ui::dragFloat("Darkest", settings.darkestExposure, 0.01f, 0.01f, 10.0f);
                changed |= ui::dragFloat("Brightest", settings.brightestExposure, 0.05f, 0.1f, 100.0f);
            }
        }
        if (ui::section("Bloom")) {
            changed |= ui::checkbox("Enabled##bloom", settings.bloomEnabled);
            if (settings.bloomEnabled) {
                changed |= ui::dragFloat("Amount", settings.bloomIntensity, 0.002f, 0.0f, 2.0f);
                changed |= ui::dragFloat("Threshold", settings.bloomThreshold, 0.01f, 0.0f, 50.0f);
                changed |= ui::dragFloat("Knee", settings.bloomKnee, 0.01f, 0.0f, 1.0f);
                if (const GameQuality* quality = projectQuality(editor); quality && !quality->bloom) qualityNote(editor, "No bloom at the project's quality.");
            }
        }
        if (ui::section("Color")) {
            changed |= ui::sliderFloat("Contrast", settings.contrast, 0.5f, 1.5f);
            changed |= ui::sliderFloat("Saturation", settings.saturation, 0.0f, 2.0f);
            changed |= ui::sliderFloat("Temperature", settings.temperature, -1.0f, 1.0f);
            ui::tooltip("Below zero bluer, as at dusk; above, warmer, as by a fire");
            changed |= ui::sliderFloat("Tint", settings.tint, -1.0f, 1.0f);
            ui::tooltip("Below zero greener, above more magenta");
            changed |= pickValue(editor, Entity{}, "picture", "Grading Table", settings.colorGrading);
            ui::tooltip("A picture in Assets of 32 squares of 32 pixels side by side (red across, green down, blue square to square). "
                        "Assets/Grading/Neutral 32.png changes nothing: grade a copy of it in a photo editor. Empty: none");
            if (!settings.colorGrading.empty()) changed |= ui::sliderFloat("Grading Amount", settings.colorGradingAmount, 0.0f, 1.0f);
        }
        if (ui::section("Lens and Film")) {
            changed |= ui::sliderFloat("Vignette", settings.vignette, 0.0f, 1.0f);
            changed |= ui::sliderFloat("Lens Distortion", settings.lensDistortion, 0.0f, 1.0f);
            changed |= ui::dragFloat("Color Fringes", settings.chromaticAberration, 0.0005f, 0.0f, 0.1f, "%.4f");
            changed |= ui::dragFloat("Film Grain", settings.filmGrain, 0.001f, 0.0f, 0.5f);
            changed |= ui::sliderFloat("Sharpen", settings.sharpen, 0.0f, 1.0f);
            ui::tooltip("Crisper edges: good with a render scale below 1");
            changed |= pickValue(editor, Entity{}, "picture", "Dirt", settings.lensDirt);
            ui::tooltip("Smudges and specks on the glass (a mostly black picture: Lens/Dirt.png comes with the engine), lit up where the sun, "
                        "the lamps and other bright light fall on them, through the bloom. None: a clean lens");
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
            draw->AddText(ImVec2(rowAt.x + 28, rowAt.y + 4), ImGui::GetColorU32(ImGuiCol_Text), action.name.empty() ? "(no name)" : action.name.c_str());
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
    editor.registry.addPanel(nullptr, std::make_unique<EnvironmentPanel>());
    editor.registry.addPanel(nullptr, std::make_unique<RenderingPanel>());
    editor.registry.addPanel(nullptr, std::make_unique<PostProcessingPanel>());
}

void drawInputSettings(EditorContext& editor) {
    static InputPanel panel;            // what it has open is kept while the editor runs
    panel.draw(editor);
}

}  // namespace molten::editor
