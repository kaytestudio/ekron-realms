// Realms Engine - the game server on the network (see GameNetServer.h).
#include "realms/GameNetServer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace realms {

namespace {

std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find('\n', at);
        out.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return out;
}

std::vector<uint8_t> typed(uint8_t type, const std::string &text) {
    std::vector<uint8_t> m(1 + text.size(), type);
    std::copy(text.begin(), text.end(), m.begin() + 1);
    return m;
}

std::string ipString(const NetAddress &a) {
    const std::string s = a.toString();
    return s.substr(0, s.rfind(':'));
}

} // namespace

struct GameNetServer::Remote {
    NetAddress address;
    uint16_t qport = 0;
    NetChannel channel;
    GameNetEncoder encoder;
    std::string name = "Player";
    float fov = 90.0f;
    int hand = 0;
    std::string skin = GameServer::kDefaultSkin, gender = GameServer::kDefaultGender;
    int client = -1;   // the game's client index once it has begun
    bool full = true;  // the next snapshot sends everything
    uint32_t lastCmd = 0;
    double connected = 0.0, lastReceived = 0.0, lastSent = -1e9;
    bool dropped = false;
};

GameNetServer::GameNetServer()
    : random_(uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ std::random_device{}()),
      buffer_(UdpSocket::kMaxDatagram) {}

GameNetServer::~GameNetServer() { close(""); }

bool GameNetServer::open(uint16_t port, std::string &error, bool loopbackOnly) {
    close("");
    return socket_.open(port, error, loopbackOnly);
}

void GameNetServer::close(const std::string &command) {
    for (auto &r : remotes_) {
        if (r->dropped || command.empty() || !socket_.isOpen()) continue;
        r->channel.sendReliable(typed(kGameNetServerCommand, command));
        for (int i = 0; i < 3; i++) transmit(*r, {}, r->lastSent);
    }
    remotes_.clear();
    challenges_.clear();
    socket_.close();
}

void GameNetServer::setMap(const std::string &path, uint32_t checksum) {
    mapPath_ = path;
    mapChecksum_ = checksum;
}

int GameNetServer::clientCount() const {
    return int(std::count_if(remotes_.begin(), remotes_.end(), [](const auto &r) { return !r->dropped; }));
}

int GameNetServer::playingCount() const {
    return int(std::count_if(remotes_.begin(), remotes_.end(),
                             [](const auto &r) { return !r->dropped && r->client >= 0; }));
}

void GameNetServer::say(const std::string &text) {
    if (print && !text.empty()) print(text);
}

void GameNetServer::reply(const NetAddress &to, const std::string &text) {
    const std::vector<uint8_t> p = NetChannel::connectionless(text);
    socket_.send(to, p.data(), p.size());
}

void GameNetServer::transmit(Remote &r, const std::vector<uint8_t> &unreliable, double now) {
    std::vector<std::vector<uint8_t>> datagrams;
    r.channel.transmit(unreliable, datagrams, now);
    for (const auto &d : datagrams) socket_.send(r.address, d.data(), d.size());
    r.lastSent = now;
}

void GameNetServer::drop(GameNetServerGame &game, Remote &r, const std::string &command, const std::string &why) {
    if (r.dropped) return;
    if (!command.empty()) {
        r.channel.sendReliable(typed(kGameNetServerCommand, command));
        for (int i = 0; i < 2; i++) transmit(r, {}, r.lastSent);
    }
    r.dropped = true;
    if (r.client > 0) game.disconnectClient(r.client); // its "<name> disconnected"
    r.client = -1;
    say(why);
}

void GameNetServer::removeDropped() {
    remotes_.erase(std::remove_if(remotes_.begin(), remotes_.end(), [](const auto &r) { return r->dropped; }),
                   remotes_.end());
}

void GameNetServer::receive(GameNetServerGame &game, double now) {
    if (!socket_.isOpen()) return;
    for (;;) {
        NetAddress from;
        const int n = socket_.receive(from, buffer_.data(), buffer_.size());
        if (n < 0) break;
        std::string text;
        if (NetChannel::parseConnectionless(buffer_.data(), size_t(n), text)) {
            connectionless(game, from, text, now);
            continue;
        }
        uint16_t qport = 0;
        if (!NetChannel::peekQport(buffer_.data(), size_t(n), qport)) continue;
        // SV_ReadPackets: the client by its address and qport (a NAT may
        // have given it another port since)
        Remote *r = nullptr;
        for (auto &c : remotes_)
            if (!c->dropped && c->address.ip == from.ip && c->qport == qport) r = c.get();
        if (!r) continue;
        NetMessage m;
        if (!r->channel.process(buffer_.data(), size_t(n), m, now)) continue;
        if (r->address.port != from.port) r->address.port = from.port;
        r->lastReceived = now;
        message(game, *r, m, now);
    }
    for (auto &r : remotes_)
        if (!r->dropped && now - r->lastReceived > kTimeoutSeconds)
            drop(game, *r, "disconnect\nTimed out", r->name + " timed out.");
    removeDropped();
}

void GameNetServer::connectionless(GameNetServerGame &game, const NetAddress &from, const std::string &text,
                                   double now) {
    const std::vector<std::string> l = lines(text);
    const std::string &command = l[0];
    if (command == "getchallenge") {
        // SVC_GetChallenge: one number per address, the oldest replaced
        auto found = std::find_if(challenges_.begin(), challenges_.end(),
                                  [&](const Challenge &c) { return c.address == from; });
        if (found == challenges_.end()) {
            if (challenges_.size() >= 64)
                challenges_.erase(std::min_element(challenges_.begin(), challenges_.end(),
                                                   [](const Challenge &a, const Challenge &b) { return a.time < b.time; }));
            Challenge c;
            c.address = from;
            c.number = random_() & 0x7fffffff;
            challenges_.push_back(c);
            found = challenges_.end() - 1;
        }
        found->time = now;
        reply(from, "challenge\n" + std::to_string(found->number));
    } else if (command == "info") {
        reply(from, "info\n" + std::to_string(kGameNetProtocol) + "\n" + mapPath_ + "\n" +
                        std::to_string(1 + clientCount()) + "\n" + std::to_string(game.maxClients()));
    } else if (command == "connect") {
        // SV_DirectConnect
        if (l.size() < 4) return;
        const int protocol = std::atoi(l[1].c_str());
        if (protocol != kGameNetProtocol) {
            reply(from, "print\nServer is protocol " + std::to_string(kGameNetProtocol) + ", not " +
                            std::to_string(protocol) + ".");
            return;
        }
        const uint16_t qport = uint16_t(std::strtoul(l[2].c_str(), nullptr, 10));
        const uint32_t number = uint32_t(std::strtoul(l[3].c_str(), nullptr, 10));
        const std::string userinfo = l.size() > 4 ? l[4] : std::string();
        auto challenge = std::find_if(challenges_.begin(), challenges_.end(),
                                      [&](const Challenge &c) { return c.address == from; });
        if (challenge == challenges_.end() || challenge->number != number) {
            reply(from, "print\nBad challenge.");
            return;
        }
        const std::string connectReply = "client_connect\n" + mapPath_ + "\n" + std::to_string(mapChecksum_);
        for (auto &r : remotes_) {
            if (r->dropped || r->address.ip != from.ip || r->qport != qport) continue;
            if (r->address.port == from.port && (r->client < 0 || now - r->connected < 3.0)) {
                reply(from, connectReply); // a resent connect: the same answer
                return;
            }
            drop(game, *r, "", ""); // the client started over
        }
        removeDropped();
        if (mapPath_.empty()) {
            reply(from, "print\nNo map is running.");
            return;
        }
        if (1 + clientCount() >= game.maxClients()) {
            reply(from, "print\nServer is full.");
            return;
        }
        auto r = std::make_unique<Remote>();
        r->address = from;
        r->qport = qport;
        r->channel.reset(qport);
        const std::string name = gameNetInfoValue(userinfo, "name");
        if (!name.empty()) r->name = name.substr(0, 32);
        const std::string fov = gameNetInfoValue(userinfo, "fov");
        if (!fov.empty()) r->fov = float(std::atof(fov.c_str()));
        if (!std::isfinite(r->fov)) r->fov = 90.0f;
        r->hand = GameServer::clampHand(std::atoi(gameNetInfoValue(userinfo, "hand").c_str()));
        // left out: Quake 2 would have no key, and the game draws "male" / says "itself"
        r->skin = gameNetInfoValue(userinfo, "skin").substr(0, 63);
        r->gender = gameNetInfoValue(userinfo, "gender").substr(0, 15);
        r->connected = r->lastReceived = now;
        say(r->name + " connected from " + from.toString() + ".");
        remotes_.push_back(std::move(r));
        challenges_.erase(challenge);
        reply(from, connectReply);
    }
}

void GameNetServer::message(GameNetServerGame &game, Remote &r, const NetMessage &m, double now) {
    for (const std::vector<uint8_t> &msg : m.reliable) {
        if (msg.empty() || r.dropped) continue;
        std::string text(msg.begin() + 1, msg.end());
        switch (msg[0]) {
        case kGameNetBegin: // SV_Begin_f; again: everything again (the client started over)
            if (r.client < 0) {
                std::string error;
                const int client = game.connectClient(r.name, r.fov, r.hand, r.skin, r.gender, ipString(r.address), error);
                if (client < 1) {
                    if (error.empty()) error = "Server is full.";
                    drop(game, r, "disconnect\n" + error, r.name + " was refused: " + error);
                    return;
                }
                r.client = client;
            }
            r.full = true;
            r.encoder.reset();
            r.channel.sendReliable(typed(kGameNetServerCommand, "playernum " + std::to_string(r.client)));
            break;
        case kGameNetStringCmd:
            if (r.client < 0 || text.size() > 1024) break;
            std::replace(text.begin(), text.end(), '\n', ' ');
            std::replace(text.begin(), text.end(), '\r', ' ');
            game.clientCommand(r.client, text);
            break;
        case kGameNetUserinfo: {
            const std::string fov = gameNetInfoValue(text, "fov");
            if (!fov.empty()) {
                const float f = float(std::atof(fov.c_str()));
                r.fov = std::isfinite(f) ? f : 90.0f;
                if (r.client >= 0) game.setFov(r.client, r.fov);
            }
            const std::string hand = gameNetInfoValue(text, "hand");
            if (!hand.empty()) {
                r.hand = GameServer::clampHand(std::atoi(hand.c_str()));
                if (r.client >= 0) game.setHand(r.client, r.hand);
            }
            // CL_SendCmd sends the whole userinfo; Ekron sends what changed
            if (gameNetInfoHas(text, "skin")) {
                r.skin = gameNetInfoValue(text, "skin").substr(0, 63);
                if (r.client >= 0) game.setSkin(r.client, r.skin);
            }
            if (gameNetInfoHas(text, "gender")) {
                r.gender = gameNetInfoValue(text, "gender").substr(0, 15);
                if (r.client >= 0) game.setGender(r.client, r.gender);
            }
            break;
        }
        case kGameNetDisconnect:
            drop(game, r, "", r.client < 0 ? r.name + " disconnected." : std::string());
            return;
        default:
            break;
        }
    }
    std::vector<GameNetUserCmd> cmds;
    if (r.dropped || m.unreliable.empty() || !decodeGameNetUserCmds(m.unreliable.data(), m.unreliable.size(), cmds))
        return;
    for (const GameNetUserCmd &c : cmds) {
        if (c.number <= r.lastCmd) continue; // applied from an earlier message
        r.lastCmd = c.number;
        if (r.client >= 0) game.clientMove(r.client, c.cmd, std::clamp(double(c.seconds), 0.0, 1.0));
    }
    (void)now;
}

void GameNetServer::send(GameNetServerGame &game, double now) {
    if (!socket_.isOpen()) return;
    std::vector<uint8_t> unreliable, reliable;
    for (auto &rp : remotes_) {
        Remote &r = *rp;
        if (r.dropped) continue;
        if (r.client < 0) { // loading the map: keepalives (and the reliable messages)
            if (now - r.lastSent >= kKeepaliveInterval) transmit(r, {}, now);
            continue;
        }
        if (now - r.lastSent < kSendInterval) continue;
        GameSnapshot snapshot;
        game.snapshot(r.client, snapshot, r.full);
        r.full = false;
        r.encoder.encode(snapshot, r.channel.outgoingSequence() + 1, r.channel.incomingAcknowledged(), unreliable,
                         reliable);
        if (!reliable.empty() && !r.channel.sendReliable(reliable)) {
            drop(game, r, "", r.name + " overflowed.");
            continue;
        }
        transmit(r, unreliable, now);
    }
    removeDropped();
}

} // namespace realms
