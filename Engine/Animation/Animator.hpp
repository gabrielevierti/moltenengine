// Animator.hpp
// Plays animations on an entity: those of a glTF model (a character that walks, a flag), whose
// bones are put where the animation has them and the model's vertices moved with them; and
// clips made in the editor (.anim, see AnimationClip.hpp: a door that opens, a light that
// flickers), which move the entity and those under it. One clip by itself (`clip`), or an
// Animator Controller (`controller`) whose states play clips of either kind as the game's
// parameters say: from code (setFloat, setBool, setTrigger, crossFade) or from a visual script
// (the Animation nodes).
//
//     entity "Robot"
//       mesh "Models/robot.glb" ...
//       component "Animator"
//         field "clip" "Walk"
#pragma once

#include "Animation/AnimationClip.hpp"
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
    /// Its own animations: clips made for this entity in the Timeline window (.anim files, by commas).
    /// A model's animations are its too, by their names.
    std::string animations;
    std::string clip;                   // the one it plays from the start; empty: the model's first
    float speed = 1.0f;
    bool loops = true;
    bool isPlaying = true;
    float blendTime = 0.2f;             // seconds to ease from one clip into the next
    /// A .animator file (see AnimatorController.hpp): states, parameters and transitions. With
    /// one, the animator plays its states instead of `clip`, as the parameters say.
    std::string controller;
    /// Root motion: the entity goes where the animation walks it (a step that covers the ground it
    /// should, a dodge, a climb), instead of the model walking away from its entity. The root
    /// bone's movement across the ground moves the entity (with its Character Controller if it
    /// has one, so that it still stops at walls); the bone itself stays over it.
    bool rootMotion = false;
    std::string rootBone;               // the bone that carries the body: empty for the skeleton's first
    /// Set by a Ragdoll while it is limp: the bones follow its bodies, and the animation waits.
    bool isHeldByRagdoll = false;

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

    /// Plays an animation by its name: one of its own ("Open"), one of its model's ("Walk"), or,
    /// with a controller, a state of it. Eases into it from where things are now.
    void play(const std::string& name);
    /// Stops, back at the start of what it plays; pause() stops where it is, resume() goes on.
    void stop();
    void pause() { isPlaying = false; }
    void resume() { isPlaying = true; }
    /// True while it plays that animation (or is in that state).
    bool isPlayingAnimation(const std::string& name) const;
    /// Its own animations, as names: "Open", "Close".
    std::vector<std::string> animationNames() const;
    /// The file or model animation a name stands for.
    std::string resolve(const std::string& name) const;
    /// With a controller: goes to a state now, easing into it over `seconds`, whatever the
    /// transitions say (Unity's CrossFade; 0 is Play).
    void crossFade(const std::string& state, float seconds = 0.2f);
    void playState(const std::string& state) { crossFade(state, 0); }
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
    /// What one set of states plays: the base's, or a layer's.
    struct Playing {
        int state = -1;
        float progress = 0;
        float blend = 1;
        float stateBlendTime = 0.2f;
        GltfPose pose, blendFrom;
        ClipPose props, propsFrom;      // what its clips made in the editor put on the properties
    };
    std::vector<Playing> layerPlaying;  // one for each layer of the controller
    std::vector<std::vector<float>> layerMasks;   // for each layer, how much of it each node takes (0 or 1)
    void runStates(World& world, float dt, const std::vector<AnimatorController::State>& states,
                   const std::vector<AnimatorController::Transition>& transitions, Playing& run, bool isBase);
    void putLayers(World& world, float dt);
    void moveWithRoot(World& world, Entity self);
    ClipPose props, propsFrom;          // the properties the clips made in the editor animate, now
    Entity owner{};
    std::string pendingState;           // asked for by crossFade
    float pendingBlend = 0;
    void playClipFile(World& world, Entity self, float dt);
    void fireClipEvents(World& world, Entity self, const AnimationClip& file, float after, float upTo);
    /// A sound is played; any other name is the game's: in firedEvents, and sent to the entity as a message.
    void fireEvent(World& world, Entity self, const std::string& name);
    Vec3 lastRoot{0, 0, 0};
    float lastRootTime = 0;
    int lastRootAnimation = -1;
    bool hasLastRoot = false;
    Vec3 lastStep{0, 0, 0};
    Vec3 rootMoved{0, 0, 0};            // how far root motion moved the entity this frame, in the world
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
