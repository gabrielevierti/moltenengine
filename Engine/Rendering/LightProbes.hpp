// LightProbes.hpp
// Light baked into a grid of probes: how much of the sky each place sees, and the light that
// comes to it off what is around it. Without them every surface is lit by the whole sky,
// indoors as out, and a room with a small window is as bright as the field outside; with
// them the room is dark where the sky does not reach and warm where the sun on the floor
// throws its light back up onto the walls.
//
// A Light Probe Volume (Kit/LightProbeVolume.hpp) says where the probes go. Baking sends rays
// from each of them into the scene's triangles (on the processor, so it needs no GPU and is
// the same on every Mac), and keeps, for each of six directions, the share of the sky seen
// that way and the light bounced from that way. The sky itself is not baked: the renderer
// still lights with the sky of the moment, and the probes only say how much of it arrives.
// So a day that turns to evening still darkens the rooms it should, though their bounced
// light stays that of the light they were baked in.
//
// The probes are kept in a `.lighting` file next to the scene (Docs/FileFormats.md).
#pragma once

#include "Math/Math.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace molten {

/// One grid of probes: a box of places `spacing` meters apart.
struct ProbeGrid {
    Vec3 origin{0, 0, 0};       // the first probe
    float spacing = 2.0f;
    int countX = 1, countY = 1, countZ = 1;
    float fade = 1.0f;          // meters inside its edge over which it fades into the sky's plain light
    /// Six for each probe, x first, then y, then z: the directions +x, -x, +y, -y, +z, -z.
    /// xyz the light bounced to it from that side (as the sky's light map holds it, so that the
    /// two add); w the share of the sky seen on that side, 0 to 1.
    std::vector<Vec4> cells;

    size_t probeCount() const { return size_t(countX) * size_t(countY) * size_t(countZ); }
    size_t indexOf(int x, int y, int z) const { return (size_t(z) * size_t(countY) + size_t(y)) * size_t(countX) + size_t(x); }
    Vec3 placeOf(int x, int y, int z) const { return origin + Vec3{float(x), float(y), float(z)} * spacing; }
    Vec3 farCorner() const { return placeOf(countX - 1, countY - 1, countZ - 1); }
};

/// What a scene's probes hold, as read from its `.lighting` file.
struct BakedLighting {
    std::vector<ProbeGrid> grids;

    /// The light a surface at `place`, facing `normal`, has from its probes (blended across the
    /// grid, and into nothing at its edges): xyz bounced, w the sky seen. Outside every grid,
    /// (0, 0, 0, 1): the sky's light as it is. What the GPU does too (Lighting.metal, probeLight).
    Vec4 lightAt(Vec3 place, Vec3 normal) const;

    std::string toText() const;
    static std::optional<BakedLighting> fromText(const std::string& text, std::string* error = nullptr);
};

// ---- baking

/// A triangle of the scene as baking sees it.
struct BakeTriangle {
    Vec3 a, b, c;
    Vec3 albedo{0.5f, 0.5f, 0.5f};      // linear, what it gives back of the light it gets
    Vec3 emissive{0, 0, 0};             // the light it gives off itself
    bool isDoubleSided = false;
};

/// The scene's triangles, in a tree of boxes (a BVH) for rays to find what they hit quickly.
class BakeScene {
public:
    void add(const BakeTriangle& triangle) { triangles_.push_back(triangle); }
    void build();
    size_t triangleCount() const { return triangles_.size(); }

    struct Hit {
        float distance = 0;
        uint32_t triangle = 0;
        bool isBackFace = false;        // it struck the side a one-sided triangle does not show
        Vec3 normal{0, 1, 0};           // facing the ray
    };
    /// The nearest triangle along the ray within `reach`, if any. `direction` is unit length.
    std::optional<Hit> intersect(Vec3 origin, Vec3 direction, float reach) const;
    /// Whether anything is between the two (for shadows).
    bool isBlocked(Vec3 from, Vec3 to) const;
    const BakeTriangle& triangle(uint32_t index) const { return triangles_[index]; }

private:
    struct Node {
        AABB box;
        uint32_t first = 0, count = 0;  // a leaf: its triangles in order_; otherwise count is 0 and first is its right child
    };
    std::vector<BakeTriangle> triangles_;
    std::vector<uint32_t> order_;
    std::vector<Node> nodes_;
    void split(uint32_t node, int depth);
    template <typename Visit>
    void walk(Vec3 origin, Vec3 direction, float reach, Visit&& visit) const;
};

/// A light, as baking needs it.
struct BakeLight {
    enum Kind { Sun, Point, Spot } kind = Sun;
    Vec3 position{0, 0, 0};
    Vec3 direction{0, -1, 0};           // the way the light travels (sun and spot)
    Vec3 radiance{1, 1, 1};             // color times intensity
    float range = 10.0f;
    float cosInner = 0.9f, cosOuter = 0.8f;
};

/// Where the probes go: a box (world space, axis-aligned) and the space between them.
struct ProbeVolumeDesc {
    AABB box;
    float spacing = 2.0f;
    float bounce = 1.0f;                // how strong the bounced light is: 1 as physics says
};

struct BakeSettings {
    int raysPerProbe = 256;
    Vec3 skyLight{0.3f, 0.4f, 0.55f};   // the sky's light falling on the surfaces the rays hit, roughly (it bounces too)
    /// How many threads to share the probes among; 0 for as many as there are cores.
    int threads = 0;
};

/// Bakes the probes of every volume. `progress` (if given) is told 0 to 1 as it goes, from
/// any of the threads; returning false from it stops the bake (the result is then empty).
BakedLighting bakeLighting(const BakeScene& scene, const std::vector<BakeLight>& lights, const std::vector<ProbeVolumeDesc>& volumes,
                           const BakeSettings& settings, const std::function<bool(float)>& progress = {});

/// The probes' place within a box: so many along each side that they are at most `spacing`
/// apart, at least two along each.
ProbeGrid gridFor(const AABB& box, float spacing);

}  // namespace molten
