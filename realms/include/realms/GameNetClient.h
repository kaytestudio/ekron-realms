// Ekron's game client on the network (Quake 2's cl_main.c / cl_parse.c
// network half): the connect handshake with a GameNetServer
// (realms/GameNetServer.h has the packets), then a NetChannel to it with
// a GameNetDecoder (realms/GameNet.h) for the snapshots. One per process,
// like Quake 2's cls (instance()).
//
// connect -> Challenging -> Connecting -> Loading: the server's map is to
// be loaded (takeMapToLoad; the console's "map" does it) -> Active once
// begin() is sent (Quake2Module, when the map is open): frame() sends the
// usercmds and hands out the snapshots for GameClient::apply. The server's
// "reconnect" (it changes maps) starts the handshake over; "disconnect",
// timeouts and a refused connect end it (takeDisconnectReason).
#pragma once

#include "realms/Engine.h"
#include "realms/GameNet.h"
#include "realms/Net.h"
#include "realms/NetChannel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace realms {

class GameNetClient {
public:
    enum class State { Disconnected, Challenging, Connecting, Loading, Active };
    // A handshake packet is sent again after this long, kMaxAttempts times.
    static constexpr double kResendSeconds = 1.0;
    static constexpr int kMaxAttempts = 15;
    // A server silent this long is gone.
    static constexpr double kTimeoutSeconds = 30.0;
    // Usercmds go out at most this often (the frames between add up).
    static constexpr double kSendInterval = 1.0 / 60.0;
    static constexpr double kKeepaliveInterval = 1.0;

    struct Userinfo {
        std::string name = "Player";
        float fov = 90.0f;
        int hand = 0;
    };

    // The process's client (the console commands and Quake2Module use it).
    static GameNetClient &instance();
    // Seconds on a steady clock: the `now` for everything here.
    static double clock();

    GameNetClient();
    ~GameNetClient();
    GameNetClient(const GameNetClient &) = delete;
    GameNetClient &operator=(const GameNetClient &) = delete;

    // Starts the handshake with "host[:port]" (kNetDefaultPort without
    // one); ends any connection first. False + error for a bad address or
    // no socket.
    bool connect(const std::string &address, const Userinfo &userinfo, double now, std::string &error);
    // The last server again (its address and userinfo); false without one.
    bool reconnect(double now, std::string &error);
    // Tells the server (when connected) and closes. No reason is kept.
    void disconnect(double now);

    State state() const { return state_; }
    bool connected() const { return state_ != State::Disconnected; }
    // As typed to connect ("" before the first).
    const std::string &serverName() const { return serverName_; }
    const NetAddress &serverAddress() const { return server_; }
    // From the server's client_connect (Loading, Active).
    const std::string &mapPath() const { return mapPath_; }
    uint32_t mapChecksum() const { return mapChecksum_; }
    // The client's slot on the server (it views from edict playerNum() + 1),
    // -1 until the server says.
    int playerNum() const { return playerNum_; }

    // The handshake and its resends, keepalives while Loading, timeouts.
    // While Active, frame() does all of this instead.
    void poll(double now);
    // Loading: the server's map, once (the caller loads it).
    bool takeMapToLoad(std::string &path);
    // Loading or Active: the map is open, the game starts (again) from a
    // full snapshot; false in other states.
    bool begin(double now);
    // Active: the frame's usercmd (dt seconds), the messages out and in;
    // the snapshots decoded since the last call, oldest first.
    void frame(const GameUserCmd &cmd, double dt, double now, std::vector<GameSnapshot> &snapshots);
    // A client command for the game ("inven", "say hi"...), and userinfo
    // changes ("\fov\100\hand\1"); sent with the next message.
    void sendCommand(const std::string &line);
    void sendUserinfo(const std::string &info);
    // Why the connection ended, once ("" when disconnect() ended it).
    std::string takeDisconnectReason();
    uint16_t qport() const { return qport_; }
    // The port of this process's own listen server (0: none): connect
    // refuses it on loopback (the client would close its own server).
    void setLocalServerPort(uint16_t port) { localServerPort_ = port; }

    // Console lines ("Connected to ...", "Server disconnected: ..."); the
    // owner prints them.
    std::function<void(const std::string &text, int level)> print;

private:
    void say(const std::string &text, int level = REALMS_PRINT_INFO);
    void fail(const std::string &reason);
    void startHandshake(double now);
    void sendConnectionless(const std::string &text);
    void transmit(const std::vector<uint8_t> &unreliable, double now);
    // Every waiting datagram; snapshots go to `snapshots` (nullptr: none
    // are decoded).
    void pump(double now, std::vector<GameSnapshot> *snapshots);
    void serverCommand(const std::string &text, double now);
    void checkTimeout(double now);

    UdpSocket socket_;
    NetChannel channel_;
    GameNetDecoder decoder_;
    State state_ = State::Disconnected;
    std::string serverName_;
    NetAddress server_;
    Userinfo userinfo_;
    uint16_t qport_ = 0;
    uint16_t localServerPort_ = 0;
    uint32_t challenge_ = 0;
    int attempts_ = 0;
    double lastAttempt_ = -1e9;
    double lastReceived_ = 0.0, lastSent_ = -1e9;
    std::string mapPath_;
    uint32_t mapChecksum_ = 0;
    bool mapToLoad_ = false;
    int playerNum_ = -1;
    std::string reason_;
    // usercmds: the one being added up, the last ones sent
    GameNetUserCmd pending_;
    bool hasPending_ = false;
    uint32_t cmdNumber_ = 0;
    std::vector<GameNetUserCmd> sentCmds_;
    std::vector<uint8_t> buffer_;
};

// Quake 2's "connect <server>", "disconnect" and "reconnect", the
// multiplayer cvars a game starts with (deathmatch, coop, maxclients; port
// for listen servers), and the engine frame hook that runs the handshake
// and loads the server's map. Once per engine, before the configs run.
void registerGameNetCommands(Engine &engine);

} // namespace realms
