// A gun and what it is fired at: a weapon with a magazine, plates that fall, machines that
// hunt, things to pick up, the view of someone seen from their own eyes.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Character.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Navigation.hpp"
#include "Kit/Shooting.hpp"
#include "Scene/Components.hpp"

using namespace molten;
using namespace molten::kit;

namespace {

Entity solid(World& world, const char* name, Vec3 at, Vec3 half) {
    Entity entity = world.scene.create(name);
    world.scene.add<Transform>(entity).position = at;
    world.scene.add(entity, Collider::box(half));
    return entity;
}

/// Someone at the origin looking along -z, with a rifle under their camera.
struct Armed {
    Entity player, camera, rifle;

    explicit Armed(World& world) {
        Scene& scene = world.scene;
        player = scene.create("Player");
        scene.add<Transform>(player);
        camera = scene.create("Camera");
        Transform& eye = scene.add<Transform>(camera);
        eye.position = Vec3{0, 1.6f, 0};
        eye.parent = player;
        scene.add<Camera>(camera);
        rifle = scene.create("Rifle");
        Transform& held = scene.add<Transform>(rifle);
        held.position = Vec3{0.15f, -0.15f, -0.3f};
        held.parent = camera;
        Weapon& weapon = scene.add<Weapon>(rifle);
        weapon.spread = 0, weapon.movingSpread = 0;
        scene.updateTransforms();
        world.input.setPointerCaptured(true);           // the game holds the pointer, as while it is played
    }
    Weapon& weapon(World& world) { return *world.scene.get<Weapon>(rifle); }
};

void frames(World& world, int count) {
    for (int i = 0; i < count; i++) world.advance(1.0 / 60.0);
}

}  // namespace

TEST(a_weapon_fires_from_its_magazine_and_is_loaded_again) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    Armed armed(world);
    Entity wall = solid(world, "Wall", Vec3{0, 1.6f, -20}, Vec3{3, 3, 0.2f});
    world.scene.add<Health>(wall).amount = 1000;
    frames(world, 2);
    CHECK(world.value("ammo") == 30 && world.value("ammoReserve") == 90 && world.value("ammoLoading") == 0);

    // Held for a second at ten shots a second: ten shots, each of which lands and takes what it should.
    world.input.handleMouseButton(MouseButton::Left, true);
    frames(world, 60);
    CHECK(armed.weapon(world).shots >= 10 && armed.weapon(world).shots <= 11);
    CHECK(armed.weapon(world).hits == armed.weapon(world).shots);
    CHECK_NEAR(world.scene.get<Health>(wall)->amount, 1000 - float(armed.weapon(world).shots), 0.01f);
    CHECK(world.value("ammo") == 30 - float(armed.weapon(world).shots));
    // And on until there is nothing in it: thirty in all, then it loads by itself, and for that long does not fire.
    frames(world, 60 * 2 + 20);
    CHECK(armed.weapon(world).shots == 30);
    CHECK(armed.weapon(world).isLoading());
    CHECK(world.value("ammoLoading") > 0 && world.value("ammoLoading") < 1);
    world.input.handleMouseButton(MouseButton::Left, false);
    frames(world, 90);
    CHECK(!armed.weapon(world).isLoading());
    CHECK(world.value("ammo") == 30 && world.value("ammoReserve") == 60 && armed.weapon(world).shots == 30);

    // One shot each press when it is not automatic; and R tops the magazine up with what it lacks.
    armed.weapon(world).isAutomatic = false;
    world.input.handleMouseButton(MouseButton::Left, true);
    frames(world, 30);
    world.input.handleMouseButton(MouseButton::Left, false);
    CHECK(armed.weapon(world).shots == 31);
    world.input.handleKey(Key::R, true);
    frames(world, 1);
    world.input.handleKey(Key::R, false);
    CHECK(armed.weapon(world).isLoading());
    frames(world, 100);
    CHECK(world.value("ammo") == 30 && world.value("ammoReserve") == 59);
    // Full, there is nothing to load.
    CHECK(!armed.weapon(world).reload(world, armed.rifle));

    // With nothing besides, an empty magazine stays empty.
    armed.weapon(world).reserve = 0, armed.weapon(world).inMagazine = 1;
    CHECK(armed.weapon(world).fire(world, armed.rifle));
    frames(world, 30);
    CHECK(!armed.weapon(world).fire(world, armed.rifle));
    CHECK(!armed.weapon(world).isLoading() && armed.weapon(world).inMagazine == 0);
    // Rounds found fill the reserve, and then it can be loaded.
    armed.weapon(world).addRounds(12);
    frames(world, 30);
    CHECK(armed.weapon(world).reload(world, armed.rifle));
    frames(world, 100);
    CHECK(world.value("ammo") == 12 && world.value("ammoReserve") == 0);
}

TEST(a_weapon_waits_for_the_pointer_and_misses_what_is_not_in_its_way) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    Armed armed(world);
    Entity crate = solid(world, "Crate", Vec3{0, 1.6f, -8}, Vec3{0.4f, 0.4f, 0.4f});
    RigidBody& body = world.scene.add<RigidBody>(crate);
    body.mass = 5;
    body.gravityScale = 0;
    Entity aside = solid(world, "Aside", Vec3{6, 1.6f, -8}, Vec3{0.4f, 0.4f, 0.4f});
    world.scene.add<Health>(aside).amount = 5;
    // The pointer given back (Esc): a click is not a shot.
    world.input.setPointerCaptured(false);
    world.input.handleMouseButton(MouseButton::Left, true);
    frames(world, 20);
    CHECK(armed.weapon(world).shots == 0);
    world.input.handleMouseButton(MouseButton::Left, false);
    world.input.setPointerCaptured(true);
    frames(world, 2);
    // A shot shoves a body that can move, and does nothing to what is beside its way.
    CHECK(armed.weapon(world).fire(world, armed.rifle));
    frames(world, 20);
    CHECK(world.scene.get<Transform>(crate)->position.z < -8.05f);
    CHECK(world.scene.get<Health>(aside)->amount == 5);
    CHECK(armed.weapon(world).hits == 0);                       // a crate is hit, but it is nothing that counts
    // Too far is a miss: nothing past its range is touched.
    armed.weapon(world).range = 5;
    world.scene.get<RigidBody>(crate)->velocity = Vec3{0, 0, 0};
    frames(world, 10);
    float before = world.scene.get<Transform>(crate)->position.z;
    armed.weapon(world).fire(world, armed.rifle);
    frames(world, 10);
    CHECK_NEAR(world.scene.get<Transform>(crate)->position.z, before, 0.02f);
    // It kicks the view up, and the view comes back.
    FirstPersonLook& look = world.scene.add<FirstPersonLook>(armed.camera);
    look.takesPointer = true;
    armed.weapon(world).range = 100;
    frames(world, 10);
    armed.weapon(world).fire(world, armed.rifle);
    CHECK(world.scene.get<FirstPersonLook>(armed.camera)->kickPitch > 0.4f);
    frames(world, 60);
    CHECK(world.scene.get<FirstPersonLook>(armed.camera)->kickPitch < 0.01f);
}

TEST(a_plate_falls_when_it_is_shot_and_stands_again) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    Armed armed(world);
    // The target: its foot on the ground ten meters ahead, its plate (what a shot meets) at the eye's height.
    Entity target = world.scene.create("Target");
    world.scene.add<Transform>(target).position = Vec3{0, 0, -10};
    ShootingTarget& falls = world.scene.add<ShootingTarget>(target);
    falls.counter = "down";
    falls.standsAfter = 1.0f;
    Entity plate = solid(world, "Plate", Vec3{0, 1.6f, 0}, Vec3{0.36f, 0.36f, 0.03f});
    world.scene.get<Transform>(plate)->parent = target;
    world.scene.updateTransforms();
    frames(world, 2);
    CHECK(world.value("down") == 0);
    CHECK(armed.weapon(world).fire(world, armed.rifle));
    CHECK(world.value("down") == 1 && world.scene.get<ShootingTarget>(target)->isDown);
    CHECK(armed.weapon(world).hits == 1);
    frames(world, 30);
    // Down: the plate has gone back and down with its foot, and a second shot where it stood meets nothing.
    Vec3 fallen = world.scene.get<Transform>(plate)->worldPosition();
    CHECK(fallen.y < 0.4f && fallen.z > -9.5f);
    CHECK(armed.weapon(world).fire(world, armed.rifle));
    CHECK(world.value("down") == 1);
    // After a second it is wound back up, and counts again.
    frames(world, 150);
    CHECK(!world.scene.get<ShootingTarget>(target)->isDown);
    CHECK_NEAR(world.scene.get<Transform>(plate)->worldPosition().y, 1.6f, 0.02f);
    CHECK(armed.weapon(world).fire(world, armed.rifle));
    CHECK(world.value("down") == 2);
}

TEST(a_hunter_finds_its_prey_goes_over_what_is_in_its_way_and_hurts_it) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    solid(world, "Ground", Vec3{0, -0.5f, 0}, Vec3{60, 0.5f, 60});
    solid(world, "Wall", Vec3{0, 1.5f, -6}, Vec3{4, 1.5f, 0.3f});             // three meters high, between them
    Entity player = world.scene.create("Player");
    world.scene.add<Transform>(player).position = Vec3{0, 0, 0};
    Health& health = world.scene.add<Health>(player);
    health.amount = 100;
    health.counter = "health";
    Entity hunter = world.scene.create("Hunter");
    world.scene.add<Transform>(hunter).position = Vec3{0, 1.5f, -14};
    world.scene.add(hunter, Collider::sphere(0.42f));
    world.scene.add<Hunter>(hunter);
    world.scene.updateTransforms();
    // Behind the wall it sees nothing, and stays where it was put.
    frames(world, 120);
    CHECK(!world.scene.get<Hunter>(hunter)->isHunting);
    CHECK(simd::length(world.scene.get<Transform>(hunter)->position - Vec3{0, 1.5f, -14}) < 0.5f);
    CHECK(world.value("health") == 100);
    // The player steps out to where it can see them: it comes, over the wall, and hurts.
    world.scene.get<Transform>(player)->position = Vec3{11, 0, 0};
    world.scene.updateTransforms();
    frames(world, 30);
    CHECK(world.scene.get<Hunter>(hunter)->isHunting);
    world.scene.get<Transform>(player)->position = Vec3{0, 0, 0};          // and back behind it: seen once, it is followed
    world.scene.updateTransforms();
    float highest = 0;
    for (int i = 0; i < 60 * 10; i++) {
        world.advance(1.0 / 60.0);
        highest = std::max(highest, world.scene.get<Transform>(hunter)->position.y);
    }
    CHECK(highest > 3.2f);                                                  // it went over the wall, not through it
    CHECK(simd::length(world.scene.get<Transform>(hunter)->position - Vec3{0, 1.1f, 0}) < 2.2f);
    CHECK(world.value("health") < 100 && world.value("health") > 0);
    // A walker's place is not a hole in the ground that is planned on: the area under it is still walked.
    Entity area = world.scene.create("Area");
    world.scene.add<Transform>(area);
    NavigationArea& ground = world.scene.add<NavigationArea>(area);
    ground.size = Vec2{20, 20};
    ground.bake(world, area);
    Vec3 under = world.scene.get<Transform>(hunter)->position, found{0, 9, 0};
    const NavigationArea* baked = world.scene.get<NavigationArea>(area);
    CHECK(baked->isWalkable(Vec3{under.x, 0, under.z}));
    CHECK(baked->nearestWalkable(Vec3{under.x, 0, under.z}, found) && std::abs(found.y) < 0.05f);      // the ground there is the ground, not the machine's back
}

TEST(supplies_fill_a_weapon_and_mend_whoever_takes_them) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    Armed armed(world);
    Health& health = world.scene.add<Health>(armed.player);
    health.amount = 55;
    health.counter = "health";
    auto supply = [&](int rounds, float mends) {
        Entity box = world.scene.create("Supply");
        world.scene.add<Transform>(box).position = Vec3{0, 50, 0};
        Supply& gives = world.scene.add<Supply>(box);
        gives.rounds = rounds, gives.health = mends;
        return box;
    };
    Entity rounds = supply(30, 0), bandage = supply(0, 60), another = supply(0, 60);
    Entity stranger = world.scene.create("Someone");
    world.scene.get<Supply>(rounds)->onTriggerEnter(world, rounds, stranger);
    CHECK(world.scene.isAlive(rounds));                                     // not for anyone: for the Player
    world.scene.get<Supply>(rounds)->onTriggerEnter(world, rounds, armed.player);
    world.scene.get<Supply>(rounds)->onTriggerEnter(world, rounds, armed.player);       // touched twice in a frame: given once
    CHECK(armed.weapon(world).reserve == 120);
    world.scene.get<Supply>(bandage)->onTriggerEnter(world, bandage, armed.player);
    frames(world, 1);
    CHECK(!world.scene.isAlive(rounds) && !world.scene.isAlive(bandage));
    CHECK(world.scene.get<Health>(armed.player)->amount == 100 && world.value("health") == 100);     // not past all of it
    // Whole already, a bandage is left for later.
    world.scene.get<Supply>(another)->onTriggerEnter(world, another, armed.player);
    frames(world, 1);
    CHECK(world.scene.isAlive(another));
}

TEST(a_first_person_view_turns_the_body_and_tips_the_camera) {
    kit::registerKit();
    World world;
    world.loadInputMap();
    Entity body = world.scene.create("Player");
    world.scene.add<Transform>(body).rotation = Quat(radians(90.0f), Vec3{0, 1, 0});       // looking along -x
    Entity camera = world.scene.create("Camera");
    Transform& eye = world.scene.add<Transform>(camera);
    eye.position = Vec3{0, 1.6f, 0};
    eye.parent = body;
    world.scene.add<Camera>(camera);
    world.scene.add<FirstPersonLook>(camera);
    frames(world, 2);
    // It starts the way the body was turned, and asks for the pointer.
    CHECK_NEAR(world.scene.get<FirstPersonLook>(camera)->yaw, 90.0f, 0.1f);
    CHECK(world.input.wantsPointerCaptured());
    auto looking = [&] { return transformDirection(world.scene.get<Transform>(camera)->world, Vec3{0, 0, -1}); };
    CHECK(looking().x < -0.99f);
    // The mouse to the right turns it right (from -x round to -z... to +x is right of -z; from -x, right is -z... toward +z is left).
    world.input.handleMouseMove(Vec2{0, 0}, Vec2{90.0f / 0.12f, 0});
    frames(world, 1);
    world.scene.updateTransforms();
    CHECK(looking().z < -0.99f);                                           // a quarter turn to the right of -x is -z
    // The mouse up looks up, and no further than nearly straight up.
    world.input.handleMouseMove(Vec2{0, 0}, Vec2{0, -30.0f / 0.12f});
    frames(world, 1);
    world.scene.updateTransforms();
    CHECK_NEAR(looking().y, std::sin(radians(30.0f)), 0.01f);
    world.input.handleMouseMove(Vec2{0, 0}, Vec2{0, -5000});
    frames(world, 1);
    CHECK_NEAR(world.scene.get<FirstPersonLook>(camera)->pitch, 85.0f, 0.01f);
    // The body turned, not tipped: it stays upright for the mover.
    CHECK_NEAR(rotate(world.scene.get<Transform>(body)->rotation, Vec3{0, 1, 0}).y, 1.0f, 0.001f);
    // Esc gives the pointer back, and the mouse then turns nothing.
    world.input.handleKey(Key::Escape, true);
    frames(world, 1);
    world.input.handleKey(Key::Escape, false);
    CHECK(!world.input.wantsPointerCaptured());
    float yaw = world.scene.get<FirstPersonLook>(camera)->yaw;
    world.input.handleMouseMove(Vec2{0, 0}, Vec2{300, 0});
    frames(world, 1);
    CHECK_NEAR(world.scene.get<FirstPersonLook>(camera)->yaw, yaw, 0.001f);
    // While the window holds it, there is no pointer over the screen's buttons.
    world.input.setPointerIsCaptured(true);
    CHECK(world.input.mousePosition().x < -1000);
    world.input.setPointerIsCaptured(false);
}

TEST(a_shuttle_goes_to_and_fro) {
    kit::registerKit();
    World world;
    Entity platform = world.scene.create("Platform");
    world.scene.add<Transform>(platform).position = Vec3{1, 2, 3};
    Shuttle& shuttle = world.scene.add<Shuttle>(platform);
    shuttle.offset = Vec3{6, 0, 0};
    shuttle.seconds = 2;
    shuttle.waits = 0.5f;
    auto at = [&] { return world.scene.get<Transform>(platform)->position; };
    frames(world, 60);
    CHECK_NEAR(at().x, 4.0f, 0.1f);                                         // half way after a second
    frames(world, 75);
    CHECK_NEAR(at().x, 7.0f, 0.01f);                                        // there, and waiting
    frames(world, 150);
    CHECK_NEAR(at().x, 1.0f, 0.05f);                                        // and back
    CHECK_NEAR(at().y, 2.0f, 0.001f);
}
