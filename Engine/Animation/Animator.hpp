// Animator.hpp
// Plays the animations of a glTF model on the entity that shows it: a character that
// walks, a door that opens, a flag. Each frame the bones are put where the animation has
// them at that moment and the model's vertices are moved with them.
//
//     entity "Robot"
//       mesh "Models/robot.glb" ...
//       component "Animator"
//         field "clip" "Walk"
#pragma once

#include "Animation/AnimatorController.hpp"
#include "Assets/Gltf.hpp"
#include "Core/World.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

struct Animator {
    std::string model;                  // the .gltf or .glb file; empty uses the file the entity's mesh came from
    std::string clip;                   // which of its animations; empty plays the first
    float speed = 1.0f;
    bool loops = true;
    bool isPlaying = true;
    float blendTime = 0.2f;             // seconds to ease from one clip into the next
    /// A .animator file (see AnimatorController.hpp): states, parameters and transitions. With
    /// one, the animator plays its states instead of `clip`, as the parameters say.
    std::string controller;

    void update(World& world, Entity self, float dt);

    // ---- with a controller: what the game tells it, and what it says back
    void setFloat(const std::string& name, float value) { parameters[name] = value; }
    void setBool(const std::string& name, bool value) { parameters[name] = value ? 1.0f : 0.0f; }
    /// Set until a transition that waits for it is taken.
    void setTrigger(const std::string& name) { parameters[name] = 1.0f; }
    float parameter(const std::string& name) const {
        auto found = parameters.find(name);
        return found == parameters.end() ? 0.0f : found->second;
    }
    /// The state playing now, or "" without a controller.
    std::string currentState() const;
    /// The events of the states passed this frame (sounds are played; the rest are for the game).
    std::vector<std::string> firedEvents;

    /// Starts another clip, easing into it from where the model is now.
    void play(const std::string& name);
    /// True once a clip that does not loop has reached its end.
    bool isFinished() const { return hasEnded; }

    // ---- state
    float time = 0;                     // seconds into the clip
    std::shared_ptr<const GltfModel> loaded;
    std::string loadedFrom, playing;
    int animation = -1;
    GltfPose pose, blendFrom;
    float blend = 1;                    // 0 just changed clip, 1 fully in the new one
    bool hasEnded = false;
    std::shared_ptr<const AnimatorController> graph;
    std::string graphFrom;
    std::unordered_map<std::string, float> parameters;
    int state = -1;
    float progress = 0;                 // 0 to 1 through the state; past 1 for a state that has looped
    float stateBlendTime = 0.2f;        // of the transition being eased through
    void updateStates(World& world, float dt);
    void reachWithLimbs(World& world, Entity self);
};

/// Two-bone inverse kinematics: puts the end of a limb (a foot, a hand) on a target, bending
/// the joint between (a knee, an elbow) toward a pole. On the entity with the Animator; it acts
/// on the pose the Animator has made, every frame.
struct TwoBoneIK {
    std::string bone;                   // the end of the limb: the foot's or the hand's node in the model
    std::string target;                 // the entity it reaches for
    std::string pole;                   // an entity the middle joint bends toward; empty: as the animation bends it
    float weight = 1.0f;                // 0 leaves the animation as it is, 1 reaches all the way
};

/// Bends `pose` so that the node `end` reaches `target` (in the model's space), with its parent
/// and grandparent as the two bones. `pole` (the model's space) is where the middle joint points;
/// none: the way it already bends. False if the node has no parent and grandparent.
bool solveTwoBoneIK(const GltfModel& model, GltfPose& pose, int end, Vec3 target, const Vec3* pole, float weight);

/// The model of a file, read once and shared by everything that plays it.
std::shared_ptr<const GltfModel> sharedGltfModel(const std::string& path);

}  // namespace molten
