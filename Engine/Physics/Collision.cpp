#include "Physics/Collision.hpp"

#include <algorithm>
#include <optional>
#include <vector>
#include <memory>
#include <cmath>

namespace molten {

namespace {

Vec3 boxAxis(const ShapeInstance& box, int i) {
    Vec3 unit{0, 0, 0};
    unit[i] = 1;
    return rotate(box.rotation, unit);
}

bool sphereSphere(Vec3 ca, float ra, Vec3 cb, float rb, ContactInfo& out) {
    Vec3 d = cb - ca;
    float distanceSquared = simd::length_squared(d), reach = ra + rb;
    if (distanceSquared >= reach * reach) return false;
    float distance = std::sqrt(distanceSquared);
    out.normal = distance > 1e-6f ? d / distance : Vec3{0, 1, 0};
    out.depth = reach - distance;
    out.point = ca + out.normal * ra;
    return true;
}

/// Normal points from the sphere to the box.
bool sphereBox(Vec3 center, float radius, const ShapeInstance& box, ContactInfo& out) {
    // Work in the box's own space, where it is axis-aligned and centered on the origin.
    Vec3 local = rotate(simd::conjugate(box.rotation), center - box.center);
    Vec3 closest = simd::clamp(local, -box.halfExtents, box.halfExtents);
    Vec3 difference = local - closest;
    float distanceSquared = simd::length_squared(difference);
    if (distanceSquared > radius * radius) return false;

    Vec3 outward;                          // from the box toward the sphere, in box space
    if (distanceSquared > 1e-10f) {
        float distance = std::sqrt(distanceSquared);
        outward = difference / distance;
        out.depth = radius - distance;
    } else {
        // The center is inside the box: leave through the nearest face.
        int axis = 0;
        float nearest = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; i++) {
            float toFace = box.halfExtents[i] - std::abs(local[i]);
            if (toFace < nearest) {
                nearest = toFace;
                axis = i;
            }
        }
        outward = Vec3{0, 0, 0};
        outward[axis] = local[axis] < 0 ? -1.0f : 1.0f;
        closest[axis] = outward[axis] * box.halfExtents[axis];
        out.depth = radius + nearest;
    }
    out.normal = -rotate(box.rotation, outward);
    out.point = box.center + rotate(box.rotation, closest);
    return true;
}

/// Separating axis test for two oriented boxes.
bool boxBox(const ShapeInstance& a, const ShapeInstance& b, ContactInfo& out) {
    Vec3 axesA[3] = {boxAxis(a, 0), boxAxis(a, 1), boxAxis(a, 2)};
    Vec3 axesB[3] = {boxAxis(b, 0), boxAxis(b, 1), boxAxis(b, 2)};
    Vec3 offset = b.center - a.center;
    float best = std::numeric_limits<float>::max();
    Vec3 bestAxis{0, 1, 0};

    // Returns false as soon as an axis separates the boxes. `preference` makes an edge
    // axis win only when it is clearly better than a face axis, which avoids jitter.
    auto test = [&](Vec3 axis, float preference) {
        float lengthSquared = simd::length_squared(axis);
        if (lengthSquared < 1e-8f) return true;                 // parallel edges: no new information
        axis /= std::sqrt(lengthSquared);
        float reachA = 0, reachB = 0;
        for (int i = 0; i < 3; i++) {
            reachA += a.halfExtents[i] * std::abs(simd::dot(axesA[i], axis));
            reachB += b.halfExtents[i] * std::abs(simd::dot(axesB[i], axis));
        }
        float distance = simd::dot(offset, axis);
        float overlap = reachA + reachB - std::abs(distance);
        if (overlap < 0) return false;
        if (overlap + preference < best) {
            best = overlap;
            bestAxis = distance < 0 ? -axis : axis;
        }
        return true;
    };

    for (int i = 0; i < 3; i++) if (!test(axesA[i], 0)) return false;
    for (int i = 0; i < 3; i++) if (!test(axesB[i], 0)) return false;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (!test(simd::cross(axesA[i], axesB[j]), 0.01f)) return false;
        }
    }

    out.normal = bestAxis;
    out.depth = best;
    out.pointCount = 0;
    // Where they touch: the middle of the corners of each that reach farthest into the
    // other and are inside it. A box lying flat on another touches with four corners, and
    // the middle of them is under its weight: it rests. One corner alone would tip it.
    auto corners = [](const ShapeInstance& box, const Vec3 axes[3], Vec3 out[8]) {
        for (int c = 0; c < 8; c++) {
            out[c] = box.center + axes[0] * (c & 1 ? box.halfExtents.x : -box.halfExtents.x) + axes[1] * (c & 2 ? box.halfExtents.y : -box.halfExtents.y) +
                     axes[2] * (c & 4 ? box.halfExtents.z : -box.halfExtents.z);
        }
    };
    auto isInside = [](const ShapeInstance& box, const Vec3 axes[3], Vec3 point) {
        Vec3 from = point - box.center;
        const float slack = 0.02f;
        return std::abs(simd::dot(from, axes[0])) <= box.halfExtents.x + slack && std::abs(simd::dot(from, axes[1])) <= box.halfExtents.y + slack &&
               std::abs(simd::dot(from, axes[2])) <= box.halfExtents.z + slack;
    };
    Vec3 cornersA[8], cornersB[8];
    corners(a, axesA, cornersA);
    corners(b, axesB, cornersB);
    float deepestB = std::numeric_limits<float>::max(), deepestA = -std::numeric_limits<float>::max();
    for (int c = 0; c < 8; c++) {
        deepestB = std::min(deepestB, simd::dot(cornersB[c], bestAxis));
        deepestA = std::max(deepestA, simd::dot(cornersA[c], bestAxis));
    }
    Vec3 sum{0, 0, 0}, fallback{0, 0, 0};
    int count = 0, fallbackCount = 0;
    for (int c = 0; c < 8; c++) {
        if (simd::dot(cornersB[c], bestAxis) < deepestB + 0.02f) {
            fallback += cornersB[c];
            fallbackCount++;
            if (isInside(a, axesA, cornersB[c])) {
                sum += cornersB[c];
                count++;
                if (out.pointCount < 8) {
                    out.depths[out.pointCount] = std::max(best - (simd::dot(cornersB[c], bestAxis) - deepestB), 0.0f);
                    out.points[out.pointCount++] = cornersB[c];
                }
            }
        }
        if (simd::dot(cornersA[c], bestAxis) > deepestA - 0.02f && isInside(b, axesB, cornersA[c])) {
            sum += cornersA[c];
            count++;
            if (out.pointCount < 8) {
                out.depths[out.pointCount] = std::max(best - (deepestA - simd::dot(cornersA[c], bestAxis)), 0.0f);
                out.points[out.pointCount++] = cornersA[c];
            }
        }
    }
    out.point = count > 0 ? sum / float(count) : fallback / float(std::max(fallbackCount, 1));
    return true;
}

/// Normal points from the capsule to the box.
bool capsuleBox(const ShapeInstance& capsule, const ShapeInstance& box, ContactInfo& out) {
    Vec3 a = capsule.capsuleA(), b = capsule.capsuleB();

    // Find the point of the capsule's inner segment that is closest to the box.
    // In the box's own space the squared distance from a point to the box is the sum,
    // over the three axes, of how far the point sticks out of that axis' slab, squared.
    // Along the segment this is a chain of parabolas that changes shape only where the
    // segment crosses a slab boundary, so we cut the segment at those crossings and
    // minimize each piece exactly.
    Quat toLocal = simd::conjugate(box.rotation);
    Vec3 origin = rotate(toLocal, a - box.center), direction = rotate(toLocal, b - a);
    Vec3 h = box.halfExtents;
    float cuts[8] = {0.0f, 1.0f};
    int cutCount = 2;
    for (int axis = 0; axis < 3; axis++) {
        if (std::abs(direction[axis]) < 1e-9f) continue;
        for (float side : {-1.0f, 1.0f}) {
            float t = (side * h[axis] - origin[axis]) / direction[axis];
            if (t > 0.0f && t < 1.0f) cuts[cutCount++] = t;
        }
    }
    std::sort(cuts, cuts + cutCount);

    float bestT = 0.0f, bestDistanceSquared = std::numeric_limits<float>::max();
    for (int i = 0; i + 1 < cutCount; i++) {
        float t0 = cuts[i], t1 = cuts[i + 1];
        Vec3 middle = origin + direction * ((t0 + t1) * 0.5f);
        float numerator = 0, denominator = 0;
        for (int axis = 0; axis < 3; axis++) {
            if (std::abs(middle[axis]) <= h[axis]) continue;       // inside this slab on this piece
            float face = middle[axis] > 0 ? h[axis] : -h[axis];
            numerator += (origin[axis] - face) * direction[axis];
            denominator += direction[axis] * direction[axis];
        }
        float t = denominator > 1e-12f ? std::clamp(-numerator / denominator, t0, t1) : t0;
        Vec3 point = origin + direction * t;
        Vec3 outside = simd::max(simd::abs(point) - h, Vec3{0, 0, 0});
        float distanceSquared = simd::length_squared(outside);
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestT = t;
        }
    }
    Vec3 localOnSegment = origin + direction * bestT;
    Vec3 localOnBox = simd::clamp(localOnSegment, -h, h);
    Vec3 onBox = box.center + rotate(box.rotation, localOnBox);

    if (bestDistanceSquared > 1e-10f) {
        // The segment is outside the box: this is a sphere around its closest point against the box.
        float distance = std::sqrt(bestDistanceSquared);
        if (distance >= capsule.radius) return false;
        out.normal = rotate(box.rotation, (localOnBox - localOnSegment) / distance);
        out.depth = capsule.radius - distance;
        out.point = onBox;
        return true;
    }

    // The segment itself passes through the box. Find the shortest way out with the
    // separating axis test (the segment is a box with no thickness), then add the radius.
    Vec3 segmentDirection = safeNormalize(b - a);
    Vec3 axes[6];
    for (int i = 0; i < 3; i++) {
        axes[i] = boxAxis(box, i);
        axes[i + 3] = simd::cross(segmentDirection, axes[i]);
    }
    Vec3 offset = box.center - capsule.center;
    float best = std::numeric_limits<float>::max();
    Vec3 bestAxis{0, 1, 0};
    for (Vec3 axis : axes) {
        float lengthSquared = simd::length_squared(axis);
        if (lengthSquared < 1e-8f) continue;
        axis /= std::sqrt(lengthSquared);
        float reachSegment = capsule.halfHeight * std::abs(simd::dot(segmentDirection, axis));
        float reachBox = 0;
        for (int i = 0; i < 3; i++) reachBox += box.halfExtents[i] * std::abs(simd::dot(axes[i], axis));
        float distance = simd::dot(offset, axis);
        float overlap = reachSegment + reachBox - std::abs(distance);
        if (overlap < best) {
            best = overlap;
            bestAxis = distance < 0 ? -axis : axis;
        }
    }
    out.normal = bestAxis;
    out.depth = std::max(best, 0.0f) + capsule.radius;
    out.point = onBox;
    return true;
}

void flip(ContactInfo& contact) { contact.normal = -contact.normal; }

}  // namespace

AABB ShapeInstance::bounds() const {
    if (shape == ColliderShape::Convex) {
        AABB box;
        if (!convex) return AABB::fromCenter(center, Vec3{0, 0, 0});
        for (int corner = 0; corner < 8; corner++) {
            Vec3 local{corner & 1 ? convex->bounds.max.x : convex->bounds.min.x, corner & 2 ? convex->bounds.max.y : convex->bounds.min.y,
                       corner & 4 ? convex->bounds.max.z : convex->bounds.min.z};
            box.expand(center + rotate(rotation, local));
        }
        return box;
    }
    if (shape == ColliderShape::Mesh) {
        AABB box;
        if (!mesh) return AABB::fromCenter(center, Vec3{0, 0, 0});
        for (int corner = 0; corner < 8; corner++) {
            Vec3 local{corner & 1 ? mesh->bounds.max.x : mesh->bounds.min.x, corner & 2 ? mesh->bounds.max.y : mesh->bounds.min.y, corner & 4 ? mesh->bounds.max.z : mesh->bounds.min.z};
            box.expand(center + rotate(rotation, local));
        }
        return box;
    }
    switch (shape) {
    case ColliderShape::Mesh:
    case ColliderShape::Convex:
        break;
    case ColliderShape::Sphere:
        return AABB::fromCenter(center, Vec3{radius, radius, radius});
    case ColliderShape::Capsule: {
        AABB box;
        box.expand(capsuleA());
        box.expand(capsuleB());
        box.min -= Vec3{radius, radius, radius};
        box.max += Vec3{radius, radius, radius};
        return box;
    }
    case ColliderShape::Box: {
        Vec3 extent{0, 0, 0};
        for (int i = 0; i < 3; i++) extent += simd::abs(boxAxis(*this, i)) * halfExtents[i];
        return AABB::fromCenter(center, extent);
    }
    }
    return AABB();
}

Vec3 closestPointOnSegment(Vec3 a, Vec3 b, Vec3 point) {
    Vec3 ab = b - a;
    float lengthSquared = simd::length_squared(ab);
    if (lengthSquared < 1e-12f) return a;
    return a + ab * saturate(simd::dot(point - a, ab) / lengthSquared);
}

// From "Real-Time Collision Detection" by Christer Ericson, section 5.1.9.
void closestPointsBetweenSegments(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, Vec3& c1, Vec3& c2) {
    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = simd::dot(d1, d1), e = simd::dot(d2, d2), f = simd::dot(d2, r);
    float s = 0, t = 0;
    const float epsilon = 1e-10f;
    if (a <= epsilon && e <= epsilon) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= epsilon) {
        t = saturate(f / e);
    } else {
        float c = simd::dot(d1, r);
        if (e <= epsilon) {
            s = saturate(-c / a);
        } else {
            float b = simd::dot(d1, d2);
            float denominator = a * e - b * b;
            s = denominator > epsilon ? saturate((b * f - c * e) / denominator) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0) {
                t = 0;
                s = saturate(-c / a);
            } else if (t > 1) {
                t = 1;
                s = saturate((b - c) / a);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

// ---------------------------------------------------------------- meshes

std::shared_ptr<const MeshShape> MeshShape::build(const std::vector<Vec3>& positions, const std::vector<uint32_t>& triangleIndices, Vec3 scale) {
    auto shape = std::make_shared<MeshShape>();
    shape->points.reserve(positions.size());
    for (Vec3 p : positions) {
        shape->points.push_back(p * scale);
        shape->bounds.expand(p * scale);
    }
    for (size_t i = 0; i + 2 < triangleIndices.size(); i += 3) {
        if (triangleIndices[i] >= positions.size() || triangleIndices[i + 1] >= positions.size() || triangleIndices[i + 2] >= positions.size()) continue;
        shape->indices.insert(shape->indices.end(), {triangleIndices[i], triangleIndices[i + 1], triangleIndices[i + 2]});
    }
    if (shape->indices.empty()) return nullptr;
    // A grid with a handful of triangles to a square.
    size_t triangles = shape->indices.size() / 3;
    Vec3 size = shape->bounds.max - shape->bounds.min;
    int across = std::clamp(int(std::sqrt(float(triangles) / 4.0f)), 1, 160);
    shape->cellSize = std::max(std::max(size.x, size.z) / float(across), 1e-3f);
    shape->cellsX = std::clamp(int(size.x / shape->cellSize) + 1, 1, 160);
    shape->cellsZ = std::clamp(int(size.z / shape->cellSize) + 1, 1, 160);
    shape->cells.resize(size_t(shape->cellsX) * size_t(shape->cellsZ));
    for (size_t t = 0; t < triangles; t++) {
        AABB box;
        for (int k = 0; k < 3; k++) box.expand(shape->points[shape->indices[t * 3 + size_t(k)]]);
        int x0 = std::clamp(int((box.min.x - shape->bounds.min.x) / shape->cellSize), 0, shape->cellsX - 1);
        int x1 = std::clamp(int((box.max.x - shape->bounds.min.x) / shape->cellSize), 0, shape->cellsX - 1);
        int z0 = std::clamp(int((box.min.z - shape->bounds.min.z) / shape->cellSize), 0, shape->cellsZ - 1);
        int z1 = std::clamp(int((box.max.z - shape->bounds.min.z) / shape->cellSize), 0, shape->cellsZ - 1);
        for (int z = z0; z <= z1; z++) {
            for (int x = x0; x <= x1; x++) shape->cells[size_t(z * shape->cellsX + x)].push_back(uint32_t(t));
        }
    }
    return shape;
}

namespace {

/// The point of a triangle nearest to a point (from Ericson's Real-Time Collision Detection).
Vec3 closestOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = simd::dot(ab, ap), d2 = simd::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    Vec3 bp = p - b;
    float d3 = simd::dot(ab, bp), d4 = simd::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    float d5 = simd::dot(ab, cp), d6 = simd::dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denominator = 1.0f / (va + vb + vc);
    return a + ab * (vb * denominator) + ac * (vc * denominator);
}

/// A ball against a mesh: how deep it is in, which way out (in the world, away from the mesh), and where.
bool ballInMesh(const ShapeInstance& mesh, Vec3 center, float radius, float& depth, Vec3& outward, Vec3& point) {
    if (!mesh.mesh) return false;
    const MeshShape& shape = *mesh.mesh;
    Vec3 local = rotate(simd::conjugate(mesh.rotation), center - mesh.center);
    const float under = 0.6f;                                   // how far below a surface something is still pushed back up through it
    Vec3 reach{radius, radius + under, radius};
    bool found = false;
    depth = 0;
    Vec3 bestNormal{0, 1, 0}, bestPoint = local;
    shape.forEachNear(AABB(local - reach, local + reach), [&](uint32_t triangle) {
        Vec3 a = shape.points[shape.indices[triangle * 3]], b = shape.points[shape.indices[triangle * 3 + 1]], c = shape.points[shape.indices[triangle * 3 + 2]];
        Vec3 facing = simd::cross(b - a, c - a);
        float area = simd::length(facing);
        if (area < 1e-12f) return;
        facing /= area;
        Vec3 nearest = closestOnTriangle(local, a, b, c);
        float side = simd::dot(local - a, facing);
        float thisDepth;
        Vec3 thisNormal;
        if (side >= 0) {
            // In front of it: the usual ball against the nearest point.
            Vec3 away = local - nearest;
            float distance = simd::length(away);
            if (distance >= radius) return;
            thisDepth = radius - distance;
            thisNormal = distance > 1e-6f ? away / distance : facing;
        } else {
            // Behind it: sunk through. Only straight under the triangle, and not too far.
            if (side < -under || simd::length_squared(local - facing * side - nearest) > 1e-6f) return;
            thisDepth = radius - side;
            thisNormal = facing;
        }
        if (thisDepth > depth) {
            depth = thisDepth;
            bestNormal = thisNormal;
            bestPoint = nearest;
            found = true;
        }
    });
    if (!found) return false;
    outward = rotate(mesh.rotation, bestNormal);
    point = mesh.center + rotate(mesh.rotation, bestPoint);
    return true;
}

/// Any shape (first) against a mesh (second).
bool shapeMesh(const ShapeInstance& a, const ShapeInstance& mesh, ContactInfo& out) {
    out.pointCount = 0;
    float depth = 0;
    Vec3 outward, point;
    if (a.shape == ColliderShape::Sphere) {
        if (!ballInMesh(mesh, a.center, a.radius, depth, outward, point)) return false;
        out.normal = -outward;
        out.depth = depth;
        out.point = point;
        return true;
    }
    // A capsule as the balls along its middle; a box as its corners: each that is in the mesh is a point of contact.
    Vec3 centers[9];
    float radii[9];
    int count = 0;
    if (a.shape == ColliderShape::Capsule) {
        Vec3 from = a.capsuleA(), to = a.capsuleB();
        for (int i = 0; i < 3; i++) {
            centers[count] = from + (to - from) * (float(i) * 0.5f);
            radii[count++] = a.radius;
        }
    } else if (a.shape == ColliderShape::Box) {
        for (int corner = 0; corner < 8; corner++) {
            Vec3 local{corner & 1 ? a.halfExtents.x : -a.halfExtents.x, corner & 2 ? a.halfExtents.y : -a.halfExtents.y, corner & 4 ? a.halfExtents.z : -a.halfExtents.z};
            centers[count] = a.center + rotate(a.rotation, local);
            radii[count++] = 0.0f;
        }
    } else {
        return false;
    }
    bool found = false;
    out.depth = 0;
    for (int i = 0; i < count; i++) {
        if (!ballInMesh(mesh, centers[i], radii[i], depth, outward, point)) continue;
        if (out.pointCount < 8) {
            out.points[out.pointCount] = point;
            out.depths[out.pointCount++] = depth;
        }
        if (depth > out.depth) {
            out.depth = depth;
            out.normal = -outward;
            out.point = point;
        }
        found = true;
    }
    return found;
}

std::optional<ShapeRayHit> raycastMesh(const Ray& ray, const ShapeInstance& mesh) {
    if (!mesh.mesh) return std::nullopt;
    const MeshShape& shape = *mesh.mesh;
    Quat inverse = simd::conjugate(mesh.rotation);
    Vec3 origin = rotate(inverse, ray.origin - mesh.center), direction = rotate(inverse, ray.direction);
    // The part of the ray that is inside the mesh's box.
    float enter = 0.0f, leave = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; axis++) {
        float low = shape.bounds.min[axis] - 1e-3f, high = shape.bounds.max[axis] + 1e-3f;
        if (std::abs(direction[axis]) < 1e-9f) {
            if (origin[axis] < low || origin[axis] > high) return std::nullopt;
            continue;
        }
        float t0 = (low - origin[axis]) / direction[axis], t1 = (high - origin[axis]) / direction[axis];
        if (t0 > t1) std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave) return std::nullopt;
    }
    float best = std::numeric_limits<float>::max();
    Vec3 bestNormal{0, 1, 0};
    auto test = [&](uint32_t triangle) {
        Vec3 a = shape.points[shape.indices[triangle * 3]], edge1 = shape.points[shape.indices[triangle * 3 + 1]] - a, edge2 = shape.points[shape.indices[triangle * 3 + 2]] - a;
        Vec3 p = simd::cross(direction, edge2);
        float det = simd::dot(edge1, p);
        if (std::abs(det) < 1e-12f) return;
        float inverseDet = 1.0f / det;
        Vec3 fromA = origin - a;
        float u = simd::dot(fromA, p) * inverseDet;
        if (u < 0 || u > 1) return;
        Vec3 q = simd::cross(fromA, edge1);
        float v = simd::dot(direction, q) * inverseDet;
        if (v < 0 || u + v > 1) return;
        float t = simd::dot(edge2, q) * inverseDet;
        if (t < 0 || t >= best) return;
        best = t;
        Vec3 facing = simd::normalize(simd::cross(edge1, edge2));
        bestNormal = simd::dot(facing, direction) > 0 ? -facing : facing;
    };
    // Walked a square of the grid at a time, stopping at the first hit that is nearer than where the walk has got to.
    float stride = shape.cellSize * 0.5f;
    int lastCell = -1;
    for (float t = enter; t <= leave + stride; t += stride) {
        Vec3 at = origin + direction * std::min(t, leave);
        int x = std::clamp(int((at.x - shape.bounds.min.x) / shape.cellSize), 0, shape.cellsX - 1);
        int z = std::clamp(int((at.z - shape.bounds.min.z) / shape.cellSize), 0, shape.cellsZ - 1);
        int cell = z * shape.cellsX + x;
        if (cell != lastCell) {
            // And the squares around it: a ray that cuts a corner misses none.
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int cx = x + dx, cz = z + dz;
                    if (cx < 0 || cz < 0 || cx >= shape.cellsX || cz >= shape.cellsZ) continue;
                    for (uint32_t triangle : shape.cells[size_t(cz * shape.cellsX + cx)]) test(triangle);
                }
            }
            lastCell = cell;
        }
        if (best <= t) break;
    }
    if (best == std::numeric_limits<float>::max()) return std::nullopt;
    return ShapeRayHit{best, rotate(mesh.rotation, bestNormal)};
}

}  // namespace

// ---------------------------------------------------------------- convex shapes

std::shared_ptr<const ConvexShape> ConvexShape::build(const std::vector<Vec3>& positions, Vec3 scale) {
    auto shape = std::make_shared<ConvexShape>();
    if (positions.empty()) return shape;
    std::vector<Vec3> scaled;
    scaled.reserve(positions.size());
    for (Vec3 p : positions) scaled.push_back(p * scale);
    // The corners: the farthest point in each of many directions spread over a sphere. The
    // shape they wrap is the model's convex hull, a little rounded off where it has many corners.
    const int directions = 120;
    const float golden = 2.39996323f;
    std::vector<Vec3> corners;
    for (int i = 0; i < directions; i++) {
        float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(directions);
        float ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
        Vec3 direction{std::cos(golden * float(i)) * ring, y, std::sin(golden * float(i)) * ring};
        size_t best = 0;
        float farthest = -1e30f;
        for (size_t k = 0; k < scaled.size(); k++) {
            float along = simd::dot(scaled[k], direction);
            if (along > farthest) farthest = along, best = k;
        }
        bool known = false;
        for (Vec3 corner : corners) known = known || simd::length_squared(corner - scaled[best]) < 1e-10f;
        if (!known) corners.push_back(scaled[best]);
    }
    shape->points = corners;
    for (Vec3 p : corners) shape->bounds.expand(p);
    // The faces: every plane through three corners with all the others behind it.
    float size = simd::length(shape->bounds.max - shape->bounds.min);
    float tolerance = std::max(size * 1e-4f, 1e-6f);
    size_t n = corners.size();
    for (size_t a = 0; a < n; a++) {
        for (size_t b = a + 1; b < n; b++) {
            for (size_t c = b + 1; c < n; c++) {
                Vec3 normal = simd::cross(corners[b] - corners[a], corners[c] - corners[a]);
                float length = simd::length(normal);
                if (length < tolerance * tolerance) continue;
                normal /= length;
                float offset = simd::dot(normal, corners[a]);
                bool front = false, back = false;
                for (size_t k = 0; k < n && !(front && back); k++) {
                    float d = simd::dot(normal, corners[k]) - offset;
                    if (d > tolerance) front = true;
                    else if (d < -tolerance) back = true;
                }
                if (front && back) continue;
                if (front) normal = -normal, offset = -offset;          // everything behind: it faces outward
                bool same = false;
                for (Vec4 plane : shape->planes) same = same || (simd::dot(xyz(plane), normal) > 0.9999f && std::abs(plane.w - offset) < tolerance * 4);
                if (!same) shape->planes.push_back(Vec4{normal.x, normal.y, normal.z, offset});
            }
        }
    }
    return shape;
}

Vec3 ConvexShape::support(Vec3 direction) const {
    Vec3 best{0, 0, 0};
    float farthest = -1e30f;
    for (Vec3 p : points) {
        float along = simd::dot(p, direction);
        if (along > farthest) farthest = along, best = p;
    }
    return best;
}

namespace {

/// The point of a convex shape farthest along a direction, in the world: what GJK and EPA ask of a shape.
Vec3 supportOf(const ShapeInstance& shape, Vec3 direction) {
    switch (shape.shape) {
    case ColliderShape::Sphere:
        return shape.center + safeNormalize(direction, Vec3{0, 1, 0}) * shape.radius;
    case ColliderShape::Capsule: {
        Vec3 a = shape.capsuleA(), b = shape.capsuleB();
        Vec3 end = simd::dot(a, direction) > simd::dot(b, direction) ? a : b;
        return end + safeNormalize(direction, Vec3{0, 1, 0}) * shape.radius;
    }
    case ColliderShape::Box: {
        Vec3 local = rotate(simd::conjugate(shape.rotation), direction);
        Vec3 corner{local.x >= 0 ? shape.halfExtents.x : -shape.halfExtents.x, local.y >= 0 ? shape.halfExtents.y : -shape.halfExtents.y,
                    local.z >= 0 ? shape.halfExtents.z : -shape.halfExtents.z};
        return shape.center + rotate(shape.rotation, corner);
    }
    case ColliderShape::Convex: {
        if (!shape.convex) return shape.center;
        return shape.center + rotate(shape.rotation, shape.convex->support(rotate(simd::conjugate(shape.rotation), direction)));
    }
    default:
        return shape.center;
    }
}

struct SupportPoint {
    Vec3 p, onA;                         // the difference of the two shapes' points, and A's own
};

SupportPoint supportPair(const ShapeInstance& a, const ShapeInstance& b, Vec3 direction) {
    Vec3 onA = supportOf(a, direction), onB = supportOf(b, -direction);
    return SupportPoint{onA - onB, onA};
}

/// GJK: whether two convex shapes overlap, by whether their difference holds the origin. On
/// overlap, `simplex` ends as a tetrahedron around it, for EPA to start from.
bool gjk(const ShapeInstance& a, const ShapeInstance& b, std::vector<SupportPoint>& simplex) {
    Vec3 direction = b.center - a.center;
    if (simd::length_squared(direction) < 1e-12f) direction = Vec3{1, 0, 0};
    simplex.clear();
    simplex.push_back(supportPair(a, b, direction));
    direction = -simplex[0].p;
    for (int iteration = 0; iteration < 48; iteration++) {
        if (simd::length_squared(direction) < 1e-14f) direction = Vec3{0, 1, 0};
        SupportPoint next = supportPair(a, b, direction);
        if (simd::dot(next.p, direction) < 0) return false;       // it did not get past the origin: apart
        simplex.insert(simplex.begin(), next);
        // The simplex nearest the origin, and the way from it toward the origin.
        Vec3 A = simplex[0].p, AO = -A;
        if (simplex.size() == 2) {
            Vec3 AB = simplex[1].p - A;
            if (simd::dot(AB, AO) > 0) direction = simd::cross(simd::cross(AB, AO), AB);
            else simplex = {simplex[0]}, direction = AO;
        } else if (simplex.size() == 3) {
            Vec3 B = simplex[1].p, C = simplex[2].p;
            Vec3 AB = B - A, AC = C - A, ABC = simd::cross(AB, AC);
            if (simd::dot(simd::cross(ABC, AC), AO) > 0) {
                if (simd::dot(AC, AO) > 0) simplex = {simplex[0], simplex[2]}, direction = simd::cross(simd::cross(AC, AO), AC);
                else if (simd::dot(AB, AO) > 0) simplex = {simplex[0], simplex[1]}, direction = simd::cross(simd::cross(AB, AO), AB);
                else simplex = {simplex[0]}, direction = AO;
            } else if (simd::dot(simd::cross(AB, ABC), AO) > 0) {
                if (simd::dot(AB, AO) > 0) simplex = {simplex[0], simplex[1]}, direction = simd::cross(simd::cross(AB, AO), AB);
                else simplex = {simplex[0]}, direction = AO;
            } else if (simd::dot(ABC, AO) > 0) {
                direction = ABC;
            } else {
                std::swap(simplex[1], simplex[2]);
                direction = -ABC;
            }
        } else {
            Vec3 B = simplex[1].p, C = simplex[2].p, D = simplex[3].p;
            Vec3 ABC = simd::cross(B - A, C - A), ACD = simd::cross(C - A, D - A), ADB = simd::cross(D - A, B - A);
            if (simd::dot(ABC, AO) > 0) simplex = {simplex[0], simplex[1], simplex[2]}, direction = ABC;
            else if (simd::dot(ACD, AO) > 0) simplex = {simplex[0], simplex[2], simplex[3]}, direction = ACD;
            else if (simd::dot(ADB, AO) > 0) simplex = {simplex[0], simplex[3], simplex[1]}, direction = ADB;
            else return true;                                       // the origin is inside: they overlap
        }
    }
    return false;
}

/// EPA: from GJK's tetrahedron, the shortest way out of the overlap: its direction and depth.
bool epa(const ShapeInstance& a, const ShapeInstance& b, std::vector<SupportPoint> points, ContactInfo& out) {
    if (points.size() < 4) return false;
    struct Face {
        int i, j, k;
        Vec3 normal;
        float distance;
    };
    std::vector<Face> faces;
    auto addFace = [&](int i, int j, int k) {
        Vec3 normal = simd::cross(points[size_t(j)].p - points[size_t(i)].p, points[size_t(k)].p - points[size_t(i)].p);
        float length = simd::length(normal);
        if (length < 1e-12f) return;
        normal /= length;
        float distance = simd::dot(normal, points[size_t(i)].p);
        if (distance < 0) {
            normal = -normal, distance = -distance;
            std::swap(j, k);
        }
        faces.push_back(Face{i, j, k, normal, distance});
    };
    addFace(0, 1, 2), addFace(0, 2, 3), addFace(0, 3, 1), addFace(1, 3, 2);
    for (int iteration = 0; iteration < 64 && !faces.empty(); iteration++) {
        size_t nearest = 0;
        for (size_t f = 1; f < faces.size(); f++) {
            if (faces[f].distance < faces[nearest].distance) nearest = f;
        }
        Face face = faces[nearest];
        SupportPoint next = supportPair(a, b, face.normal);
        float reach = simd::dot(next.p, face.normal);
        if (reach - face.distance < 1e-4f || iteration == 63) {
            out.normal = face.normal;
            out.depth = face.distance;
            // Where on A: the nearest face's corners, weighed by where the origin falls on it.
            Vec3 p0 = points[size_t(face.i)].p, p1 = points[size_t(face.j)].p, p2 = points[size_t(face.k)].p;
            Vec3 at = face.normal * face.distance;
            Vec3 v0 = p1 - p0, v1 = p2 - p0, v2 = at - p0;
            float d00 = simd::dot(v0, v0), d01 = simd::dot(v0, v1), d11 = simd::dot(v1, v1), d20 = simd::dot(v2, v0), d21 = simd::dot(v2, v1);
            float denominator = d00 * d11 - d01 * d01;
            float v = denominator != 0 ? (d11 * d20 - d01 * d21) / denominator : 0.33f, w = denominator != 0 ? (d00 * d21 - d01 * d20) / denominator : 0.33f;
            float u = 1 - v - w;
            out.point = points[size_t(face.i)].onA * u + points[size_t(face.j)].onA * v + points[size_t(face.k)].onA * w;
            out.points[0] = out.point;
            out.depths[0] = out.depth;
            out.pointCount = 1;
            return true;
        }
        // Grow the hull to the new point: every face it can see goes, and the hole is closed to it.
        int added = int(points.size());
        points.push_back(next);
        std::vector<std::pair<int, int>> edges;
        auto edge = [&](int from, int to) {
            for (size_t e = 0; e < edges.size(); e++) {
                if (edges[e].first == to && edges[e].second == from) {
                    edges.erase(edges.begin() + long(e));
                    return;
                }
            }
            edges.push_back({from, to});
        };
        for (size_t f = 0; f < faces.size();) {
            if (simd::dot(faces[f].normal, next.p - points[size_t(faces[f].i)].p) > 0) {
                edge(faces[f].i, faces[f].j), edge(faces[f].j, faces[f].k), edge(faces[f].k, faces[f].i);
                faces.erase(faces.begin() + long(f));
            } else {
                f++;
            }
        }
        for (auto [from, to] : edges) addFace(from, to, added);
    }
    return false;
}

bool convexGeneral(const ShapeInstance& a, const ShapeInstance& b, ContactInfo& out) {
    std::vector<SupportPoint> simplex;
    if (!gjk(a, b, simplex)) return false;
    if (simplex.size() < 4) return false;
    if (!epa(a, b, simplex, out)) return false;
    // The face of A - B nearest the origin lies the way B is from A: the normal goes from A to B.
    return out.depth > 0;
}

/// The corners of a convex shape, in the world.
int convexCorners(const ShapeInstance& shape, Vec3* corners, int most) {
    if (!shape.convex) return 0;
    int count = 0;
    for (Vec3 p : shape.convex->points) {
        if (count >= most) break;
        corners[count++] = shape.center + rotate(shape.rotation, p);
    }
    return count;
}

/// Keeps the deepest points of contact (eight at most), and the normal of the deepest.
void notePoint(ContactInfo& out, Vec3 point, float depth, Vec3 normal) {
    if (out.pointCount < 8) {
        out.points[out.pointCount] = point;
        out.depths[out.pointCount++] = depth;
    } else {
        int shallowest = 0;
        for (int k = 1; k < 8; k++) if (out.depths[k] < out.depths[shallowest]) shallowest = k;
        if (depth <= out.depths[shallowest]) return;
        out.points[shallowest] = point;
        out.depths[shallowest] = depth;
    }
    if (depth > out.depth) {
        out.depth = depth;
        out.normal = normal;
        out.point = point;
    }
}

/// A convex shape (A) against a box (B): the corners of each inside the other, as points of
/// contact, so that one resting on the other stands on several; where no corner is in (an
/// edge across a face), the general way.
bool convexBox(const ShapeInstance& a, const ShapeInstance& box, ContactInfo& out) {
    out = ContactInfo{};
    out.depth = 0;
    bool found = false;
    static thread_local std::vector<Vec3> corners(256);
    int count = convexCorners(a, corners.data(), 256);
    Quat toBox = simd::conjugate(box.rotation);
    for (int k = 0; k < count; k++) {
        Vec3 local = rotate(toBox, corners[size_t(k)] - box.center);
        Vec3 room = box.halfExtents - simd::abs(local);
        if (room.x < 0 || room.y < 0 || room.z < 0) continue;
        int axis = room.x < room.y ? (room.x < room.z ? 0 : 2) : (room.y < room.z ? 1 : 2);
        Vec3 outward{0, 0, 0};
        outward[axis] = local[axis] >= 0 ? 1.0f : -1.0f;
        notePoint(out, corners[size_t(k)], room[axis], -rotate(box.rotation, outward));
        found = true;
    }
    if (a.convex) {
        Quat toA = simd::conjugate(a.rotation);
        for (int corner = 0; corner < 8; corner++) {
            Vec3 local{corner & 1 ? box.halfExtents.x : -box.halfExtents.x, corner & 2 ? box.halfExtents.y : -box.halfExtents.y,
                       corner & 4 ? box.halfExtents.z : -box.halfExtents.z};
            Vec3 world = box.center + rotate(box.rotation, local);
            Vec3 inA = rotate(toA, world - a.center);
            float depth = 1e30f;
            Vec3 normal{0, 1, 0};
            for (Vec4 plane : a.convex->planes) {
                float room = plane.w - simd::dot(xyz(plane), inA);
                if (room < 0) {
                    depth = -1;
                    break;
                }
                if (room < depth) depth = room, normal = xyz(plane);
            }
            if (depth < 0 || a.convex->planes.empty()) continue;
            notePoint(out, world, depth, rotate(a.rotation, normal));
            found = true;
        }
    }
    if (found) return true;
    return convexGeneral(a, box, out);
}

/// A convex shape (A) against a mesh that stands still: its corners in the mesh, as a box's are.
bool convexMesh(const ShapeInstance& a, const ShapeInstance& mesh, ContactInfo& out) {
    out = ContactInfo{};
    bool found = false;
    static thread_local std::vector<Vec3> corners(256);
    int count = convexCorners(a, corners.data(), 256);
    float depth = 0;
    Vec3 outward, point;
    for (int k = 0; k < count; k++) {
        if (!ballInMesh(mesh, corners[size_t(k)], 0.0f, depth, outward, point)) continue;
        notePoint(out, point, depth, -outward);
        found = true;
    }
    return found;
}

std::optional<ShapeRayHit> raycastConvex(const Ray& ray, const ShapeInstance& shape) {
    if (!shape.convex || shape.convex->planes.empty()) return std::nullopt;
    Quat toShape = simd::conjugate(shape.rotation);
    Vec3 origin = rotate(toShape, ray.origin - shape.center), direction = rotate(toShape, ray.direction);
    float enter = 0, leave = 1e30f;
    Vec3 normal{0, 0, 0};
    bool entered = false;
    for (Vec4 plane : shape.convex->planes) {
        Vec3 n = xyz(plane);
        float toward = simd::dot(n, direction), room = plane.w - simd::dot(n, origin);
        if (std::abs(toward) < 1e-9f) {
            if (room < 0) return std::nullopt;                       // beside this face, outside it, going along it
            continue;
        }
        float t = room / toward;
        if (toward < 0) {
            if (t > enter || !entered) {
                if (t >= enter) enter = t, normal = n;
                entered = entered || t >= 0;
            }
        } else {
            leave = std::min(leave, t);
        }
        if (enter > leave) return std::nullopt;
    }
    if (!entered) return std::nullopt;                              // it starts inside: nothing to hit on the way out
    return ShapeRayHit{enter, rotate(shape.rotation, normal)};
}

}  // namespace

bool collide(const ShapeInstance& a, const ShapeInstance& b, ContactInfo& out) {
    using S = ColliderShape;
    if (a.shape == S::Convex || b.shape == S::Convex) {
        if ((a.shape == S::Convex && !a.convex) || (b.shape == S::Convex && !b.convex)) return false;
        if (a.shape == S::Convex && b.shape == S::Mesh) return convexMesh(a, b, out);
        if (b.shape == S::Convex && a.shape == S::Mesh) {
            if (!convexMesh(b, a, out)) return false;
            flip(out);
            return true;
        }
        if (a.shape == S::Convex && b.shape == S::Box) return convexBox(a, b, out);
        if (b.shape == S::Convex && a.shape == S::Box) {
            if (!convexBox(b, a, out)) return false;
            flip(out);
            return true;
        }
        if (a.shape == S::Mesh || b.shape == S::Mesh) return false;
        out = ContactInfo{};
        return convexGeneral(a, b, out);
    }
    if (a.shape == S::Mesh && b.shape == S::Mesh) return false;         // two meshes are two things that stand still
    if (b.shape == S::Mesh) return shapeMesh(a, b, out);
    if (a.shape == S::Mesh) {
        if (!shapeMesh(b, a, out)) return false;
        flip(out);
        return true;
    }
    if (a.shape == S::Sphere && b.shape == S::Sphere) return sphereSphere(a.center, a.radius, b.center, b.radius, out);
    if (a.shape == S::Sphere && b.shape == S::Box) return sphereBox(a.center, a.radius, b, out);
    if (a.shape == S::Box && b.shape == S::Box) return boxBox(a, b, out);
    if (a.shape == S::Capsule && b.shape == S::Sphere) {
        Vec3 onSegment = closestPointOnSegment(a.capsuleA(), a.capsuleB(), b.center);
        return sphereSphere(onSegment, a.radius, b.center, b.radius, out);
    }
    if (a.shape == S::Capsule && b.shape == S::Capsule) {
        Vec3 onA, onB;
        closestPointsBetweenSegments(a.capsuleA(), a.capsuleB(), b.capsuleA(), b.capsuleB(), onA, onB);
        return sphereSphere(onA, a.radius, onB, b.radius, out);
    }
    if (a.shape == S::Capsule && b.shape == S::Box) return capsuleBox(a, b, out);

    // The remaining pairs are the ones above with the arguments swapped.
    if (!collide(b, a, out)) return false;
    flip(out);
    return true;
}

std::optional<ShapeRayHit> raycastShape(const Ray& ray, const ShapeInstance& shape) {
    if (shape.shape == ColliderShape::Mesh) return raycastMesh(ray, shape);
    if (shape.shape == ColliderShape::Convex) return raycastConvex(ray, shape);
    switch (shape.shape) {
    case ColliderShape::Mesh:
    case ColliderShape::Convex:
        break;
    case ColliderShape::Sphere: {
        auto t = intersectSphere(ray, shape.center, shape.radius);
        if (!t) return std::nullopt;
        return ShapeRayHit{*t, safeNormalize(ray.at(*t) - shape.center)};
    }
    case ColliderShape::Box: {
        // Move the ray into the box's space and run the slab test, remembering which face was entered.
        Quat inverse = simd::conjugate(shape.rotation);
        Vec3 origin = rotate(inverse, ray.origin - shape.center), direction = rotate(inverse, ray.direction);
        float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
        Vec3 normal = -direction;                               // used when the ray starts inside
        for (int axis = 0; axis < 3; axis++) {
            float h = shape.halfExtents[axis];
            if (std::abs(direction[axis]) < 1e-8f) {
                if (std::abs(origin[axis]) > h) return std::nullopt;
                continue;
            }
            float t1 = (-h - origin[axis]) / direction[axis];
            float t2 = (h - origin[axis]) / direction[axis];
            float sign = -1.0f;
            if (t1 > t2) {
                std::swap(t1, t2);
                sign = 1.0f;
            }
            if (t1 > tMin) {
                tMin = t1;
                normal = Vec3{0, 0, 0};
                normal[axis] = sign;
            }
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return std::nullopt;
        }
        return ShapeRayHit{tMin, rotate(shape.rotation, normal)};
    }
    case ColliderShape::Capsule: {
        // Inigo Quilez's capsule intersection.
        Vec3 pa = shape.capsuleA(), pb = shape.capsuleB();
        Vec3 ba = pb - pa, oa = ray.origin - pa;
        float baba = simd::dot(ba, ba), bard = simd::dot(ba, ray.direction), baoa = simd::dot(ba, oa);
        float rdoa = simd::dot(ray.direction, oa), oaoa = simd::dot(oa, oa);
        float a = baba - bard * bard;
        float b = baba * rdoa - baoa * bard;
        float c = baba * oaoa - baoa * baoa - shape.radius * shape.radius * baba;
        float h = b * b - a * c;
        float t = -1.0f;
        if (h >= 0.0f && std::abs(a) > 1e-10f) {
            float candidate = (-b - std::sqrt(h)) / a;
            float y = baoa + candidate * bard;
            if (y > 0.0f && y < baba) {
                t = candidate;                                  // hit the cylindrical part
            } else {
                Vec3 oc = y <= 0.0f ? oa : ray.origin - pb;     // otherwise test the nearer cap
                float capB = simd::dot(ray.direction, oc);
                float capC = simd::dot(oc, oc) - shape.radius * shape.radius;
                float capH = capB * capB - capC;
                if (capH > 0.0f) t = -capB - std::sqrt(capH);
            }
        } else if (std::abs(a) <= 1e-10f) {
            // The ray runs along the capsule's axis: only the caps can be hit.
            for (Vec3 capCenter : {pa, pb}) {
                auto capHit = intersectSphere(ray, capCenter, shape.radius);
                if (capHit && (t < 0.0f || *capHit < t)) t = *capHit;
            }
        }
        if (t < 0.0f) return std::nullopt;
        Vec3 hitPoint = ray.at(t);
        return ShapeRayHit{t, safeNormalize(hitPoint - closestPointOnSegment(pa, pb, hitPoint))};
    }
    }
    return std::nullopt;
}

}  // namespace molten
