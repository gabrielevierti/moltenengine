// Weapon.cpp
// The gun held before the first-person view: it shoots where the view looks, from a magazine
// that empties and is loaded again (Shooting.hpp says all it does).
// This game's own script: change it as the game needs.
#include "Shooting.hpp"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- Weapon

bool Weapon::reload(World& world, Entity) {
    if (loading > 0 || inMagazine >= magazineSize || reserve == 0) return false;
    loading = std::max(reloadTime, 0.01f);
    if (!reloadSound.empty()) world.playSound(reloadSound);
    return true;
}

bool Weapon::fire(World& world, Entity self) {
    if (inMagazine < 0) inMagazine = magazineSize;
    // (A thousandth of a second is "now": what is left of the wait after whole frames is never quite nothing.)
    if (loading > 0 || untilNext > 0.001f) return false;
    if (inMagazine <= 0) {
        // Nothing in it: the click of that, and it is loaded if there is anything to load it with.
        untilNext = 0.3f;
        if (!emptySound.empty()) world.playSound(emptySound);
        reload(world, self);
        return false;
    }
    Scene& scene = world.scene;
    inMagazine--;
    shots++;
    untilNext = 1.0f / std::max(fireRate, 0.1f);
    sinceShot = 0;
    recoil = 1;

    // It shoots where the view looks: from the camera it is under, or from itself.
    Entity eye = self;
    for (Entity above = self; scene.isAlive(above); above = parentOf(scene, above)) {
        if (scene.get<Camera>(above)) {
            eye = above;
            break;
        }
    }
    const Transform* view = scene.get<Transform>(eye);
    if (!view) return true;
    Vec3 origin = view->worldPosition();
    Vec3 forward = safeNormalize(transformDirection(view->world, Vec3{0, 0, -1}), Vec3{0, 0, -1});
    Vec3 right = safeNormalize(transformDirection(view->world, Vec3{1, 0, 0}), Vec3{1, 0, 0});
    Vec3 up = simd::cross(right, forward);
    // Not quite where it looks: somewhere in a circle that is wider at a run and after firing.
    float wide = std::tan(radians(strayNow())), turn = next(chance) * 2 * kPi, out = std::sqrt(next(chance));
    Vec3 direction = simd::normalize(forward + (right * std::cos(turn) + up * std::sin(turn)) * (wide * out));
    Entity holder = topOf(scene, self);
    // What follows may move the components about (something dies, something is spawned): what is needed of this one is taken first.
    std::string effect = hitEffect;
    float dealt = damage, shove = push;
    bool counted = false;

    if (auto hit = world.physics.raycast(scene, Ray{origin, direction}, range, 0xFFFFFFFFu, holder)) {
        Entity struck = hit->entity;
        Vec3 point = hit->point, normal = hit->normal;
        // A body that can move is shoved.
        if (RigidBody* body = scene.get<RigidBody>(struck); body && body->type == BodyType::Dynamic) {
            body->velocity += direction * (shove * 4.0f / std::max(body->mass, 1.0f));
        }
        // What is struck is told, and whatever it is part of; the first of them with kit::Health loses some.
        std::vector<Entity> told;
        for (Entity at = struck; scene.isAlive(at); at = parentOf(scene, at)) told.push_back(at);
        for (Entity at : told) {
            if (!scene.isAlive(at)) break;
            if (scene.get<ShootingTarget>(at)) counted = true;
            world.send("shot", at, dealt, "", holder);
        }
        for (Entity at : told) {
            if (!scene.isAlive(at)) break;
            if (kit::Health* health = scene.get<kit::Health>(at)) {
                health->damage(world, at, dealt);
                counted = true;
                break;
            }
        }
        if (!effect.empty()) world.spawn(effect, point + normal * 0.03f);
    }

    Weapon* me = world.scene.get<Weapon>(self);
    if (!me) return true;
    if (counted) {
        me->hits++;
        me->sinceHit = 0;
    }
    me->bloom = std::min(me->bloom + 0.22f, 1.8f);
    if (kit::FirstPersonLook* look = world.scene.get<kit::FirstPersonLook>(eye)) {
        look->kick(me->kick * (0.75f + 0.5f * next(me->chance)), me->kick * 0.4f * (next(me->chance) * 2 - 1));
    }
    if (!me->shotSound.empty()) world.playSound(me->shotSound);
    return true;
}

void Weapon::update(World& world, Entity self, float dt) {
    if (inMagazine < 0) inMagazine = magazineSize;
    untilNext = std::max(untilNext - dt, 0.0f);
    sinceShot += dt;
    sinceHit += dt;
    bloom = std::max(bloom - 3.5f * dt, 0.0f);
    if (loading > 0) {
        loading -= dt;
        if (loading <= 0) {
            // Loaded: what the magazine lacks comes out of the reserve.
            loading = 0;
            int wanted = magazineSize - inMagazine;
            int moved = reserve < 0 ? wanted : std::min(wanted, reserve);
            inMagazine += moved;
            if (reserve > 0) reserve -= moved;
        }
    }
    // How fast its holder goes: a shot strays more at a run.
    Entity holder = topOf(world.scene, self);
    float speed = 0;
    if (const kit::CharacterMover* mover = world.scene.get<kit::CharacterMover>(holder)) speed = mover->speedNow / std::max(mover->runSpeed, 0.1f);
    moving += (saturate(speed) - moving) * std::min(dt * 8.0f, 1.0f);

    const Input& input = world.input;
    if (!needsPointer || input.wantsPointerCaptured()) {
        bool wantsShot = isAutomatic ? input.isDown("fire") : input.wasPressed("fire");
        bool wantsLoad = input.wasPressed("reload") || input.wasPressed(Key::R);
        // Either may move this component: nothing of it is used after them.
        if (wantsLoad) reload(world, self);
        else if (wantsShot) fire(world, self);
    }
    Weapon* me = world.scene.get<Weapon>(self);
    if (!me) return;
    if (!me->counter.empty()) {
        world.setValue(me->counter, float(me->inMagazine));
        world.setValue(me->counter + "Reserve", float(std::max(me->reserve, 0)));
        world.setValue(me->counter + "Loading", me->loading > 0 ? 1.0f - me->loading / std::max(me->reloadTime, 0.01f) : 0.0f);
    }
    // The flash at the end of the barrel: a light, and whatever is drawn under it, for a moment.
    Entity end = findUnder(world.scene, self, me->muzzle);
    if (world.scene.isAlive(end)) {
        bool flashes = me->sinceShot < 0.045f;
        if (Light* light = world.scene.get<Light>(end)) light->isEnabled = flashes;
        Entity shown = end;
        world.scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform& place) {
            if (entity != shown && !Physics::isUnder(world.scene, entity, shown)) return;
            renderer.isVisible = flashes;
            if (flashes && entity != shown) place.rotation = Quat(next(me->chance) * 2 * kPi, Vec3{0, 0, 1});
        });
    }
}

void Weapon::lateUpdate(World& world, Entity self, float dt) {
    Transform* held = world.scene.get<Transform>(self);
    if (!held) return;
    if (!hasRest) {
        restPosition = held->position;
        restRotation = held->rotation;
        hasRest = true;
    }
    // It rocks as its holder walks.
    bob += dt * (3.0f + 9.0f * moving);
    Vec3 offset{std::sin(bob) * 0.007f * moving, -std::abs(std::cos(bob)) * 0.009f * moving, 0};
    // It jumps back at a shot and comes forward again.
    recoil = std::max(recoil - dt * 8.0f, 0.0f);
    float jump = recoil * recoil;
    offset.z += 0.05f * jump;
    float tip = 2.5f * jump;
    // Loading, it is lowered and turned, and brought back up.
    float wanted = loading > 0 ? std::sin(kPi * saturate(1.0f - loading / std::max(reloadTime, 0.01f))) : 0.0f;
    lowered += (wanted - lowered) * std::min(dt * 14.0f, 1.0f);
    offset.y -= 0.17f * lowered;
    // Against a wall it is drawn back and raised, instead of going through.
    Entity holder = topOf(world.scene, self);
    float room = 1.0f;
    if (const Transform* above = world.scene.get<Transform>(held->parent)) {
        Vec3 from = above->worldPosition(), ahead = safeNormalize(transformDirection(above->world, Vec3{0, 0, -1}), Vec3{0, 0, -1});
        if (auto wall = world.physics.raycast(world.scene, Ray{from, ahead}, 1.0f, 0xFFFFFFFFu, holder)) room = wall->distance;
    }
    float back = saturate((0.95f - room) / 0.6f);
    drawnBack += (back - drawnBack) * std::min(dt * 12.0f, 1.0f);
    offset.z += 0.22f * drawnBack;
    offset.y -= 0.05f * drawnBack;
    held->position = restPosition + offset;
    held->rotation = restRotation * Quat(radians(tip - 24.0f * lowered + 38.0f * drawnBack), Vec3{1, 0, 0}) * Quat(radians(28.0f * lowered), Vec3{0, 0, 1});
}

void Weapon::draw(World& world, Entity, Canvas& canvas) {
    if (!showsSights) return;
    if (needsPointer && !world.input.wantsPointerCaptured()) return;        // the pointer is out: it is the pointer that is looked at
    Vec2 middle = canvas.size() * 0.5f;
    // Four marks about the middle, wider apart the more a shot can stray; a dot where it would go.
    float gap = 5.0f + strayNow() * 7.0f + recoil * 4.0f, arm = 8.0f;
    Vec4 white{1, 1, 1, loading > 0 ? 0.35f : 0.9f}, shade{0, 0, 0, 0.35f};
    for (int side = 0; side < 4; side++) {
        Vec2 way = side == 0 ? Vec2{1, 0} : side == 1 ? Vec2{-1, 0} : side == 2 ? Vec2{0, 1} : Vec2{0, -1};
        canvas.line(middle + way * (gap - 1), middle + way * (gap + arm + 1), 4.0f, shade);
        canvas.line(middle + way * gap, middle + way * (gap + arm), 2.0f, white);
    }
    canvas.circle(middle, 1.5f, white);
    // A shot that told: a cross over the sights, for a moment.
    if (sinceHit < 0.18f) {
        float fade = 1.0f - sinceHit / 0.18f, from = 6.0f, to = 13.0f;
        Vec4 mark{1.0f, 0.55f, 0.2f, fade};
        for (Vec2 way : {Vec2{1, 1}, Vec2{-1, 1}, Vec2{1, -1}, Vec2{-1, -1}}) canvas.line(middle + way * from, middle + way * to, 2.5f, mark);
    }
}

MOLTEN_COMPONENT(Weapon, MOLTEN_FIELD(damage).range(0, 100).help("What a hit takes from the Health of what is hit"), MOLTEN_FIELD(range).range(1, 1000),
                 MOLTEN_FIELD(fireRate).range(0.2f, 30).help("Shots a second"), MOLTEN_FIELD(isAutomatic).help("Goes on firing while fire is held"),
                 MOLTEN_FIELD(magazineSize).range(1, 500), MOLTEN_FIELD(reserve).range(-1, 9999).help("Rounds besides those in it; -1: no end of them"),
                 MOLTEN_FIELD(reloadTime).range(0.1f, 10), MOLTEN_FIELD(spread).range(0, 10).help("Degrees a shot can stray, standing"),
                 MOLTEN_FIELD(movingSpread).range(0, 15).help("And at a run"), MOLTEN_FIELD(kick).range(0, 10).help("Degrees the view jumps at each shot"),
                 MOLTEN_FIELD(push).range(0, 50).help("How hard a hit shoves a body"), MOLTEN_FIELD(muzzle).picks("entity").help("An entity under it at the end of the barrel: its light flashes"),
                 MOLTEN_FIELD(hitEffect).picks("prefab").help("Left where a shot lands"), MOLTEN_FIELD(shotSound).picks("sound"), MOLTEN_FIELD(emptySound).picks("sound"),
                 MOLTEN_FIELD(reloadSound).picks("sound"), MOLTEN_FIELD(counter).picks("value").help("Shared values: this, this + Reserve, this + Loading"),
                 MOLTEN_FIELD(showsSights), MOLTEN_FIELD(needsPointer).help("Listens only while the game holds the pointer"))
