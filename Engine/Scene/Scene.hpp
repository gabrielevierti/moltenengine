// Scene.hpp
// A small entity-component system.
//
// An Entity is only an id. Data lives in components, plain structs that are stored
// packed together by type (a "sparse set" per component type), so that a system which
// walks every Transform or every RigidBody reads memory in order.
//
//     Entity crate = scene.create("Crate");
//     scene.add(crate, Transform{.position = {0, 2, 0}});
//     scene.add(crate, RigidBody{});
//
//     scene.each<Transform, RigidBody>([](Entity e, Transform& t, RigidBody& body) { ... });
#pragma once

#include "Math/Math.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molten {

/// A handle to an entity. The generation makes stale handles detectable: when an entity
/// is destroyed its index is reused, but with a higher generation.
struct Entity {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool isValid() const { return index != UINT32_MAX; }
    bool operator==(const Entity& other) const { return index == other.index && generation == other.generation; }
    bool operator!=(const Entity& other) const { return !(*this == other); }
};

/// Where an entity is. Positions, rotations and scales are relative to `parent`
/// when there is one. `world` is filled in by Scene::updateTransforms().
struct Transform {
    Vec3 position{0, 0, 0};
    Quat rotation{};
    Vec3 scale{1, 1, 1};
    Entity parent{};
    Mat4 world = identity();
    /// Off: the entity, and everything under it, is not drawn, does not collide, and its
    /// components do not run. It stays in the scene, ready to be switched on.
    bool isActive = true;
    /// This entity and all its parents are active. Filled in by Scene::updateTransforms().
    bool isActiveInHierarchy = true;
    uint32_t stamp = 0;            // internal: the update that last computed `world`

    Mat4 localMatrix() const { return compose(position, rotation, scale); }
    Vec3 forward() const { return rotate(rotation, Vec3{0, 0, -1}); }
    Vec3 right() const { return rotate(rotation, Vec3{1, 0, 0}); }
    Vec3 up() const { return rotate(rotation, Vec3{0, 1, 0}); }
    Vec3 worldPosition() const { return xyz(world.columns[3]); }
};

/// What kind of thing an entity is to the game ("Player", "Enemy"), and the layer it is on.
/// Many entities share a tag; code asks for them by it, whatever they are called.
struct Tag {
    std::string value = "Untagged";
    int layer = 0;                  // 0 to 31: what it collides with, see Collider
};

struct Name {
    std::string value;
};

namespace detail {

inline uint32_t nextComponentId() {
    static uint32_t next = 0;
    return next++;
}

/// A different small integer for every component type, assigned on first use.
template <class T> uint32_t componentId() {
    static const uint32_t id = nextComponentId();
    return id;
}

struct StorageBase {
    virtual ~StorageBase() = default;
    virtual void remove(uint32_t entityIndex) = 0;
    virtual void clear() = 0;
};

/// Sparse set: `dense` holds the components back to back, `owners[i]` is the entity that
/// owns `dense[i]`, and `sparse[entityIndex]` is the position in `dense`, or -1.
template <class T> struct Storage final : StorageBase {
    std::vector<T> dense;
    std::vector<uint32_t> owners;
    std::vector<int32_t> sparse;

    bool contains(uint32_t entityIndex) const {
        return entityIndex < sparse.size() && sparse[entityIndex] >= 0;
    }

    T* find(uint32_t entityIndex) { return contains(entityIndex) ? &dense[size_t(sparse[entityIndex])] : nullptr; }
    const T* find(uint32_t entityIndex) const { return contains(entityIndex) ? &dense[size_t(sparse[entityIndex])] : nullptr; }

    T& set(uint32_t entityIndex, T value) {
        if (T* existing = find(entityIndex)) {
            *existing = std::move(value);
            return *existing;
        }
        if (entityIndex >= sparse.size()) sparse.resize(size_t(entityIndex) + 1, -1);
        sparse[entityIndex] = int32_t(dense.size());
        dense.push_back(std::move(value));
        owners.push_back(entityIndex);
        return dense.back();
    }

    /// Removes by moving the last component into the hole, so `dense` stays packed.
    void remove(uint32_t entityIndex) override {
        if (!contains(entityIndex)) return;
        size_t hole = size_t(sparse[entityIndex]);
        size_t last = dense.size() - 1;
        if (hole != last) {
            dense[hole] = std::move(dense[last]);
            owners[hole] = owners[last];
            sparse[owners[hole]] = int32_t(hole);
        }
        dense.pop_back();
        owners.pop_back();
        sparse[entityIndex] = -1;
    }

    void clear() override {
        dense.clear();
        owners.clear();
        sparse.clear();
    }
};

}  // namespace detail

class Scene {
public:
    // ---- entities

    Entity create(std::string_view name = {});

    /// Marks the entity, and every entity parented to it, for destruction. They disappear
    /// in flushDestroyed(), at the end of the frame, so it is always safe to call this
    /// from inside each() or from a collision callback.
    void destroy(Entity entity);

    bool isAlive(Entity entity) const {
        return entity.index < generations_.size() && alive_[entity.index] && generations_[entity.index] == entity.generation;
    }

    size_t entityCount() const { return liveCount_; }
    Entity find(std::string_view name) const;
    void clear();
    /// After clear(): also forgets the containers the components were kept in. Needed
    /// before a game module is replaced, because a container of a game's component type
    /// was made by the old module's code and must not be handed to the new one.
    void dropStorages() { storages_.clear(); }

    // ---- components

    /// Adds the component, or replaces the one the entity already has.
    template <class T> T& add(Entity entity, T component = T{}) {
        assert(isAlive(entity) && "adding a component to an entity that no longer exists");
        return storage<T>().set(entity.index, std::move(component));
    }

    /// The entity's component, or nullptr. The pointer stays valid until a component of
    /// the same type is added or removed.
    template <class T> T* get(Entity entity) {
        return isAlive(entity) ? storage<T>().find(entity.index) : nullptr;
    }

    template <class T> const T* get(Entity entity) const {
        const detail::Storage<T>* found = findStorage<T>();
        return found && isAlive(entity) ? found->find(entity.index) : nullptr;
    }

    template <class T> bool has(Entity entity) const { return get<T>(entity) != nullptr; }
    template <class T> void remove(Entity entity) {
        if (isAlive(entity)) storage<T>().remove(entity.index);
    }
    template <class T> size_t count() const {
        const detail::Storage<T>* found = findStorage<T>();
        return found ? found->dense.size() : 0;
    }

    /// Calls fn(Entity, First&, Rest&...) for every entity that has all the listed components.
    /// Do not add or remove components of the listed types inside fn.
    template <class First, class... Rest, class Fn> void each(Fn&& fn) {
        detail::Storage<First>& primary = storage<First>();
        for (size_t i = 0; i < primary.dense.size(); i++) {
            uint32_t index = primary.owners[i];
            if ((storage<Rest>().contains(index) && ...)) {
                fn(Entity{index, generations_[index]}, primary.dense[i], *storage<Rest>().find(index)...);
            }
        }
    }

    // ---- hierarchy

    void setParent(Entity child, Entity parent);

    /// Recomputes Transform::world for every entity, parents before children.
    void updateTransforms();

    /// Actually removes the entities passed to destroy(). Called by the engine once per frame.
    void flushDestroyed();

private:
    template <class T> detail::Storage<T>& storage() {
        uint32_t id = detail::componentId<T>();
        if (id >= storages_.size()) storages_.resize(size_t(id) + 1);
        if (!storages_[id]) storages_[id] = std::make_unique<detail::Storage<T>>();
        return static_cast<detail::Storage<T>&>(*storages_[id]);
    }

    template <class T> const detail::Storage<T>* findStorage() const {
        uint32_t id = detail::componentId<T>();
        if (id >= storages_.size() || !storages_[id]) return nullptr;
        return static_cast<const detail::Storage<T>*>(storages_[id].get());
    }

    const Mat4& resolveWorld(detail::Storage<Transform>& transforms, uint32_t index);

    std::vector<uint32_t> generations_;
    std::vector<uint8_t> alive_;
    std::vector<uint8_t> doomed_;
    std::vector<uint32_t> freeIndices_;
    std::vector<Entity> pendingDestroy_;
    std::vector<std::unique_ptr<detail::StorageBase>> storages_;
    size_t liveCount_ = 0;
    uint32_t transformStamp_ = 0;
};

}  // namespace molten
