#include "MultiEdit.hpp"

#include "Game/Component.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace molten {

namespace {

// The lines of the built-in components, and how each is copied from one entity to another.
const std::set<std::string> kComponentLines = {"camera", "light", "mesh", "collider", "body", "particles", "particleLook", "particleSet"};

/// The component a line belongs to, for adding it to and removing it from another entity.
std::string componentOfLine(const std::string& key) {
    if (key.rfind("particle", 0) == 0) return "particles";
    return key;
}

template <typename T>
void copyOne(Scene& from, Entity a, Scene& to, Entity b) {
    if (const T* value = from.get<T>(a)) to.add(b, *value);
}

void copyBuiltIn(const std::string& component, Scene& from, Entity a, Scene& to, Entity b) {
    if (component == "camera") copyOne<Camera>(from, a, to, b);
    else if (component == "light") copyOne<Light>(from, a, to, b);
    else if (component == "mesh") copyOne<MeshRenderer>(from, a, to, b);
    else if (component == "collider") copyOne<Collider>(from, a, to, b);
    else if (component == "body") copyOne<RigidBody>(from, a, to, b);
    else if (component == "particles") copyOne<ParticleEmitter>(from, a, to, b);
}

void removeBuiltIn(const std::string& component, Scene& scene, Entity entity) {
    if (component == "camera") scene.remove<Camera>(entity);
    else if (component == "light") scene.remove<Light>(entity);
    else if (component == "mesh") scene.remove<MeshRenderer>(entity);
    else if (component == "collider") scene.remove<Collider>(entity);
    else if (component == "body") scene.remove<RigidBody>(entity);
    else if (component == "particles") scene.remove<ParticleEmitter>(entity);
}

std::string vecText(Vec3 v) {
    char text[96];
    std::snprintf(text, sizeof(text), "%g %g %g", double(v.x), double(v.y), double(v.z));
    return text;
}

/// Three numbers, if the text is three numbers.
bool threeNumbers(const std::string& text, float out[3]) {
    std::istringstream stream(text);
    for (int i = 0; i < 3; i++) {
        if (!(stream >> out[i])) return false;
    }
    std::string rest;
    return !(stream >> rest);
}

}  // namespace

std::vector<std::string> splitWords(const std::string& line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= line.size()) break;
        size_t start = i;
        if (line[i] == '"') {
            for (i++; i < line.size() && line[i] != '"'; i++) {
                if (line[i] == '\\' && i + 1 < line.size()) i++;
            }
            if (i < line.size()) i++;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
        }
        words.push_back(line.substr(start, i - start));
    }
    return words;
}

EntityPicture pictureOf(Scene& scene, Entity entity, const std::vector<NamedMaterial>& materials) {
    EntityPicture picture;
    if (const Transform* transform = scene.get<Transform>(entity)) {
        picture.values["Transform\nposition"] = vecText(transform->position);
        picture.values["Transform\nrotation"] = vecText(toEulerAngles(transform->rotation) * (180.0f / kPi));
        picture.values["Transform\nscale"] = vecText(transform->scale);
        picture.values["Transform\nactive"] = transform->isActive ? "1" : "0";
    }
    if (const MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && renderer->material && !renderer->material->assetName.empty()) {
        picture.values["MeshRenderer\nmaterial"] = renderer->material->assetName;
    }
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        void* component = type.get ? type.get(scene, entity) : nullptr;
        if (!component) continue;
        picture.values[type.name + "\n"] = "";             // that it has one
        for (const Field& field : type.fields) picture.values[type.name + "\n" + field.name] = field.valueText(component);
    }
    std::vector<Entity> only{entity};
    std::istringstream text(writeScene(scene, materials, RenderSettings{}, &only));
    std::string line;
    while (std::getline(text, line)) {
        std::vector<std::string> words = splitWords(line);
        if (words.empty() || !kComponentLines.count(words[0])) continue;
        picture.lines[words[0]] = std::vector<std::string>(words.begin() + 1, words.end());
    }
    return picture;
}

int spreadChanges(Scene& scene, const EntityPicture& before, const EntityPicture& after, Entity edited, const std::vector<Entity>& others,
                  const std::vector<NamedMaterial>& materials, const SceneAssets& assets) {
    int changes = 0;
    std::vector<Entity> targets;
    for (Entity other : others) {
        if (other != edited && scene.isAlive(other)) targets.push_back(other);
    }
    if (targets.empty()) return 0;
    ComponentRegistry& registry = ComponentRegistry::instance();

    // ---- declared components, added and removed
    for (const auto& [key, value] : after.values) {
        if (key.back() != '\n' || before.values.count(key)) continue;
        const ComponentType* type = registry.find(key.substr(0, key.size() - 1));
        if (!type) continue;
        for (Entity other : targets) {
            if (!type->has(scene, other)) {
                type->add(scene, other);
                changes++;
            }
        }
    }
    for (const auto& [key, value] : before.values) {
        if (key.back() != '\n' || after.values.count(key)) continue;
        const ComponentType* type = registry.find(key.substr(0, key.size() - 1));
        if (!type) continue;
        for (Entity other : targets) {
            if (type->has(scene, other)) {
                type->remove(scene, other);
                changes++;
            }
        }
    }

    // ---- values: the transform, and the declared components' fields
    for (const auto& [key, value] : after.values) {
        if (key.back() == '\n') continue;
        auto was = before.values.find(key);
        if (was == before.values.end() || was->second == value) continue;
        size_t split = key.find('\n');
        std::string component = key.substr(0, split), field = key.substr(split + 1);
        float old3[3], new3[3];
        bool isVector = threeNumbers(was->second, old3) && threeNumbers(value, new3);
        for (Entity other : targets) {
            std::string theirs = value;
            if (isVector) {
                // Only the numbers that changed: moving along x keeps each one's own y and z.
                std::string current;
                if (component == "Transform") {
                    const Transform* transform = scene.get<Transform>(other);
                    if (!transform) continue;
                    current = field == "position" ? vecText(transform->position)
                              : field == "scale"  ? vecText(transform->scale)
                                                  : vecText(toEulerAngles(transform->rotation) * (180.0f / kPi));
                } else if (const ComponentType* type = registry.find(component)) {
                    void* instance = type->get ? type->get(scene, other) : nullptr;
                    const Field* declared = type->findField(field);
                    if (!instance || !declared) continue;
                    current = declared->valueText(instance);
                }
                float mine[3];
                if (threeNumbers(current, mine)) {
                    for (int axis = 0; axis < 3; axis++) {
                        if (std::abs(new3[axis] - old3[axis]) > 1e-6f) mine[axis] = new3[axis];
                    }
                    theirs = vecText(Vec3{mine[0], mine[1], mine[2]});
                }
            }
            if (applyPrefabValue(scene, other, PrefabOverride{"", component, field, theirs}, assets)) changes++;
        }
    }

    // ---- the built-in components, word by word of their lines
    std::set<std::string> components;
    for (const auto& [key, words] : after.lines) components.insert(componentOfLine(key));
    for (const auto& [key, words] : before.lines) components.insert(componentOfLine(key));
    for (const std::string& component : components) {
        auto has = [&](const EntityPicture& picture) {
            for (const auto& [key, words] : picture.lines) {
                if (componentOfLine(key) == component) return true;
            }
            return false;
        };
        bool had = has(before), has_ = has(after);
        if (!had && !has_) continue;
        if (!has_) {
            for (Entity other : targets) removeBuiltIn(component, scene, other), changes++;
            continue;
        }
        if (!had) {
            for (Entity other : targets) copyBuiltIn(component, scene, edited, scene, other), changes++;
            continue;
        }
        // Which words changed, line by line.
        std::map<std::string, std::vector<size_t>> changed;
        for (const auto& [key, words] : after.lines) {
            if (componentOfLine(key) != component) continue;
            auto was = before.lines.find(key);
            if (was == before.lines.end() || was->second.size() != words.size()) {
                changed[key] = {};                          // all of it
                for (size_t i = 0; i < words.size(); i++) changed[key].push_back(i);
                continue;
            }
            for (size_t i = 0; i < words.size(); i++) {
                if (words[i] != was->second[i]) changed[key].push_back(i);
            }
        }
        if (changed.empty()) continue;
        for (Entity other : targets) {
            EntityPicture their = pictureOf(scene, other, materials);
            bool hasIt = false;
            for (const auto& [key, words] : their.lines) hasIt = hasIt || componentOfLine(key) == component;
            if (!hasIt) continue;                           // only those that have it
            // Their lines with the changed words put in, read back into a scratch entity.
            std::string text = "molten-scene 1\nentity \"scratch\"\n";
            bool takesMaterial = false;
            for (const auto& [key, words] : their.lines) {
                if (componentOfLine(key) != component) continue;
                std::vector<std::string> mine = words;
                auto now = after.lines.find(key);
                auto which = changed.find(key);
                if (now != after.lines.end() && which != changed.end()) {
                    if (mine.size() != now->second.size()) mine = now->second;
                    else for (size_t i : which->second) mine[i] = now->second[i];
                    // The material of a mesh line is an index into the file's materials: its own.
                    if (key == "mesh") takesMaterial = std::find(which->second.begin(), which->second.end(), size_t(1)) != which->second.end();
                }
                text += "  " + key;
                for (const std::string& word : mine) text += " " + word;
                text += "\n";
            }
            Scene scratch;
            std::vector<NamedMaterial> scratchMaterials;
            RenderSettings settings;
            std::vector<Entity> created;
            SceneAssets noMaterials = assets;
            if (!readScene(text, scratch, scratchMaterials, settings, noMaterials, &created) || created.empty()) continue;
            if (component == "mesh") {
                // Its material stays its own unless the material is what was changed.
                MeshRenderer* read = scratch.get<MeshRenderer>(created[0]);
                const MeshRenderer* own = scene.get<MeshRenderer>(other);
                const MeshRenderer* source = scene.get<MeshRenderer>(edited);
                if (read && own && source) read->material = takesMaterial ? source->material : own->material;
                if (read && own && !takesMaterial && after.lines.count("mesh") && before.lines.count("mesh") &&
                    after.lines.at("mesh").size() > 0 && after.lines.at("mesh")[0] == before.lines.at("mesh")[0]) {
                    read->mesh = own->mesh;                 // the same model as before: theirs, not one read again
                }
            }
            copyBuiltIn(component, scratch, created[0], scene, other);
            changes++;
        }
    }
    return changes;
}

}  // namespace molten
