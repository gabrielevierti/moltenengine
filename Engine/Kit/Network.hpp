// Network.hpp
// A game played together, on several machines.
//
//   NetworkSession   one in the scene: who is in the game. One machine is the host (it listens,
//                    and plays too) or a server (it listens, and does not play); the others
//                    join it. Everyone in the game gets a copy of the player prefab, theirs to
//                    move; everyone else sees it move.
//   Networked        on an entity: it is the same thing on every machine. Where it is goes from
//                    the machine that owns it to the others; when it is destroyed there, it is
//                    gone everywhere; what is told to it can be told to it on another machine.
//
// Who owns what: what was in the scene from the start, and what the server spawns, is the
// server's (it runs their components; the other machines only show them). A player's own
// object is that player's: their machine moves it, with their keys.
//
// `Owner Only` on a Networked says which of its components and which entities under it only
// the owner's machine keeps: the mover and the camera of a player (a copy on another machine
// has no keys to read and no eyes to see with); the Collectible of a gem (only the server
// decides who collected it).
//
// Everyone must be in the same scene, with the same things in it: a session is of one scene,
// and ends when another is loaded.
#pragma once

#include "Core/World.hpp"
#include "Net/Net.hpp"

#include <memory>
#include <string>
#include <vector>

namespace molten {
class Canvas;
}

namespace molten::kit {

/// Whose machine a thing told to a networked entity is for.
enum class NetTo : uint8_t { EveryoneElse, Owner, Server, Everyone };

struct NetworkSession {
    /// What it does when the game starts: "Menu" asks on the screen (Host a game, Join one, or
    /// Play alone); "Host", "Join", "Server" and "Alone" do that at once; "Off" waits to be told
    /// from code. The program can be told from outside too: MOLTEN_NET=host, =server, =alone or
    /// =join:192.168.1.20 in its environment says the same, whatever is set here.
    std::string mode = "Menu";
    std::string address = "127.0.0.1";  // where the host is, for one who joins: numbers or a name, with ":port" if not this one's
    int port = 7777;
    int maxPlayers = 8;
    std::string playerName = "Player";
    std::string playerPrefab = "Prefabs/Player.prefab";    // what everyone in the game gets one of; empty: nothing
    std::string spawnPoints = "Spawn";  // entities whose names start with this: where players appear, in turn
    std::string sharedValues;           // shared values that are the server's on every machine, by commas: "score, gems"
    float sendRate = 20.0f;             // times a second each machine says where its things are
    bool showsStatus = true;            // a line at the top of the screen: hosting or joined, how many players

    void update(World& world, Entity self, float dt);
    void lateUpdate(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
    void onDestroy(World& world, Entity self);

    /// Listens on `port` and plays; listens and does not; joins the host at `address`. False,
    /// and `status` says why, if the port is taken or the address means nothing.
    bool host(World& world);
    bool serve(World& world);
    bool join(World& world);
    /// The game with no one else and no network: this machine is its server and its only
    /// player. For a game that is also played alone, and for trying one without a second machine.
    void playAlone(World& world);
    /// Out of the game: the scene starts over, alone.
    void leave(World& world);
    /// The same over a transport of one's own (a MemoryNetwork's end, in a test).
    void listenOn(World& world, std::unique_ptr<NetTransport> transport, bool alsoPlays);
    void joinOn(World& world, std::unique_ptr<NetTransport> transport, NetAddress server);

    bool isOnline() const;              // hosting, serving, or joined and told what is in the game
    bool isServer() const;
    bool isClient() const { return isOnline() && !isServer(); }
    /// This machine's number in the game: 0 the server's, 1 and up for those who joined; -1 out of one.
    int localId() const;
    int playerCount() const;
    float ping() const;                 // seconds to the server and back, for one who joined

    /// Tells a networked entity something on other machines: there, its components hear the
    /// message `name` as if it had been sent to it there (World::send). To everyone else by
    /// default; or only to the machine that owns it, or to the server.
    void tell(World& world, Entity entity, const std::string& name, float number = 0, const std::string& text = {}, NetTo to = NetTo::EveryoneElse);
    /// The server's: a prefab that appears on every machine, owned by `owner` (0: the server).
    /// What is spawned any other way is only on the machine that spawned it.
    Entity spawn(World& world, const std::string& prefab, Vec3 position, Quat rotation = Quat{}, int owner = 0);

    // ---- state
    std::string status;                 // what is going on, for the screen
    struct State;
    std::shared_ptr<State> state;
    bool hasStarted = false;
};

struct Networked {
    bool syncsPlace = true;             // where it is and how it is turned go from its owner's machine to the others
    bool syncsAnimator = true;          // and what its Animator (on it, or under it) has been told of how it moves
    /// Components of it, and entities under it, by name and by commas, that only its owner's
    /// machine keeps: "CharacterController, CharacterMover" on a player; "Collectible" on a gem.
    std::string ownerOnly;
    /// Messages that, heard by a copy on a machine that does not own it, are told to the
    /// owner's instead, by commas. "shot=damage" tells the owner "damage" where the copy heard
    /// "shot": a shot that lands on a copy of a player hurts the player.
    std::string forwards;
    float smoothing = 0.1f;             // seconds a copy runs behind what it is told, to go smoothly from one word to the next

    void onMessage(World& world, Entity self, const Message& message);

    // ---- state
    uint32_t id = 0;                    // the same on every machine; 0 out of a game
    int owner = 0;                      // 0 the server's, or a player's number
    bool isMine = true;                 // this machine moves it
    bool isPlayer = false;              // someone's own object, made by the session
    struct Told {
        double at = 0;
        Vec3 position{0, 0, 0};
        Quat rotation{};
    };
    std::vector<Told> told;             // the last few words of where it is, on a machine that does not own it
    Vec3 sentPosition{1e9f, 0, 0};      // what was last sent, on the one that does
    Quat sentRotation{};
    double sentAt = -1e9;
    std::vector<std::pair<std::string, float>> animator;   // what its animator was told, as last said (by its owner) or last heard (to pass on)
};

/// The scene's session, if it has one, and the entity it is on.
NetworkSession* sessionOf(World& world, Entity* on = nullptr);

/// Keeps this file's components in a program that links the kit statically.
void linkNetwork();

}  // namespace molten::kit
