#include "Kit/Ragdoll.hpp"

#include "Animation/Animator.hpp"
#include "Game/Component.hpp"
#include "Kit/Character.hpp"
#include "Physics/Physics.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>

namespace molten::kit {

void linkRagdoll() {
    ComponentRegistry::instance().setCategory("Ragdoll", "Physics");
}

namespace {

Vec3 placeOf(const Mat4& m) { return xyz(m.columns[3]); }

/// The turn that takes up (0, 1, 0) to `direction`.
Quat upTo(Vec3 direction) {
    Vec3 up{0, 1, 0};
    Vec3 d = safeNormalize(direction, up);
    float cosine = simd::dot(up, d);
    if (cosine > 0.9999f) return Quat{};
    if (cosine < -0.9999f) return Quat(kPi, Vec3{1, 0, 0});
    return Quat(std::acos(std::clamp(cosine, -1.0f, 1.0f)), simd::normalize(simd::cross(up, d)));
}

/// How many nodes are below a node: the child that carries the most is the way the body goes on.
int countBelow(const GltfModel& model, int node) {
    int count = 0;
    for (int child : model.nodes[size_t(node)].children) count += 1 + countBelow(model, child);
    return count;
}

std::vector<std::string> splitNames(const std::string& text) {
    std::vector<std::string> names;
    std::string name;
    std::istringstream stream(text);
    while (std::getline(stream, name, ',')) {
        size_t a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
        if (a != std::string::npos) names.push_back(name.substr(a, b - a + 1));
    }
    return names;
}

}  // namespace

std::vector<RagdollBone> ragdollBones(const GltfModel& model, const GltfPose& pose, const std::vector<std::string>& names, float thickness, float shortest) {
    std::vector<RagdollBone> bones;
    if (model.nodes.empty()) return bones;
    std::vector<Mat4> global = model.globalMatrices(pose);
    // The skeleton: the skin's joints, or every node of a model without one.
    std::set<int> skeleton;
    if (!model.skins.empty()) skeleton.insert(model.skins[0].joints.begin(), model.skins[0].joints.end());
    else for (size_t i = 0; i < model.nodes.size(); i++) skeleton.insert(int(i));
    // The model's height, to say what is too short to bother with.
    float low = 1e30f, high = -1e30f;
    for (int node : skeleton) low = std::min(low, placeOf(global[size_t(node)]).y), high = std::max(high, placeOf(global[size_t(node)]).y);
    float height = std::max(high - low, 0.01f);
    std::set<std::string> wanted(names.begin(), names.end());

    // Down the tree from its roots, so that a body's parent is always made before it.
    std::vector<int> bodyOf(model.nodes.size(), -1);
    std::function<void(int, int)> visit = [&](int node, int parentBody) {
        const GltfNode& n = model.nodes[size_t(node)];
        int mine = parentBody;
        bool isBone = skeleton.count(node) > 0;
        bool chosen = isBone && (wanted.empty() || wanted.count(n.name));
        if (chosen) {
            // Along to the child the body goes on in (the one with most below it); a bone at the end
            // of a chain goes on as far as its parent's did, the way it points.
            int next = -1, most = -1;
            for (int child : n.children) {
                if (!skeleton.count(child)) continue;
                int below = countBelow(model, child);
                if (below > most) most = below, next = child;
            }
            Vec3 from = placeOf(global[size_t(node)]);
            Vec3 to = from;
            if (next >= 0) to = placeOf(global[size_t(next)]);
            else if (n.parent >= 0) {
                Vec3 along = from - placeOf(global[size_t(n.parent)]);
                to = from + along * 0.6f;
            }
            float length = simd::length(to - from);
            bool isLongEnough = length >= shortest * height || (!wanted.empty() && length > 1e-4f);
            if (isLongEnough) {
                RagdollBone bone;
                bone.node = node;
                bone.parent = parentBody;
                bone.from = from;
                bone.to = to;
                bone.radius = std::max(length * thickness, 0.005f);
                bones.push_back(bone);
                mine = int(bones.size()) - 1;
            }
        }
        bodyOf[size_t(node)] = mine;
        for (int child : n.children) visit(child, mine);
    };
    for (size_t i = 0; i < model.nodes.size(); i++) {
        if (model.nodes[i].parent < 0) visit(int(i), -1);
    }
    return bones;
}

void poseFromBodies(const GltfModel& model, const std::vector<RagdollBone>& bones, const std::vector<Mat4>& bodies, const std::vector<Mat4>& offsets,
                    const Mat4& entity, GltfPose& pose) {
    if (pose.translations.size() != model.nodes.size()) return;
    std::vector<int> bodyOfNode(model.nodes.size(), -1);
    for (size_t i = 0; i < bones.size() && i < bodies.size() && i < offsets.size(); i++) bodyOfNode[size_t(bones[i].node)] = int(i);
    Mat4 toModel = simd::inverse(entity);
    std::vector<Mat4> global(model.nodes.size(), identity());
    std::function<void(int, const Mat4&)> visit = [&](int node, const Mat4& parent) {
        Mat4 local = compose(pose.translations[size_t(node)], pose.rotations[size_t(node)], pose.scales[size_t(node)]);
        int body = bodyOfNode[size_t(node)];
        if (body >= 0) {
            global[size_t(node)] = toModel * bodies[size_t(body)] * offsets[size_t(body)];
            // Back into the bone's own place below its parent; its size kept as it was.
            Vec3 position, size;
            Quat turned;
            decompose(simd::inverse(parent) * global[size_t(node)], position, turned, size);
            pose.translations[size_t(node)] = position;
            pose.rotations[size_t(node)] = simd::normalize(turned);
        } else {
            global[size_t(node)] = parent * local;
        }
        for (int child : model.nodes[size_t(node)].children) visit(child, global[size_t(node)]);
    };
    for (size_t i = 0; i < model.nodes.size(); i++) {
        if (model.nodes[i].parent < 0) visit(int(i), identity());
    }
}

void Ragdoll::limp(World& world, Entity self, Vec3 push) {
    if (!bodies.empty()) return;
    isLimp = wasLimp = true;
    Animator* animator = world.scene.get<Animator>(self);
    const Transform* transform = world.scene.get<Transform>(self);
    if (!animator || !animator->loaded || !transform) return;
    const GltfModel& model = *animator->loaded;
    heldPose = animator->pose.translations.size() == model.nodes.size() ? animator->pose : model.restPose();
    made = ragdollBones(model, heldPose, splitNames(bones), thickness);
    if (made.empty()) return;
    animator->isHeldByRagdoll = true;
    // It goes on moving as it was: walking, running, falling.
    Vec3 velocity = push;
    if (const CharacterMover* mover = world.scene.get<CharacterMover>(self)) velocity += mover->velocity;
    if (const RigidBody* body = world.scene.get<RigidBody>(self)) velocity += body->velocity;
    // The character's own collider lets its parts through.
    if (Collider* own = world.scene.get<Collider>(self)) {
        keptMask = own->mask;
        own->mask &= ~kRagdollLayer;
    }
    float totalLength = 0;
    for (const RagdollBone& bone : made) totalLength += simd::length(bone.to - bone.from);
    std::vector<Mat4> global = model.globalMatrices(heldPose);
    // A copy: making the parts adds transforms to the scene, which moves the one `transform` points at.
    const Mat4 entity = transform->world;
    Vec3 scale{simd::length(xyz(entity.columns[0])), simd::length(xyz(entity.columns[1])), simd::length(xyz(entity.columns[2]))};
    float size = std::max({scale.x, scale.y, scale.z});
    std::string prefix = "Ragdoll " + std::to_string(self.index) + " ";
    bodies.clear();
    offsets.clear();
    for (size_t i = 0; i < made.size(); i++) {
        const RagdollBone& bone = made[i];
        Vec3 from = xyz(entity * Vec4{bone.from.x, bone.from.y, bone.from.z, 1});
        Vec3 to = xyz(entity * Vec4{bone.to.x, bone.to.y, bone.to.z, 1});
        float length = std::max(simd::length(to - from), 0.01f);
        float radius = bone.radius * size;
        Transform place;
        place.position = (from + to) * 0.5f;
        place.rotation = upTo(to - from);
        Entity part = world.scene.create(prefix + model.nodes[size_t(bone.node)].name);
        world.scene.add(part, place);
        world.scene.add(part, Name{prefix + model.nodes[size_t(bone.node)].name});
        RigidBody body;
        body.mass = std::max(mass * (simd::length(bone.to - bone.from) / std::max(totalLength, 1e-4f)), 0.2f);
        body.rotates = true;
        body.friction = friction;
        body.restitution = 0.05f;
        body.velocity = velocity;
        body.angularDamping = 0.4f;
        world.scene.add(part, body);
        Collider collider;
        collider.shape = ColliderShape::Capsule;
        collider.radius = radius;
        collider.halfHeight = std::max(length * 0.5f - radius, 0.005f);
        collider.layer = kRagdollLayer;
        collider.mask = ~kRagdollLayer;
        world.scene.add(part, collider);
        if (bone.parent >= 0) {
            Joint joint;
            joint.kind = "ball";
            joint.connectedTo = prefix + model.nodes[size_t(made[size_t(bone.parent)].node)].name;
            joint.anchor = Vec3{0, -length * 0.5f, 0};          // where it meets the one above
            joint.swingLimit = bend;
            world.scene.add(part, joint);
        }
        world.scene.updateTransforms();
        Mat4 bodyWorld = compose(place.position, place.rotation, Vec3{1, 1, 1});
        offsets.push_back(simd::inverse(bodyWorld) * entity * global[size_t(bone.node)]);
        bodies.push_back(part);
    }
}

void Ragdoll::standUp(World& world, Entity self) {
    isLimp = wasLimp = false;
    for (Entity part : bodies) {
        if (world.scene.isAlive(part)) world.scene.destroy(part);
    }
    bodies.clear();
    offsets.clear();
    made.clear();
    if (Animator* animator = world.scene.get<Animator>(self)) animator->isHeldByRagdoll = false;
    if (Collider* own = world.scene.get<Collider>(self); own && keptMask != 0) own->mask = keptMask;
    keptMask = 0;
}

void Ragdoll::onMessage(World& world, Entity self, const Message& message) {
    if (message.name == "limp") limp(world, self);
    else if (message.name == "stand up") standUp(world, self);
}

void Ragdoll::lateUpdate(World& world, Entity self, float) {
    if (isLimp != wasLimp) isLimp ? limp(world, self) : standUp(world, self);
    if (!isLimp || bodies.empty()) return;
    Animator* animator = world.scene.get<Animator>(self);
    const Transform* transform = world.scene.get<Transform>(self);
    if (!animator || !animator->loaded || !transform) return;
    std::vector<Mat4> worlds;
    for (Entity part : bodies) {
        const Transform* place = world.scene.get<Transform>(part);
        worlds.push_back(place ? compose(place->worldPosition(), place->rotation, Vec3{1, 1, 1}) : identity());
    }
    GltfPose pose = heldPose;
    poseFromBodies(*animator->loaded, made, worlds, offsets, transform->world, pose);
    animator->pose = pose;
    world.showPose(self, animator->loaded, pose);
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Ragdoll, MOLTEN_FIELD(isLimp).help("Limp: its bones follow bodies that fall, instead of the animation"),
                 MOLTEN_FIELD(bones).picks("bones").help("The bones that get a body, by name, with commas. Empty: worked out from the skeleton"),
                 MOLTEN_FIELD(mass).range(1, 500).help("Of the whole body, in kilograms"),
                 MOLTEN_FIELD(thickness).range(0.05f, 0.6f).help("Each body's radius, as part of its bone's length"),
                 MOLTEN_FIELD(bend).range(5, 180).help("Degrees each joint bends at most"), MOLTEN_FIELD(friction).range(0, 2))
