// Ekron networking (realms/Net.h, realms/NetChannel.h, realms/GameNet.h):
// addresses, UDP on 127.0.0.1, the channel over a lossy, duplicating,
// reordering link (reliable messages once and in order, fragments, latest
// wins), and game snapshots over it ending up like a GameClient fed
// directly. No game module needed.
#include "realms/GameClient.h"
#include "realms/GameNet.h"
#include "realms/Net.h"
#include "realms/NetChannel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;

#define CHECK(cond)                                                                                                \
    do {                                                                                                           \
        ++gChecks;                                                                                                 \
        if (!(cond)) {                                                                                             \
            ++gFailures;                                                                                           \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                                          \
    } while (0)

using Bytes = std::vector<uint8_t>;

Bytes bytes(const std::string &s) { return Bytes(s.begin(), s.end()); }
std::string text(const Bytes &b) { return std::string(b.begin(), b.end()); }

// xorshift: the same "random" link every run
struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    bool chance(int percent) { return int(next() % 100) < percent; }
};

// One direction of a link: drops, duplicates and reorders datagrams.
struct Link {
    Rng rng;
    int lossPercent = 0, duplicatePercent = 0, reorderPercent = 0;
    std::vector<Bytes> inFlight;
    int sent = 0, lost = 0;
    void send(const std::vector<Bytes> &datagrams) {
        for (const Bytes &d : datagrams) {
            sent++;
            if (rng.chance(lossPercent)) {
                lost++;
                continue;
            }
            inFlight.push_back(d);
            if (rng.chance(duplicatePercent)) inFlight.push_back(d);
        }
    }
    std::vector<Bytes> deliver() {
        std::vector<Bytes> out;
        out.swap(inFlight);
        for (size_t i = 1; i < out.size(); i++)
            if (rng.chance(reorderPercent)) std::swap(out[i], out[rng.next() % (i + 1)]);
        return out;
    }
};

// ------------------------------------------------------------ addresses / UDP

void addressChecks() {
    realms::NetAddress a;
    CHECK(realms::NetAddress::parse("127.0.0.1", a) && a.ip == 0x7f000001u && a.port == realms::kNetDefaultPort);
    CHECK(a.isLoopback() && a.toString() == "127.0.0.1:27910");
    CHECK(realms::NetAddress::parse("10.0.200.5:1234", a) && a.ip == 0x0a00c805u && a.port == 1234);
    CHECK(!a.isLoopback() && a.toString() == "10.0.200.5:1234");
    CHECK(realms::NetAddress::parse("localhost:5", a) && a.ip == 0x7f000001u && a.port == 5);
    CHECK(realms::NetAddress::parse("loopback", a, 99) && a.port == 99);
    realms::NetAddress b;
    CHECK(realms::NetAddress::parse(a.toString(), b) && b == a);
    std::string error;
    for (const char *bad : {"", "1.2.3.4:", ":80", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:12a", "a b", "1.2.3.4.5:x",
                            "bad\\name"}) {
        error.clear();
        const bool ok = realms::NetAddress::parse(bad, a, realms::kNetDefaultPort, &error);
        CHECK(!ok && !error.empty());
        if (ok) std::fprintf(stderr, "  parsed \"%s\"\n", bad);
    }
    CHECK(!realms::NetAddress::parse("1.2.3.256", a) || a.ip != 0x01020300u); // not a dotted quad: a name
}

void udpChecks() {
    realms::UdpSocket a, b;
    std::string error;
    const bool opened = a.open(0, error, true) && b.open(0, error, true);
    if (!opened) std::fprintf(stderr, "  udp: %s\n", error.c_str());
    CHECK(opened);
    if (!opened) return;
    CHECK(a.isOpen() && a.port() != 0 && b.port() != 0 && a.port() != b.port());
    uint8_t buf[realms::UdpSocket::kMaxDatagram];
    realms::NetAddress from;
    CHECK(b.receive(from, buf, sizeof buf) == -1); // nothing waiting, and no blocking
    realms::NetAddress to;
    to.ip = 0x7f000001u;
    to.port = b.port();
    const Bytes hello = bytes("hello, server");
    Bytes big(4000);
    for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t(i * 7);
    CHECK(a.send(to, hello.data(), hello.size()));
    CHECK(a.send(to, big.data(), big.size()));
    std::vector<Bytes> got;
    for (int i = 0; i < 2000 && got.size() < 2; i++) {
        const int n = b.receive(from, buf, sizeof buf);
        if (n >= 0) {
            got.emplace_back(buf, buf + n);
            CHECK(from.ip == 0x7f000001u && from.port == a.port());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    CHECK(got.size() == 2 && got[0] == hello && got[1] == big);
    // a reply to the sender's address
    CHECK(b.send(from, hello.data(), 5));
    int n = -1;
    for (int i = 0; i < 2000 && n < 0; i++) {
        n = a.receive(from, buf, sizeof buf);
        if (n < 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(n == 5 && std::memcmp(buf, "hello", 5) == 0 && from.port == b.port());
    // the port is taken while `a` holds it
    realms::UdpSocket c;
    CHECK(!c.open(a.port(), error, true) && !error.empty());
    a.close();
    CHECK(!a.isOpen() && !a.send(to, hello.data(), hello.size()));
}

// ------------------------------------------------------------ the channel

void channelBasics() {
    realms::NetChannel client(77), server(77);
    std::vector<Bytes> out;
    realms::NetMessage msg;
    CHECK(client.sendReliable(bytes("connect")));
    client.transmit(bytes("move"), out, 0.0);
    CHECK(out.size() == 1 && client.outgoingSequence() == 1);
    CHECK(out[0].size() < realms::NetChannel::kHeaderBytes + 20);
    uint16_t qport = 0;
    CHECK(realms::NetChannel::peekQport(out[0].data(), out[0].size(), qport) && qport == 77);
    CHECK(server.process(out[0].data(), out[0].size(), msg, 0.5));
    CHECK(msg.sequence == 1 && msg.reliable.size() == 1 && text(msg.reliable[0]) == "connect");
    CHECK(text(msg.unreliable) == "move" && server.lastReceived() == 0.5);
    // the same datagram again: a duplicate
    CHECK(!server.process(out[0].data(), out[0].size(), msg, 0.6));
    // another qport, cut short, connectionless: not for this channel
    realms::NetChannel other(78);
    CHECK(!other.process(out[0].data(), out[0].size(), msg, 0.6));
    CHECK(!server.process(out[0].data(), realms::NetChannel::kHeaderBytes - 1, msg, 0.6));
    const Bytes oob = realms::NetChannel::connectionless("getchallenge");
    std::string oobText;
    CHECK(realms::NetChannel::parseConnectionless(oob.data(), oob.size(), oobText) && oobText == "getchallenge");
    CHECK(!realms::NetChannel::parseConnectionless(out[0].data(), out[0].size(), oobText));
    CHECK(!server.process(oob.data(), oob.size(), msg, 0.6) && !realms::NetChannel::peekQport(oob.data(), oob.size(), qport));
    // the reply acknowledges both: the message and the reliable one
    CHECK(client.reliablePending() == 1);
    server.transmit(bytes("snap"), out, 0.7);
    CHECK(client.process(out[0].data(), out[0].size(), msg, 0.8));
    CHECK(client.incomingAcknowledged() == 1 && client.reliablePending() == 0 && client.reliableBacklog() == 0);
    CHECK(msg.reliable.empty() && text(msg.unreliable) == "snap");
    // a big message goes in fragments, delivered whole, in any order
    Bytes big(5000);
    for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t(i * 31 + 1);
    server.transmit(big, out, 1.0);
    CHECK(out.size() == 5);
    for (const Bytes &d : out) CHECK(d.size() <= realms::NetChannel::kHeaderBytes + realms::NetChannel::kFragmentBytes);
    std::reverse(out.begin(), out.end());
    for (size_t i = 0; i + 1 < out.size(); i++) CHECK(!client.process(out[i].data(), out[i].size(), msg, 1.0));
    CHECK(client.process(out.back().data(), out.back().size(), msg, 1.0) && msg.unreliable == big);
    // an older message's fragments after a newer message: dropped
    server.transmit(big, out, 1.1);
    const std::vector<Bytes> older = out;
    server.transmit(bytes("newer"), out, 1.2);
    CHECK(client.process(out[0].data(), out[0].size(), msg, 1.2) && text(msg.unreliable) == "newer");
    CHECK(client.droppedMessages() == 1);
    for (const Bytes &d : older) CHECK(!client.process(d.data(), d.size(), msg, 1.3));
    // the reliable backlog has a limit
    realms::NetChannel flood(1);
    bool overflowed = false;
    for (int i = 0; i < 1000 && !overflowed; i++) overflowed = !flood.sendReliable(Bytes(1024, 1));
    CHECK(overflowed && flood.overflowed() && flood.reliableBacklog() <= realms::NetChannel::kMaxReliableBytes);
    CHECK(!flood.sendReliable(Bytes(1, 1)));
}

// Both ends talk over a bad link for a while: every reliable message
// arrives once and in order, unreliable ones only ever newer.
void channelOverBadLink() {
    realms::NetChannel a(5), b(5);
    Link ab{{12345}, 25, 10, 30}, ba{{999}, 25, 10, 30};
    int nextA = 0, nextB = 0;    // reliable messages queued
    std::vector<std::string> gotA, gotB; // received by b from a, by a from b
    uint32_t lastUnreliableB = 0, lastUnreliableA = 0;
    bool orderOk = true, bigOk = false, bigSeen = false;
    std::vector<Bytes> out;
    realms::NetMessage msg;
    auto reliableText = [](char who, int n) {
        std::string s = std::string(1, who) + std::to_string(n) + ":";
        s.append(size_t(n % 7 == 3 ? 3000 : n % 5), char('a' + n % 26)); // some need fragments
        return s;
    };
    for (int tick = 0; tick < 3000; tick++) {
        const bool settle = tick >= 2800; // a clean link at the end
        ab.lossPercent = ba.lossPercent = settle ? 0 : 25;
        if (tick < 2000 && tick % 3 == 0) CHECK(a.sendReliable(bytes(reliableText('a', nextA++))));
        if (tick < 2000 && tick % 4 == 1) CHECK(b.sendReliable(bytes(reliableText('b', nextB++))));
        Bytes ua = bytes("ua" + std::to_string(tick));
        if (tick % 50 == 7 && tick < 2000) ua.resize(6000, uint8_t(tick)); // a big unreliable one now and then
        a.transmit(ua, out, tick * 0.01);
        ab.send(out);
        b.transmit(bytes("ub" + std::to_string(tick)), out, tick * 0.01);
        ba.send(out);
        for (const Bytes &d : ab.deliver())
            if (b.process(d.data(), d.size(), msg, tick * 0.01)) {
                for (const Bytes &r : msg.reliable) gotB.push_back(text(r));
                orderOk &= msg.sequence > lastUnreliableB;
                lastUnreliableB = msg.sequence;
                if (msg.unreliable.size() == 6000) {
                    bigSeen = true;
                    bigOk = std::all_of(msg.unreliable.begin() + 10, msg.unreliable.end(),
                                        [&](uint8_t v) { return v == msg.unreliable.back(); });
                }
            }
        for (const Bytes &d : ba.deliver())
            if (a.process(d.data(), d.size(), msg, tick * 0.01)) {
                for (const Bytes &r : msg.reliable) gotA.push_back(text(r));
                orderOk &= msg.sequence > lastUnreliableA;
                lastUnreliableA = msg.sequence;
            }
    }
    CHECK(orderOk);
    CHECK(ab.lost > 500 && ba.lost > 500);
    CHECK(int(gotB.size()) == nextA && int(gotA.size()) == nextB);
    bool same = gotB.size() == size_t(nextA) && gotA.size() == size_t(nextB);
    for (int i = 0; same && i < nextA; i++) same = gotB[size_t(i)] == reliableText('a', i);
    for (int i = 0; same && i < nextB; i++) same = gotA[size_t(i)] == reliableText('b', i);
    CHECK(same);
    CHECK(a.reliablePending() == 0 && b.reliablePending() == 0);
    CHECK(bigSeen && bigOk);
    CHECK(b.droppedMessages() > 0 && b.incomingSequence() == a.outgoingSequence());
    CHECK(a.incomingAcknowledged() + 1 == a.outgoingSequence()); // b acknowledges in its next one
    std::printf("[net] link: %d of %d datagrams lost, %u messages dropped, %d + %d reliable delivered\n",
                ab.lost + ba.lost, ab.sent + ba.sent, b.droppedMessages(), nextA, nextB);
}

// ------------------------------------------------------------ snapshots

realms::GameRenderEntity entity(int number, const std::string &model, float x) {
    realms::GameRenderEntity e;
    e.number = number;
    e.modelindex = number + 1;
    e.model = model;
    e.origin = {x, 8.0f, 24.0f};
    e.oldOrigin = {x - 1.0f, 8.0f, 24.0f};
    e.mins = {-16, -16, -24};
    e.maxs = {16, 16, 32};
    e.frame = number;
    return e;
}

// What GameServer::snapshot(client) would hand out at sandbox frame `i`
// (three per 100 ms server frame): new entity lists on new frames,
// config strings, events and inventory only when they change.
realms::GameSnapshot gameSnapshot(int i) {
    realms::GameSnapshot s;
    s.running = true;
    s.frameNumber = 2 + i / 3;
    s.lerpFraction = (i % 3) / 3.0;
    s.hasPlayer = true;
    s.player.pmove.origin[0] = int16_t(-2560 + i * 4);
    s.player.pmove.gravity = 800;
    s.player.viewangles[1] = float(i % 360);
    s.player.fov = 90.0f;
    s.player.stats[Q2_STAT_HEALTH] = int16_t(100 - i / 50);
    s.playerOrigin = {-320.0f + i * 0.5f, 0.0f, 24.125f};
    if (i == 0 || i % 3 == 0) {
        s.entitiesChanged = true;
        const int frame = s.frameNumber;
        s.entities.push_back(entity(3, "*1", 0.0f));
        s.entities.push_back(entity(4, "models/items/armor/shard/tris.md2", -160.0f));
        for (int k = 0; k < 30; k++) // monsters walking about
            s.entities.push_back(entity(10 + k, "models/monsters/soldier/tris.md2", float(k * 40 + frame)));
        if (frame % 20 < 10) s.entities.push_back(entity(60, "models/objects/rocket/tris.md2", float(frame * 20)));
        realms::GameBeamEntity beam;
        beam.number = 70;
        beam.origin = {float(frame), 0, 0};
        s.beams.push_back(beam);
        s.loopSounds.push_back({4, "sound/world/amb10.wav", {-160, 0, 15}});
    }
    if (i == 0) {
        for (int k = 0; k < 400; k++) // a level's worth
            s.configStrings.emplace_back(Q2_CS_ITEMS + k, "item number " + std::to_string(k));
        s.configStrings.emplace_back(Q2_CS_NAME, "The Arena");
        s.configStrings.emplace_back(Q2_CS_MODELS + 1, "maps/arena.bsp");
        s.inventoryChanged = true;
        s.inventory[7] = 1;
    }
    if (i > 0 && i % 37 == 0) s.configStrings.emplace_back(Q2_CS_LIGHTS + 1, "abc" + std::to_string(i));
    if (i % 41 == 5) {
        s.inventoryChanged = true;
        s.inventory[7] = 1;
        s.inventory[18] = int16_t(i);
    }
    if (i % 5 == 2) {
        realms::GameEvent ev;
        ev.kind = realms::GameEvent::Kind::Print;
        ev.level = 1;
        ev.text = "message " + std::to_string(i) + "\n";
        s.events.push_back(ev);
        realms::GameEvent snd;
        snd.kind = realms::GameEvent::Kind::Sound;
        snd.text = "sound/weapons/shotgf1b.wav";
        snd.entity = 1;
        s.events.push_back(snd);
    }
    return s;
}

bool sameEntities(const std::vector<realms::GameRenderEntity> &a, const std::vector<realms::GameRenderEntity> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].number != b[i].number || a[i].model != b[i].model || a[i].origin != b[i].origin ||
            a[i].frame != b[i].frame)
            return false;
    return true;
}

bool sameClient(const realms::GameClient &a, const realms::GameClient &b) {
    if (a.running() != b.running() || a.frameNumber() != b.frameNumber()) return false;
    if (!a.running()) return true;
    const q2player_state_t *pa = a.playerState(), *pb = b.playerState();
    if (!pa || !pb || std::memcmp(pa, pb, sizeof *pa) != 0) return false;
    if (a.playerOrigin() != b.playerOrigin() || a.lerpFraction() != b.lerpFraction()) return false;
    if (!sameEntities(a.renderEntities(true), b.renderEntities(true))) return false;
    if (a.beamEntities().size() != b.beamEntities().size() || a.loopSounds().size() != b.loopSounds().size())
        return false;
    if (a.inventory() != b.inventory()) return false;
    for (int i = 0; i < Q2_MAX_CONFIGSTRINGS; i++)
        if (a.configString(i) != b.configString(i)) return false;
    return true;
}

void snapshotChecks() {
    // a perfect link first: what the changes cost
    {
        realms::GameNetEncoder enc;
        realms::GameNetDecoder dec;
        Bytes u, r;
        realms::GameSnapshot out;
        enc.encode(gameSnapshot(0), 1, 0, u, r);
        CHECK(enc.lastBase() == 0 && !r.empty() && r[0] == realms::kGameNetEvents);
        const size_t first = u.size();
        CHECK(dec.reliable(r.data(), r.size()) && dec.waitingEvents() == 0);
        CHECK(dec.decode(u.data(), u.size(), 1, out));
        CHECK(out.running && out.entitiesChanged && out.entities.size() == 33 && out.configStrings.size() == 402);
        CHECK(out.inventoryChanged && out.inventory[7] == 1);
        // the client acknowledged message 1: the next is changes from it
        enc.encode(gameSnapshot(1), 2, 1, u, r);
        CHECK(enc.lastBase() == 1 && r.empty());
        const size_t playerBytes = u.size();
        CHECK(u.size() < 64 && u.size() < first / 20);
        CHECK(dec.decode(u.data(), u.size(), 2, out));
        CHECK(out.running && !out.entitiesChanged && !out.inventoryChanged && out.configStrings.empty());
        CHECK(out.playerOrigin[0] == gameSnapshot(1).playerOrigin[0] && out.lerpFraction == gameSnapshot(1).lerpFraction);
        // a new server frame, still from message 1 (2 not acknowledged yet)
        enc.encode(gameSnapshot(3), 3, 1, u, r);
        CHECK(dec.decode(u.data(), u.size(), 3, out) && out.entitiesChanged && out.entities.size() == 33);
        const size_t frameBytes = u.size();
        // from 3: the same frame again, so no lists
        enc.encode(gameSnapshot(4), 4, 3, u, r);
        CHECK(dec.decode(u.data(), u.size(), 4, out) && !out.entitiesChanged && u.size() < frameBytes);
        // the client already had frame 3's lists: sent again from base 2, not handed out twice
        enc.encode(gameSnapshot(5), 5, 2, u, r);
        CHECK(u.size() > 200);
        CHECK(dec.decode(u.data(), u.size(), 5, out) && !out.entitiesChanged && out.entities.empty());
        // a base the decoder doesn't have, damaged bytes
        realms::GameNetDecoder fresh;
        CHECK(!fresh.decode(u.data(), u.size(), 5, out));
        Bytes cut(u.begin(), u.end() - 3);
        enc.encode(gameSnapshot(6), 6, 5, u, r);
        CHECK(!dec.decode(cut.data(), cut.size(), 6, out));
        CHECK(dec.decode(u.data(), u.size(), 6, out));
        CHECK(!dec.reliable(u.data(), u.size()));
        // the game stopping
        realms::GameSnapshot stopped;
        enc.encode(stopped, 7, 6, u, r);
        CHECK(dec.decode(u.data(), u.size(), 7, out) && !out.running);
        std::printf("[net] snapshot bytes: first %zu, player only %zu, new frame %zu\n", first, playerBytes, frameBytes);
    }

    // then over a bad link: server -> client snapshots, client -> server acks
    realms::NetChannel server(9), client(9);
    Link down{{4242}, 20, 5, 25}, up{{777}, 20, 5, 25};
    realms::GameNetEncoder enc;
    realms::GameNetDecoder dec;
    realms::GameClient direct, remote;
    std::vector<realms::GameEvent> sentEvents, gotEvents;
    std::vector<Bytes> out;
    realms::NetMessage msg;
    Bytes u, r;
    int decoded = 0, failed = 0;
    size_t maxBytes = 0;
    const int frames = 900;
    for (int i = 0; i < frames + 60; i++) {
        const double now = i / 30.0;
        if (i < frames) {
            realms::GameSnapshot s = gameSnapshot(i);
            for (const auto &e : s.events) sentEvents.push_back(e);
            direct.apply(s);
            enc.encode(s, server.outgoingSequence() + 1, server.incomingAcknowledged(), u, r);
            if (!r.empty()) CHECK(server.sendReliable(r));
            maxBytes = std::max(maxBytes, u.size());
        } else if (i == frames) {
            down.lossPercent = up.lossPercent = 0; // settle on a clean link, the last state again
        }
        if (i >= frames) {
            realms::GameSnapshot quiet = gameSnapshot(frames - 1); // nothing new
            quiet.entitiesChanged = quiet.inventoryChanged = false;
            quiet.entities.clear();
            quiet.beams.clear();
            quiet.loopSounds.clear();
            quiet.configStrings.clear();
            quiet.events.clear();
            enc.encode(quiet, server.outgoingSequence() + 1, server.incomingAcknowledged(), u, r);
        }
        server.transmit(u, out, now);
        down.send(out);
        client.transmit({}, out, now);
        up.send(out);
        for (const Bytes &d : down.deliver()) {
            if (!client.process(d.data(), d.size(), msg, now)) continue;
            for (const Bytes &rel : msg.reliable) CHECK(dec.reliable(rel.data(), rel.size()));
            realms::GameSnapshot s;
            if (dec.decode(msg.unreliable.data(), msg.unreliable.size(), msg.sequence, s)) {
                decoded++;
                remote.apply(std::move(s));
                for (auto &e : remote.takeEvents()) gotEvents.push_back(std::move(e));
            } else {
                failed++;
            }
        }
        for (const Bytes &d : up.deliver()) server.process(d.data(), d.size(), msg, now);
    }
    direct.takeEvents();
    CHECK(failed == 0);
    CHECK(decoded > frames / 2 && decoded < frames + 60);
    CHECK(sameClient(direct, remote));
    CHECK(remote.configString(Q2_CS_ITEMS + 399) == "item number 399" && remote.configString(Q2_CS_NAME) == "The Arena");
    bool sameEvents = gotEvents.size() == sentEvents.size();
    for (size_t k = 0; sameEvents && k < gotEvents.size(); k++)
        sameEvents = gotEvents[k].kind == sentEvents[k].kind && gotEvents[k].text == sentEvents[k].text;
    CHECK(sameEvents);
    CHECK(remote.serverFrames() <= direct.serverFrames());
    std::printf("[net] snapshots: %d of %d decoded over the bad link, %zu events, largest %zu bytes\n", decoded,
                frames + 60, gotEvents.size(), maxBytes);
}

} // namespace

int main() {
    addressChecks();
    udpChecks();
    channelBasics();
    channelOverBadLink();
    snapshotChecks();
    std::printf("[net] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
