#include "Assets/ImportSettings.hpp"
#include "Core/World.hpp"
#include "Kit/Weather.hpp"
#include "Kit/Wakes.hpp"

#include "Audio/Mixer.hpp"

#include "Assets/MeshData.hpp"

#include "Assets/Gltf.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <cstdlib>
#include <cstdio>

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

// ---------------------------------------------------------------- preferences

void World::readPreferences() const {
    if (preferencesReadFrom_ == preferencesFile && (!preferencesFile.empty() || !preferences_.empty())) return;
    if (preferencesReadFrom_ != preferencesFile) preferences_.clear();
    preferencesReadFrom_ = preferencesFile;
    if (preferencesFile.empty()) return;
    std::optional<std::string> text = text::readFile(preferencesFile);
    if (!text) return;
    // One a line: a name, a space, a number.
    size_t at = 0;
    while (at < text->size()) {
        size_t end = text->find('\n', at);
        std::string line = text->substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text->size() : end + 1;
        size_t space = line.rfind(' ');
        if (space == std::string::npos || space == 0) continue;
        preferences_[line.substr(0, space)] = std::strtof(line.c_str() + space + 1, nullptr);
    }
}

float World::preference(const std::string& name, float fallback) const {
    readPreferences();
    auto found = preferences_.find(name);
    return found == preferences_.end() ? fallback : found->second;
}

bool World::hasPreference(const std::string& name) const {
    readPreferences();
    return preferences_.count(name) > 0;
}

void World::setPreference(const std::string& name, float value) {
    readPreferences();
    auto found = preferences_.find(name);
    if (found != preferences_.end() && found->second == value) return;
    preferences_[name] = value;
    if (preferencesFile.empty()) return;
    std::vector<std::string> names;
    for (const auto& entry : preferences_) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    std::string text;
    char number[48];
    for (const std::string& key : names) {
        std::snprintf(number, sizeof(number), " %.9g\n", double(preferences_[key]));
        text += key + number;
    }
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(preferencesFile).parent_path(), code);
    if (!text::writeFile(preferencesFile, text)) logError("cannot save the preferences in %s", preferencesFile.c_str());
}

// ---------------------------------------------------------------- messages

void World::send(const std::string& name, Entity to, float number, const std::string& text, Entity from) {
    send(Message{name, number, text, from, to});
}

void World::send(const Message& message) {
    // A message that makes another that makes the first...: stopped, and said.
    if (sendDepth_ >= 32) {
        logWarning("the message %s was sent from inside itself 32 times: not sent again", message.name.c_str());
        return;
    }
    sendDepth_++;
    // Code that listens (a copy: a listener may listen or stop listening as it hears).
    std::vector<Listener> listening = listeners_;
    for (const Listener& listener : listening) {
        if (listener.name != "*" && listener.name != message.name) continue;
        if (listener.hasOwner && !scene.isAlive(listener.owner)) continue;
        listener.heard(message);
    }
    // Components with onMessage: the one it is for, or every one.
    if (runsComponents) {
        for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
            const ComponentType& declared = ComponentRegistry::instance().types()[i];
            if (!declared.onMessage) continue;
            std::string typeName = declared.name;
            auto heard = declared.onMessage;
            auto get = declared.get;
            std::vector<Entity> hearing;
            if (message.to.isValid()) hearing.push_back(message.to);
            else declared.collect(scene, hearing);
            for (Entity entity : hearing) {
                if (!scene.isAlive(entity) || !runs(typeName, entity)) continue;
                if (void* component = get(scene, entity)) heard(component, *this, entity, message);
            }
        }
    }
    sendDepth_--;
}

int World::listen(const std::string& name, std::function<void(const Message&)> heard, Entity owner) {
    listeners_.push_back(Listener{nextListener_, name, std::move(heard), owner, owner.isValid()});
    return nextListener_++;
}

void World::stopListening(int id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const Listener& listener) { return listener.id == id; }),
                     listeners_.end());
}

// ---------------------------------------------------------------- shared words

const std::string& World::text(const std::string& name) const {
    static const std::string none;
    auto found = texts_.find(name);
    return found == texts_.end() ? none : found->second;
}

void World::readTextPreferences() const {
    if (textPreferencesReadFrom_ == preferencesFile) return;
    textPreferencesReadFrom_ = preferencesFile;
    textPreferences_.clear();
    if (preferencesFile.empty()) return;
    std::optional<std::string> file = text::readFile(preferencesFile + ".texts");
    if (!file) return;
    // One a line: a name, a tab, the words (with \n, \t and \\ for what cannot be on a line).
    size_t at = 0;
    while (at < file->size()) {
        size_t end = file->find('\n', at);
        std::string line = file->substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? file->size() : end + 1;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        std::string value;
        for (size_t i = tab + 1; i < line.size(); i++) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                char next = line[++i];
                value += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            } else {
                value += line[i];
            }
        }
        textPreferences_[line.substr(0, tab)] = value;
    }
}

std::string World::textPreference(const std::string& name, const std::string& fallback) const {
    readTextPreferences();
    auto found = textPreferences_.find(name);
    return found == textPreferences_.end() ? fallback : found->second;
}

bool World::hasTextPreference(const std::string& name) const {
    readTextPreferences();
    return textPreferences_.count(name) > 0;
}

void World::setTextPreference(const std::string& name, const std::string& value) {
    readTextPreferences();
    auto found = textPreferences_.find(name);
    if (found != textPreferences_.end() && found->second == value) return;
    textPreferences_[name] = value;
    if (preferencesFile.empty()) return;
    std::vector<std::string> names;
    for (const auto& entry : textPreferences_) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    std::string out;
    for (const std::string& key : names) {
        out += key + "\t";
        for (char c : textPreferences_[key]) {
            if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        out += "\n";
    }
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(preferencesFile).parent_path(), code);
    if (!text::writeFile(preferencesFile + ".texts", out)) logError("cannot save the preferences in %s.texts", preferencesFile.c_str());
}

// ---------------------------------------------------------------- languages

std::string World::translate(const std::string& text) const {
    if (language.empty() || text.empty()) return text;
    if (translationsFor_ != language) {
        translationsFor_ = language;
        translations_.clear();
        std::optional<std::string> file = text::readFile(assetPath("Localization/" + language + ".txt"));
        if (!file) {
            logWarning("there is no Localization/%s.txt: texts are shown as written", language.c_str());
            return text;
        }
        // "as written" = "translated", with \n for a new line and \" for a quote.
        auto quoted = [](const std::string& line, size_t& at, std::string& out) {
            at = line.find('"', at);
            if (at == std::string::npos) return false;
            out.clear();
            for (at++; at < line.size(); at++) {
                if (line[at] == '\\' && at + 1 < line.size()) {
                    char next = line[++at];
                    out += next == 'n' ? '\n' : next;
                } else if (line[at] == '"') {
                    at++;
                    return true;
                } else {
                    out += line[at];
                }
            }
            return false;
        };
        size_t start = 0;
        while (start < file->size()) {
            size_t end = file->find('\n', start);
            std::string line = file->substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? file->size() : end + 1;
            size_t at = line.find_first_not_of(" \t");
            if (at == std::string::npos || line[at] == '#') continue;          // a comment
            at = 0;
            std::string from, to;
            if (quoted(line, at, from) && line.find('=', at) != std::string::npos && quoted(line, at, to)) translations_[from] = to;
        }
    }
    auto found = translations_.find(text);
    return found == translations_.end() ? text : found->second;
}

std::vector<std::string> World::languages() const {
    std::vector<std::string> found;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(assetPath("Localization"), code)) {
        if (entry.path().extension() == ".txt") found.push_back(entry.path().stem().string());
    }
    std::sort(found.begin(), found.end());
    return found;
}

// ---------------------------------------------------------------- saved games

std::string World::saveFileOf(const std::string& slot) const {
    if (preferencesFile.empty()) return "";
    // A slot's name as a file name: letters, digits, spaces, - and _ kept, the rest made _.
    std::string file;
    for (char c : slot) file += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
    if (file.empty()) file = "Save";
    return (std::filesystem::path(preferencesFile).parent_path() / "Saves" / (file + ".save")).string();
}

bool World::saveGame(const SaveGame& save, const std::string& slot) {
    std::string path = saveFileOf(slot);
    if (path.empty()) {
        savesInMemory_[slot] = save.toText();
        return true;
    }
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), code);
    // Written beside, then put in place: a game that stops halfway leaves the old save whole.
    std::string partial = path + ".partial";
    if (!text::writeFile(partial, save.toText())) {
        logError("cannot save the game in %s", path.c_str());
        return false;
    }
    std::filesystem::rename(partial, path, code);
    if (code) logError("cannot save the game in %s: %s", path.c_str(), code.message().c_str());
    return !code;
}

std::optional<SaveGame> World::loadGame(const std::string& slot) const {
    std::string path = saveFileOf(slot);
    if (path.empty()) {
        auto found = savesInMemory_.find(slot);
        if (found == savesInMemory_.end()) return std::nullopt;
        return SaveGame::fromText(found->second);
    }
    std::optional<std::string> contents = text::readFile(path);
    if (!contents) return std::nullopt;
    return SaveGame::fromText(*contents);
}

bool World::hasSavedGame(const std::string& slot) const {
    std::string path = saveFileOf(slot);
    if (path.empty()) return savesInMemory_.count(slot) > 0;
    std::error_code code;
    return std::filesystem::exists(path, code);
}

bool World::deleteSavedGame(const std::string& slot) {
    std::string path = saveFileOf(slot);
    if (path.empty()) return savesInMemory_.erase(slot) > 0;
    std::error_code code;
    return std::filesystem::remove(path, code);
}

std::vector<std::string> World::savedGames() const {
    std::vector<std::string> slots;
    if (preferencesFile.empty()) {
        for (const auto& entry : savesInMemory_) slots.push_back(entry.first);
    } else {
        std::error_code code;
        std::filesystem::path folder = std::filesystem::path(preferencesFile).parent_path() / "Saves";
        for (const auto& entry : std::filesystem::directory_iterator(folder, code)) {
            if (entry.path().extension() == ".save") slots.push_back(entry.path().stem().string());
        }
    }
    std::sort(slots.begin(), slots.end());
    return slots;
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
    assets.prefabText = [this](const std::string& name) { return text::readFile(assetPath(name)); };
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

Entity World::take(const std::string& prefab, Vec3 position) {
    std::vector<Entity>& waiting = pools_[prefab];
    while (!waiting.empty()) {
        Entity entity = waiting.back();
        waiting.pop_back();
        Pooled* pooled = scene.get<Pooled>(entity);
        Transform* transform = scene.get<Transform>(entity);
        if (!pooled || !transform || !pooled->isWaiting) continue;         // destroyed meanwhile
        pooled->isWaiting = false;
        transform->isActive = true;
        transform->position = position;
        transform->rotation = pooled->rotation;
        transform->scale = pooled->scale;
        scene.updateTransforms();
        if (RigidBody* body = scene.get<RigidBody>(entity)) {
            body->velocity = Vec3{0, 0, 0};
            body->angularVelocity = Vec3{0, 0, 0};
            physics.teleport(scene, entity, position);
        }
        return entity;
    }
    Entity made = spawn(prefab, position);
    if (const Transform* transform = scene.get<Transform>(made)) scene.add(made, Pooled{prefab, transform->rotation, transform->scale, false});
    return made;
}

void World::release(Entity entity) {
    Pooled* pooled = scene.get<Pooled>(entity);
    Transform* transform = scene.get<Transform>(entity);
    if (!pooled || !transform) {
        if (scene.isAlive(entity)) scene.destroy(entity);
        return;
    }
    if (pooled->isWaiting) return;
    pooled->isWaiting = true;
    transform->isActive = false;
    // When it comes back, its components start again; what it waited for is forgotten.
    std::vector<Entity> family = {entity};
    for (size_t i = 0; i < family.size(); i++) {
        scene.each<Transform>([&](Entity child, Transform& place) {
            if (place.parent == family[i]) family.push_back(child);
        });
    }
    for (Entity member : family) {
        uint64_t key = (uint64_t(member.index) << 32) | member.generation;
        for (auto& [type, started] : startedComponents_) started.erase(key);
        tasks.cancelOwnedBy(member);
    }
    pools_[pooled->prefab].push_back(entity);
}

size_t World::waitingInPool(const std::string& prefab) const {
    auto found = pools_.find(prefab);
    return found == pools_.end() ? 0 : found->second.size();
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

uint32_t World::startSound(const std::string& name, float volume, float pitch, bool loops, const std::string& group) {
    uint32_t sound = beginSound(name, volume * soundVolume, pitch, loops);
    if (sound != 0) {
        startedSounds_.push_back(sound);
        if (!group.empty()) groupSound(sound, group);
    }
    return sound;
}

void World::setVolume(const std::string& group, float volume) {
    GroupMix& mix = groupMix_[group.empty() ? "Effects" : group];
    mix.volume = std::clamp(volume, 0.0f, 1.0f);
    mixGroup(group.empty() ? "Effects" : group, mix.volume, mix.isMuted);
}

float World::volumeOf(const std::string& group) const {
    auto found = groupMix_.find(group.empty() ? "Effects" : group);
    return found == groupMix_.end() ? 1.0f : found->second.volume;
}

void World::setMuted(const std::string& group, bool isMuted) {
    GroupMix& mix = groupMix_[group.empty() ? "Effects" : group];
    mix.isMuted = isMuted;
    mixGroup(group.empty() ? "Effects" : group, mix.volume, mix.isMuted);
}

void World::duck(const std::string& group, const std::string& under, float to) {
    duckGroup(group, under, to);
}

void World::askForRoom(float weight, float size, float damping, float wet) {
    if (weight <= roomWish_[0]) return;
    roomWish_[0] = weight;
    roomWish_[1] = size;
    roomWish_[2] = damping;
    roomWish_[3] = wet * std::clamp(weight, 0.0f, 1.0f);
}

void World::hearRoom() {
    // The room most asked for this frame; none: dry. Said to the mixer only when it changes.
    float size = roomWish_[0] > 0 ? roomWish_[1] : roomNow_[0];
    float damping = roomWish_[0] > 0 ? roomWish_[2] : roomNow_[1];
    float wet = roomWish_[0] > 0 ? roomWish_[3] : 0.0f;
    if (std::abs(size - roomNow_[0]) > 1e-4f || std::abs(damping - roomNow_[1]) > 1e-4f || std::abs(wet - roomNow_[2]) > 1e-4f) {
        roomNow_[0] = size, roomNow_[1] = damping, roomNow_[2] = wet;
        setRoom(size, damping, wet);
    }
    roomWish_[0] = roomWish_[1] = roomWish_[2] = roomWish_[3] = 0;
}

float World::muffleFor(Vec3 from, Vec3 position) {
    Vec3 toSound = position - from;
    float distance = simd::length(toSound);
    if (distance < 1.0f) return 0;
    std::optional<RaycastHit> hit = physics.raycast(scene, Ray{from, toSound / distance}, distance);
    // A wall: something that does not move, at least a meter before the sound (not what makes it).
    if (!hit || hit->distance > distance - 1.0f || scene.get<RigidBody>(hit->entity)) return 0;
    return 0.8f;
}

void World::changeSound(uint32_t sound, float volume, float pitch) {
    if (sound == 0) return;
    auto placed = soundPlaces_.find(sound);
    if (placed == soundPlaces_.end()) {
        adjustSound(sound, volume * soundVolume, pitch);
        return;
    }
    placed->second.volume = volume;
    placed->second.pitch = pitch;
    voiceSound(sound, volume, pitch);
}

void World::placeSound(uint32_t sound, Vec3 position, float fullWithin, float silentBeyond) {
    if (sound == 0) return;
    SoundPlace& place = soundPlaces_[sound];
    place.position = position;
    place.fullWithin = fullWithin;
    place.silentBeyond = std::max(silentBeyond, fullWithin + 0.01f);
    if (place.volume >= 0) voiceSound(sound, place.volume, place.pitch);
}

void World::voiceSound(uint32_t sound, float volume, float pitch) {
    const SoundPlace& place = soundPlaces_[sound];
    Mat4 ears;
    if (!listener(ears)) {
        adjustSound(sound, volume * soundVolume, pitch);
        return;
    }
    SpatialGain gain = spatialize(ears, place.position, place.fullWithin, place.silentBeyond);
    adjustSound(sound, volume * soundVolume * gain.volume, pitch);
    panSound(sound, gain.pan);
    float muffle = muffledByWalls && gain.volume > 0 ? muffleFor(xyz(ears.columns[3]), place.position) : 0.0f;
    if (muffle != soundPlaces_[sound].muffle) {
        soundPlaces_[sound].muffle = muffle;
        muffleSound(sound, muffle);
    }
}

void World::stopSound(uint32_t sound) {
    if (sound == 0) return;
    endSound(sound);
    soundPlaces_.erase(sound);
    startedSounds_.erase(std::remove(startedSounds_.begin(), startedSounds_.end(), sound), startedSounds_.end());
}

void World::stopSounds() {
    for (uint32_t sound : startedSounds_) endSound(sound);
    startedSounds_.clear();
    soundPlaces_.clear();
}

void World::applySettings(const GameSettings& wanted) {
    settings = wanted;
    physics.gravity = settings.gravity;
    physics.layerMeets = settings.meets;
    time.fixedDelta = 1.0f / float(std::clamp(settings.stepsPerSecond, 20, 240));
}

void World::loadSettings(const std::string& settingsDirectory) {
    std::optional<std::string> contents = text::readFile(settingsDirectory + "/Game.settings");
    applySettings(contents ? GameSettings::parse(*contents) : GameSettings{});
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
    assets.prefabText = [this](const std::string& prefabName) { return text::readFile(assetPath(prefabName)); };
    std::string contents = sceneText;       // a copy: the text may be the one this world holds
    endScene();                             // its components hear they are going; its sounds, tasks and pools go with it
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

// ---------------------------------------------------------------- scenes in the background, scenes added

void World::loadSceneInBackground(const std::string& name, bool isAdded) {
    if (loading_.valid()) {
        logWarning("%s is still loading: %s is not started", loadingName_.c_str(), name.c_str());
        return;
    }
    loadingName_ = name;
    loadingIsAdded_ = isAdded;
    loadingProgress_ = std::make_shared<SceneAheadProgress>();
    setValue("isLoading", 1);
    setValue("loading", 0);
    // Only files are read there, models made into triangles and pictures into pixels (see
    // SceneAhead.hpp): nothing of the scene that is open.
    loading_ = std::async(std::launch::async, [name, path = assetPath(name), pathOf = assetPathFinder(), progress = loadingProgress_]() {
        return readSceneAhead(name, path, pathOf, progress.get());
    });
}

float World::sceneLoadProgress() const {
    if (!loading_.valid() || !loadingProgress_) return 1.0f;
    return std::clamp(loadingProgress_->fraction(), 0.0f, 0.99f);
}

void World::finishBackgroundLoad() {
    SceneAhead loaded = loading_.get();
    for (SceneAhead::Model& model : loaded.models) preloadMesh(model.name, std::move(model.data));
    for (SceneAhead::Picture& picture : loaded.pictures) preloadPicture(picture.name, std::move(picture.pixels), picture.width, picture.height);
    if (!loaded.isRead) {
        logError("there is no scene %s", loaded.name.c_str());
    } else if (loadingIsAdded_) {
        addSceneText(loaded.name, loaded.text);
    } else {
        loadSceneText(loaded.name, loaded.text);
    }
    setValue("loading", 1);
    setValue("isLoading", 0);
    std::vector<std::function<void()>> then = std::move(afterScene_);
    afterScene_.clear();
    for (const std::function<void()>& action : then) action();
}

Entity World::addScene(const std::string& name) {
    std::optional<std::string> contents = text::readFile(assetPath(name));
    if (!contents) {
        logError("there is no scene %s", name.c_str());
        return Entity{};
    }
    return addSceneText(name, *contents);
}

Entity World::addSceneText(const std::string& name, const std::string& sceneText) {
    SceneAssets assets;
    assets.mesh = [this](const std::string& meshName) { return loadMesh(meshName); };
    assets.texture = [this](const std::string& textureName, TextureFormat format) { return loadTexture(textureName, format); };
    assets.material = [this](const std::string& materialName) { return loadMaterial(materialName); };
    assets.prefabText = [this](const std::string& prefabName) { return text::readFile(assetPath(prefabName)); };
    std::vector<NamedMaterial> materials;      // alive through the meshes that use them
    RenderSettings ignored;                    // the light and sky stay the open scene's
    std::vector<Entity> created;
    if (!readScene(sceneText, scene, materials, ignored, assets, &created)) return Entity{};
    Entity holder = scene.create("Scene: " + name);
    scene.add(holder, Transform{});
    scene.add(holder, AddedScene{name});
    for (Entity entity : created) {
        Transform* transform = scene.get<Transform>(entity);
        if (transform && !scene.isAlive(transform->parent)) transform->parent = holder;
    }
    scene.updateTransforms();
    return holder;
}

void World::removeScene(const std::string& name) {
    scene.each<AddedScene>([&](Entity entity, AddedScene& added) {
        if (added.name == name) scene.destroy(entity);
    });
}

void World::applyPendingScene() {
    if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) finishBackgroundLoad();
    else if (loading_.valid()) setValue("loading", sceneLoadProgress());
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
    std::vector<std::function<void()>> then = std::move(afterScene_);
    afterScene_.clear();
    for (const std::function<void()>& action : then) action();
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

namespace {
/// What the loops below need of a component type: its name, where it is filed, and its code.
/// Copied (a component's code may declare types, which moves the list), but without its
/// fields, which every frame would otherwise copy for nothing.
struct TypeCalls {
    std::string name, category;
    bool isInterface = false;
    void* (*get)(Scene&, Entity) = nullptr;
    void (*collect)(Scene&, std::vector<Entity>& out) = nullptr;
    void (*start)(void*, World&, Entity) = nullptr;
    void (*update)(void*, World&, Entity, float) = nullptr;
    void (*fixedUpdate)(void*, World&, Entity, float) = nullptr;
    void (*lateUpdate)(void*, World&, Entity, float) = nullptr;
    void (*onDestroy)(void*, World&, Entity) = nullptr;
    void (*onCollision)(void*, World&, Entity, Entity) = nullptr;
    void (*onTriggerEnter)(void*, World&, Entity, Entity) = nullptr;
    void (*draw)(void*, World&, Entity, Canvas&) = nullptr;
};
TypeCalls callsOf(const ComponentType& type) {
    return TypeCalls{type.name, type.category, type.isInterface(), type.get, type.collect, type.start, type.update, type.fixedUpdate, type.lateUpdate,
                     type.onDestroy, type.onCollision, type.onTriggerEnter, type.draw};
}
}  // namespace

void World::runComponents(bool isFixedStep, float dt, bool isLate) {
    if (!runsComponents) return;
    // By index, and a copy of each type: a component's code may load a game module or
    // declare types, which moves the list.
    for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
        const ComponentType& declared = ComponentRegistry::instance().types()[i];
        if (isLate ? !declared.lateUpdate : (!declared.start && !declared.update && !declared.fixedUpdate)) continue;
        TypeCalls type = callsOf(declared);
        // First note who has one, then look each up again: the code may create or destroy
        // entities and components, its own included.
        scratchEntities_.clear();
        type.collect(scene, scratchEntities_);
        if (scratchEntities_.empty()) continue;
        Profiler::Scope timed(profiler, type.name);     // each kind of component its own line in the profiler
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
        TypeCalls type = callsOf(ComponentRegistry::instance().types()[i]);
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

float World::interfaceScaleFor(Vec2 screen, Vec2 reference, float match) {
    if (screen.x <= 0 || screen.y <= 0 || reference.x <= 0 || reference.y <= 0) return 1.0f;
    float byWidth = std::log(screen.x / reference.x), byHeight = std::log(screen.y / reference.y);
    return std::exp(byWidth + (byHeight - byWidth) * std::clamp(match, 0.0f, 1.0f));
}

void World::drawComponents(Canvas& canvas) {
    // The interface ("UI" components) last, scaled to the screen as one; everything else as it is.
    interfaceScale = interfaceScaleFor(canvas.size(), interfaceReference, interfaceMatch);
    for (int pass = 0; pass < 2; pass++) {
        bool isInterface = pass == 1;
        if (isInterface) canvas.beginArea(Vec2{0, 0}, canvas.size(), interfaceScale);
        for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
            const ComponentType& declared = ComponentRegistry::instance().types()[i];
            if (!declared.draw) continue;
            TypeCalls type = callsOf(declared);
            if (!type.draw || type.isInterface != isInterface) continue;
            // While the game is not running (an editor, between plays) only the interface is
            // drawn: it has to be seen to be laid out. The rest draws what it does while it runs.
            if (!runsComponents && (!type.isInterface || !showsInterfaceWhileStopped)) continue;
            scratchEntities_.clear();
            type.collect(scene, scratchEntities_);
            std::vector<Entity> entities = scratchEntities_;
            for (Entity entity : entities) {
                if (!runs(type.name, entity)) continue;
                if (void* component = type.get(scene, entity)) type.draw(component, *this, entity, canvas);
            }
        }
        if (isInterface) canvas.endArea();
    }
}

void World::endScene() {
    // The last word, to everything that started: a looping sound to stop, a record to keep.
    if (runsComponents) {
        std::vector<Entity> all;
        scene.each<Transform>([&](Entity entity, Transform&) { all.push_back(entity); });
        tellDestroyed(all);
    }
    stopSounds();                           // what was sounding belonged to the scene that is leaving
    tasks.clear();                          // and so did what was waiting to happen in it
    pools_.clear();
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [](const Listener& listener) { return listener.hasOwner; }),
                     listeners_.end());
}

void World::tellDestroyed(const std::vector<Entity>& entities) {
    if (entities.empty()) return;
    for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
        const ComponentType& declared = ComponentRegistry::instance().types()[i];
        if (!declared.onDestroy) continue;
        TypeCalls type = callsOf(declared);
        std::unordered_set<uint64_t>& started = startedComponents_[type.name];
        for (Entity entity : entities) {
            if (!scene.isAlive(entity)) continue;
            // Only what started is told it ends: a component that never ran has nothing to undo.
            uint64_t key = (uint64_t(entity.index) << 32) | entity.generation;
            if (!started.erase(key) && type.start) continue;
            if (void* component = type.get(scene, entity)) type.onDestroy(component, *this, entity);
        }
    }
}

void World::removeDestroyed() {
    if (runsComponents) {
        // What onDestroy itself destroys is told too, each once: the list only grows at its end.
        size_t told = 0;
        for (int round = 0; round < 8; round++) {
            std::vector<Entity> going = scene.doomedEntities();
            if (going.size() <= told) break;
            std::vector<Entity> fresh(going.begin() + std::ptrdiff_t(told), going.end());
            told = going.size();
            tellDestroyed(fresh);
        }
    }
    scene.flushDestroyed();
}

const RenderSettings& World::effectSettings() { return materials_->settings; }

Vec3 World::wind() { return windWithWeather(effectSettings()); }
float World::gusts() { return gustsWithWeather(effectSettings()); }
const SeaSettings& World::sea() { return effectSettings().sea; }

const WeatherSettings& World::weather() { return effectSettings().weather; }

void World::simulateFrame(double realDelta) {
    input.isTakingText = wantsTextInput;        // as the last frame left it
    wantsTextInput = false;                     // until a text field that is typed in says so again
    // What was created or moved since the last frame (by an editor, by code between frames)
    // has its place in the world before any component asks for it.
    scene.updateTransforms();

    // 1. Physics and every fixedUpdate advance in steps of constant length.
    int steps = time.beginFrame(realDelta);
    for (int i = 0; i < steps; i++) {
        {
            Profiler::Scope timed(profiler, "Fixed update");
            onFixedUpdate(time.fixedDelta);
            runComponents(true, time.fixedDelta);
        }
        if (!isSimulationPaused) {
            Profiler::Scope timed(profiler, "Physics");
            prepareMeshColliders();
            physics.step(scene, time.fixedDelta);
            dispatchPhysicsEvents();        // components hear about collisions and triggers
        }
        removeDestroyed();
    }
    // 2. Bodies are shown where they are between the last two steps, for smooth motion at any frame rate.
    if (!isSimulationPaused) physics.writeTransforms(scene, time.alpha);

    // 3. The frame's own update.
    {
        Profiler::Scope timed(profiler, "Update");
        onUpdate(time.delta);
        runComponents(false, time.delta);
    }
    if (runsComponents) {
        Profiler::Scope timed(profiler, "Waiting tasks");
        tasks.run(time.delta, [this](Entity entity) { return scene.isAlive(entity); });
    }
    removeDestroyed();
    scene.updateTransforms();

    // 4. lateUpdate: cameras that follow, with everything where it will be drawn.
    {
        Profiler::Scope timed(profiler, "Late update");
        runComponents(false, time.delta, true);
    }
    scene.updateTransforms();

    // 5. What only follows: particles, and the sound of the room.
    {
        Profiler::Scope timed(profiler, "Particles");
        const RenderSettings& settings = effectSettings();
        if (simulatesEffects) {
            effectsTime += time.delta;
            kit::updateWeather(*this, time.delta);      // rain born and landed, lightning: before the particles move
            kit::updateWakes(*this, time.delta);        // what floats, and where it has been: for the water to draw its wake
            updateParticles(scene, time.delta, windWithWeather(settings), gustsWithWeather(settings), effectsTime);
        }
    }
    hearRoom();
}

void World::advance(double realDelta) {
    profiler.beginFrame();
    simulateFrame(realDelta);
    applyPendingScene();
    input.endFrame();
    profiler.endFrame();
}

std::optional<MeshData> World::readMeshData(const std::string& name) const {
    if (name == "cube") return MeshData::cube();
    if (name == "plane") return MeshData::plane();
    if (name == "sphere") return MeshData::sphere();
    if (name == "cylinder") return MeshData::cylinder();
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
    // The same model at the same scale is read and shaped once, however many colliders use it
    // (five cars of one model: one read, one hull).
    struct Made {
        std::shared_ptr<const MeshShape> mesh;
        std::shared_ptr<const ConvexShape> convex;
    };
    std::map<std::string, Made> made;
    auto keyOf = [](const std::string& name, Vec3 scale) {
        char text[96];
        std::snprintf(text, sizeof(text), "|%.6g|%.6g|%.6g", scale.x, scale.y, scale.z);
        return name + text;
    };
    scene.each<Collider, Transform>([&](Entity, Collider& collider, Transform& transform) {
        if (collider.shape != ColliderShape::Mesh) return;
        if (collider.meshName.empty()) return;          // its triangles come from a script (a SplineRoad), not from a file
        Vec3 scale = transform.scale;
        bool hasWhatItNeeds = collider.isConvex ? collider.convex != nullptr : collider.mesh != nullptr;
        if (hasWhatItNeeds && simd::length_squared(scale - collider.meshScale) < 1e-10f) return;
        if (collider.isConvex && !collider.convex && collider.mesh && simd::length_squared(scale - collider.meshScale) < 1e-10f) {
            Made& same = made[keyOf(collider.meshName, scale) + "|convex"];
            if (!same.convex) same.convex = ConvexShape::build(collider.mesh->points, Vec3{1, 1, 1});   // made convex since it was read
            collider.convex = same.convex;
            return;
        }
        if (!collider.mesh && simd::length_squared(collider.meshScale) > 0 && simd::length_squared(scale - collider.meshScale) < 1e-10f) return;   // tried, and there is no such model
        collider.meshScale = scale;
        collider.mesh = nullptr;
        Made& same = made[keyOf(collider.meshName, scale)];
        if (!same.mesh) {
            std::optional<MeshData> data = readMeshData(collider.meshName);
            if (!data) {
                logError("mesh collider: cannot read the model \"%s\"", collider.meshName.c_str());
                return;
            }
            same.mesh = MeshShape::build(data->positions, data->indices, scale);
            if (collider.isConvex) same.convex = ConvexShape::build(data->positions, scale);
        }
        if (collider.isConvex && !same.convex) same.convex = ConvexShape::build(same.mesh->points, Vec3{1, 1, 1});
        collider.mesh = same.mesh;
        collider.convex = collider.isConvex ? same.convex : nullptr;
    });
}

}  // namespace molten
