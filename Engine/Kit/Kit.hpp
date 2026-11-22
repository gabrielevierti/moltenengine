// Kit.hpp
// Components that come with the engine, for the things nearly every game needs: a
// player that moves, a camera that follows, things to collect, things that shoot and
// things that break, a score on the screen, a way to win and a way to start over.
//
// They are ordinary components declared with MOLTEN_COMPONENT, with nothing the game's
// own code could not do: when one does not fit, copy it from Kit.cpp into the project
// and change it. A game made only of these needs no code at all.
//
// All of them take a World, so a game built from them can be played without a screen,
// which is how the project templates are tested.
#pragma once

#include "Core/World.hpp"
#include "Rendering/Canvas.hpp"

#include <string>

namespace molten::kit {

/// Moves its entity with the keyboard: W A S D and the arrow keys, Space to jump.
/// On a dynamic body it steers the body's velocity (or pushes it, with `rolls`, for a
/// ball); on a kinematic body or none it moves the entity directly.
struct PlayerMover {
    float speed = 6.0f;                 // meters per second
    float jumpSpeed = 6.0f;             // 0: cannot jump
    bool rolls = false;                 // push instead of steer: a ball that gathers speed
    float acceleration = 18.0f;         // for `rolls`: meters per second, per second
    bool movesAlongX = true;
    bool movesAlongZ = true;            // off for a paddle that only goes left and right
    float limitX = 0.0f;                // if above zero, x stays between -limitX and +limitX
    bool facesMovement = false;         // turn to look where it is going

    void update(World& world, Entity self, float dt);
    void fixedUpdate(World& world, Entity self, float dt);

    bool wantsJump = false;             // a key press seen in update, used by the next physics step
};

/// Keeps its entity (a camera, usually) at a fixed offset from another, moving smoothly.
struct Follow {
    std::string target = "Player";      // the name of the entity to follow
    Vec3 offset{0, 7, 9};
    float smoothness = 6.0f;            // higher follows more tightly; 0 jumps there at once
    bool looksAtTarget = true;
    bool turnsWithTarget = false;       // the offset turns as the target does: a camera that stays behind a car

    /// After everything has moved, so that it follows where the target is drawn this frame.
    void lateUpdate(World& world, Entity self, float dt);
    Vec3 heading{0, 0, 0};              // which way the camera is behind the target, eased
};

/// Turns its entity at a steady rate.
struct Spin {
    Vec3 axis{0, 1, 0};
    float degreesPerSecond = 90.0f;

    void update(World& world, Entity self, float dt);
};

/// Takes its entity to and fro between where it was put and `offset` from there: a platform,
/// a target on its rail, a gate that opens and shuts. What is under it goes with it.
struct Shuttle {
    Vec3 offset{4, 0, 0};               // where it goes to, from where it starts, in its parent's axes
    float seconds = 3.0f;               // each way
    float waits = 0.0f;                 // seconds at each end
    bool eases = true;                  // slows into each end, instead of turning round at full speed

    void update(World& world, Entity self, float dt);

    Vec3 from{0, 0, 0};
    bool hasFrom = false;
    float time = 0;
};

/// Moves its entity at a steady velocity, or toward another entity.
struct Mover {
    Vec3 velocity{0, 0, -12};
    bool isLocal = true;                // the velocity is in the entity's own axes: -z is forward
    std::string chases;                 // if set: move toward the entity with this name instead
    float chaseSpeed = 3.0f;

    void update(World& world, Entity self, float dt);
};

/// Removes its entity after some seconds: bullets, effects, anything that should not pile up.
/// One that came from a pool (World::take, a Spawner) goes back to it instead.
struct Lifetime {
    float seconds = 3.0f;

    void start(World&, Entity) { age = 0; }         // again when reused
    void update(World& world, Entity self, float dt);
    float age = 0;
};

/// Something to pick up. Needs a trigger collider. When the collector touches it, a
/// shared value goes up and the entity is gone.
struct Collectible {
    std::string collector = "Player";   // who can pick it up, by name; empty for anyone
    std::string counter = "score";      // the shared value that goes up
    float amount = 1.0f;
    std::string sound;                  // a sound file from Assets, if any
    std::string effect;                 // a prefab to leave behind, if any

    void onTriggerEnter(World& world, Entity self, Entity other);
};

/// How much damage its entity can take. At zero the entity is removed.
struct Health {
    float amount = 3.0f;
    std::string counter;                // a shared value that shows it ("lives"); empty for none
    std::string scoreCounter = "score"; // the shared value that goes up when it dies
    float points = 0.0f;
    std::string effect;                 // a prefab spawned where it died: an explosion
    bool restartsSceneOnDeath = false;  // for the player

    void start(World& world, Entity self);
    /// Takes damage; removes the entity when nothing is left.
    void damage(World& world, Entity self, float damageAmount);
    /// The message "damage" takes the number it carries: how something on another machine, or
    /// a visual script, hurts it.
    void onMessage(World& world, Entity self, const Message& message);
};

/// Hurts what it touches, if that has Health. Works with a trigger or a solid collider.
struct Damage {
    float amount = 1.0f;
    std::string onlyTo;                 // if set: only the entity with this name, or whose name starts with it
    bool destroysSelf = true;           // a bullet is used up; a spike is not

    void onTriggerEnter(World& world, Entity self, Entity other);
    void onCollision(World& world, Entity self, Entity other);
    void hit(World& world, Entity self, Entity other);
};

/// Spawns a prefab in front of its entity while a key is held: a gun.
struct Shooter {
    std::string prefab = "Prefabs/Bullet.prefab";
    std::string key = "Space";
    bool pointerToo = true;             // the mouse button or a finger also fires
    float interval = 0.2f;              // seconds between shots
    Vec3 muzzle{0, 0, -1};              // where shots appear, in the entity's own axes
    std::string sound;

    void update(World& world, Entity self, float dt);
    float cooldown = 0;
};

/// What is under it in the Hierarchy (a picture, "{loading:percent}", a bar of "loading") is
/// shown while a scene loads in the background, and hidden otherwise. Put it in the scene that
/// starts the loading: the menu, the level being left.
struct LoadingScreen {
    void update(World& world, Entity self, float dt);
};

/// Spawns a prefab again and again around its entity: enemies, pickups, rain. In waves, if
/// `count` says how many a wave has; reusing what was put back (World::take, Lifetime) so that
/// hundreds come and go without being made and destroyed each time.
struct Spawner {
    std::string prefab = "Prefabs/Enemy.prefab";
    float interval = 2.0f;              // seconds between spawns
    float radius = 0.0f;                // spawn within this distance, on the entity's level
    int limit = 0;                      // stop after this many; 0 for no limit
    float firstDelay = 1.0f;
    int count = 0;                      // in a wave; 0: no waves, one after the other
    int waves = 0;                      // with waves: how many (0: forever)
    float waveSeconds = 5.0f;           // the pause between two waves
    int maxAlive = 0;                   // no more than this about at once (0: no limit)
    bool usesPool = true;
    std::string aliveValue;             // a shared value kept at how many are about; empty: none

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);
    /// Those it made still about: alive, switched on, not put back.
    int countAlive(World& world);
    float waited = 0;
    int spawned = 0;
    bool hasBegun = false;
    int wave = 0, inWave = 0;
    bool isPausing = false, isDone = false, toldDone = false;
    std::vector<Entity> alive;
    Random random{4242};
};

/// Text on the screen. {name} in the text is replaced with a shared value: "Coins: {score}".
/// `text` may hold values of the game in braces: "{score}" its number, "{abs:off|on}" a word for it,
/// "{lap:time}" seconds as a time (1:07.4).
struct HudText {
    std::string text = "Score: {score}";
    Vec2 anchor{0.5f, 0.0f};            // where on the screen: 0 0 is the top left, 1 1 the bottom right
    Vec2 offset{0, 28};                 // from the anchor, in points
    float size = 26.0f;
    Vec4 color{1, 1, 1, 1};
    std::string font;                   // a font of Assets ("Fonts/Title", made by Tools/bake_font.py); empty for the game's own

    std::shared_ptr<struct Font> loadedFont;
    std::string loadedFontName = "\n";

    void draw(World& world, Entity self, Canvas& canvas);
    /// The text with the values filled in.
    static std::string filled(const World& world, const std::string& text);
};

// ---------------------------------------------------------------- the game's interface
//
// Each piece of interface is a component on an entity of its own. They are placed the same
// way: `anchor` is a point of the screen (0 0 its top left, 1 1 its bottom right, 0.5 0.5
// its middle) and `offset` is how far from there the piece's middle is, in points. A health
// bar anchored to the top left stays in that corner on a phone and on a wide monitor.
// In the editor they are made from Entity > UI and dragged into place in the Game view.

/// Where a piece of interface is on a screen of a size: its middle.
inline Vec2 placeOnScreen(Vec2 screen, Vec2 anchor, Vec2 offset) { return Vec2{screen.x * anchor.x + offset.x, screen.y * anchor.y + offset.y}; }

/// A rectangle of color: a background for other pieces, a frame, a divider.
struct UIPanel {
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    Vec2 size{320, 180};
    Vec4 color{0.08f, 0.09f, 0.11f, 0.8f};
    float border = 0;                   // the width of a line around it, in points; 0 for none
    Vec4 borderColor{1, 1, 1, 0.5f};

    void draw(World& world, Entity self, Canvas& canvas);
};

/// A picture: an icon, a logo, a portrait. `picture` is a file of Assets.
struct UIImage {
    std::string picture;
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    Vec2 size{128, 128};
    Vec4 tint{1, 1, 1, 1};

    std::shared_ptr<Texture> loaded;
    std::string loadedName;

    void draw(World& world, Entity self, Canvas& canvas);
};

/// A button to click or to tap. While a finger (or the mouse) is on it, it holds a button
/// action, as a key would; let go on it, it can also add to a shared value and load a scene:
/// "Play" loads the first level, "+" adds one to {count}.
struct UIButton {
    std::string label = "Button";
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    Vec2 size{180, 52};
    Vec4 color{0.95f, 0.47f, 0.18f, 1};
    Vec4 textColor{1, 1, 1, 1};
    std::string action;                 // a button action it holds while pressed: "jump"; empty for none
    bool isRound = false;               // a circle of `radius` instead of a box of `size`: a thumb's button
    float radius = 50.0f;
    bool onlyOnTouchScreens = false;    // hidden until the screen is touched: for a game that also has keys
    std::string scene;                  // a scene it loads when clicked: "Scenes/Level1.scene"; empty for none
    std::string value;                  // a shared value it adds `amount` to when clicked; empty for none
    float amount = 1.0f;
    std::string font;                   // of its label: a font of Assets; empty for the game's own

    std::shared_ptr<struct Font> loadedFont;
    std::string loadedFontName = "\n";

    // State.
    bool isHeld = false, isHovered = false;
    bool wasClicked = false;            // true for the frame it was clicked in: for scripts that ask

    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
};

/// A dial that shows a shared value: a speedometer, a rev counter, a boost gauge. An arc
/// that fills, marks with numbers along it, a needle, and the value in the middle.
struct UIGauge {
    std::string value = "speed";
    float most = 200.0f;                // the value at the end of the arc
    float numbersEvery = 20.0f;         // a numbered mark at each multiple of this (0: none)
    float numbersScale = 1.0f;          // the numbers as written: 0.001 writes rpm in thousands
    float redFrom = 0.0f;               // where the red part of the arc starts, as a share (0: none)
    std::string label = "km/h";
    bool showsValue = true;             // the value itself, large, in the middle
    Vec2 anchor{0.5f, 1.0f};
    Vec2 offset{0, -90};
    float radius = 70.0f;
    float thickness = 7.0f;
    Vec4 color{1.0f, 0.5f, 0.18f, 1};
    Vec4 backColor{0.04f, 0.045f, 0.055f, 0.7f};

    float shown = 0;                    // eased toward the value: a needle has weight

    void draw(World& world, Entity self, Canvas& canvas);
};

// ---- the pieces dials and gauges are made of: each one thing, put together under a parent
// (Entity > UI > Gauge is a background, a filled arc, marks, a needle and two texts), so a dial
// of one's own is made the same way. Angles are in degrees, clockwise from the right.

/// An arc of a ring, filled as far as a shared value says (all of it with no value): a dial's
/// track, a timer's ring, a cooldown. Thickness as large as the radius makes a disc.
struct UIArc {
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    float radius = 70.0f;
    float thickness = 7.0f;
    float startAngle = 135.0f;
    float sweep = 270.0f;
    std::string value;                  // filled as far as this value is of `most`; empty: all of it
    float most = 100.0f;
    Vec4 color{1.0f, 0.5f, 0.18f, 1};
    float shown = 0;                    // eased toward the value

    void draw(World& world, Entity self, Canvas& canvas);
};

/// Marks along an arc, long and numbered at each `every`, short between: a dial's scale.
struct UIMarks {
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    float radius = 62.0f;               // of their outer ends
    float startAngle = 135.0f;
    float sweep = 270.0f;
    float most = 200.0f;                // the value at the end of the arc
    float every = 20.0f;                // a numbered mark at each multiple of this
    float numbersScale = 1.0f;          // the numbers as written: 0.001 writes rpm in thousands
    bool showsNumbers = true;
    Vec4 color{1, 1, 1, 0.85f};

    void draw(World& world, Entity self, Canvas& canvas);
};

/// A needle turned as far along an arc as a shared value says.
struct UINeedle {
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    float radius = 60.0f;               // its length
    float startAngle = 135.0f;
    float sweep = 270.0f;
    std::string value = "speed";
    float most = 200.0f;
    float thickness = 2.5f;
    Vec4 color{1.0f, 0.3f, 0.15f, 1};
    float shown = 0;

    void draw(World& world, Entity self, Canvas& canvas);
};

/// A bar that shows a shared value as a share of its most: health, fuel, progress.
struct UIBar {
    std::string value = "health";
    float most = 100.0f;
    Vec2 anchor{0, 0};
    Vec2 offset{130, 36};
    Vec2 size{220, 18};
    Vec4 color{0.35f, 0.85f, 0.4f, 1};
    Vec4 backColor{0, 0, 0, 0.5f};

    void draw(World& world, Entity self, Canvas& canvas);
};

/// A stick for a thumb, drawn on the screen: for phones and tablets, where there are no
/// keys. It pushes two axis actions ("moveX" and "moveZ" unless told otherwise), so a game
/// written for a keyboard or a controller is played by touch without changing a line.
/// With a mouse, press on it and drag: that is how it is tried on a Mac.
struct TouchStick {
    std::string horizontal = "moveX", vertical = "moveZ";      // the axis actions it pushes
    Vec2 anchor{0, 1};                  // the lower left corner
    Vec2 offset{120, -120};             // to the middle of its base
    float radius = 62.0f;               // of its base, in points
    bool verticalIsReversed = false;    // up on the stick is -1 as it is: forward for "moveZ"
    bool onlyOnTouchScreens = false;    // hidden until the screen is touched: for a game that also has keys
    Vec4 color{1, 1, 1, 0.5f};

    // State.
    uint64_t finger = 0;
    bool isHeld = false;
    Vec2 knob{0, 0};                    // where the thumb has dragged the knob to, in points

    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
};

/// A round button for a thumb, drawn on the screen: it holds a button action ("jump",
/// "fire") while a finger, or the mouse, is on it.
struct TouchButton {
    std::string action = "jump";
    std::string label = "A";
    Vec2 anchor{1, 1};                  // the lower right corner
    Vec2 offset{-90, -110};             // to its middle
    float radius = 38.0f;
    bool onlyOnTouchScreens = false;
    Vec4 color{1, 1, 1, 0.5f};

    bool isHeld = false;

    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
    Vec2 place(Vec2 screen) const { return placeOnScreen(screen, anchor, offset); }
};

/// Watches a shared value. When it reaches the target the game is won: a message
/// appears, the shared value "won" becomes 1, and after a moment the next scene loads.
struct Goal {
    std::string counter = "score";
    float target = 8.0f;
    std::string message = "You win!";
    std::string nextScene;              // empty: stay here
    float delay = 2.5f;                 // seconds the message shows before the next scene

    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
    float sinceReached = -1;
};

/// Starts the scene over when its entity falls below a height: the player fell off.
struct RestartWhenBelow {
    float height = -10.0f;

    void update(World& world, Entity self, float dt);
};

/// Keeps a body moving at one speed whatever it bounces off: the ball of a brick game.
struct ConstantSpeed {
    float speed = 9.0f;
    Vec3 startDirection{0.4f, 0, -1};   // where it goes first
    float minimumAlongZ = 0.35f;        // never travel flatter than this share of the speed: no endless side to side

    void fixedUpdate(World& world, Entity self, float dt);
    bool hasStarted = false;
};

/// Removed when something bumps into it: a brick, a crate, a target.
struct Breakable {
    std::string by;                     // if set: only when hit by the entity with this name
    std::string counter = "score";
    float points = 1.0f;
    std::string effect;                 // a prefab spawned where it was
    std::string sound;

    void onCollision(World& world, Entity self, Entity other);
};

/// A blast: once, when it appears, it pushes the bodies around it away and hurts what
/// has Health. Put it on an entity with a particle emitter (Burst at Start) and a
/// Lifetime, and that entity is an explosion to spawn wherever one is needed.
struct Explosion {
    float radius = 5.0f;
    float force = 12.0f;                // the speed given to a body at the center; less farther out
    float damage = 2.0f;
    float flashSeconds = 0.35f;         // how long the entity's light, if it has one, takes to fade

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);
    float age = 0;
    float lightIntensity = -1;
};

/// Makes a light waver like a flame.
struct Flicker {
    float amount = 0.35f;               // how far the brightness strays, as a share of itself
    float speed = 9.0f;

    void update(World& world, Entity self, float dt);
    float baseIntensity = -1;
    float phase = 0;
};

/// Plays a sound from its entity: music, an ambience, an engine. Starts with the scene,
/// or when something calls play().
struct AudioSource {
    std::string clip;                   // a sound in Assets: "Audio/music.wav"
    float volume = 1.0f;
    float pitch = 1.0f;                 // 2 is an octave up and twice as fast
    bool loops = false;
    bool playsOnStart = true;
    std::string group = "Effects";      // which volume it follows: Effects, Music, Voices, Interface, or the game's own
    bool comesFromHere = false;         // heard from where its entity is: louder near, from its side, muffled behind walls
    float fullWithin = 6;               // with comesFromHere: meters within which it is at full volume
    float silentBeyond = 60;            // and beyond which it is not heard

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);
    void play(World& world);
    void stop(World& world);

    uint32_t sound = 0;                 // what is playing; 0 for nothing
    float shownVolume = -1, shownPitch = -1;
};


/// Makes a body float: below `waterLevel` it is pushed up, the more the deeper it is, and
/// slowed as water slows things. On an entity with a RigidBody.
struct Buoyancy {
    float waterLevel = 0;               // the height of the water's surface, where there is no Water component: then its waves are followed
    float lift = 2.0f;                  // times its weight when fully under: above 1 it floats, below it sinks slowly
    float depth = 0.5f;                 // how far under it is fully under: about half its height
    float drag = 2.5f;                  // how quickly the water stops it
    /// A hull: half its size each way (a boat's, a raft's, a crate's). Then it is held up at its
    /// four lower corners, each as deep as the water is there: it rocks on the waves, lists
    /// when one side is lower and rights itself. Zero: held up at its middle only. It needs its
    /// RigidBody to rotate.
    Vec3 hull{0, 0, 0};
    float turnDrag = 1.5f;              // how quickly the water stops it turning
    Vec3 current{0, 0, 0};              // the water's flow, meters per second: a river carries what floats on it
    float lengthDrag = 1;               // of `drag`, along its length (its -z): a hull cuts through the water ahead

    void fixedUpdate(World& world, Entity self, float dt);
    bool isInWater = false;
    float underNow = 0;                 // how much of it is under, 0 to 1
};

/// A boat: pushed ahead and turned by its rudder, only while it floats. With a Buoyancy and a
/// RigidBody. Driven by the move actions (W/S and A/D, the left stick), or by `throttle` and
/// `steer` from code with `usesInput` off. It makes its Buoyancy's lengthwise drag small, as a
/// hull's is. Writes the values "speed" (km/h) and "knots".
struct Boat {
    float acceleration = 5;             // meters per second, per second, at full throttle
    float topSpeed = 12;                // meters per second
    float turnSpeed = 45;               // degrees per second at full rudder and full speed
    float reverse = 0.4f;               // of the acceleration, going backwards
    bool usesInput = true;
    float throttle = 0;                 // -1 astern to 1 full ahead
    float steer = 0;                    // -1 left to 1 right
    std::string sound;                  // its engine: a sound of Assets that loops, louder and higher with the throttle; empty for silence

    void start(World& world, Entity self);
    void fixedUpdate(World& world, Entity self, float dt);
    void update(World& world, Entity self, float dt);
    float speed = 0;                    // along the way it points, now
    uint32_t voice = 0;
};

/// Makes sure the kit's components are known even to a program that never mentions them.
void registerKit();

}  // namespace molten::kit
