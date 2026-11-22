// UDP between machines, with the sockets every Unix has: macOS, iOS, Linux.
#include "Net/Net.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace molten {

namespace {

class UdpTransport : public NetTransport {
public:
    UdpTransport(int socket, NetAddress address) : socket_(socket), address_(address) {}
    ~UdpTransport() override { ::close(socket_); }

    bool send(const NetAddress& to, const uint8_t* data, size_t size) override {
        sockaddr_in there{};
        there.sin_family = AF_INET;
        there.sin_addr.s_addr = htonl(to.ip);
        there.sin_port = htons(to.port);
        return ::sendto(socket_, data, size, 0, reinterpret_cast<const sockaddr*>(&there), sizeof(there)) == ssize_t(size);
    }
    bool receive(NetAddress& from, std::vector<uint8_t>& data) override {
        uint8_t buffer[2048];
        sockaddr_in there{};
        socklen_t length = sizeof(there);
        ssize_t got = ::recvfrom(socket_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&there), &length);
        if (got <= 0) return false;             // nothing waiting (it never blocks), or an error that the next try may not have
        from.ip = ntohl(there.sin_addr.s_addr);
        from.port = ntohs(there.sin_port);
        data.assign(buffer, buffer + got);
        return true;
    }
    NetAddress address() const override { return address_; }

private:
    int socket_;
    NetAddress address_;
};

}  // namespace

std::unique_ptr<NetTransport> openUdp(uint16_t port, bool onlyThisMachine, std::string* error) {
    auto fail = [&](const char* what) {
        if (error) *error = std::string(what) + ": " + std::strerror(errno);
        return std::unique_ptr<NetTransport>();
    };
    int made = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (made < 0) return fail("cannot open a socket");
    sockaddr_in here{};
    here.sin_family = AF_INET;
    here.sin_addr.s_addr = htonl(onlyThisMachine ? INADDR_LOOPBACK : INADDR_ANY);
    here.sin_port = htons(port);
    if (::bind(made, reinterpret_cast<const sockaddr*>(&here), sizeof(here)) != 0) {
        auto failed = fail("cannot listen on that port");
        ::close(made);
        return failed;
    }
    // Asked for what has arrived, it answers at once whether or not anything has.
    int flags = ::fcntl(made, F_GETFL, 0);
    if (flags < 0 || ::fcntl(made, F_SETFL, flags | O_NONBLOCK) != 0) {
        auto failed = fail("cannot set the socket up");
        ::close(made);
        return failed;
    }
    socklen_t length = sizeof(here);
    ::getsockname(made, reinterpret_cast<sockaddr*>(&here), &length);
    NetAddress address{ntohl(here.sin_addr.s_addr), ntohs(here.sin_port)};
    if (address.ip == 0) address.ip = 0x7F000001u;     // heard on every address of this machine: this one is always one of them
    return std::make_unique<UdpTransport>(made, address);
}

std::optional<NetAddress> resolveAddress(const std::string& host, uint16_t port) {
    if (std::optional<NetAddress> numbers = NetAddress::parse(host, port)) return numbers;
    // "name:port" is a name and a port.
    std::string name = host;
    size_t colon = name.rfind(':');
    if (colon != std::string::npos) {
        int given = std::atoi(name.c_str() + colon + 1);
        if (given > 0 && given < 65536) port = uint16_t(given);
        name.erase(colon);
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (::getaddrinfo(name.c_str(), nullptr, &hints, &found) != 0 || !found) return std::nullopt;
    NetAddress address{ntohl(reinterpret_cast<const sockaddr_in*>(found->ai_addr)->sin_addr.s_addr), port};
    ::freeaddrinfo(found);
    return address;
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
    ifaddrs* all = nullptr;
    if (::getifaddrs(&all) != 0) return out;
    for (const ifaddrs* one = all; one; one = one->ifa_next) {
        if (!one->ifa_addr || one->ifa_addr->sa_family != AF_INET) continue;
        if (!(one->ifa_flags & IFF_UP) || (one->ifa_flags & IFF_LOOPBACK)) continue;
        uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(one->ifa_addr)->sin_addr.s_addr);
        std::string text = NetAddress{ip, 0}.toString();
        out.push_back(text.substr(0, text.rfind(':')));
    }
    ::freeifaddrs(all);
    return out;
}

}  // namespace molten
