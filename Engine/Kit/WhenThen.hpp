// WhenThen.hpp
// Game logic without code: WHEN something happens, THEN up to four things are done. Set in the
// Inspector, as words chosen from lists.
//
//   when   "Something enters"   something (named, or tagged, as `who` says) enters this
//                               entity's trigger collider (a Collider with Is Trigger)
//          "A message"          the message named in `message` is sent (to it, or to everyone)
//          "It starts"          the game starts, or the entity appears
//          "A value reaches"    the shared value `value` reaches `atLeast`
//          "It is clicked"      its button (a UI Button on it) is clicked
//          "Every few seconds"  again and again, every `seconds`
//   then   Show, Hide, Switch (on/off), Destroy, Play sound, Start timeline, Load scene,
//          Restart scene, Set value, Add to value, Send message, Spawn prefab, Move here,
//          Save game, Load game (the slot: target; empty is "Save")
//          each with what it acts on (`target`: an entity's name, "self", a file, a value's name)
//          and a number (`amount`).
//   seconds  for the others: how long to wait before doing them
//   once     only the first time
#pragma once

#include "Core/World.hpp"

#include <string>

namespace molten::kit {

struct WhenThen {
    std::string when = "Something enters";
    std::string who;                    // for "Something enters": a name or a tag; empty: anything
    std::string message = "doorOpened";
    std::string value = "score";
    float atLeast = 10;
    float seconds = 0;
    bool once = true;

    std::string action1 = "Show", target1, action2 = "Nothing", target2, action3 = "Nothing", target3, action4 = "Nothing", target4;
    float amount1 = 1, amount2 = 1, amount3 = 1, amount4 = 1;

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);
    void onTriggerEnter(World& world, Entity self, Entity other);
    void onMessage(World& world, Entity self, const Message& message);

    /// Does the four (after `seconds`, unless it is "Every few seconds"), if it may happen again.
    void happen(World& world, Entity self, Entity other);
    /// One action, now.
    static void act(World& world, Entity self, Entity other, const std::string& action, const std::string& target, float amount);

    int times = 0;
    bool wasReached = false;
    float waited = 0;
};

void linkWhenThen();

}  // namespace molten::kit
