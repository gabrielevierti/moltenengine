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

#include "Assets/Gltf.hpp"
#include "Core/World.hpp"

#include <memory>
#include <string>

namespace molten {

struct Animator {
    std::string model;                  // the .gltf or .glb file; empty uses the file the entity's mesh came from
    std::string clip;                   // which of its animations; empty plays the first
    float speed = 1.0f;
    bool loops = true;
    bool isPlaying = true;
    float blendTime = 0.2f;             // seconds to ease from one clip into the next

    void update(World& world, Entity self, float dt);

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
};

/// The model of a file, read once and shared by everything that plays it.
std::shared_ptr<const GltfModel> sharedGltfModel(const std::string& path);

}  // namespace molten
