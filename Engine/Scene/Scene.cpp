#include "Scene/Scene.hpp"

namespace molten {

Entity Scene::create(std::string_view name) {
    uint32_t index;
    if (!freeIndices_.empty()) {
        index = freeIndices_.back();
        freeIndices_.pop_back();
    } else {
        index = uint32_t(generations_.size());
        generations_.push_back(0);
        alive_.push_back(0);
        doomed_.push_back(0);
    }
    alive_[index] = 1;
    doomed_[index] = 0;
    liveCount_++;
    Entity entity{index, generations_[index]};
    if (!name.empty()) add(entity, Name{std::string(name)});
    return entity;
}

void Scene::destroy(Entity entity) {
    if (!isAlive(entity) || doomed_[entity.index]) return;
    doomed_[entity.index] = 1;
    pendingDestroy_.push_back(entity);
}

Entity Scene::find(std::string_view name) const {
    const detail::Storage<Name>* names = findStorage<Name>();
    if (!names) return Entity{};
    for (size_t i = 0; i < names->dense.size(); i++) {
        if (names->dense[i].value == name) return Entity{names->owners[i], generations_[names->owners[i]]};
    }
    return Entity{};
}

void Scene::clear() {
    for (auto& storage : storages_) {
        if (storage) storage->clear();
    }
    for (uint32_t index = 0; index < generations_.size(); index++) {
        if (alive_[index]) {
            alive_[index] = 0;
            generations_[index]++;
            freeIndices_.push_back(index);
        }
        doomed_[index] = 0;
    }
    pendingDestroy_.clear();
    liveCount_ = 0;
}

void Scene::setParent(Entity child, Entity parent) {
    if (Transform* transform = get<Transform>(child)) transform->parent = parent;
}

const Mat4& Scene::resolveWorld(detail::Storage<Transform>& transforms, uint32_t index) {
    Transform& transform = *transforms.find(index);
    if (transform.stamp == transformStamp_) return transform.world;
    transform.stamp = transformStamp_;          // set first, so a parent cycle cannot recurse forever
    Mat4 local = transform.localMatrix();
    Entity parent = transform.parent;
    if (parent.isValid() && isAlive(parent) && transforms.contains(parent.index)) {
        transform.world = resolveWorld(transforms, parent.index) * local;
    } else {
        transform.world = local;
    }
    return transform.world;
}

void Scene::updateTransforms() {
    detail::Storage<Transform>& transforms = storage<Transform>();
    transformStamp_++;
    for (size_t i = 0; i < transforms.dense.size(); i++) {
        resolveWorld(transforms, transforms.owners[i]);
    }
}

void Scene::flushDestroyed() {
    if (pendingDestroy_.empty()) return;

    // Children go with their parents. Repeat until no new entity is caught, so that
    // grandchildren are found whatever order the transforms are stored in.
    detail::Storage<Transform>& transforms = storage<Transform>();
    bool foundMore = true;
    while (foundMore) {
        foundMore = false;
        for (size_t i = 0; i < transforms.dense.size(); i++) {
            uint32_t index = transforms.owners[i];
            Entity parent = transforms.dense[i].parent;
            if (!doomed_[index] && parent.isValid() && isAlive(parent) && doomed_[parent.index]) {
                doomed_[index] = 1;
                pendingDestroy_.push_back(Entity{index, generations_[index]});
                foundMore = true;
            }
        }
    }

    for (Entity entity : pendingDestroy_) {
        for (auto& storage : storages_) {
            if (storage) storage->remove(entity.index);
        }
        alive_[entity.index] = 0;
        doomed_[entity.index] = 0;
        generations_[entity.index]++;
        freeIndices_.push_back(entity.index);
        liveCount_--;
    }
    pendingDestroy_.clear();
}

}  // namespace molten
