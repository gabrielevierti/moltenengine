// DebugDraw.hpp
// Lines in 3D that last for one frame: bounding boxes, rays, axes, collider outlines.
// Call these from anywhere during a frame; the renderer draws them after the scene.
#pragma once

#include "Math/Math.hpp"
#include "Rendering/GPUTypes.hpp"

#include <vector>

namespace molten {

class DebugDraw {
public:
    void line(Vec3 from, Vec3 to, Vec4 color) {
        vertices_.push_back(gpu::LineVertex{from, color});
        vertices_.push_back(gpu::LineVertex{to, color});
    }

    /// A box with any orientation, as its twelve edges.
    void box(Vec3 center, Vec3 halfExtents, Quat rotation, Vec4 color) {
        Vec3 corners[8];
        for (int i = 0; i < 8; i++) {
            Vec3 sign{(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f};
            corners[i] = center + rotate(rotation, sign * halfExtents);
        }
        // Two corners share an edge when their indices differ in exactly one bit.
        for (int i = 0; i < 8; i++) {
            for (int bit : {1, 2, 4}) {
                if (!(i & bit)) line(corners[i], corners[i | bit], color);
            }
        }
    }

    void box(const AABB& bounds, Vec4 color) { box(bounds.center(), bounds.halfExtents(), Quat{}, color); }

    /// Three circles, one around each axis.
    void sphere(Vec3 center, float radius, Vec4 color, int segments = 24) {
        for (int axis = 0; axis < 3; axis++) {
            Vec3 previous{0, 0, 0};
            for (int i = 0; i <= segments; i++) {
                float angle = float(i) / float(segments) * 2.0f * kPi;
                float c = std::cos(angle) * radius, s = std::sin(angle) * radius;
                Vec3 point = center + (axis == 0 ? Vec3{0, c, s} : axis == 1 ? Vec3{c, 0, s} : Vec3{c, s, 0});
                if (i > 0) line(previous, point, color);
                previous = point;
            }
        }
    }

    /// The X (red), Y (green) and Z (blue) axes of a transform.
    void axes(const Mat4& transform, float size = 1.0f) {
        Vec3 origin = xyz(transform.columns[3]);
        line(origin, origin + safeNormalize(xyz(transform.columns[0])) * size, Vec4{1, 0.2f, 0.2f, 1});
        line(origin, origin + safeNormalize(xyz(transform.columns[1])) * size, Vec4{0.2f, 1, 0.2f, 1});
        line(origin, origin + safeNormalize(xyz(transform.columns[2])) * size, Vec4{0.3f, 0.5f, 1, 1});
    }

    const std::vector<gpu::LineVertex>& vertices() const { return vertices_; }
    void clear() { vertices_.clear(); }

private:
    std::vector<gpu::LineVertex> vertices_;
};

}  // namespace molten
