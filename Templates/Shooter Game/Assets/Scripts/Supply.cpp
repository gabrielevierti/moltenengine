// Supply.cpp
// Rounds, or health, to pick up.
// This game's own script: change it as the game needs.
#include "Shooting.hpp"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- Supply, DamageFlash

void Supply::onTriggerEnter(World& world, Entity self, Entity other) {
    if (isTaken || !isNamed(world, other, collector)) return;
    Scene& scene = world.scene;
    bool taken = false;
    if (rounds > 0) {
        int given = rounds;
        scene.each<Weapon, Transform>([&](Entity entity, Weapon& weapon, Transform&) {
            if (taken || (entity != other && !Physics::isUnder(scene, entity, other))) return;
            weapon.addRounds(given);
            taken = true;
        });
    }
    if (health > 0) {
        if (kit::Health* theirs = scene.get<kit::Health>(other); theirs && theirs->amount < mostHealth) {
            theirs->amount = std::min(theirs->amount + health, mostHealth);
            if (!theirs->counter.empty()) world.setValue(theirs->counter, theirs->amount);
            taken = true;
        }
    }
    if (!taken) return;                                 // nothing it could give: it stays for later
    isTaken = true;                                     // it is gone when the frame is over; until then it gives nothing more
    if (!sound.empty()) world.playSound(sound);
    scene.destroy(self);
}

MOLTEN_COMPONENT(Supply, MOLTEN_FIELD(collector).picks("entityOrTag"), MOLTEN_FIELD(rounds).range(0, 9999), MOLTEN_FIELD(health).range(0, 1000), MOLTEN_FIELD(mostHealth).range(1, 10000),
                 MOLTEN_FIELD(sound).picks("sound"))
