#include "Animation/Animator.hpp"

#include "Core/Log.hpp"
#include "Game/Component.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Mesh.hpp"

#include <cmath>
#include <unordered_map>

namespace molten {

std::shared_ptr<const GltfModel> sharedGltfModel(const std::string& path) {
    static std::unordered_map<std::string, std::shared_ptr<const GltfModel>> models;
    auto found = models.find(path);
    if (found != models.end()) return found->second;
    std::string error;
    std::shared_ptr<const GltfModel> model;
    if (std::optional<GltfModel> read = GltfModel::load(path, &error)) model = std::make_shared<const GltfModel>(std::move(*read));
    else logError("animator: %s", error.c_str());
    models[path] = model;
    return model;
}

void Animator::play(const std::string& name) {
    if (name == clip && !hasEnded) return;
    clip = name;
}

void Animator::update(World& world, Entity self, float dt) {
    // Which file: the one named, or the one the entity's mesh was read from.
    std::string file = model;
    if (file.empty()) {
        const MeshRenderer* renderer = world.scene.get<MeshRenderer>(self);
        if (renderer && renderer->mesh) file = renderer->mesh->name;
    }
    if (file.empty() || !isGltfFile(file)) return;
    if (file != loadedFrom) {
        loaded = sharedGltfModel(world.assetPath(file));
        loadedFrom = file;
        animation = -1;
        playing = "\n";                 // no clip has this name: the one asked for is looked up below
    }
    if (!loaded || loaded->animations.empty()) return;

    if (playing != clip) {
        int wanted = clip.empty() ? 0 : loaded->findAnimation(clip);
        if (wanted < 0) {
            logError("animator: %s has no animation called \"%s\"", file.c_str(), clip.c_str());
            wanted = 0;
        }
        // From where the bones are now, so that the change does not show as a jump.
        blendFrom = pose;
        blend = animation >= 0 && blendTime > 0 && pose.translations.size() == loaded->nodes.size() ? 0.0f : 1.0f;
        animation = wanted;
        playing = clip;
        time = 0;
        hasEnded = false;
    }

    float duration = loaded->animations[size_t(animation)].duration;
    if (isPlaying && !hasEnded) {
        time += dt * speed;
        if (duration <= 0) time = 0;
        else if (loops) time = time - duration * std::floor(time / duration);
        else if (time >= duration) {
            time = duration;
            hasEnded = true;
        } else if (time < 0) {
            time = 0;
            hasEnded = true;
        }
        blend = blendTime > 0 ? std::min(1.0f, blend + dt / blendTime) : 1.0f;
    }

    pose = loaded->restPose();
    loaded->sample(animation, time, pose);
    if (blend < 1 && blendFrom.translations.size() == pose.translations.size()) {
        float share = blend * blend * (3 - 2 * blend);
        for (size_t i = 0; i < pose.translations.size(); i++) {
            pose.translations[i] = blendFrom.translations[i] + (pose.translations[i] - blendFrom.translations[i]) * share;
            pose.scales[i] = blendFrom.scales[i] + (pose.scales[i] - blendFrom.scales[i]) * share;
            pose.rotations[i] = simd::slerp(blendFrom.rotations[i], pose.rotations[i], share);
        }
    }
    world.showMesh(self, loaded->assembled(pose));
}

MOLTEN_COMPONENT(Animator, MOLTEN_FIELD(model).help("The .gltf or .glb file. Empty uses the file the entity's mesh came from"),
                 MOLTEN_FIELD(clip).help("Which of the model's animations. Empty plays the first"), MOLTEN_FIELD(speed).range(-4, 4), MOLTEN_FIELD(loops),
                 MOLTEN_FIELD(isPlaying), MOLTEN_FIELD(blendTime).range(0, 2).help("Seconds to ease from one clip into the next"))

}  // namespace molten
