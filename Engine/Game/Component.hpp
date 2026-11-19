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
//         // Any of these three, if the component does something:
//         void start(molten::Engine& engine, molten::Entity self);
//         void update(molten::Engine& engine, molten::Entity self, float dt);
//         void fixedUpdate(molten::Engine& engine, molten::Entity self, float dt);
//     };
//     MOLTEN_COMPONENT(Spinner, MOLTEN_FIELD(turnsPerSecond), MOLTEN_FIELD(axis))
//
// Fields may be float, int, bool, Vec2, Vec3, Vec4 and std::string. A field that is not
// listed still exists; it is just not shown or saved (a timer, a cached pointer).
#pragma once

#include "Scene/Scene.hpp"

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace molten {

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

    Field& range(float low, float high) {
        minimum = low;
        maximum = high;
        return *this;
    }
    Field& color() {
        isColor = true;
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
struct ComponentType {
    std::string name;
    std::vector<Field> fields;
    /// Which game module declared it; nullptr for types built into the program.
    const void* module = nullptr;

    bool (*has)(const Scene&, Entity) = nullptr;
    void* (*get)(Scene&, Entity) = nullptr;                 // nullptr if the entity does not have one
    void* (*add)(Scene&, Entity) = nullptr;                 // a new one with its default values
    void (*remove)(Scene&, Entity) = nullptr;
    void (*copy)(Scene&, Entity from, Entity to) = nullptr;
    void (*collect)(Scene&, std::vector<Entity>& out) = nullptr;    // every entity that has one

    // The component's own code; nullptr where the struct has no such function.
    void (*start)(void* component, Engine&, Entity) = nullptr;
    void (*update)(void* component, Engine&, Entity, float dt) = nullptr;
    void (*fixedUpdate)(void* component, Engine&, Entity, float dt) = nullptr;

    bool hasCode() const { return start || update || fixedUpdate; }
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

    const ComponentType* find(const std::string& name) const;
    const std::vector<ComponentType>& types() const { return types_; }

    /// While a game module is being loaded, the module: types declared meanwhile belong to it.
    const void* loadingModule = nullptr;

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
        if constexpr (requires(T& component, Engine& engine, Entity self) { component.start(engine, self); }) {
            type.start = [](void* component, Engine& engine, Entity self) { static_cast<T*>(component)->start(engine, self); };
        }
        if constexpr (requires(T& component, Engine& engine, Entity self, float dt) { component.update(engine, self, dt); }) {
            type.update = [](void* component, Engine& engine, Entity self, float dt) { static_cast<T*>(component)->update(engine, self, dt); };
        }
        if constexpr (requires(T& component, Engine& engine, Entity self, float dt) { component.fixedUpdate(engine, self, dt); }) {
            type.fixedUpdate = [](void* component, Engine& engine, Entity self, float dt) {
                static_cast<T*>(component)->fixedUpdate(engine, self, dt);
            };
        }
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
