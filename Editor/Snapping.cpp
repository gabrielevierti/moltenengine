#include "Snapping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace molten {

namespace {

Vec3 apply(const Mat4& m, Vec3 p) {
    Vec4 h = m * Vec4{p.x, p.y, p.z, 1};
    return Vec3{h.x, h.y, h.z};
}

/// Möller–Trumbore: the distance along the ray to the triangle, if it meets it.
std::optional<float> hitTriangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c) {
    Vec3 ab = b - a, ac = c - a;
    Vec3 p = simd::cross(ray.direction, ac);
    float det = simd::dot(ab, p);
    if (std::abs(det) < 1e-10f) return std::nullopt;
    float inverse = 1.0f / det;
    Vec3 t = ray.origin - a;
    float u = simd::dot(t, p) * inverse;
    if (u < 0 || u > 1) return std::nullopt;
    Vec3 q = simd::cross(t, ab);
    float v = simd::dot(ray.direction, q) * inverse;
    if (v < 0 || u + v > 1) return std::nullopt;
    float distance = simd::dot(ac, q) * inverse;
    if (distance <= 1e-5f) return std::nullopt;
    return distance;
}

}  // namespace

std::optional<SurfaceHit> hitSurface(const std::vector<SnapMesh>& meshes, const Ray& ray) {
    std::optional<SurfaceHit> best;
    Vec3 direction = safeNormalize(ray.direction, Vec3{0, 0, -1});
    Ray along{ray.origin, direction};
    for (const SnapMesh& mesh : meshes) {
        if (!mesh.positions || !mesh.indices) continue;
        const std::vector<Vec3>& positions = *mesh.positions;
        const std::vector<uint32_t>& indices = *mesh.indices;
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() || indices[i + 2] >= positions.size()) continue;
            Vec3 a = apply(mesh.world, positions[indices[i]]), b = apply(mesh.world, positions[indices[i + 1]]),
                 c = apply(mesh.world, positions[indices[i + 2]]);
            std::optional<float> distance = hitTriangle(along, a, b, c);
            if (!distance || (best && *distance >= best->distance)) continue;
            SurfaceHit hit;
            hit.entity = mesh.entity;
            hit.distance = *distance;
            hit.point = along.origin + direction * *distance;
            hit.normal = safeNormalize(simd::cross(b - a, c - a), Vec3{0, 1, 0});
            if (simd::dot(hit.normal, direction) > 0) hit.normal = -hit.normal;     // the side that faces the ray
            best = hit;
        }
    }
    return best;
}

std::optional<Vec3> nearestVertex(const std::vector<SnapMesh>& meshes, const Mat4& viewProjection, Vec2 screen, Vec2 mouse, float maxPixels) {
    std::optional<Vec3> best;
    float bestPixels = std::numeric_limits<float>::max();
    float bestDepth = std::numeric_limits<float>::max();
    for (const SnapMesh& mesh : meshes) {
        if (!mesh.positions) continue;
        Mat4 toClip = viewProjection * mesh.world;
        for (Vec3 local : *mesh.positions) {
            Vec4 clip = toClip * Vec4{local.x, local.y, local.z, 1};
            if (clip.w <= 1e-4f) continue;                      // behind the camera
            Vec2 onScreen{(clip.x / clip.w * 0.5f + 0.5f) * screen.x, (0.5f - clip.y / clip.w * 0.5f) * screen.y};
            float pixels = simd::length(onScreen - mouse);
            // The nearest on screen; of corners on top of one another, the one nearest the eye.
            if (pixels > maxPixels) continue;
            if (!best || pixels < bestPixels - 0.5f || (pixels <= bestPixels + 0.5f && clip.w < bestDepth)) {
                bestPixels = pixels;
                bestDepth = clip.w;
                best = apply(mesh.world, local);
            }
        }
    }
    return best;
}

Quat standOn(Vec3 normal, Quat current) {
    Vec3 up = safeNormalize(normal, Vec3{0, 1, 0});
    Vec3 forward = rotate(current, Vec3{0, 0, -1});
    // The heading laid onto the surface; if it points along the normal, another one.
    Vec3 along = forward - up * simd::dot(forward, up);
    if (simd::length_squared(along) < 1e-6f) {
        Vec3 right = rotate(current, Vec3{1, 0, 0});
        along = simd::cross(up, right);
    }
    return lookRotation(safeNormalize(along, Vec3{0, 0, -1}), up);
}

void alignPoints(std::vector<Vec3>& points, int axis, AlignTo to) {
    if (points.empty() || axis < 0 || axis > 2) return;
    float least = points[0][axis], most = points[0][axis];
    for (const Vec3& point : points) least = std::min(least, point[axis]), most = std::max(most, point[axis]);
    float at = to == AlignTo::Least ? least : to == AlignTo::Most ? most : (least + most) * 0.5f;
    for (Vec3& point : points) point[axis] = at;
}

void distributePoints(std::vector<Vec3>& points, int axis) {
    if (points.size() < 3 || axis < 0 || axis > 2) return;
    std::vector<size_t> order(points.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return points[a][axis] < points[b][axis]; });
    float first = points[order.front()][axis], last = points[order.back()][axis];
    for (size_t k = 0; k < order.size(); k++) points[order[k]][axis] = first + (last - first) * float(k) / float(order.size() - 1);
}

}  // namespace molten
