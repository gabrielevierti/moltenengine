// A game on several machines: here, several worlds in one program, joined by a network that
// is only memory. Each reads the same scene from the same file, as machines would.
#include "Test.hpp"

#include "Animation/Animator.hpp"
#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Character.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Network.hpp"
#include "Scene/Components.hpp"
#include "Scene/SceneFile.hpp"
#include "Script/Runtime.hpp"

#include <filesystem>

using namespace molten;
using namespace molten::kit;

namespace {

namespace fs = std::filesystem;

/// The game the tests play together, written as files: a floor, three places to appear at, a
/// gem that only the server lets anyone collect, a lift only the server moves, and the prefab
/// everyone gets one of.
std::string writeTheGame() {
    kit::registerKit();
    script::registerStandardNodes();
    fs::path root = fs::temp_directory_path() / "molten-network-game";
    fs::remove_all(root);
    fs::create_directories(root / "Scenes");
    fs::create_directories(root / "Prefabs");
    fs::create_directories(root / "Animations");
    text::writeFile((root / "Animations/Player.animator").string(), "molten-animator 1\nparameter float speed 0\nparameter bool grounded 1\nstate Idle\n  motion Idle\nstart Idle\n");
    RenderSettings none;
    {
        Scene prefab;
        Entity player = prefab.create("Player");
        prefab.add<Transform>(player);
        prefab.add<CharacterController>(player);
        prefab.add<CharacterMover>(player);
        Collider seen = Collider::capsule(0.35f, 0.55f);
        seen.offset = Vec3{0, 0.9f, 0};
        seen.isTrigger = true;
        prefab.add(player, seen);
        Health& health = prefab.add<Health>(player);
        health.amount = 50;
        health.scoreCounter.clear();
        Networked& networked = prefab.add<Networked>(player);
        networked.ownerOnly = "CharacterController, CharacterMover, Health, Eyes";
        networked.forwards = "shot=damage";
        Entity figure = prefab.create("Figure");
        prefab.add<Transform>(figure).parent = player;
        prefab.add<Animator>(figure).controller = "Animations/Player.animator";
        Entity eyes = prefab.create("Eyes");                    // what only the one who plays it has: a camera, say
        prefab.add<Transform>(eyes).parent = player;
        prefab.updateTransforms();
        text::writeFile((root / "Prefabs/Player.prefab").string(), writeScene(prefab, {}, none));
        Scene crate;
        Entity box = crate.create("Crate");
        crate.add<Transform>(box);
        crate.add(box, Collider::box(Vec3{0.4f, 0.4f, 0.4f}));
        crate.updateTransforms();
        text::writeFile((root / "Prefabs/Crate.prefab").string(), writeScene(crate, {}, none));
    }
    Scene scene;
    Entity ground = scene.create("Ground");
    scene.add<Transform>(ground).position = Vec3{0, -0.5f, 0};
    scene.add(ground, Collider::box(Vec3{60, 0.5f, 60}));
    for (int i = 0; i < 3; i++) {
        Entity point = scene.create("Spawn " + std::to_string(i + 1));
        scene.add<Transform>(point).position = Vec3{float(i) * 4.0f, 0.1f, 0};
    }
    for (int i = 0; i < 2; i++) {
        Entity gem = scene.create("Gem " + std::to_string(i + 1));
        scene.add<Transform>(gem).position = Vec3{i ? 30.0f : 4.0f, 1.0f, i ? 30.0f : -2.5f};      // the first is ahead of the second player
        Collider touch = Collider::sphere(0.8f);
        touch.isTrigger = true;
        scene.add(gem, touch);
        Collectible& collectible = scene.add<Collectible>(gem);
        collectible.counter = "gems";
        Networked& networked = scene.add<Networked>(gem);
        networked.syncsPlace = false;
        networked.ownerOnly = "Collectible";
    }
    Entity lift = scene.create("Lift");
    scene.add<Transform>(lift).position = Vec3{-10, 0, -10};
    Shuttle& shuttle = scene.add<Shuttle>(lift);
    shuttle.offset = Vec3{0, 6, 0};
    shuttle.seconds = 3;
    scene.add<Networked>(lift).ownerOnly = "Shuttle";
    Entity session = scene.create("Session");
    scene.add<Transform>(session);
    NetworkSession& play = scene.add<NetworkSession>(session);
    play.mode = "Off";
    play.sharedValues = "gems";
    scene.updateTransforms();
    saveSceneFile((root / "Scenes/Main.scene").string(), scene, {}, none);
    return root.string();
}

/// One machine of the game.
struct Machine {
    World world;
    int heard = 0;
    float heardNumber = 0;

    Machine(const std::string& game, const std::string& name) {
        world.assetRoot = game;
        world.loadSceneNow("Scenes/Main.scene");
        if (NetworkSession* session = sessionOf(world)) session->playerName = name;
        world.listen("wave", [this](const Message& message) { heard++, heardNumber = message.number; });
    }
    NetworkSession& session() { return *sessionOf(world); }
    Vec3 at(const char* name) {
        const Transform* place = world.scene.get<Transform>(world.find(name));
        return place ? place->worldPosition() : Vec3{0, -999, 0};
    }
    bool has(const char* name) { return world.scene.isAlive(world.find(name)); }
    template <typename T>
    T* part(const char* name) {
        return world.scene.get<T>(world.find(name));
    }
};

/// The machines, and the network between them.
struct Table {
    MemoryNetwork network;
    std::vector<Machine*> machines;
    NetAddress hostAt;

    void host(Machine& machine) {
        std::unique_ptr<NetTransport> end = network.open(7777);
        hostAt = end->address();
        machine.session().listenOn(machine.world, std::move(end), true);
        machines.push_back(&machine);
    }
    void join(Machine& machine, uint16_t port) {
        machine.session().joinOn(machine.world, network.open(port), hostAt);
        machines.push_back(&machine);
    }
    void run(double seconds) {
        for (int i = 0; i < int(seconds * 60 + 0.5); i++) {
            network.advance(1.0 / 60.0);
            for (Machine* machine : machines) machine->world.advance(1.0 / 60.0);
        }
    }
};

}  // namespace

TEST(players_of_one_game_see_each_other_move) {
    std::string game = writeTheGame();
    Table table;
    Machine host(game, "Host"), anna(game, "Anna"), bruno(game, "Bruno");
    table.host(host);
    table.join(anna, 5001);
    table.run(0.3);
    table.join(bruno, 5002);
    table.run(0.7);

    // Everyone is in, numbered as they came, and there are three in the game on every machine.
    CHECK(host.session().isServer() && host.session().localId() == 0);
    CHECK(anna.session().isClient() && anna.session().localId() == 1);
    CHECK(bruno.session().isClient() && bruno.session().localId() == 2);
    for (Machine* machine : table.machines) CHECK(machine->session().playerCount() == 3);
    // Each has their own player under the prefab's name, and the others' numbered.
    CHECK(host.has("Player") && host.has("Player 2") && host.has("Player 3") && !host.has("Player 1"));
    CHECK(anna.has("Player") && anna.has("Player 1") && anna.has("Player 3") && !anna.has("Player 2"));
    CHECK(bruno.has("Player") && bruno.has("Player 1") && bruno.has("Player 2") && !bruno.has("Player 3"));
    // Each appeared at the next place in turn, and is there on every machine.
    CHECK_NEAR(anna.at("Player").x, 4.0f, 0.05f);
    CHECK_NEAR(host.at("Player 2").x, 4.0f, 0.05f);
    CHECK_NEAR(bruno.at("Player 2").x, 4.0f, 0.05f);
    CHECK_NEAR(bruno.at("Player").x, 8.0f, 0.05f);
    CHECK_NEAR(anna.at("Player 1").x, 0.0f, 0.05f);
    // Only one's own has what moves it and what only its player needs; a copy has neither.
    CHECK(anna.part<CharacterMover>("Player") && anna.part<Health>("Player") && anna.has("Eyes"));
    CHECK(!host.part<CharacterMover>("Player 2") && !host.part<CharacterController>("Player 2") && !host.part<Health>("Player 2"));
    CHECK(!bruno.part<CharacterMover>("Player 2"));
    int eyes = 0;
    host.world.scene.each<Name>([&](Entity, Name& name) { eyes += name.value == "Eyes" ? 1 : 0; });
    CHECK(eyes == 1);                                           // the host's own, and no copy's
    // What is in the scene is the server's: only it runs the lift, and the gem's rule.
    CHECK(host.part<Shuttle>("Lift") && !anna.part<Shuttle>("Lift") && !bruno.part<Collectible>("Gem 1") && host.part<Collectible>("Gem 1"));

    // Anna walks forward (-z) for a second and a half. The others see her go, a moment behind, and her animator is told she moves.
    anna.world.input.setVirtualAxis("moveZ", -1);
    table.run(0.75);
    Vec3 there = anna.at("Player");
    CHECK(there.z < -1.0f);
    CHECK(simd::length(host.at("Player 2") - there) < 1.2f);            // on her way: behind by the smoothing, not left where she was
    CHECK(host.at("Player 2").z < -0.3f);
    Animator* shown = nullptr;
    bruno.world.scene.each<Animator, Transform>([&](Entity entity, Animator& animator, Transform&) {
        if (Physics::isUnder(bruno.world.scene, entity, bruno.world.find("Player 2"))) shown = &animator;
    });
    CHECK(shown && shown->parameter("speed") > 1.0f);
    table.run(0.75);
    // She passed over the first gem: the server, which saw her copy touch it, took it and counted it for everyone.
    CHECK(host.world.value("gems") == 1 && anna.world.value("gems") == 1 && bruno.world.value("gems") == 1);
    CHECK(!host.has("Gem 1") && !anna.has("Gem 1") && !bruno.has("Gem 1"));
    CHECK(host.has("Gem 2") && anna.has("Gem 2") && bruno.has("Gem 2"));
    anna.world.input.setVirtualAxis("moveZ", 0);
    table.run(0.6);
    there = anna.at("Player");
    CHECK(simd::length(host.at("Player 2") - there) < 0.05f);           // stopped: where she is, on every machine
    CHECK(simd::length(bruno.at("Player 2") - there) < 0.05f);
    CHECK(shown->parameter("speed") < 0.2f);
    // The lift is where the server has it, near enough for something that moves.
    CHECK(simd::length(anna.at("Lift") - host.at("Lift")) < 0.5f);
    CHECK(anna.at("Lift").y > 0.2f || host.at("Lift").y < 0.3f);

    // One who comes late finds the game as it is: the gem gone, the count, everyone where they are.
    Machine carla(game, "Carla");
    table.join(carla, 5003);
    table.run(0.8);
    CHECK(carla.session().localId() == 3 && carla.session().playerCount() == 4);
    CHECK(!carla.has("Gem 1") && carla.has("Gem 2") && carla.world.value("gems") == 1);
    CHECK(simd::length(carla.at("Player 2") - there) < 0.05f);
    CHECK(host.has("Player 4") && anna.has("Player 4"));
    CHECK_NEAR(carla.at("Player").x, 0.0f, 0.05f);                      // the places to appear at come round again
}

TEST(a_game_for_several_is_played_alone_with_no_network) {
    std::string game = writeTheGame();
    Machine alone(game, "Solo");
    alone.session().playAlone(alone.world);
    for (int i = 0; i < 30; i++) alone.world.advance(1.0 / 60.0);
    // Its own server and its only player: the player is there, with what moves it, and the scene's rules run here.
    CHECK(alone.session().isOnline() && alone.session().isServer() && alone.session().localId() == 0 && alone.session().playerCount() == 1);
    CHECK(alone.has("Player") && alone.part<CharacterMover>("Player") && alone.part<Collectible>("Gem 1") && alone.part<Shuttle>("Lift"));
    CHECK(!alone.world.input.isPointerFree());                          // no menu holds the pointer out
    // It walks to the gem ahead of where it appeared, and the gem is counted.
    alone.world.scene.get<Transform>(alone.world.find("Player"))->position = Vec3{4, 0.1f, 0};
    alone.world.input.setVirtualAxis("moveZ", -1);
    for (int i = 0; i < 100; i++) alone.world.advance(1.0 / 60.0);
    CHECK(alone.world.value("gems") == 1 && !alone.has("Gem 1"));
    // Leaving puts the scene back as it was.
    alone.session().leave(alone.world);
    for (int i = 0; i < 5; i++) alone.world.advance(1.0 / 60.0);
    CHECK(!alone.session().isOnline() && !alone.has("Player") && alone.has("Gem 1"));
}

TEST(what_is_told_to_a_networked_thing_reaches_the_machines_it_is_for) {
    std::string game = writeTheGame();
    Table table;
    Machine host(game, "Host"), anna(game, "Anna"), bruno(game, "Bruno");
    table.host(host);
    table.join(anna, 5001);
    table.join(bruno, 5002);
    table.run(1.0);
    CHECK(bruno.session().isOnline());
    if (!bruno.session().isOnline()) return;

    // Anna waves: everyone else hears it, on their copy of her; she does not hear herself.
    anna.session().tell(anna.world, anna.world.find("Player"), "wave", 3);
    table.run(0.2);
    CHECK(host.heard == 1 && bruno.heard == 1 && anna.heard == 0);
    CHECK(host.heardNumber == 3);
    // To everyone, herself included; to the server only; to the owner only (Bruno tells Anna's copy: Anna hears).
    anna.session().tell(anna.world, anna.world.find("Player"), "wave", 4, "", NetTo::Everyone);
    table.run(0.2);
    CHECK(host.heard == 2 && bruno.heard == 2 && anna.heard == 1);
    bruno.session().tell(bruno.world, bruno.world.find("Player 2"), "wave", 5, "", NetTo::Server);
    table.run(0.2);
    CHECK(host.heard == 3 && bruno.heard == 2 && anna.heard == 1);
    bruno.session().tell(bruno.world, bruno.world.find("Player 2"), "wave", 6, "", NetTo::Owner);
    table.run(0.2);
    CHECK(host.heard == 3 && bruno.heard == 2 && anna.heard == 2 && anna.heardNumber == 6);
    // The server tells too: everyone else is everyone who joined.
    host.session().tell(host.world, host.world.find("Player"), "wave", 7);
    table.run(0.2);
    CHECK(host.heard == 3 && bruno.heard == 3 && anna.heard == 3);

    // A shot lands on Bruno's copy of Anna: the copy has no health to lose. It is Anna's machine that is told, as "damage", and her health that goes.
    CHECK(anna.part<Health>("Player")->amount == 50);
    bruno.world.send("shot", bruno.world.find("Player 2"), 7);
    table.run(0.2);
    CHECK_NEAR(anna.part<Health>("Player")->amount, 43.0f, 0.001f);
    host.world.send("shot", host.world.find("Player 2"), 3);
    table.run(0.2);
    CHECK_NEAR(anna.part<Health>("Player")->amount, 40.0f, 0.001f);

    // What the server spawns is on every machine, and gone from every machine when the server destroys it.
    Entity crate = host.session().spawn(host.world, "Prefabs/Crate.prefab", Vec3{2, 0.4f, 5});
    CHECK(host.world.scene.isAlive(crate));
    CHECK(!anna.world.scene.isAlive(anna.session().spawn(anna.world, "Prefabs/Crate.prefab", Vec3{0, 0, 0})));       // not one who joined
    table.run(0.2);
    CHECK(anna.has("Crate") && bruno.has("Crate"));
    CHECK_NEAR(bruno.at("Crate").z, 5.0f, 0.01f);
    host.world.scene.get<Transform>(crate)->position = Vec3{2, 0.4f, 9};       // moved there, it moves here
    table.run(0.4);
    CHECK_NEAR(anna.at("Crate").z, 9.0f, 0.02f);
    host.world.scene.destroy(crate);
    table.run(0.2);
    CHECK(!host.has("Crate") && !anna.has("Crate") && !bruno.has("Crate"));

    // Anna leaves: her player goes from the others' games, and she is alone in the scene as it was.
    anna.session().leave(anna.world);
    table.run(0.3);
    CHECK(!host.has("Player 2") && !bruno.has("Player 2"));
    CHECK(host.session().playerCount() == 2 && bruno.session().playerCount() == 2);
    CHECK(!anna.session().isOnline() && !anna.has("Player") && anna.has("Gem 1"));
    // The host stops: for Bruno the game is over, and he is back where he started too.
    host.session().leave(host.world);
    table.run(0.3);
    CHECK(!bruno.session().isOnline() && !bruno.has("Player") && !bruno.has("Player 1"));
}
