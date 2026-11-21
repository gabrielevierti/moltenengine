// WhenThen.cpp
#include "Kit/WhenThen.hpp"

#include "Animation/Timeline.hpp"
#include "Core/Log.hpp"
#include "Game/Component.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Saveable.hpp"

namespace molten::kit {

void linkWhenThen() {}

namespace {

/// "self", the one that entered ("other"), a name, or (failing that) a tag.
Entity entityFor(World& world, Entity self, Entity other, const std::string& target) {
    if (target.empty() || target == "self") return self;
    if (target == "other") return other;
    Entity named = world.find(target);
    return named.isValid() ? named : world.findWithTag(target);
}

bool isWho(World& world, Entity other, const std::string& who) {
    if (who.empty()) return true;
    const Name* name = world.scene.get<Name>(other);
    const Tag* tag = world.scene.get<Tag>(other);
    // The entity, or anything under the one with that name (a car's body, its wheels).
    for (Entity at = other; world.scene.isAlive(at);) {
        const Name* above = world.scene.get<Name>(at);
        if (above && above->value == who) return true;
        const Transform* transform = world.scene.get<Transform>(at);
        if (!transform) break;
        at = transform->parent;
    }
    return (name && name->value == who) || (tag && tag->value == who);
}

}  // namespace

void WhenThen::act(World& world, Entity self, Entity other, const std::string& action, const std::string& target, float amount) {
    if (action.empty() || action == "Nothing") return;
    if (action == "Show" || action == "Hide" || action == "Switch") {
        if (Transform* transform = world.scene.get<Transform>(entityFor(world, self, other, target))) {
            transform->isActive = action == "Show" ? true : action == "Hide" ? false : !transform->isActive;
        }
    } else if (action == "Destroy") {
        Entity entity = entityFor(world, self, other, target);
        if (world.scene.isAlive(entity)) world.release(entity);         // back to its pool, if it came from one
    } else if (action == "Play sound") {
        world.playSound(target, amount);
    } else if (action == "Start timeline") {
        if (TimelinePlayer* player = world.scene.get<TimelinePlayer>(entityFor(world, self, other, target))) {
            player->seek(world, 0);
            player->hasEnded = false;
            player->play();
        }
    } else if (action == "Load scene") {
        world.loadScene(target);
    } else if (action == "Load scene in background") {
        world.loadSceneInBackground(target);
    } else if (action == "Add scene") {
        world.addScene(target);
    } else if (action == "Remove scene") {
        world.removeScene(target);
    } else if (action == "Restart scene") {
        world.reloadScene();
    } else if (action == "Set value") {
        world.setValue(target, amount);
    } else if (action == "Add to value") {
        world.addValue(target, amount);
    } else if (action == "Send message") {
        world.send(target, Entity{}, amount, "", self);
    } else if (action == "Spawn prefab") {
        const Transform* here = world.scene.get<Transform>(self);
        world.take(target, here ? here->worldPosition() : Vec3{0, 0, 0});
    } else if (action == "Move here") {
        // What is named goes to where this entity is (a checkpoint, a teleporter).
        Entity mover = entityFor(world, self, other, target.empty() ? "other" : target);
        const Transform* here = world.scene.get<Transform>(self);
        if (here && world.scene.isAlive(mover)) world.physics.teleport(world.scene, mover, here->worldPosition());
    } else if (action == "Save game") {
        saveState(world, target.empty() ? std::string("Save") : target);
    } else if (action == "Load game") {
        loadState(world, target.empty() ? std::string("Save") : target);
    } else {
        logWarning("When/Then: \"%s\" is not something it can do", action.c_str());
    }
}

void WhenThen::happen(World& world, Entity self, Entity other) {
    if (once && times > 0) return;
    times++;
    std::string actions[4] = {action1, action2, action3, action4}, targets[4] = {target1, target2, target3, target4};
    float amounts[4] = {amount1, amount2, amount3, amount4};
    auto doAll = [&world, self, other, actions, targets, amounts] {
        for (int k = 0; k < 4; k++) act(world, self, other, actions[k], targets[k], amounts[k]);
    };
    if (seconds > 0 && when != "Every few seconds") world.after(seconds, doAll, self);
    else doAll();
}

void WhenThen::start(World& world, Entity self) {
    if (when == "It starts") happen(world, self, Entity{});
}

void WhenThen::update(World& world, Entity self, float dt) {
    if (when == "A value reaches") {
        bool reached = world.value(value) >= atLeast;
        if (reached && !wasReached) happen(world, self, Entity{});
        wasReached = reached;
    } else if (when == "It is clicked") {
        if (const UIButton* button = world.scene.get<UIButton>(self); button && button->wasClicked) happen(world, self, Entity{});
    } else if (when == "Every few seconds") {
        waited += dt;
        if (seconds > 0 && waited >= seconds) {
            waited -= seconds;
            happen(world, self, Entity{});
        }
    }
}

void WhenThen::onTriggerEnter(World& world, Entity self, Entity other) {
    if (when == "Something enters" && isWho(world, other, who)) happen(world, self, other);
}

void WhenThen::onMessage(World& world, Entity self, const Message& heard) {
    if (when == "A message" && heard.name == message) happen(world, self, heard.from);
}

}  // namespace molten::kit

using namespace molten::kit;

namespace {
const std::vector<std::string> kActions = {"Nothing", "Show", "Hide", "Switch", "Destroy", "Play sound", "Start timeline", "Load scene", "Load scene in background",
                                           "Add scene", "Remove scene", "Restart scene",
                                           "Set value", "Add to value", "Send message", "Spawn prefab", "Move here", "Save game", "Load game"};
// What each action's target is, for the editor to offer the right things.
const std::vector<std::pair<std::string, std::string>> kTargetKinds = {
    {"Nothing", "none"}, {"Show", "target"}, {"Hide", "target"}, {"Switch", "target"}, {"Destroy", "target"}, {"Play sound", "sound"},
    {"Start timeline", "target"}, {"Load scene", "scene"}, {"Load scene in background", "scene"}, {"Add scene", "scene"},
    {"Remove scene", "scene"}, {"Restart scene", "none"}, {"Set value", "value"}, {"Add to value", "value"}, {"Send message", "message"},
    {"Spawn prefab", "prefab"}, {"Move here", "target"}, {"Save game", ""}, {"Load game", ""}};
const char* kTargetHelp = "What it acts on: an entity's name (or tag), \"self\", \"other\" (who entered), a sound, a scene, a value's name";
}  // namespace

MOLTEN_COMPONENT(WhenThen,
                 MOLTEN_FIELD(when).choices({"Something enters", "A message", "It starts", "A value reaches", "It is clicked", "Every few seconds"}),
                 MOLTEN_FIELD(who).picks("entityOrTag").help("For Something enters: a name or a tag; empty: anything"),
                 MOLTEN_FIELD(message).picks("message").help("For A message: its name"), MOLTEN_FIELD(value).picks("value").help("For A value reaches: the shared value"),
                 MOLTEN_FIELD(atLeast), MOLTEN_FIELD(seconds).range(0, 600).help("Wait this long before doing it; for Every few seconds, how often"),
                 MOLTEN_FIELD(once).help("Only the first time"), MOLTEN_FIELD(action1).choices(kActions), MOLTEN_FIELD(target1).picksBy("action1", kTargetKinds).help(kTargetHelp),
                 MOLTEN_FIELD(amount1).help("A number: the value, the volume"), MOLTEN_FIELD(action2).choices(kActions), MOLTEN_FIELD(target2).picksBy("action2", kTargetKinds).help(kTargetHelp),
                 MOLTEN_FIELD(amount2), MOLTEN_FIELD(action3).choices(kActions), MOLTEN_FIELD(target3).picksBy("action3", kTargetKinds).help(kTargetHelp), MOLTEN_FIELD(amount3),
                 MOLTEN_FIELD(action4).choices(kActions), MOLTEN_FIELD(target4).picksBy("action4", kTargetKinds).help(kTargetHelp), MOLTEN_FIELD(amount4))
