#include "Rendering/Cascades.hpp"

namespace molten {

Cascades computeCascades(const Mat4& cameraWorld, float fovY, float aspect, float nearPlane,
                         Vec3 lightDirection, const CascadeSettings& settings) {
    Cascades cascades;
    cascades.count = std::clamp(settings.count, 1, kMaxCascades);
    float farPlane = std::max(settings.shadowDistance, nearPlane + 0.1f);

    // The light's orientation is the same for every cascade: a view looking along the light.
    Vec3 direction = safeNormalize(lightDirection, Vec3{0, -1, 0});
    Vec3 up = std::abs(direction.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    Mat4 lightRotation = lookAt(Vec3{0, 0, 0}, direction, up);
    Mat4 lightRotationInverse = simd::transpose(lightRotation);        // a pure rotation: inverse = transpose

    float tanHalfY = std::tan(fovY * 0.5f), tanHalfX = tanHalfY * aspect;
    float sliceNear = nearPlane;

    for (int i = 0; i < cascades.count; i++) {
        // Where this slice ends: a blend of even spacing and logarithmic spacing.
        float p = float(i + 1) / float(cascades.count);
        float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        float sliceFar = lerp(uniformSplit, logSplit, settings.splitLambda);

        // The eight corners of the slice of the view frustum, in world space.
        Vec3 corners[8];
        int c = 0;
        for (float distance : {sliceNear, sliceFar}) {
            for (float sy : {-1.0f, 1.0f}) {
                for (float sx : {-1.0f, 1.0f}) {
                    corners[c++] = transformPoint(cameraWorld, Vec3{sx * tanHalfX * distance, sy * tanHalfY * distance, -distance});
                }
            }
        }

        // Enclose the slice in a sphere. A sphere looks the same from every direction,
        // so the area the cascade covers does not change when the camera turns.
        Vec3 center{0, 0, 0};
        for (Vec3 corner : corners) center += corner;
        center /= 8.0f;
        float radius = 0.0f;
        for (Vec3 corner : corners) radius = std::max(radius, simd::distance(center, corner));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Snap the center to whole shadow-map texels, measured in the light's space.
        // Without this, shadow edges shimmer as the camera moves.
        float texel = 2.0f * radius / float(settings.resolution);
        Vec3 lightSpaceCenter = transformPoint(lightRotation, center);
        lightSpaceCenter.x = std::floor(lightSpaceCenter.x / texel) * texel;
        lightSpaceCenter.y = std::floor(lightSpaceCenter.y / texel) * texel;
        Vec3 snappedCenter = transformPoint(lightRotationInverse, lightSpaceCenter);

        // An orthographic box around the sphere, pulled back toward the light so that
        // objects outside the view can still cast shadows into it.
        Vec3 eye = snappedCenter - direction * (radius + settings.casterDistance);
        Mat4 view = lookAt(eye, snappedCenter, up);
        Mat4 projection = orthographic(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + settings.casterDistance);

        cascades.viewProjection[i] = projection * view;
        cascades.splitFar[i] = sliceFar;
        cascades.radius[i] = radius;
        sliceNear = sliceFar;

        // The slice as this map sees it, for Cascades::reaches: the outline of its corners.
        Vec3 seen[8];
        float farDepth = 0.0f;
        for (int k = 0; k < 8; k++) {
            Vec4 clip = cascades.viewProjection[i] * Vec4{corners[k].x, corners[k].y, corners[k].z, 1.0f};
            seen[k] = Vec3{clip.x, clip.y, clip.z};
            farDepth = std::max(farDepth, clip.z);
        }
        cascades.sliceFarDepth[i] = farDepth;
        // The 3 x 3 filter and the normal offset read a few texels beyond the slice itself.
        cascades.margin[i] = 12.0f / float(std::max(settings.resolution, 16));
        int& edges = cascades.sliceEdgeCount[i];
        edges = 0;
        for (int a = 0; a < 8; a++) {
            for (int b = 0; b < 8 && edges < Cascades::kMaxSliceEdges; b++) {
                if (a == b) continue;
                Vec2 along{seen[b].x - seen[a].x, seen[b].y - seen[a].y};
                float length = simd::length(along);
                if (length < 1e-5f) continue;
                Vec2 normal{along.y / length, -along.x / length};
                float offset = normal.x * seen[a].x + normal.y * seen[a].y;
                // An edge of the outline: every corner is on its inner side.
                bool isEdge = true;
                for (int k = 0; k < 8 && isEdge; k++) isEdge = normal.x * seen[k].x + normal.y * seen[k].y <= offset + 1e-4f;
                if (!isEdge) continue;
                bool isKnown = false;
                for (int e = 0; e < edges && !isKnown; e++) {
                    const Vec3& known = cascades.sliceEdges[i][e];
                    isKnown = std::abs(known.x - normal.x) < 1e-3f && std::abs(known.y - normal.y) < 1e-3f && std::abs(known.z - offset) < 1e-3f;
                }
                if (!isKnown) cascades.sliceEdges[i][edges++] = Vec3{normal.x, normal.y, offset};
            }
        }
    }
    return cascades;
}

bool Cascades::reaches(int cascade, const AABB& bounds) const {
    if (cascade < 0 || cascade >= count) return false;
    // The box as the shadow map sees it: a rectangle, and how near the light it starts.
    Vec2 low{1e30f, 1e30f}, high{-1e30f, -1e30f};
    float nearest = 1e30f;
    for (int k = 0; k < 8; k++) {
        Vec3 corner{k & 1 ? bounds.max.x : bounds.min.x, k & 2 ? bounds.max.y : bounds.min.y, k & 4 ? bounds.max.z : bounds.min.z};
        Vec4 clip = viewProjection[cascade] * Vec4{corner.x, corner.y, corner.z, 1.0f};
        low.x = std::min(low.x, clip.x), low.y = std::min(low.y, clip.y);
        high.x = std::max(high.x, clip.x), high.y = std::max(high.y, clip.y);
        nearest = std::min(nearest, clip.z);
    }
    const float m = margin[cascade];
    // Outside the map, or farther from the light than everything the slice holds.
    if (high.x < -1.0f - m || low.x > 1.0f + m || high.y < -1.0f - m || low.y > 1.0f + m) return false;
    if (nearest > sliceFarDepth[cascade] + 1e-3f) return false;
    // Beside the slice: one of the outline's edges has the whole rectangle on its outer side.
    for (int e = 0; e < sliceEdgeCount[cascade]; e++) {
        const Vec3& edge = sliceEdges[cascade][e];
        float least = edge.x * (edge.x >= 0 ? low.x : high.x) + edge.y * (edge.y >= 0 ? low.y : high.y);
        if (least > edge.z + m) return false;
    }
    return true;
}

}  // namespace molten
