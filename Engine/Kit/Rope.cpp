#include "Kit/Rope.hpp"

#include "Game/Component.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

void linkRope() {
    ComponentRegistry::instance().setCategory("Rope", "Physics");
}

namespace {

/// The turn that takes up (0, 1, 0) to `direction`.
Quat upTo(Vec3 direction) {
    Vec3 up{0, 1, 0};
    Vec3 d = safeNormalize(direction, up);
    float cosine = simd::dot(up, d);
    if (cosine > 0.9999f) return Quat{};
    if (cosine < -0.9999f) return Quat(kPi, Vec3{1, 0, 0});
    return Quat(std::acos(std::clamp(cosine, -1.0f, 1.0f)), simd::normalize(simd::cross(up, d)));
}

/// Two directions across `along`, at right angles to it and to each other.
void across(Vec3 along, Vec3& u, Vec3& v) {
    Vec3 helper = std::abs(along.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    u = safeNormalize(simd::cross(along, helper), Vec3{1, 0, 0});
    v = simd::cross(along, u);
}

void addVertex(MeshData& mesh, Vec3 position, Vec3 normal, Vec2 uv) {
    mesh.positions.push_back(position);
    mesh.normals.push_back(normal);
    mesh.uvs.push_back(uv);
}

}  // namespace

std::vector<Vec3> hangingPoints(Vec3 from, Vec3 to, float length, int count) {
    std::vector<Vec3> points;
    count = std::max(count, 1);
    float distance = simd::length(to - from);
    // A parabola down from the line between the ends, as deep as makes it `length` long.
    auto at = [&](float t, float sag) { return from + (to - from) * t + Vec3{0, -4 * sag * t * (1 - t), 0}; };
    auto arc = [&](float sag) {
        float total = 0;
        Vec3 last = from;
        for (int i = 1; i <= 64; i++) {
            Vec3 next = at(float(i) / 64, sag);
            total += simd::length(next - last);
            last = next;
        }
        return total;
    };
    float sag = 0;
    if (length > distance + 1e-4f) {
        float low = 0, high = std::max(length, 0.01f);
        for (int i = 0; i < 40; i++) {
            float middle = (low + high) * 0.5f;
            (arc(middle) < length ? low : high) = middle;
        }
        sag = (low + high) * 0.5f;
    }
    // Equally far apart along the curve.
    std::vector<Vec3> fine;
    std::vector<float> along{0};
    for (int i = 0; i <= 256; i++) fine.push_back(at(float(i) / 256, sag));
    for (size_t i = 1; i < fine.size(); i++) along.push_back(along.back() + simd::length(fine[i] - fine[i - 1]));
    float total = along.back();
    size_t k = 0;
    for (int i = 0; i <= count; i++) {
        float wanted = total * float(i) / float(count);
        while (k + 1 < fine.size() && along[k + 1] < wanted) k++;
        size_t next = std::min(k + 1, fine.size() - 1);
        float span = along[next] - along[k];
        float t = span > 1e-6f ? (wanted - along[k]) / span : 0;
        points.push_back(fine[k] + (fine[next] - fine[k]) * t);
    }
    return points;
}

MeshData ropeTube(const std::vector<Vec3>& points, float radius, int sides) {
    MeshData mesh;
    if (points.size() < 2) return mesh;
    sides = std::max(sides, 3);
    float travelled = 0;
    for (size_t i = 0; i < points.size(); i++) {
        Vec3 along = i + 1 < points.size() ? points[i + 1] - points[i] : points[i] - points[i - 1];
        if (i > 0 && i + 1 < points.size()) along = points[i + 1] - points[i - 1];
        along = safeNormalize(along, Vec3{0, -1, 0});
        if (i > 0) travelled += simd::length(points[i] - points[i - 1]);
        Vec3 u, v;
        across(along, u, v);
        for (int s = 0; s <= sides; s++) {
            float angle = 2 * kPi * float(s) / float(sides);
            Vec3 normal = u * std::cos(angle) + v * std::sin(angle);
            addVertex(mesh, points[i] + normal * radius, normal, Vec2{float(s) / float(sides), travelled / std::max(radius * 6, 0.01f)});
        }
    }
    uint32_t ring = uint32_t(sides + 1);
    for (uint32_t i = 0; i + 1 < uint32_t(points.size()); i++) {
        for (uint32_t s = 0; s < uint32_t(sides); s++) {
            uint32_t a = i * ring + s, b = a + 1, c = a + ring, d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

MeshData chainRings(const std::vector<Vec3>& points, float thickness) {
    MeshData mesh;
    const int around = 12, tube = 6;
    for (size_t i = 0; i + 1 < points.size(); i++) {
        Vec3 middle = (points[i] + points[i + 1]) * 0.5f;
        Vec3 along = points[i + 1] - points[i];
        float length = simd::length(along);
        along = safeNormalize(along, Vec3{0, -1, 0});
        Vec3 u, v;
        across(along, u, v);
        if (i % 2) std::swap(u, v);                         // each a quarter round from the last
        // An oval ring in the plane of `along` and `u`, reaching a little past each joint so they hook.
        float halfLong = length * 0.62f, halfWide = std::max(length * 0.3f, thickness * 2), r = thickness * 0.5f;
        uint32_t first = uint32_t(mesh.positions.size());
        for (int a = 0; a <= around; a++) {
            float angle = 2 * kPi * float(a) / float(around);
            Vec3 center = middle + along * (std::cos(angle) * halfLong) + u * (std::sin(angle) * halfWide);
            Vec3 outward = safeNormalize(along * (std::cos(angle) / halfLong) + u * (std::sin(angle) / halfWide), u);
            for (int t = 0; t <= tube; t++) {
                float turn = 2 * kPi * float(t) / float(tube);
                Vec3 normal = outward * std::cos(turn) + v * std::sin(turn);
                addVertex(mesh, center + normal * r, normal, Vec2{float(a) / float(around), float(t) / float(tube)});
            }
        }
        for (uint32_t a = 0; a < uint32_t(around); a++) {
            for (uint32_t t = 0; t < uint32_t(tube); t++) {
                uint32_t p = first + a * uint32_t(tube + 1) + t, q = p + 1, r2 = p + uint32_t(tube + 1), s = r2 + 1;
                mesh.indices.insert(mesh.indices.end(), {p, r2, q, q, r2, s});
            }
        }
    }
    return mesh;
}

std::vector<Vec3> Rope::points(const World& world) const {
    std::vector<Vec3> out;
    for (size_t i = 0; i < parts.size(); i++) {
        const Transform* place = world.scene.get<Transform>(parts[i]);
        if (!place) continue;
        Vec3 up = rotate(place->rotation, Vec3{0, linkLength * 0.5f, 0});
        if (i == 0) out.push_back(place->position + up);
        out.push_back(place->position - up);
    }
    return out;
}

void Rope::start(World& world, Entity self) {
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform || !parts.empty()) return;
    Vec3 from = transform->worldPosition();
    tiedTo = to.empty() ? Entity{} : world.find(to);
    const Transform* end = world.scene.get<Transform>(tiedTo);
    float ropeLength = length > 0 ? length : end ? simd::length(end->worldPosition() - from) : 3.0f;
    Vec3 target = end ? end->worldPosition() : from + Vec3{0, -ropeLength, 0};
    int count = std::clamp(links, 2, 200);
    std::vector<Vec3> line = hangingPoints(from, target, ropeLength, count);
    linkLength = ropeLength / float(count);
    std::string prefix = "Rope " + std::to_string(self.index) + " link ";
    bool startMoves = world.scene.has<RigidBody>(self);
    const Name* ownName = world.scene.get<Name>(self);
    for (int i = 0; i < count; i++) {
        Transform place;
        place.position = (line[size_t(i)] + line[size_t(i + 1)]) * 0.5f;
        place.rotation = upTo(line[size_t(i)] - line[size_t(i + 1)]);       // +y toward the start
        Entity part = world.scene.create(prefix + std::to_string(i));
        world.scene.add(part, place);
        world.scene.add(part, Name{prefix + std::to_string(i)});
        RigidBody body;
        body.mass = std::max(mass / float(count), 0.01f);
        body.rotates = true;
        body.angularDamping = isChain ? 0.6f : 0.4f;
        body.linearDamping = 0.1f;
        body.friction = 0.7f;
        body.restitution = 0;
        world.scene.add(part, body);
        Collider collider;
        collider.shape = ColliderShape::Capsule;
        collider.radius = std::max(thickness * 0.5f, 0.005f);
        collider.halfHeight = std::max(linkLength * 0.5f - collider.radius, 0.001f);
        collider.layer = kRopeLayer;
        collider.mask = ~kRopeLayer;
        world.scene.add(part, collider);
        Joint joint;
        joint.kind = "ball";
        joint.anchor = Vec3{0, linkLength * 0.5f, 0};
        joint.swingLimit = bend > 0 ? bend : isChain ? 75.0f : 0.0f;
        if (i > 0) joint.connectedTo = prefix + std::to_string(i - 1);
        else if (startMoves && ownName) joint.connectedTo = ownName->value;
        world.scene.add(part, joint);
        parts.push_back(part);
    }
    // What is drawn: one mesh along the links, of the rope's own material if it has one.
    look = world.scene.create(prefix + "look");
    world.scene.add(look, Transform{});
    MeshRenderer renderer;
    if (const MeshRenderer* own = world.scene.get<MeshRenderer>(self)) renderer.material = own->material;
    world.scene.add(look, renderer);
    world.scene.updateTransforms();
}

void Rope::fixedUpdate(World& world, Entity self, float dt) {
    if (parts.empty()) return;
    float pull = 0.2f / std::max(dt, 1e-5f);
    // A rope does not stretch, however heavy what hangs from it: no link (and nothing tied to
    // its end) gets further from where it starts than the rope between them is long. Joints
    // alone, between light links and a heavy load, would give.
    auto tether = [&](RigidBody& body, Vec3 anchor, float most) {
        Vec3 out = body.position - anchor;
        float distance = simd::length(out);
        if (distance <= most || distance < 1e-6f) return;
        Vec3 n = out / distance;
        float going = simd::dot(body.velocity, n);
        if (going > 0) body.velocity -= n * going;
        body.velocity -= n * ((distance - most) * pull);
    };
    const Transform* own = world.scene.get<Transform>(self);
    const RigidBody* ownBody = world.scene.get<RigidBody>(self);
    bool startMoves = ownBody && ownBody->type == BodyType::Dynamic;
    const Transform* held = world.scene.get<Transform>(tiedTo);
    RigidBody* other = world.scene.get<RigidBody>(tiedTo);
    bool otherMoves = other && other->type == BodyType::Dynamic && other->isInitialized;
    if (own && !startMoves) {
        Vec3 anchor = own->worldPosition();
        for (size_t i = 0; i < parts.size(); i++) {
            RigidBody* link = world.scene.get<RigidBody>(parts[i]);
            if (link && link->isInitialized) tether(*link, anchor, (float(i) + 0.5f) * linkLength);
        }
        if (otherMoves) tether(*other, anchor, float(parts.size()) * linkLength);
    }
    // Tied at its end to something that stays where it is: the same from that end.
    if (held && !otherMoves && world.scene.isAlive(tiedTo)) {
        Vec3 anchor = held->worldPosition();
        for (size_t i = 0; i < parts.size(); i++) {
            RigidBody* link = world.scene.get<RigidBody>(parts[i]);
            if (link && link->isInitialized) tether(*link, anchor, (float(parts.size() - i) - 0.5f) * linkLength);
        }
    }
    // The end held to what it is tied to, as a joint holds: each pulled toward the other, the
    // lighter the more.
    if (!world.scene.isAlive(tiedTo)) return;
    RigidBody* link = world.scene.get<RigidBody>(parts.back());
    if (!link || !held || !link->isInitialized) return;
    Vec3 arm = rotate(link->rotation, Vec3{0, -linkLength * 0.5f, 0});
    Vec3 endPoint = link->position + arm;
    Vec3 target = otherMoves ? other->position : held->worldPosition();
    Vec3 apart = target - endPoint;
    Vec3 relative = (otherMoves ? other->velocity : Vec3{0, 0, 0}) - (link->velocity + simd::cross(link->angularVelocity, arm));
    float inverseLink = 1.0f / std::max(link->mass, 1e-4f), inverseOther = otherMoves ? 1.0f / std::max(other->mass, 1e-4f) : 0.0f;
    Vec3 impulse = (apart * (0.2f / std::max(dt, 1e-5f)) + relative) / (inverseLink + inverseOther);
    link->velocity += impulse * inverseLink;
    if (otherMoves) other->velocity -= impulse * inverseOther;
}

void Rope::lateUpdate(World& world, Entity, float) {
    if (parts.empty() || !world.scene.isAlive(look)) return;
    std::vector<Vec3> along = points(world);
    world.showMesh(look, isChain ? chainRings(along, thickness) : ropeTube(along, thickness * 0.5f));
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Rope, MOLTEN_FIELD(to).picks("entity").help("The entity its end is tied to. Empty: it hangs free"),
                 MOLTEN_FIELD(length).range(0, 100).help("Meters. 0: as far as what it is tied to (or 3 hanging free)"),
                 MOLTEN_FIELD(links).range(2, 100).help("More: smoother, more to work out"), MOLTEN_FIELD(thickness).range(0.005f, 0.5f),
                 MOLTEN_FIELD(isChain).help("Drawn as rings, a little stiffer"), MOLTEN_FIELD(mass).range(0.01f, 200).help("Of the whole rope, kilograms"),
                 MOLTEN_FIELD(bend).range(0, 180).help("Degrees each link bends at most. 0: as a rope does"))
