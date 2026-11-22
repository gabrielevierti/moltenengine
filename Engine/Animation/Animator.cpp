#include "Animation/Animator.hpp"
#include "Kit/Character.hpp"

#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Game/Component.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Mesh.hpp"

#include <cctype>
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

namespace {

std::vector<std::string> listOf(const std::string& text) {
    std::vector<std::string> out;
    size_t from = 0;
    while (from <= text.size()) {
        size_t comma = text.find(',', from);
        std::string item = text.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
        size_t a = item.find_first_not_of(' '), b = item.find_last_not_of(' ');
        if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    return out;
}

std::string stemOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace

std::vector<std::string> Animator::animationNames() const {
    std::vector<std::string> names;
    for (const std::string& path : listOf(animations)) names.push_back(stemOf(path));
    return names;
}

std::string Animator::resolve(const std::string& name) const {
    for (const std::string& path : listOf(animations)) {
        if (path == name || stemOf(path) == name) return path;
    }
    return name;
}

void Animator::play(const std::string& name) {
    isPlaying = true;
    if (!controller.empty()) {
        crossFade(name, blendTime);
        return;
    }
    std::string wanted = resolve(name);
    if (wanted == clip && !hasEnded) return;
    clip = wanted;
    playing = "\n";                     // from its start, even if it was the one playing
    hasEnded = false;
}

void Animator::stop() {
    isPlaying = false;
    hasEnded = false;
    time = 0;
    progress = 0;
}

bool Animator::isPlayingAnimation(const std::string& name) const {
    if (!isPlaying || hasEnded) return false;
    if (!controller.empty()) return currentState() == name;
    return clip == resolve(name);
}

void Animator::update(World& world, Entity self, float dt) {
    owner = self;
    // Which file: the one named, or the one the entity's mesh was read from. None is fine: the
    // clips made in the editor animate any entity, model or not.
    std::string file = model;
    if (file.empty()) {
        const MeshRenderer* renderer = world.scene.get<MeshRenderer>(self);
        if (renderer && renderer->mesh) file = renderer->mesh->name;
        else if (renderer) file = renderer->meshName;      // where there is no GPU to hold the mesh, its name is still known
    }
    if (file.empty() || !isGltfFile(file)) file.clear();
    if (file != loadedFrom) {
        loaded = file.empty() ? nullptr : sharedGltfModel(world.assetPath(file));
        loadedFrom = file;
        animation = -1;
        playing = "\n";                 // no clip has this name: the one asked for is looked up below
    }
    if (isHeldByRagdoll) return;
    bool hasModel = loaded && !loaded->animations.empty();

    if (!controller.empty()) {
        updateStates(world, dt);
        if (graph) {
            if (!props.values.empty()) props.apply(world.scene, self);
            if (hasModel && pose.translations.size() == loaded->nodes.size()) {
                moveWithRoot(world, self);
                reachWithLimbs(world, self);
                world.showMesh(self, loaded->assembled(pose));
            }
            return;
        }
    }

    // A clip made in the editor, played by itself.
    if (isAnimationClipFile(clip)) {
        playClipFile(world, self, dt);
        return;
    }
    if (!hasModel) return;

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

void Animator::playClipFile(World& world, Entity self, float dt) {
    std::shared_ptr<const AnimationClip> file = sharedAnimationClip(world.assetPath(clip));
    if (!file) return;
    if (playing != clip) {
        propsFrom = props;
        blend = !props.values.empty() && blendTime > 0 ? 0.0f : 1.0f;
        playing = clip;
        time = 0;
        hasEnded = false;
        if (isPlaying) fireClipEvents(world, self, *file, -1, 0);
    }
    float duration = file->length;
    if (isPlaying && !hasEnded) {
        float before = time;
        time += dt * speed;
        if (loops && duration > 0) {
            if (time >= duration) {
                fireClipEvents(world, self, *file, before, duration);
                before = -1;
            }
            time = time - duration * std::floor(time / duration);
        } else if (time >= duration) {
            time = duration;
            hasEnded = true;
        } else if (time < 0) {
            time = 0;
            hasEnded = true;
        }
        fireClipEvents(world, self, *file, before, time);
        blend = blendTime > 0 ? std::min(1.0f, blend + dt / blendTime) : 1.0f;
    }
    props = ClipPose::of(*file, time, &parameters);
    if (blend < 1) {
        ClipPose from = propsFrom;
        from.mixToward(props, blend * blend * (3 - 2 * blend));
        props = std::move(from);
    }
    props.apply(world.scene, self);
}

void Animator::fireClipEvents(World& world, Entity self, const AnimationClip& file, float after, float upTo) {
    for (const AnimationClip::Event& event : file.events) {
        if (event.time > after && event.time <= upTo) fireEvent(world, self, event.name);
    }
}

void Animator::fireEvent(World& world, Entity self, const std::string& name) {
    if (name.empty()) return;
    std::string lowered = name;
    for (char& c : lowered) c = char(std::tolower(static_cast<unsigned char>(c)));
    bool isSound = lowered.size() > 4 && (lowered.ends_with(".wav") || lowered.ends_with(".mp3") || lowered.ends_with(".ogg") || lowered.ends_with(".flac"));
    if (isSound) {
        world.playSound(name);
        return;
    }
    // For the game: heard as a message by the entity's scripts (On Message, onMessage).
    firedEvents.push_back(name);
    world.send(name, self, 0, {}, self);
}

void Animator::crossFade(const std::string& stateName, float seconds) {
    pendingState = stateName;
    pendingBlend = std::max(seconds, 0.0f);
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
        // Read again (the editor saved it while the game runs): the state it is in goes on.
        std::string wasIn = graphFrom == "\n" ? currentState() : std::string();
        float wasAt = progress;
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
        if (int again = graph->findState(wasIn); again >= 0) {
            state = again;
            progress = wasAt;
        }
        blend = 1;
        layerPlaying.clear();
    }
    if (!graph || state < 0) return;

    Playing base{state, progress, blend, stateBlendTime, std::move(pose), std::move(blendFrom), std::move(props), std::move(propsFrom)};
    runStates(world, dt, graph->states, graph->transitions, base, true);
    state = base.state, progress = base.progress, blend = base.blend, stateBlendTime = base.stateBlendTime;
    pose = std::move(base.pose);
    blendFrom = std::move(base.blendFrom);
    props = std::move(base.props);
    propsFrom = std::move(base.propsFrom);
    putLayers(world, dt);
}

void Animator::runStates(World& world, float dt, const std::vector<AnimatorController::State>& states,
                         const std::vector<AnimatorController::Transition>& transitions, Playing& run, bool isBase) {
    if (run.state < 0 || size_t(run.state) >= states.size()) return;
    auto findIn = [&](const std::string& name) {
        for (size_t i = 0; i < states.size(); i++) if (states[i].name == name) return int(i);
        return -1;
    };
    auto enter = [&](int to, float blendSeconds) {
        run.blendFrom = run.pose;
        run.propsFrom = run.props;
        run.blend = blendSeconds > 0 ? 0.0f : 1.0f;
        run.stateBlendTime = blendSeconds;
        run.state = to;
        run.progress = 0;
        // Its events at the very start happen now.
        for (const AnimatorController::Event& event : states[size_t(run.state)].events) {
            if (event.at <= 0) fireEvent(world, owner, event.name);
        }
    };
    // Asked for by the game (crossFade, Play State): before any transition.
    if (isBase && !pendingState.empty()) {
        int to = findIn(pendingState);
        if (to >= 0) enter(to, pendingBlend);
        pendingState.clear();
    }
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
        enter(to, transition.blend);
        break;
    }

    const AnimatorController::State& now = states[size_t(run.state)];
    if (now.motions.empty()) return;
    // How much of each clip: one, or a blend tree's two around the parameter's value.
    std::vector<float> weights = now.blendBy.empty()    ? std::vector<float>(now.motions.size(), 0.0f)
                                 : now.blendByY.empty() ? AnimatorController::blendWeights(now.motions, parameter(now.blendBy))
                                                        : AnimatorController::blendWeights2D(now.motions, parameter(now.blendBy), parameter(now.blendByY));
    if (now.blendBy.empty()) weights[0] = 1;
    // Each motion: an animation of the model, or a clip made in the editor (a .anim file).
    std::vector<int> clips(now.motions.size(), -1);
    std::vector<std::shared_ptr<const AnimationClip>> files(now.motions.size());
    float duration = 0;
    for (size_t i = 0; i < now.motions.size(); i++) {
        if (isAnimationClipFile(now.motions[i].clip)) {
            files[i] = sharedAnimationClip(world.assetPath(now.motions[i].clip));
            if (files[i]) duration += weights[i] * files[i]->length;
        } else if (loaded) {
            clips[i] = loaded->findAnimation(now.motions[i].clip);
            if (clips[i] >= 0) duration += weights[i] * loaded->animations[size_t(clips[i])].duration;
        }
    }

    // Through the state: the clips of a blend tree keep step with one another (a walk and a run
    // put each foot down at the same moment of their cycles).
    float before = run.progress;
    if (isPlaying && duration > 0) run.progress += dt * speed * now.speed / duration;
    if (!now.loops) run.progress = std::min(run.progress, 1.0f);
    // Or as far through as a parameter says: a door opened as much as "open" is.
    if (!now.timeBy.empty()) run.progress = std::clamp(parameter(now.timeBy), 0.0f, 1.0f);
    if (isBase) hasEnded = !now.loops && run.progress >= 1.0f;
    for (const AnimatorController::Event& event : now.events) {
        if (event.at <= 0) continue;
        // Passed this frame, in any of the cycles crossed.
        float first = std::floor(before - event.at) + 1 + event.at;
        if (first <= run.progress && first > before) fireEvent(world, owner, event.name);
    }
    float phase = now.loops ? run.progress - std::floor(run.progress) : run.progress;
    float lastPhase = now.loops ? before - std::floor(before) : before;
    // The events of the clips made in the editor, as the state passes them.
    if (isBase) {
        for (size_t i = 0; i < files.size(); i++) {
            if (!files[i] || weights[i] <= 0.5f) continue;
            float length = files[i]->length;
            if (phase >= lastPhase) fireClipEvents(world, owner, *files[i], lastPhase * length, phase * length);
            else {
                fireClipEvents(world, owner, *files[i], lastPhase * length, length);
                fireClipEvents(world, owner, *files[i], -1, phase * length);
            }
        }
    }

    float sum = 0;
    if (loaded) {
        GltfPose mixed = loaded->restPose();
        for (size_t i = 0; i < now.motions.size(); i++) {
            if (weights[i] <= 0 || clips[i] < 0) continue;
            GltfPose one = loaded->restPose();
            loaded->sample(clips[i], phase * loaded->animations[size_t(clips[i])].duration, one);
            if (sum <= 0) mixed = one;
            else lerpPose(mixed, one, weights[i] / (sum + weights[i]));
            sum += weights[i];
        }
        run.pose = mixed;
    }
    ClipPose mixedProps;
    sum = 0;
    for (size_t i = 0; i < now.motions.size(); i++) {
        if (weights[i] <= 0 || !files[i]) continue;
        ClipPose one = ClipPose::of(*files[i], phase * files[i]->length, &parameters);
        if (sum <= 0) mixedProps = std::move(one);
        else mixedProps.mixToward(one, weights[i] / (sum + weights[i]));
        sum += weights[i];
    }
    if (dt > 0) run.blend = run.stateBlendTime > 0 ? std::min(1.0f, run.blend + dt / run.stateBlendTime) : 1.0f;
    float share = run.blend * run.blend * (3 - 2 * run.blend);
    if (run.blend < 1 && loaded && run.blendFrom.translations.size() == run.pose.translations.size()) {
        GltfPose from = run.blendFrom;
        lerpPose(from, run.pose, share);
        run.pose = std::move(from);
    }
    if (run.blend < 1 && !run.propsFrom.values.empty()) {
        ClipPose from = run.propsFrom;
        from.mixToward(mixedProps, share);
        mixedProps = std::move(from);
    }
    run.props = std::move(mixedProps);
}

void Animator::putLayers(World& world, float dt) {
    if (!graph || graph->layers.empty()) return;
    if (!loaded || pose.translations.size() != loaded->nodes.size()) {
        // No model: the layers' clips made in the editor go over the base's, by their weight.
        if (layerPlaying.size() != graph->layers.size()) {
            layerPlaying.assign(graph->layers.size(), Playing{});
            for (size_t l = 0; l < graph->layers.size(); l++) layerPlaying[l].state = graph->layers[l].findState(graph->layers[l].start);
        }
        for (size_t l = 0; l < graph->layers.size(); l++) {
            const AnimatorController::Layer& layer = graph->layers[l];
            runStates(world, dt, layer.states, layer.transitions, layerPlaying[l], false);
            float weight = std::clamp(layer.weightBy.empty() ? layer.weight : parameter(layer.weightBy), 0.0f, 1.0f);
            if (weight > 0) props.mixToward(layerPlaying[l].props, weight);
        }
        return;
    }
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
        if (weight > 0 && !run.props.values.empty()) props.mixToward(run.props, weight);
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

MOLTEN_COMPONENT(TwoBoneIK, MOLTEN_FIELD(bone).picks("bone").help("The end of the limb: the foot's or hand's node in the model"),
                 MOLTEN_FIELD(target).picks("entity").help("The entity it reaches for"), MOLTEN_FIELD(pole).picks("entity").help("An entity the knee or elbow bends toward; empty: as animated"),
                 MOLTEN_FIELD(weight).range(0, 1))
MOLTEN_COMPONENT(Animator, MOLTEN_FIELD(animations).picks("ownClips").help("Its animations: made for this entity in the Timeline window (Window > Animation > Timeline, Create Animation)"),
                 MOLTEN_FIELD(clip).picks("ownAnimation").help("What it plays from the start. From code: animator->play(\"Open\"), stop(), pause(), resume()"),
                 MOLTEN_FIELD(model).picks("model").help("A .gltf or .glb whose animations are its too. Empty: the file the entity's mesh came from, if any"), MOLTEN_FIELD(speed).range(-4, 4), MOLTEN_FIELD(loops),
                 MOLTEN_FIELD(isPlaying), MOLTEN_FIELD(blendTime).range(0, 2).help("Seconds to ease from one clip into the next"),
                 MOLTEN_FIELD(controller).picks("controller").help("A .animator file: states and transitions the game drives with parameters. With one, `clip` is not used"),
                 MOLTEN_FIELD(rootMotion).help("The entity goes where the animation walks it, instead of the model walking away from it"),
                 MOLTEN_FIELD(rootBone).picks("bone").help("The bone that carries the body. Empty: the skeleton's first"))

}  // namespace molten
