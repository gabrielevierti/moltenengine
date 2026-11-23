#include "Rendering/Quality.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace molten {

namespace {
const char* lightingName(LightingQuality most) {
    return most == LightingQuality::Low ? "Low" : most == LightingQuality::Medium ? "Medium" : most == LightingQuality::Ultra ? "Ultra" : "High";
}
}  // namespace

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
        case Level::Low: return "For the slowest machines the game should run on: a little smaller and made sharp by MetalFX, shadow maps near the camera, no ray tracing, clouds walked coarsely.";
        case Level::Medium: return "For most laptops, phones and tablets: the light ray traced where the GPU can, with fewer rays; clouds with depth walked coarsely.";
        case Level::High: return "For a recent Mac: every pixel drawn, all of the light ray traced.";
        case Level::Ultra: return "The best there is, at 60 frames a second on a recent Mac: smoothest edges, the most rays, rougher things mirrored, shadows to 250 m, the finest clouds and light shafts. Every pixel while the GPU keeps up.";
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

void GameQuality::setRayTracing(LightingQuality wanted) {
    if (wanted != LightingQuality::Custom) rayTracing = wanted;
}

GameQuality GameQuality::preset(Level level) {
    GameQuality q;                      // High: the defaults
    q.level = level;
    switch (level) {
        case Level::Low:
            // Still a good picture: drawn a little smaller and made sharp again by MetalFX, real
            // shadows near the camera, the screen's ambient occlusion, clouds with depth walked coarsely.
            q.renderScale = 0.8f;
            q.antiAliasing = 1;
            q.shadowReach = 0.5f, q.shadowResolution = 2048, q.shadowCascades = 2;
            q.lightShadowResolution = 256, q.shadowedLights = 4;
            q.rayTraced = false, q.setRayTracing(LightingQuality::Low);
            q.lightShafts = 8;
            q.cloudSteps = 24, q.cloudResolution = 0.35f;
            q.detailDistance = 0.7f;
            break;
        case Level::Medium:
            q.renderScale = 0.9f;
            q.shadowReach = 0.75f, q.shadowCascades = 3;
            q.shadowedLights = 6;
            q.setRayTracing(LightingQuality::Medium);
            q.lightShafts = 12;
            q.cloudSteps = 32, q.cloudResolution = 0.4f;
            q.detailDistance = 0.85f;
            break;
        case Level::High: break;
        case Level::Ultra:
            // The best there is, at 60 frames a second on a recent Mac (an M3): every pixel while
            // the GPU keeps up, a little fewer, made sharp by MetalFX, while it cannot.
            q.lowestScale = 0.65f;
            q.antiAliasing = 4;
            q.shadowReach = 1.5f, q.shadowResolution = 4096;
            q.lightShadowResolution = 1024, q.shadowedLights = 12;
            q.setRayTracing(LightingQuality::Ultra);
            q.lightShafts = 24;
            q.cloudSteps = 64, q.cloudResolution = 0.5f;
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
            shadows = from.shadows, shadowReach = from.shadowReach, shadowResolution = from.shadowResolution, shadowCascades = from.shadowCascades;
            lightShadowResolution = from.lightShadowResolution, shadowedLights = from.shadowedLights;
            break;
        case Group::RayTracing: rayTraced = from.rayTraced, rayTracing = from.rayTracing; break;
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
            return shadows == p.shadows && shadowReach == p.shadowReach && shadowResolution == p.shadowResolution && shadowCascades == p.shadowCascades &&
                   lightShadowResolution == p.lightShadowResolution && shadowedLights == p.shadowedLights;
        case Group::RayTracing: return rayTraced == p.rayTraced && (!rayTraced || rayTracing == p.rayTracing);
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
            else std::snprintf(text, sizeof(text), "%.0f%% as far as each scene asks, %d px, %d slice%s; %d lights", shadowReach * 100.0f, shadowResolution,
                               shadowCascades, shadowCascades == 1 ? "" : "s", shadowedLights);
            break;
        case Group::RayTracing:
            if (!rayTraced) std::snprintf(text, sizeof(text), "Off: shadow maps");
            else std::snprintf(text, sizeof(text), "%s: shadows, light around things, bounced light, reflections", lightingName(rayTracing));
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
    // As far as the scene asks, times this level's reach: the scene's distance always counts.
    settings.cascades.shadowDistance = std::max(settings.cascades.shadowDistance * std::clamp(shadowReach, 0.1f, 4.0f), 1.0f);
    settings.cascades.resolution = shadowResolution;
    settings.cascades.count = std::clamp(shadowCascades, 1, 4);
    settings.lightShadowResolution = std::clamp(lightShadowResolution, 64, 2048);
    settings.shadowedLights = std::clamp(shadowedLights, 0, 48);
    // Ray tracing is the project's alone: what the scene file says of it (scenes of before) is not
    // asked. On, all of it, as the level has it, at full strength.
    if (rayTraced) {
        applyLightingQuality(settings, rayTracing);
        settings.bounceIntensity = 1.0f;
        settings.reflectionStrength = 1.0f;
    } else {
        settings.rayTracedShadows = settings.rayTracedOcclusion = settings.rayTracedBounce = settings.rayTracedReflections = false;
    }
    settings.ambientOcclusionEnabled = settings.ambientOcclusionEnabled && ambientOcclusion;
    settings.bloomEnabled = settings.bloomEnabled && bloom;
    settings.sky.cloudsHaveDepth = settings.sky.cloudsHaveDepth && volumetricClouds;
    settings.sky.cloudSteps = std::clamp(cloudSteps, 16, 128);
    settings.sky.cloudResolution = std::clamp(cloudResolution, 0.2f, 1.0f);
    // Shafts of light: the scene says whether it has them, the quality how finely they are drawn.
    settings.fogShaftSteps = settings.fogShaftSteps > 0 ? std::max(lightShafts, 0) : 0;
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
        else if (key == "shadowReach") q.shadowReach = number(1);
        // Files of before said meters, a limit for every scene: the share of the 150 m it was at High.
        else if (key == "shadowDistance") q.shadowReach = std::clamp(number(150) / 150.0f, 0.25f, 2.0f);
        else if (key == "shadowResolution") q.shadowResolution = int(number(2048));
        else if (key == "shadowCascades") q.shadowCascades = int(number(4));
        else if (key == "lightShadowResolution") q.lightShadowResolution = int(number(512));
        else if (key == "shadowedLights") q.shadowedLights = int(number(8));
        else if (key == "rayTracing") {
            Level most = levelNamed(Level::High);
            q.setRayTracing(most == Level::Low ? LightingQuality::Low : most == Level::Medium ? LightingQuality::Medium : most == Level::Ultra ? LightingQuality::Ultra : LightingQuality::High);
        }
        else if (key == "rayTraced") q.rayTraced = flag(true);
        // A file of before, that chose what was traced one by one: off if the sun's shadows were not.
        else if (key == "tracedShadows") q.rayTraced = flag(true);
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
        << "shadowReach " << shadowReach << "\n"
        << "shadowResolution " << shadowResolution << "\n"
        << "shadowCascades " << shadowCascades << "\n"
        << "lightShadowResolution " << lightShadowResolution << "\n"
        << "shadowedLights " << shadowedLights << "\n"
        << "rayTraced " << (rayTraced ? 1 : 0) << "\n"
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
