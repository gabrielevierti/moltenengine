#include "Kit/Network.hpp"

#include "Animation/Animator.hpp"
#include "Game/Component.hpp"
#include "Physics/Collision.hpp"
#include "Rendering/Canvas.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

namespace molten::kit {

void linkNetwork() {}

// What is said between the machines of a session: the first byte of each message.
namespace {

enum Said : uint8_t {
    kWelcome = 1,           // to one who joined: their number, what of the scene is gone already, who is in
    kSpawn,                 // a prefab appears, with its number and whose it is
    kDespawn,               // a thing is gone
    kPlaces,                // where things are, and what their animators were told
    kValue,                 // a shared value is so much
    kTell,                  // a message for an entity
    kReady,                 // to one who joined: that was everything, the game is as you now have it
    kPlayers,               // who is in the game
};

}  // namespace

struct NetworkSession::State {
    NetHost host;
    bool isServer = false, plays = false, isReady = false;
    int localId = -1;
    double clock = 0, sinceSent = 0;
    uint32_t nextId = 100000;                       // of things spawned: above anything a scene has
    std::map<uint32_t, Entity> known;               // every networked thing here, by its number
    std::set<uint32_t> ofTheScene;                  // those that were in the scene from the start
    std::vector<uint32_t> gone;                     // of those, the ones destroyed since: one who joins late is told
    struct Spawned {
        int owner = 0;
        std::string prefab;
        bool isPlayer = false;
    };
    std::map<uint32_t, Spawned> spawned;
    std::map<int, std::string> players;             // who is in, by number
    std::map<int, Entity> playerEntities;
    std::map<std::string, float> sentValues;
    bool isDelivering = false;                      // a message from another machine is being told here: it is not sent on again
    int turn = 0;                                   // which spawn point is next
    // The menu.
    bool isTyping = false;
    int asked = 0;                                  // 1 host, 2 join: clicked while drawing, done at the next update
    Vec2 screen{1280, 720};
};

namespace {

std::vector<std::string> listOf(const std::string& text) {
    std::vector<std::string> out;
    std::string one;
    auto keep = [&] {
        size_t from = one.find_first_not_of(" \t"), to = one.find_last_not_of(" \t");
        if (from != std::string::npos) out.push_back(one.substr(from, to - from + 1));
        one.clear();
    };
    for (char c : text) {
        if (c == ',') keep();
        else one += c;
    }
    keep();
    return out;
}

/// An entity's names from the top of the scene down to it: the same on every machine that read the same scene.
std::string pathOf(Scene& scene, Entity entity) {
    std::string path;
    for (int up = 0; up < 32 && scene.isAlive(entity); up++) {
        const Name* name = scene.get<Name>(entity);
        path = (name ? name->value : std::string("?")) + "/" + path;
        const Transform* place = scene.get<Transform>(entity);
        entity = place ? place->parent : Entity{};
    }
    return path;
}

Animator* animatorOf(Scene& scene, Entity root) {
    if (Animator* own = scene.get<Animator>(root)) return own;
    Animator* found = nullptr;
    scene.each<Animator>([&](Entity entity, Animator& animator) {
        if (!found && Physics::isUnder(scene, entity, root)) found = &animator;
    });
    return found;
}

/// Takes from an entity what only its owner's machine keeps.
void strip(World& world, Entity entity, const std::string& ownerOnly) {
    Scene& scene = world.scene;
    for (const std::string& name : listOf(ownerOnly)) {
        if (name == "RigidBody" || name == "Rigidbody") {
            scene.remove<RigidBody>(entity);
        } else if (name == "Collider") {
            scene.remove<Collider>(entity);
        } else if (name == "Camera") {
            scene.remove<Camera>(entity);
        } else if (name == "Light") {
            scene.remove<Light>(entity);
        } else if (const ComponentType* type = ComponentRegistry::instance().find(name); type && type->has && type->remove) {
            if (type->has(scene, entity)) type->remove(scene, entity);
        } else {
            // Not a component: an entity under it, by that name.
            std::vector<Entity> under;
            scene.each<Name, Transform>([&](Entity child, Name& named, Transform&) {
                if (named.value == name && Physics::isUnder(scene, child, entity)) under.push_back(child);
            });
            for (Entity child : under) scene.destroy(child);
        }
    }
}

void writePlace(ByteWriter& out, uint32_t id, Vec3 position, Quat rotation, const std::vector<std::pair<std::string, float>>& animator) {
    out.u32(id);
    out.f32(position.x), out.f32(position.y), out.f32(position.z);
    Quat turn = simd::normalize(rotation);
    out.unit(turn.vector.x), out.unit(turn.vector.y), out.unit(turn.vector.z), out.unit(turn.vector.w);
    size_t count = std::min<size_t>(animator.size(), 8);
    out.u8(uint8_t(count));
    for (size_t i = 0; i < count; i++) {
        out.text(animator[i].first);
        out.f32(animator[i].second);
    }
}

}  // namespace

NetworkSession* sessionOf(World& world, Entity* on) {
    NetworkSession* found = nullptr;
    world.scene.each<NetworkSession>([&](Entity entity, NetworkSession& session) {
        if (found) return;
        found = &session;
        if (on) *on = entity;
    });
    return found;
}

// ---------------------------------------------------------------- who is in

bool NetworkSession::isOnline() const { return state && (state->isServer || state->isReady); }
bool NetworkSession::isServer() const { return state && state->isServer; }
int NetworkSession::localId() const { return isOnline() ? state->localId : -1; }
int NetworkSession::playerCount() const { return state ? int(state->players.size()) : 0; }
float NetworkSession::ping() const { return state && !state->isServer ? state->host.ping(NetHost::kServer) : 0.0f; }

namespace {

/// Gives every networked thing of the scene its number: in the order of their names, which is
/// the same wherever the same scene was read.
void numberTheScene(World& world, NetworkSession::State& state) {
    Scene& scene = world.scene;
    std::vector<std::pair<std::string, Entity>> found;
    scene.each<Networked>([&](Entity entity, Networked& networked) {
        if (networked.id == 0) found.emplace_back(pathOf(scene, entity), entity);
    });
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    uint32_t next = uint32_t(state.ofTheScene.size()) + 1;
    for (const auto& one : found) {
        Networked* networked = scene.get<Networked>(one.second);
        networked->id = next++;
        networked->owner = 0;
        state.known[networked->id] = one.second;
        state.ofTheScene.insert(networked->id);
    }
}

/// A spawned thing, made here as it is on every machine.
Entity makeSpawned(World& world, NetworkSession::State& state, uint32_t id, int owner, const std::string& prefab, Vec3 position, Quat rotation, bool isPlayer) {
    Scene& scene = world.scene;
    Entity made = world.spawn(prefab, position);
    if (!scene.isAlive(made)) return Entity{};
    if (Transform* place = scene.get<Transform>(made)) place->rotation = rotation;
    if (!scene.get<Networked>(made)) scene.add<Networked>(made);
    Networked* networked = scene.get<Networked>(made);
    networked->id = id;
    networked->owner = owner;
    networked->isMine = owner == state.localId;
    networked->isPlayer = isPlayer;
    std::string ownerOnly = networked->ownerOnly;
    bool isMine = networked->isMine;
    // One's own player keeps the prefab's name (what the camera looks for); the others' are numbered.
    if (isPlayer && !isMine) {
        if (Name* name = scene.get<Name>(made)) name->value += " " + std::to_string(owner + 1);
    }
    if (!isMine) strip(world, made, ownerOnly);
    state.known[id] = made;
    state.spawned[id] = NetworkSession::State::Spawned{owner, prefab, isPlayer};
    if (isPlayer) state.playerEntities[owner] = made;
    scene.updateTransforms();
    return made;
}

std::vector<uint8_t> spawnMessage(uint32_t id, const NetworkSession::State::Spawned& what, Vec3 position, Quat rotation) {
    ByteWriter out;
    out.u8(kSpawn);
    out.u32(id);
    out.u8(uint8_t(what.owner));
    out.u8(what.isPlayer ? 1 : 0);
    out.text(what.prefab);
    out.f32(position.x), out.f32(position.y), out.f32(position.z);
    Quat turn = simd::normalize(rotation);
    out.unit(turn.vector.x), out.unit(turn.vector.y), out.unit(turn.vector.z), out.unit(turn.vector.w);
    return out.bytes;
}

std::vector<uint8_t> playersMessage(const NetworkSession::State& state) {
    ByteWriter out;
    out.u8(kPlayers);
    out.u8(uint8_t(state.players.size()));
    for (const auto& [id, name] : state.players) {
        out.u8(uint8_t(id));
        out.text(name);
    }
    return out.bytes;
}

/// The server makes a player's own object, where the next spawn point is, and says so to everyone.
void spawnPlayer(World& world, NetworkSession& session, int owner) {
    NetworkSession::State& state = *session.state;
    if (session.playerPrefab.empty()) return;
    Scene& scene = world.scene;
    std::vector<std::pair<std::string, Entity>> points;
    if (!session.spawnPoints.empty()) {
        scene.each<Name, Transform>([&](Entity entity, Name& name, Transform&) {
            if (name.value.compare(0, session.spawnPoints.size(), session.spawnPoints) == 0 && !scene.get<Networked>(entity)) points.emplace_back(name.value, entity);
        });
    }
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Vec3 position{float(state.turn) * 1.5f, 0.2f, 0};
    Quat rotation{};
    if (!points.empty()) {
        const Transform* at = scene.get<Transform>(points[size_t(state.turn) % points.size()].second);
        position = at->worldPosition();
        rotation = at->rotation;
    }
    state.turn++;
    uint32_t id = state.nextId++;
    std::string prefab = session.playerPrefab;
    if (!scene.isAlive(makeSpawned(world, state, id, owner, prefab, position, rotation, true))) return;
    state.host.sendToAll(spawnMessage(id, state.spawned[id], position, rotation), true);
}

void forget(NetworkSession::State& state, uint32_t id) {
    state.known.erase(id);
    auto was = state.spawned.find(id);
    if (was == state.spawned.end()) return;
    if (was->second.isPlayer) state.playerEntities.erase(was->second.owner);
    state.spawned.erase(was);
}

void deliver(World& world, NetworkSession::State& state, uint32_t id, const std::string& name, float number, const std::string& text, int from) {
    auto found = state.known.find(id);
    if (found == state.known.end() || !world.scene.isAlive(found->second)) return;
    auto who = state.playerEntities.find(from);
    state.isDelivering = true;
    world.send(name, found->second, number, text, who == state.playerEntities.end() ? Entity{} : who->second);
    state.isDelivering = false;
}

/// The server sends a message for an entity on to whoever it is for, and hears it itself if it is among them.
void route(World& world, NetworkSession::State& state, uint32_t id, NetTo to, const std::string& name, float number, const std::string& text, int from) {
    ByteWriter out;
    out.u8(kTell);
    out.u32(id);
    out.u8(uint8_t(to));
    out.text(name);
    out.f32(number);
    out.text(text);
    out.u8(uint8_t(from));
    int owner = 0;
    if (auto found = state.known.find(id); found != state.known.end()) {
        if (const Networked* networked = world.scene.get<Networked>(found->second)) owner = networked->owner;
    }
    switch (to) {
        case NetTo::EveryoneElse:
            state.host.sendToAll(out.bytes, true, from);
            if (from != 0) deliver(world, state, id, name, number, text, from);
            break;
        case NetTo::Everyone:
            state.host.sendToAll(out.bytes, true);
            deliver(world, state, id, name, number, text, from);
            break;
        case NetTo::Owner:
            if (owner == 0) deliver(world, state, id, name, number, text, from);
            else state.host.send(owner, out.bytes, true);
            break;
        case NetTo::Server:
            deliver(world, state, id, name, number, text, from);
            break;
    }
}

}  // namespace

void NetworkSession::listenOn(World& world, std::unique_ptr<NetTransport> transport, bool alsoPlays) {
    if (!state) state = std::make_shared<State>();
    if (!transport) return;
    numberTheScene(world, *state);
    state->host.listen(std::move(transport), std::max(maxPlayers - (alsoPlays ? 1 : 0), 1));
    state->isServer = true;
    state->plays = alsoPlays;
    state->localId = 0;
    // What is in the scene is the server's: this machine runs it.
    world.scene.each<Networked>([](Entity, Networked& networked) { networked.isMine = networked.owner == 0; });
    if (alsoPlays) {
        state->players[0] = playerName;
        spawnPlayer(world, *this, 0);
    }
    status = alsoPlays ? "Hosting" : "Serving";
}

void NetworkSession::joinOn(World& world, std::unique_ptr<NetTransport> transport, NetAddress server) {
    if (!state) state = std::make_shared<State>();
    if (!transport) return;
    numberTheScene(world, *state);
    state->host.join(std::move(transport), server, playerName);
    state->isServer = false;
    state->isReady = false;
    status = "Joining " + server.toString() + "...";
}

bool NetworkSession::host(World& world) {
    std::string error;
    std::unique_ptr<NetTransport> transport = openUdp(uint16_t(port), false, &error);
    if (!transport) {
        status = "Cannot host: " + error;
        return false;
    }
    listenOn(world, std::move(transport), true);
    return true;
}

bool NetworkSession::serve(World& world) {
    std::string error;
    std::unique_ptr<NetTransport> transport = openUdp(uint16_t(port), false, &error);
    if (!transport) {
        status = "Cannot serve: " + error;
        return false;
    }
    listenOn(world, std::move(transport), false);
    return true;
}

bool NetworkSession::join(World& world) {
    std::optional<NetAddress> server = resolveAddress(address, uint16_t(port));
    if (!server) {
        status = "There is no \"" + address + "\" to join";
        return false;
    }
    std::string error;
    std::unique_ptr<NetTransport> transport = openUdp(0, false, &error);
    if (!transport) {
        status = "Cannot join: " + error;
        return false;
    }
    joinOn(world, std::move(transport), *server);
    return true;
}

void NetworkSession::leave(World& world) {
    if (!state) return;
    bool wasIn = state->host.isOpen();
    state->host.close();
    state.reset();
    world.input.setPointerFree(false);
    world.input.isTakingText = false;
    if (wasIn) world.reloadScene();             // the scene as it was, alone
}

void NetworkSession::onDestroy(World& world, Entity) {
    if (state) state->host.close();
    world.input.setPointerFree(false);
    world.input.isTakingText = false;
}

Entity NetworkSession::spawn(World& world, const std::string& prefab, Vec3 position, Quat rotation, int owner) {
    if (!isServer()) return Entity{};
    uint32_t id = state->nextId++;
    Entity made = makeSpawned(world, *state, id, owner, prefab, position, rotation, false);
    if (!world.scene.isAlive(made)) return Entity{};
    state->host.sendToAll(spawnMessage(id, state->spawned[id], position, rotation), true);
    return made;
}

void NetworkSession::tell(World& world, Entity entity, const std::string& name, float number, const std::string& text, NetTo to) {
    if (!isOnline()) return;
    const Networked* networked = world.scene.get<Networked>(entity);
    if (!networked || networked->id == 0) return;
    uint32_t id = networked->id;
    if (state->isServer) {
        route(world, *state, id, to, name, number, text, 0);
        return;
    }
    ByteWriter out;
    out.u8(kTell);
    out.u32(id);
    out.u8(uint8_t(to));
    out.text(name);
    out.f32(number);
    out.text(text);
    out.u8(uint8_t(state->localId));
    state->host.send(NetHost::kServer, out.bytes, true);         // (told to everyone, it comes back to us from the server with the rest)
}

// ---------------------------------------------------------------- each frame

void NetworkSession::update(World& world, Entity, float dt) {
    if (!state) state = std::make_shared<State>();
    State& net = *state;
    Scene& scene = world.scene;
    if (!hasStarted) {
        hasStarted = true;
        numberTheScene(world, net);
        std::string wanted = mode;
        if (const char* told = std::getenv("MOLTEN_NET"); told && *told) {
            wanted = told;
            if (wanted.rfind("join:", 0) == 0) address = wanted.substr(5), wanted = "Join";
        }
        if (wanted == "Host" || wanted == "host") host(world);
        else if (wanted == "Server" || wanted == "server") serve(world);
        else if (wanted == "Join" || wanted == "join") join(world);
    }
    // What was clicked in the menu.
    if (net.asked == 1) host(world);
    else if (net.asked == 2) join(world);
    net.asked = 0;
    // While the menu is on the screen the pointer is out, whatever view would have it.
    bool showsMenu = mode == "Menu" && !net.host.isOpen();
    world.input.setPointerFree(showsMenu);
    if (!showsMenu && net.isTyping) net.isTyping = false, world.input.isTakingText = false;

    net.clock += double(dt);
    if (!net.host.isOpen()) return;
    net.host.update(net.clock);

    NetHost::Event event;
    while (net.host.poll(event)) {
        if (event.kind == NetHost::Event::Kind::Joined && net.isServer) {
            // Someone new: told what the game is now, then given their own player, and everyone told who is in.
            int who = event.peer;
            net.players[who] = event.name.empty() ? "Player" : event.name;
            ByteWriter welcome;
            welcome.u8(kWelcome);
            welcome.u8(uint8_t(who));
            welcome.u16(uint16_t(net.gone.size()));
            for (uint32_t id : net.gone) welcome.u32(id);
            net.host.send(who, welcome.bytes, true);
            for (const auto& [id, what] : net.spawned) {
                const Transform* place = scene.get<Transform>(net.known[id]);
                if (place) net.host.send(who, spawnMessage(id, what, place->position, place->rotation), true);
            }
            for (const auto& [name, value] : net.sentValues) {
                ByteWriter out;
                out.u8(kValue);
                out.text(name);
                out.f32(value);
                net.host.send(who, out.bytes, true);
            }
            ByteWriter ready;
            ready.u8(kReady);
            net.host.send(who, ready.bytes, true);
            spawnPlayer(world, *this, who);
            net.host.sendToAll(playersMessage(net), true);
        } else if (event.kind == NetHost::Event::Kind::Left) {
            if (!net.isServer) {
                // The game is over for us: the scene again, alone, and the menu.
                std::string why = event.name;
                bool wasIn = net.isReady;
                state.reset();
                world.input.setPointerFree(false);
                if (wasIn) world.reloadScene();
                else state = std::make_shared<State>(), state->clock = 0, numberTheScene(world, *state);
                status = wasIn ? "The game ended: " + why : "Could not join: " + why;
                return;
            }
            // Their things go with them.
            std::vector<uint32_t> theirs;
            for (const auto& [id, what] : net.spawned) {
                if (what.owner == event.peer) theirs.push_back(id);
            }
            for (uint32_t id : theirs) {
                if (scene.isAlive(net.known[id])) scene.destroy(net.known[id]);
            }
            net.players.erase(event.peer);
            net.host.sendToAll(playersMessage(net), true);
        } else if (event.kind == NetHost::Event::Kind::Data) {
            ByteReader in(event.data);
            uint8_t said = in.u8();
            if (said == kPlaces) {
                while (in.left() > 0 && in.isGood) {
                    uint32_t id = in.u32();
                    Vec3 position{in.f32(), in.f32(), in.f32()};
                    float x = in.unit(), y = in.unit(), z = in.unit(), w = in.unit();
                    std::vector<std::pair<std::string, float>> animator;
                    for (int count = in.u8(); count > 0 && in.isGood; count--) {
                        std::string name = in.text();
                        animator.emplace_back(name, in.f32());
                    }
                    if (!in.isGood) break;
                    auto found = net.known.find(id);
                    if (found == net.known.end()) continue;
                    Networked* networked = scene.get<Networked>(found->second);
                    if (!networked || networked->isMine) continue;
                    // One who joined can say where their own things are, and no one else's.
                    if (net.isServer && networked->owner != event.peer) continue;
                    if (!std::isfinite(position.x + position.y + position.z) || x * x + y * y + z * z + w * w < 0.25f) continue;
                    networked->told.push_back(Networked::Told{net.clock, position, simd::normalize(Quat(x, y, z, w))});
                    if (networked->told.size() > 12) networked->told.erase(networked->told.begin());
                    networked->animator = animator;
                    bool tellsAnimator = networked->syncsAnimator && !animator.empty();
                    if (tellsAnimator) {
                        if (Animator* shown = animatorOf(scene, found->second)) {
                            for (const auto& [name, value] : animator) shown->setFloat(name, value);
                        }
                    }
                }
            } else if (said == kTell) {
                uint32_t id = in.u32();
                NetTo to = NetTo(std::min<int>(in.u8(), int(NetTo::Everyone)));
                std::string name = in.text();
                float number = in.f32();
                std::string text = in.text();
                int from = in.u8();
                if (!in.isGood) continue;
                if (net.isServer) route(world, net, id, to, name, number, text, event.peer);    // who it came from is who sent it, whatever it says
                else deliver(world, net, id, name, number, text, from);
            } else if (net.isServer) {
                continue;                       // the rest is the server's to say, not to be told
            } else if (said == kWelcome) {
                net.localId = in.u8();
                std::vector<uint32_t> gone;
                for (int count = in.u16(); count > 0 && in.isGood; count--) gone.push_back(in.u32());
                // The scene is the server's: its things are not ours to run, and some are gone already.
                std::vector<std::pair<Entity, std::string>> notMine;
                scene.each<Networked>([&](Entity entity, Networked& networked) {
                    if (networked.owner != 0 || net.spawned.count(networked.id)) return;
                    networked.isMine = false;
                    notMine.emplace_back(entity, networked.ownerOnly);
                });
                for (const auto& [entity, ownerOnly] : notMine) strip(world, entity, ownerOnly);
                for (uint32_t id : gone) {
                    auto found = net.known.find(id);
                    if (found == net.known.end()) continue;
                    if (scene.isAlive(found->second)) scene.destroy(found->second);
                    net.known.erase(found);
                }
            } else if (said == kSpawn) {
                uint32_t id = in.u32();
                int owner = in.u8();
                bool isPlayer = in.u8() != 0;
                std::string prefab = in.text();
                Vec3 position{in.f32(), in.f32(), in.f32()};
                float x = in.unit(), y = in.unit(), z = in.unit(), w = in.unit();
                if (!in.isGood || net.known.count(id)) continue;
                Quat rotation = x * x + y * y + z * z + w * w > 0.25f ? simd::normalize(Quat(x, y, z, w)) : Quat(0, 0, 0, 1);
                makeSpawned(world, net, id, owner, prefab, position, rotation, isPlayer);
            } else if (said == kDespawn) {
                uint32_t id = in.u32();
                auto found = net.known.find(id);
                if (!in.isGood || found == net.known.end()) continue;
                if (scene.isAlive(found->second)) scene.destroy(found->second);
                forget(net, id);
            } else if (said == kValue) {
                std::string name = in.text();
                float value = in.f32();
                if (in.isGood) world.setValue(name, value);
            } else if (said == kReady) {
                net.isReady = true;
                status = "Joined";
            } else if (said == kPlayers) {
                net.players.clear();
                for (int count = in.u8(); count > 0 && in.isGood; count--) {
                    int id = in.u8();
                    net.players[id] = in.text();
                }
            }
        }
    }

    // What has been destroyed here: on the server, everyone is told; elsewhere it is only forgotten.
    std::vector<uint32_t> dead;
    for (const auto& [id, entity] : net.known) {
        if (!scene.isAlive(entity)) dead.push_back(id);
    }
    for (uint32_t id : dead) {
        if (net.isServer) {
            if (net.ofTheScene.count(id)) net.gone.push_back(id);
            ByteWriter out;
            out.u8(kDespawn);
            out.u32(id);
            net.host.sendToAll(out.bytes, true);
        }
        forget(net, id);
    }
    // The values everyone shares are the server's: said when they change.
    if (net.isServer) {
        for (const std::string& name : listOf(sharedValues)) {
            float value = world.value(name);
            auto sent = net.sentValues.find(name);
            if (sent != net.sentValues.end() && sent->second == value) continue;
            net.sentValues[name] = value;
            ByteWriter out;
            out.u8(kValue);
            out.text(name);
            out.f32(value);
            net.host.sendToAll(out.bytes, true);
        }
    }
}

void NetworkSession::lateUpdate(World& world, Entity, float dt) {
    if (!state || !isOnline()) return;
    State& net = *state;
    Scene& scene = world.scene;

    // Copies of what other machines move go where they were told, a moment behind, smoothly from one word to the next.
    scene.each<Networked, Transform>([&](Entity, Networked& networked, Transform& place) {
        if (networked.isMine || !networked.syncsPlace || networked.told.empty()) return;
        double at = net.clock - double(std::max(networked.smoothing, 0.0f));
        const std::vector<Networked::Told>& told = networked.told;
        size_t next = 0;
        while (next < told.size() && told[next].at <= at) next++;
        if (next == 0) {
            place.position = told.front().position, place.rotation = told.front().rotation;
        } else if (next == told.size()) {
            place.position = told.back().position, place.rotation = told.back().rotation;
        } else {
            const Networked::Told& a = told[next - 1];
            const Networked::Told& b = told[next];
            float t = float((at - a.at) / std::max(b.at - a.at, 1e-6));
            place.position = lerp(a.position, b.position, t);
            place.rotation = simd::normalize(simd::slerp(a.rotation, b.rotation, t));
        }
    });

    // Now and then, each machine says where its own things are. The server also passes on what the others said of theirs.
    net.sinceSent += double(dt);
    if (net.sinceSent < 1.0 / double(std::max(sendRate, 1.0f))) return;
    net.sinceSent = 0;
    struct Place {
        uint32_t id;
        int owner;
        std::vector<uint8_t> bytes;
    };
    std::vector<Place> places;
    scene.each<Networked, Transform>([&](Entity entity, Networked& networked, Transform& place) {
        if (networked.id == 0 || !networked.syncsPlace) return;
        ByteWriter out;
        if (networked.isMine) {
            std::vector<std::pair<std::string, float>> animator;
            if (networked.syncsAnimator) {
                if (const Animator* shown = animatorOf(scene, entity)) {
                    for (const auto& [name, value] : shown->parameters) animator.emplace_back(name, value);
                    std::sort(animator.begin(), animator.end());
                }
            }
            // Not said again while nothing of it changes, but for once a second: a word may have been lost.
            bool moved = simd::length(place.position - networked.sentPosition) > 0.002f || std::abs(simd::dot(place.rotation.vector, networked.sentRotation.vector)) < 0.99999f;
            if (!moved && animator == networked.animator && net.clock - networked.sentAt < 1.0) return;
            networked.sentPosition = place.position, networked.sentRotation = place.rotation, networked.sentAt = net.clock;
            networked.animator = animator;
            writePlace(out, networked.id, place.position, place.rotation, animator);
        } else if (net.isServer && !networked.told.empty() && networked.told.back().at > networked.sentAt) {
            // Someone else's, as they last said it: passed on to the rest.
            networked.sentAt = networked.told.back().at;
            writePlace(out, networked.id, networked.told.back().position, networked.told.back().rotation, networked.animator);
        } else {
            return;
        }
        places.push_back(Place{networked.id, networked.owner, std::move(out.bytes)});
    });
    if (places.empty()) return;
    auto sendTo = [&](int peer) {
        ByteWriter out;
        out.u8(kPlaces);
        for (const Place& place : places) {
            if (place.owner == peer && net.isServer) continue;          // they know where their own things are
            if (out.bytes.size() + place.bytes.size() > 900) {
                net.host.send(peer, out.bytes, false);
                out.bytes.assign(1, uint8_t(kPlaces));
            }
            out.bytes.insert(out.bytes.end(), place.bytes.begin(), place.bytes.end());
        }
        if (out.bytes.size() > 1) net.host.send(peer, out.bytes, false);
    };
    for (int peer : net.host.peers()) sendTo(peer);
}

// ---------------------------------------------------------------- on the screen

void NetworkSession::draw(World& world, Entity, Canvas& canvas) {
    if (!state) return;
    State& net = *state;
    Vec2 screen = canvas.size();
    const Vec4 ink{1, 1, 1, 0.92f}, faint{1, 1, 1, 0.55f}, glass{0.05f, 0.055f, 0.07f, 0.86f}, accent{1.0f, 0.48f, 0.18f, 1};
    if (net.host.isOpen() || mode != "Menu") {
        if (!showsStatus || status.empty()) return;
        // A line at the top left: what this machine is in the game, and how many are in it.
        std::string line = status;
        if (isOnline()) {
            int count = playerCount();
            line += ", " + std::to_string(count) + " in the game";
            if (isServer()) {
                line += ", port " + std::to_string(port);
                std::vector<std::string> mine = localAddresses();
                if (!mine.empty()) line += ". Others join at " + mine.front();
            } else {
                line += ", " + std::to_string(int(ping() * 1000 + 0.5f)) + " ms";
            }
        }
        Vec2 size = canvas.measureText(line, 13);
        canvas.roundedRect(Vec2{10, 10}, Vec2{size.x + 20, 26}, 6, Vec4{0, 0, 0, 0.42f});
        canvas.text(line, Vec2{20, 16}, 13, faint);
        return;
    }

    // The menu: host a game, or join one at an address.
    const Input& input = world.input;
    Vec2 pointer = input.mousePosition();
    bool clicked = input.wasPressed(MouseButton::Left);
    Vec2 size{380, 252}, corner = (screen - size) * 0.5f;
    canvas.rect(Vec2{0, 0}, screen, Vec4{0, 0, 0, 0.35f});
    canvas.roundedRect(corner, size, 12, glass);
    canvas.roundedRectOutline(corner, size, 12, 1.5f, Vec4{1, 1, 1, 0.14f});
    canvas.text("Play together", corner + Vec2{size.x * 0.5f, 22}, 24, ink, TextAlign::Center);
    auto button = [&](Vec2 at, Vec2 extent, const char* label, bool isMain) {
        bool isOver = pointer.x >= at.x && pointer.x <= at.x + extent.x && pointer.y >= at.y && pointer.y <= at.y + extent.y;
        Vec4 fill = isMain ? accent : Vec4{1, 1, 1, 0.12f};
        if (isOver) fill = isMain ? Vec4{1.0f, 0.58f, 0.3f, 1} : Vec4{1, 1, 1, 0.2f};
        canvas.roundedRect(at, extent, 8, fill);
        canvas.text(label, at + Vec2{extent.x * 0.5f, (extent.y - 16) * 0.5f - 1}, 16, isMain ? Vec4{0.08f, 0.05f, 0.03f, 1} : ink, TextAlign::Center);
        return isOver && clicked;
    };
    if (button(corner + Vec2{24, 68}, Vec2{size.x - 48, 44}, "Host a game", true)) net.asked = 1;
    canvas.text("or join one at", corner + Vec2{24, 128}, 13, faint);
    // The address: clicked, it is typed in.
    Vec2 fieldAt = corner + Vec2{24, 150}, fieldSize{size.x - 48 - 92, 40};
    bool isOverField = pointer.x >= fieldAt.x && pointer.x <= fieldAt.x + fieldSize.x && pointer.y >= fieldAt.y && pointer.y <= fieldAt.y + fieldSize.y;
    if (clicked) net.isTyping = isOverField;
    world.input.isTakingText = net.isTyping;
    if (net.isTyping) {
        for (char typed : input.textInput()) {
            if (typed > ' ' && typed < 127 && address.size() < 64) address += typed;
        }
        if (input.wasPressed(Key::Backspace) && !address.empty()) address.pop_back();
        if (input.wasPressed(Key::Enter)) net.asked = 2;
    }
    canvas.roundedRect(fieldAt, fieldSize, 8, Vec4{0, 0, 0, 0.45f});
    canvas.roundedRectOutline(fieldAt, fieldSize, 8, 1.5f, net.isTyping ? accent : Vec4{1, 1, 1, 0.18f});
    bool showsCaret = net.isTyping && std::fmod(net.clock, 1.0) < 0.55;
    canvas.text(address + (showsCaret ? "|" : ""), fieldAt + Vec2{12, 12}, 15, ink);
    if (button(fieldAt + Vec2{fieldSize.x + 12, 0}, Vec2{80, 40}, "Join", false)) net.asked = 2;
    if (!status.empty()) canvas.text(status, corner + Vec2{size.x * 0.5f, 212}, 12, Vec4{1.0f, 0.7f, 0.5f, 0.95f}, TextAlign::Center);
}

// ---------------------------------------------------------------- Networked

void Networked::onMessage(World& world, Entity self, const Message& message) {
    if (isMine || forwards.empty() || id == 0) return;
    NetworkSession* session = sessionOf(world);
    if (!session || !session->isOnline() || session->state->isDelivering) return;
    for (const std::string& one : listOf(forwards)) {
        size_t equals = one.find('=');
        std::string heard = one.substr(0, equals), toldAs = equals == std::string::npos ? heard : one.substr(equals + 1);
        if (heard != message.name) continue;
        session->tell(world, self, toldAs, message.number, message.text, NetTo::Owner);
        return;
    }
}

}  // namespace molten::kit

namespace molten::kit {

MOLTEN_COMPONENT(NetworkSession, MOLTEN_FIELD(mode).choices({"Menu", "Host", "Join", "Server", "Off"}).help("What it does when the game starts: ask on the screen, or do it at once"),
                 MOLTEN_FIELD(address).help("Where the host is, for one who joins: numbers or a name"), MOLTEN_FIELD(port).range(1, 65535),
                 MOLTEN_FIELD(maxPlayers).range(1, 64), MOLTEN_FIELD(playerName).help("What this player is called in the game"),
                 MOLTEN_FIELD(playerPrefab).picks("prefab").help("Everyone in the game gets one, theirs to move"),
                 MOLTEN_FIELD(spawnPoints).picks("entityOrTag").help("Entities whose names start with this: where players appear"),
                 MOLTEN_FIELD(sharedValues).help("Shared values that are the server's on every machine, by commas"),
                 MOLTEN_FIELD(sendRate).range(1, 60).help("Times a second each machine says where its things are"), MOLTEN_FIELD(showsStatus))
MOLTEN_COMPONENT(Networked, MOLTEN_FIELD(syncsPlace).help("Where it is goes from its owner's machine to the others"),
                 MOLTEN_FIELD(syncsAnimator).help("And what its Animator was told of how it moves"),
                 MOLTEN_FIELD(ownerOnly).help("Components of it and entities under it, by commas, that only its owner's machine keeps"),
                 MOLTEN_FIELD(forwards).help("Messages a copy hears that are told to its owner's machine instead, by commas; \"shot=damage\" renames one"),
                 MOLTEN_FIELD(smoothing).range(0, 1).help("Seconds a copy runs behind what it is told, to move smoothly"))

}  // namespace molten::kit
