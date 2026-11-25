// Realms Engine - the game client on the network (see GameNetClient.h).
#include "realms/GameNetClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

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

// Userinfo values can't hold the separators.
std::string infoSafe(std::string v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\\' || c == '"' || c == ';' || c == '\n'; }),
            v.end());
    return v;
}

std::string formatFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", double(v));
    return buf;
}

} // namespace

GameNetClient &GameNetClient::instance() {
    static GameNetClient client;
    return client;
}

double GameNetClient::clock() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

GameNetClient::GameNetClient() : buffer_(UdpSocket::kMaxDatagram) {}
GameNetClient::~GameNetClient() = default;

void GameNetClient::say(const std::string &text, int level) {
    if (print && !text.empty()) print(text, level);
}

void GameNetClient::fail(const std::string &reason) {
    reason_ = reason;
    say(reason, REALMS_PRINT_WARNING);
    state_ = State::Disconnected;
    mapToLoad_ = false;
    socket_.close();
}

bool GameNetClient::connect(const std::string &address, const Userinfo &userinfo, double now, std::string &error) {
    NetAddress to;
    if (!NetAddress::parse(address, to, kNetDefaultPort, &error)) return false;
    if (to.isLoopback() && localServerPort_ && to.port == localServerPort_) {
        error = "that is this game's own server";
        return false;
    }
    disconnect(now);
    if (!socket_.open(0, error)) return false;
    serverName_ = address;
    server_ = to;
    userinfo_ = userinfo;
    reason_.clear();
    std::random_device rd;
    qport_ = uint16_t(1 + rd() % 65535);
    startHandshake(now);
    poll(now); // the first getchallenge goes now
    return true;
}

bool GameNetClient::reconnect(double now, std::string &error) {
    if (serverName_.empty()) {
        error = "no server to reconnect to";
        return false;
    }
    const std::string name = serverName_;
    const Userinfo info = userinfo_;
    return connect(name, info, now, error);
}

void GameNetClient::startHandshake(double now) {
    state_ = State::Challenging;
    attempts_ = 0;
    lastAttempt_ = -1e9;
    mapToLoad_ = false;
    playerNum_ = -1;
    mapPath_.clear();
    mapChecksum_ = 0;
    hasPending_ = false;
    sentCmds_.clear();
    cmdNumber_ = 0;
    channel_.reset(qport_);
    decoder_.reset();
    (void)now; // the next poll() sends the first getchallenge
}

void GameNetClient::disconnect(double now) {
    if ((state_ == State::Loading || state_ == State::Active) && socket_.isOpen()) {
        channel_.sendReliable(typed(kGameNetDisconnect, ""));
        for (int i = 0; i < 3; i++) transmit({}, now); // CL_Disconnect sends it three times
    }
    state_ = State::Disconnected;
    mapToLoad_ = false;
    playerNum_ = -1;
    socket_.close();
}

void GameNetClient::sendConnectionless(const std::string &text) {
    const std::vector<uint8_t> p = NetChannel::connectionless(text);
    socket_.send(server_, p.data(), p.size());
}

void GameNetClient::transmit(const std::vector<uint8_t> &unreliable, double now) {
    std::vector<std::vector<uint8_t>> datagrams;
    channel_.transmit(unreliable, datagrams, now);
    for (const auto &d : datagrams) socket_.send(server_, d.data(), d.size());
    lastSent_ = now;
}

void GameNetClient::poll(double now) {
    if (state_ == State::Disconnected) return;
    pump(now, nullptr);
    if (state_ == State::Challenging || state_ == State::Connecting) {
        // CL_CheckForResend
        if (now - lastAttempt_ < kResendSeconds) return;
        if (attempts_ >= kMaxAttempts) {
            fail("No answer from " + serverName_ + ".");
            return;
        }
        attempts_++;
        lastAttempt_ = now;
        if (state_ == State::Challenging) {
            sendConnectionless("getchallenge");
        } else {
            const std::string info = "\\name\\" + infoSafe(userinfo_.name) + "\\fov\\" + formatFloat(userinfo_.fov) +
                                     "\\hand\\" + std::to_string(userinfo_.hand);
            sendConnectionless("connect\n" + std::to_string(kGameNetProtocol) + "\n" + std::to_string(qport_) + "\n" +
                               std::to_string(challenge_) + "\n" + info);
        }
        return;
    }
    if (state_ == State::Loading && now - lastSent_ >= kKeepaliveInterval) transmit({}, now);
    checkTimeout(now);
}

void GameNetClient::checkTimeout(double now) {
    if ((state_ == State::Loading || state_ == State::Active) && now - lastReceived_ > kTimeoutSeconds)
        fail("Server timed out.");
}

void GameNetClient::pump(double now, std::vector<GameSnapshot> *snapshots) {
    while (socket_.isOpen()) {
        NetAddress from;
        const int n = socket_.receive(from, buffer_.data(), buffer_.size());
        if (n < 0) break;
        if (from != server_) continue;
        std::string text;
        if (NetChannel::parseConnectionless(buffer_.data(), size_t(n), text)) {
            const std::vector<std::string> l = lines(text);
            if (l[0] == "challenge" && state_ == State::Challenging && l.size() >= 2) {
                challenge_ = uint32_t(std::strtoul(l[1].c_str(), nullptr, 10));
                state_ = State::Connecting;
                attempts_ = 0;
                lastAttempt_ = -1e9; // the connect goes at once
            } else if (l[0] == "client_connect" && state_ == State::Connecting && l.size() >= 3) {
                mapPath_ = l[1];
                mapChecksum_ = uint32_t(std::strtoul(l[2].c_str(), nullptr, 10));
                channel_.reset(qport_);
                decoder_.reset();
                state_ = State::Loading;
                mapToLoad_ = true;
                lastReceived_ = now;
                lastSent_ = now;
                say("Connected to " + serverName_ + ": " + mapPath_);
            } else if (l[0] == "print" && (state_ == State::Challenging || state_ == State::Connecting)) {
                std::string why = l.size() >= 2 ? l[1] : std::string("refused");
                fail("Connection to " + serverName_ + " refused: " + why);
                return;
            }
            continue;
        }
        if (state_ != State::Loading && state_ != State::Active) continue;
        NetMessage m;
        if (!channel_.process(buffer_.data(), size_t(n), m, now)) continue;
        lastReceived_ = now;
        for (const std::vector<uint8_t> &r : m.reliable) {
            if (r.empty()) continue;
            if (r[0] == kGameNetEvents) {
                decoder_.reliable(r.data(), r.size());
            } else if (r[0] == kGameNetServerCommand) {
                serverCommand(std::string(r.begin() + 1, r.end()), now);
                if (state_ != State::Loading && state_ != State::Active) return;
            }
        }
        if (state_ == State::Active && snapshots && !m.unreliable.empty()) {
            GameSnapshot s;
            if (decoder_.decode(m.unreliable.data(), m.unreliable.size(), m.sequence, s))
                snapshots->push_back(std::move(s));
        }
    }
}

void GameNetClient::serverCommand(const std::string &text, double now) {
    const std::vector<std::string> l = lines(text);
    if (l[0].rfind("playernum ", 0) == 0) {
        playerNum_ = std::atoi(l[0].c_str() + 10);
    } else if (l[0] == "disconnect") {
        const std::string why = l.size() >= 2 && !l[1].empty() ? l[1] : std::string("disconnected");
        fail("Server disconnected: " + why);
    } else if (l[0] == "reconnect") {
        // the server changes maps: the handshake again, then its new map
        say("Server is changing maps, reconnecting to " + serverName_ + ".");
        startHandshake(now);
    }
}

bool GameNetClient::takeMapToLoad(std::string &path) {
    if (!mapToLoad_ || state_ != State::Loading) return false;
    mapToLoad_ = false;
    path = mapPath_;
    return true;
}

bool GameNetClient::begin(double now) {
    if (state_ != State::Loading && state_ != State::Active) return false;
    mapToLoad_ = false;
    decoder_.reset(); // the server sends everything again
    state_ = State::Active;
    channel_.sendReliable(typed(kGameNetBegin, ""));
    transmit({}, now);
    return true;
}

void GameNetClient::frame(const GameUserCmd &cmd, double dt, double now, std::vector<GameSnapshot> &snapshots) {
    snapshots.clear();
    if (state_ != State::Active) {
        poll(now);
        return;
    }
    // CL_SendCmd: this frame's usercmd joins the one being added up (the
    // newest move and angles, buttons held in any of the frames)
    const double seconds = std::isfinite(dt) ? std::clamp(dt, 0.0, 1.0) : 0.0;
    if (!hasPending_) {
        pending_ = GameNetUserCmd{};
        pending_.cmd = cmd;
        hasPending_ = true;
    } else {
        const bool attack = pending_.cmd.attack || cmd.attack, use = pending_.cmd.use || cmd.use;
        const int impulse = pending_.cmd.impulse ? pending_.cmd.impulse : cmd.impulse;
        pending_.cmd = cmd;
        pending_.cmd.attack = attack;
        pending_.cmd.use = use;
        pending_.cmd.impulse = impulse;
    }
    pending_.seconds = float(double(pending_.seconds) + seconds);
    if (now - lastSent_ >= kSendInterval) {
        pending_.number = ++cmdNumber_;
        sentCmds_.push_back(pending_);
        if (sentCmds_.size() > kGameNetUserCmdsPerMessage) sentCmds_.erase(sentCmds_.begin());
        hasPending_ = false;
        transmit(encodeGameNetUserCmds(sentCmds_), now);
    }
    pump(now, &snapshots);
    checkTimeout(now);
}

void GameNetClient::sendCommand(const std::string &line) {
    if ((state_ == State::Loading || state_ == State::Active) && !line.empty())
        channel_.sendReliable(typed(kGameNetStringCmd, line));
}

void GameNetClient::sendUserinfo(const std::string &info) {
    if ((state_ == State::Loading || state_ == State::Active) && !info.empty())
        channel_.sendReliable(typed(kGameNetUserinfo, info));
    const std::string fov = gameNetInfoValue(info, "fov"), hand = gameNetInfoValue(info, "hand");
    if (!fov.empty()) userinfo_.fov = float(std::atof(fov.c_str()));
    if (!hand.empty()) userinfo_.hand = std::atoi(hand.c_str());
}

std::string GameNetClient::takeDisconnectReason() {
    std::string out;
    out.swap(reason_);
    return out;
}

// ------------------------------------------------------------ console

void registerGameNetCommands(Engine &engine) {
    // SV_InitGame's cvars: Quake2Module starts the game with them. Taken by
    // the next game (GameServer keeps the running game's own copies).
    engine.registerCvar("deathmatch", "0", REALMS_CVAR_SERVERINFO);
    engine.registerCvar("coop", "0", 0);
    engine.registerCvar("maxclients", "1", REALMS_CVAR_SERVERINFO);
    // The UDP port a deathmatch or coop game listens on.
    engine.registerCvar("port", std::to_string(kNetDefaultPort), 0);

    Engine *e = &engine;
    GameNetClient &net = GameNetClient::instance();
    net.print = [e](const std::string &text, int level) { e->print(text, level); };
    const auto userinfo = [e]() {
        GameNetClient::Userinfo info;
        info.name = e->cvarString("name", "Player");
        info.fov = e->cvarFloat("fov", 90.0f);
        info.hand = GameServer::clampHand(e->cvarInt("hand", 0));
        return info;
    };
    if (!engine.commandExists("connect"))
        engine.addCommand("connect", [e, userinfo](const CommandArgs &args) {
            if (args.size() != 2) {
                e->print("USAGE: connect <server>");
                return;
            }
            std::string error;
            if (!GameNetClient::instance().connect(args[1], userinfo(), GameNetClient::clock(), error))
                e->print("connect: " + args[1] + ": " + error, REALMS_PRINT_WARNING);
            else
                e->print("Connecting to " + args[1] + "...");
        });
    if (!engine.commandExists("disconnect"))
        engine.addCommand("disconnect", [e](const CommandArgs &) {
            GameNetClient &c = GameNetClient::instance();
            if (!c.connected()) {
                e->print("Not connected to a server.");
                return;
            }
            c.disconnect(GameNetClient::clock());
            e->print("Disconnected from " + c.serverName() + ".");
        });
    if (!engine.commandExists("reconnect"))
        engine.addCommand("reconnect", [e](const CommandArgs &) {
            GameNetClient &c = GameNetClient::instance();
            std::string error;
            if (!c.reconnect(GameNetClient::clock(), error)) e->print("reconnect: " + error, REALMS_PRINT_WARNING);
            else e->print("Reconnecting to " + c.serverName() + "...");
        });
    // the handshake runs whatever the host shows (menus, the sandbox); the
    // game's own frames (Quake2Module) take over once Active
    engine.addFrameHook([e](double) {
        GameNetClient &c = GameNetClient::instance();
        if (c.state() == GameNetClient::State::Disconnected || c.state() == GameNetClient::State::Active) return;
        c.poll(GameNetClient::clock());
        std::string path;
        if (!c.takeMapToLoad(path)) return;
        if (e->findMap(path).empty()) {
            c.disconnect(GameNetClient::clock());
            e->print("connect: the server's map " + path + " isn't here", REALMS_PRINT_WARNING);
            return;
        }
        std::string quoted;
        for (char ch : path)
            if (ch != '"') quoted += ch;
        e->insertText("map \"" + quoted + "\"\n");
    });
}

} // namespace realms
