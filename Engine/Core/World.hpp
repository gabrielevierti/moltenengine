// World.hpp
// The game as it is without a screen: the scene, physics, input, time, and what a
// game's components need to reach each other and change the course of the game.
//
// Engine is a World with a renderer, a window and sound added. Keeping this part
// separate has a practical point: a whole game can be run here, with no GPU, in a
// test. Components that take `World&` instead of `Engine&` work in both.
#pragma once

#include <optional>
#include "Assets/MeshData.hpp"
#include "Core/Time.hpp"
#include "Input/Input.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"
#include "Scene/Scene.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace molten {

class Canvas;
class Engine;
class Mesh;
struct NamedMaterial;
struct RenderSettings;

class World {
public:
    World();
    virtual ~World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ---- the parts. A game uses them directly.
    Scene scene;
    Physics physics;
    Input input;
    Time time;

    /// While true, physics stands still. The editor sets it while a scene is edited.
    bool isSimulationPaused = false;
    /// While true, the components a game declared with MOLTEN_COMPONENT run: start() once
    /// per component, update() every frame, fixedUpdate() every physics step, and the
    /// events (onCollision, onTriggerEnter). An editor turns this off while editing.
    bool runsComponents = true;
    /// Makes every component start() again: when a scene is loaded or play begins.
    void restartComponents() { startedComponents_.clear(); }

    // ---- values the whole game shares: the score, lives, keys collected. Components
    // and visual scripts read and write them by name; interface text can show them.
    float value(const std::string& name) const;
    /// True once something has set the value.
    bool hasValue(const std::string& name) const { return values_.count(name) > 0; }
    void setValue(const std::string& name, float value) { values_[name] = value; }
    float addValue(const std::string& name, float amount) { return values_[name] += amount; }
    const std::unordered_map<std::string, float>& values() const { return values_; }
    void clearValues() { values_.clear(); }

    // ---- entities

    /// The first entity with this name; invalid if there is none.
    Entity find(const std::string& name) const { return scene.find(name); }
    /// The first entity with a tag, and all of them: "Player", "Enemy", whatever they are called.
    Entity findWithTag(const std::string& tag) const;
    std::vector<Entity> findAllWithTag(const std::string& tag) const;
    /// True if a component of an entity should run: the entity is active and the component not switched off.
    bool runs(const std::string& typeName, Entity entity);
    /// A copy of an entity and of everything under it, components included.
    Entity clone(Entity original);
    /// Adds a prefab (a .prefab file, as a path relative to Assets) to the scene with its
    /// root at `position`. Returns the root, or an invalid entity if the file cannot be read.
    Entity spawn(const std::string& prefab, Vec3 position);
    /// Like spawn, with the prefab's text already in hand.
    Entity spawnFromText(const std::string& prefabText, Vec3 position);

    // ---- scenes

    /// Replaces the scene with another (a path relative to Assets) at the end of this
    /// frame: safe to call from a component in the middle of its update.
    void loadScene(const std::string& name) { pendingScene_ = name; }
    /// The scene that is loaded, as the name it was loaded by; "" if it was built in code.
    const std::string& sceneName() const { return sceneName_; }
    /// Loads the same scene again: the usual "try again".
    /// The shared values are cleared with it, so the score counts from nothing again.
    void reloadScene() {
        pendingReload_ = true;
        pendingClearsValues_ = true;
    }
    /// Replaces the scene with one given as text, now. `name` is what sceneName() will say.
    bool loadSceneText(const std::string& name, const std::string& sceneText);
    /// Tells the world what the scene it holds is, when someone else put it there (the
    /// editor, when Play is pressed): reloadScene() returns to this text.
    void noteScene(const std::string& name, const std::string& sceneText) {
        sceneName_ = name;
        sceneText_ = sceneText;
    }
    /// Called after a scene was loaded by loadScene or reloadScene, for whoever keeps
    /// things in the scene that are not part of it (the editor and its camera).
    std::function<void()> onSceneLoaded;
    /// Reads a scene now. Returns false if there is no such scene.
    bool loadSceneNow(const std::string& name);
    /// The materials of the loaded scene, by name, and how it asks to be rendered.
    std::vector<NamedMaterial>& materials();
    RenderSettings& sceneSettings();

    // ---- what only a full engine can do. Here they do nothing, so that gameplay code
    // need not ask whether there is a screen or a loudspeaker.

    /// Plays a sound file from Assets ("Audio/coin.wav").
    virtual void playSound(const std::string& name, float volume = 1.0f) {}
    /// A sound that goes on until stopped (an engine, wind, music), whose loudness and
    /// pitch can follow what happens. Returns 0 where there is no sound to make. It stops
    /// by itself when the scene is replaced.
    uint32_t startSound(const std::string& name, float volume = 1.0f, float pitch = 1.0f, bool loops = true);
    void changeSound(uint32_t sound, float volume, float pitch);
    void stopSound(uint32_t sound);
    void stopSounds();
    /// Where a file of the project's Assets is on disk.
    virtual std::string assetPath(const std::string& name) const { return assetRoot + "/" + name; }
    /// How scene files find meshes. Without a GPU there are none: entities keep their
    /// colliders and their code, and are simply not drawn.
    virtual std::shared_ptr<Mesh> loadMesh(const std::string& name) { return nullptr; }
    /// Gives an entity that draws a mesh one of its own, made of these triangles: what an
    /// animated model shows each frame. A World that draws nothing does nothing.
    virtual void showMesh(Entity entity, const MeshData& data) {}
    /// Reads the triangles of every mesh collider that does not have them yet (or whose
    /// entity has changed size). Called before each step of the physics.
    void prepareMeshColliders();
    /// The triangles of a model: "cube", "plane", "sphere", or an .obj, .gltf or .glb file among the assets.
    std::optional<MeshData> readMeshData(const std::string& name) const;
    /// Reads the project's actions (Input.actions in Assets) into `input`; the standard
    /// ones if the project has no such file.
    void loadInputMap();
    /// A material asset. Without an engine it is read from the file, without its textures.
    virtual std::shared_ptr<Material> loadMaterial(const std::string& name);
    virtual std::shared_ptr<Texture> loadTexture(const std::string& name, TextureFormat format) { return nullptr; }
    /// This world as an Engine, if it is one.
    virtual Engine* asEngine() { return nullptr; }
    /// The folder assetPath() looks in, for a World that is not an Engine.
    std::string assetRoot = ".";

    // ---- running without an Engine

    /// One frame of `realDelta` seconds: physics in fixed steps, then every component.
    /// What Engine does each frame, minus the game object, the picture and the sound.
    void advance(double realDelta);
    /// Calls draw() on the components that have one. An Engine does this with its own canvas.
    void drawComponents(Canvas& canvas);

protected:
    void runComponents(bool isFixedStep, float dt, bool isLate = false);
    /// Tells components about the collisions and trigger overlaps of the last physics step.
    void dispatchPhysicsEvents();
    /// Carries out loadScene(), if one was asked for.
    void applyPendingScene();
    /// Called after a scene was read, so that an Engine can do its part (render settings).
    virtual void sceneWasLoaded(const std::string& sceneText) {}
    // What an engine with sound does for the functions above.
    virtual uint32_t beginSound(const std::string&, float, float, bool) { return 0; }
    virtual void adjustSound(uint32_t, float, float) {}
    virtual void endSound(uint32_t) {}
    std::vector<uint32_t> startedSounds_;

    std::unordered_map<std::string, std::unordered_set<uint64_t>> startedComponents_;   // by type: the entities whose start() ran
    std::unordered_map<std::string, float> values_;
    std::string pendingScene_, sceneName_;
    std::string sceneText_;             // the scene as it was loaded: what reloadScene() goes back to
    bool pendingReload_ = false;
    bool pendingClearsValues_ = false;
    std::vector<Entity> scratchEntities_;

private:
    struct Materials;
    std::shared_ptr<Materials> materials_;
};

}  // namespace molten
