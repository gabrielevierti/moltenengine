#include "Rendering/Quality.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace molten {

float& detailDistanceScale() {
    static float scale = 1.0f;
    return scale;
}

float nextDynamicScale(float scale, float gpuMilliseconds, float targetFrameRate, float lowest) {
    lowest = std::clamp(lowest, 0.25f, 1.0f);
    scale = std::clamp(scale, lowest, 1.0f);
    if (gpuMilliseconds <= 0) return scale;
    float budget = 1000.0f / std::max(targetFrameRate, 1.0f);
    if (gpuMilliseconds > budget * 0.92f) {
        // Behind: to the scale at which the frame should take 85% of its time, rounded down.
        float fitting = scale * std::sqrt(budget * 0.85f / gpuMilliseconds);
        float stepped = std::floor(fitting * 20.0f + 0.001f) / 20.0f;
        return std::clamp(std::min(stepped, scale - 0.05f), lowest, 1.0f);
    }
    if (gpuMilliseconds < budget * 0.7f) return std::min(1.0f, scale + 0.05f);       // room to spare: larger, slowly
    return scale;
}

// ---------------------------------------------------------------- names

const char* GameQuality::levelName(Level level) {
    switch (level) {
        case Level::Low: return "Low";
        case Level::Medium: return "Medium";
        case Level::High: return "High";
        case Level::Ultra: return "Ultra";
        case Level::Custom: break;
    }
    return "Custom";
}

const char* GameQuality::levelPurpose(Level level) {
    switch (level) {
        case Level::Low: return "For the slowest machines the game should run on, and still a good picture: a little smaller and made sharp by MetalFX, "
                                "smooth edges, real shadows, soft clouds with depth; no ray tracing.";
        case Level::Medium: return "For most laptops, phones and tablets: the light ray traced where the GPU can, with fewer rays.";
        case Level::High: return "For a recent Mac: every pixel drawn, all of the light ray traced.";
        case Level::Ultra: return "The best there is, at 60 frames a second on a recent Mac: smoothest edges, the most rays, rougher things mirrored, "
                                  "shadows farther, the finest clouds and light shafts.";
        case Level::Custom: break;
    }
    return "";
}

const char* GameQuality::groupName(Group group) {
    switch (group) {
        case Group::Resolution: return "Resolution";
        case Group::AntiAliasing: return "Anti-Aliasing";
        case Group::Shadows: return "Shadows";
        case Group::RayTracing: return "Ray Tracing";
        case Group::Effects: return "Effects";
        case Group::Sky: return "Sky";
        case Group::Detail: return "Detail";
    }
    return "";
}

const char* GameQuality::groupPurpose(Group group) {
    switch (group) {
        case Group::Resolution: return "How many pixels the scene is drawn with before the picture is made the screen's size, and whether fewer are drawn "
                                       "while the GPU cannot keep up. Fewer pixels is the surest way to a faster frame.";
        case Group::AntiAliasing: return "How smooth the edges of things are.";
        case Group::Shadows: return "How far from the camera the sun's shadows reach (a share of what each scene asks), how sharp they are, and the "
                                    "shadows of point and spot lights.";
        case Group::RayTracing: return "The light found with rays, where the GPU can: the sun's shadows, the light around things and what it bounces, "
                                       "reflections. Each can be on or off, with its own number of rays.";
        case Group::Effects: return "The screen's ambient occlusion, bloom, shafts of light through fog, lights that light the air, motion blur and "
                                    "depth of field, where a scene has them.";
        case Group::Sky: return "Clouds with depth, where a scene has them, and how finely they are walked through.";
        case Group::Detail: return "How far the detail of far things lasts (LOD groups, painted foliage), and the light baked into probes.";
    }
    return "";
}

// ---------------------------------------------------------------- the four of a new project

GameQuality GameQuality::preset(Level level) {
    GameQuality q;                      // High: the defaults
    q.name = levelName(level);
    switch (level) {
        case Level::Low:
            // A good picture still: drawn a little smaller and made sharp again by MetalFX, smooth
            // edges (2x multisampling costs little on Apple's GPUs), real shadows near the camera, the
            // screen's ambient occlusion, clouds with depth walked coarsely but at a size that keeps them soft.
            q.renderScale = 0.85f;
            q.lowestScale = 0.7f;
            q.shadowReach = 0.6f, q.shadowResolution = 2048, q.shadowCascades = 2;
            q.lightShadowResolution = 256, q.shadowedLights = 4;
            q.rayTraced = false;
            q.shadowRays = 4, q.occlusionRays = 1, q.reflectionRoughness = 0.15f;
            q.lightShafts = 8;
            q.motionBlur = false;
            q.cloudSteps = 24, q.cloudResolution = 0.4f;
            q.detailDistance = 0.7f;
            break;
        case Level::Medium:
            q.renderScale = 0.9f;
            q.lowestScale = 0.65f;
            q.shadowReach = 0.8f, q.shadowCascades = 3;
            q.shadowedLights = 6;
            q.shadowRays = 6, q.occlusionRays = 1, q.reflectionRoughness = 0.25f;
            q.lightShafts = 12;
            q.cloudSteps = 32, q.cloudResolution = 0.45f;
            q.detailDistance = 0.85f;
            break;
        case Level::High: break;
        case Level::Ultra:
            // Every pixel while the GPU keeps up, a little fewer, made sharp by MetalFX, while it cannot.
            q.lowestScale = 0.7f;
            q.antiAliasing = 4;
            q.shadowReach = 1.5f, q.shadowResolution = 4096;
            q.lightShadowResolution = 1024, q.shadowedLights = 12;
            q.shadowRays = 12, q.occlusionRays = 3, q.reflectionRoughness = 0.55f;
            q.lightShafts = 24;
            q.cloudSteps = 64, q.cloudResolution = 0.6f;
            q.detailDistance = 1.5f;
            break;
        case Level::Custom: break;
    }
    return q;
}

void GameQuality::copyGroup(Group group, const GameQuality& from) {
    switch (group) {
        case Group::Resolution:
            renderScale = from.renderScale, metalFX = from.metalFX, dynamicResolution = from.dynamicResolution;
            targetFrameRate = from.targetFrameRate, lowestScale = from.lowestScale, followsDeviceHeat = from.followsDeviceHeat;
            break;
        case Group::AntiAliasing: antiAliasing = from.antiAliasing; break;
        case Group::Shadows:
            shadows = from.shadows, shadowReach = from.shadowReach, shadowResolution = from.shadowResolution, shadowCascades = from.shadowCascades;
            lightShadowResolution = from.lightShadowResolution, shadowedLights = from.shadowedLights;
            break;
        case Group::RayTracing:
            rayTraced = from.rayTraced, tracedShadows = from.tracedShadows, shadowRays = from.shadowRays, tracedOcclusion = from.tracedOcclusion;
            occlusionRays = from.occlusionRays, tracedBounce = from.tracedBounce, tracedReflections = from.tracedReflections;
            reflectionRoughness = from.reflectionRoughness;
            break;
        case Group::Effects:
            ambientOcclusion = from.ambientOcclusion, bloom = from.bloom, lightShafts = from.lightShafts, lightsInAir = from.lightsInAir;
            motionBlur = from.motionBlur, depthOfField = from.depthOfField;
            break;
        case Group::Sky: volumetricClouds = from.volumetricClouds, cloudSteps = from.cloudSteps, cloudResolution = from.cloudResolution; break;
        case Group::Detail: detailDistance = from.detailDistance, lightProbes = from.lightProbes; break;
    }
}

bool GameQuality::sameGroup(Group group, const GameQuality& other) const {
    GameQuality copy = *this;
    copy.copyGroup(group, other);
    return copy.toLines() == toLines();
}

// ---------------------------------------------------------------- in a line

std::string GameQuality::summaryOf(Group group) const {
    char text[200] = "";
    switch (group) {
        case Group::Resolution: {
            char size[48];
            if (renderScale == 1.0f) std::snprintf(size, sizeof(size), "Every pixel");
            else std::snprintf(size, sizeof(size), "%.0f%% of the size%s", renderScale * 100.0f, metalFX && renderScale < 1.0f ? ", MetalFX" : "");
            if (dynamicResolution) std::snprintf(text, sizeof(text), "%s; down to %.0f%% to keep %.0f fps", size, renderScale * lowestScale * 100.0f, targetFrameRate);
            else std::snprintf(text, sizeof(text), "%s, always", size);
            break;
        }
        case Group::AntiAliasing:
            std::snprintf(text, sizeof(text), "%s", antiAliasing >= 4 ? "4x multisampling" : antiAliasing >= 2 ? "2x multisampling" : "Off");
            break;
        case Group::Shadows:
            if (!shadows) std::snprintf(text, sizeof(text), "None");
            else std::snprintf(text, sizeof(text), "%.0f%% as far as each scene asks, %d px, %d slice%s; %d lights", shadowReach * 100.0f, shadowResolution,
                               shadowCascades, shadowCascades == 1 ? "" : "s", shadowedLights);
            break;
        case Group::RayTracing: {
            if (!rayTraced) {
                std::snprintf(text, sizeof(text), "Off: shadow maps");
                break;
            }
            std::string parts;
            auto add = [&](const std::string& part) { parts += (parts.empty() ? "" : ", ") + part; };
            if (tracedShadows) add("shadows (" + std::to_string(shadowRays) + " rays)");
            if (tracedOcclusion) add("light around things (" + std::to_string(occlusionRays) + ")");
            if (tracedBounce) add("bounced light");
            if (tracedReflections) add("reflections");
            std::snprintf(text, sizeof(text), "%s", parts.empty() ? "On, but nothing traced" : parts.c_str());
            break;
        }
        case Group::Effects: {
            std::string parts;
            auto add = [&](const std::string& part) { parts += (parts.empty() ? "" : ", ") + part; };
            if (ambientOcclusion) add("ambient occlusion");
            if (bloom) add("bloom");
            if (lightShafts > 0) add("light shafts in " + std::to_string(lightShafts) + " steps");
            if (lightsInAir) add("lights in the air");
            if (motionBlur) add("motion blur");
            if (depthOfField) add("depth of field");
            if (!parts.empty()) parts[0] = char(std::toupper(static_cast<unsigned char>(parts[0])));
            std::snprintf(text, sizeof(text), "%s", parts.empty() ? "None" : parts.c_str());
            break;
        }
        case Group::Sky:
            if (!volumetricClouds) std::snprintf(text, sizeof(text), "Flat clouds");
            else std::snprintf(text, sizeof(text), "Clouds with depth: %d steps at %.0f%% of the size", cloudSteps, std::max(cloudResolution, 0.35f) * 100.0f);
            break;
        case Group::Detail:
            std::snprintf(text, sizeof(text), "Lasts %.0f%% as far%s", detailDistance * 100.0f, lightProbes ? "; baked light probes" : "; no light probes");
            break;
    }
    return text;
}

// ---------------------------------------------------------------- on the renderer

void GameQuality::dynamicResolutionFor(bool& isOn, float& target, float& lowest) const {
    if (isOn) return;
    isOn = dynamicResolution;
    target = targetFrameRate;
    lowest = lowestScale;
}

void GameQuality::applyTo(RenderSettings& settings) const {
    settings.sampleCount = antiAliasing >= 4 ? 4 : antiAliasing >= 2 ? 2 : 1;
    // A scene may draw itself smaller still (a game's own menu does, through its settings): the two multiply.
    settings.renderScale = std::clamp(settings.renderScale * renderScale, 0.25f, 2.0f);
    settings.metalFXUpscaling = metalFX;
    dynamicResolutionFor(settings.dynamicResolution, settings.targetFrameRate, settings.lowestDynamicScale);
    settings.followsDeviceHeat = followsDeviceHeat;
    settings.shadowsEnabled = settings.shadowsEnabled && shadows;
    // As far as the scene asks, times this level's reach: the scene's distance always counts.
    settings.cascades.shadowDistance = std::max(settings.cascades.shadowDistance * std::clamp(shadowReach, 0.1f, 4.0f), 1.0f);
    settings.cascades.resolution = shadowResolution;
    settings.cascades.count = std::clamp(shadowCascades, 1, 4);
    settings.lightShadowResolution = std::clamp(lightShadowResolution, 64, 2048);
    settings.shadowedLights = std::clamp(shadowedLights, 0, 48);
    // Ray tracing is the project's alone: what the scene file says of it (scenes of before) is not asked.
    settings.rayTracedShadows = rayTraced && tracedShadows;
    settings.shadowRayCount = std::clamp(shadowRays, 1, 32);
    settings.rayTracedOcclusion = rayTraced && tracedOcclusion;
    settings.occlusionRayCount = std::clamp(occlusionRays, 1, 16);
    settings.rayTracedBounce = rayTraced && tracedBounce;
    settings.bounceIntensity = 1.0f;
    settings.rayTracedReflections = rayTraced && tracedReflections;
    settings.reflectionMaxRoughness = std::clamp(reflectionRoughness, 0.0f, 1.0f);
    settings.reflectionStrength = 1.0f;
    settings.ambientOcclusionEnabled = settings.ambientOcclusionEnabled && ambientOcclusion;
    settings.bloomEnabled = settings.bloomEnabled && bloom;
    settings.motionBlur = settings.motionBlur && motionBlur;
    settings.depthOfField = settings.depthOfField && depthOfField;
    settings.volumetricLights = lightsInAir;
    settings.usesLightProbes = lightProbes;
    settings.sky.cloudsHaveDepth = settings.sky.cloudsHaveDepth && volumetricClouds;
    settings.sky.cloudSteps = std::clamp(cloudSteps, 16, 128);
    settings.sky.cloudResolution = std::clamp(cloudResolution, 0.35f, 1.0f);
    // Shafts of light: the scene says whether it has them, the quality how finely they are drawn.
    settings.fogShaftSteps = settings.fogShaftSteps > 0 ? std::max(lightShafts, 0) : 0;
    detailDistanceScale() = std::max(detailDistance, 0.05f);
}

// ---------------------------------------------------------------- the file

std::vector<std::string> GameQuality::toLines() const {
    auto number = [](const char* key, double value) {
        char line[96];
        std::snprintf(line, sizeof line, "%s %g", key, value);
        return std::string(line);
    };
    auto flag = [](const char* key, bool value) { return std::string(key) + (value ? " 1" : " 0"); };
    return {number("renderScale", renderScale), flag("metalFX", metalFX), flag("dynamicResolution", dynamicResolution),
            number("targetFrameRate", targetFrameRate), number("lowestScale", lowestScale), flag("followsDeviceHeat", followsDeviceHeat),
            number("antiAliasing", antiAliasing), flag("shadows", shadows), number("shadowReach", shadowReach),
            number("shadowResolution", shadowResolution), number("shadowCascades", shadowCascades),
            number("lightShadowResolution", lightShadowResolution), number("shadowedLights", shadowedLights), flag("rayTraced", rayTraced),
            flag("tracedShadows", tracedShadows), number("shadowRays", shadowRays), flag("tracedOcclusion", tracedOcclusion),
            number("occlusionRays", occlusionRays), flag("tracedBounce", tracedBounce), flag("tracedReflections", tracedReflections),
            number("reflectionRoughness", reflectionRoughness), flag("ambientOcclusion", ambientOcclusion), flag("bloom", bloom),
            number("lightShafts", lightShafts), flag("lightsInAir", lightsInAir), flag("motionBlur", motionBlur),
            flag("depthOfField", depthOfField), flag("volumetricClouds", volumetricClouds), number("cloudSteps", cloudSteps),
            number("cloudResolution", cloudResolution), number("detailDistance", detailDistance), flag("lightProbes", lightProbes)};
}

bool GameQuality::readLine(const std::vector<std::string>& words) {
    if (words.size() < 2) return false;
    const std::string& key = words[0];
    float n = std::strtof(words[1].c_str(), nullptr);
    bool on = words[1] != "0";
    if (key == "renderScale") renderScale = n;
    else if (key == "metalFX") metalFX = on;
    else if (key == "dynamicResolution") dynamicResolution = on;
    else if (key == "targetFrameRate") targetFrameRate = n;
    else if (key == "lowestScale") lowestScale = n;
    else if (key == "followsDeviceHeat") followsDeviceHeat = on;
    else if (key == "antiAliasing") antiAliasing = int(n);
    else if (key == "shadows") shadows = on;
    else if (key == "shadowReach") shadowReach = n;
    // Files of before said meters, a limit for every scene: the share of the 150 m it was at High.
    else if (key == "shadowDistance") shadowReach = std::clamp(n / 150.0f, 0.25f, 2.0f);
    else if (key == "shadowResolution") shadowResolution = int(n);
    else if (key == "shadowCascades") shadowCascades = int(n);
    else if (key == "lightShadowResolution") lightShadowResolution = int(n);
    else if (key == "shadowedLights") shadowedLights = int(n);
    else if (key == "rayTraced") rayTraced = on;
    else if (key == "tracedShadows") tracedShadows = on;
    else if (key == "shadowRays") shadowRays = int(n);
    else if (key == "tracedOcclusion") tracedOcclusion = on;
    else if (key == "occlusionRays") occlusionRays = int(n);
    else if (key == "tracedBounce") tracedBounce = on;
    else if (key == "tracedReflections") tracedReflections = on;
    else if (key == "reflectionRoughness") reflectionRoughness = n;
    else if (key == "rayTracing") {
        // Version 2 said how well in one word: the rays and roughness that level had.
        static const char* const names[4] = {"Low", "Medium", "High", "Ultra"};
        static const int edge[4] = {4, 6, 8, 12}, around[4] = {1, 1, 2, 3};
        static const float roughest[4] = {0.15f, 0.25f, 0.4f, 0.55f};
        for (int i = 0; i < 4; i++) {
            if (words[1] == names[i]) shadowRays = edge[i], occlusionRays = around[i], reflectionRoughness = roughest[i];
        }
    }
    else if (key == "ambientOcclusion") ambientOcclusion = on;
    else if (key == "bloom") bloom = on;
    else if (key == "lightShafts") lightShafts = int(n);
    else if (key == "lightsInAir") lightsInAir = on;
    else if (key == "motionBlur") motionBlur = on;
    else if (key == "depthOfField") depthOfField = on;
    else if (key == "volumetricClouds") volumetricClouds = on;
    else if (key == "cloudSteps") cloudSteps = int(n);
    else if (key == "cloudResolution") cloudResolution = n;
    else if (key == "detailDistance") detailDistance = n;
    else if (key == "lightProbes") lightProbes = on;
    else return false;                  // from a newer engine: skipped
    return true;
}

QualityLevels QualityLevels::standard() {
    QualityLevels all;
    for (GameQuality::Level level : {GameQuality::Level::Low, GameQuality::Level::Medium, GameQuality::Level::High, GameQuality::Level::Ultra}) {
        all.levels.push_back(GameQuality::preset(level));
    }
    all.start = "High";
    return all;
}

const GameQuality* QualityLevels::find(const std::string& name) const {
    for (const GameQuality& level : levels)
        if (level.name == name) return &level;
    return nullptr;
}

GameQuality* QualityLevels::find(const std::string& name) {
    for (GameQuality& level : levels)
        if (level.name == name) return &level;
    return nullptr;
}

int QualityLevels::indexOf(const std::string& name) const {
    for (size_t i = 0; i < levels.size(); i++)
        if (levels[i].name == name) return int(i);
    return -1;
}

const GameQuality& QualityLevels::starting() const {
    static const GameQuality fallback = GameQuality::preset(GameQuality::Level::High);
    if (const GameQuality* found = find(start)) return *found;
    return levels.empty() ? fallback : levels.front();
}

std::string QualityLevels::freeName(const std::string& wanted) const {
    std::string base = wanted.empty() ? std::string("Level") : wanted;
    if (!find(base)) return base;
    for (int n = 2;; n++) {
        std::string name = base + " " + std::to_string(n);
        if (!find(name)) return name;
    }
}

std::vector<std::string> QualityLevels::names() const {
    std::vector<std::string> out;
    for (const GameQuality& level : levels) out.push_back(level.name);
    return out;
}

std::optional<QualityLevels> QualityLevels::parse(const std::string& text) {
    std::vector<std::vector<std::string>> lines = text::parseLines(text);
    if (lines.empty() || lines[0].empty() || lines[0][0] != "molten-quality") return std::nullopt;
    int version = lines[0].size() > 1 ? std::max(std::atoi(lines[0][1].c_str()), 1) : 1;
    if (version > text::newestVersion("molten-quality")) return std::nullopt;

    if (version < 3) {
        // One level of values: the standard four, and that one as the file says it.
        QualityLevels all = standard();
        std::string named;
        GameQuality one = *all.find("High");
        for (size_t i = 1; i < lines.size(); i++) {
            const std::vector<std::string>& words = lines[i];
            if (words.empty()) continue;
            if (words[0] == "level" && words.size() > 1) {
                named = words[1];
                if (const GameQuality* preset = all.find(named)) one = *preset;
            } else {
                one.readLine(words);
            }
        }
        // A file of before the groups (version 1) at one of the four: that level as it is now. Any
        // other (values of its own, "Custom") is those values, as High.
        bool isOneOfThem = !named.empty() && all.find(named);
        if (version >= 2 || !isOneOfThem) {
            if (!isOneOfThem) named = "High";
            one.name = named;
            *all.find(named) = one;
        }
        all.start = named;
        return all;
    }

    QualityLevels all;
    GameQuality* current = nullptr;
    for (size_t i = 1; i < lines.size(); i++) {
        const std::vector<std::string>& words = lines[i];
        if (words.empty()) continue;
        if (words[0] == "start" && words.size() > 1) {
            all.start = words[1];
        } else if (words[0] == "level" && words.size() > 1) {
            // A level starts from the one of the standard four of its name (or High), and the
            // file says the rest: a value a newer engine added is the preset's here.
            GameQuality level = GameQuality::preset(GameQuality::Level::High);
            for (GameQuality::Level known : {GameQuality::Level::Low, GameQuality::Level::Medium, GameQuality::Level::Ultra}) {
                if (words[1] == GameQuality::levelName(known)) level = GameQuality::preset(known);
            }
            level.name = all.freeName(words[1]);
            all.levels.push_back(level);
            current = &all.levels.back();
        } else if (current) {
            current->readLine(words);
        }
    }
    if (all.levels.empty()) return standard();
    if (!all.find(all.start)) all.start = all.levels.front().name;
    return all;
}

std::string QualityLevels::toText() const {
    std::string out = "molten-quality 3\n";
    out += "start " + text::quote(start) + "\n";
    for (const GameQuality& level : levels) {
        out += "level " + text::quote(level.name) + "\n";
        for (const std::string& line : level.toLines()) out += "  " + line + "\n";
    }
    return out;
}

}  // namespace molten
