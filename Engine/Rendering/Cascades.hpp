// Cascades.hpp
// Cascaded shadow maps: the math that fits several shadow maps to the camera's view.
//
// One shadow map stretched over everything the camera sees would be far too coarse up
// close. So the view distance is cut into slices, the "cascades", and each slice gets a
// shadow map of its own: small and sharp near the camera, large and soft far away.
#pragma once

#include "Math/Math.hpp"

namespace molten {

constexpr int kMaxCascades = 4;

struct Cascades {
    int count = 0;
    Mat4 viewProjection[kMaxCascades];      // world space to each cascade's shadow map
    float splitFar[kMaxCascades] = {};      // view-space distance at which each cascade ends
    float radius[kMaxCascades] = {};        // world-space radius each cascade covers

    /// True if a thing with these bounds can cast a shadow onto what the camera sees in this
    /// cascade's slice of the view. A cascade's shadow map covers a sphere around its slice,
    /// which is much more than the slice: a box at the camera's feet is inside the maps of all
    /// four cascades, but its shadow only falls in the first. Drawing it into the other three
    /// would be work nobody sees. Never false for a thing whose shadow does fall in the slice.
    bool reaches(int cascade, const AABB& bounds) const;

    // The slice of the view each cascade is for, as its shadow map sees it: the outline of its
    // eight corners (half-planes n.x * x + n.y * y <= n.z that hold inside it, in the map's
    // -1 to 1), and how deep into the map its farthest corner is.
    static constexpr int kMaxSliceEdges = 12;
    Vec3 sliceEdges[kMaxCascades][kMaxSliceEdges] = {};
    int sliceEdgeCount[kMaxCascades] = {};
    float sliceFarDepth[kMaxCascades] = {};
    float margin[kMaxCascades] = {};        // what the filter and the normal offset read beyond the slice, in the map's -1 to 1
};

struct CascadeSettings {
    int count = 4;
    float shadowDistance = 60.0f;           // no sun shadows beyond this distance from the camera
    float splitLambda = 0.75f;              // 0 spaces the slices evenly, 1 logarithmically
    int resolution = 2048;                  // shadow map edge, in texels
    float casterDistance = 80.0f;           // how far behind a cascade an object can be and still cast into it
};

/// `cameraWorld` is the camera's world matrix (the inverse of its view matrix) and
/// `lightDirection` the direction the sunlight travels.
Cascades computeCascades(const Mat4& cameraWorld, float fovY, float aspect, float nearPlane,
                         Vec3 lightDirection, const CascadeSettings& settings);

}  // namespace molten
