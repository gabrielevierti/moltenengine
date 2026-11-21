#include "Animation/Animator.hpp"
#include "Kit/Character.hpp"

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
    if (!loaded || loaded->animations.empty() || isHeldByRagdoll) return;

    if (!controller.empty()) {
        updateStates(world, dt);
        if (graph) {
            moveWithRoot(world, self);
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
    moveWithRoot(world, self);
    reachWithLimbs(world, self);
    world.showMesh(self, loaded->assembled(pose));
}

void Animator::moveWithRoot(World& world, Entity self) {
    rootMoved = Vec3{0, 0, 0};
    if (!rootMotion || !loaded) return;
    // The bone: the one named, or the first of the skeleton.
    int root = -1;
    if (!rootBone.empty()) {
        for (size_t i = 0; i < loaded->nodes.size(); i++) {
            if (loaded->nodes[i].name == rootBone) root = int(i);
        }
    } else if (!loaded->skins.empty() && !loaded->skins[0].joints.empty()) {
        root = loaded->skins[0].joints[0];
    }
    if (root < 0 || size_t(root) >= pose.translations.size()) return;
    Vec3 now = pose.translations[size_t(root)];
    Vec3 step{0, 0, 0};
    if (hasLastRoot && lastRootAnimation == animation) {
        if (time >= lastRootTime || graph) {
            step = now - lastRoot;
        } else if (animation >= 0) {
            // Round again: to the end of the clip, then from its start.
            GltfPose ends = loaded->restPose();
            float duration = loaded->animations[size_t(animation)].duration;
            loaded->sample(animation, duration, ends);
            Vec3 end = ends.translations[size_t(root)];
            ends = loaded->restPose();
            loaded->sample(animation, 0, ends);
            step = (end - lastRoot) + (now - ends.translations[size_t(root)]);
        }
    }
    // A jump of more than a meter in one frame is a change of clip, not a step; so is one that
    // goes back the way it came (a state of the controller starting again).
    if (simd::length(step) > 1.0f || (graph && simd::dot(step, lastStep) < 0 && simd::length(step) > 0.2f)) step = Vec3{0, 0, 0};
    lastStep = step;
    lastRoot = now;
    lastRootTime = time;
    lastRootAnimation = animation;
    hasLastRoot = true;
    // The bone stays over its entity: only its height moves (a step's bounce).
    Vec3 rest = loaded->nodes[size_t(root)].translation;
    pose.translations[size_t(root)] = Vec3{rest.x, now.y, rest.z};
    // Across the ground, turned and sized as the entity is.
    Transform* transform = world.scene.get<Transform>(self);
    if (!transform) return;
    Vec3 position, scale;
    Quat rotation;
    decompose(transform->world, position, rotation, scale);
    Vec3 across = rotate(rotation, Vec3{step.x * scale.x, 0, step.z * scale.z});
    if (simd::length(across) < 1e-7f) return;
    if (kit::CharacterController* body = world.scene.get<kit::CharacterController>(self)) {
        rootMoved = body->move(world, self, across);
    } else {
        Mat4 placed = compose(position + across, rotation, scale);
        if (const Transform* parent = world.scene.get<Transform>(transform->parent)) placed = simd::inverse(parent->world) * placed;
        Vec3 ignored;
        decompose(placed, transform->position, transform->rotation, ignored);
        rootMoved = across;
    }
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
        layerPlaying.clear();
    }
    if (!graph || state < 0) return;

    Playing base{state, progress, blend, stateBlendTime, std::move(pose), std::move(blendFrom)};
    runStates(world, dt, graph->states, graph->transitions, base, true);
    state = base.state, progress = base.progress, blend = base.blend, stateBlendTime = base.stateBlendTime;
    pose = std::move(base.pose);
    blendFrom = std::move(base.blendFrom);
    putLayers(world, dt);
}

void Animator::runStates(World& world, float dt, const std::vector<AnimatorController::State>& states,
                         const std::vector<AnimatorController::Transition>& transitions, Playing& run, bool isBase) {
    if (run.state < 0 || size_t(run.state) >= states.size()) return;
    auto findIn = [&](const std::string& name) {
        for (size_t i = 0; i < states.size(); i++) if (states[i].name == name) return int(i);
        return -1;
    };
    // A transition whose conditions hold: the first, in the file's order.
    using Test = AnimatorController::Condition::Test;
    for (const AnimatorController::Transition& transition : transitions) {
        int to = findIn(transition.to);
        if (to < 0) continue;
        if (transition.from == "Any" ? to == run.state : findIn(transition.from) != run.state) continue;
        if (transition.exitAt >= 0 && run.progress < transition.exitAt) continue;
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
        run.blendFrom = run.pose;
        run.blend = transition.blend > 0 && run.pose.translations.size() == loaded->nodes.size() ? 0.0f : 1.0f;
        run.stateBlendTime = transition.blend;
        run.state = to;
        run.progress = 0;
        // Its events at the very start happen now.
        for (const AnimatorController::Event& event : states[size_t(run.state)].events) {
            if (event.at <= 0) fileIsSound(event.name) ? world.playSound(event.name) : firedEvents.push_back(event.name);
        }
        break;
    }

    const AnimatorController::State& now = states[size_t(run.state)];
    if (now.motions.empty()) return;
    // How much of each clip: one, or a blend tree's two around the parameter's value.
    std::vector<float> weights = now.blendBy.empty()    ? std::vector<float>(now.motions.size(), 0.0f)
                                 : now.blendByY.empty() ? AnimatorController::blendWeights(now.motions, parameter(now.blendBy))
                                                        : AnimatorController::blendWeights2D(now.motions, parameter(now.blendBy), parameter(now.blendByY));
    if (now.blendBy.empty()) weights[0] = 1;
    std::vector<int> clips(now.motions.size(), -1);
    float duration = 0;
    for (size_t i = 0; i < now.motions.size(); i++) {
        clips[i] = loaded->findAnimation(now.motions[i].clip);
        if (clips[i] >= 0) duration += weights[i] * loaded->animations[size_t(clips[i])].duration;
    }

    // Through the state: the clips of a blend tree keep step with one another (a walk and a run
    // put each foot down at the same moment of their cycles).
    float before = run.progress;
    if (isPlaying && duration > 0) run.progress += dt * speed * now.speed / duration;
    if (!now.loops) run.progress = std::min(run.progress, 1.0f);
    if (isBase) hasEnded = !now.loops && run.progress >= 1.0f;
    for (const AnimatorController::Event& event : now.events) {
        if (event.at <= 0) continue;
        // Passed this frame, in any of the cycles crossed.
        float first = std::floor(before - event.at) + 1 + event.at;
        if (first <= run.progress && first > before) fileIsSound(event.name) ? world.playSound(event.name) : firedEvents.push_back(event.name);
    }
    float phase = now.loops ? run.progress - std::floor(run.progress) : run.progress;

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
    if (dt > 0) run.blend = run.stateBlendTime > 0 ? std::min(1.0f, run.blend + dt / run.stateBlendTime) : 1.0f;
    if (run.blend < 1 && run.blendFrom.translations.size() == mixed.translations.size()) {
        float share = run.blend * run.blend * (3 - 2 * run.blend);
        GltfPose from = run.blendFrom;
        lerpPose(from, mixed, share);
        mixed = from;
    }
    run.pose = mixed;
}

void Animator::putLayers(World& world, float dt) {
    if (!graph || graph->layers.empty() || pose.translations.size() != loaded->nodes.size()) return;
    // Each layer's states from its start, and the nodes its mask covers, once.
    if (layerPlaying.size() != graph->layers.size()) {
        layerPlaying.assign(graph->layers.size(), Playing{});
        layerMasks.assign(graph->layers.size(), {});
        for (size_t l = 0; l < graph->layers.size(); l++) {
            const AnimatorController::Layer& layer = graph->layers[l];
            layerPlaying[l].state = layer.findState(layer.start);
            std::vector<float>& mask = layerMasks[l];
            mask.assign(loaded->nodes.size(), layer.mask.empty() ? 1.0f : 0.0f);
            for (const std::string& bone : layer.mask) {
                int found = -1;
                for (size_t i = 0; i < loaded->nodes.size(); i++) if (loaded->nodes[i].name == bone) found = int(i);
                if (found < 0) {
                    logError("animator: layer %s: the model has no bone called \"%s\"", layer.name.c_str(), bone.c_str());
                    continue;
                }
                // It and everything below it.
                std::vector<int> open{found};
                while (!open.empty()) {
                    int node = open.back();
                    open.pop_back();
                    mask[size_t(node)] = 1;
                    for (int child : loaded->nodes[size_t(node)].children) open.push_back(child);
                }
            }
        }
    }
    GltfPose rest = loaded->restPose();
    for (size_t l = 0; l < graph->layers.size(); l++) {
        const AnimatorController::Layer& layer = graph->layers[l];
        Playing& run = layerPlaying[l];
        runStates(world, dt, layer.states, layer.transitions, run, false);
        float weight = std::clamp(layer.weightBy.empty() ? layer.weight : parameter(layer.weightBy), 0.0f, 1.0f);
        if (weight <= 0 || run.pose.translations.size() != pose.translations.size()) continue;
        for (size_t i = 0; i < pose.translations.size(); i++) {
            float share = weight * layerMasks[l][i];
            if (share <= 0) continue;
            if (layer.additive) {
                // What the layer moves the node from where it rests, added to what is under it.
                pose.translations[i] += (run.pose.translations[i] - rest.translations[i]) * share;
                Quat turn = simd::normalize(run.pose.rotations[i] * simd::conjugate(rest.rotations[i]));
                pose.rotations[i] = simd::normalize(simd::slerp(Quat(0, 0, 0, 1), turn, share) * pose.rotations[i]);
            } else {
                pose.translations[i] += (run.pose.translations[i] - pose.translations[i]) * share;
                pose.scales[i] += (run.pose.scales[i] - pose.scales[i]) * share;
                pose.rotations[i] = simd::slerp(pose.rotations[i], run.pose.rotations[i], share);
            }
        }
    }
}

MOLTEN_COMPONENT(TwoBoneIK, MOLTEN_FIELD(bone).help("The end of the limb: the foot's or hand's node in the model"),
                 MOLTEN_FIELD(target).help("The entity it reaches for"), MOLTEN_FIELD(pole).help("An entity the knee or elbow bends toward; empty: as animated"),
                 MOLTEN_FIELD(weight).range(0, 1))
MOLTEN_COMPONENT(Animator, MOLTEN_FIELD(model).help("The .gltf or .glb file. Empty uses the file the entity's mesh came from"),
                 MOLTEN_FIELD(clip).help("Which of the model's animations. Empty plays the first"), MOLTEN_FIELD(speed).range(-4, 4), MOLTEN_FIELD(loops),
                 MOLTEN_FIELD(isPlaying), MOLTEN_FIELD(blendTime).range(0, 2).help("Seconds to ease from one clip into the next"),
                 MOLTEN_FIELD(controller).help("A .animator file: states and transitions the game drives with parameters. With one, `clip` is not used"),
                 MOLTEN_FIELD(rootMotion).help("The entity goes where the animation walks it, instead of the model walking away from it"),
                 MOLTEN_FIELD(rootBone).help("The bone that carries the body. Empty: the skeleton's first"))

}  // namespace molten
