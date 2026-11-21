// Spline.cpp
#include "Kit/Spline.hpp"

#include "Assets/MeshData.hpp"
#include "Game/Component.hpp"
#include "Physics/Collision.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace molten::kit {

void linkSplines() {}

Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    // Centripetal: knots spaced by the square root of the distances, so the curve neither
    // loops nor cusps where points are close together.
    auto knot = [](float previous, Vec3 a, Vec3 b) { return previous + std::max(std::sqrt(simd::length(b - a)), 1e-4f); };
    float t0 = 0, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
    float u = t1 + (t2 - t1) * t;
    Vec3 a1 = p0 * ((t1 - u) / (t1 - t0)) + p1 * ((u - t0) / (t1 - t0));
    Vec3 a2 = p1 * ((t2 - u) / (t2 - t1)) + p2 * ((u - t1) / (t2 - t1));
    Vec3 a3 = p2 * ((t3 - u) / (t3 - t2)) + p3 * ((u - t2) / (t3 - t2));
    Vec3 b1 = a1 * ((t2 - u) / (t2 - t0)) + a2 * ((u - t0) / (t2 - t0));
    Vec3 b2 = a2 * ((t3 - u) / (t3 - t1)) + a3 * ((u - t1) / (t3 - t1));
    return b1 * ((t2 - u) / (t2 - t1)) + b2 * ((u - t1) / (t2 - t1));
}

void SplinePath::build(const std::vector<Vec3>& controls, bool closed, int perSegment) {
    points.clear();
    distances.clear();
    isClosed = closed && controls.size() > 2;
    size_t count = controls.size();
    if (count == 0) return;
    if (count == 1) {
        points = {controls[0]};
        distances = {0};
        return;
    }
    perSegment = std::clamp(perSegment, 1, 256);
    auto at = [&](long i) {
        if (isClosed) return controls[size_t(((i % long(count)) + long(count)) % long(count))];
        // Past the ends: the end point mirrored, so the curve leaves it straight.
        if (i < 0) return controls[0] * 2.0f - controls[1];
        if (i >= long(count)) return controls[count - 1] * 2.0f - controls[count - 2];
        return controls[size_t(i)];
    };
    long segments = isClosed ? long(count) : long(count) - 1;
    for (long s = 0; s < segments; s++) {
        Vec3 p0 = at(s - 1), p1 = at(s), p2 = at(s + 1), p3 = at(s + 2);
        for (int k = 0; k < perSegment; k++) points.push_back(catmullRom(p0, p1, p2, p3, float(k) / float(perSegment)));
    }
    points.push_back(isClosed ? controls[0] : controls[count - 1]);
    distances.resize(points.size());
    distances[0] = 0;
    for (size_t i = 1; i < points.size(); i++) distances[i] = distances[i - 1] + simd::length(points[i] - points[i - 1]);
}

namespace {
/// The piece of the path a distance falls in, and how far into it (0 to 1).
size_t pieceAt(const SplinePath& path, float& distance, float& fraction) {
    float length = path.length();
    if (path.isClosed && length > 0) {
        distance = std::fmod(distance, length);
        if (distance < 0) distance += length;
    } else {
        distance = std::clamp(distance, 0.0f, length);
    }
    size_t i = size_t(std::upper_bound(path.distances.begin(), path.distances.end(), distance) - path.distances.begin());
    i = std::clamp<size_t>(i, 1, path.points.size() - 1);
    float span = path.distances[i] - path.distances[i - 1];
    fraction = span > 1e-6f ? (distance - path.distances[i - 1]) / span : 0.0f;
    return i;
}
}  // namespace

Vec3 SplinePath::pointAt(float distance) const {
    if (points.empty()) return Vec3{0, 0, 0};
    if (points.size() == 1) return points[0];
    float fraction = 0;
    size_t i = pieceAt(*this, distance, fraction);
    return points[i - 1] + (points[i] - points[i - 1]) * fraction;
}

Vec3 SplinePath::directionAt(float distance) const {
    if (points.size() < 2) return Vec3{0, 0, -1};
    float fraction = 0;
    size_t i = pieceAt(*this, distance, fraction);
    Vec3 along = points[i] - points[i - 1];
    // Between the pieces on either side, so that it turns smoothly rather than at each point.
    Vec3 next = i + 1 < points.size() ? points[i + 1] - points[i] : (isClosed ? points[1] - points[0] : along);
    Vec3 before = i >= 2 ? points[i - 1] - points[i - 2] : (isClosed ? points[points.size() - 1] - points[points.size() - 2] : along);
    Vec3 blended = fraction < 0.5f ? before * (0.5f - fraction) + along * (0.5f + fraction) : along * (1.5f - fraction) + next * (fraction - 0.5f);
    float length = simd::length(blended);
    return length > 1e-6f ? blended / length : Vec3{0, 0, -1};
}

float SplinePath::closestDistance(Vec3 point) const {
    if (points.size() < 2) return 0;
    float best = 1e30f, found = 0;
    for (size_t i = 1; i < points.size(); i++) {
        Vec3 a = points[i - 1], ab = points[i] - a;
        float span = simd::length_squared(ab);
        float t = span > 1e-12f ? std::clamp(simd::dot(point - a, ab) / span, 0.0f, 1.0f) : 0.0f;
        float gap = simd::length_squared(point - (a + ab * t));
        if (gap < best) {
            best = gap;
            found = distances[i - 1] + (distances[i] - distances[i - 1]) * t;
        }
    }
    return found;
}

// ---------------------------------------------------------------- Spline

std::vector<Entity> Spline::pointsOf(World& world, Entity self) {
    // Each child with the number its name ends in: "Point 12" after "Point 2".
    struct Found {
        long number;
        size_t order;
        Entity entity;
    };
    std::vector<Found> found;
    world.scene.each<Transform>([&](Entity entity, Transform& transform) {
        if (transform.parent != self) return;
        long number = -1;
        if (const Name* name = world.scene.get<Name>(entity)) {
            const std::string& text = name->value;
            size_t end = text.size();
            while (end > 0 && !std::isdigit(static_cast<unsigned char>(text[end - 1]))) end--;
            size_t start = end;
            while (start > 0 && std::isdigit(static_cast<unsigned char>(text[start - 1]))) start--;
            if (start < end && end - start < 9) number = std::stol(text.substr(start, end - start));
        }
        found.push_back(Found{number, found.size(), entity});
    });
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.number < b.number; });
    std::vector<Entity> out;
    for (const Found& f : found) out.push_back(f.entity);
    return out;
}

bool Spline::refresh(World& world, Entity self) {
    std::vector<Vec3> controls;
    for (Entity point : pointsOf(world, self)) {
        if (const Transform* transform = world.scene.get<Transform>(point)) controls.push_back(transform->worldPosition());
    }
    // A fingerprint of what the curve is made from: nothing to do if it has not changed.
    uint64_t made = 1469598103934665603ull;
    auto mix = [&made](const void* data, size_t size) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; i++) made = (made ^ bytes[i]) * 1099511628211ull;
    };
    for (const Vec3& p : controls) {
        float xyz[3] = {p.x, p.y, p.z};
        mix(xyz, sizeof(xyz));
    }
    mix(&closed, sizeof(closed));
    mix(&smoothness, sizeof(smoothness));
    if (made == signature && !path.points.empty()) return false;
    signature = made;
    path.build(controls, closed, smoothness);
    return true;
}

Spline* findSpline(World& world, const std::string& name, Entity* entity) {
    Entity found = world.find(name);
    Spline* spline = world.scene.get<Spline>(found);
    if (spline) spline->refresh(world, found);
    if (entity) *entity = found;
    return spline;
}

// ---------------------------------------------------------------- SplineFollower

void SplineFollower::update(World& world, Entity self, float dt) {
    Spline* curve = findSpline(world, spline);
    Transform* transform = world.scene.get<Transform>(self);
    if (!curve || !transform || curve->path.points.size() < 2) return;
    const SplinePath& path = curve->path;
    float length = path.length();
    if (!hasEnded) distance += speed * direction * dt;
    if (!path.isClosed) {
        if (distance > length || distance < 0) {
            if (ending == 0) {
                distance = distance > length ? std::fmod(distance, std::max(length, 1e-3f)) : length;
            } else if (ending == 1) {
                direction = -direction;
                distance = distance > length ? 2 * length - distance : -distance;
            } else {
                hasEnded = true;
            }
            distance = std::clamp(distance, 0.0f, length);
        }
    }
    Vec3 forward = path.directionAt(distance) * (direction < 0 ? -1.0f : 1.0f);
    Vec3 right = simd::normalize(simd::cross(forward, Vec3{0, 1, 0}) + Vec3{1e-6f, 0, 0});
    Vec3 up = simd::cross(right, forward);
    Vec3 at = path.pointAt(distance) + right * offset.x + up * offset.y - forward * offset.z;
    // Where the transform keeps it: relative to its parent, if it has one.
    Quat facing = transform->rotation;
    if (facesForward) facing = lookRotation(forward, Vec3{0, 1, 0});
    Mat4 world4 = compose(at, facing, transform->scale);
    if (const Transform* parent = world.scene.get<Transform>(transform->parent)) world4 = simd::inverse(parent->world) * world4;
    Vec3 scale;
    decompose(world4, transform->position, transform->rotation, scale);
    if (!facesForward) transform->rotation = facing;
}

// ---------------------------------------------------------------- SplineRoad

MeshData SplineRoad::makeMesh(const SplinePath& path, const Mat4& toLocal, float width, float uvLength, float lift) {
    MeshData mesh;
    size_t count = path.points.size();
    if (count < 2) return mesh;
    float half = width * 0.5f;
    for (size_t i = 0; i < count; i++) {
        float distance = path.distances[i];
        Vec3 forward = path.directionAt(std::min(distance, path.length() - 1e-4f));
        Vec3 right = simd::cross(forward, Vec3{0, 1, 0});
        float side = simd::length(right);
        right = side > 1e-5f ? right / side : Vec3{1, 0, 0};
        Vec3 up = simd::normalize(simd::cross(right, forward));
        Vec3 middle = path.points[i] + up * lift;
        for (int edge = 0; edge < 2; edge++) {
            Vec3 at = middle + right * (edge == 0 ? -half : half);
            mesh.positions.push_back(xyz(toLocal * Vec4{at.x, at.y, at.z, 1}));
            mesh.normals.push_back(simd::normalize(xyz(toLocal * Vec4{up.x, up.y, up.z, 0})));
            mesh.uvs.push_back(Vec2{float(edge), distance / std::max(uvLength, 0.01f)});
        }
    }
    for (uint32_t i = 0; i + 1 < uint32_t(count); i++) {
        uint32_t a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});     // counter-clockwise from above
    }
    mesh.computeTangents();
    return mesh;
}

void SplineRoad::update(World& world, Entity self, float) {
    const Spline* spline = world.scene.get<Spline>(self);
    if (spline && spline->signature != builtFrom) rebuild(world, self);
}

bool SplineRoad::rebuild(World& world, Entity self) {
    Spline* spline = world.scene.get<Spline>(self);
    Transform* transform = world.scene.get<Transform>(self);
    if (!spline || !transform) return false;
    spline->refresh(world, self);
    builtFrom = spline->signature;
    if (spline->path.points.size() < 2) return false;
    MeshData mesh = makeMesh(spline->path, simd::inverse(transform->world), width, uvLength, lift);
    if (!world.scene.has<MeshRenderer>(self)) world.scene.add(self, MeshRenderer{});
    world.showMesh(self, mesh);
    if (hasCollider) {
        if (!world.scene.has<Collider>(self)) world.scene.add(self, Collider{});
        Collider* collider = world.scene.get<Collider>(self);
        Vec3 position, scale;
        Quat rotation;
        decompose(transform->world, position, rotation, scale);
        collider->shape = ColliderShape::Mesh;
        collider->isConvex = false;
        collider->meshName.clear();
        collider->mesh = MeshShape::build(mesh.positions, mesh.indices, scale);
        collider->convex = nullptr;
        collider->meshScale = transform->scale;
    }
    return true;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Spline, MOLTEN_FIELD(closed).help("The end joins the start, as on a circuit"),
                 MOLTEN_FIELD(smoothness).range(1, 64).help("Points made between two of its points"))
MOLTEN_COMPONENT(SplineFollower, MOLTEN_FIELD(spline).help("The name of the entity with the Spline"), MOLTEN_FIELD(speed).range(-50, 50),
                 MOLTEN_FIELD(distance).help("Meters along the curve: where it starts"),
                 MOLTEN_FIELD(ending).range(0, 2).help("At the end: 0 round again, 1 back the other way, 2 stop"), MOLTEN_FIELD(facesForward),
                 MOLTEN_FIELD(offset).help("From the curve, turned with it: x to its right, y up"))
MOLTEN_COMPONENT(SplineRoad, MOLTEN_FIELD(width).range(0.5f, 60), MOLTEN_FIELD(uvLength).range(0.1f, 100).help("Meters of road per repeat of its texture"),
                 MOLTEN_FIELD(lift).range(0, 1), MOLTEN_FIELD(hasCollider).help("Solid, to drive and walk on"))
