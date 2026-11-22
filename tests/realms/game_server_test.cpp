// Realms: the Quake 2 game module (game/, Free Pascal) hosted by the C++
// realms::GameServer on realms::Engine.
//
//   realms_game_server_test --game librealms_game.so [--pascal librealms_core.so]
//
// 1. Q2Pmove (qcommon/pmove.c) straight on Engine traces: standing, walking,
//    stepping, walls, jumping, water - no game module needed.
// 2. The game module: GetGameAPI / Init / SpawnEntities on a generated
//    arena, then the local player walks (ClientThink -> gi.Pmove), picks up
//    an armor shard (touch -> gi.sound), opens a func_door (trigger field ->
//    door movement through SV_LinkEdict / gi.trace), runs client commands,
//    and the server restarts on the same map.
// Every check runs on the core the engine loads (C++ or Free Pascal).
// Without --game (and no librealms_game on the search path) part 2 is
// skipped with exit code 77.
#include "bsp_builder.h"

#include "realms/Engine.h"
#include "realms/GameServer.h"
#include "realms/MapGeometry.h"
#include "realms/Q2Pmove.h"
#include "realms/q2game.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

namespace fs = std::filesystem;

// 1024 x 1024 room, floor top z = 0, ceiling at 256. A 16 unit step and a
// 40 unit block for Pmove, a water pool, a door (*1) across the middle, an
// armor shard on the way to it, the player start at -320 0 facing +X.
bsp_builder::MapDef arena() {
    bsp_builder::MapDef def;
    def.texinfo = {{"e1u1/floor", 0, 0}, {"e1u1/wall", 0, 0}, {"e1u1/water", REALMS_SURF_WARP, 0},
                   {"e1u1/door", 0, 0}};
    def.world.brushes = {
        {{-512, -512, -64}, {512, 512, 0}, REALMS_CONTENTS_SOLID, 0},   // floor
        {{-528, -512, 0}, {-512, 512, 256}, REALMS_CONTENTS_SOLID, 1},  // walls
        {{512, -512, 0}, {528, 512, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, -528, 0}, {528, -512, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, 512, 0}, {528, 528, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, -528, 256}, {528, 528, 272}, REALMS_CONTENTS_SOLID, 1}, // ceiling
        {{-160, 256, 0}, {-96, 384, 16}, REALMS_CONTENTS_SOLID, 0},     // step
        {{-32, 256, 0}, {32, 384, 40}, REALMS_CONTENTS_SOLID, 1},       // block
        {{-448, -448, 0}, {-320, -320, 64}, REALMS_CONTENTS_WATER, 2},  // pool
    };
    bsp_builder::ModelDef door;
    door.brushes = {{{0, -64, 0}, {16, 64, 128}, REALMS_CONTENTS_SOLID, 3}};
    def.inlineModels.push_back(door);
    def.entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Realms arena\"\n}\n"
                   "{\n\"classname\" \"info_player_start\"\n\"origin\" \"-320 0 24\"\n\"angle\" \"0\"\n}\n"
                   "{\n\"classname\" \"item_armor_shard\"\n\"origin\" \"-160 0 16\"\n}\n"
                   "{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n\"angle\" \"-1\"\n\"speed\" \"200\"\n}\n";
    return def;
}

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// ------------------------------------------------------------ 1. Pmove

realms::Engine *gPmEngine = nullptr;

q2trace_t pmTrace(q2vec3_t start, q2vec3_t mins, q2vec3_t maxs, q2vec3_t end) {
    const realms::TraceResult tr = gPmEngine->boxTrace({start[0], start[1], start[2]}, {end[0], end[1], end[2]},
                                                       {mins[0], mins[1], mins[2]}, {maxs[0], maxs[1], maxs[2]}, 0,
                                                       REALMS_MASK_PLAYERSOLID);
    static q2csurface_t surf{};
    q2trace_t t{};
    t.allsolid = tr.allsolid;
    t.startsolid = tr.startsolid;
    t.fraction = tr.fraction;
    for (int i = 0; i < 3; i++) {
        t.endpos[i] = tr.endpos[size_t(i)];
        t.plane.normal[i] = tr.normal[size_t(i)];
    }
    t.plane.dist = tr.planeDist;
    t.surface = &surf;
    t.contents = tr.contents;
    static char world; // any non-null "entity" means: hit the world
    t.ent = tr.fraction < 1.0f || tr.startsolid ? reinterpret_cast<q2edict_t *>(&world) : nullptr;
    return t;
}

int32_t pmContents(q2vec3_t p) { return gPmEngine->pointContents({p[0], p[1], p[2]}); }

struct PmPlayer {
    q2pmove_t pm{};
    PmPlayer(float x, float y, float z) {
        pm.s.pm_type = Q2_PM_NORMAL;
        pm.s.gravity = 800;
        pm.s.origin[0] = int16_t(x * 8);
        pm.s.origin[1] = int16_t(y * 8);
        pm.s.origin[2] = int16_t(z * 8);
        pm.trace = &pmTrace;
        pm.pointcontents = &pmContents;
    }
    void move(int frames, float forward, float yaw, float up = 0, int msec = 25) {
        for (int i = 0; i < frames; i++) {
            pm.cmd = q2usercmd_t{};
            pm.cmd.msec = uint8_t(msec);
            pm.cmd.forwardmove = int16_t(forward);
            pm.cmd.upmove = int16_t(up);
            pm.cmd.angles[1] = int16_t(realms::q2::angleToShort(yaw));
            realms::q2::Pmove(&pm);
        }
    }
    float x() const { return pm.s.origin[0] * 0.125f; }
    float y() const { return pm.s.origin[1] * 0.125f; }
    float z() const { return pm.s.origin[2] * 0.125f; }
};

void runPmoveChecks(realms::Engine &engine) {
    gPmEngine = &engine;
    // standing on the floor: mins z -24, so origin z settles at 24
    {
        PmPlayer p(-320, 0, 40);
        p.move(40, 0, 0);
        CHECK(near(p.z(), 24, 0.25f));
        CHECK(p.pm.groundentity != nullptr);
        CHECK(p.pm.s.pm_flags & Q2_PMF_ON_GROUND);
        CHECK(p.pm.viewheight == 22);
        CHECK(p.pm.waterlevel == 0);
    }
    // walking +X at run speed: 300 u/s cap (sv maxspeed), 1 second
    {
        PmPlayer p(-320, 0, 25);
        p.move(40, 400, 0);
        CHECK(p.x() > -320 + 250 && p.x() < -320 + 305);
        CHECK(near(p.y(), 0, 0.5f));
        CHECK(near(p.pm.s.velocity[0] * 0.125f, 300, 1.0f));
        // walk into the +X wall: stops 16 units (half the hull) before x = 512
        p.move(200, 400, 0);
        CHECK(near(p.x(), 512 - 16, 0.25f));
        // turn around (yaw 180) and walk back
        p.move(20, 400, 180);
        CHECK(p.x() < 512 - 50);
    }
    // a 16 unit step is climbed (STEPSIZE 18), a 40 unit block is not
    {
        PmPlayer p(-128, 200, 25);
        p.move(14, 400, 90);
        CHECK(near(p.z(), 40, 0.25f));     // on top of the step
        PmPlayer b(0, 200, 25);
        b.move(60, 400, 90);
        CHECK(near(b.z(), 24, 0.25f));     // still on the floor
        CHECK(near(b.y(), 256 - 16, 0.25f)); // blocked by the block's face
    }
    // jumping: +270 u/s, gravity 800 brings the player back down
    {
        PmPlayer p(-320, 100, 25);
        p.move(4, 0, 0);
        p.move(1, 0, 0, 400);
        CHECK(p.pm.groundentity == nullptr);
        float top = p.z();
        for (int i = 0; i < 20; i++) {
            p.move(1, 0, 0, 400);
            top = std::max(top, p.z());
        }
        CHECK(top > 24 + 40 && top < 24 + 50); // v^2 / 2g = 45.6
        p.move(40, 0, 0, 0);
        CHECK(near(p.z(), 24, 0.25f));
        CHECK(p.pm.groundentity != nullptr);
        // holding jump doesn't jump again (PMF_JUMP_HELD)
        p.move(1, 0, 0, 400);
        p.move(30, 0, 0, 400);
        CHECK(near(p.z(), 24, 0.25f));
    }
    // ducking: viewheight -2, maxs z 4
    {
        PmPlayer p(-320, 100, 25);
        p.move(4, 0, 0);
        p.move(2, 0, 0, -400);
        CHECK(p.pm.s.pm_flags & Q2_PMF_DUCKED);
        CHECK(p.pm.viewheight == -2 && near(p.pm.maxs[2], 4, 0.01f));
        p.move(2, 0, 0, 0);
        CHECK(!(p.pm.s.pm_flags & Q2_PMF_DUCKED));
    }
    // water: standing in the 64 deep pool
    {
        PmPlayer p(-384, -384, 25);
        p.move(4, 0, 0);
        CHECK(p.pm.waterlevel == 3); // eyes (z 46) under the 64 unit surface
        CHECK(p.pm.watertype & REALMS_CONTENTS_WATER);
    }
    // spectators fly through walls
    {
        PmPlayer p(400, 0, 100);
        p.pm.s.pm_type = Q2_PM_SPECTATOR;
        p.move(40, 400, 0);
        CHECK(p.x() > 512);
    }
    gPmEngine = nullptr;
}

// ------------------------------------------------------------ 2. the game

bool hasEvent(const std::vector<realms::GameEvent> &events, realms::GameEvent::Kind kind, const std::string &needle) {
    for (const auto &e : events)
        if (e.kind == kind && e.text.find(needle) != std::string::npos) return true;
    return false;
}

const q2edict_t *findByModel(const realms::GameServer &server, const std::string &model) {
    for (int i = 1; i < server.numEdicts(); i++) {
        const q2edict_t *e = server.edict(i);
        if (e && e->inuse && server.modelName(e->s.modelindex) == model) return e;
    }
    return nullptr;
}

void runGameChecks(realms::Engine &engine, const std::string &gameLibrary) {
    realms::GameServer server;
    realms::GameServerConfig gcfg;
    gcfg.gameLibrary = gameLibrary;
    gcfg.playerName = "Tester";
    std::string error;
    const bool started = server.start(engine, gcfg, error);
    if (!started) std::fprintf(stderr, "  game start failed: %s\n", error.c_str());
    CHECK(started);
    if (!started) return;
    std::printf("[game] module: %s\n", server.moduleOrigin().c_str());
    CHECK(server.running());
    CHECK(server.mapName() == "arena");
    CHECK(server.configString(Q2_CS_MODELS + 1) == "maps/arena.bsp");
    CHECK(server.configString(Q2_CS_MODELS + 2) == "*1");
    CHECK(server.configString(Q2_CS_NAME) == "Realms arena");
    CHECK(server.configString(Q2_CS_MAXCLIENTS) == "1");
    CHECK(server.frameNumber() == 2);

    // the game's edicts: world, the player, the shard, the door, its trigger...
    CHECK(server.numEdicts() > 4);
    const q2edict_t *world = server.edict(0);
    CHECK(world && world->inuse && world->solid == Q2_SOLID_BSP);
    const q2edict_t *player = server.player();
    CHECK(player && player->inuse && player->client);
    if (!player || !player->client) return;
    CHECK(player->s.number == 1);
    CHECK(player->solid == Q2_SOLID_BBOX);
    CHECK(player->svflags == 0 || !(player->svflags & Q2_SVF_NOCLIENT));
    CHECK(player->s.modelindex == 255); // "use the skin specified model"
    CHECK(player->linkcount > 0 && player->area.prev != nullptr);
    CHECK(player->client->ps.stats[Q2_STAT_HEALTH] == 100);
    CHECK(near(player->client->ps.fov, 90, 0.01f));
    CHECK(near(server.playerOrigin()[0], -320, 0.2f) && near(server.playerOrigin()[1], 0, 0.2f));
    CHECK(near(server.playerOrigin()[2], 24 + 9 + 1, 0.2f)); // SelectSpawnPoint +9, PutClientInServer +1
    // CS_PLAYERSKINS: "name\model/skin"
    CHECK(server.configString(Q2_CS_PLAYERSKINS) == "Tester\\male/grunt");

    const q2edict_t *door = findByModel(server, "*1");
    CHECK(door && door->solid == Q2_SOLID_BSP);
    const q2edict_t *shard = findByModel(server, "models/items/armor/shard/tris.md2");
    CHECK(shard && shard->solid == Q2_SOLID_TRIGGER);
    // droptofloor put the shard on the floor (mins z -15)
    if (shard) CHECK(near(shard->s.origin[2], 15, 0.2f));

    // gi.trace from the server side: down onto the floor, blocked by the door
    {
        float s[3] = {-200, 0, 100}, e[3] = {-200, 0, -100}, mn[3] = {-16, -16, -24}, mx[3] = {16, 16, 32};
        q2trace_t tr = server.trace(s, mn, mx, e, nullptr, REALMS_MASK_PLAYERSOLID);
        CHECK(near(tr.endpos[2], 24, 0.1f) && tr.ent == world && near(tr.plane.normal[2], 1, 1e-4f));
        CHECK(tr.surface && std::string(tr.surface->name) == "e1u1/floor");
        float s2[3] = {-100, 0, 64}, e2[3] = {100, 0, 64};
        tr = server.trace(s2, mn, mx, e2, nullptr, REALMS_MASK_PLAYERSOLID);
        CHECK(door && tr.ent == door && near(tr.endpos[0], -16, 0.1f));
        float pt[3] = {8, 0, 64};
        CHECK(server.pointContents(pt) & REALMS_CONTENTS_SOLID);
        float wet[3] = {-384, -384, 32};
        CHECK(server.pointContents(wet) & REALMS_CONTENTS_WATER);
        q2edict_t *list[64];
        float bmn[3] = {-400, -100, 0}, bmx[3] = {-240, 100, 64};
        CHECK(server.areaEdicts(bmn, bmx, list, 64, Q2_AREA_SOLID) == 1 && list[0] == player);
        float tmn[3] = {-200, -40, 0}, tmx[3] = {-120, 40, 64};
        CHECK(server.areaEdicts(tmn, tmx, list, 64, Q2_AREA_TRIGGERS) >= 1);
    }
    server.takeEvents();

    // standing still for a second: on the floor, the game advanced 10 frames
    realms::GameUserCmd idle;
    for (int i = 0; i < 20; i++) server.frame(idle, 0.05);
    CHECK(server.frameNumber() == 12);
    CHECK(near(server.playerOrigin()[2], 24.125f, 0.01f)); // snapped off the floor like Quake 2
    CHECK(near(server.viewOrigin()[2], 24.125f + 22, 0.01f));
    CHECK(player->client->ps.pmove.pm_flags & Q2_PMF_ON_GROUND);

    // walk +X: pick up the shard on the way (touch -> sound, freed)
    realms::GameUserCmd walk;
    walk.forward = 400;
    walk.yaw = 0;
    std::vector<realms::GameEvent> events;
    for (int i = 0; i < 12; i++) {
        server.frame(walk, 0.05);
        auto ev = server.takeEvents();
        events.insert(events.end(), ev.begin(), ev.end());
    }
    CHECK(server.playerOrigin()[0] > -160);
    CHECK(hasEvent(events, realms::GameEvent::Kind::Sound, "sound/misc/ar2_pkup.wav"));
    CHECK(findByModel(server, "models/items/armor/shard/tris.md2") == nullptr);
    CHECK(player->client->ps.stats[Q2_STAT_ARMOR] == 2);
    CHECK(player->client->ps.stats[Q2_STAT_PICKUP_STRING] != 0);
    CHECK(server.configString(player->client->ps.stats[Q2_STAT_PICKUP_STRING]) == "Armor Shard");

    // keep walking into the door: its trigger field opens it, the door rises
    // (gi.linkentity every frame) and the player walks through
    const float doorZ0 = door ? door->s.origin[2] : 0;
    events.clear();
    for (int i = 0; i < 40; i++) {
        server.frame(walk, 0.05);
        auto ev = server.takeEvents();
        events.insert(events.end(), ev.begin(), ev.end());
    }
    CHECK(door && door->s.origin[2] > doorZ0 + 60);
    CHECK(hasEvent(events, realms::GameEvent::Kind::Sound, "sound/doors/dr1_strt.wav"));
    CHECK(server.playerOrigin()[0] > 100);

    // client commands through gi.argv / gi.cprintf
    server.clientCommand("god");
    events = server.takeEvents();
    CHECK(hasEvent(events, realms::GameEvent::Kind::Print, "godmode ON"));
    server.clientCommand("give shotgun");
    server.clientCommand("use shotgun");
    for (int i = 0; i < 20; i++) server.frame(idle, 0.05);
    CHECK(server.modelName(player->client->ps.gunindex) == "models/weapons/v_shotg/tris.md2");
    // console commands registered on the engine forward to the game
    CHECK(engine.commandExists("give") && engine.commandExists("sv") && engine.commandExists("cmd"));
    engine.exec("notarget\n");
    events = server.takeEvents();
    CHECK(hasEvent(events, realms::GameEvent::Kind::Print, "notarget ON"));
    // firing: muzzle flash and the shotgun sound
    realms::GameUserCmd fire;
    fire.attack = true;
    events.clear();
    for (int i = 0; i < 6; i++) {
        server.frame(fire, 0.05);
        auto ev = server.takeEvents();
        events.insert(events.end(), ev.begin(), ev.end());
    }
    bool flash = false;
    for (const auto &e : events) flash |= e.kind == realms::GameEvent::Kind::MuzzleFlash && e.entity == 1;
    CHECK(flash);
    CHECK(player->client->ps.stats[Q2_STAT_AMMO] < 10);
    // the game's cvars mirror the engine's
    engine.setCvar("skill", "3");
    CHECK(engine.cvarString("skill") == "1" || engine.cvarString("skill") == "3"); // latched

    // restart on the same map: Shutdown, GetGameAPI, Init, SpawnEntities
    server.stop();
    CHECK(!server.running());
    CHECK(!engine.commandExists("cmd"));
    const bool restarted = server.start(engine, gcfg, error);
    if (!restarted) std::fprintf(stderr, "  game restart failed: %s\n", error.c_str());
    CHECK(restarted);
    if (restarted) {
        CHECK(server.player() && server.player()->client && server.player()->client->ps.stats[Q2_STAT_ARMOR] == 0);
        CHECK(findByModel(server, "models/items/armor/shard/tris.md2") != nullptr);
        CHECK(server.frame(idle, 0.5) == 5);
    }
    server.stop();
}

} // namespace

int main(int argc, char **argv) {
    std::string pascalLibrary, gameLibrary;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--pascal" && i + 1 < argc) pascalLibrary = argv[++i];
        else if (a == "--game" && i + 1 < argc) gameLibrary = argv[++i];
    }
    const fs::path tmp = fs::temp_directory_path() /
        ("realms-game-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         (pascalLibrary.empty() ? "native" : "pascal"));
    std::error_code ec;
    fs::create_directories(tmp / "maps", ec);
    {
        const std::string bsp = bsp_builder::Builder(arena()).build();
        std::ofstream(tmp / "maps" / "arena.bsp", std::ios::binary).write(bsp.data(), std::streamsize(bsp.size()));
    }
    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {tmp.string()};
    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.loadMap("maps/arena.bsp"));
    const std::string backend = engine.backendName();
    std::printf("[game] core: %s (%s)\n", backend.c_str(), engine.backendOrigin().c_str());
    if (!pascalLibrary.empty()) CHECK(engine.pascalBackend());

    runPmoveChecks(engine);

    // Drawable geometry is split by BSP model so the sandbox can move brush
    // entities: the door (*1) is its own batch, everything else is world -
    // from brush windings (this map) and from a face lump (qbsp-style).
    for (const bool faces : {false, true}) {
        realms::MapGeometry g;
        std::string gerr;
        if (faces) {
            auto def = arena();
            def.emitFaces = true;
            const std::string bsp = bsp_builder::Builder(def).build();
            CHECK(realms::bspGeometry(reinterpret_cast<const unsigned char *>(bsp.data()), bsp.size(), g, gerr));
            CHECK(g.source == realms::MapGeometrySource::Faces);
        } else {
            CHECK(realms::loadMapGeometry(engine, g, gerr));
            CHECK(g.source == realms::MapGeometrySource::Brushes);
        }
        bool door = false, misplaced = false, world = false;
        for (const auto &b : g.batches) {
            if (b.material == "e1u1/door") (b.model == 1 ? door : misplaced) = true;
            else if (b.model == 0) world = true;
            else misplaced = true;
        }
        CHECK(door && world && !misplaced);
    }

    std::string where, why;
    if (!gameLibrary.empty() || realms::GameServer::moduleAvailable({}, &where, &why)) {
        runGameChecks(engine, gameLibrary);
    } else {
        std::printf("[game] %d checks, %d failures; game module skipped: %s\n", gChecks, gFailures, why.c_str());
        engine.shutdown();
        fs::remove_all(tmp, ec);
        return gFailures ? 1 : 77;
    }
    engine.shutdown();
    fs::remove_all(tmp, ec);
    std::printf("[game] %d checks, %d failures (%s)\n", gChecks, gFailures, backend.c_str());
    return gFailures == 0 ? 0 : 1;
}
