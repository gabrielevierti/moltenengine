// Quality.hpp
// How well the game is drawn, and at what cost: the project's own (Project Settings > Quality),
// the same for every scene of it. Kept in Settings/Quality.settings.
//
// A scene says what it wants to look like: where its sun is, whether its clouds have depth,
// how far its shadows should reach. A quality level says how much of that the machine is asked
// for, and what a scene has no say in: how many pixels are drawn, how sharp the shadow maps are,
// which of the light is found with rays.
//
// The levels are the game's own. A new project has four (Low, Medium, High, Ultra) and the game's
// maker sets every value of each, renames them, adds more ("Handheld", "Cinematic"), takes some
// away. One of them is the level the game starts at; a game's own settings menu lists the levels
// and lets the player choose (World::qualityLevels, World::setQualityLevel).
//
// Each level has every value, in seven groups:
//
//   Resolution      how many pixels are drawn before the picture is made the screen's size,
//                   and whether fewer are drawn while the GPU cannot keep up
//   Anti-Aliasing   how smooth the edges of things are
//   Shadows         how far the shadows reach (a share of what each scene asks), how sharp they are
//   Ray Tracing     on or off, and each part of it: the sun's shadows, the light around things,
//                   the light it bounces, reflections, and how many rays each sends
//   Effects         ambient occlusion, bloom, shafts of light, lights in the air, motion blur, depth of field
//   Sky             whether clouds have depth, and how finely they are walked through
//   Detail          how far the detail of far things lasts, and baked light probes
//
//   molten-quality 3
//   start "High"
//   level "Low"
//     renderScale 0.85
//     ...
#pragma once

#include "Rendering/Renderer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace molten {

/// One quality level: every value the quality sets.
struct GameQuality {
    /// The levels a new project has, and what "Copy From" offers.
    enum class Level { Low, Medium, High, Ultra, Custom };
    enum class Group { Resolution, AntiAliasing, Shadows, RayTracing, Effects, Sky, Detail };
    static constexpr int kGroupCount = 7;

    std::string name = "High";

    // ---- Resolution
    float renderScale = 1.0f;           // the scene drawn smaller (faster) and made the screen's size
    bool metalFX = true;                // made larger by MetalFX, sharp, instead of stretched
    bool dynamicResolution = true;      // smaller while the GPU cannot keep up with the frame rate below
    float targetFrameRate = 60;
    float lowestScale = 0.6f;           // and never smaller than this share of renderScale
    bool followsDeviceHeat = true;      // smaller while the device is hot
    // ---- Anti-aliasing
    int antiAliasing = 2;               // multisampling: 1 off, 2 or 4
    // ---- Shadows
    bool shadows = true;
    float shadowReach = 1.0f;           // how far the sun's shadows reach, as a share of how far each scene asks (Lighting > Shadows): 0.5 half as far
    int shadowResolution = 2048;        // the shadow map's edge
    int shadowCascades = 4;             // slices: more is sharper near and far
    int lightShadowResolution = 512;    // point and spot lights that cast shadows: the edge of each one's map
    int shadowedLights = 8;             // and how many at once, the nearest and brightest first
    // ---- Ray tracing: on or off, and each part of it. Where the GPU cannot trace rays, shadow
    // maps (the Shadows group), the screen's own ambient occlusion, reflections of the sky.
    bool rayTraced = true;
    bool tracedShadows = true;          // the sun's shadows, sharp where things touch and soft away from them
    int shadowRays = 8;                 // rays toward the sun along shadows' edges (one is enough elsewhere)
    bool tracedOcclusion = true;        // the light around things: hidden under a car, in a corner
    int occlusionRays = 2;              // rays a pixel for it
    bool tracedBounce = true;           // and the light a sunlit wall throws on the floor
    bool tracedReflections = true;      // what is around, mirrored
    float reflectionRoughness = 0.4f;   // mirrored on surfaces smoother than this
    // ---- Effects
    bool ambientOcclusion = true;       // the screen's own
    bool bloom = true;
    int lightShafts = 16;               // the most steps through the fog for beams of light; 0 none
    bool lightsInAir = true;            // point and spot lights that light the fog they pass through (Light::isVolumetric)
    bool motionBlur = true;             // where a scene has it
    bool depthOfField = true;           // where a scene has it
    // ---- Sky
    bool volumetricClouds = true;       // clouds with depth, where a scene has them
    int cloudSteps = 48;                // walked through in this many steps
    float cloudResolution = 0.5f;       // into a picture this share of the screen's size (never less than 35%)
    // ---- Detail
    float detailDistance = 1.0f;        // how far detail lasts (LOD groups, foliage): 0.5 half as far, 2 twice
    bool lightProbes = true;            // the light baked into a scene's Light Probe Volumes

    /// The four a new project has: Low, Medium, High, Ultra.
    static GameQuality preset(Level level);
    /// Sets one group's values as `from` has them.
    void copyGroup(Group group, const GameQuality& from);
    /// Whether a group's values are those of `other`.
    bool sameGroup(Group group, const GameQuality& other) const;
    /// What a group's values come to, in a line: "2048 px, 4 slices, up to 150 m".
    std::string summaryOf(Group group) const;

    /// Puts it on the renderer's settings, over what the scene said.
    void applyTo(RenderSettings& settings) const;
    /// Whether the picture is drawn smaller while the GPU is behind, and how. A scene that asks
    /// for it itself (a game's own menu, through its settings) keeps what it says; for any
    /// other this level decides.
    void dynamicResolutionFor(bool& isOn, float& targetFrameRate, float& lowest) const;

    /// Its values as the file writes them, a line each ("renderScale 0.85").
    std::vector<std::string> toLines() const;
    /// Takes one such line; false for one it does not know (a newer engine's).
    bool readLine(const std::vector<std::string>& words);

    static const char* levelName(Level level);
    static const char* groupName(Group group);
    /// What a group is about, in a sentence.
    static const char* groupPurpose(Group group);
    /// What one of the four is for, in a line.
    static const char* levelPurpose(Level level);
};

/// The game's quality levels, and the one it starts at.
struct QualityLevels {
    std::vector<GameQuality> levels;
    std::string start = "High";         // the level a game starts at, until the player chooses another

    /// Low, Medium, High and Ultra, starting at High: what a new project has.
    static QualityLevels standard();
    /// The level of this name, or null.
    const GameQuality* find(const std::string& name) const;
    GameQuality* find(const std::string& name);
    int indexOf(const std::string& name) const;
    /// The level a game starts at (the first, if `start` names none).
    const GameQuality& starting() const;
    /// A name no level has: "High" again is "High 2".
    std::string freeName(const std::string& wanted) const;
    std::vector<std::string> names() const;

    /// Reads version 3, and the files of before: one level of values (version 2) becomes that
    /// level of the standard four, the others as they are.
    static std::optional<QualityLevels> parse(const std::string& text);
    std::string toText() const;
};

/// The global "how far detail lasts" the LOD groups and foliage multiply their distances by.
float& detailDistanceScale();

/// Dynamic resolution's next scale: `scale` now, with the GPU taking `gpuMilliseconds` a frame
/// against a frame rate to keep. The cost of a frame goes with its pixels, the square of the
/// scale: so when the GPU is behind the scale goes at once to where it should fit, in steps
/// of a twentieth; and back up a step at a time once there is clear room, so that it does not
/// swing between two sizes. Never under `lowest`, never over 1.
float nextDynamicScale(float scale, float gpuMilliseconds, float targetFrameRate, float lowest);

}  // namespace molten
