// Tasks.cpp
#include "Core/Tasks.hpp"

#include <algorithm>

namespace molten {

int Tasks::add(Entry entry, Entity owner) {
    entry.id = nextId_++;
    // A wait inside a coroutine belongs to the coroutine's owner.
    if (!owner.isValid() && currentOwner_.isValid()) owner = currentOwner_;
    entry.owner = owner;
    entry.hasOwner = owner.isValid();
    entries_.push_back(std::move(entry));
    return entries_.back().id;
}

int Tasks::after(double seconds, Action action, Entity owner) {
    Entry entry;
    entry.due = clock_ + std::max(seconds, 0.0);
    entry.action = std::move(action);
    return add(std::move(entry), owner);
}

int Tasks::every(double seconds, Action action, Entity owner) {
    Entry entry;
    entry.every = std::max(seconds, 1e-4);
    entry.due = clock_ + entry.every;
    entry.action = std::move(action);
    return add(std::move(entry), owner);
}

int Tasks::when(Condition condition, Action action, Entity owner) {
    Entry entry;
    entry.condition = std::move(condition);
    entry.action = std::move(action);
    return add(std::move(entry), owner);
}

void Tasks::cancel(int id) {
    for (Entry& entry : entries_) {
        if (entry.id == id) entry.isDone = true;
    }
}

void Tasks::cancelOwnedBy(Entity owner) {
    for (Entry& entry : entries_) {
        if (entry.hasOwner && entry.owner == owner) entry.isDone = true;
    }
    for (size_t i = 0; i < coroutines_.size();) {
        if (coroutines_[i].second == owner) {
            coroutines_[i].first.destroy();
            coroutines_.erase(coroutines_.begin() + long(i));
        } else {
            i++;
        }
    }
}

void Tasks::clear() {
    entries_.clear();
    for (auto& [handle, owner] : coroutines_) handle.destroy();
    coroutines_.clear();
}

Tasks::~Tasks() {
    entries_.clear();
    for (auto& [handle, owner] : coroutines_) handle.destroy();
}

void Tasks::adopt(std::coroutine_handle<Task::promise_type> handle, Entity owner) {
    if (!handle) return;
    // Its waits belong to its owner: the owner gone, nothing resumes it.
    Entity before = currentOwner_;
    currentOwner_ = owner;
    handle.resume();
    currentOwner_ = before;
    if (handle.done()) {
        handle.destroy();
        return;
    }
    coroutines_.push_back({handle, owner});
}

void Tasks::Awaiter::await_suspend(std::coroutine_handle<> handle) const {
    auto resume = [handle] { handle.resume(); };
    if (condition) tasks->when(condition, resume);
    else tasks->after(std::max(seconds, 0.0), resume);
}

void Tasks::run(double dt, const std::function<bool(Entity)>& isAlive) {
    clock_ += dt;
    // By index, and only those there now: what runs may add more (for the next frame).
    size_t count = entries_.size();
    for (size_t i = 0; i < count; i++) {
        if (entries_[i].isDone) continue;
        if (entries_[i].hasOwner && !isAlive(entries_[i].owner)) {
            entries_[i].isDone = true;
            continue;
        }
        bool isDue = entries_[i].condition ? entries_[i].condition() : clock_ >= entries_[i].due;
        if (!isDue) continue;
        Action action = entries_[i].action;         // a copy: the list may grow while it runs
        Entity owner = entries_[i].owner;
        if (entries_[i].every > 0) entries_[i].due += entries_[i].every;
        else entries_[i].isDone = true;
        currentOwner_ = owner;
        if (action) action();
        currentOwner_ = Entity{};
    }
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& entry) { return entry.isDone; }), entries_.end());
    // Coroutines that ended, or whose owner is gone, are let go.
    for (size_t i = 0; i < coroutines_.size();) {
        auto [handle, owner] = coroutines_[i];
        bool ownerGone = owner.isValid() && !isAlive(owner);
        if (handle.done() || ownerGone) {
            if (ownerGone) {
                // Its pending wait must not resume it after it is destroyed.
                for (Entry& entry : entries_) {
                    if (entry.hasOwner && entry.owner == owner) entry.isDone = true;
                }
            }
            handle.destroy();
            coroutines_.erase(coroutines_.begin() + long(i));
        } else {
            i++;
        }
    }
}

}  // namespace molten
