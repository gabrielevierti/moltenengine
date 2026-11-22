#include "Kit/Cloth.hpp"

#include "Core/Log.hpp"
#include "Game/Component.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Mesh.hpp"
#include "Rendering/Particles.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace molten::kit {

void linkCloth() {
    ComponentRegistry::instance().setCategory("Cloth", "Physics");
}

namespace {

constexpr float kAirDensity = 1.2f;         // kilograms per cubic meter
constexpr size_t kMostPoints = 12000;       // more than this is not worked out (a whole model by mistake)

/// The axis a mesh is thinnest along (0 x, 1 y, 2 z): a sheet's is the one it faces along.
int thinnestAxis(const AABB& box) {
    Vec3 size = box.max - box.min;
    if (size.x <= size.y && size.x <= size.z) return 0;
    return size.y <= size.z ? 1 : 2;
}

}  // namespace

std::vector<size_t> pinnedPoints(const std::vector<Vec3>& points, const std::string& pinned, float spacing) {
    std::vector<size_t> out;
    if (points.empty() || pinned == "none" || pinned.empty()) return out;
    AABB box;
    for (Vec3 p : points) box.expand(p);
    // Which way is up, and which way across, in the mesh's space.
    int thin = thinnestAxis(box);
    Vec3 up = thin == 1 ? Vec3{0, 0, -1} : Vec3{0, 1, 0};
    Vec3 across = thin == 0 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    float top = -1e30f, low = 1e30f, left = 1e30f, right = -1e30f;
    for (Vec3 p : points) {
        top = std::max(top, simd::dot(p, up));
        low = std::min(low, simd::dot(p, up));
        left = std::min(left, simd::dot(p, across));
        right = std::max(right, simd::dot(p, across));
    }
    float band = std::max(spacing * 0.3f, 1e-5f);
    for (size_t i = 0; i < points.size(); i++) {
        float height = simd::dot(points[i], up), side = simd::dot(points[i], across);
        bool atTop = height >= top - band, atBottom = height <= low + band;
        bool atLeft = side <= left + band, atRight = side >= right - band;
        bool holds = false;
        if (pinned == "top edge") holds = atTop;
        else if (pinned == "top corners") holds = atTop && (atLeft || atRight);
        else if (pinned == "left edge") holds = atLeft;
        else if (pinned == "corners") holds = (atTop || atBottom) && (atLeft || atRight);
        if (holds) out.push_back(i);
    }
    return out;
}

bool ClothSimulation::build(const MeshData& mesh, const Mat4& place, const std::string& pinned, const ClothSettings& wanted) {
    settings = wanted;
    if (mesh.indices.size() < 3 || mesh.positions.empty()) return false;
    rest_ = mesh;
    lastPlace_ = place;
    // Corners in one place (split for their uvs or normals) are one point of the cloth.
    AABB box;
    for (Vec3 p : mesh.positions) box.expand(p);
    float size = std::max(simd::length(box.max - box.min), 1e-4f);
    float cell = size * 1e-5f;
    std::map<std::tuple<long, long, long>, uint32_t> found;
    pointOf_.assign(mesh.positions.size(), 0);
    restLocal_.clear();
    for (size_t i = 0; i < mesh.positions.size(); i++) {
        Vec3 p = mesh.positions[i];
        auto key = std::make_tuple(long(std::lround(p.x / cell)), long(std::lround(p.y / cell)), long(std::lround(p.z / cell)));
        auto at = found.find(key);
        if (at == found.end()) {
            at = found.emplace(key, uint32_t(restLocal_.size())).first;
            restLocal_.push_back(p);
        }
        pointOf_[i] = at->second;
    }
    if (restLocal_.size() > kMostPoints) {
        logWarning("cloth: %zu points is too many to work out (at most %zu): use a simpler mesh", restLocal_.size(), kMostPoints);
        restLocal_.clear();
        return false;
    }
    size_t count = restLocal_.size();
    positions_.resize(count);
    for (size_t i = 0; i < count; i++) positions_[i] = transformPoint(place, restLocal_[i]);
    velocities_.assign(count, Vec3{0, 0, 0});

    // Triangles, and each point's share of the cloth's weight (a third of each triangle it is a corner of).
    triangles_.clear();
    std::vector<float> mass(count, 0.0f);
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        uint32_t a = pointOf_[mesh.indices[t]], b = pointOf_[mesh.indices[t + 1]], c = pointOf_[mesh.indices[t + 2]];
        if (a == b || b == c || a == c) continue;
        triangles_.insert(triangles_.end(), {a, b, c});
        float area = 0.5f * simd::length(simd::cross(positions_[b] - positions_[a], positions_[c] - positions_[a]));
        float share = std::max(settings.weight, 0.001f) * area / 3.0f;
        mass[a] += share;
        mass[b] += share;
        mass[c] += share;
    }
    if (triangles_.empty()) return false;

    // Links along every edge, and across every edge two triangles share (between their far corners).
    links_.clear();
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> oppositeOf;        // an edge, and the far corner of the first triangle seen on it
    size_t edges = 0;
    auto rest = [&](uint32_t a, uint32_t b) { return simd::length(positions_[a] - positions_[b]); };
    for (size_t t = 0; t < triangles_.size(); t += 3) {
        for (int k = 0; k < 3; k++) {
            uint32_t a = triangles_[t + size_t(k)], b = triangles_[t + size_t((k + 1) % 3)], far = triangles_[t + size_t((k + 2) % 3)];
            auto edge = std::make_pair(std::min(a, b), std::max(a, b));
            auto seen = oppositeOf.find(edge);
            if (seen == oppositeOf.end()) {
                oppositeOf.emplace(edge, far);
                links_.push_back(Link{a, b, rest(a, b), false});
                edges++;
            } else if (seen->second != far) {
                links_.push_back(Link{seen->second, far, rest(seen->second, far), true});
            }
        }
    }

    // The pins: held, so of no weight that anything can move.
    inverseMass_.resize(count);
    for (size_t i = 0; i < count; i++) inverseMass_[i] = mass[i] > 0 ? 1.0f / mass[i] : 0.0f;
    float localSpacing = 0.0f;
    for (const Link& link : links_) {
        if (!link.isBend) localSpacing += simd::length(restLocal_[link.a] - restLocal_[link.b]);
    }
    localSpacing = edges > 0 ? localSpacing / float(edges) : 0.1f;
    std::vector<size_t> pins = pinnedPoints(restLocal_, pinned, localSpacing);
    for (size_t pin : pins) inverseMass_[pin] = 0.0f;
    // Each free point tethered to its nearest pin: it can come nearer, never go further than at
    // rest. What keeps a long curtain from stretching under its own weight between passes.
    tethers_.clear();
    if (!pins.empty()) {
        for (size_t i = 0; i < count; i++) {
            if (inverseMass_[i] <= 0) continue;
            float nearest = 1e30f;
            size_t which = pins.front();
            for (size_t pin : pins) {
                float d = simd::length(positions_[i] - positions_[pin]);
                if (d < nearest) nearest = d, which = pin;
            }
            tethers_.push_back(Tether{uint32_t(i), uint32_t(which), nearest});
        }
    }
    return true;
}

std::vector<size_t> ClothSimulation::pinned() const {
    std::vector<size_t> out;
    for (size_t i = 0; i < inverseMass_.size(); i++) {
        if (inverseMass_[i] <= 0) out.push_back(i);
    }
    return out;
}

AABB ClothSimulation::bounds() const {
    AABB box;
    for (Vec3 p : positions_) box.expand(p);
    return box;
}

float ClothSimulation::mostStretched() const {
    float most = 1.0f;
    for (const Link& link : links_) {
        if (link.isBend || link.rest <= 1e-6f) continue;
        most = std::max(most, simd::length(positions_[link.a] - positions_[link.b]) / link.rest);
    }
    return most;
}

void ClothSimulation::step(float dt, const Mat4& place, Vec3 gravity, const std::function<Vec3(Vec3)>& air,
                           const std::vector<ShapeInstance>& obstacles, double time) {
    size_t count = positions_.size();
    if (count == 0 || dt <= 0) return;
    // Put somewhere else at once (a scene loaded, a player sent back to the start): it starts again there.
    if (simd::length(xyz(place.columns[3]) - xyz(lastPlace_.columns[3])) > 10.0f) {
        for (size_t i = 0; i < count; i++) positions_[i] = transformPoint(place, restLocal_[i]);
        velocities_.assign(count, Vec3{0, 0, 0});
        lastPlace_ = place;
        return;
    }

    // ---- the air, once a step: on each triangle, as much as it meets it (and a little along it),
    // from the wind against the cloth's own motion. A sheet edge-on to the wind still flaps: the
    // pressure on it comes and goes in waves that travel down the wind, as the air that leaves it does.
    std::vector<Vec3> pushed(count, Vec3{0, 0, 0});
    for (size_t t = 0; t < triangles_.size(); t += 3) {
        uint32_t a = triangles_[t], b = triangles_[t + 1], c = triangles_[t + 2];
        Vec3 across = simd::cross(positions_[b] - positions_[a], positions_[c] - positions_[a]);
        float twice = simd::length(across);
        if (twice < 1e-10f) continue;
        Vec3 n = across / twice;
        float area = 0.5f * twice;
        Vec3 middle = (positions_[a] + positions_[b] + positions_[c]) / 3.0f;
        Vec3 wind = air ? air(middle) * settings.wind : Vec3{0, 0, 0};
        Vec3 relative = wind - (velocities_[a] + velocities_[b] + velocities_[c]) / 3.0f;
        float against = simd::dot(relative, n);
        Vec3 along = relative - n * against;
        Vec3 force = n * (0.5f * kAirDensity * 1.2f * area * against * std::fabs(against));
        force += along * (0.5f * kAirDensity * 0.04f * area * simd::length(along));
        float speed = simd::length(wind);
        if (speed > 0.3f) {
            Vec3 way = wind / speed;
            float k = 2.0f * kPi / 1.6f;                                    // waves about a meter and a half long
            float wave = std::sin(k * simd::dot(middle, way) - float(time) * k * speed * 0.55f + 1.7f * std::sin(float(time) * 0.9f));
            force += n * (0.5f * kAirDensity * 0.12f * area * speed * speed * wave);
        }
        Vec3 each = force / 3.0f;
        pushed[a] += each;
        pushed[b] += each;
        pushed[c] += each;
    }

    // ---- small steps, each going once over the links (stiffer than many passes over few steps)
    int steps = std::max(substeps, 1);
    float h = dt / float(steps);
    float invH2 = 1.0f / (h * h);
    float stretchGive = (1.0f - std::clamp(settings.stretch, 0.0f, 1.0f));
    stretchGive = stretchGive * stretchGive * 0.05f;
    float bendGive = 1.0f - std::clamp(settings.stiffness, 0.0f, 1.0f);
    bendGive = bendGive * bendGive * bendGive * bendGive * 2.0f;
    float keep = std::max(0.0f, 1.0f - std::max(settings.damping, 0.0f) * h);
    float radius = std::max(settings.thickness, 0.001f);
    AABB reach = bounds();
    reach.min -= Vec3{1, 1, 1} * (radius + 0.5f);
    reach.max += Vec3{1, 1, 1} * (radius + 0.5f);
    std::vector<const ShapeInstance*> near;
    for (const ShapeInstance& shape : obstacles) {
        if (shape.bounds().intersects(reach)) near.push_back(&shape);
    }
    std::vector<AABB> nearBounds;
    for (const ShapeInstance* shape : near) {
        AABB box = shape->bounds();
        box.min -= Vec3{1, 1, 1} * radius;
        box.max += Vec3{1, 1, 1} * radius;
        nearBounds.push_back(box);
    }
    std::vector<Vec3> before(count);
    for (int s = 0; s < steps; s++) {
        float through = float(s + 1) / float(steps);
        for (size_t i = 0; i < count; i++) {
            before[i] = positions_[i];
            if (inverseMass_[i] <= 0) {
                // A pin: where the entity puts it, on the way from where it was at the last step.
                positions_[i] = lerp(transformPoint(lastPlace_, restLocal_[i]), transformPoint(place, restLocal_[i]), through);
                continue;
            }
            velocities_[i] += (gravity + pushed[i] * inverseMass_[i]) * h;
            velocities_[i] *= keep;
            positions_[i] += velocities_[i] * h;
        }
        // The links: each pulled back toward its length, the more the less it gives (XPBD).
        for (const Link& link : links_) {
            float wa = inverseMass_[link.a], wb = inverseMass_[link.b];
            float w = wa + wb;
            if (w <= 0) continue;
            Vec3 apart = positions_[link.a] - positions_[link.b];
            float distance = simd::length(apart);
            if (distance < 1e-9f) continue;
            float give = (link.isBend ? bendGive : stretchGive) * invH2;
            float move = -(distance - link.rest) / (w + give);
            Vec3 n = apart / distance;
            positions_[link.a] += n * (move * wa);
            positions_[link.b] -= n * (move * wb);
        }
        for (const Tether& tether : tethers_) {
            Vec3 out = positions_[tether.point] - positions_[tether.pin];
            float distance = simd::length(out);
            if (distance > tether.most && distance > 1e-9f) positions_[tether.point] -= out * ((distance - tether.most) / distance);
        }
        // What it meets: each point a small ball, pushed out of the shape and held by its friction.
        if (!near.empty()) {
            ShapeInstance ball;
            ball.shape = ColliderShape::Sphere;
            ball.radius = radius;
            for (size_t i = 0; i < count; i++) {
                if (inverseMass_[i] <= 0) continue;
                for (size_t k = 0; k < near.size(); k++) {
                    if (!nearBounds[k].contains(positions_[i])) continue;
                    ball.center = positions_[i];
                    ContactInfo contact;
                    if (!collide(ball, *near[k], contact) || contact.depth <= 0) continue;
                    positions_[i] -= contact.normal * contact.depth;
                    Vec3 moved = positions_[i] - before[i];
                    Vec3 slid = moved - contact.normal * simd::dot(moved, contact.normal);
                    positions_[i] -= slid * std::clamp(settings.friction, 0.0f, 1.0f);
                }
            }
        }
        for (size_t i = 0; i < count; i++) velocities_[i] = (positions_[i] - before[i]) / h;
    }
    lastPlace_ = place;
}

MeshData ClothSimulation::shape(const Mat4& place, bool withBackFaces) const {
    MeshData out = rest_;
    if (positions_.empty()) return out;
    Mat4 toLocal = simd::inverse(place);
    std::vector<Vec3> local(positions_.size());
    for (size_t i = 0; i < positions_.size(); i++) local[i] = transformPoint(toLocal, positions_[i]);
    // Each point's normal: the triangles round it, the larger the more.
    std::vector<Vec3> normal(positions_.size(), Vec3{0, 0, 0});
    for (size_t t = 0; t < triangles_.size(); t += 3) {
        uint32_t a = triangles_[t], b = triangles_[t + 1], c = triangles_[t + 2];
        Vec3 n = simd::cross(local[b] - local[a], local[c] - local[a]);
        normal[a] += n;
        normal[b] += n;
        normal[c] += n;
    }
    for (Vec3& n : normal) n = safeNormalize(n, Vec3{0, 1, 0});
    out.normals.resize(out.positions.size());
    for (size_t v = 0; v < out.positions.size(); v++) {
        out.positions[v] = local[pointOf_[v]];
        out.normals[v] = normal[pointOf_[v]];
    }
    if (withBackFaces) {
        size_t corners = out.positions.size();
        for (size_t v = 0; v < corners; v++) {
            out.positions.push_back(out.positions[v]);
            out.normals.push_back(-out.normals[v]);
            if (v < out.uvs.size()) out.uvs.push_back(out.uvs[v]);
            if (v < out.tangents.size()) out.tangents.push_back(out.tangents[v]);
        }
        size_t triangles = out.indices.size();
        for (size_t t = 0; t + 2 < triangles; t += 3) {
            out.indices.push_back(out.indices[t] + uint32_t(corners));
            out.indices.push_back(out.indices[t + 2] + uint32_t(corners));
            out.indices.push_back(out.indices[t + 1] + uint32_t(corners));
        }
    }
    return out;
}

// ---------------------------------------------------------------- the component

ClothSettings Cloth::settingsNow() const {
    ClothSettings out;
    out.stretch = stretch;
    out.stiffness = stiffness;
    out.weight = weight;
    out.wind = wind;
    out.damping = damping;
    out.thickness = thickness;
    return out;
}

void Cloth::start(World& world, Entity self) {
    const Transform* transform = world.scene.get<Transform>(self);
    const MeshRenderer* renderer = world.scene.get<MeshRenderer>(self);
    if (!transform || !renderer) return;
    std::string name = renderer->mesh ? renderer->mesh->name : renderer->meshName;
    std::optional<MeshData> mesh;
    // The built-in plane is one square: as cloth, a sheet of many.
    if (name.empty() || name == "plane") mesh = MeshData::plane(1.0f, 1.0f, std::clamp(segments, 2, 96));
    else mesh = world.readMeshData(name);
    if (!mesh) {
        logWarning("cloth: the mesh '%s' cannot be read", name.c_str());
        return;
    }
    auto made = std::make_shared<ClothSimulation>();
    if (!made->build(*mesh, transform->world, pinned, settingsNow())) return;
    simulation = made;
}

void Cloth::fixedUpdate(World& world, Entity self, float dt) {
    const Transform* transform = world.scene.get<Transform>(self);
    if (!simulation || !transform) return;
    simulation->settings = settingsNow();          // changed while it runs (the Inspector, the game's code): at once
    std::vector<ShapeInstance> obstacles;
    if (collides) {
        world.scene.each<Collider, Transform>([&](Entity entity, Collider& collider, Transform& place) {
            if (entity == self || collider.isTrigger || !place.isActiveInHierarchy) return;
            if (collider.shape == ColliderShape::Mesh && !collider.mesh && !collider.convex) return;      // not read yet
            Vec3 position;
            Quat rotation;
            worldPlace(place, position, rotation);
            obstacles.push_back(Physics::makeShape(collider, position, rotation));
        });
    }
    Vec3 wind = world.wind();
    float gusts = world.gusts();
    double time = world.effectsTime;
    simulation->step(dt, transform->world, world.physics.gravity, [&](Vec3 at) { return windAt(wind, gusts, at, time); }, obstacles, time);
}

void Cloth::lateUpdate(World& world, Entity self, float) {
    const Transform* transform = world.scene.get<Transform>(self);
    const MeshRenderer* renderer = world.scene.get<MeshRenderer>(self);
    if (!simulation || !transform || !renderer) return;
    // Seen from behind too, unless its material already is.
    bool backFaces = !(renderer->material && renderer->material->isDoubleSided);
    world.showMesh(self, simulation->shape(transform->world, backFaces));
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Cloth,
                 MOLTEN_FIELD(pinned).choices({"top edge", "top corners", "left edge", "corners", "none"})
                     .help("Which of its points hold where the entity puts them, and go where it goes: the top edge (a curtain, washing), "
                           "the top corners (a banner, a hammock), the left edge (a flag on its pole), the four corners (a canopy, a sail), "
                           "none (it falls: a tablecloth). The top is the mesh's up; for the plane, the edge on top once it is stood up"),
                 MOLTEN_FIELD(segments).range(2, 96).help("For the plane: squares along each side. More: finer folds, more to work out"),
                 MOLTEN_FIELD(stretch).range(0, 1).help("1: it does not stretch at all; lower, as a knit does; 0 as rubber"),
                 MOLTEN_FIELD(stiffness).range(0, 1).help("How it resists folding: 0 silk, 0.2 cotton, 0.6 leather, 1 card"),
                 MOLTEN_FIELD(weight).range(0.01f, 5).help("Kilograms per square meter: 0.05 silk, 0.25 cotton, 0.5 canvas, 2 a rubber mat"),
                 MOLTEN_FIELD(wind).range(0, 10).help("How much the scene's wind and its gusts push it (Environment > Weather > Wind). 0: none, though it still falls through the air"),
                 MOLTEN_FIELD(damping).range(0, 10).help("How soon it settles, per second"),
                 MOLTEN_FIELD(collides).help("Kept out of the colliders around it (not out of itself)"),
                 MOLTEN_FIELD(thickness).range(0.001f, 0.2f).help("Meters it keeps from what it collides with"))
