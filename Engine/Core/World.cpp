#include "Assets/ImportSettings.hpp"
#include "Core/World.hpp"

#include "Assets/MeshData.hpp"

#include "Assets/Gltf.hpp"

#include <algorithm>

#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Game/Component.hpp"
#include "Rendering/Particles.hpp"
#include "Scene/SceneFile.hpp"

namespace molten {

struct World::Materials {
    std::vector<NamedMaterial> list;
    RenderSettings settings;
};

World::World() : materials_(std::make_shared<Materials>()) {}

std::vector<NamedMaterial>& World::materials() {
    return materials_->list;
}

RenderSettings& World::sceneSettings() {
    return materials_->settings;
}

float World::value(const std::string& name) const {
    auto found = values_.find(name);
    return found == values_.end() ? 0.0f : found->second;
}

// ---------------------------------------------------------------- entities

namespace {

/// Copies the components of one entity to another: the engine's own, then the game's.
void copyComponents(Scene& scene, Entity from, Entity to) {
    // Components are plain values: copying an entity is copying each one. A copy is made
    // first because adding may move the storage the source lives in.
    if (const Tag* c = scene.get<Tag>(from)) {
        Tag value = *c;
        scene.add(to, value);
    }
    if (const DisabledComponents* c = scene.get<DisabledComponents>(from)) {
        DisabledComponents value = *c;
        scene.add(to, value);
    }
    if (const MeshRenderer* c = scene.get<MeshRenderer>(from)) {
        MeshRenderer value = *c;
        scene.add(to, value);
    }
    if (const Light* c = scene.get<Light>(from)) {
        Light value = *c;
        scene.add(to, value);
    }
    if (const Camera* c = scene.get<Camera>(from)) {
        Camera value = *c;
        value.isActive = false;         // two cameras cannot both be the one in use
        scene.add(to, value);
    }
    if (const Collider* c = scene.get<Collider>(from)) {
        Collider value = *c;
        scene.add(to, value);
    }
    if (const RigidBody* c = scene.get<RigidBody>(from)) {
        RigidBody value = *c;
        value.isInitialized = false;    // physics takes its place from the transform again
        scene.add(to, value);
    }
    if (const ParticleEmitter* c = scene.get<ParticleEmitter>(from)) {
        ParticleEmitter value = *c;
        value.particles.clear();
        scene.add(to, value);
    }
    for (const ComponentType& type : ComponentRegistry::instance().types()) type.copy(scene, from, to);
    if (const UnknownComponents* c = scene.get<UnknownComponents>(from)) {
        UnknownComponents value = *c;
        scene.add(to, value);
    }
}

Entity cloneTree(Scene& scene, Entity entity, Entity newParent) {
    const Transform* source = scene.get<Transform>(entity);
    if (!source) return Entity{};
    // The children are noted before the copy exists, or the copy's own children would be found too.
    std::vector<Entity> children;
    scene.each<Transform>([&](Entity candidate, Transform& transform) {
        if (transform.parent == entity) children.push_back(candidate);
    });
    const Name* name = scene.get<Name>(entity);
    Entity copy = scene.create(name ? name->value : std::string());
    Transform transform = *source;
    transform.parent = newParent;
    scene.add(copy, transform);
    copyComponents(scene, entity, copy);
    for (Entity child : children) cloneTree(scene, child, copy);
    return copy;
}

}  // namespace

Entity World::clone(Entity original) {
    const Transform* transform = scene.get<Transform>(original);
    if (!transform) return Entity{};
    return cloneTree(scene, original, transform->parent);
}

Entity World::spawnFromText(const std::string& prefabText, Vec3 position) {
    SceneAssets assets;
    assets.mesh = [this](const std::string& name) { return loadMesh(name); };
    assets.texture = [this](const std::string& name, TextureFormat format) { return loadTexture(name, format); };
    assets.material = [this](const std::string& name) { return loadMaterial(name); };
    // A prefab is a scene file with a few entities in it. Its materials stay alive through
    // the meshes that use them; its settings are not the scene's and are dropped.
    std::vector<NamedMaterial> prefabMaterials;
    RenderSettings ignored;
    std::vector<Entity> created;
    if (!readScene(prefabText, scene, prefabMaterials, ignored, assets, &created) || created.empty()) return Entity{};
    // The root is the first entity without a parent; it goes to `position` and takes the others with it.
    Entity root = created.front();
    for (Entity entity : created) {
        const Transform* transform = scene.get<Transform>(entity);
        if (transform && !scene.isAlive(transform->parent)) {
            root = entity;
            break;
        }
    }
    if (Transform* transform = scene.get<Transform>(root)) transform->position = position;
    scene.updateTransforms();
    return root;
}

Entity World::spawn(const std::string& prefab, Vec3 position) {
    // Prefabs are spawned again and again (bullets, enemies): the file is read once.
    static std::unordered_map<std::string, std::string> cache;
    std::string path = assetPath(prefab);
    auto found = cache.find(path);
    if (found == cache.end()) {
        std::optional<std::string> contents = text::readFile(path);
        if (!contents) {
            logError("there is no prefab %s", prefab.c_str());
            contents = std::string();
        }
        found = cache.emplace(path, std::move(*contents)).first;
    }
    return found->second.empty() ? Entity{} : spawnFromText(found->second, position);
}

uint32_t World::startSound(const std::string& name, float volume, float pitch, bool loops) {
    uint32_t sound = beginSound(name, volume, pitch, loops);
    if (sound != 0) startedSounds_.push_back(sound);
    return sound;
}

void World::changeSound(uint32_t sound, float volume, float pitch) {
    if (sound != 0) adjustSound(sound, volume, pitch);
}

void World::stopSound(uint32_t sound) {
    if (sound == 0) return;
    endSound(sound);
    startedSounds_.erase(std::remove(startedSounds_.begin(), startedSounds_.end(), sound), startedSounds_.end());
}

void World::stopSounds() {
    for (uint32_t sound : startedSounds_) endSound(sound);
    startedSounds_.clear();
}

void World::loadInputMap() {
    std::optional<std::string> contents = text::readFile(assetPath("Input.actions"));
    input.apply(contents ? InputMap::fromText(*contents) : InputMap::standard());
}

std::shared_ptr<Material> World::loadMaterial(const std::string& name) {
    std::optional<std::string> contents = text::readFile(assetPath(name));
    auto material = std::make_shared<Material>();
    material->assetName = name;
    if (!contents || !readMaterial(*contents, *material)) return nullptr;
    return material;
}

// ---------------------------------------------------------------- scenes

bool World::loadSceneNow(const std::string& name) {
    if (sceneName_.empty()) loadInputMap();     // the first scene of a game: its keys come with it
    std::optional<std::string> contents = text::readFile(assetPath(name));
    if (!contents) {
        logError("there is no scene %s", name.c_str());
        return false;
    }
    return loadSceneText(name, *contents);
}

bool World::loadSceneText(const std::string& name, const std::string& sceneText) {
    SceneAssets assets;
    assets.mesh = [this](const std::string& meshName) { return loadMesh(meshName); };
    assets.texture = [this](const std::string& textureName, TextureFormat format) { return loadTexture(textureName, format); };
    assets.material = [this](const std::string& materialName) { return loadMaterial(materialName); };
    std::string contents = sceneText;       // a copy: the text may be the one this world holds
    stopSounds();                           // what was sounding belonged to the scene that is leaving
    scene.clear();
    physics.reset();
    materials_->settings = RenderSettings{};
    bool isRead = readScene(contents, scene, materials_->list, materials_->settings, assets);
    sceneName_ = name;
    sceneText_ = contents;
    restartComponents();
    scene.updateTransforms();
    sceneWasLoaded(contents);
    if (onSceneLoaded) onSceneLoaded();
    return isRead;
}

void World::applyPendingScene() {
    if (pendingScene_.empty() && !pendingReload_) return;
    std::string name = std::move(pendingScene_);
    pendingScene_.clear();
    bool isReload = pendingReload_ && name.empty();
    pendingReload_ = false;
    if (pendingClearsValues_) values_.clear();
    pendingClearsValues_ = false;
    if (!isReload) {
        loadSceneNow(name);
    } else if (!sceneText_.empty()) {
        loadSceneText(sceneName_, sceneText_);
    } else {
        logError("reloadScene: no scene was loaded, so there is none to go back to");
    }
}

// ---------------------------------------------------------------- components

bool World::runs(const std::string& typeName, Entity entity) {
    const Transform* transform = scene.get<Transform>(entity);
    if (transform && !transform->isActiveInHierarchy) return false;
    const DisabledComponents* disabled = scene.get<DisabledComponents>(entity);
    return !disabled || !disabled->has(typeName);
}

Entity World::findWithTag(const std::string& tag) const {
    Entity found;
    const_cast<Scene&>(scene).each<Tag>([&](Entity entity, Tag& entityTag) {
        if (!found.isValid() && entityTag.value == tag) found = entity;
    });
    return found;
}

std::vector<Entity> World::findAllWithTag(const std::string& tag) const {
    std::vector<Entity> found;
    const_cast<Scene&>(scene).each<Tag>([&](Entity entity, Tag& entityTag) {
        if (entityTag.value == tag) found.push_back(entity);
    });
    return found;
}

void World::runComponents(bool isFixedStep, float dt, bool isLate) {
    if (!runsComponents) return;
    // By index, and a copy of each type: a component's code may load a game module or
    // declare types, which moves the list.
    for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
        ComponentType type = ComponentRegistry::instance().types()[i];
        if (isLate ? !type.lateUpdate : (!type.start && !type.update && !type.fixedUpdate)) continue;
        // First note who has one, then look each up again: the code may create or destroy
        // entities and components, its own included.
        scratchEntities_.clear();
        type.collect(scene, scratchEntities_);
        std::unordered_set<uint64_t>& started = startedComponents_[type.name];
        std::vector<Entity> entities = scratchEntities_;
        for (Entity entity : entities) {
            void* component = type.get(scene, entity);
            if (!component || !runs(type.name, entity)) continue;
            uint64_t key = (uint64_t(entity.index) << 32) | entity.generation;
            if (started.insert(key).second && type.start) {
                type.start(component, *this, entity);
                component = type.get(scene, entity);        // start() may have moved it
                if (!component) continue;
            }
            if (isLate) {
                type.lateUpdate(component, *this, entity, dt);
            } else if (isFixedStep) {
                if (type.fixedUpdate) type.fixedUpdate(component, *this, entity, dt);
            } else if (type.update) {
                type.update(component, *this, entity, dt);
            }
            if (!pendingScene_.empty() || pendingReload_) return;   // the scene is about to be replaced: stop here
        }
    }
}

void World::dispatchPhysicsEvents() {
    std::vector<Contact> contacts = physics.takeBegunContacts();
    std::vector<std::pair<Entity, Entity>> triggers = physics.takeEnteredTriggers();
    if (!runsComponents || (contacts.empty() && triggers.empty())) return;
    for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
        ComponentType type = ComponentRegistry::instance().types()[i];
        // Each of the two entities hears about the other.
        auto tell = [&](void (*function)(void*, World&, Entity, Entity), Entity self, Entity other) {
            if (!function || !scene.isAlive(self) || !scene.isAlive(other)) return;
            if (!runs(type.name, self)) return;
            if (void* component = type.get(scene, self)) function(component, *this, self, other);
        };
        for (const Contact& contact : contacts) {
            tell(type.onCollision, contact.a, contact.b);
            tell(type.onCollision, contact.b, contact.a);
        }
        for (const auto& [trigger, other] : triggers) {
            tell(type.onTriggerEnter, trigger, other);
            tell(type.onTriggerEnter, other, trigger);
        }
    }
}

void World::drawComponents(Canvas& canvas) {
    for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
        ComponentType type = ComponentRegistry::instance().types()[i];
        if (!type.draw) continue;
        // While the game is not running (an editor, between plays) only the interface is
        // drawn: it has to be seen to be laid out. The rest draws what it does while it runs.
        if (!runsComponents && type.category != "UI") continue;
        scratchEntities_.clear();
        type.collect(scene, scratchEntities_);
        std::vector<Entity> entities = scratchEntities_;
        for (Entity entity : entities) {
            if (!runs(type.name, entity)) continue;
            if (void* component = type.get(scene, entity)) type.draw(component, *this, entity, canvas);
        }
    }
}

void World::advance(double realDelta) {
    // What was created or moved since the last frame (by an editor, by code between frames)
    // has its place in the world before any component asks for it.
    scene.updateTransforms();
    int steps = time.beginFrame(realDelta);
    for (int i = 0; i < steps; i++) {
        runComponents(true, time.fixedDelta);
        if (!isSimulationPaused) {
            prepareMeshColliders();
            physics.step(scene, time.fixedDelta);
            dispatchPhysicsEvents();
        }
        scene.flushDestroyed();
    }
    if (!isSimulationPaused) physics.writeTransforms(scene, time.alpha);
    runComponents(false, time.delta);
    scene.flushDestroyed();
    scene.updateTransforms();
    runComponents(false, time.delta, true);     // lateUpdate: cameras that follow, with everything where it will be drawn
    scene.updateTransforms();
    updateParticles(scene, time.delta, materials_->settings.wind, materials_->settings.windGustiness, time.elapsed);
    applyPendingScene();
    input.endFrame();
}

std::optional<MeshData> World::readMeshData(const std::string& name) const {
    if (name == "cube") return MeshData::cube();
    if (name == "plane") return MeshData::plane();
    if (name == "sphere") return MeshData::sphere();
    if (name.empty()) return std::nullopt;
    if (isGltfFile(name)) {
        int part = -1;
        std::string file = gltfFileOf(name, &part);
        std::optional<GltfModel> model = GltfModel::load(assetPath(file));
        if (!model) return std::nullopt;
        MeshData data;
        if (part < 0) {
            data = model->assembled(model->restPose());
        } else {
            std::vector<GltfModel::Part> parts = model->parts();
            if (size_t(part) >= parts.size()) return std::nullopt;
            data = model->part(parts[size_t(part)]);
        }
        ImportSettings::load(assetPath(file)).apply(data);
        return data;
    }
    std::optional<std::string> text = text::readFile(assetPath(name));
    std::optional<MeshData> data = text ? MeshData::parseOBJ(*text) : std::nullopt;
    // As the file's import settings say: what is solid is the shape that is drawn.
    if (data) ImportSettings::load(assetPath(name)).apply(*data);
    return data;
}

void World::prepareMeshColliders() {
    scene.each<Collider, Transform>([&](Entity, Collider& collider, Transform& transform) {
        if (collider.shape != ColliderShape::Mesh) return;
        Vec3 scale = transform.scale;
        if (collider.mesh && simd::length_squared(scale - collider.meshScale) < 1e-10f) return;
        if (!collider.mesh && simd::length_squared(collider.meshScale) > 0 && simd::length_squared(scale - collider.meshScale) < 1e-10f) return;   // tried, and there is no such model
        collider.meshScale = scale;
        collider.mesh = nullptr;
        std::optional<MeshData> data = readMeshData(collider.meshName);
        if (!data) {
            logError("mesh collider: cannot read the model \"%s\"", collider.meshName.c_str());
            return;
        }
        collider.mesh = MeshShape::build(data->positions, data->indices, scale);
    });
}

}  // namespace molten
