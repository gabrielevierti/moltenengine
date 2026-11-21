#include "Animation/Animator.hpp"

#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
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

    if (!controller.empty()) {
        updateStates(world, dt);
        if (graph) {
            reachWithLimbs(world, self);
            world.showMesh(self, loaded->assembled(pose));
            return;
        }
    }

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
    reachWithLimbs(world, self);
    world.showMesh(self, loaded->assembled(pose));
}

void Animator::reachWithLimbs(World& world, Entity self) {
    const TwoBoneIK* ik = world.scene.get<TwoBoneIK>(self);
    const Transform* place = world.scene.get<Transform>(self);
    if (!ik || !place || ik->weight <= 0 || ik->bone.empty() || !loaded) return;
    const Transform* target = world.scene.get<Transform>(world.find(ik->target));
    if (!target) return;
    int end = -1;
    for (size_t i = 0; i < loaded->nodes.size(); i++) {
        if (loaded->nodes[i].name == ik->bone) end = int(i);
    }
    // The target and the pole in the model's own space.
    Mat4 toModel = simd::inverse(place->world);
    Vec3 goal = transformPoint(toModel, target->worldPosition());
    Vec3 poleAt;
    const Transform* pole = ik->pole.empty() ? nullptr : world.scene.get<Transform>(world.find(ik->pole));
    if (pole) poleAt = transformPoint(toModel, pole->worldPosition());
    solveTwoBoneIK(*loaded, pose, end, goal, pole ? &poleAt : nullptr, std::clamp(ik->weight, 0.0f, 1.0f));
}

std::string Animator::currentState() const {
    return graph && state >= 0 && size_t(state) < graph->states.size() ? graph->states[size_t(state)].name : std::string();
}

namespace {

void lerpPose(GltfPose& pose, const GltfPose& other, float t) {
    if (other.translations.size() != pose.translations.size()) return;
    for (size_t i = 0; i < pose.translations.size(); i++) {
        pose.translations[i] += (other.translations[i] - pose.translations[i]) * t;
        pose.scales[i] += (other.scales[i] - pose.scales[i]) * t;
        pose.rotations[i] = simd::slerp(pose.rotations[i], other.rotations[i], t);
    }
}

bool fileIsSound(const std::string& name) {
    return name.size() > 4 && (name.compare(name.size() - 4, 4, ".wav") == 0 || name.compare(name.size() - 4, 4, ".mp3") == 0);
}

}  // namespace

bool solveTwoBoneIK(const GltfModel& model, GltfPose& pose, int end, Vec3 target, const Vec3* pole, float weight) {
    if (end < 0 || size_t(end) >= model.nodes.size()) return false;
    int mid = model.nodes[size_t(end)].parent;
    int root = mid >= 0 ? model.nodes[size_t(mid)].parent : -1;
    if (root < 0) return false;
    std::vector<Mat4> global = model.globalMatrices(pose);
    auto place = [&](int node, Vec3& at, Quat& turned) {
        Vec3 size;
        decompose(global[size_t(node)], at, turned, size);
    };
    Vec3 a, b, c;
    Quat aTurn, bTurn, cTurn;
    place(root, a, aTurn);
    place(mid, b, bTurn);
    place(end, c, cTurn);
    auto unit = [](Vec3 v) { return safeNormalize(v, Vec3{0, 1, 0}); };
    auto angle = [](Vec3 x, Vec3 y) { return std::acos(std::clamp(simd::dot(x, y), -1.0f, 1.0f)); };
    float lab = simd::length(b - a), lcb = simd::length(c - b);
    float lat = std::clamp(simd::length(target - a), 0.001f, (lab + lcb) * 0.9999f);
    // The angles now, and those that put the end at the target's distance (the law of cosines).
    float acAB0 = angle(unit(c - a), unit(b - a));
    float baBC0 = angle(unit(a - b), unit(c - b));
    float acAT0 = angle(unit(c - a), unit(target - a));
    float acAB1 = std::acos(std::clamp((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
    float baBC1 = std::acos(std::clamp((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));
    Vec3 bendTo = pole ? *pole - a : b - a;
    Vec3 axis0 = simd::cross(c - a, bendTo);
    if (simd::length_squared(axis0) < 1e-10f) axis0 = simd::cross(c - a, Vec3{0, 0, 1});
    axis0 = unit(axis0);
    (void)acAT0;
    // First the bend: the hip (or shoulder) and the knee (or elbow) turned in the limb's plane until
    // the end is as far from the root as the target is. Each as a turn in its own bone's space.
    Quat r0 = Quat((acAB1 - acAB0) * weight, unit(rotate(simd::conjugate(aTurn), axis0)));
    Quat r1 = Quat((baBC1 - baBC0) * weight, unit(rotate(simd::conjugate(bTurn), axis0)));
    pose.rotations[size_t(root)] = simd::normalize(pose.rotations[size_t(root)] * r0);
    pose.rotations[size_t(mid)] = simd::normalize(pose.rotations[size_t(mid)] * r1);
    // Then the swing: the whole limb turned at its root until the end points at the target.
    global = model.globalMatrices(pose);
    place(root, a, aTurn);
    place(end, c, cTurn);
    Vec3 from = unit(c - a), to = unit(target - a);
    Vec3 axis1 = simd::cross(from, to);
    float swing = angle(from, to) * weight;
    if (simd::length_squared(axis1) > 1e-12f && swing > 1e-6f) {
        Quat r2 = Quat(swing, unit(rotate(simd::conjugate(aTurn), unit(axis1))));
        pose.rotations[size_t(root)] = simd::normalize(pose.rotations[size_t(root)] * r2);
    }
    return true;
}

void Animator::updateStates(World& world, float dt) {
    firedEvents.clear();
    // The controller, read once (and again if another is named).
    if (controller != graphFrom) {
        graphFrom = controller;
        graph = nullptr;
        std::string error;
        if (std::optional<std::string> text = text::readFile(world.assetPath(controller))) {
            if (std::optional<AnimatorController> parsed = AnimatorController::parse(*text, &error)) graph = std::make_shared<const AnimatorController>(std::move(*parsed));
            else logError("animator: %s: %s", controller.c_str(), error.c_str());
        } else {
            logError("animator: cannot read %s", controller.c_str());
        }
        if (!graph) return;
        for (const AnimatorController::Parameter& parameter : graph->parameters) {
            if (!parameters.count(parameter.name)) parameters[parameter.name] = parameter.kind == AnimatorController::Parameter::Kind::Trigger ? 0.0f : parameter.value;
        }
        state = graph->findState(graph->start);
        progress = 0;
        blend = 1;
    }
    if (!graph || state < 0) return;

    // A transition whose conditions hold: the first, in the file's order.
    using Test = AnimatorController::Condition::Test;
    for (const AnimatorController::Transition& transition : graph->transitions) {
        int to = graph->findState(transition.to);
        if (to < 0) continue;
        if (transition.from == "Any" ? to == state : graph->findState(transition.from) != state) continue;
        if (transition.exitAt >= 0 && progress < transition.exitAt) continue;
        bool holds = true;
        for (const AnimatorController::Condition& condition : transition.conditions) {
            float value = parameter(condition.parameter);
            switch (condition.test) {
                case Test::IsTrue: holds = value > 0.5f; break;
                case Test::IsFalse: holds = value <= 0.5f; break;
                case Test::Greater: holds = value > condition.value; break;
                case Test::Less: holds = value < condition.value; break;
                case Test::Triggered: holds = value > 0.5f; break;
            }
            if (!holds) break;
        }
        if (!holds) continue;
        for (const AnimatorController::Condition& condition : transition.conditions) {
            if (condition.test == Test::Triggered) parameters[condition.parameter] = 0;        // used up
        }
        blendFrom = pose;
        blend = transition.blend > 0 && pose.translations.size() == loaded->nodes.size() ? 0.0f : 1.0f;
        stateBlendTime = transition.blend;
        state = to;
        progress = 0;
        // Its events at the very start happen now.
        for (const AnimatorController::Event& event : graph->states[size_t(state)].events) {
            if (event.at <= 0) fileIsSound(event.name) ? world.playSound(event.name) : firedEvents.push_back(event.name);
        }
        break;
    }

    const AnimatorController::State& now = graph->states[size_t(state)];
    if (now.motions.empty()) return;
    // How much of each clip: one, or a blend tree's two around the parameter's value.
    std::vector<float> weights = now.blendBy.empty() ? std::vector<float>(now.motions.size(), 0.0f)
                                                     : AnimatorController::blendWeights(now.motions, parameter(now.blendBy));
    if (now.blendBy.empty()) weights[0] = 1;
    std::vector<int> clips(now.motions.size(), -1);
    float duration = 0;
    for (size_t i = 0; i < now.motions.size(); i++) {
        clips[i] = loaded->findAnimation(now.motions[i].clip);
        if (clips[i] >= 0) duration += weights[i] * loaded->animations[size_t(clips[i])].duration;
    }

    // Through the state: the clips of a blend tree keep step with one another (a walk and a run
    // put each foot down at the same moment of their cycles).
    float before = progress;
    if (isPlaying && duration > 0) progress += dt * speed * now.speed / duration;
    if (!now.loops) progress = std::min(progress, 1.0f);
    hasEnded = !now.loops && progress >= 1.0f;
    for (const AnimatorController::Event& event : now.events) {
        if (event.at <= 0) continue;
        // Passed this frame, in any of the cycles crossed.
        float first = std::floor(before - event.at) + 1 + event.at;
        if (first <= progress && first > before) fileIsSound(event.name) ? world.playSound(event.name) : firedEvents.push_back(event.name);
    }
    float phase = now.loops ? progress - std::floor(progress) : progress;

    GltfPose mixed = loaded->restPose();
    float sum = 0;
    for (size_t i = 0; i < now.motions.size(); i++) {
        if (weights[i] <= 0 || clips[i] < 0) continue;
        GltfPose one = loaded->restPose();
        loaded->sample(clips[i], phase * loaded->animations[size_t(clips[i])].duration, one);
        if (sum <= 0) mixed = one;
        else lerpPose(mixed, one, weights[i] / (sum + weights[i]));
        sum += weights[i];
    }
    if (dt > 0) blend = stateBlendTime > 0 ? std::min(1.0f, blend + dt / stateBlendTime) : 1.0f;
    if (blend < 1 && blendFrom.translations.size() == mixed.translations.size()) {
        float share = blend * blend * (3 - 2 * blend);
        GltfPose from = blendFrom;
        lerpPose(from, mixed, share);
        mixed = from;
    }
    pose = mixed;
}

MOLTEN_COMPONENT(TwoBoneIK, MOLTEN_FIELD(bone).help("The end of the limb: the foot's or hand's node in the model"),
                 MOLTEN_FIELD(target).help("The entity it reaches for"), MOLTEN_FIELD(pole).help("An entity the knee or elbow bends toward; empty: as animated"),
                 MOLTEN_FIELD(weight).range(0, 1))
MOLTEN_COMPONENT(Animator, MOLTEN_FIELD(model).help("The .gltf or .glb file. Empty uses the file the entity's mesh came from"),
                 MOLTEN_FIELD(clip).help("Which of the model's animations. Empty plays the first"), MOLTEN_FIELD(speed).range(-4, 4), MOLTEN_FIELD(loops),
                 MOLTEN_FIELD(isPlaying), MOLTEN_FIELD(blendTime).range(0, 2).help("Seconds to ease from one clip into the next"),
                 MOLTEN_FIELD(controller).help("A .animator file: states and transitions the game drives with parameters. With one, `clip` is not used"))

}  // namespace molten
