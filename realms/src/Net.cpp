// Realms Engine - IPv4 addresses and UDP sockets (see Net.h).
#include "realms/Net.h"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace realms {

namespace {

#if defined(_WIN32)
// WSAStartup once for the process (WSACleanup is left to process exit).
bool winsockReady() {
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
int lastError() { return WSAGetLastError(); }
#else
bool winsockReady() { return true; }
int lastError() { return errno; }
#endif

sockaddr_in toSockaddr(const NetAddress &a) {
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(a.ip);
    sa.sin_port = htons(a.port);
    return sa;
}

// a.b.c.d, each part 0..255
bool parseDotted(const std::string &s, uint32_t &ip) {
    uint32_t v = 0;
    int parts = 0;
    size_t i = 0;
    while (parts < 4) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
        uint32_t n = 0;
        size_t digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 4) {
            n = n * 10 + uint32_t(s[i++] - '0');
            digits++;
        }
        if (n > 255) return false;
        v = (v << 8) | n;
        if (++parts < 4) {
            if (i >= s.size() || s[i] != '.') return false;
            i++;
        }
    }
    if (i != s.size()) return false;
    ip = v;
    return true;
}

} // namespace

std::string NetAddress::toString() const {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) +
           "." + std::to_string(ip & 255) + ":" + std::to_string(port);
}

bool NetAddress::parse(const std::string &text, NetAddress &out, uint16_t defaultPort, std::string *error) {
    auto fail = [&](const std::string &why) {
        if (error) *error = why;
        return false;
    };
    std::string host = text;
    uint16_t port = defaultPort;
    const size_t colon = text.rfind(':');
    if (colon != std::string::npos) {
        host = text.substr(0, colon);
        const std::string p = text.substr(colon + 1);
        if (p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos)
            return fail("bad port in " + text);
        const long n = std::strtol(p.c_str(), nullptr, 10);
        if (n <= 0 || n > 65535) return fail("bad port in " + text);
        port = uint16_t(n);
    }
    if (host.empty()) return fail("no address");
    uint32_t ip = 0;
    if (host == "localhost" || host == "loopback") {
        ip = 0x7f000001u;
    } else if (!parseDotted(host, ip)) {
        if (host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_") !=
            std::string::npos)
            return fail("bad address " + host);
        if (!winsockReady()) return fail("no network");
        addrinfo hints;
        std::memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo *res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return fail("unknown host " + host);
        bool found = false;
        for (addrinfo *r = res; r && !found; r = r->ai_next)
            if (r->ai_family == AF_INET && r->ai_addrlen >= sizeof(sockaddr_in)) {
                ip = ntohl(reinterpret_cast<const sockaddr_in *>(r->ai_addr)->sin_addr.s_addr);
                found = true;
            }
        freeaddrinfo(res);
        if (!found) return fail("unknown host " + host);
    }
    out.ip = ip;
    out.port = port;
    return true;
}

bool UdpSocket::open(uint16_t port, std::string &error, bool loopbackOnly) {
    close();
    if (!winsockReady()) {
        error = "WSAStartup failed";
        return false;
    }
    const auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        error = "socket: error " + std::to_string(lastError());
        return false;
    }
    handle_ = Handle(s);
#if defined(_WIN32)
    u_long nonBlocking = 1;
    const bool nb = ioctlsocket(s, FIONBIO, &nonBlocking) == 0;
    // no WSAECONNRESET from receive after an ICMP "port unreachable"
    BOOL noReset = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &noReset, sizeof noReset, nullptr, 0, &returned,
             nullptr, nullptr);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    const bool nb = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    if (!nb) {
        error = "can't make the socket non-blocking";
        close();
        return false;
    }
    NetAddress local;
    local.ip = loopbackOnly ? 0x7f000001u : 0u; // INADDR_ANY
    local.port = port;
    const sockaddr_in sa = toSockaddr(local);
    if (::bind(s, reinterpret_cast<const sockaddr *>(&sa), sizeof sa) != 0) {
        error = "can't bind UDP port " + std::to_string(port) + " (error " + std::to_string(lastError()) + ")";
        close();
        return false;
    }
    sockaddr_in bound;
    std::memset(&bound, 0, sizeof bound);
    socklen_t len = sizeof bound;
    port_ = ::getsockname(s, reinterpret_cast<sockaddr *>(&bound), &len) == 0 ? ntohs(bound.sin_port) : port;
    return true;
}

void UdpSocket::close() {
    if (handle_ == kInvalid) return;
#if defined(_WIN32)
    ::closesocket(SOCKET(handle_));
#else
    ::close(handle_);
#endif
    handle_ = kInvalid;
    port_ = 0;
}

bool UdpSocket::send(const NetAddress &to, const uint8_t *data, size_t size) {
    if (handle_ == kInvalid || size > kMaxDatagram) return false;
    const sockaddr_in sa = toSockaddr(to);
#if defined(_WIN32)
    const int n = ::sendto(SOCKET(handle_), reinterpret_cast<const char *>(data), int(size), 0,
                           reinterpret_cast<const sockaddr *>(&sa), sizeof sa);
#else
    const ssize_t n = ::sendto(handle_, data, size, 0, reinterpret_cast<const sockaddr *>(&sa), sizeof sa);
#endif
    return n >= 0 && size_t(n) == size;
}

int UdpSocket::receive(NetAddress &from, uint8_t *buffer, size_t capacity) {
    if (handle_ == kInvalid) return -1;
    for (int tries = 0; tries < 64; tries++) {
        sockaddr_in sa;
        std::memset(&sa, 0, sizeof sa);
        socklen_t len = sizeof sa;
#if defined(_WIN32)
        const int n = ::recvfrom(SOCKET(handle_), reinterpret_cast<char *>(buffer), int(capacity), 0,
                                 reinterpret_cast<sockaddr *>(&sa), &len);
        if (n < 0) {
            const int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;
            return -1;
        }
#else
        const ssize_t n = ::recvfrom(handle_, buffer, capacity, 0, reinterpret_cast<sockaddr *>(&sa), &len);
        if (n < 0) {
            if (errno == ECONNREFUSED || errno == EINTR) continue;
            return -1;
        }
#endif
        from.ip = ntohl(sa.sin_addr.s_addr);
        from.port = ntohs(sa.sin_port);
        return int(n);
    }
    return -1;
}

} // namespace realms
