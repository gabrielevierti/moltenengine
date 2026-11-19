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
    }
    return cascades;
}

}  // namespace molten
