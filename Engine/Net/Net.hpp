// Net.hpp
// Machines that play one game together: packets between them, and on top of the packets a
// connection that knows who is there and gets what must arrive there.
//
//   NetTransport   sends and receives packets, each a few hundred bytes that arrive whole or
//                  not at all, in any order: UDP between machines (openUdp), or nothing but
//                  memory between the ends of a MemoryNetwork (for tests, and for trying what
//                  a bad line does to a game)
//   NetHost        one end of a game: it listens and others join it, or it joins one that
//                  listens. What is sent "reliable" arrives, once, in the order it was sent,
//                  however many packets are lost on the way; what is not, arrives at once or
//                  never (where a thing is: by the time it could be sent again it is elsewhere)
//
// The one that listens is peer 0 to everyone; those who join are numbered from 1 as they come.
// They talk only to it: what one of them has to tell another, it passes on.
//
// The kit's NetworkSession and Networked (Kit/Network.hpp) are what a game uses: who plays,
// what each of them moves, what is the same for everyone. This is what they are made of.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace molten {

/// Where a machine is heard: its address on the network (IPv4) and a port.
struct NetAddress {
    uint32_t ip = 0;                    // 192.168.1.20 is 0xC0A80114
    uint16_t port = 0;

    bool operator==(const NetAddress& other) const { return ip == other.ip && port == other.port; }
    bool operator!=(const NetAddress& other) const { return !(*this == other); }
    bool operator<(const NetAddress& other) const { return ip != other.ip ? ip < other.ip : port < other.port; }
    /// "192.168.1.20:7777"
    std::string toString() const;
    /// From "192.168.1.20" or "192.168.1.20:7777"; without a port, `port`. Numbers only: a name is asked of resolveAddress.
    static std::optional<NetAddress> parse(const std::string& text, uint16_t port = 0);
};

class NetTransport {
public:
    virtual ~NetTransport() = default;
    virtual bool send(const NetAddress& to, const uint8_t* data, size_t size) = 0;
    /// The next packet that has arrived, if there is one.
    virtual bool receive(NetAddress& from, std::vector<uint8_t>& data) = 0;
    /// Where it is heard.
    virtual NetAddress address() const = 0;
};

/// UDP on a port of this machine (0: any that is free). `onlyThisMachine` hears nothing from
/// outside it: for tests, and for two copies of a game on one computer. None, and `error` says
/// why, if the port is taken or there is no network.
std::unique_ptr<NetTransport> openUdp(uint16_t port, bool onlyThisMachine = false, std::string* error = nullptr);
/// The address of a machine by its name ("mac-mini.local", "example.com") or its numbers.
/// Waits for the answer: a moment on a home network.
std::optional<NetAddress> resolveAddress(const std::string& host, uint16_t port);
/// This machine's own addresses on the networks it is on, for telling others where to join: "192.168.1.20".
std::vector<std::string> localAddresses();

/// A network that is not there: packets go from one of its ends to another through memory,
/// late or not at all if asked. Its ends are transports like any other.
class MemoryNetwork {
public:
    MemoryNetwork();
    ~MemoryNetwork();
    float loss = 0;                     // 0 to 1: how many packets never arrive
    float delay = 0, jitter = 0;        // seconds every packet takes, and up to how much more
    uint32_t seed = 1;

    /// An end of it, heard at `port` of an address of its own.
    std::unique_ptr<NetTransport> open(uint16_t port);
    /// Time passes: what was on its way long enough arrives.
    void advance(double seconds);
    double now() const;
    int sent() const, lost() const;

    struct Shared;

private:
    std::shared_ptr<Shared> shared_;
};

class NetHost {
public:
    static constexpr int kServer = 0;
    static constexpr size_t kLargestMessage = 1000;     // bytes: what one message can carry

    struct Event {
        enum class Kind { Joined, Left, Data } kind = Kind::Data;
        int peer = 0;                   // who: 0 is the one that listens
        std::string name;               // Joined: what they call themselves. Left: why
        std::vector<uint8_t> data;
        bool isReliable = false;
    };

    NetHost();
    ~NetHost();
    NetHost(const NetHost&) = delete;
    NetHost& operator=(const NetHost&) = delete;

    /// Listens for others to join, as many as `mostPeers`.
    void listen(std::unique_ptr<NetTransport> transport, int mostPeers);
    /// Asks to join the one listening at `server`. Joined (peer 0) is told when it answers;
    /// Left, if it does not in a few seconds, or says no.
    void join(std::unique_ptr<NetTransport> transport, NetAddress server, const std::string& name);
    /// Says goodbye to everyone and stops.
    void close();

    bool isListening() const { return isListening_; }
    bool isJoining() const { return isJoining_; }
    bool isJoined() const { return isJoined_; }
    bool isOpen() const { return transport_ != nullptr; }
    /// Its own number: 0 listening, what the listener gave it once joined, -1 before.
    int id() const { return id_; }
    NetAddress address() const;

    /// Takes what has arrived, sends what is waiting and again what was not heard of, and
    /// notices who has gone quiet. `now` is seconds on any clock that only goes forward.
    void update(double now);
    /// What happened, one thing at a time: false when there is no more.
    bool poll(Event& event);

    /// To one peer (0, for one who joined: there is no one else to talk to). False if it is
    /// too large or there is no such peer.
    bool send(int peer, const std::vector<uint8_t>& data, bool isReliable);
    void sendToAll(const std::vector<uint8_t>& data, bool isReliable, int except = -1);

    std::vector<int> peers() const;
    /// Seconds there and back, as lately measured; 0 for no such peer.
    float ping(int peer) const;
    std::string nameOf(int peer) const;
    /// Seconds without a word before a peer is taken to have gone.
    double quietLimit = 8.0;

private:
    struct Peer;
    Peer* find(int id);
    const Peer* find(int id) const;
    Peer* findAt(const NetAddress& address);
    void receive(double now);
    void read(Peer& peer, const uint8_t* data, size_t size, double now);
    void flush(Peer& peer, double now, bool mustSend);
    void drop(int id, const std::string& why);
    void sendControl(const NetAddress& to, uint8_t kind, const std::vector<uint8_t>& payload);

    std::unique_ptr<NetTransport> transport_;
    std::vector<std::unique_ptr<Peer>> peers_;
    std::deque<Event> events_;
    bool isListening_ = false, isJoining_ = false, isJoined_ = false;
    int id_ = -1, mostPeers_ = 0;
    NetAddress server_;
    std::string name_;
    uint32_t token_ = 0;
    double joinStarted_ = -1, lastAsked_ = -1, now_ = 0;
};

// ---------------------------------------------------------------- what goes in a message

/// Writes numbers and text as bytes, the same on every machine.
struct ByteWriter {
    std::vector<uint8_t> bytes;

    void u8(uint8_t value) { bytes.push_back(value); }
    void u16(uint16_t value) { bytes.push_back(uint8_t(value)), bytes.push_back(uint8_t(value >> 8)); }
    void u32(uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
    }
    void f32(float value);
    /// A number between -1 and 1 in two bytes: a part of a rotation.
    void unit(float value);
    void text(const std::string& value);
};

/// Reads them back. Past the end, it gives zeros and `isGood` is false: a packet cut short
/// or made up is noticed, not followed off the end of memory.
struct ByteReader {
    const uint8_t* at = nullptr;
    const uint8_t* end = nullptr;
    bool isGood = true;

    ByteReader(const uint8_t* data, size_t size) : at(data), end(data + size) {}
    explicit ByteReader(const std::vector<uint8_t>& data) : at(data.data()), end(data.data() + data.size()) {}
    size_t left() const { return size_t(end - at); }
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    float f32();
    float unit();
    std::string text();
    /// The next `size` bytes, or none of them if there are not that many.
    std::vector<uint8_t> take(size_t size);
};

}  // namespace molten
