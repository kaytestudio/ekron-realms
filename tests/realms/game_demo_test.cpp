// Ekron demo files (realms/GameDemo.h): snapshots in, the same snapshots
// out; changes-only encoding; cut-short and damaged files; names and paths;
// a GameClient fed from a demo ends up like one fed directly. No game
// module needed.
#include "realms/Engine.h"
#include "realms/GameClient.h"
#include "realms/GameDemo.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

namespace fs = std::filesystem;

realms::GameRenderEntity entity(int number, const std::string &model, float x) {
    realms::GameRenderEntity e;
    e.number = number;
    e.modelindex = number + 1;
    e.model = model;
    e.inlineModel = model.size() > 1 && model[0] == '*' ? std::atoi(model.c_str() + 1) : 0;
    e.origin = {x, -2.5f, 24.0f};
    e.oldOrigin = {x - 1.0f, -2.5f, 24.0f};
    e.angles = {0.0f, 90.0f, 0.0f};
    e.mins = {-16, -16, -24};
    e.maxs = {16, 16, 32};
    e.frame = 3;
    e.skinnum = 1;
    e.effects = 0x80000000u | 4u;
    e.renderfx = 2;
    e.svflags = 0;
    e.solid = 2;
    e.sound = number == 2 ? "world/amb10.wav" : "";
    if (number == 7) { // linked models: a player's weapon and another
        e.modelindex2 = 255;
        e.modelindex3 = 5;
    }
    return e;
}

// A first snapshot with everything, as GameServer::snapshot(full) makes them.
realms::GameSnapshot fullSnapshot() {
    realms::GameSnapshot s;
    s.running = true;
    s.frameNumber = 12;
    s.lerpFraction = 0.25;
    s.hasPlayer = true;
    s.player.pmove.pm_type = Q2_PM_NORMAL;
    s.player.pmove.origin[0] = -2560; // 12.3 fixed point: -320 units
    s.player.pmove.origin[2] = 272;
    s.player.pmove.velocity[1] = -8;
    s.player.pmove.pm_flags = Q2_PMF_ON_GROUND;
    s.player.pmove.gravity = 800;
    s.player.pmove.delta_angles[1] = -1234;
    s.player.viewangles[0] = 10.5f;
    s.player.viewangles[1] = -45.0f;
    s.player.viewoffset[2] = 22.0f;
    s.player.kick_angles[0] = -2.0f;
    s.player.gunindex = 5;
    s.player.gunframe = 7;
    s.player.blend[3] = 0.3f;
    s.player.fov = 90.0f;
    s.player.rdflags = 1;
    s.player.stats[Q2_STAT_HEALTH] = 100;
    s.player.stats[Q2_STAT_AMMO] = -1;
    s.playerOrigin = {-320.125f, 0.0f, 34.0f};
    s.entitiesChanged = true;
    s.entities = {entity(1, "*1", 0.0f), entity(2, "models/items/armor/shard/tris.md2", -160.0f),
                  entity(7, "models/objects/rocket/tris.md2", 50.0f)};
    realms::GameBeamEntity beam;
    beam.number = 9;
    beam.origin = {1, 2, 3};
    beam.oldOrigin = {4, 5, 6};
    beam.frame = 4;
    beam.skinnum = int(0xf2f2f0f0u);
    s.beams = {beam};
    s.loopSounds = {{2, "sound/world/amb10.wav", {-160, 0, 15}}};
    s.configStrings = {{Q2_CS_NAME, "The Arena"}, {Q2_CS_CDTRACK, "7"}, {Q2_CS_MODELS + 1, "maps/arena.bsp"},
                       {Q2_CS_MODELS + 2, "*1"}, {Q2_CS_LIGHTS + 0, "m"}};
    s.inventoryChanged = true;
    s.inventory[7] = 1;
    s.inventory[18] = 50;
    realms::GameEvent sound;
    sound.kind = realms::GameEvent::Kind::Sound;
    sound.level = 2;
    sound.entity = 1;
    sound.origin = {1.5f, 2.5f, 3.5f};
    sound.volume = 0.5f;
    sound.attenuation = 3.0f;
    sound.delay = 0.1f;
    sound.text = "sound/items/pkup.wav";
    realms::GameEvent te;
    te.kind = realms::GameEvent::Kind::TempEntity;
    te.level = 13;
    te.origin = {9, 8, 7};
    te.end = {6, 5, 4};
    te.dir = {0, 0, 1};
    te.count = 40;
    te.color = 0xe0;
    te.durationMs = 1000;
    realms::GameEvent print;
    print.kind = realms::GameEvent::Kind::Print;
    print.level = 2;
    print.text = "You got the Shotgun\n";
    s.events = {sound, te, print};
    return s;
}

bool sameEntity(const realms::GameRenderEntity &a, const realms::GameRenderEntity &b) {
    return a.number == b.number && a.modelindex == b.modelindex && a.model == b.model &&
           a.inlineModel == b.inlineModel && a.origin == b.origin && a.oldOrigin == b.oldOrigin &&
           a.angles == b.angles && a.mins == b.mins && a.maxs == b.maxs && a.frame == b.frame &&
           a.skinnum == b.skinnum && a.effects == b.effects && a.renderfx == b.renderfx && a.svflags == b.svflags &&
           a.solid == b.solid && a.sound == b.sound && a.modelindex2 == b.modelindex2 &&
           a.modelindex3 == b.modelindex3 && a.modelindex4 == b.modelindex4;
}

bool sameEvent(const realms::GameEvent &a, const realms::GameEvent &b) {
    return a.kind == b.kind && a.level == b.level && a.entity == b.entity && a.origin == b.origin &&
           a.volume == b.volume && a.attenuation == b.attenuation && a.delay == b.delay && a.text == b.text &&
           a.entityEvent == b.entityEvent && a.angles == b.angles && a.end == b.end && a.dir == b.dir &&
           a.offset == b.offset && a.count == b.count && a.color == b.color && a.entity2 == b.entity2 &&
           a.magnitude == b.magnitude && a.id == b.id && a.durationMs == b.durationMs;
}

bool samePlayer(const q2player_state_t &a, const q2player_state_t &b) {
    const q2pmove_state_t &p = a.pmove, &q = b.pmove;
    bool same = p.pm_type == q.pm_type && p.pm_flags == q.pm_flags && p.pm_time == q.pm_time &&
                p.gravity == q.gravity && a.gunindex == b.gunindex && a.gunframe == b.gunframe && a.fov == b.fov &&
                a.rdflags == b.rdflags;
    for (int i = 0; i < 3; i++)
        same = same && p.origin[i] == q.origin[i] && p.velocity[i] == q.velocity[i] &&
               p.delta_angles[i] == q.delta_angles[i] && a.viewangles[i] == b.viewangles[i] &&
               a.viewoffset[i] == b.viewoffset[i] && a.kick_angles[i] == b.kick_angles[i] &&
               a.gunangles[i] == b.gunangles[i] && a.gunoffset[i] == b.gunoffset[i];
    for (int i = 0; i < 4; i++) same = same && a.blend[i] == b.blend[i];
    for (int i = 0; i < Q2_MAX_STATS; i++) same = same && a.stats[i] == b.stats[i];
    return same;
}

bool sameSnapshot(const realms::GameSnapshot &a, const realms::GameSnapshot &b) {
    bool same = a.running == b.running && a.frameNumber == b.frameNumber && a.lerpFraction == b.lerpFraction &&
                a.hasPlayer == b.hasPlayer && a.playerOrigin == b.playerOrigin &&
                a.entitiesChanged == b.entitiesChanged && a.entities.size() == b.entities.size() &&
                a.beams.size() == b.beams.size() && a.loopSounds.size() == b.loopSounds.size() &&
                a.configStrings == b.configStrings && a.inventoryChanged == b.inventoryChanged &&
                a.events.size() == b.events.size();
    if (!same) return false;
    if (a.hasPlayer && !samePlayer(a.player, b.player)) return false;
    if (a.inventoryChanged && a.inventory != b.inventory) return false;
    for (size_t i = 0; i < a.entities.size(); i++)
        if (!sameEntity(a.entities[i], b.entities[i])) return false;
    for (size_t i = 0; i < a.beams.size(); i++) {
        const auto &p = a.beams[i], &q = b.beams[i];
        if (p.number != q.number || p.origin != q.origin || p.oldOrigin != q.oldOrigin || p.frame != q.frame ||
            p.skinnum != q.skinnum)
            return false;
    }
    for (size_t i = 0; i < a.loopSounds.size(); i++) {
        const auto &p = a.loopSounds[i], &q = b.loopSounds[i];
        if (p.entity != q.entity || p.name != q.name || p.origin != q.origin) return false;
    }
    for (size_t i = 0; i < a.events.size(); i++)
        if (!sameEvent(a.events[i], b.events[i])) return false;
    return true;
}

// A run of snapshots like a game makes them: the full one, then frames in
// between server frames (player only), server frames with an entity moving,
// one gone and one new, a config string and the inventory changing, and the
// game restarting (running false, then a full one).
std::vector<realms::GameSnapshot> session() {
    std::vector<realms::GameSnapshot> out;
    realms::GameSnapshot s = fullSnapshot();
    out.push_back(s);
    for (int i = 1; i <= 30; i++) {
        realms::GameSnapshot d;
        d.running = true;
        d.frameNumber = 12 + i / 3;
        d.lerpFraction = (i % 3) / 3.0;
        d.hasPlayer = true;
        d.player = s.player;
        d.player.viewangles[1] += float(i);
        d.player.pmove.origin[0] = int16_t(-2560 + 8 * i);
        if (i == 20) d.player.stats[Q2_STAT_HEALTH] = 75;
        s.player = d.player;
        d.playerOrigin = {-320.0f + float(i), 0.0f, 34.0f};
        if (i % 3 == 0) {
            d.entitiesChanged = true;
            d.entities = {entity(1, "*1", float(i)), entity(7, "models/objects/rocket/tris.md2", 50.0f + 20.0f * i)};
            if (i >= 15) d.entities.push_back(entity(30, "models/items/armor/shard/tris.md2", -100.0f));
        }
        if (i == 9) d.configStrings = {{Q2_CS_LIGHTS + 0, "az"}, {Q2_CS_MODELS + 3, "models/weapons/g_shotg/tris.md2"}};
        if (i == 21) {
            d.inventoryChanged = true;
            d.inventory = s.inventory;
            d.inventory[18] = 49;
        }
        if (i == 12) {
            realms::GameEvent ev;
            ev.kind = realms::GameEvent::Kind::Sound;
            ev.text = "sound/items/pkup.wav"; // sent before: by number now
            d.events.push_back(ev);
        }
        out.push_back(d);
    }
    out.push_back(realms::GameSnapshot{}); // the game restarted
    out.push_back(fullSnapshot());
    return out;
}

void roundTrip() {
    const std::vector<realms::GameSnapshot> snaps = session();
    std::vector<uint8_t> file;
    realms::GameDemoHeader header;
    header.mapPath = "maps/arena.bsp";
    header.mapChecksum = 0xdeadbeefu;
    realms::encodeGameDemoHeader(header, file);
    const size_t headerBytes = file.size();
    CHECK(headerBytes == 7 + 1 + 1 + header.mapPath.size() + 4);
    CHECK(std::memcmp(file.data(), "EKRDEMO\x02", 8) == 0);
    realms::GameDemoState state;
    std::vector<size_t> sizes;
    for (size_t i = 0; i < snaps.size(); i++) {
        const size_t before = file.size();
        realms::encodeGameDemoRecord(state, snaps[i], i == 0 ? 0.0f : 1.0f / 60.0f, file);
        sizes.push_back(file.size() - before);
    }
    // changes only: a frame with only the player moving is a few bytes; a
    // server frame with two entities is far smaller than the first record
    CHECK(sizes[1] < 48);
    CHECK(sizes[3] < sizes[0] / 3);
    CHECK(sizes[31] < 16); // running false
    // the restart's full snapshot: its strings are all known by now
    CHECK(sizes[32] < sizes[0]);
    std::printf("[demo] header %zu bytes, first record %zu, player-only %zu, server frame %zu, total %zu\n",
                headerBytes, sizes[0], sizes[1], sizes[3], file.size());

    realms::GameDemoReader reader;
    std::string error;
    CHECK(reader.open(file, error));
    CHECK(reader.header().mapPath == "maps/arena.bsp" && reader.header().mapChecksum == 0xdeadbeefu);
    realms::GameSnapshot got;
    float seconds = -1.0f;
    size_t matched = 0;
    for (size_t i = 0; i < snaps.size(); i++) {
        if (!reader.next(got, seconds)) break;
        if (sameSnapshot(got, snaps[i]) && seconds == (i == 0 ? 0.0f : 1.0f / 60.0f)) matched++;
        else std::fprintf(stderr, "  record %zu differs\n", i);
    }
    CHECK(matched == snaps.size());
    CHECK(!reader.next(got, seconds) && reader.error().empty()); // the end
    CHECK(reader.records() == int(snaps.size()));
    CHECK(std::fabs(reader.time() - 32.0 / 60.0) < 1e-6);

    // a GameClient fed from the demo ends up like one fed directly
    realms::GameClient direct, replayed;
    CHECK(reader.open(file, error));
    for (size_t i = 0; i + 2 < snaps.size(); i++) { // up to the restart
        direct.apply(snaps[i]);
        if (reader.next(got, seconds)) replayed.apply(got);
    }
    CHECK(replayed.running() && replayed.frameNumber() == direct.frameNumber());
    CHECK(replayed.snapshots() == direct.snapshots() && replayed.serverFrames() == direct.serverFrames());
    CHECK(replayed.playerState() && direct.playerState() &&
          samePlayer(*replayed.playerState(), *direct.playerState()));
    CHECK(replayed.playerState() && replayed.playerState()->stats[Q2_STAT_HEALTH] == 75);
    CHECK(replayed.renderEntities(true).size() == 3 && replayed.renderEntities().size() == 2);
    CHECK(replayed.configString(Q2_CS_LIGHTS) == "az" && replayed.configString(Q2_CS_NAME) == "The Arena");
    CHECK(replayed.inventory() == direct.inventory() && replayed.inventory()[18] == 49);
    CHECK(replayed.modelName(2) == "*1" && replayed.inlineModel(2) == 1); // "*1" at CS_MODELS + 2
    CHECK(replayed.takeEvents().size() == direct.takeEvents().size());
    // the restart resets it; the full snapshot brings it back
    if (reader.next(got, seconds)) replayed.apply(got);
    CHECK(!replayed.running() && replayed.configString(Q2_CS_NAME).empty());
    if (reader.next(got, seconds)) replayed.apply(got);
    CHECK(replayed.running() && replayed.configString(Q2_CS_NAME) == "The Arena" && replayed.snapshots() == 1);

    // GameClient::fullSnapshot: everything the client holds, applied to a
    // fresh client, gives the same client (what "record" starts a demo with)
    {
        realms::GameClient c;
        for (size_t i = 0; i + 2 < snaps.size(); i++) c.apply(snaps[i]);
        c.takeEvents();
        const realms::GameSnapshot full = c.fullSnapshot();
        CHECK(full.running && full.entitiesChanged && full.inventoryChanged && full.events.empty());
        CHECK(full.configStrings.size() == 6); // the first 5 (one changed since) + model 3
        realms::GameClient copy;
        copy.apply(full);
        CHECK(copy.frameNumber() == c.frameNumber() && copy.lerpFraction() == c.lerpFraction());
        CHECK(copy.playerState() && samePlayer(*copy.playerState(), *c.playerState()));
        CHECK(copy.renderEntities(true).size() == c.renderEntities(true).size());
        bool same = true;
        for (int i = 0; i < Q2_MAX_CONFIGSTRINGS; i++) same &= copy.configString(i) == c.configString(i);
        CHECK(same && copy.inventory() == c.inventory());
        CHECK(copy.loopSounds().size() == c.loopSounds().size() && copy.beamEntities().size() == c.beamEntities().size());
        CHECK(!realms::GameClient().fullSnapshot().running);
    }

    // cut short while recording: everything up to the last whole record
    for (size_t cut : {file.size() - 1, file.size() - sizes.back() + 2, headerBytes + 3}) {
        std::vector<uint8_t> part(file.begin(), file.begin() + long(cut));
        realms::GameDemoReader r;
        CHECK(r.open(part, error));
        int n = 0;
        while (r.next(got, seconds)) n++;
        CHECK(r.error().empty());
        CHECK(n == (cut == headerBytes + 3 ? 0 : int(snaps.size()) - 1));
    }
    // damaged: a record's contents changed (its flags byte)
    {
        std::vector<uint8_t> bad = file;
        bad[headerBytes + sizes[0] + 4 + 1 + 4] = 0xff; // record 2: no new strings, seconds, then flags
        realms::GameDemoReader r;
        CHECK(r.open(bad, error));
        int n = 0;
        while (r.next(got, seconds)) n++;
        CHECK(n == 1 && r.error() == "damaged record 2");
        // a record length past any sane size
        bad = file;
        bad[headerBytes + 3] = 0x7f;
        CHECK(r.open(bad, error) && !r.next(got, seconds) && r.error() == "damaged record 1");
    }
    // not a demo, another version, no map
    {
        realms::GameDemoReader r;
        realms::GameDemoHeader h;
        std::vector<uint8_t> bad = file;
        bad[0] = 'X';
        CHECK(!r.open(bad, error) && error == "not an Ekron demo");
        CHECK(!realms::GameDemoReader::readHeader({}, h, error) && error == "not an Ekron demo");
        bad = file;
        bad[7] = 3;
        CHECK(!r.open(bad, error) && error == "demo version 3 (this build plays versions 1 to 2)");
        bad[7] = 0;
        CHECK(!r.open(bad, error) && error == "demo version 0 (this build plays versions 1 to 2)");
        CHECK(realms::GameDemoReader::readHeader(file, h, error) && h.version == 2);
        std::vector<uint8_t> empty;
        realms::encodeGameDemoHeader(realms::GameDemoHeader{}, empty);
        CHECK(!realms::GameDemoReader::readHeader(empty, h, error) && error == "damaged demo header");
        CHECK(realms::GameDemoReader::readHeader(file, h, error) && h.mapPath == "maps/arena.bsp");
    }
}

// Version 1 demos (before the linked models) still play: the same
// records without modelindex2..4, which read back as 0.
void versionOne() {
    const std::vector<realms::GameSnapshot> snaps = session();
    std::vector<uint8_t> file;
    realms::GameDemoHeader header;
    header.mapPath = "maps/arena.bsp";
    realms::encodeGameDemoHeader(header, file);
    file[7] = 1;
    const size_t headerBytes = file.size();
    realms::GameDemoState state;
    state.version = 1;
    std::vector<uint8_t> v2;
    realms::GameDemoState state2;
    for (size_t i = 0; i < snaps.size(); i++) {
        realms::encodeGameDemoRecord(state, snaps[i], 0.0f, file);
        realms::encodeGameDemoRecord(state2, snaps[i], 0.0f, v2);
    }
    CHECK(file.size() - headerBytes < v2.size()); // without the three words
    realms::GameDemoReader reader;
    std::string error;
    CHECK(reader.open(file, error) && reader.header().version == 1);
    realms::GameSnapshot got;
    float seconds = 0.0f;
    size_t matched = 0, linked = 0;
    for (size_t i = 0; i < snaps.size() && reader.next(got, seconds); i++) {
        realms::GameSnapshot want = snaps[i];
        for (auto &e : want.entities) {
            linked += e.modelindex2 != 0;
            e.modelindex2 = e.modelindex3 = e.modelindex4 = 0;
        }
        if (sameSnapshot(got, want)) matched++;
        else std::fprintf(stderr, "  version 1 record %zu differs\n", i);
    }
    CHECK(matched == snaps.size() && linked > 0 && reader.error().empty());
}

void namesAndFiles() {
    CHECK(realms::gameDemoFile("demo1") == "demos/demo1.ekd");
    CHECK(realms::gameDemoFile("demo1.ekd") == "demos/demo1.ekd");
    CHECK(realms::gameDemoFile("Speed_Run-2") == "demos/Speed_Run-2.ekd");
    CHECK(realms::gameDemoFile("").empty() && realms::gameDemoFile(".ekd").empty());
    CHECK(realms::gameDemoFile("../x").empty() && realms::gameDemoFile("a/b").empty());
    CHECK(realms::gameDemoFile("a b").empty() && realms::gameDemoFile(std::string(33, 'a')).empty());

    // no writable folder: nowhere to record (one engine at a time)
    {
        realms::Engine readOnly;
        realms::EngineConfig ro;
        ro.backend = realms::BackendPreference::Native;
        CHECK(readOnly.init(ro));
        CHECK(realms::gameDemoPath(readOnly, "run1").empty());
        readOnly.shutdown();
    }
    const fs::path tmp = fs::temp_directory_path() /
                         ("realms-demo-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    realms::EngineConfig cfg;
    cfg.backend = realms::BackendPreference::Native;
    cfg.writableRoot = tmp.string();
    realms::Engine engine;
    CHECK(engine.init(cfg));
    const std::string path = realms::gameDemoPath(engine, "run1");
    CHECK(fs::path(path) == tmp / "demos" / "run1.ekd");
    CHECK(realms::gameDemoPath(engine, "../run1").empty());

    // the writer makes the folder; the engine finds the file by its VFS name
    const std::vector<realms::GameSnapshot> snaps = session();
    {
        realms::GameDemoWriter w;
        realms::GameDemoHeader header;
        header.mapPath = "maps/arena.bsp";
        std::string error;
        CHECK(w.open(path, header, error) && w.isOpen() && w.path() == path);
        for (const auto &s : snaps) CHECK(w.write(s, 0.05f));
        CHECK(w.records() == int(snaps.size()) && w.bytes() == fs::file_size(path));
        w.close();
        CHECK(!w.isOpen() && !w.write(snaps[0], 0.05f));
        CHECK(!w.open((tmp / "demos" / "run1.ekd" / "inside.ekd").string(), header, error) &&
              error.find("can't write") == 0);
    }
    std::vector<unsigned char> bytes;
    CHECK(engine.loadFile(realms::gameDemoFile("run1"), bytes));
    realms::GameDemoReader r;
    std::string error;
    CHECK(r.open(std::vector<uint8_t>(bytes.begin(), bytes.end()), error));
    realms::GameSnapshot got;
    float seconds = 0;
    int n = 0;
    while (r.next(got, seconds)) n++;
    CHECK(n == int(snaps.size()) && r.error().empty() && std::fabs(r.time() - 0.05 * n) < 1e-5);
    engine.shutdown();
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

} // namespace

int main() {
    roundTrip();
    versionOne();
    namesAndFiles();
    std::printf("[demo] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
