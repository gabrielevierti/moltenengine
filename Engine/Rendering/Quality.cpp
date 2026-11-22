#include "Rendering/Quality.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
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
        case Level::Low: return "For the slowest machines the game should run on: fewer pixels, short soft shadows, no ray tracing, flat clouds.";
        case Level::Medium: return "For most laptops, phones and tablets: the sun's shadows ray traced where a scene asks, clouds with depth walked coarsely.";
        case Level::High: return "For a recent Mac: every pixel drawn, the light around things and reflections ray traced where a scene asks.";
        case Level::Ultra: return "The best there is: smoothest edges, sharpest and farthest shadows, bounced light, the finest clouds.";
        case Level::Custom: break;
    }
    return "Each group at a level of its own, or values set one by one.";
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

GameQuality GameQuality::preset(Level level) {
    GameQuality q;                      // High: the defaults
    q.level = level;
    switch (level) {
        case Level::Low:
            q.renderScale = 0.75f;
            q.antiAliasing = 1;
            q.shadowDistance = 40, q.shadowResolution = 1024, q.shadowCascades = 2;
            q.rayTracing = LightingQuality::Low;
            q.ambientOcclusion = false, q.lightShafts = 0;
            q.volumetricClouds = false, q.cloudSteps = 32, q.cloudResolution = 0.4f;
            q.detailDistance = 0.6f;
            break;
        case Level::Medium:
            q.renderScale = 0.9f;
            q.shadowDistance = 80, q.shadowCascades = 3;
            q.rayTracing = LightingQuality::Medium;
            q.lightShafts = 8;
            q.cloudSteps = 32, q.cloudResolution = 0.4f;
            q.detailDistance = 0.8f;
            break;
        case Level::High: break;
        case Level::Ultra:
            q.lowestScale = 0.6f;
            q.antiAliasing = 4;
            q.shadowDistance = 400, q.shadowResolution = 4096;
            q.rayTracing = LightingQuality::Ultra;
            q.lightShafts = 32;
            q.cloudSteps = 72, q.cloudResolution = 0.75f;
            q.detailDistance = 1.5f;
            break;
        case Level::Custom: break;
    }
    return q;
}

void GameQuality::setGroup(Group group, Level wanted) {
    if (wanted == Level::Custom) return;
    GameQuality from = preset(wanted);
    switch (group) {
        case Group::Resolution:
            renderScale = from.renderScale, metalFX = from.metalFX, dynamicResolution = from.dynamicResolution;
            targetFrameRate = from.targetFrameRate, lowestScale = from.lowestScale, followsDeviceHeat = from.followsDeviceHeat;
            break;
        case Group::AntiAliasing: antiAliasing = from.antiAliasing; break;
        case Group::Shadows:
            shadows = from.shadows, shadowDistance = from.shadowDistance, shadowResolution = from.shadowResolution, shadowCascades = from.shadowCascades;
            break;
        case Group::RayTracing: rayTracing = from.rayTracing; break;
        case Group::Effects: ambientOcclusion = from.ambientOcclusion, bloom = from.bloom, lightShafts = from.lightShafts; break;
        case Group::Sky: volumetricClouds = from.volumetricClouds, cloudSteps = from.cloudSteps, cloudResolution = from.cloudResolution; break;
        case Group::Detail: detailDistance = from.detailDistance; break;
    }
    findLevel();
}

bool GameQuality::isAt(Group group, Level candidate) const {
    if (candidate == Level::Custom) return false;
    GameQuality p = preset(candidate);
    switch (group) {
        case Group::Resolution:
            return renderScale == p.renderScale && metalFX == p.metalFX && dynamicResolution == p.dynamicResolution &&
                   (!dynamicResolution || (targetFrameRate == p.targetFrameRate && lowestScale == p.lowestScale)) && followsDeviceHeat == p.followsDeviceHeat;
        case Group::AntiAliasing: return antiAliasing == p.antiAliasing;
        case Group::Shadows:
            return shadows == p.shadows && shadowDistance == p.shadowDistance && shadowResolution == p.shadowResolution && shadowCascades == p.shadowCascades;
        case Group::RayTracing: return rayTracing == p.rayTracing;
        case Group::Effects: return ambientOcclusion == p.ambientOcclusion && bloom == p.bloom && lightShafts == p.lightShafts;
        case Group::Sky: return volumetricClouds == p.volumetricClouds && (!volumetricClouds || (cloudSteps == p.cloudSteps && cloudResolution == p.cloudResolution));
        case Group::Detail: return detailDistance == p.detailDistance;
    }
    return false;
}

GameQuality::Level GameQuality::levelOf(Group group) const {
    if (isAt(group, level)) return level;
    for (Level candidate : {Level::Low, Level::Medium, Level::High, Level::Ultra}) {
        if (isAt(group, candidate)) return candidate;
    }
    return Level::Custom;
}

void GameQuality::findLevel() {
    // The level every group is at. The one it was said to be at first: of two that fit it stays.
    Level before = level;
    level = Level::Custom;
    for (Level candidate : {before, Level::Low, Level::Medium, Level::High, Level::Ultra}) {
        if (candidate == Level::Custom) continue;
        bool all = true;
        for (int i = 0; i < kGroupCount; i++) all = all && isAt(Group(i), candidate);
        if (all) {
            level = candidate;
            return;
        }
    }
}

std::string GameQuality::summaryOf(Group group) const {
    char text[160] = "";
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
            else std::snprintf(text, sizeof(text), "Up to %.0f m, %d px, %d slice%s", shadowDistance, shadowResolution, shadowCascades, shadowCascades == 1 ? "" : "s");
            break;
        case Group::RayTracing:
            std::snprintf(text, sizeof(text), "%s", rayTracing == LightingQuality::Low      ? "None: shadow maps"
                                                    : rayTracing == LightingQuality::Medium ? "The sun's shadows"
                                                    : rayTracing == LightingQuality::High   ? "Shadows, the light around things, reflections"
                                                                                            : "All a scene asks, bounced light too");
            break;
        case Group::Effects: {
            std::string parts;
            auto add = [&](const std::string& part) { parts += (parts.empty() ? "" : ", ") + part; };
            if (ambientOcclusion) add("Ambient occlusion");
            if (bloom) add(parts.empty() ? "Bloom" : "bloom");
            if (lightShafts > 0) add(std::string(parts.empty() ? "Light" : "light") + " shafts in " + std::to_string(lightShafts) + " steps");
            std::snprintf(text, sizeof(text), "%s", parts.empty() ? "None" : parts.c_str());
            break;
        }
        case Group::Sky:
            if (!volumetricClouds) std::snprintf(text, sizeof(text), "Flat clouds");
            else std::snprintf(text, sizeof(text), "Clouds with depth: %d steps at %.0f%% of the size", cloudSteps, cloudResolution * 100.0f);
            break;
        case Group::Detail: std::snprintf(text, sizeof(text), "Lasts %.0f%% as far", detailDistance * 100.0f); break;
    }
    return text;
}

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
    // As far as the scene asks, and no farther than this level reaches.
    settings.cascades.shadowDistance = std::min(settings.cascades.shadowDistance, std::max(shadowDistance, 1.0f));
    settings.cascades.resolution = shadowResolution;
    settings.cascades.count = std::clamp(shadowCascades, 1, 4);
    limitLightingQuality(settings, rayTracing);
    settings.ambientOcclusionEnabled = settings.ambientOcclusionEnabled && ambientOcclusion;
    settings.bloomEnabled = settings.bloomEnabled && bloom;
    settings.sky.cloudsHaveDepth = settings.sky.cloudsHaveDepth && volumetricClouds;
    settings.sky.cloudSteps = std::clamp(cloudSteps, 16, 128);
    settings.sky.cloudResolution = std::clamp(cloudResolution, 0.2f, 1.0f);
    settings.fogShaftSteps = std::min(settings.fogShaftSteps, lightShafts);
    detailDistanceScale() = std::max(detailDistance, 0.05f);
}

std::optional<GameQuality> GameQuality::parse(const std::string& text) {
    GameQuality q;
    bool hasHeader = false;
    int version = 1;
    Level named = Level::Custom;
    for (const std::vector<std::string>& words : text::parseLines(text)) {
        if (words.empty()) continue;
        const std::string& key = words[0];
        auto number = [&](float otherwise) { return words.size() > 1 ? std::strtof(words[1].c_str(), nullptr) : otherwise; };
        auto flag = [&](bool otherwise) { return words.size() > 1 ? words[1] != "0" : otherwise; };
        auto levelNamed = [&](Level otherwise) {
            for (Level level : {Level::Low, Level::Medium, Level::High, Level::Ultra}) {
                if (words.size() > 1 && words[1] == levelName(level)) return level;
            }
            return otherwise;
        };
        if (key == "molten-quality") {
            hasHeader = true;
            version = std::max(int(number(1)), 1);
        } else if (key == "level") {
            named = levelNamed(Level::Custom);
            // The level first, then what the file says of each value: what it leaves out is the level's.
            if (named != Level::Custom) q = preset(named);
        }
        else if (key == "antiAliasing") q.antiAliasing = int(number(2));
        else if (key == "renderScale") q.renderScale = number(1);
        else if (key == "metalFX") q.metalFX = flag(true);
        else if (key == "dynamicResolution") q.dynamicResolution = flag(true);
        else if (key == "targetFrameRate") q.targetFrameRate = number(60);
        else if (key == "lowestScale") q.lowestScale = number(0.5f);
        else if (key == "followsDeviceHeat") q.followsDeviceHeat = flag(true);
        else if (key == "shadows") q.shadows = flag(true);
        else if (key == "shadowDistance") q.shadowDistance = number(150);
        else if (key == "shadowResolution") q.shadowResolution = int(number(2048));
        else if (key == "shadowCascades") q.shadowCascades = int(number(4));
        else if (key == "rayTracing") {
            Level most = levelNamed(Level::High);
            q.rayTracing = most == Level::Low ? LightingQuality::Low : most == Level::Medium ? LightingQuality::Medium : most == Level::Ultra ? LightingQuality::Ultra : LightingQuality::High;
        }
        else if (key == "ambientOcclusion") q.ambientOcclusion = flag(true);
        else if (key == "bloom") q.bloom = flag(true);
        else if (key == "volumetricClouds") q.volumetricClouds = flag(true);
        else if (key == "cloudSteps") q.cloudSteps = int(number(48));
        else if (key == "cloudResolution") q.cloudResolution = number(0.5f);
        else if (key == "lightShafts") q.lightShafts = int(number(16));
        else if (key == "detailDistance") q.detailDistance = number(1);
    }
    if (!hasHeader) return std::nullopt;
    // A file of before the groups, at a level: that level as it is now. (Its values were the
    // level's own of then: shadows that reached 60 meters and no farther, whatever the scene.)
    if (version < 2 && named != Level::Custom) return preset(named);
    q.findLevel();
    return q;
}

std::string GameQuality::toText() const {
    auto lightingName = [](LightingQuality most) {
        return most == LightingQuality::Low ? "Low" : most == LightingQuality::Medium ? "Medium" : most == LightingQuality::Ultra ? "Ultra" : "High";
    };
    std::ostringstream out;
    out << "molten-quality 2\n"
        << "level " << levelName(level) << "\n"
        << "renderScale " << renderScale << "\n"
        << "metalFX " << (metalFX ? 1 : 0) << "\n"
        << "dynamicResolution " << (dynamicResolution ? 1 : 0) << "\n"
        << "targetFrameRate " << targetFrameRate << "\n"
        << "lowestScale " << lowestScale << "\n"
        << "followsDeviceHeat " << (followsDeviceHeat ? 1 : 0) << "\n"
        << "antiAliasing " << antiAliasing << "\n"
        << "shadows " << (shadows ? 1 : 0) << "\n"
        << "shadowDistance " << shadowDistance << "\n"
        << "shadowResolution " << shadowResolution << "\n"
        << "shadowCascades " << shadowCascades << "\n"
        << "rayTracing " << lightingName(rayTracing) << "\n"
        << "ambientOcclusion " << (ambientOcclusion ? 1 : 0) << "\n"
        << "bloom " << (bloom ? 1 : 0) << "\n"
        << "lightShafts " << lightShafts << "\n"
        << "volumetricClouds " << (volumetricClouds ? 1 : 0) << "\n"
        << "cloudSteps " << cloudSteps << "\n"
        << "cloudResolution " << cloudResolution << "\n"
        << "detailDistance " << detailDistance << "\n";
    return out.str();
}

}  // namespace molten
