#include "Animation/AnimationClip.hpp"

#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Game/Component.hpp"
#include "Rendering/Material.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace molten {

namespace {

float numberAt(const std::vector<std::string>& words, size_t i, float otherwise = 0) {
    if (i >= words.size()) return otherwise;
    char* end = nullptr;
    float value = std::strtof(words[i].c_str(), &end);
    return end != words[i].c_str() ? value : otherwise;
}

std::string numberText(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", double(value));
    return text;
}

const char* easeName(AnimationKey::Ease ease) {
    switch (ease) {
        case AnimationKey::Ease::Linear: return "linear";
        case AnimationKey::Ease::Step: return "step";
        case AnimationKey::Ease::Smooth: break;
    }
    return "smooth";
}

/// The registered component and its field, for the components the registry knows.
const Field* registeredField(const std::string& component, const std::string& field, const ComponentType** typeOut = nullptr) {
    const ComponentType* type = ComponentRegistry::instance().find(component);
    if (!type) return nullptr;
    if (typeOut) *typeOut = type;
    return type->findField(field);
}

int widthOf(FieldType type) {
    switch (type) {
        case FieldType::Float:
        case FieldType::Int:
        case FieldType::Bool: return 1;
        case FieldType::Vec2: return 2;
        case FieldType::Vec3: return 3;
        case FieldType::Vec4: return 4;
        default: return 0;
    }
}

}  // namespace

// ---------------------------------------------------------------- tracks

void AnimationTrack::sort() {
    std::stable_sort(keys.begin(), keys.end(), [](const AnimationKey& a, const AnimationKey& b) { return a.time < b.time; });
}

std::string AnimationTrack::label() const {
    return (path.empty() ? std::string() : path + ": ") + component + " " + field;
}

void AnimationTrack::setKey(float time, const std::array<float, 4>& value) {
    for (AnimationKey& key : keys) {
        if (std::abs(key.time - time) < 0.005f) {
            key.value = value;
            return;
        }
    }
    AnimationKey key;
    key.time = time;
    key.value = value;
    keys.push_back(key);
    sort();
}

std::array<float, 4> AnimationTrack::sample(float time) const {
    if (keys.empty()) return {0, 0, 0, 0};
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    size_t b = 1;
    while (b < keys.size() && keys[b].time < time) b++;
    const AnimationKey& k0 = keys[b - 1];
    const AnimationKey& k1 = keys[b];
    float span = std::max(k1.time - k0.time, 1e-6f);
    float t = (time - k0.time) / span;
    std::array<float, 4> out{};
    if (k1.ease == AnimationKey::Ease::Step) return k0.value;
    if (k1.ease == AnimationKey::Ease::Linear) {
        for (int i = 0; i < 4; i++) out[size_t(i)] = k0.value[size_t(i)] + (k1.value[size_t(i)] - k0.value[size_t(i)]) * t;
        return out;
    }
    // Smooth: through the keys with the slope of their neighbours (flat at the first and the last,
    // and where a key is a peak or a dip, so that it does not overshoot).
    auto slope = [&](size_t k, int i) {
        if (k == 0 || k + 1 >= keys.size()) return 0.0f;
        float before = keys[k].value[size_t(i)] - keys[k - 1].value[size_t(i)];
        float after = keys[k + 1].value[size_t(i)] - keys[k].value[size_t(i)];
        if (before * after <= 0) return 0.0f;
        return (keys[k + 1].value[size_t(i)] - keys[k - 1].value[size_t(i)]) / std::max(keys[k + 1].time - keys[k - 1].time, 1e-6f);
    };
    float t2 = t * t, t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    for (int i = 0; i < 4; i++) {
        out[size_t(i)] = h00 * k0.value[size_t(i)] + h10 * span * slope(b - 1, i) + h01 * k1.value[size_t(i)] + h11 * span * slope(b, i);
    }
    return out;
}

// ---------------------------------------------------------------- clips

AnimationTrack* AnimationClip::find(const std::string& path, const std::string& component, const std::string& field) {
    for (AnimationTrack& track : tracks) {
        if (track.path == path && track.component == component && track.field == field) return &track;
    }
    return nullptr;
}

const AnimationTrack* AnimationClip::find(const std::string& path, const std::string& component, const std::string& field) const {
    return const_cast<AnimationClip*>(this)->find(path, component, field);
}

std::optional<AnimationClip> AnimationClip::parse(const std::string& text, std::string* error) {
    AnimationClip clip;
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    bool hasHeader = false;
    while (std::getline(lines, line)) {
        number++;
        std::vector<std::string> words = text::splitWords(line);
        if (words.empty() || words[0][0] == '#') continue;
        const std::string& keyword = words[0];
        if (keyword == "molten-clip") {
            hasHeader = true;
        } else if (keyword == "length") {
            clip.length = std::max(numberAt(words, 1, 1), 0.01f);
        } else if (keyword == "loops") {
            clip.loops = numberAt(words, 1, 1) != 0;
        } else if (keyword == "track") {
            if (words.size() < 4) {
                if (error) *error = "line " + std::to_string(number) + ": a track is: track \"path\" Component field";
                return std::nullopt;
            }
            AnimationTrack track;
            track.path = words[1];
            track.component = words[2];
            track.field = words[3];
            clip.tracks.push_back(track);
        } else if (keyword == "key") {
            if (clip.tracks.empty()) {
                if (error) *error = "line " + std::to_string(number) + ": a key before any track";
                return std::nullopt;
            }
            AnimationTrack& track = clip.tracks.back();
            AnimationKey key;
            key.time = numberAt(words, 1);
            int values = 0;
            for (size_t i = 2; i < words.size() && values < 4; i++) {
                if (words[i] == "linear") key.ease = AnimationKey::Ease::Linear;
                else if (words[i] == "step") key.ease = AnimationKey::Ease::Step;
                else if (words[i] == "smooth") key.ease = AnimationKey::Ease::Smooth;
                else key.value[size_t(values++)] = numberAt(words, i);
            }
            track.width = std::max(track.width, std::max(values, 1));
            track.keys.push_back(key);
        } else if (keyword == "event") {
            clip.events.push_back(Event{numberAt(words, 1), words.size() > 2 ? words[2] : std::string()});
        } else {
            if (error) *error = "line " + std::to_string(number) + ": \"" + keyword + "\" is not part of a clip";
            return std::nullopt;
        }
    }
    if (!hasHeader) {
        if (error) *error = "not a clip: it does not start with molten-clip";
        return std::nullopt;
    }
    for (AnimationTrack& track : clip.tracks) track.sort();
    std::stable_sort(clip.events.begin(), clip.events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
    return clip;
}

std::string AnimationClip::toText() const {
    std::string out = "molten-clip 1\nlength " + numberText(length) + "\nloops " + (loops ? "1" : "0") + "\n";
    for (const AnimationTrack& track : tracks) {
        out += "track " + text::quote(track.path) + " " + track.component + " " + track.field + "\n";
        for (const AnimationKey& key : track.keys) {
            out += "  key " + numberText(key.time) + " ";
            for (int i = 0; i < track.width; i++) out += " " + numberText(key.value[size_t(i)]);
            if (key.ease != AnimationKey::Ease::Smooth) out += std::string(" ") + easeName(key.ease);
            out += "\n";
        }
    }
    for (const Event& event : events) out += "event " + numberText(event.time) + " " + text::quote(event.name) + "\n";
    return out;
}

// ---------------------------------------------------------------- poses

ClipPose ClipPose::of(const AnimationClip& clip, float time) {
    ClipPose pose;
    for (const AnimationTrack& track : clip.tracks) {
        if (track.keys.empty()) continue;
        Value& value = pose.values[track.id()];
        value.track = &track;
        value.path = track.path;
        value.component = track.component;
        value.field = track.field;
        value.width = track.width;
        value.value = track.sample(time);
    }
    return pose;
}

void ClipPose::mixToward(const ClipPose& other, float t) {
    for (const auto& [id, theirs] : other.values) {
        auto mine = values.find(id);
        if (mine == values.end()) {
            values[id] = theirs;
            continue;
        }
        for (int i = 0; i < 4; i++) mine->second.value[size_t(i)] += (theirs.value[size_t(i)] - mine->second.value[size_t(i)]) * t;
    }
}

void ClipPose::apply(Scene& scene, Entity root) const {
    for (const auto& [id, value] : values) {
        Entity entity = entityAtPath(scene, root, value.path);
        if (entity.isValid()) writeProperty(scene, entity, value.component, value.field, value.value);
    }
}

// ---------------------------------------------------------------- paths

Entity entityAtPath(const Scene& scene, Entity root, const std::string& path) {
    if (path.empty()) return root;
    Entity at = root;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        std::string name = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        Entity found{};
        const_cast<Scene&>(scene).each<Transform>([&](Entity entity, Transform& transform) {
            if (found.isValid() || transform.parent != at) return;
            const Name* named = scene.get<Name>(entity);
            if (named && named->value == name) found = entity;
        });
        if (!found.isValid()) return Entity{};
        at = found;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return at;
}

std::optional<std::string> pathFromRoot(const Scene& scene, Entity root, Entity entity) {
    std::string path;
    Entity at = entity;
    for (int depth = 0; depth < 64; depth++) {
        if (at == root) return path;
        const Transform* transform = scene.get<Transform>(at);
        const Name* name = scene.get<Name>(at);
        if (!transform || !name) return std::nullopt;
        path = path.empty() ? name->value : name->value + "/" + path;
        at = transform->parent;
        if (!at.isValid()) return std::nullopt;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- properties

std::vector<AnimatableProperty> animatableProperties(Scene& scene, Entity entity) {
    std::vector<AnimatableProperty> out;
    if (scene.has<Transform>(entity)) {
        out.push_back({"Transform", "position", 3});
        out.push_back({"Transform", "rotation", 3});
        out.push_back({"Transform", "scale", 3});
        out.push_back({"Transform", "active", 1, false, true});
    }
    if (scene.has<MeshRenderer>(entity)) {
        out.push_back({"MeshRenderer", "tint", 4, true});
        out.push_back({"MeshRenderer", "visible", 1, false, true});
    }
    if (scene.has<Light>(entity)) {
        out.push_back({"Light", "color", 3, true});
        out.push_back({"Light", "intensity", 1});
        out.push_back({"Light", "range", 1});
        out.push_back({"Light", "enabled", 1, false, true});
    }
    if (scene.has<Camera>(entity)) {
        out.push_back({"Camera", "fieldOfView", 1});
        out.push_back({"Camera", "orthographicHeight", 1});
    }
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (!type.get || !type.get(scene, entity)) continue;
        for (const Field& field : type.fields) {
            int width = widthOf(field.type);
            if (width == 0) continue;
            out.push_back({type.name, field.name, width, field.isColor, field.type == FieldType::Bool});
        }
    }
    return out;
}

bool readProperty(Scene& scene, Entity entity, const std::string& component, const std::string& field, std::array<float, 4>& value, int* width) {
    value = {0, 0, 0, 0};
    auto put = [&](std::initializer_list<float> numbers) {
        size_t i = 0;
        for (float n : numbers) value[i++] = n;
        if (width) *width = int(numbers.size());
        return true;
    };
    if (component == "Transform") {
        Transform* transform = scene.get<Transform>(entity);
        if (!transform) return false;
        if (field == "position") return put({transform->position.x, transform->position.y, transform->position.z});
        if (field == "rotation") {
            Vec3 angles = toEulerAngles(transform->rotation) * (180.0f / kPi);
            return put({angles.x, angles.y, angles.z});
        }
        if (field == "scale") return put({transform->scale.x, transform->scale.y, transform->scale.z});
        if (field == "active") return put({transform->isActive ? 1.0f : 0.0f});
        return false;
    }
    if (component == "MeshRenderer") {
        MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        if (!renderer) return false;
        if (field == "tint") return put({renderer->tint.x, renderer->tint.y, renderer->tint.z, renderer->tint.w});
        if (field == "visible") return put({renderer->isVisible ? 1.0f : 0.0f});
        return false;
    }
    if (component == "Light") {
        Light* light = scene.get<Light>(entity);
        if (!light) return false;
        if (field == "color") return put({light->color.x, light->color.y, light->color.z});
        if (field == "intensity") return put({light->intensity});
        if (field == "range") return put({light->range});
        if (field == "enabled") return put({light->isEnabled ? 1.0f : 0.0f});
        return false;
    }
    if (component == "Camera") {
        Camera* camera = scene.get<Camera>(entity);
        if (!camera) return false;
        if (field == "fieldOfView") return put({camera->fovY * (180.0f / kPi)});
        if (field == "orthographicHeight") return put({camera->orthographicHeight});
        return false;
    }
    const ComponentType* type = nullptr;
    const Field* found = registeredField(component, field, &type);
    if (!found || !type->get) return false;
    void* object = type->get(scene, entity);
    if (!object) return false;
    void* at = found->address(object);
    switch (found->type) {
        case FieldType::Float: return put({*static_cast<float*>(at)});
        case FieldType::Int: return put({float(*static_cast<int*>(at))});
        case FieldType::Bool: return put({*static_cast<bool*>(at) ? 1.0f : 0.0f});
        case FieldType::Vec2: {
            Vec2 v = *static_cast<Vec2*>(at);
            return put({v.x, v.y});
        }
        case FieldType::Vec3: {
            Vec3 v = *static_cast<Vec3*>(at);
            return put({v.x, v.y, v.z});
        }
        case FieldType::Vec4: {
            Vec4 v = *static_cast<Vec4*>(at);
            return put({v.x, v.y, v.z, v.w});
        }
        default: return false;
    }
}

bool writeProperty(Scene& scene, Entity entity, const std::string& component, const std::string& field, const std::array<float, 4>& v) {
    if (component == "Transform") {
        Transform* transform = scene.get<Transform>(entity);
        if (!transform) return false;
        if (field == "position") transform->position = Vec3{v[0], v[1], v[2]};
        else if (field == "rotation") transform->rotation = eulerAngles(radians(v[0]), radians(v[1]), radians(v[2]));
        else if (field == "scale") transform->scale = Vec3{v[0], v[1], v[2]};
        else if (field == "active") transform->isActive = v[0] > 0.5f;
        else return false;
        return true;
    }
    if (component == "MeshRenderer") {
        MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        if (!renderer) return false;
        if (field == "tint") renderer->tint = Vec4{v[0], v[1], v[2], v[3]};
        else if (field == "visible") renderer->isVisible = v[0] > 0.5f;
        else return false;
        return true;
    }
    if (component == "Light") {
        Light* light = scene.get<Light>(entity);
        if (!light) return false;
        if (field == "color") light->color = Vec3{v[0], v[1], v[2]};
        else if (field == "intensity") light->intensity = v[0];
        else if (field == "range") light->range = v[0];
        else if (field == "enabled") light->isEnabled = v[0] > 0.5f;
        else return false;
        return true;
    }
    if (component == "Camera") {
        Camera* camera = scene.get<Camera>(entity);
        if (!camera) return false;
        if (field == "fieldOfView") camera->fovY = radians(v[0]);
        else if (field == "orthographicHeight") camera->orthographicHeight = v[0];
        else return false;
        return true;
    }
    const ComponentType* type = nullptr;
    const Field* found = registeredField(component, field, &type);
    if (!found || !type->get) return false;
    void* object = type->get(scene, entity);
    if (!object) return false;
    void* at = found->address(object);
    switch (found->type) {
        case FieldType::Float: *static_cast<float*>(at) = v[0]; return true;
        case FieldType::Int: *static_cast<int*>(at) = int(std::lround(v[0])); return true;
        case FieldType::Bool: *static_cast<bool*>(at) = v[0] > 0.5f; return true;
        case FieldType::Vec2: *static_cast<Vec2*>(at) = Vec2{v[0], v[1]}; return true;
        case FieldType::Vec3: *static_cast<Vec3*>(at) = Vec3{v[0], v[1], v[2]}; return true;
        case FieldType::Vec4: *static_cast<Vec4*>(at) = Vec4{v[0], v[1], v[2], v[3]}; return true;
        default: return false;
    }
}

// ---------------------------------------------------------------- files

std::shared_ptr<const AnimationClip> sharedAnimationClip(const std::string& path) {
    struct Entry {
        std::shared_ptr<const AnimationClip> clip;
        std::filesystem::file_time_type written;
        std::chrono::steady_clock::time_point looked;
    };
    static std::unordered_map<std::string, Entry> clips;
    auto now = std::chrono::steady_clock::now();
    auto found = clips.find(path);
    if (found != clips.end() && now - found->second.looked < std::chrono::milliseconds(500)) return found->second.clip;
    std::error_code code;
    auto written = std::filesystem::last_write_time(path, code);
    if (found != clips.end() && !code && written == found->second.written) {
        found->second.looked = now;
        return found->second.clip;
    }
    Entry entry;
    entry.written = written;
    entry.looked = now;
    std::string error;
    if (std::optional<std::string> text = text::readFile(path)) {
        if (std::optional<AnimationClip> clip = AnimationClip::parse(*text, &error)) entry.clip = std::make_shared<const AnimationClip>(std::move(*clip));
        else logError("animation: %s: %s", path.c_str(), error.c_str());
    } else if (found == clips.end()) {
        logError("animation: cannot read %s", path.c_str());
    }
    clips[path] = entry;
    return entry.clip;
}

}  // namespace molten
