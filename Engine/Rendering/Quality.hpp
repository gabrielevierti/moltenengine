// Quality.hpp
// How well the game is drawn, and at what cost: the project's own (Project Settings > Quality),
// the same for every scene of it, as the game ships. Kept in Settings/Quality.settings.
//
// A scene says what it wants to look like: where its sun is, whether its clouds have depth,
// how far its shadows should reach, which of its light is found with rays. The quality says
// how much of that the machine is asked for. So most of what is here is a limit on what the
// scene asks, and the rest is what a scene has no say in: how many pixels are drawn, how
// sharp the shadow maps are.
//
// It comes in four levels, and in seven groups that can each be at a level of their own
// (the picture at High with the shadows at Medium), down to the single values:
//
//   Resolution      how many pixels are drawn before the picture is made the screen's size,
//                   and whether fewer are drawn while the GPU cannot keep up
//   Anti-Aliasing   how smooth the edges of things are
//   Shadows         how far the shadows reach (a share of what each scene asks), how sharp they are
//   Ray Tracing     on or off, and how well. On, all of the light is found with rays where
//                   the GPU can: the sun's shadows, the light around things and what it
//                   bounces, reflections; a higher level sends more rays. The project's
//                   alone: a scene does not choose it
//   Effects         ambient occlusion, bloom, shafts of light
//   Sky             whether clouds have depth, and how finely they are walked through
//   Detail          how far the detail of far things lasts
//
//   molten-quality 2
//   level High
//   renderScale 1
//   ...
#pragma once

#include "Rendering/Renderer.hpp"

#include <optional>
#include <string>

namespace molten {

struct GameQuality {
    enum class Level { Low, Medium, High, Ultra, Custom };
    enum class Group { Resolution, AntiAliasing, Shadows, RayTracing, Effects, Sky, Detail };
    static constexpr int kGroupCount = 7;

    /// The level of the whole: the one every group is at, or Custom.
    Level level = Level::High;

    // ---- Resolution
    float renderScale = 1.0f;           // the scene drawn smaller (faster) and made the screen's size
    bool metalFX = true;                // made larger by MetalFX, sharp, instead of stretched
    bool dynamicResolution = true;      // smaller while the GPU cannot keep up with the frame rate below
    float targetFrameRate = 60;
    float lowestScale = 0.5f;           // and never smaller than this share of renderScale
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
    // ---- Ray tracing: on or off, and how well, for every scene. On, all of the light is found
    // with rays: the sun's shadows, the light around things and what it bounces, reflections.
    // Where the GPU cannot trace rays, shadow maps (the Shadows group).
    bool rayTraced = true;
    LightingQuality rayTracing = LightingQuality::High;     // how well: Low to Ultra, more rays at each
    // ---- Effects
    bool ambientOcclusion = true;       // the screen's own
    bool bloom = true;
    int lightShafts = 16;               // the most steps through the fog for beams of light; 0 none
    // ---- Sky
    bool volumetricClouds = true;       // clouds with depth, where a scene has them
    int cloudSteps = 48;                // walked through in this many steps
    float cloudResolution = 0.5f;       // into a picture this share of the scene's size
    // ---- Detail
    float detailDistance = 1.0f;        // how far detail lasts (LOD groups, foliage): 0.5 half as far, 2 twice

    static GameQuality preset(Level level);
    /// One group set as `level` has it; the rest stays. Then `level` is found again.
    void setGroup(Group group, Level level);
    /// True if a group's values are those `level` has. Two levels may have the same (edges are
    /// as smooth at Medium as at High): a group can be at both.
    bool isAt(Group group, Level level) const;
    /// The level a group is at, or Custom if its values are no level's. Of two it is at, the
    /// level of the whole if that is one of them, else the lower.
    Level levelOf(Group group) const;
    /// Sets `level` to the one every group is at, or to Custom.
    void findLevel();
    /// What a group's values come to, in a line: "2048 px, 4 slices, up to 150 m".
    std::string summaryOf(Group group) const;

    /// How well rays are traced (on or off stays as it is).
    void setRayTracing(LightingQuality level);

    /// Puts it on the renderer's settings, over what the scene said.
    void applyTo(RenderSettings& settings) const;
    /// Whether the picture is drawn smaller while the GPU is behind, and how. A scene that asks
    /// for it itself (a game's own menu, through its settings) keeps what it says; for any
    /// other this level decides.
    void dynamicResolutionFor(bool& isOn, float& targetFrameRate, float& lowest) const;

    static std::optional<GameQuality> parse(const std::string& text);
    std::string toText() const;
    static const char* levelName(Level level);
    static const char* groupName(Group group);
    /// What a level is for, in a line.
    static const char* levelPurpose(Level level);
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
