// Component.hpp
// Components a game declares, in a form the rest of the engine can use without knowing
// their C++ type: the editor shows and edits their fields, scene files save them, and
// the engine runs their code while the game plays.
//
// A component is a plain struct. Declaring it takes one line after it:
//
//     struct Spinner {
//         float turnsPerSecond = 0.5f;
//         molten::Vec3 axis{0, 1, 0};
//
//         // Any of these, if the component does something:
//         void start(molten::Engine& engine, molten::Entity self);
//         void update(molten::Engine& engine, molten::Entity self, float dt);
//         void fixedUpdate(molten::Engine& engine, molten::Entity self, float dt);
//         void onCollision(molten::Engine& engine, molten::Entity self, molten::Entity other);
//         void onTriggerEnter(molten::Engine& engine, molten::Entity self, molten::Entity other);
//         void draw(molten::Engine& engine, molten::Entity self, molten::Canvas& canvas);
//     };
//     MOLTEN_COMPONENT(Spinner, MOLTEN_FIELD(turnsPerSecond), MOLTEN_FIELD(axis))
//
// Fields may be float, int, bool, Vec2, Vec3, Vec4 and std::string. A field that is not
// listed still exists; it is just not shown or saved (a timer, a cached pointer).
#pragma once

#include "Core/World.hpp"
#include "Scene/Scene.hpp"

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace molten {

class Canvas;
class Engine;

enum class FieldType : uint8_t { Float, Int, Bool, Vec2, Vec3, Vec4, String };

namespace detail {
template <class T> struct FieldTypeOf;
template <> struct FieldTypeOf<float> { static constexpr FieldType value = FieldType::Float; };
template <> struct FieldTypeOf<int> { static constexpr FieldType value = FieldType::Int; };
template <> struct FieldTypeOf<bool> { static constexpr FieldType value = FieldType::Bool; };
template <> struct FieldTypeOf<Vec2> { static constexpr FieldType value = FieldType::Vec2; };
template <> struct FieldTypeOf<Vec3> { static constexpr FieldType value = FieldType::Vec3; };
template <> struct FieldTypeOf<Vec4> { static constexpr FieldType value = FieldType::Vec4; };
template <> struct FieldTypeOf<std::string> { static constexpr FieldType value = FieldType::String; };
}  // namespace detail

/// One field of a component: its name, its type, and how to reach it in an object.
struct Field {
    std::string name;                   // as in the code: "turnsPerSecond"
    FieldType type = FieldType::Float;
    /// The address of this field inside a component of the type it belongs to.
    std::function<void*(void* component)> address;

    // How the editor presents it. Set with the functions below.
    float minimum = 0, maximum = 0;     // equal: no limits
    bool isColor = false;               // Vec3 and Vec4 only: edited with a color picker
    std::string tooltip;

    std::vector<std::string> options;   // see choices()
    /// What a text field holds, so that the editor offers what it can be instead of a place to
    /// type it (one wrong letter and nothing works):
    ///   "sound", "picture", "prefab", "scene", "font", "model", "controller", "clip", "timeline",
    ///   "graph", "material", "folder"           a file of the project's Assets, of that kind
    ///   "entity", "child", "entityOrTag", "tag" an entity of the scene (by name), one under this one, a tag
    ///   "bone", "bones", "animation"            of the entity's model: a bone, several, an animation in it
    ///   "value", "message"                      a shared value's name, a message's name (those in use, or a new one)
    ///   "action", "axis"                        an input action of the project, a button or an axis
    ///   "state", "parameter"                    of the entity's Animator Controller
    /// Empty: words typed freely, for what is read by people (a button's label, a text).
    std::string pick;
    /// The pick decided by another field of the component: When / Then's target is a sound for
    /// "Play sound", a scene for "Load scene"...  (that field's value, the pick for it)
    std::string pickBy;
    std::vector<std::pair<std::string, std::string>> pickFor;
    /// Kept with the scene but not shown in the Inspector: data the editor manages (the file of a
    /// terrain's heights, of foliage's copies).
    bool isHidden = false;
    Field& hidden() {
        isHidden = true;
        return *this;
    }
    Field& range(float low, float high) {
        minimum = low;
        maximum = high;
        return *this;
    }
    Field& color() {
        isColor = true;
        return *this;
    }
    /// For a text field that is one of a few words: the inspector shows a list to choose from.
    ///     MOLTEN_FIELD(drive).choices({"front", "rear", "all"})
    Field& choices(std::vector<std::string> words) {
        options = std::move(words);
        return *this;
    }
    /// What it holds, for the editor to offer (see `pick`): MOLTEN_FIELD(clip).picks("sound").
    Field& picks(std::string kind) {
        pick = std::move(kind);
        return *this;
    }
    /// Its pick follows another field: picksBy("action1", {{"Play sound", "sound"}, {"Load scene", "scene"}}).
    Field& picksBy(std::string field, std::vector<std::pair<std::string, std::string>> kinds) {
        pickBy = std::move(field);
        pickFor = std::move(kinds);
        return *this;
    }
    Field& help(std::string text) {
        tooltip = std::move(text);
        return *this;
    }

    /// "turnsPerSecond" as a label: "Turns Per Second".
    std::string label() const;
    /// The field's value in an object as text ("0.5", "0 1 0", a string as it is), and back.
    /// These are what scene files hold.
    std::string valueText(void* component) const;
    void setValueText(void* component, const std::string& text) const;
};

template <class T, class Member> Field makeField(const char* name, Member T::*member) {
    Field field;
    field.name = name;
    field.type = detail::FieldTypeOf<Member>::value;    // a compile error here: the field's type is not one of the seven
    field.address = [member](void* component) -> void* { return &(static_cast<T*>(component)->*member); };
    return field;
}

/// Everything the engine knows about one component type.
/// The components of an entity that are switched off, by type name: they keep their
/// values and their place, and their code does not run.
struct DisabledComponents {
    std::vector<std::string> names;
    bool has(const std::string& name) const {
        for (const std::string& disabled : names) {
            if (disabled == name) return true;
        }
        return false;
    }
};

struct ComponentType {
    std::string name;
    std::vector<Field> fields;
    /// Where the editor's Add Component menu puts it: "Scripts" for a game's own, unless it says otherwise.
    std::string category = "Scripts";
    /// A piece of the game's interface that is not filed under "UI": one no longer offered in the
    /// menus, kept for the scenes that have it. It is still laid out and scaled with the interface.
    bool isInterfacePiece = false;
    /// Drawn with the game's interface: laid out for one screen, scaled to every other.
    bool isInterface() const { return category == "UI" || isInterfacePiece; }
    /// Which game module declared it; nullptr for types built into the program.
    const void* module = nullptr;

    bool (*has)(const Scene&, Entity) = nullptr;
    void* (*get)(Scene&, Entity) = nullptr;                 // nullptr if the entity does not have one
    void* (*add)(Scene&, Entity) = nullptr;                 // a new one with its default values
    void (*remove)(Scene&, Entity) = nullptr;
    void (*copy)(Scene&, Entity from, Entity to) = nullptr;
    void (*collect)(Scene&, std::vector<Entity>& out) = nullptr;    // every entity that has one

    // The component's own code; nullptr where the struct has no such function.
    void (*start)(void* component, World&, Entity) = nullptr;
    void (*update)(void* component, World&, Entity, float dt) = nullptr;
    void (*fixedUpdate)(void* component, World&, Entity, float dt) = nullptr;
    void (*lateUpdate)(void* component, World&, Entity, float dt) = nullptr;     // after every update(), with everything where it will be drawn
    void (*onDestroy)(void* component, World&, Entity) = nullptr;                // its entity is going (destroyed, or the scene is leaving): the last word
    void (*onCollision)(void* component, World&, Entity self, Entity other) = nullptr;
    void (*onTriggerEnter)(void* component, World&, Entity self, Entity other) = nullptr;
    void (*onMessage)(void* component, World&, Entity self, const Message& message) = nullptr;
    void (*draw)(void* component, World&, Entity, Canvas&) = nullptr;

    bool hasCode() const { return start || update || fixedUpdate || lateUpdate || onDestroy || onCollision || onTriggerEnter || onMessage || draw; }
    const Field* findField(const std::string& fieldName) const;
};

/// The component types that have been declared. One for the whole program.
class ComponentRegistry {
public:
    static ComponentRegistry& instance();

    /// Adds a type, or replaces the one with the same name.
    void add(ComponentType type);
    void remove(const std::string& name);
    /// Removes every type a game module declared.
    void removeModule(const void* module);
    /// Types built into the program that a game module replaced with its own of the same name: they return when the module goes.
    std::vector<ComponentType> replaced;

    const ComponentType* find(const std::string& name) const;
    /// Files a type under a category of the Add Component menu. Does nothing if there is no such type.
    void setCategory(const std::string& name, const std::string& category);
    /// Says that a type is a piece of the interface whatever category it is filed under (see ComponentType::isInterfacePiece).
    void setInterfacePiece(const std::string& name);
    /// "PlayerMover" -> "Player Mover": a type's name as people read it.
    static std::string displayName(const std::string& typeName);
    /// A name of its own for a type, where its type's name reads badly: "HudText" is "Text".
    static void setDisplayName(const std::string& typeName, const std::string& shown);
    const std::vector<ComponentType>& types() const { return types_; }

    /// While a game module is being loaded, the module: types declared meanwhile belong to it.
    const void* loadingModule = nullptr;
    /// The module of a game's code linked into the program itself (the iPhone player), where
    /// there is no module to load: its types are declared before the engine's own, and still win.
    static inline const char linkedGameModule = 0;

private:
    std::vector<ComponentType> types_;
};

/// Lines of a scene file that describe components of types nobody has declared (their
/// game module is not loaded, or the type was renamed). Kept on the entity and written
/// back as they were, so that opening and saving a scene never loses them.
struct UnknownComponents {
    std::vector<std::string> lines;
};

/// MOLTEN_COMPONENT makes one of these: it declares the type when the program (or the
/// game module) starts.
template <class T> struct ComponentRegistration {
    ComponentRegistration(const char* name, std::vector<Field> fields) {
        ComponentType type;
        type.name = name;
        type.fields = std::move(fields);
        type.module = ComponentRegistry::instance().loadingModule;
#ifdef MOLTEN_GAME_CODE
        if (!type.module) type.module = &ComponentRegistry::linkedGameModule;
#endif
        type.has = [](const Scene& scene, Entity entity) { return scene.has<T>(entity); };
        type.get = [](Scene& scene, Entity entity) -> void* { return scene.get<T>(entity); };
        type.add = [](Scene& scene, Entity entity) -> void* { return &scene.add(entity, T{}); };
        type.remove = [](Scene& scene, Entity entity) { scene.remove<T>(entity); };
        type.copy = [](Scene& scene, Entity from, Entity to) {
            if (const T* source = scene.get<T>(from)) {
                T value = *source;      // a copy first: adding may move the storage `source` points into
                scene.add(to, std::move(value));
            }
        };
        type.collect = [](Scene& scene, std::vector<Entity>& out) {
            scene.each<T>([&out](Entity entity, T&) { out.push_back(entity); });
        };
        // Each function a component may have, in either of its two forms: taking a World
        // (works with or without a screen) or an Engine (the World plus renderer, assets,
        // sound). The second kind is simply not called in a World that is not an Engine.
#define MOLTEN_HOOK(member, name, ...)                                                                                    \
    if constexpr (requires(T& c, World& w) { c.name(w, __VA_ARGS__); }) {                                                  \
        type.member = [](void* component, World& world, MOLTEN_HOOK_PARAMS) { static_cast<T*>(component)->name(world, MOLTEN_HOOK_ARGS); }; \
    } else if constexpr (requires(T& c, Engine& e) { c.name(e, __VA_ARGS__); }) {                                          \
        type.member = [](void* component, World& world, MOLTEN_HOOK_PARAMS) {                                              \
            if (Engine* engine = world.asEngine()) static_cast<T*>(component)->name(*engine, MOLTEN_HOOK_ARGS);            \
        };                                                                                                                \
    }
        Entity self{}, other{};
        float dt = 0;
        (void)self, (void)other, (void)dt;
#define MOLTEN_HOOK_PARAMS Entity self_
#define MOLTEN_HOOK_ARGS self_
        MOLTEN_HOOK(start, start, self)
        MOLTEN_HOOK(onDestroy, onDestroy, self)
#undef MOLTEN_HOOK_PARAMS
#undef MOLTEN_HOOK_ARGS
#define MOLTEN_HOOK_PARAMS Entity self_, float dt_
#define MOLTEN_HOOK_ARGS self_, dt_
        MOLTEN_HOOK(update, update, self, dt)
        MOLTEN_HOOK(fixedUpdate, fixedUpdate, self, dt)
        MOLTEN_HOOK(lateUpdate, lateUpdate, self, dt)
#undef MOLTEN_HOOK_PARAMS
#undef MOLTEN_HOOK_ARGS
#define MOLTEN_HOOK_PARAMS Entity self_, Entity other_
#define MOLTEN_HOOK_ARGS self_, other_
        MOLTEN_HOOK(onCollision, onCollision, self, other)
        MOLTEN_HOOK(onTriggerEnter, onTriggerEnter, self, other)
#undef MOLTEN_HOOK_PARAMS
#undef MOLTEN_HOOK_ARGS
#define MOLTEN_HOOK_PARAMS Entity self_, const Message& message_
#define MOLTEN_HOOK_ARGS self_, message_
        MOLTEN_HOOK(onMessage, onMessage, self, std::declval<const Message&>())
#undef MOLTEN_HOOK_PARAMS
#undef MOLTEN_HOOK_ARGS
#define MOLTEN_HOOK_PARAMS Entity self_, Canvas& canvas_
#define MOLTEN_HOOK_ARGS self_, canvas_
        MOLTEN_HOOK(draw, draw, self, std::declval<Canvas&>())
#undef MOLTEN_HOOK_PARAMS
#undef MOLTEN_HOOK_ARGS
#undef MOLTEN_HOOK
        ComponentRegistry::instance().add(std::move(type));
    }
};

}  // namespace molten

/// Declares a component type and the fields the editor shows and scenes save.
/// Put it after the struct, at file level, in a .cpp file.
#define MOLTEN_COMPONENT(Type, ...)                                                             \
    static const ::molten::ComponentRegistration<Type> moltenRegistration_##Type(#Type, [] {    \
        using Self [[maybe_unused]] = Type;                                                      \
        return std::vector<::molten::Field>{__VA_ARGS__};                                        \
    }());

/// One field of the component being declared. Follow it with .range(0, 10), .color() or
/// .help("...") to say how the editor presents it.
#define MOLTEN_FIELD(member) ::molten::makeField(#member, &Self::member)
