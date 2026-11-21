// Ragdoll.hpp
// A character that goes limp: hit, fallen, knocked out. A Ragdoll beside the Animator of a
// skinned model makes, when it goes limp, a body for each of the skeleton's main bones (a
// capsule along the bone), joined to the body of the bone above it by a joint that bends only
// so far; then the bones follow the bodies instead of the animation. Standing up takes the
// bodies away and the animation goes on.
//
//   ragdoll->limp(world, self, push);      // or tick Is Limp, or the message "limp"
//   ragdoll->standUp(world, self);         // or the message "stand up"
//
// The bones it makes bodies for: those named in `bones` ("Hips, Spine, Head, LeftArm..."), or,
// left empty, every bone of the skeleton that has a bone after it and is long enough.
#pragma once

#include "Assets/Gltf.hpp"
#include "Core/World.hpp"

#include <string>
#include <vector>

namespace molten::kit {

/// A bone a ragdoll makes a body for, in the model's space.
struct RagdollBone {
    int node = -1;
    int parent = -1;                    // the body above it, in the list; -1 for the first
    Vec3 from{0, 0, 0}, to{0, 0, 0};    // where the bone starts, and where the next one does
    float radius = 0.05f;
};

/// The bones to make bodies for, in an order where each body's parent comes before it.
/// `names` empty: automatic, with bones shorter than `shortest` of the model's height left out.
std::vector<RagdollBone> ragdollBones(const GltfModel& model, const GltfPose& pose, const std::vector<std::string>& names, float thickness,
                                      float shortest = 0.04f);

/// The pose the bodies give: each bone with a body where its body is (`bodies`: the bodies'
/// world matrices, `offsets`: from each body to its bone, as it was when they were made), the
/// others as they were in `pose`, below their parents. `entity` is the entity's world matrix.
void poseFromBodies(const GltfModel& model, const std::vector<RagdollBone>& bones, const std::vector<Mat4>& bodies, const std::vector<Mat4>& offsets,
                    const Mat4& entity, GltfPose& pose);

struct Ragdoll {
    bool isLimp = false;
    std::string bones;                  // comma separated; empty: worked out from the skeleton
    float mass = 70;                    // of the whole body, shared by the bones as their lengths
    float thickness = 0.28f;            // each body's radius, as part of its bone's length
    float bend = 70;                    // degrees each joint bends at most from how it was
    float friction = 0.8f;

    void limp(World& world, Entity self, Vec3 push = Vec3{0, 0, 0});
    void standUp(World& world, Entity self);
    void lateUpdate(World& world, Entity self, float dt);
    void onMessage(World& world, Entity self, const Message& message);

    // ---- while limp
    std::vector<RagdollBone> made;
    std::vector<Entity> bodies;
    std::vector<Mat4> offsets;
    GltfPose heldPose;                  // the pose it went limp in: the bones without a body keep theirs
    bool wasLimp = false;
    uint32_t keptMask = 0;              // the character's own collider's, while its parts are about
};

/// The collision layer of ragdolls' parts: they do not collide with one another.
inline constexpr uint32_t kRagdollLayer = 1u << 30;

void linkRagdoll();

}  // namespace molten::kit
