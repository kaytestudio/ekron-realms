// Multiplayer over UDP on 127.0.0.1 without the game module: GameNetServer
// with a stand-in game, GameNetClient (handshake, refusals, snapshots,
// usercmds, commands, userinfo, map changes, timeouts, leaving), the
// usercmd format, and the console commands on an Engine.
#include "realms/Engine.h"
#include "realms/GameClient.h"
#include "realms/GameNet.h"
#include "realms/GameNetClient.h"
#include "realms/GameNetServer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++gChecks;                                                                      \
        if (!(cond)) {                                                                  \
            ++gFailures;                                                                \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                               \
    } while (0)

using State = realms::GameNetClient::State;

// The game: up to `max` clients (0 local); each client's player moves along
// x by its usercmds' forward * seconds; every snapshot has a frame number,
// a config string, and an entity per player.
struct StandInGame : realms::GameNetServerGame {
    int max = 3;
    struct Player {
        bool active = false;
        std::string name, ip;
        float fov = 90.0f;
        int hand = 0;
        double x = 0.0, seconds = 0.0;
        int moves = 0;
        bool attack = false;
        std::vector<std::string> commands;
        bool sentStrings = false;
        std::vector<realms::GameEvent> events;
    };
    std::map<int, Player> players;
    int frame = 0;
    std::string refuse; // connectClient's error when set

    StandInGame() { players[0].active = true; }
    int maxClients() const override { return max; }
    int connectClient(const std::string &name, float fov, int hand, const std::string &ip,
                      std::string &error) override {
        if (!refuse.empty()) {
            error = refuse;
            return -1;
        }
        for (int i = 1; i < max; i++) {
            if (players[i].active) continue;
            players[i] = Player{};
            Player &p = players[i];
            p.active = true;
            p.name = name;
            p.ip = ip;
            p.fov = fov;
            p.hand = hand;
            broadcast(name + " entered the game");
            return i;
        }
        error = "Server is full.";
        return -1;
    }
    void disconnectClient(int client) override {
        players[client].active = false;
        broadcast(players[client].name + " disconnected");
    }
    void clientMove(int client, const realms::GameUserCmd &cmd, double seconds) override {
        Player &p = players[client];
        p.x += cmd.forward * seconds;
        p.seconds += seconds;
        p.moves++;
        p.attack |= cmd.attack;
    }
    void clientCommand(int client, const std::string &line) override {
        players[client].commands.push_back(line);
        if (line.rfind("say ", 0) == 0) broadcast(players[client].name + ": " + line.substr(4));
    }
    void setFov(int client, float fov) override { players[client].fov = fov; }
    void setHand(int client, int hand) override { players[client].hand = hand; }
    void broadcast(const std::string &text) {
        realms::GameEvent ev;
        ev.kind = realms::GameEvent::Kind::Print;
        ev.text = text;
        for (auto &[n, p] : players)
            if (p.active) p.events.push_back(ev);
    }
    void snapshot(int client, realms::GameSnapshot &s, bool full) override {
        Player &p = players[client];
        s = realms::GameSnapshot{};
        if (!p.active) return;
        s.running = true;
        s.frameNumber = frame;
        s.hasPlayer = true;
        s.player.fov = p.fov;
        s.playerOrigin = {float(p.x), 0.0f, 24.0f};
        s.entitiesChanged = true;
        for (auto &[n, other] : players) {
            if (!other.active || n == client) continue;
            realms::GameRenderEntity e;
            e.number = n + 1;
            e.modelindex = 255;
            e.model = "players/male/tris.md2";
            e.origin = {float(other.x), 0.0f, 24.0f};
            s.entities.push_back(e);
        }
        if (full || !p.sentStrings) {
            s.configStrings.push_back({Q2_CS_NAME, "Stand-in"});
            p.sentStrings = true;
        }
        s.events.swap(p.events);
    }
};

struct Rig {
    StandInGame game;
    realms::GameNetServer server;
    std::vector<std::string> said;
    double now = 1000.0;

    bool open(uint16_t port = 0) {
        std::string error;
        server.print = [this](const std::string &t) { said.push_back(t); };
        if (!server.open(port, error, true)) {
            std::fprintf(stderr, "  no UDP socket: %s\n", error.c_str());
            return false;
        }
        server.setMap("maps/stand-in.bsp", 0xabcd1234u);
        return true;
    }
    std::string address() const { return "127.0.0.1:" + std::to_string(server.port()); }
    // one 20 ms frame: the server's, then each client's
    void step(std::vector<realms::GameNetClient *> clients, const realms::GameUserCmd &cmd = {},
              std::vector<realms::GameSnapshot> *out = nullptr, bool serve = true) {
        now += 0.02;
        if (serve) {
            server.receive(game, now);
            game.frame++;
            server.send(game, now);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (realms::GameNetClient *c : clients) {
            std::vector<realms::GameSnapshot> snaps;
            c->frame(cmd, 0.02, now, snaps);
            if (out && c == clients.front())
                for (auto &s : snaps) out->push_back(std::move(s));
        }
    }
    bool until(realms::GameNetClient &c, State state, int steps = 200) {
        for (int i = 0; i < steps && c.state() != state; i++) step({&c});
        return c.state() == state;
    }
};

bool hasPrint(const std::vector<realms::GameEvent> &events, const std::string &text) {
    for (const auto &e : events)
        if (e.kind == realms::GameEvent::Kind::Print && e.text == text) return true;
    return false;
}

// A raw connectionless exchange with the server.
std::string ask(Rig &rig, const std::string &text) {
    realms::UdpSocket s;
    std::string error;
    if (!s.open(0, error, true)) return "no socket";
    realms::NetAddress to;
    realms::NetAddress::parse(rig.address(), to);
    const auto p = realms::NetChannel::connectionless(text);
    s.send(to, p.data(), p.size());
    std::vector<uint8_t> buf(realms::UdpSocket::kMaxDatagram);
    for (int i = 0; i < 200; i++) {
        rig.server.receive(rig.game, rig.now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        realms::NetAddress from;
        const int n = s.receive(from, buf.data(), buf.size());
        std::string reply;
        if (n >= 0 && realms::NetChannel::parseConnectionless(buf.data(), size_t(n), reply)) return reply;
    }
    return "no reply";
}

void formatChecks() {
    std::vector<realms::GameNetUserCmd> cmds(3);
    for (size_t i = 0; i < cmds.size(); i++) {
        cmds[i].number = uint32_t(41 + i);
        cmds[i].seconds = 0.016f * float(i + 1);
        cmds[i].cmd.forward = 400.0f - 100.0f * float(i);
        cmds[i].cmd.side = -200.0f;
        cmds[i].cmd.up = i == 1 ? 400.0f : 0.0f;
        cmds[i].cmd.pitch = -12.5f;
        cmds[i].cmd.yaw = 270.25f;
        cmds[i].cmd.attack = i == 2;
        cmds[i].cmd.use = i == 0;
        cmds[i].cmd.impulse = i == 1 ? 7 : 0;
    }
    const auto bytes = realms::encodeGameNetUserCmds(cmds);
    std::vector<realms::GameNetUserCmd> back;
    CHECK(realms::decodeGameNetUserCmds(bytes.data(), bytes.size(), back) && back.size() == 3);
    for (size_t i = 0; i < back.size() && i < 3; i++) {
        CHECK(back[i].number == cmds[i].number && back[i].seconds == cmds[i].seconds);
        CHECK(back[i].cmd.forward == cmds[i].cmd.forward && back[i].cmd.side == cmds[i].cmd.side);
        CHECK(back[i].cmd.up == cmds[i].cmd.up && back[i].cmd.pitch == cmds[i].cmd.pitch);
        CHECK(back[i].cmd.yaw == cmds[i].cmd.yaw && back[i].cmd.attack == cmds[i].cmd.attack);
        CHECK(back[i].cmd.use == cmds[i].cmd.use && back[i].cmd.impulse == cmds[i].cmd.impulse);
    }
    std::printf("[netplay] 3 usercmds: %zu bytes\n", bytes.size());
    // damaged: every cut short, an extra byte, number 0
    for (size_t n = 0; n < bytes.size(); n++) CHECK(!realms::decodeGameNetUserCmds(bytes.data(), n, back));
    auto longer = bytes;
    longer.push_back(0);
    CHECK(!realms::decodeGameNetUserCmds(longer.data(), longer.size(), back));
    const uint8_t zero[] = {1, 0};
    CHECK(!realms::decodeGameNetUserCmds(zero, sizeof zero, back));
    const uint8_t none[] = {0};
    CHECK(realms::decodeGameNetUserCmds(none, 1, back) && back.empty());

    using realms::gameNetInfoValue;
    const std::string info = "\\name\\Big Guy\\fov\\100\\hand\\\\skin\\male/grunt";
    CHECK(gameNetInfoValue(info, "name") == "Big Guy" && gameNetInfoValue(info, "fov") == "100");
    CHECK(gameNetInfoValue(info, "hand").empty() && gameNetInfoValue(info, "skin") == "male/grunt");
    CHECK(gameNetInfoValue(info, "rate").empty() && gameNetInfoValue("", "name").empty());
    CHECK(gameNetInfoValue("name\\x", "name") == "x");
}

void playChecks() {
    Rig rig;
    if (!rig.open()) return;
    // "info" and handshake refusals, as raw packets
    CHECK(ask(rig, "info") == "info\n1\nmaps/stand-in.bsp\n1\n3");
    CHECK(ask(rig, "connect\n99\n5\n0\n\\name\\x") == "print\nServer is protocol 1, not 99.");
    CHECK(ask(rig, "connect\n1\n5\n12345\n\\name\\x") == "print\nBad challenge.");
    CHECK(ask(rig, "getchallenge").rfind("challenge\n", 0) == 0);
    CHECK(rig.server.clientCount() == 0);

    realms::GameNetClient a;
    std::vector<std::string> aSaid;
    a.print = [&aSaid](const std::string &t, int) { aSaid.push_back(t); };
    realms::GameNetClient::Userinfo info;
    info.name = "Alpha";
    info.fov = 105.0f;
    info.hand = 1;
    std::string error;
    CHECK(!a.connect("bad host!", info, rig.now, error) && error == "bad address bad host!");
    CHECK(a.state() == State::Disconnected);
    a.setLocalServerPort(rig.server.port());
    CHECK(!a.connect(rig.address(), info, rig.now, error) && error == "that is this game's own server");
    a.setLocalServerPort(0);
    CHECK(a.connect(rig.address(), info, rig.now, error));
    CHECK(a.state() == State::Challenging && a.serverName() == rig.address());
    CHECK(rig.until(a, State::Loading));
    CHECK(a.mapPath() == "maps/stand-in.bsp" && a.mapChecksum() == 0xabcd1234u && a.playerNum() == -1);
    std::string map;
    CHECK(a.takeMapToLoad(map) && map == "maps/stand-in.bsp" && !a.takeMapToLoad(map));
    CHECK(rig.server.clientCount() == 1 && rig.server.playingCount() == 0 && !rig.game.players[1].active);
    CHECK(!aSaid.empty() && aSaid.back() == "Connected to " + rig.address() + ": maps/stand-in.bsp");
    CHECK(!rig.said.empty() && rig.said.back().rfind("Alpha connected from 127.0.0.1:", 0) == 0);
    // loading takes a while: keepalives keep both ends up
    for (int i = 0; i < 150; i++) rig.step({&a});
    CHECK(a.state() == State::Loading && rig.server.clientCount() == 1);

    // in the game
    CHECK(a.begin(rig.now) && a.state() == State::Active);
    realms::GameClient view;
    std::vector<realms::GameEvent> events;
    std::vector<realms::GameSnapshot> snaps;
    for (int i = 0; i < 100 && (a.playerNum() < 0 || !view.running()); i++) {
        rig.step({&a}, {}, &snaps);
        for (auto &s : snaps) view.apply(std::move(s));
        snaps.clear();
    }
    CHECK(a.playerNum() == 1 && view.running() && view.playerState());
    StandInGame::Player &pa = rig.game.players[1];
    CHECK(pa.active && pa.name == "Alpha" && pa.ip == "127.0.0.1" && pa.fov == 105.0f && pa.hand == 1);
    CHECK(rig.server.playingCount() == 1);
    CHECK(view.configString(Q2_CS_NAME) == "Stand-in" && view.playerState()->fov == 105.0f);
    for (const auto &e : view.takeEvents()) events.push_back(e);
    CHECK(hasPrint(events, "Alpha entered the game"));
    {
        const auto ents = view.renderEntities(); // the local player's body, not its own
        CHECK(ents.size() == 1 && ents[0].number == 1 && ents[0].model == "players/male/tris.md2");
    }

    // usercmds: 2 seconds of running forward, attack once; every second
    // arrives (each message repeats the last three)
    realms::GameUserCmd run;
    run.forward = 400.0f;
    double sent = 0.0;
    for (int i = 0; i < 100; i++) {
        run.attack = i == 50;
        rig.step({&a}, run, &snaps);
        sent += 0.02;
        for (auto &s : snaps) view.apply(std::move(s));
        snaps.clear();
    }
    run.attack = false;
    for (int i = 0; i < 5; i++) {
        rig.step({&a}, realms::GameUserCmd{}, &snaps);
        for (auto &s : snaps) view.apply(std::move(s));
        snaps.clear();
    }
    std::printf("[netplay] ran %.1f units in %.2f s (%d usercmds)\n", pa.x, pa.seconds, pa.moves);
    CHECK(std::fabs(pa.x - 400.0 * sent) < 1.0 && pa.attack);
    CHECK(pa.moves >= 90 && pa.moves <= 106);
    CHECK(std::fabs(view.playerOrigin()[0] - float(pa.x)) < 0.01f);

    // commands and userinfo
    a.sendCommand("inven");
    a.sendCommand("say hello\nthere");
    a.sendUserinfo("\\fov\\120\\hand\\2");
    for (int i = 0; i < 10; i++) {
        rig.step({&a}, {}, &snaps);
        for (auto &s : snaps) view.apply(std::move(s));
        snaps.clear();
    }
    CHECK(pa.commands.size() == 2 && pa.commands[0] == "inven" && pa.commands[1] == "say hello there");
    CHECK(pa.fov == 120.0f && pa.hand == 2 && view.playerState()->fov == 120.0f);
    for (const auto &e : view.takeEvents()) events.push_back(e);
    CHECK(hasPrint(events, "Alpha: hello there"));

    // a second player; the third is refused (3 slots, one local)
    realms::GameNetClient b, c;
    info.name = "Bravo";
    CHECK(b.connect(rig.address(), info, rig.now, error));
    for (int i = 0; i < 200 && b.state() != State::Loading; i++) rig.step({&a, &b});
    CHECK(b.state() == State::Loading && b.begin(rig.now));
    for (int i = 0; i < 100 && b.playerNum() < 0; i++) rig.step({&a, &b}, {}, &snaps);
    CHECK(b.playerNum() == 2 && rig.server.playingCount() == 2);
    for (int i = 0; i < 5; i++) rig.step({&a, &b}, {}, &snaps);
    for (auto &s : snaps) view.apply(std::move(s));
    snaps.clear();
    {
        const auto ents = view.renderEntities();
        CHECK(ents.size() == 2 && ents[0].number == 1 && ents[1].number == 3);
    }
    info.name = "Charlie";
    CHECK(c.connect(rig.address(), info, rig.now, error));
    for (int i = 0; i < 100 && c.state() != State::Disconnected; i++) rig.step({&a, &b, &c});
    CHECK(c.state() == State::Disconnected);
    CHECK(c.takeDisconnectReason() == "Connection to " + rig.address() + " refused: Server is full.");
    // ... and the game refusing one
    b.disconnect(rig.now);
    for (int i = 0; i < 10; i++) rig.step({&a});
    CHECK(!rig.game.players[2].active && rig.server.clientCount() == 1);
    rig.game.refuse = "Password required or incorrect.";
    CHECK(c.connect(rig.address(), info, rig.now, error));
    for (int i = 0; i < 200 && c.state() != State::Loading; i++) rig.step({&a, &c});
    CHECK(c.begin(rig.now));
    for (int i = 0; i < 100 && c.state() != State::Disconnected; i++) rig.step({&a, &c});
    CHECK(c.state() == State::Disconnected);
    CHECK(c.takeDisconnectReason() == "Server disconnected: Password required or incorrect.");
    CHECK(rig.server.clientCount() == 1);
    rig.game.refuse.clear();

    // the server changes maps: "reconnect", then the new map
    const uint16_t port = rig.server.port();
    rig.server.close("reconnect");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    rig.step({&a}, {}, nullptr, false);
    CHECK(a.state() == State::Challenging && a.takeDisconnectReason().empty());
    CHECK(!aSaid.empty() && aSaid.back().find("changing maps") != std::string::npos);
    for (int i = 0; i < 5; i++) rig.step({&a}, {}, nullptr, false); // the server loads its map
    rig.game.players[1].active = false;
    CHECK(rig.server.open(port, error, true));
    rig.server.setMap("maps/next.bsp", 7);
    CHECK(rig.until(a, State::Loading, 400));
    CHECK(a.takeMapToLoad(map) && map == "maps/next.bsp" && a.mapChecksum() == 7);
    CHECK(a.begin(rig.now));
    for (int i = 0; i < 100 && a.playerNum() < 0; i++) rig.step({&a});
    CHECK(a.playerNum() == 1 && rig.game.players[1].active);

    // the server going quiet: the client times out; a quiet client is dropped
    for (int i = 0; i < 3; i++) rig.step({&a}, {}, nullptr, false);
    rig.now += realms::GameNetClient::kTimeoutSeconds + 1.0;
    rig.step({&a}, {}, nullptr, false);
    CHECK(a.state() == State::Disconnected && a.takeDisconnectReason() == "Server timed out.");
    rig.server.receive(rig.game, rig.now); // what it sent before
    CHECK(rig.server.clientCount() == 1);
    rig.now += realms::GameNetServer::kTimeoutSeconds + 1.0;
    rig.server.receive(rig.game, rig.now);
    CHECK(rig.server.clientCount() == 0 && !rig.game.players[1].active);
    CHECK(!rig.said.empty() && rig.said.back() == "Alpha timed out.");

    // nobody there: the handshake gives up
    {
        realms::UdpSocket probe;
        CHECK(probe.open(0, error, true));
        const uint16_t empty = probe.port();
        probe.close();
        realms::GameNetClient d;
        CHECK(d.connect("127.0.0.1:" + std::to_string(empty), info, rig.now, error));
        for (int i = 0; i < realms::GameNetClient::kMaxAttempts + 2 && d.connected(); i++) {
            rig.now += realms::GameNetClient::kResendSeconds + 0.01;
            d.poll(rig.now);
        }
        CHECK(d.state() == State::Disconnected);
        CHECK(d.takeDisconnectReason() == "No answer from 127.0.0.1:" + std::to_string(empty) + ".");
    }

    // reconnect: the same server and userinfo again; then "Server quit"
    CHECK(a.reconnect(rig.now, error) && a.state() == State::Challenging);
    CHECK(rig.until(a, State::Loading));
    a.begin(rig.now);
    for (int i = 0; i < 100 && a.playerNum() < 0; i++) rig.step({&a});
    CHECK(rig.game.players[1].active && rig.game.players[1].name == "Alpha");
    rig.server.close();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    rig.step({&a}, {}, nullptr, false);
    CHECK(a.state() == State::Disconnected && a.takeDisconnectReason() == "Server disconnected: Server quit");
    realms::GameNetClient never;
    CHECK(!never.reconnect(rig.now, error) && error == "no server to reconnect to");
}

// The console commands, the cvars and the frame hook on an Engine.
void consoleChecks() {
    realms::Engine engine;
    realms::EngineConfig cfg;
    cfg.backend = realms::BackendPreference::Native;
    if (!engine.init(cfg)) {
        CHECK(false);
        return;
    }
    realms::registerGameNetCommands(engine);
    CHECK(engine.commandExists("connect") && engine.commandExists("disconnect") && engine.commandExists("reconnect"));
    CHECK(engine.cvarString("deathmatch") == "0" && engine.cvarString("coop") == "0");
    CHECK(engine.cvarString("maxclients") == "1" && engine.cvarString("port") == "27910");
    engine.exec("deathmatch 1\nmaxclients 6\n");
    CHECK(engine.cvarString("deathmatch") == "1" && engine.cvarString("maxclients") == "6");
    const auto lastLine = [&engine]() { return engine.console().empty() ? std::string() : engine.console().back().text; };
    engine.exec("connect\n");
    CHECK(lastLine() == "USAGE: connect <server>");
    engine.exec("disconnect\n");
    CHECK(lastLine() == "Not connected to a server.");

    Rig rig;
    if (!rig.open()) return;
    realms::GameNetClient &net = realms::GameNetClient::instance();
    engine.exec("name Echo\nconnect " + rig.address() + "\n");
    CHECK(net.state() == State::Challenging && lastLine() == "Connecting to " + rig.address() + "...");
    // the frame hook runs the handshake; the server's map isn't here
    for (int i = 0; i < 200 && net.connected(); i++) {
        rig.server.receive(rig.game, realms::GameNetClient::clock());
        rig.server.send(rig.game, realms::GameNetClient::clock());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        engine.frame(0.01);
    }
    CHECK(!net.connected());
    CHECK(lastLine() == "connect: the server's map maps/stand-in.bsp isn't here");
    CHECK(rig.server.clientCount() == 1);
    for (int i = 0; i < 10; i++) rig.server.receive(rig.game, realms::GameNetClient::clock());
    CHECK(rig.server.clientCount() == 0); // it said goodbye
    engine.exec("reconnect\n");
    CHECK(net.state() == State::Challenging && lastLine() == "Reconnecting to " + rig.address() + "...");
    engine.exec("disconnect\n");
    CHECK(!net.connected() && lastLine() == "Disconnected from " + rig.address() + ".");
    engine.exec("connect 127.0.0.1:99999\n");
    CHECK(!net.connected() && lastLine() == "connect: 127.0.0.1:99999: bad port in 127.0.0.1:99999");
    engine.shutdown();
}

} // namespace

int main() {
    formatChecks();
    playChecks();
    consoleChecks();
    std::printf("[netplay] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
