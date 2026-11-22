// AnimatorController.hpp
// What a character's animations do, and when: states (each a clip, or a blend of clips by a
// number), the parameters the game sets (speed, grounded, jump), and the transitions between
// states when those parameters say so. An Animator with `controller` set follows one.
//
// A motion is a clip: one of the animations of the entity's model, by its name ("Walk"), or a
// clip made in the editor, by its file (Animations/DoorOpen.anim; see AnimationClip.hpp). The
// Animator window draws the states as boxes and the transitions as arrows, and keeps where each
// box is ("place 120 40" inside its state).
//
// A .animator file, as text:
//
//   molten-animator 1
//   parameter float speed 0
//   parameter bool grounded 1
//   parameter trigger jump
//
//   state Locomotion blend speed           a blend tree: Idle at 0, Walk at 2.2, Run at 5.5
//     motion Idle 0
//     motion Walk 2.2
//     motion Run 5.5
//   state Move blend moveX moveY           a 2D blend tree: each motion at a point of (moveX, moveY)
//     motion Idle 0 0
//     motion Forward 0 1
//     motion Back 0 -1
//     motion Left -1 0
//     motion Right 1 0
//   state Jump loops 0
//     motion Jump
//     event Audio/jump.wav 0               played as the state begins; any other name is for the game
//   state Fall
//     motion Fall
//
//   start Locomotion
//   transition Locomotion Jump when jump
//   transition Any Fall when grounded false blend 0.25
//   transition Jump Fall exit 0.9          when 90% of the clip has played
//   transition Fall Locomotion when grounded true blend 0.1
//
// Layers play over the states above, on part of the body: a character that walks with its legs
// and aims or waves with its arms. Each has its own states and transitions (the parameters are
// shared), the bones it acts on (a bone and everything below it; none: the whole body), and how
// much: a number, or the name of a float parameter. "additive" adds its movement to what is
// under it (a breath, a flinch) instead of taking its place.
//
//   layer Arms mask Spine weight 1
//     state Relaxed
//       motion Idle
//     state Aiming
//       motion Aim
//     start Relaxed
//     transition Relaxed Aiming when aiming
//   layer Breath additive weight breathing
//     state Breathe
//       motion Breathe
//
// Conditions: "when <bool>" or "when <bool> true|false", "when <trigger>", "when <float> > 1.5" or
// "< 1.5"; several are joined by "and". A trigger is used up by the transition it starts.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace molten {

struct AnimatorController {
    struct Parameter {
        enum class Kind { Float, Bool, Trigger } kind = Kind::Float;
        std::string name;
        float value = 0;                            // what it starts at; a bool is 0 or 1
    };
    struct Motion {
        std::string clip;
        float at = 0;                               // in a blend tree: the parameter's value where it is played alone
        float y = 0;                                // in a 2D blend tree: the second parameter's
    };
    struct Event {
        std::string name;                           // a sound file (.wav) is played; any other name is for the game to hear
        float at = 0;                               // 0 to 1 through the state
    };
    struct State {
        std::string name;
        std::vector<Motion> motions;
        std::string blendBy;                        // the float parameter of a blend tree; empty for one clip
        std::string blendByY;                       // the second, for a 2D blend tree (walking in eight directions)
        bool loops = true;
        float speed = 1;
        std::vector<Event> events;
        float x = 0, y = 0;                         // where the Animator window draws it ("place x y")
        std::string timeBy;                         // a float parameter (0 to 1) that says how far through it is, instead of time ("timeBy open")
    };
    struct Condition {
        enum class Test { IsTrue, IsFalse, Greater, Less, Triggered } test = Test::IsTrue;
        std::string parameter;
        float value = 0;
    };
    struct Transition {
        std::string from, to;                       // from "Any": from every state but `to`
        std::vector<Condition> conditions;
        float exitAt = -1;                          // 0 to 1: not before this much of the state has played; -1 any time
        float blend = 0.2f;                         // seconds to ease into the next state
    };

    struct Layer {
        std::string name;
        std::vector<std::string> mask;              // the bones it acts on, with all below them; none: all
        float weight = 1;
        std::string weightBy;                       // a float parameter for its weight, instead of `weight`
        bool additive = false;                      // adds to the pose under it instead of taking its place
        std::vector<State> states;
        std::vector<Transition> transitions;
        std::string start;
        int findState(const std::string& name) const;
    };

    std::vector<Parameter> parameters;
    std::vector<State> states;
    std::vector<Transition> transitions;
    std::string start;
    std::vector<Layer> layers;                      // over the states above, in order

    int findState(const std::string& name) const;
    const Parameter* findParameter(const std::string& name) const;

    static std::optional<AnimatorController> parse(const std::string& text, std::string* error = nullptr);
    std::string toText() const;

    /// How much of each motion of a blend tree plays at `value`: the two around it, in proportion.
    /// Motions are taken in the order of their `at`.
    static std::vector<float> blendWeights(const std::vector<Motion>& motions, float value);
    /// The same in two dimensions: each motion is played alone at its point, and between points
    /// the nearest share it (gradient bands); the weights add up to 1.
    static std::vector<float> blendWeights2D(const std::vector<Motion>& motions, float x, float y);
};

}  // namespace molten
