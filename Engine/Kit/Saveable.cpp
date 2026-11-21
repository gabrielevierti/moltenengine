// Saveable.cpp
#include "Kit/Saveable.hpp"

#include "Core/Log.hpp"
#include "Game/Component.hpp"

#include <map>
#include <set>
#include <sstream>

namespace molten::kit {

void linkSaveable() {}

namespace {

/// Each saveable entity with the name it is saved by: its id, or its path of names; the same
/// path twice is told apart by order ("Crate", "Crate#2").
std::vector<std::pair<std::string, Entity>> saveables(World& world) {
    std::vector<std::pair<std::string, Entity>> out;
    std::map<std::string, int> seen;
    world.scene.each<Saveable>([&](Entity entity, Saveable& saveable) {
        std::string key = saveable.id;
        if (key.empty()) {
            for (Entity at = entity; world.scene.isAlive(at);) {
                const Name* name = world.scene.get<Name>(at);
                key = (name ? name->value : std::string("?")) + (key.empty() ? "" : "/" + key);
                const Transform* transform = world.scene.get<Transform>(at);
                if (!transform) break;
                at = transform->parent;
            }
        }
        int count = ++seen[key];
        if (count > 1) key += "#" + std::to_string(count);
        out.push_back({key, entity});
    });
    return out;
}

/// Components whose values are not the game's to keep: where it is (kept apart), how it is
/// drawn, and the Saveable itself.
bool isKeptApart(const std::string& type) {
    return type == "Saveable" || type == "Transform" || type == "Name" || type == "Tag";
}

}  // namespace

SaveGame captureState(World& world) {
    SaveGame save;
    save.setText("state/scene", world.sceneName());
    for (const auto& [name, value] : world.values()) save.setNumber("value/" + name, value);
    for (const auto& [name, value] : world.texts()) save.setText("words/" + name, value);
    std::string present;
    for (const auto& [key, entity] : saveables(world)) {
        const Saveable* saveable = world.scene.get<Saveable>(entity);
        const Transform* transform = world.scene.get<Transform>(entity);
        present += key + "\n";
        std::string at = "entity/" + key + "/";
        if (transform) {
            save.setNumber(at + "active", transform->isActive ? 1.0f : 0.0f);
            if (saveable->keepsPlace) {
                save.setVector(at + "position", transform->position);
                save.setVector(at + "rotation", toEulerAngles(transform->rotation));
                save.setVector(at + "scale", transform->scale);
            }
        }
        if (!saveable->keepsValues) continue;
        for (const ComponentType& type : ComponentRegistry::instance().types()) {
            if (isKeptApart(type.name) || !type.get) continue;
            void* component = type.get(world.scene, entity);
            if (!component) continue;
            for (const Field& field : type.fields) save.setText(at + type.name + "/" + field.name, field.valueText(component));
        }
    }
    save.setText("state/present", present);
    return save;
}

bool restoreState(World& world, const SaveGame& save) {
    if (!save.has("state/present")) return false;
    // The shared values as they were (those set since are let go).
    world.clearValues();
    for (const std::string& name : save.names()) {
        if (name.rfind("value/", 0) == 0) world.setValue(name.substr(6), save.number(name));
        else if (name.rfind("words/", 0) == 0) world.setText(name.substr(6), save.text(name));
    }
    std::set<std::string> present;
    std::istringstream lines(save.text("state/present"));
    std::string line;
    while (std::getline(lines, line)) present.insert(line);
    for (const auto& [key, entity] : saveables(world)) {
        // Not there when it was saved: it had been destroyed (a coin picked up, a crate broken).
        if (!present.count(key)) {
            world.scene.destroy(entity);
            continue;
        }
        std::string at = "entity/" + key + "/";
        Transform* transform = world.scene.get<Transform>(entity);
        if (transform) {
            if (save.has(at + "active")) transform->isActive = save.number(at + "active") > 0.5f;
            if (save.has(at + "position")) {
                transform->position = save.vector(at + "position");
                Vec3 angles = save.vector(at + "rotation");
                transform->rotation = eulerAngles(angles.x, angles.y, angles.z);
                transform->scale = save.vector(at + "scale", transform->scale);
                // A body that moves goes there too, not back to where physics had it.
                if (world.scene.has<RigidBody>(entity)) {
                    world.scene.updateTransforms();
                    world.physics.teleport(world.scene, entity, transform->worldPosition());
                }
            }
        }
        for (const ComponentType& type : ComponentRegistry::instance().types()) {
            if (isKeptApart(type.name) || !type.get) continue;
            void* component = type.get(world.scene, entity);
            if (!component) continue;
            for (const Field& field : type.fields) {
                std::string name = at + type.name + "/" + field.name;
                if (save.has(name)) field.setValueText(component, save.text(name));
            }
        }
    }
    world.scene.flushDestroyed();
    world.scene.updateTransforms();
    return true;
}

bool saveState(World& world, const std::string& slot) {
    return world.saveGame(captureState(world), slot);
}

bool loadState(World& world, const std::string& slot) {
    std::optional<SaveGame> save = world.loadGame(slot);
    if (!save) return false;
    // The scene as it starts, always (what was picked up since the save comes back), at the
    // end of this frame: the components running now keep their scene until then.
    std::string scene = save->text("state/scene");
    if (scene.empty()) return restoreState(world, *save);
    SaveGame kept = *save;
    world.loadScene(scene);
    world.afterSceneLoads([&world, kept] { restoreState(world, kept); });
    return true;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(Saveable, MOLTEN_FIELD(id).help("What it is known by in the save; empty: its path of names"),
                 MOLTEN_FIELD(keepsPlace).help("Where it is, how it is turned and sized"),
                 MOLTEN_FIELD(keepsValues).help("The values of its other components"))
