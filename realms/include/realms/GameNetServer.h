// Ekron's game server on the network (Quake 2's sv_main.c / sv_user.c
// network half): a UDP socket (realms/Net.h), the connect handshake, and
// one NetChannel (realms/NetChannel.h) with a GameNetEncoder
// (realms/GameNet.h) for each remote client. The game itself is
// GameServer (realms/GameServer.h), seen through GameNetServerGame; the
// local player stays client 0 of a listen server (Quake2Module).
//
// Handshake (connectionless packets, lines of text):
//   client: "getchallenge"
//   server: "challenge\n<number>"
//   client: "connect\n<protocol>\n<qport>\n<challenge>\n<userinfo>"
//           (userinfo "\name\Player\fov\90\hand\0\skin\male/grunt\gender\male")
//   server: "client_connect\n<map path>\n<map checksum>" or
//           "print\n<why not>" (wrong protocol or challenge, server full)
// The client loads the map, then sends kGameNetBegin over the channel and
// enters the game (GameServer::connectClient); its snapshots follow.
// "info" gets "info\n<protocol>\n<map path>\n<players>\n<max players>".
#pragma once

#include "realms/GameNet.h"
#include "realms/GameServer.h"
#include "realms/Net.h"
#include "realms/NetChannel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace realms {

// What GameNetServer needs from the game (client indices as in
// GameServer). GameServerNetGame is the real one; tests use their own.
class GameNetServerGame {
public:
    virtual ~GameNetServerGame() = default;
    virtual int maxClients() const = 0;
    // -1 + error when the game refuses the client.
    virtual int connectClient(const std::string &name, float fov, int hand, const std::string &skin,
                              const std::string &gender, const std::string &ip, std::string &error) = 0;
    virtual void disconnectClient(int client) = 0;
    virtual void clientMove(int client, const GameUserCmd &cmd, double seconds) = 0;
    virtual void clientCommand(int client, const std::string &line) = 0;
    virtual void setFov(int client, float fov) = 0;
    virtual void setHand(int client, int hand) = 0;
    virtual void setSkin(int client, const std::string &skin) = 0;
    virtual void setGender(int client, const std::string &gender) = 0;
    virtual void snapshot(int client, GameSnapshot &out, bool full) = 0;
};

class GameServerNetGame : public GameNetServerGame {
public:
    explicit GameServerNetGame(GameServer &server) : server_(server) {}
    int maxClients() const override { return server_.maxClients(); }
    int connectClient(const std::string &name, float fov, int hand, const std::string &skin, const std::string &gender,
                      const std::string &ip, std::string &error) override {
        return server_.connectClient(name, fov, hand, error, ip, skin, gender);
    }
    void disconnectClient(int client) override { server_.disconnectClient(client); }
    void clientMove(int client, const GameUserCmd &cmd, double seconds) override {
        server_.clientMove(client, cmd, seconds);
    }
    void clientCommand(int client, const std::string &line) override { server_.clientCommand(client, line); }
    void setFov(int client, float fov) override { server_.setFov(client, fov); }
    void setHand(int client, int hand) override { server_.setHand(client, hand); }
    void setSkin(int client, const std::string &skin) override { server_.setSkin(client, skin); }
    void setGender(int client, const std::string &gender) override { server_.setGender(client, gender); }
    void snapshot(int client, GameSnapshot &out, bool full) override { server_.snapshot(client, out, full); }

private:
    GameServer &server_;
};

class GameNetServer {
public:
    // A client silent this long is dropped (loading a map included).
    static constexpr double kTimeoutSeconds = 30.0;
    // Snapshots to a client at most this often (the rest of the frames'
    // events wait in GameServer's queue for the next one).
    static constexpr double kSendInterval = 1.0 / 60.0;
    // Messages to a client still loading the map (keeps its channel going).
    static constexpr double kKeepaliveInterval = 1.0;

    GameNetServer();
    ~GameNetServer();
    GameNetServer(const GameNetServer &) = delete;
    GameNetServer &operator=(const GameNetServer &) = delete;

    // Binds UDP `port` (0: any free one; loopbackOnly: 127.0.0.1 only).
    bool open(uint16_t port, std::string &error, bool loopbackOnly = false);
    // Tells every client `command` (kGameNetServerCommand: "reconnect", or
    // "disconnect\n<reason>"), a few times, then closes. The game's clients
    // are not touched (the game is going away). Nothing to tell: "".
    void close(const std::string &command = "disconnect\nServer quit");
    bool isOpen() const { return socket_.isOpen(); }
    uint16_t port() const { return socket_.port(); }
    // The map clients are told to load.
    void setMap(const std::string &path, uint32_t checksum);

    // Every waiting datagram: handshakes, then each client's commands and
    // usercmds for `game`; clients silent for kTimeoutSeconds are dropped.
    // Before the game's frame.
    void receive(GameNetServerGame &game, double now);
    // Each client's snapshot (or a keepalive while it loads). After the
    // game's frame.
    void send(GameNetServerGame &game, double now);

    // Remote clients: connected (loading the map) or in the game.
    int clientCount() const;
    int playingCount() const;
    // Console lines ("Player connected from 1.2.3.4:27901", "Player timed
    // out"...); the owner prints them.
    std::function<void(const std::string &)> print;

private:
    struct Remote;
    struct Challenge {
        NetAddress address;
        uint32_t number = 0;
        double time = 0.0;
    };

    void connectionless(GameNetServerGame &game, const NetAddress &from, const std::string &text, double now);
    void reply(const NetAddress &to, const std::string &text);
    void message(GameNetServerGame &game, Remote &r, const NetMessage &m, double now);
    void transmit(Remote &r, const std::vector<uint8_t> &unreliable, double now);
    // Tells the client `command`, then forgets it (and takes it out of
    // the game when it is in).
    void drop(GameNetServerGame &game, Remote &r, const std::string &command, const std::string &why);
    void removeDropped();
    void say(const std::string &text);

    UdpSocket socket_;
    std::vector<std::unique_ptr<Remote>> remotes_;
    std::vector<Challenge> challenges_;
    std::string mapPath_;
    uint32_t mapChecksum_ = 0;
    std::mt19937 random_;
    std::vector<uint8_t> buffer_;
};

} // namespace realms
