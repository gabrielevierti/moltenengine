// Packets between machines: what must arrive does, once and in order, on a line that loses
// one packet in three; who joins is numbered, who goes quiet is gone.
#include "Test.hpp"

#include "Net/Net.hpp"

#include <cstring>

using namespace molten;

namespace {

std::vector<uint8_t> bytesOf(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }
std::string textOf(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

/// Runs every host for `seconds`, sixty times a second, and keeps what they are told.
struct Line {
    MemoryNetwork network;
    std::vector<NetHost*> hosts;
    std::vector<std::vector<NetHost::Event>> told;
    double now = 0;

    void add(NetHost& host) {
        hosts.push_back(&host);
        told.emplace_back();
    }
    void run(double seconds) {
        for (int i = 0; i < int(seconds * 60 + 0.5); i++) {
            now += 1.0 / 60.0;
            network.advance(1.0 / 60.0);
            for (size_t h = 0; h < hosts.size(); h++) {
                hosts[h]->update(now);
                NetHost::Event event;
                while (hosts[h]->poll(event)) told[h].push_back(event);
            }
        }
    }
    int count(size_t host, NetHost::Event::Kind kind) const {
        int found = 0;
        for (const NetHost::Event& event : told[host]) found += event.kind == kind ? 1 : 0;
        return found;
    }
};

}  // namespace

TEST(net_addresses_are_read_and_written) {
    std::optional<NetAddress> address = NetAddress::parse("192.168.1.20:7777");
    CHECK(address && address->ip == 0xC0A80114u && address->port == 7777);
    CHECK(address->toString() == "192.168.1.20:7777");
    CHECK(NetAddress::parse("10.0.0.5", 9000)->port == 9000);
    CHECK(!NetAddress::parse("10.0.0") && !NetAddress::parse("300.1.1.1") && !NetAddress::parse("mac-mini.local") && !NetAddress::parse("1.2.3.4:"));
    // What is written is read back the same, and a message cut short is noticed.
    ByteWriter out;
    out.u8(7), out.u16(65000), out.u32(123456789u), out.f32(-2.5f), out.unit(0.5f), out.text("ciao");
    ByteReader in(out.bytes);
    CHECK(in.u8() == 7 && in.u16() == 65000 && in.u32() == 123456789u && in.f32() == -2.5f);
    CHECK_NEAR(in.unit(), 0.5f, 0.0001f);
    CHECK(in.text() == "ciao" && in.isGood && in.left() == 0);
    ByteReader cut(out.bytes.data(), out.bytes.size() - 2);
    cut.u8(), cut.u16(), cut.u32(), cut.f32(), cut.unit();
    CHECK(cut.text().empty() && !cut.isGood);
    CHECK(cut.u32() == 0);
}

TEST(net_peers_join_and_talk_both_ways) {
    Line line;
    NetHost server, anna, bruno;
    line.add(server), line.add(anna), line.add(bruno);
    server.listen(line.network.open(7777), 4);
    anna.join(line.network.open(5001), server.address(), "Anna");
    line.run(0.2);
    bruno.join(line.network.open(5002), server.address(), "Bruno");
    line.run(0.3);
    // Numbered as they came; each knows its number, and the server their names.
    CHECK(server.isListening() && server.id() == 0);
    CHECK(anna.isJoined() && anna.id() == 1 && bruno.isJoined() && bruno.id() == 2);
    CHECK(server.peers().size() == 2 && server.nameOf(1) == "Anna" && server.nameOf(2) == "Bruno");
    CHECK(line.count(0, NetHost::Event::Kind::Joined) == 2 && line.count(1, NetHost::Event::Kind::Joined) == 1);
    CHECK(anna.peers().size() == 1 && anna.peers()[0] == 0);

    CHECK(anna.send(0, bytesOf("from anna"), true));
    CHECK(server.send(2, bytesOf("to bruno"), true));
    server.sendToAll(bytesOf("to all"), false);
    server.sendToAll(bytesOf("not to anna"), true, 1);
    line.run(0.2);
    auto heard = [&](size_t host, const std::string& text) {
        int times = 0;
        for (const NetHost::Event& event : line.told[host]) times += event.kind == NetHost::Event::Kind::Data && textOf(event.data) == text ? 1 : 0;
        return times;
    };
    CHECK(heard(0, "from anna") == 1);
    CHECK(heard(2, "to bruno") == 1 && heard(1, "to bruno") == 0);
    CHECK(heard(1, "to all") == 1 && heard(2, "to all") == 1);
    CHECK(heard(1, "not to anna") == 0 && heard(2, "not to anna") == 1);
    // Too large for one message, or to no one: not sent.
    CHECK(!server.send(1, std::vector<uint8_t>(NetHost::kLargestMessage + 1), true));
    CHECK(!server.send(9, bytesOf("x"), true));
    CHECK(!anna.send(2, bytesOf("x"), true));                    // those who joined talk to the server only
    // How long a word takes there and back is known, near enough.
    CHECK(server.ping(1) >= 0 && server.ping(1) < 0.1f);

    // Anna says goodbye: the server knows at once. Bruno just stops: it knows when he has been quiet long enough.
    anna.close();
    line.run(0.1);
    CHECK(server.peers().size() == 1 && line.count(0, NetHost::Event::Kind::Left) == 1);
    server.quietLimit = 1.0;
    line.hosts.pop_back();                                       // Bruno is no longer run
    line.run(1.5);
    CHECK(server.peers().empty() && line.count(0, NetHost::Event::Kind::Left) == 2);
}

TEST(net_what_must_arrive_does_once_and_in_order_on_a_bad_line) {
    Line line;
    line.network.loss = 0.3f;
    line.network.delay = 0.04f;
    line.network.jitter = 0.05f;                                 // later packets overtake earlier ones
    line.network.seed = 77;
    NetHost server, client;
    line.add(server), line.add(client);
    server.listen(line.network.open(7777), 2);
    client.join(line.network.open(5001), server.address(), "Carla");
    line.run(2.0);
    CHECK(client.isJoined());
    if (!client.isJoined()) return;
    // Three hundred each way that must arrive, a few a frame, and as many that need not.
    int sentBoth = 0;
    for (int frame = 0; frame < 100; frame++) {
        for (int k = 0; k < 3; k++, sentBoth++) {
            CHECK(client.send(0, bytesOf("up " + std::to_string(sentBoth)), true));
            CHECK(server.send(1, bytesOf("down " + std::to_string(sentBoth)), true));
            client.send(0, bytesOf("maybe " + std::to_string(sentBoth)), false);
        }
        line.run(1.0 / 60.0);
    }
    line.run(6.0);
    auto inOrder = [&](size_t host, const std::string& word, int& maybes) {
        int next = 0;
        bool ordered = true;
        std::vector<int> seenMaybe(size_t(sentBoth), 0);
        for (const NetHost::Event& event : line.told[host]) {
            if (event.kind != NetHost::Event::Kind::Data) continue;
            std::string text = textOf(event.data);
            if (text.rfind("maybe ", 0) == 0) {
                seenMaybe[size_t(std::stoi(text.substr(6)))]++;
                continue;
            }
            ordered = ordered && text == word + " " + std::to_string(next);
            next++;
        }
        maybes = 0;
        for (int times : seenMaybe) {
            ordered = ordered && times <= 1;                    // never twice
            maybes += times;
        }
        return ordered ? next : -1;
    };
    int maybes = 0, none = 0;
    CHECK(inOrder(0, "up", maybes) == 300);
    CHECK(maybes > 120 && maybes < 290);                         // about seven in ten of those that need not
    CHECK(inOrder(1, "down", none) == 300);
    std::printf("    (bad line: %d packets sent, %d lost)\n", line.network.sent(), line.network.lost());
    CHECK(line.network.lost() > line.network.sent() / 5);
    CHECK(server.peers().size() == 1 && client.isJoined());      // and no one took the other for gone
}

TEST(net_a_full_game_says_no_and_a_silent_one_is_given_up) {
    Line line;
    NetHost server, first, second, nobody;
    line.add(server), line.add(first), line.add(second), line.add(nobody);
    server.listen(line.network.open(7777), 1);
    first.join(line.network.open(5001), server.address(), "One");
    line.run(0.2);
    second.join(line.network.open(5002), server.address(), "Two");
    nobody.join(line.network.open(5003), NetAddress{0x0A0000FEu, 1}, "Lost");       // where no one listens
    line.run(0.5);
    CHECK(first.isJoined() && !second.isJoined() && !second.isJoining());
    bool wasTold = false;
    for (const NetHost::Event& event : line.told[2]) wasTold = wasTold || (event.kind == NetHost::Event::Kind::Left && event.name == "the game is full");
    CHECK(wasTold);
    CHECK(nobody.isJoining());
    line.run(5.5);
    CHECK(!nobody.isJoining() && !nobody.isOpen() && line.count(3, NetHost::Event::Kind::Left) == 1);
    // The one who was in stops and joins again from the same place: the old one is gone, the new one is in.
    NetAddress from = first.address();
    (void)from;
    first.close();
    line.run(0.1);
    first.join(line.network.open(5001), server.address(), "One again");
    line.run(0.5);
    CHECK(first.isJoined() && server.peers().size() == 1 && server.nameOf(first.id()) == "One again");
}

TEST(net_udp_carries_a_game_between_two_ends_of_this_machine) {
    std::string error;
    std::unique_ptr<NetTransport> listening = openUdp(0, true, &error);
    if (!listening) {
        std::printf("    (no sockets here: %s)\n", error.c_str());
        return;
    }
    NetAddress where = listening->address();
    CHECK(where.port != 0 && where.ip == 0x7F000001u);
    NetHost server, client;
    server.listen(std::move(listening), 2);
    client.join(openUdp(0, true), where, "Dario");
    std::vector<NetHost::Event> serverTold, clientTold;
    double now = 0;
    auto run = [&](int frames) {
        for (int i = 0; i < frames; i++) {
            now += 1.0 / 60.0;
            server.update(now), client.update(now);
            NetHost::Event event;
            while (server.poll(event)) serverTold.push_back(event);
            while (client.poll(event)) clientTold.push_back(event);
        }
    };
    run(30);
    CHECK(client.isJoined() && server.peers().size() == 1);
    for (int i = 0; i < 50; i++) client.send(0, bytesOf("n" + std::to_string(i)), true);
    server.send(1, bytesOf("welcome"), true);
    run(30);
    int next = 0;
    for (const NetHost::Event& event : serverTold) {
        if (event.kind == NetHost::Event::Kind::Data && textOf(event.data) == "n" + std::to_string(next)) next++;
    }
    CHECK(next == 50);
    bool welcomed = false;
    for (const NetHost::Event& event : clientTold) welcomed = welcomed || textOf(event.data) == "welcome";
    CHECK(welcomed);
    // A name is looked up, and numbers are numbers.
    std::optional<NetAddress> local = resolveAddress("127.0.0.1:9", 7777);
    CHECK(local && local->ip == 0x7F000001u && local->port == 9);
}
