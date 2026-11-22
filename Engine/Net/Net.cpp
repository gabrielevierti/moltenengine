#include "Net/Net.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace molten {

// ---------------------------------------------------------------- addresses, bytes

std::string NetAddress::toString() const {
    char text[40];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", unsigned(ip >> 24), unsigned((ip >> 16) & 255), unsigned((ip >> 8) & 255), unsigned(ip & 255), unsigned(port));
    return text;
}

std::optional<NetAddress> NetAddress::parse(const std::string& text, uint16_t port) {
    unsigned a = 0, b = 0, c = 0, d = 0, p = port;
    char after = 0;
    int read = std::sscanf(text.c_str(), " %u.%u.%u.%u:%u %c", &a, &b, &c, &d, &p, &after);
    if (read != 4 && read != 5) return std::nullopt;
    if (read == 4 && text.find(':') != std::string::npos) return std::nullopt;
    if (a > 255 || b > 255 || c > 255 || d > 255 || p > 65535) return std::nullopt;
    return NetAddress{(a << 24) | (b << 16) | (c << 8) | d, uint16_t(p)};
}

void ByteWriter::f32(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    u32(bits);
}

void ByteWriter::unit(float value) {
    float clamped = value < -1 ? -1 : value > 1 ? 1 : value;
    u16(uint16_t(int16_t(clamped * 32767.0f)));
}

void ByteWriter::text(const std::string& value) {
    size_t size = std::min<size_t>(value.size(), 65535);
    u16(uint16_t(size));
    bytes.insert(bytes.end(), value.begin(), value.begin() + long(size));
}

uint8_t ByteReader::u8() {
    if (left() < 1) {
        isGood = false;
        return 0;
    }
    return *at++;
}

uint16_t ByteReader::u16() {
    if (left() < 2) {
        isGood = false, at = end;
        return 0;
    }
    uint16_t value = uint16_t(at[0] | (at[1] << 8));
    at += 2;
    return value;
}

uint32_t ByteReader::u32() {
    if (left() < 4) {
        isGood = false, at = end;
        return 0;
    }
    uint32_t value = uint32_t(at[0]) | (uint32_t(at[1]) << 8) | (uint32_t(at[2]) << 16) | (uint32_t(at[3]) << 24);
    at += 4;
    return value;
}

float ByteReader::f32() {
    uint32_t bits = u32();
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

float ByteReader::unit() { return float(int16_t(u16())) / 32767.0f; }

std::string ByteReader::text() {
    size_t size = u16();
    if (left() < size) {
        isGood = false, at = end;
        return {};
    }
    std::string value(reinterpret_cast<const char*>(at), size);
    at += size;
    return value;
}

std::vector<uint8_t> ByteReader::take(size_t size) {
    if (left() < size) {
        isGood = false, at = end;
        return {};
    }
    std::vector<uint8_t> value(at, at + size);
    at += size;
    return value;
}

// ---------------------------------------------------------------- MemoryNetwork

struct MemoryNetwork::Shared {
    struct Packet {
        NetAddress to, from;
        std::vector<uint8_t> data;
        double at = 0;
    };
    std::map<NetAddress, std::deque<Packet>> arrived;
    std::vector<Packet> onTheWay;
    double now = 0;
    uint32_t state = 1;
    int ends = 0, sent = 0, lost = 0;
    MemoryNetwork* owner = nullptr;

    float chance() {
        state = state * 1664525u + 1013904223u;
        return float(state >> 8) * (1.0f / 16777216.0f);
    }
};

namespace {

class MemoryEnd : public NetTransport {
public:
    MemoryEnd(std::shared_ptr<MemoryNetwork::Shared> shared, NetAddress address, const MemoryNetwork* network)
        : shared_(std::move(shared)), address_(address), network_(network) {
        shared_->arrived[address_];
    }
    ~MemoryEnd() override { shared_->arrived.erase(address_); }

    bool send(const NetAddress& to, const uint8_t* data, size_t size) override {
        shared_->sent++;
        // The network may be gone before its ends: what is sent then goes nowhere.
        const MemoryNetwork* network = shared_->owner ? network_ : nullptr;
        float loss = network ? network->loss : 0, delay = network ? network->delay : 0, jitter = network ? network->jitter : 0;
        if (loss > 0 && shared_->chance() < loss) {
            shared_->lost++;
            return true;
        }
        MemoryNetwork::Shared::Packet packet{to, address_, std::vector<uint8_t>(data, data + size), shared_->now + double(delay) + double(jitter * shared_->chance())};
        if (packet.at <= shared_->now) {
            auto there = shared_->arrived.find(to);
            if (there != shared_->arrived.end()) there->second.push_back(std::move(packet));
        } else {
            shared_->onTheWay.push_back(std::move(packet));
        }
        return true;
    }
    bool receive(NetAddress& from, std::vector<uint8_t>& data) override {
        std::deque<MemoryNetwork::Shared::Packet>& mine = shared_->arrived[address_];
        if (mine.empty()) return false;
        from = mine.front().from;
        data = std::move(mine.front().data);
        mine.pop_front();
        return true;
    }
    NetAddress address() const override { return address_; }

private:
    std::shared_ptr<MemoryNetwork::Shared> shared_;
    NetAddress address_;
    const MemoryNetwork* network_;
};

}  // namespace

MemoryNetwork::MemoryNetwork() : shared_(std::make_shared<Shared>()) { shared_->owner = this; }
MemoryNetwork::~MemoryNetwork() { shared_->owner = nullptr; }

std::unique_ptr<NetTransport> MemoryNetwork::open(uint16_t port) {
    shared_->owner = this;
    shared_->state = shared_->ends == 0 ? seed : shared_->state;
    NetAddress address{0x0A000001u + uint32_t(shared_->ends++), port};
    return std::make_unique<MemoryEnd>(shared_, address, this);
}

void MemoryNetwork::advance(double seconds) {
    shared_->now += seconds;
    std::vector<Shared::Packet>& waiting = shared_->onTheWay;
    // In the order they were to arrive: one that was late is behind one that was not.
    std::stable_sort(waiting.begin(), waiting.end(), [](const Shared::Packet& a, const Shared::Packet& b) { return a.at < b.at; });
    size_t done = 0;
    for (; done < waiting.size() && waiting[done].at <= shared_->now; done++) {
        auto there = shared_->arrived.find(waiting[done].to);
        if (there != shared_->arrived.end()) there->second.push_back(std::move(waiting[done]));
    }
    waiting.erase(waiting.begin(), waiting.begin() + long(done));
}

double MemoryNetwork::now() const { return shared_->now; }
int MemoryNetwork::sent() const { return shared_->sent; }
int MemoryNetwork::lost() const { return shared_->lost; }

// ---------------------------------------------------------------- NetHost

namespace {

const uint32_t kMagic = 0x3154'4C4Du;            // "MLT1": another program's packets, or another version's, are not ours
const size_t kLargestPacket = 1200;             // bytes: what gets through any network in one piece
enum Kind : uint8_t { kAsk = 1, kYes = 2, kNo = 3, kData = 4, kBye = 5 };

}  // namespace

struct NetHost::Peer {
    int id = 0;
    NetAddress address;
    std::string name;
    uint32_t token = 0;
    double lastHeard = 0, lastSent = -1;
    // What it has sent us: the newest packet, and which of the 32 before it.
    bool hasHeard = false;
    uint16_t heard = 0;
    uint32_t heardBefore = 0;
    bool owesAnswer = false;
    // What we send it.
    uint16_t nextSequence = 0;
    struct Waiting {
        uint16_t id = 0;
        std::vector<uint8_t> data;
        double sentAt = -1;
    };
    std::deque<Waiting> reliable;                   // not yet known to have arrived
    uint16_t nextReliable = 0;
    std::deque<std::vector<uint8_t>> loose;         // for the next packet, and no other
    struct Sent {
        uint16_t sequence = 0;
        double at = 0;
        std::vector<uint16_t> ids;
    };
    std::deque<Sent> sent;                          // lately, to know what an answer answers
    // What it sends us that must come in order.
    uint16_t expected = 0;
    std::map<uint16_t, std::vector<uint8_t>> early;
    float ping = 0;
};

NetHost::NetHost() = default;
NetHost::~NetHost() { close(); }

NetAddress NetHost::address() const { return transport_ ? transport_->address() : NetAddress{}; }

NetHost::Peer* NetHost::find(int id) {
    for (auto& peer : peers_) {
        if (peer->id == id) return peer.get();
    }
    return nullptr;
}

const NetHost::Peer* NetHost::find(int id) const {
    for (const auto& peer : peers_) {
        if (peer->id == id) return peer.get();
    }
    return nullptr;
}

NetHost::Peer* NetHost::findAt(const NetAddress& address) {
    for (auto& peer : peers_) {
        if (peer->address == address) return peer.get();
    }
    return nullptr;
}

void NetHost::listen(std::unique_ptr<NetTransport> transport, int mostPeers) {
    close();
    transport_ = std::move(transport);
    isListening_ = transport_ != nullptr;
    mostPeers_ = std::max(mostPeers, 1);
    id_ = isListening_ ? kServer : -1;
}

void NetHost::join(std::unique_ptr<NetTransport> transport, NetAddress server, const std::string& name) {
    close();
    transport_ = std::move(transport);
    if (!transport_) return;
    isJoining_ = true;
    server_ = server;
    name_ = name;
    joinStarted_ = lastAsked_ = -1;
    // Something of this try's own, so that the listener knows an old try of ours from a new one.
    static uint32_t tries = 0;
    token_ = (uint32_t(transport_->address().port) << 16) ^ (++tries * 2654435761u) ^ uint32_t(reinterpret_cast<uintptr_t>(this));
}

void NetHost::sendControl(const NetAddress& to, uint8_t kind, const std::vector<uint8_t>& payload) {
    if (!transport_) return;
    ByteWriter out;
    out.u32(kMagic);
    out.u8(kind);
    out.bytes.insert(out.bytes.end(), payload.begin(), payload.end());
    transport_->send(to, out.bytes.data(), out.bytes.size());
}

void NetHost::close() {
    if (transport_) {
        // Twice: goodbyes get lost like anything else.
        for (int again = 0; again < 2; again++) {
            for (const auto& peer : peers_) sendControl(peer->address, kBye, {});
        }
    }
    peers_.clear();
    transport_.reset();
    isListening_ = isJoining_ = isJoined_ = false;
    id_ = -1;
}

void NetHost::drop(int id, const std::string& why) {
    for (size_t i = 0; i < peers_.size(); i++) {
        if (peers_[i]->id != id) continue;
        Event event;
        event.kind = Event::Kind::Left;
        event.peer = id;
        event.name = why;
        events_.push_back(std::move(event));
        peers_.erase(peers_.begin() + long(i));
        break;
    }
    if (!isListening_ && id == kServer) {
        // The one we joined is gone: there is no game to be in.
        peers_.clear();
        transport_.reset();
        isJoining_ = isJoined_ = false;
        id_ = -1;
    }
}

bool NetHost::send(int peer, const std::vector<uint8_t>& data, bool isReliable) {
    Peer* to = find(peer);
    if (!to || data.size() > kLargestMessage || (!isListening_ && !isJoined_)) return false;
    if (isReliable) {
        Peer::Waiting waiting;
        waiting.id = to->nextReliable++;
        waiting.data = data;
        to->reliable.push_back(std::move(waiting));
    } else {
        to->loose.push_back(data);
    }
    return true;
}

void NetHost::sendToAll(const std::vector<uint8_t>& data, bool isReliable, int except) {
    for (const auto& peer : peers_) {
        if (peer->id != except) send(peer->id, data, isReliable);
    }
}

std::vector<int> NetHost::peers() const {
    std::vector<int> ids;
    if (!isListening_ && !isJoined_) return ids;
    for (const auto& peer : peers_) ids.push_back(peer->id);
    return ids;
}

float NetHost::ping(int peer) const {
    const Peer* found = find(peer);
    return found ? found->ping : 0.0f;
}

std::string NetHost::nameOf(int peer) const {
    const Peer* found = find(peer);
    return found ? found->name : std::string();
}

bool NetHost::poll(Event& event) {
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}

void NetHost::flush(Peer& peer, double now, bool mustSend) {
    float again = std::clamp(peer.ping * 1.5f + 0.03f, 0.08f, 0.6f);
    for (int packets = 0; packets < 12; packets++) {
        ByteWriter out;
        out.u32(kMagic);
        out.u8(kData);
        out.u16(peer.nextSequence);
        out.u16(peer.heard);
        out.u32(peer.heardBefore);
        out.u8(peer.hasHeard ? 1 : 0);
        size_t empty = out.bytes.size();
        Peer::Sent record;
        record.sequence = peer.nextSequence;
        record.at = now;
        // What must arrive goes first: what has not been sent, and what was sent long enough ago to have been answered.
        bool moreIsDue = false;
        for (Peer::Waiting& waiting : peer.reliable) {
            if (waiting.sentAt >= 0 && now - waiting.sentAt < double(again)) continue;
            if (out.bytes.size() + 5 + waiting.data.size() > kLargestPacket) {
                moreIsDue = true;
                continue;
            }
            out.u8(1);
            out.u16(waiting.id);
            out.u16(uint16_t(waiting.data.size()));
            out.bytes.insert(out.bytes.end(), waiting.data.begin(), waiting.data.end());
            waiting.sentAt = now;
            record.ids.push_back(waiting.id);
        }
        while (!peer.loose.empty() && out.bytes.size() + 3 + peer.loose.front().size() <= kLargestPacket) {
            out.u8(0);
            out.u16(uint16_t(peer.loose.front().size()));
            out.bytes.insert(out.bytes.end(), peer.loose.front().begin(), peer.loose.front().end());
            peer.loose.pop_front();
        }
        if (out.bytes.size() == empty && !mustSend) return;
        transport_->send(peer.address, out.bytes.data(), out.bytes.size());
        peer.nextSequence++;
        peer.lastSent = now;
        peer.owesAnswer = false;
        peer.sent.push_back(std::move(record));
        while (peer.sent.size() > 96) peer.sent.pop_front();
        mustSend = false;
        if (!moreIsDue && peer.loose.empty()) return;
    }
}

void NetHost::read(Peer& peer, const uint8_t* data, size_t size, double now) {
    ByteReader in(data, size);
    uint16_t sequence = in.u16(), answered = in.u16();
    uint32_t answeredBefore = in.u32();
    bool answers = in.u8() != 0;
    if (!in.isGood) return;

    // What it has heard of ours: those need not be sent again.
    if (answers) {
        for (size_t i = 0; i < peer.sent.size();) {
            const Peer::Sent& sent = peer.sent[i];
            uint16_t back = uint16_t(answered - sent.sequence);
            bool arrived = back == 0 || (back <= 32 && ((answeredBefore >> (back - 1)) & 1u));
            if (!arrived) {
                i++;
                continue;
            }
            if (back == 0) {
                float took = float(now - sent.at);
                peer.ping = peer.ping <= 0 ? took : peer.ping + (took - peer.ping) * 0.15f;
            }
            for (uint16_t id : sent.ids) {
                for (size_t w = 0; w < peer.reliable.size(); w++) {
                    if (peer.reliable[w].id == id) {
                        peer.reliable.erase(peer.reliable.begin() + long(w));
                        break;
                    }
                }
            }
            peer.sent.erase(peer.sent.begin() + long(i));
        }
    }

    // Is this packet one we have had already? Packets arrive twice, and late.
    bool isNew = false;
    if (!peer.hasHeard) {
        peer.hasHeard = true, peer.heard = sequence, peer.heardBefore = 0, isNew = true;
    } else {
        uint16_t ahead = uint16_t(sequence - peer.heard);
        if (ahead == 0) {
            isNew = false;
        } else if (ahead < 32768) {
            peer.heardBefore = ahead > 32 ? 0 : ahead == 32 ? (1u << 31) : ((peer.heardBefore << ahead) | (1u << (ahead - 1)));
            peer.heard = sequence;
            isNew = true;
        } else {
            uint16_t behind = uint16_t(peer.heard - sequence);
            if (behind <= 32 && !(peer.heardBefore & (1u << (behind - 1)))) {
                peer.heardBefore |= 1u << (behind - 1);
                isNew = true;
            }
        }
    }
    peer.lastHeard = now;
    if (!isNew) return;

    while (in.left() > 0 && in.isGood) {
        bool isReliable = in.u8() != 0;
        uint16_t id = isReliable ? in.u16() : uint16_t(0);
        size_t length = in.u16();
        std::vector<uint8_t> message = in.take(length);
        if (!in.isGood) break;
        auto tell = [&](std::vector<uint8_t>&& what, bool reliable) {
            Event event;
            event.kind = Event::Kind::Data;
            event.peer = peer.id;
            event.data = std::move(what);
            event.isReliable = reliable;
            events_.push_back(std::move(event));
        };
        if (!isReliable) {
            tell(std::move(message), false);
            continue;
        }
        peer.owesAnswer = true;
        uint16_t ahead = uint16_t(id - peer.expected);
        if (ahead >= 32768) continue;                           // had already
        if (ahead > 0) {
            // Before its turn: kept until what comes before it has come.
            if (peer.early.size() < 2048) peer.early.emplace(id, std::move(message));
            continue;
        }
        tell(std::move(message), true);
        peer.expected++;
        for (auto next = peer.early.find(peer.expected); next != peer.early.end(); next = peer.early.find(peer.expected)) {
            tell(std::move(next->second), true);
            peer.early.erase(next);
            peer.expected++;
        }
    }
}

void NetHost::receive(double now) {
    NetAddress from;
    std::vector<uint8_t> data;
    while (transport_ && transport_->receive(from, data)) {
        ByteReader in(data);
        if (in.u32() != kMagic) continue;
        uint8_t kind = in.u8();
        if (!in.isGood) continue;
        if (kind == kAsk && isListening_) {
            uint32_t token = in.u32();
            std::string name = in.text();
            if (!in.isGood) continue;
            Peer* known = findAt(from);
            if (known && known->token != token) {
                // The same machine asking anew: it stopped and started again.
                drop(known->id, "came again");
                known = nullptr;
            }
            if (!known) {
                if (int(peers_.size()) >= mostPeers_) {
                    ByteWriter no;
                    no.text("the game is full");
                    sendControl(from, kNo, no.bytes);
                    continue;
                }
                int id = 1;
                while (find(id)) id++;
                auto peer = std::make_unique<Peer>();
                peer->id = id;
                peer->address = from;
                peer->name = name;
                peer->token = token;
                peer->lastHeard = now;
                known = peer.get();
                peers_.push_back(std::move(peer));
                Event event;
                event.kind = Event::Kind::Joined;
                event.peer = id;
                event.name = name;
                events_.push_back(std::move(event));
            }
            ByteWriter yes;
            yes.u8(uint8_t(known->id));
            yes.u32(token);
            sendControl(from, kYes, yes.bytes);
        } else if (kind == kYes && isJoining_ && from == server_) {
            int id = in.u8();
            if (!in.isGood || in.u32() != token_) continue;
            isJoining_ = false;
            isJoined_ = true;
            id_ = id;
            auto server = std::make_unique<Peer>();
            server->id = kServer;
            server->address = server_;
            server->lastHeard = now;
            peers_.push_back(std::move(server));
            Event event;
            event.kind = Event::Kind::Joined;
            event.peer = kServer;
            events_.push_back(std::move(event));
        } else if (kind == kNo && isJoining_ && from == server_) {
            Event event;
            event.kind = Event::Kind::Left;
            event.peer = kServer;
            event.name = in.text();
            events_.push_back(std::move(event));
            close();
        } else if (kind == kData) {
            if (Peer* peer = findAt(from)) read(*peer, in.at, in.left(), now);
        } else if (kind == kBye) {
            if (Peer* peer = findAt(from)) drop(peer->id, "left");
        }
    }
}

void NetHost::update(double now) {
    now_ = now;
    if (!transport_) return;
    receive(now);
    if (isJoining_ && transport_) {
        if (joinStarted_ < 0) joinStarted_ = now;
        if (now - joinStarted_ > 5.0) {
            Event event;
            event.kind = Event::Kind::Left;
            event.peer = kServer;
            event.name = "no answer";
            events_.push_back(std::move(event));
            close();
            return;
        }
        if (lastAsked_ < 0 || now - lastAsked_ >= 0.25) {
            lastAsked_ = now;
            ByteWriter ask;
            ask.u32(token_);
            ask.text(name_);
            sendControl(server_, kAsk, ask.bytes);
        }
    }
    // Who has gone quiet is gone; everyone else is sent what is waiting, and a word now and then so that they know we are here.
    std::vector<int> quiet;
    for (const auto& peer : peers_) {
        if (now - peer->lastHeard > quietLimit) quiet.push_back(peer->id);
    }
    for (int id : quiet) drop(id, "no word from them");
    if (!transport_) return;
    for (const auto& peer : peers_) flush(*peer, now, peer->owesAnswer || peer->lastSent < 0 || now - peer->lastSent >= 0.25);
}

}  // namespace molten
