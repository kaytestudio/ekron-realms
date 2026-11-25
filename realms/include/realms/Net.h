// Ekron networking, the lowest layer: IPv4 addresses and a non-blocking UDP
// socket (Quake 2's net_udp.c / net_wins.c). The channel on top is
// realms/NetChannel.h; the game's snapshots over it are realms/GameNet.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace realms {

// Quake 2's PORT_SERVER, used by Ekron servers too.
constexpr uint16_t kNetDefaultPort = 27910;

struct NetAddress {
    uint32_t ip = 0;   // host byte order: 127.0.0.1 is 0x7f000001
    uint16_t port = 0; // host byte order

    bool operator==(const NetAddress &o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress &o) const { return !(*this == o); }
    bool isLoopback() const { return (ip >> 24) == 127; }
    // "a.b.c.d:port"
    std::string toString() const;
    // NET_StringToAdr: "host", "host:port", "a.b.c.d[:port]" or "localhost";
    // names are looked up (getaddrinfo, IPv4). `defaultPort` without one.
    static bool parse(const std::string &text, NetAddress &out, uint16_t defaultPort = kNetDefaultPort,
                      std::string *error = nullptr);
};

class UdpSocket {
public:
    // Datagrams larger than this are not received whole.
    static constexpr size_t kMaxDatagram = 65536;

    UdpSocket() = default;
    ~UdpSocket() { close(); }
    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;

    // Binds `port` (0: any free one) on every interface, or on 127.0.0.1
    // with `loopbackOnly`. False + error when it can't.
    bool open(uint16_t port, std::string &error, bool loopbackOnly = false);
    void close();
    bool isOpen() const { return handle_ != kInvalid; }
    // The bound port (the one picked for 0).
    uint16_t port() const { return port_; }
    // Never blocks; false when the datagram could not be sent.
    bool send(const NetAddress &to, const uint8_t *data, size_t size);
    // One waiting datagram into `buffer`: its size, or -1 when none is
    // waiting (or on an error; ICMP "port unreachable" replies are skipped).
    int receive(NetAddress &from, uint8_t *buffer, size_t capacity);

private:
#if defined(_WIN32)
    using Handle = uintptr_t; // SOCKET
#else
    using Handle = int;
#endif
    static constexpr Handle kInvalid = Handle(~Handle(0));
    Handle handle_ = kInvalid;
    uint16_t port_ = 0;
};

} // namespace realms
