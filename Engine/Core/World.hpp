// World.hpp
// The game as it is without a screen: the scene, physics, input, time, and what a
// game's components need to reach each other and change the course of the game.
//
// Engine is a World with a renderer, a window and sound added. Keeping this part
// separate has a practical point: a whole game can be run here, with no GPU, in a
// test. Components that take `World&` instead of `Engine&` work in both.
#pragma once

#include <optional>
#include "Core/Tasks.hpp"
#include "Assets/MeshData.hpp"
#include "Core/Profiler.hpp"
#include "Core/SaveGame.hpp"
#include "Core/Time.hpp"
#include "Input/Input.hpp"
#include "Physics/Physics.hpp"
#include "Project/GameSettings.hpp"
#include "Rendering/Material.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneAhead.hpp"

#include <functional>
#include <atomic>
#include <future>
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

/// On the entity addScene() puts a scene's entities under: the scene it was.
struct AddedScene {
    std::string name;
};

/// On what take() made: the prefab it came from, and how it stood, to stand so again.
struct Pooled {
    std::string prefab;
    Quat rotation{};
    Vec3 scale{1, 1, 1};
    bool isWaiting = false;
};

/// Something that happened, told to whoever cares: "doorOpened", "playerDied", "lapDone".
struct Message {
    std::string name;
    float number = 0;                   // what it carries: the damage, the lap's time
    std::string text;
    Entity from, to;                    // `to` none: everyone listening
};

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
    Profiler profiler;                  // where each frame's time went: see Profiler.hpp

    /// While true, physics stands still. The editor sets it while a scene is edited.
    bool isSimulationPaused = false;
    /// While true, the components a game declared with MOLTEN_COMPONENT run: start() once
    /// per component, update() every frame, fixedUpdate() every physics step, and the
    /// events (onCollision, onTriggerEnter). An editor turns this off while editing.
    bool runsComponents = true;
    /// While components do not run, the interface is still drawn: an editor lays it out there.
    /// A game's player turns it off, so that nothing of the game shows behind its splash.
    bool showsInterfaceWhileStopped = true;
    /// A text field is being typed in: a phone shows its keyboard. Set again every frame by it.
    bool wantsTextInput = false;
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
    /// Shared words, beside the numbers: a name typed in a text field, the player's team.
    /// Interface text shows them as it shows a value: "Hello {playerName}".
    const std::string& text(const std::string& name) const;
    bool hasText(const std::string& name) const { return texts_.count(name) > 0; }
    void setText(const std::string& name, const std::string& value) { texts_[name] = value; }
    const std::unordered_map<std::string, std::string>& texts() const { return texts_; }

    // ---- preferences: numbers kept from one time the game is played to the next (settings,
    // records). Read from and written to `preferencesFile` at once; with no file, kept for this run.
    float preference(const std::string& name, float fallback = 0.0f) const;
    bool hasPreference(const std::string& name) const;
    void setPreference(const std::string& name, float value);
    /// Words kept the same way (in a file beside the preferences: `preferencesFile`.texts).
    std::string textPreference(const std::string& name, const std::string& fallback = {}) const;
    bool hasTextPreference(const std::string& name) const;
    void setTextPreference(const std::string& name, const std::string& value);
    std::string preferencesFile;

    // ---- languages: the texts of the interface (HudText, button labels, the words of
    // "{value:a|b}") in another language, from Assets/Localization/<language>.txt, a line per
    // text as it is written in the game:   "RACE" = "GARA"
    // A text with no line, or with no language chosen, is shown as it is written.
    std::string language;               // "it", "fr"...; empty: as written
    std::string translate(const std::string& text) const;
    /// The languages the game has files for, by name ("it", "fr"), sorted.
    std::vector<std::string> languages() const;

    // ---- saved games (see SaveGame.hpp): each slot a file in a "Saves" folder beside the
    // preferences; with no preferences file, kept for this run.
    bool saveGame(const SaveGame& save, const std::string& slot = "Save");
    std::optional<SaveGame> loadGame(const std::string& slot = "Save") const;
    bool hasSavedGame(const std::string& slot = "Save") const;
    bool deleteSavedGame(const std::string& slot = "Save");
    /// The slots saved, by name.
    std::vector<std::string> savedGames() const;
    void clearValues() {
        values_.clear();
        texts_.clear();
    }

    // ---- messages between scripts (see the Message struct above)

    /// Tells something happened: to one entity (`to`), or to everyone listening when `to` is
    /// none. Components hear it in onMessage(World&, Entity self, const Message&); code that
    /// listen()s hears it too; visual scripts in their On Message event. At once, in order.
    void send(const std::string& name, Entity to = {}, float number = 0, const std::string& text = {}, Entity from = {});
    void send(const Message& message);
    /// Calls `heard` for each message of that name ("*" for all), sent to anyone. Stops with
    /// stopListening(id), or when `owner` is destroyed.
    int listen(const std::string& name, std::function<void(const Message&)> heard, Entity owner = {});
    void stopListening(int id);

    // ---- later (see Tasks.hpp): in game time, paused and slowed with the game

    int after(double seconds, std::function<void()> action, Entity owner = {}) { return tasks.after(seconds, std::move(action), owner); }
    int every(double seconds, std::function<void()> action, Entity owner = {}) { return tasks.every(seconds, std::move(action), owner); }
    int when(std::function<bool()> condition, std::function<void()> action, Entity owner = {}) {
        return tasks.when(std::move(condition), std::move(action), owner);
    }
    void cancel(int id) { tasks.cancel(id); }
    /// Runs a coroutine (a function returning molten::Task that co_awaits) until it ends.
    void start(Task task, Entity owner = {}) { tasks.adopt(task.release(), owner); }
    /// What a coroutine awaits: `co_await world.seconds(2)`, `co_await world.until(...)`.
    Tasks::Awaiter seconds(double count) { return Tasks::Awaiter{&tasks, count, {}}; }
    Tasks::Awaiter until(std::function<bool()> condition) { return Tasks::Awaiter{&tasks, -1, std::move(condition)}; }
    Tasks tasks;

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
    /// Like spawn, but reuses one put back with release() when there is one: bullets, sparks,
    /// enemies by the hundred without making and destroying each. A reused one is switched on
    /// again where asked, still, and its components' start() run again; their other values are
    /// as it left them (set what matters in start()).
    Entity take(const std::string& prefab, Vec3 position);
    /// Puts back what take() gave, switched off, for the next take(); destroys anything else.
    void release(Entity entity);
    /// How many of a prefab wait in its pool.
    size_t waitingInPool(const std::string& prefab) const;
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
    /// Runs once the scene asked for with loadScene() or reloadScene() is in place: putting a
    /// saved game back into it, placing the player at a door.
    void afterSceneLoads(std::function<void()> action) { afterScene_.push_back(std::move(action)); }
    /// Goes to a scene whose models are read on another thread meanwhile: the game goes on
    /// (a loading screen, music) until the scene is ready, and then it is put in its place at
    /// the end of a frame. With `isAdded`, the scene is added to the one open instead (see
    /// addScene). The shared values "loading" (0 to 1) and "isLoading" say how it goes.
    void loadSceneInBackground(const std::string& name, bool isAdded = false);
    bool isLoadingScene() const { return loading_.valid(); }
    float sceneLoadProgress() const;
    /// Adds a scene's entities to the scene open, under one entity named "Scene: <name>":
    /// a level's next room, a menu over the game. Its lighting stays the open scene's.
    Entity addScene(const std::string& name);
    Entity addSceneText(const std::string& name, const std::string& sceneText);
    /// Takes away what addScene added.
    void removeScene(const std::string& name);
    /// The materials of the loaded scene, by name, and how it asks to be rendered.
    std::vector<NamedMaterial>& materials();
    RenderSettings& sceneSettings();

    // ---- what only a full engine can do. Here they do nothing, so that gameplay code
    // need not ask whether there is a screen or a loudspeaker.

    /// How hot the device is: 0 cool, 1 warm, 2 hot (the system is about to slow down), 3 very hot.
    /// The renderer already draws smaller when it is hot (RenderSettings::followsDeviceHeat);
    /// a game can do more: fewer effects, no traced light.
    virtual int deviceHeat() const { return 0; }
    /// Low Power Mode is on: a game may lower its frame rate or its quality.
    virtual bool isSavingBattery() const { return false; }

    /// Plays a sound file from Assets ("Audio/coin.wav").
    /// `group`: "Effects" (also for ""), "Music", "Voices", "Interface", or one of the game's own.
    virtual void playSound(const std::string& name, float volume = 1.0f, const std::string& group = "") {}
    /// A sound that goes on until stopped (an engine, wind, music), whose loudness and
    /// pitch can follow what happens. Returns 0 where there is no sound to make. It stops
    /// by itself when the scene is replaced.
    uint32_t startSound(const std::string& name, float volume = 1.0f, float pitch = 1.0f, bool loops = true, const std::string& group = "");
    /// Every sound the game makes, from 0 (silent) to 1: a volume setting, a pause. Sounds already
    /// playing follow it the next time they are changed.
    float soundVolume = 1.0f;
    void changeSound(uint32_t sound, float volume, float pitch);
    /// Where a sound started with startSound comes from: from then on it is as loud as it is
    /// near the camera (full within `fullWithin` meters, gone beyond `silentBeyond`) and comes
    /// from its side. Call it as the thing making it moves: another car's engine, a waterfall.
    void placeSound(uint32_t sound, Vec3 position, float fullWithin = 6.0f, float silentBeyond = 150.0f);
    void stopSound(uint32_t sound);
    void stopSounds();

    // ---- the mixer: groups of sounds and their volumes (kept from scene to scene)
    /// The volume of a group of sounds, 0 to 1: "Music", "Effects", "Voices", "Interface", or the
    /// game's own. A settings menu's sliders set these.
    void setVolume(const std::string& group, float volume);
    float volumeOf(const std::string& group) const;
    void setMuted(const std::string& group, bool isMuted);
    /// While anything of `under` plays, `group` goes down to `to` of its volume, and back up after.
    /// The music ducks under the voices to begin with. An empty `under` stops it.
    void duck(const std::string& group, const std::string& under, float to = 0.3f);

    // ---- rooms: reverb, and walls
    /// Sounds placed with placeSound are muffled when a wall (a collider without a body) is between
    /// them and the listener.
    bool muffledByWalls = true;
    /// A Reverb Zone asks for its room each frame, with how much the listener is in it (0 to 1);
    /// the room most asked for is heard, and none, dry.
    void askForRoom(float weight, float size, float damping, float wet);
    /// The room heard now: its wet, 0 for none.
    float roomWet() const { return roomNow_[2]; }
    /// Where the listener is (the camera), if there is one.
    bool ears(Mat4& world) { return listener(world); }
    /// Where a file of the project's Assets is on disk.
    virtual std::string assetPath(const std::string& name) const { return assetRoot + "/" + name; }
    /// How scene files find meshes. Without a GPU there are none: entities keep their
    /// colliders and their code, and are simply not drawn.
    virtual std::shared_ptr<Mesh> loadMesh(const std::string& name) { return nullptr; }
    /// A model read in the background, for loadMesh to use instead of reading its file.
    virtual void preloadMesh(const std::string& name, MeshData data) {}
    /// A picture decoded in the background (four bytes a pixel, rows from the top), for the
    /// materials that use it.
    virtual void preloadPicture(const std::string& name, std::vector<uint8_t> pixels, int width, int height) {}
    /// assetPath as something other threads may call while this one goes on: it holds copies
    /// of the folders, and only reads the disk.
    virtual std::function<std::string(const std::string&)> assetPathFinder() const {
        return [root = assetRoot](const std::string& name) { return root + "/" + name; };
    }
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
    /// A font of Assets made by Tools/bake_font.py (`name`.png and `name`.txt): for text in
    /// another typeface than the game's own. Null without a renderer, or if there is none.
    virtual std::shared_ptr<struct Font> loadFont(const std::string& name) { return nullptr; }
    /// This world as an Engine, if it is one.
    virtual Engine* asEngine() { return nullptr; }
    // ---- the project's own settings

    /// What the project decided for all of its scenes: tags, layers and which of them collide,
    /// gravity, how often physics steps. See Project/GameSettings.hpp.
    GameSettings settings;
    /// Makes them so: physics gets the gravity and the layers, the clock its step.
    void applySettings(const GameSettings& wanted);
    /// Reads Game.settings from a project's Settings folder and applies it (none: as a new project has them).
    void loadSettings(const std::string& settingsDirectory);

    /// The line from the camera through a point of the screen (in points, as Input::mousePosition gives
    /// it): what is under the mouse or a finger. A world without a screen looks straight down.
    virtual Ray screenRay(Vec2 /*screenPoint*/) { return Ray{Vec3{0, 0, 0}, Vec3{0, -1, 0}}; }
    /// The folder assetPath() looks in, for a World that is not an Engine.
    std::string assetRoot = ".";

    // ---- running without an Engine

    /// One frame of `realDelta` seconds, for a world that has no screen (a test, a tool): the
    /// frame below, then the scene asked for is loaded and the input's frame ends.
    void advance(double realDelta);
    /// The frame itself. There is one, here, and an Engine runs this same one: what a game does
    /// in a test is what it does on the screen. In order:
    ///   1. As many fixed steps as the time that passed holds (none, one, several). Each:
    ///      onFixedUpdate, every component's fixedUpdate, physics, collisions and triggers
    ///      told (onCollision, onTriggerEnter), the destroyed removed.
    ///   2. Bodies are placed between their last two physics steps.
    ///   3. onUpdate, every component's start (once) and update, then what was waiting:
    ///      after(), every(), when() and coroutines. The destroyed are removed.
    ///   4. Every component's lateUpdate, with everything where it will be drawn.
    ///   5. Particles move, and the room the listener is in is heard.
    /// A component whose entity goes hears onDestroy first, while everything is still there.
    void simulateFrame(double realDelta);
    /// What belongs to the game that is ending or to the scene that is leaving: its sounds stop,
    /// what was waiting is forgotten, so are the pools and whoever was listening for an entity.
    void endScene();
    /// Removes what destroy() was called for, after its components heard onDestroy.
    void removeDestroyed();
    /// Calls draw() on the components that have one. An Engine does this with its own canvas.
    void drawComponents(Canvas& canvas);

    /// The game's interface is laid out for a screen of `interfaceReference` points and drawn
    /// larger or smaller on others, as Unity's canvas scaler does: by its width (match 0), its
    /// height (1), or between the two (0.5). `interfaceScale` is what the last frame used, for
    /// turning where a finger is into the interface's own points.
    Vec2 interfaceReference{1280, 720};
    float interfaceMatch = 0.5f;
    float interfaceScale = 1.0f;
    static float interfaceScaleFor(Vec2 screen, Vec2 reference, float match);

protected:
    /// What a World with a game object and a screen adds to the frame: an Engine calls its
    /// Game and its Behaviors here, before the components.
    virtual void onFixedUpdate(float /*dt*/) {}
    virtual void onUpdate(float /*dt*/) {}
    /// The wind particles are blown by: the scene's own, as the renderer has it in an Engine.
    virtual const RenderSettings& effectSettings();
    /// Every component hears onDestroy for these entities.
    void tellDestroyed(const std::vector<Entity>& entities);
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
    virtual void panSound(uint32_t, float) {}
    virtual void endSound(uint32_t) {}
    virtual void groupSound(uint32_t, const std::string&) {}
public:
    /// The sea's waves of a Water, for the water materials to draw them (Engine: the renderer).
    virtual void showOcean(const class Ocean&, Vec2 /*origin*/) {}
protected:
    virtual void mixGroup(const std::string&, float /*volume*/, bool /*isMuted*/) {}
    virtual void duckGroup(const std::string&, const std::string&, float) {}
    virtual void muffleSound(uint32_t, float) {}
    virtual void setRoom(float /*size*/, float /*damping*/, float /*wet*/) {}
    struct GroupMix {
        float volume = 1;
        bool isMuted = false;
    };
    std::unordered_map<std::string, GroupMix> groupMix_;
    float roomWish_[4] = {0, 0, 0, 0};                  // weight, size, damping, wet: the most asked for this frame
    float roomNow_[3] = {0, 0, 0};
    void hearRoom();
    float muffleFor(Vec3 ears, Vec3 position) ;
    /// Who listens: the camera's world matrix, if there is one.
    virtual bool listener(Mat4&) { return false; }
    struct SoundPlace {
        Vec3 position{0, 0, 0};
        float fullWithin = 6, silentBeyond = 150;
        float volume = -1, pitch = 1;               // as last asked for, before the distance
        float muffle = -1;
    };
    std::unordered_map<uint32_t, SoundPlace> soundPlaces_;
    void voiceSound(uint32_t sound, float volume, float pitch);
    std::vector<uint32_t> startedSounds_;

    std::unordered_map<std::string, std::unordered_set<uint64_t>> startedComponents_;   // by type: the entities whose start() ran
    std::unordered_map<std::string, float> values_;
    std::string pendingScene_, sceneName_;
    std::string sceneText_;             // the scene as it was loaded: what reloadScene() goes back to
    bool pendingReload_ = false;
    bool pendingClearsValues_ = false;
    std::vector<Entity> scratchEntities_;
    mutable std::unordered_map<std::string, float> preferences_;
    mutable std::unordered_map<std::string, std::string> textPreferences_;
    mutable std::string textPreferencesReadFrom_ = "\n";
    void readTextPreferences() const;
    std::unordered_map<std::string, std::string> texts_;
    struct Listener {
        int id;
        std::string name;
        std::function<void(const Message&)> heard;
        Entity owner;
        bool hasOwner;
    };
    std::vector<Listener> listeners_;
    std::vector<std::function<void()>> afterScene_;
    std::future<SceneAhead> loading_;
    std::string loadingName_;
    bool loadingIsAdded_ = false;
    std::shared_ptr<SceneAheadProgress> loadingProgress_;
    void finishBackgroundLoad();
    std::unordered_map<std::string, std::vector<Entity>> pools_;     // by prefab: those put back
    int nextListener_ = 1;
    int sendDepth_ = 0;
    std::unordered_map<std::string, std::string> savesInMemory_;       // with no preferences file
    mutable std::unordered_map<std::string, std::string> translations_;
    mutable std::string translationsFor_ = "\n";                      // the language `translations_` holds
    std::string saveFileOf(const std::string& slot) const;
    mutable std::string preferencesReadFrom_;           // which file `preferences_` holds, once read
    void readPreferences() const;

private:
    struct Materials;
    std::shared_ptr<Materials> materials_;
};

}  // namespace molten
