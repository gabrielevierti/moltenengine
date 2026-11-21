// Tasks.hpp
// Things to do later, in game time: "in 2 seconds", "every half second", "when the door is
// open". Two ways to write them, as one prefers:
//
//     world.after(2, [&] { door.open(); });
//     world.when([&] { return world.value("keys") >= 3; }, [&] { gate.open(); });
//
// or, for a sequence, as one function that waits (a C++20 coroutine):
//
//     molten::Task intro(molten::World& world) {
//         world.setValue("lights", 1);
//         co_await world.seconds(2);
//         world.playSound("Audio/horn.wav");
//         co_await world.until([&] { return world.value("ready") > 0; });
//         world.loadScene("Scenes/Race.scene");
//     }
//     world.start(intro(world), self);       // stops if `self` is destroyed first
//
// Game time: paused when the game is, slowed with timeScale. A task given an owner stops when
// the owner is destroyed; every task stops when the scene changes.
#pragma once

#include "Scene/Scene.hpp"

#include <coroutine>
#include <functional>
#include <memory>
#include <vector>

namespace molten {

/// A function that can wait: returned by a coroutine, handed to World::start.
class Task {
public:
    struct promise_type {
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }      // World::start runs it, knowing whose it is
        std::suspend_always final_suspend() noexcept { return {}; }        // kept until its owner lets it go
        void return_void() {}
        void unhandled_exception() {}
    };

    Task() = default;
    explicit Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = {}; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = {};
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { reset(); }

    bool isDone() const { return !handle_ || handle_.done(); }
    std::coroutine_handle<promise_type> release() {
        auto handle = handle_;
        handle_ = {};
        return handle;
    }

private:
    void reset() {
        if (handle_) handle_.destroy();
        handle_ = {};
    }
    std::coroutine_handle<promise_type> handle_;
};

/// The waiting things of a World, run each frame after the components' update.
class Tasks {
public:
    using Action = std::function<void()>;
    using Condition = std::function<bool()>;

    /// `action` once, `seconds` from now. Returns an id for cancel().
    int after(double seconds, Action action, Entity owner = {});
    /// `action` again and again, every `seconds`.
    int every(double seconds, Action action, Entity owner = {});
    /// `action` once, the first frame `condition` holds (checked each frame).
    int when(Condition condition, Action action, Entity owner = {});
    void cancel(int id);
    /// What `owner` was waiting for does not happen (it was put back in its pool).
    void cancelOwnedBy(Entity owner);
    /// Every task stops (the scene changes); coroutines are let go.
    void clear();
    size_t count() const { return entries_.size(); }

    /// One frame: the clock moves on by `dt` and what is due runs. `isAlive` says whether an
    /// owner still exists.
    void run(double dt, const std::function<bool(Entity)>& isAlive);
    double now() const { return clock_; }

    /// For World::start: runs a coroutine until it first waits, then keeps it until it ends.
    void adopt(std::coroutine_handle<Task::promise_type> handle, Entity owner);
    ~Tasks();

    /// What `co_await` waits on: a time, or a condition.
    struct Awaiter {
        Tasks* tasks;
        double seconds = -1;
        Condition condition;
        bool await_ready() const { return seconds == 0 || (condition && condition()); }
        void await_suspend(std::coroutine_handle<> handle) const;
        void await_resume() const {}
    };

private:
    struct Entry {
        int id = 0;
        double due = 0, every = 0;
        Action action;
        Condition condition;
        Entity owner;
        bool hasOwner = false;
        bool isDone = false;
    };
    std::vector<Entry> entries_;
    std::vector<std::pair<std::coroutine_handle<Task::promise_type>, Entity>> coroutines_;
    std::coroutine_handle<> resuming_;          // the coroutine whose wait is being added
    Entity currentOwner_;
    int nextId_ = 1;
    double clock_ = 0;
    int add(Entry entry, Entity owner);
};

}  // namespace molten
