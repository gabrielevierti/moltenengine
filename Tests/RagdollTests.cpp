// Ragdolls made from a skeleton.
#include "Test.hpp"

#include "Animation/Animator.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Ragdoll.hpp"
#include "Physics/Physics.hpp"

#include <cmath>

using namespace molten;

namespace {
/// Hips a meter up; a spine, a chest and a head above; one leg below, to the knee and the foot.
GltfModel figure() {
    GltfModel model;
    auto add = [&](const char* name, int parent, Vec3 at) {
        GltfNode node;
        node.name = name;
        node.parent = parent;
        node.translation = at;
        model.nodes.push_back(node);
        int index = int(model.nodes.size()) - 1;
        if (parent >= 0) model.nodes[size_t(parent)].children.push_back(index);
        return index;
    };
    int hips = add("Hips", -1, Vec3{0, 1, 0});
    int spine = add("Spine", hips, Vec3{0, 0.25f, 0});
    int chest = add("Chest", spine, Vec3{0, 0.25f, 0});
    add("Head", chest, Vec3{0, 0.3f, 0});
    int leg = add("Leg", hips, Vec3{0.15f, -0.05f, 0});
    int knee = add("Knee", leg, Vec3{0, -0.45f, 0});
    add("Foot", knee, Vec3{0, -0.45f, 0});
    return model;
}
}  // namespace

TEST(a_ragdoll_is_made_from_the_bones_and_moves_them) {
    GltfModel model = figure();
    GltfPose rest = model.restPose();
    std::vector<kit::RagdollBone> bones = kit::ragdollBones(model, rest, {}, 0.25f);
    // Every bone but none: the head and the foot go on the way their chains point.
    CHECK(bones.size() == 7);
    CHECK(model.nodes[size_t(bones[0].node)].name == "Hips" && bones[0].parent == -1);
    for (size_t i = 1; i < bones.size(); i++) CHECK(bones[i].parent >= 0 && bones[i].parent < int(i));
    // The hips go up the spine (more is above them than below).
    CHECK(bones[0].to.y > bones[0].from.y);
    // Only those named.
    std::vector<kit::RagdollBone> few = kit::ragdollBones(model, rest, {"Hips", "Chest", "Knee"}, 0.25f);
    CHECK(few.size() == 3 && few[1].parent == 0 && few[2].parent == 0);

    // The bodies where the bones are: the pose comes back as it was.
    std::vector<Mat4> bodies, offsets;
    std::vector<Mat4> global = model.globalMatrices(rest);
    for (const kit::RagdollBone& bone : bones) {
        Mat4 body = translation((bone.from + bone.to) * 0.5f);
        bodies.push_back(body);
        offsets.push_back(simd::inverse(body) * global[size_t(bone.node)]);
    }
    GltfPose again = rest;
    kit::poseFromBodies(model, bones, bodies, offsets, identity(), again);
    for (size_t i = 0; i < model.nodes.size(); i++) CHECK(simd::length(again.translations[i] - rest.translations[i]) < 1e-4f);
    // The chest's body turned a quarter round: the chest turns, and the head with it.
    size_t chestBody = 0;
    for (size_t i = 0; i < bones.size(); i++) if (model.nodes[size_t(bones[i].node)].name == "Chest") chestBody = i;
    Vec3 pivot = xyz(global[2].columns[3]);
    bodies[chestBody] = translation(pivot) * rotation(Quat(kPi / 2, Vec3{0, 0, 1})) * translation(-pivot) * bodies[chestBody];
    for (size_t i = 0; i < bones.size(); i++) if (model.nodes[size_t(bones[i].node)].name == "Head") bodies[i] = translation(pivot) * rotation(Quat(kPi / 2, Vec3{0, 0, 1})) * translation(-pivot) * bodies[i];
    GltfPose turned = rest;
    kit::poseFromBodies(model, bones, bodies, offsets, identity(), turned);
    std::vector<Mat4> after = model.globalMatrices(turned);
    Vec3 head = xyz(after[3].columns[3]);
    CHECK(std::abs(head.x + 0.3f) < 1e-3f && std::abs(head.y - pivot.y) < 1e-3f);     // to the side, as the chest now points
}

TEST(a_ragdoll_falls_in_one_piece_and_stands_up) {
    kit::registerKit();
    World world;
    Entity floor = world.scene.create("Floor");
    world.scene.add(floor, Transform{});
    Collider ground;
    ground.halfExtents = Vec3{10, 0.5f, 10};
    Transform below;
    below.position = Vec3{0, -0.5f, 0};
    world.scene.add(floor, below);
    world.scene.add(floor, ground);
    Entity man = world.scene.create("Man");
    world.scene.add(man, Transform{});
    Animator animator;
    GltfModel model = figure();
    animator.loaded = std::make_shared<const GltfModel>(model);
    animator.pose = model.restPose();
    world.scene.add(man, animator);
    kit::Ragdoll ragdoll;
    world.scene.add(man, ragdoll);
    world.advance(1.0 / 60.0);
    world.send("limp", man);
    world.advance(1.0 / 60.0);
    kit::Ragdoll* limp = world.scene.get<kit::Ragdoll>(man);
    CHECK(limp->isLimp && limp->bodies.size() == 7 && world.scene.get<Animator>(man)->isHeldByRagdoll);
    for (int i = 0; i < 240; i++) world.advance(1.0 / 60.0);
    // Down on the floor, and still joined: each body's end near where its parent's is.
    for (Entity part : limp->bodies) CHECK(world.scene.get<Transform>(part)->position.y > -0.05f && world.scene.get<Transform>(part)->position.y < 0.6f);
    std::vector<Mat4> global = limp->made.empty() ? std::vector<Mat4>{} : model.globalMatrices(world.scene.get<Animator>(man)->pose);
    for (const kit::RagdollBone& bone : limp->made) {
        const GltfNode& node = model.nodes[size_t(bone.node)];
        if (node.parent < 0) continue;
        float apart = simd::length(xyz(global[size_t(bone.node)].columns[3]) - xyz(global[size_t(node.parent)].columns[3]));
        CHECK(std::abs(apart - simd::length(node.translation)) < 0.08f);
    }
    world.send("stand up", man);
    world.advance(1.0 / 60.0);
    world.scene.flushDestroyed();
    CHECK(!limp->isLimp && limp->bodies.empty() && !world.scene.get<Animator>(man)->isHeldByRagdoll);
    CHECK(!world.find("Ragdoll " + std::to_string(man.index) + " Hips").isValid());
}
