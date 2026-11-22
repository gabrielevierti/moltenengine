// DamageFlash.cpp
// The edges of the screen go red when the player's health drops.
// This game's own script: change it as the game needs.
#include "Shooting.hpp"

#include <algorithm>
#include <cmath>

void DamageFlash::update(World& world, Entity, float dt) {
    float now = world.value(value);
    if (last >= 0 && now < last - 1e-4f) {
        shown = 1;
        if (!sound.empty()) world.playSound(sound);
    }
    last = now;
    shown = std::max(shown - dt / std::max(seconds, 0.05f), 0.0f);
}

void DamageFlash::draw(World&, Entity, Canvas& canvas) {
    if (shown <= 0) return;
    // Red at the edges, clear in the middle: bands that thin toward it.
    Vec2 size = canvas.size();
    float deep = std::min(size.x, size.y) * 0.16f;
    for (int band = 0; band < 8; band++) {
        float t = float(band) / 8.0f, inset = deep * t, thick = deep / 8.0f + 1.0f;
        Vec4 tint{color.x, color.y, color.z, 0.55f * shown * (1 - t) * (1 - t)};
        canvas.rect(Vec2{inset, inset}, Vec2{size.x - 2 * inset, thick}, tint);
        canvas.rect(Vec2{inset, size.y - inset - thick}, Vec2{size.x - 2 * inset, thick}, tint);
        canvas.rect(Vec2{inset, inset + thick}, Vec2{thick, size.y - 2 * inset - 2 * thick}, tint);
        canvas.rect(Vec2{size.x - inset - thick, inset + thick}, Vec2{thick, size.y - 2 * inset - 2 * thick}, tint);
    }
}

MOLTEN_COMPONENT(DamageFlash, MOLTEN_FIELD(value).picks("value"), MOLTEN_FIELD(color).color(), MOLTEN_FIELD(seconds).range(0.05f, 5), MOLTEN_FIELD(sound).picks("sound"))
