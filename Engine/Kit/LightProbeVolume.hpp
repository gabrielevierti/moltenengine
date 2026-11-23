// LightProbeVolume.hpp
// A Light Probe Volume, as Unity's light probe groups and Adaptive Probe Volumes: a box of the
// scene where light is baked into probes (Rendering/LightProbes.hpp), so that inside it rooms
// are as dark as their windows make them and walls glow with the light bounced off the floor.
// Put one round a building, a cave, a street between tall houses; outside every volume
// everything is lit by the sky as before.
//
// The box is centered on the entity, `size` meters along the world's axes (times the entity's
// scale; its rotation is not used). Lighting > Bake Light Probes bakes all of the scene's volumes
// into one `.lighting` file beside the scene, which the scene then names. What moves (bodies
// that are not static) and what is seen through is left out of the bake; what is drawn with it
// is lit by it, moving or not.
#pragma once

#include "Core/World.hpp"
#include "Rendering/LightProbes.hpp"

#include <functional>
#include <string>

namespace molten::kit {

struct LightProbeVolume {
    Vec3 size{20, 8, 20};
    float spacing = 2.0f;               // meters between probes: 1 for rooms, 4 or more for open ground
    float bounce = 1.0f;                // how much light the walls throw back: 1 as their colors say

    /// Where its probes go, in the world.
    ProbeVolumeDesc desc(const Transform& transform) const;
};

/// What baking a world's scene needs, read from it: its triangles, its lights, its volumes.
/// Read on the main thread (it reads the scene and the models' files); baked on any.
struct LightingBakeJob {
    BakeScene scene;
    std::vector<BakeLight> lights;
    std::vector<ProbeVolumeDesc> volumes;
    BakeSettings settings;
    size_t triangleCount() const { return scene.triangleCount(); }
};
/// `look` is the scene's sky, for how much of its light falls on what the rays hit.
LightingBakeJob prepareLightingBake(World& world, const struct RenderSettings& look, int raysPerProbe = 256);

/// Where a scene's baked light is kept: "Scenes/Main.scene" has "Scenes/Main.lighting".
std::string lightingFileFor(const std::string& sceneName);

void linkLightProbes();

}  // namespace molten::kit
