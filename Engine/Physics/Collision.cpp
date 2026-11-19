#include "Physics/Collision.hpp"

#include <algorithm>

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
    // The corner of b that reaches farthest into a.
    Vec3 corner = b.center;
    for (int i = 0; i < 3; i++) {
        corner += axesB[i] * (simd::dot(axesB[i], bestAxis) > 0 ? -b.halfExtents[i] : b.halfExtents[i]);
    }
    out.point = corner;
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
    switch (shape) {
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

bool collide(const ShapeInstance& a, const ShapeInstance& b, ContactInfo& out) {
    using S = ColliderShape;
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
    switch (shape.shape) {
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
