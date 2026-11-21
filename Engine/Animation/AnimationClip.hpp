// AnimationClip.hpp
// An animation made in the editor (or written by hand): how properties of an entity and of the
// entities under it change over time. A door that swings open, a light that flickers, a
// platform that goes back and forth, a button that pulses, a whole little scene. The same kind of
// clip as a model's animations as far as an Animator Controller is concerned: a state plays
// either, and blends them the same way.
//
// A .anim file of Assets, as text:
//
//   molten-clip 1
//   length 2
//   loops 1
//   track "" Transform position              "" is the entity the Animator is on
//     key 0  0 0 0
//     key 1  0 2 0
//     key 2  0 0 0
//   track "Hinge" Transform rotation          an entity under it, by its path of names: "Arm/Hand"
//     key 0  0 0 0
//     key 0.6  0 -95 0 linear                 smooth (the default), linear or step into this key
//   track "Lamp" Light intensity
//     key 0 1
//     key 0.1 0.2 step
//   event 0.5 "Audio/creak.wav"               a sound is played; any other name is sent to the
//                                             entity as a message (On Message, onMessage)
//
// What can be animated: Transform position, rotation (as three angles in degrees) and scale and
// active; Light color, intensity, range and enabled; Camera fieldOfView (degrees) and
// orthographicHeight; MeshRenderer tint and visible; and every number, switch, vector and color
// field of any component, by the component's and field's names.
#pragma once

#include "Core/World.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace molten {

struct AnimationKey {
    enum class Ease : uint8_t { Smooth, Linear, Step };
    float time = 0;
    std::array<float, 4> value{0, 0, 0, 0};
    Ease ease = Ease::Smooth;           // how the curve comes into this key from the one before
};

struct AnimationTrack {
    std::string path;                   // "" the entity itself; "Arm/Hand" one under it
    std::string component;              // "Transform", "Light", or a registered component's name
    std::string field;                  // "position"; a registered component's field name
    int width = 1;                      // how many numbers: 1, 2, 3 or 4
    std::vector<AnimationKey> keys;     // by time

    /// The value at `time`: the first key's before it, the last's after it.
    std::array<float, 4> sample(float time) const;
    /// Puts a key at `time` (replacing one within a hundredth of a second of it).
    void setKey(float time, const std::array<float, 4>& value);
    void sort();
    /// "Hinge: Transform rotation".
    std::string label() const;
    std::string id() const { return path + "\n" + component + "\n" + field; }
};

struct AnimationClip {
    struct Event {
        float time = 0;
        std::string name;
    };
    float length = 1;
    bool loops = true;
    std::vector<AnimationTrack> tracks;
    std::vector<Event> events;          // by time

    AnimationTrack* find(const std::string& path, const std::string& component, const std::string& field);
    const AnimationTrack* find(const std::string& path, const std::string& component, const std::string& field) const;

    static std::optional<AnimationClip> parse(const std::string& text, std::string* error = nullptr);
    std::string toText() const;
};

/// What a clip, or a blend of clips, puts on the properties: by track id, the numbers.
struct ClipPose {
    struct Value {
        AnimationTrack const* track = nullptr;
        std::string path, component, field;
        int width = 1;
        std::array<float, 4> value{0, 0, 0, 0};
    };
    std::map<std::string, Value> values;

    /// The clip at `time` (seconds).
    static ClipPose of(const AnimationClip& clip, float time);
    /// Eases this pose toward `other` by `t`: values in both are mixed, values only in `other` taken.
    void mixToward(const ClipPose& other, float t);
    /// Puts every value on the entities under `root`.
    void apply(Scene& scene, Entity root) const;
};

/// The entity at `path` under `root` ("" is root itself), or none.
Entity entityAtPath(const Scene& scene, Entity root, const std::string& path);
/// The path of names from `root` down to `entity`: "" for root, "Arm/Hand"; none if it is not under it.
std::optional<std::string> pathFromRoot(const Scene& scene, Entity root, Entity entity);

/// A property that can be animated, of an entity as it is.
struct AnimatableProperty {
    std::string component, field;
    int width = 1;
    bool isColor = false, isSwitch = false;
};
/// Every property the entity has that a clip can animate.
std::vector<AnimatableProperty> animatableProperties(Scene& scene, Entity entity);
/// Reads or writes one; false if the entity has no such property.
bool readProperty(Scene& scene, Entity entity, const std::string& component, const std::string& field, std::array<float, 4>& value, int* width = nullptr);
bool writeProperty(Scene& scene, Entity entity, const std::string& component, const std::string& field, const std::array<float, 4>& value);

inline bool isAnimationClipFile(const std::string& name) {
    return name.size() > 5 && name.compare(name.size() - 5, 5, ".anim") == 0;
}

/// A clip file read once and shared by everything that plays it; read again when the file changes
/// (the editor saving it). Nullptr if it cannot be read.
std::shared_ptr<const AnimationClip> sharedAnimationClip(const std::string& path);

}  // namespace molten
