// Realms Engine bridge conformance suite.
//
// Runs the same Quake 2 cvar / command buffer / alias / PAK filesystem / BSP
// collision model (realms_map_suite.h) checks
// against a RealmsCoreApi implementation:
//   realms_bridge_test                      -> C++ reference core
//   realms_bridge_test --pascal <library>   -> Free Pascal core (dlopen)
// Built with -DREALMS_PASCAL_STATIC and linked against librealms_core.a (the
// iOS packaging), `--pascal static` uses the statically linked Pascal core.
// and then drives the realms::Engine facade on top of the same backend.

#include "realms/Engine.h"
#include "realms/SandboxBoot.h"
#include "realms/realms_bridge.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#if defined(REALMS_PASCAL_STATIC)
extern "C" const RealmsCoreApi *realms_pascal_get_api(uint32_t requested_version);
#elif !defined(_WIN32)
#include <dlfcn.h>
#endif

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

// ------------------------------------------------------------ fake host ---

struct FakeHost {
    std::map<std::string, std::string> files;
    std::map<std::string, std::string> written;
    std::vector<std::string> lines;
    std::vector<std::vector<std::string>> commands;
    bool rangeReads = true;
    int fullReads = 0;
    int rangeReadCalls = 0;
};

FakeHost gHost;

void hostPrint(void *, int32_t, const char *text) { gHost.lines.emplace_back(text ? text : ""); }

int32_t hostRead(void *, const char *path, void **out, int32_t *size) {
    ++gHost.fullReads;
    auto it = gHost.files.find(path);
    if (it == gHost.files.end()) return 0;
    *size = int32_t(it->second.size());
    *out = std::malloc(it->second.size() + 1);
    std::memcpy(*out, it->second.data(), it->second.size());
    return 1;
}

void hostFree(void *, void *data) { std::free(data); }

int32_t hostRange(void *, const char *path, int64_t offset, int32_t size, void *out) {
    ++gHost.rangeReadCalls;
    auto it = gHost.files.find(path);
    if (it == gHost.files.end() || offset < 0 || size_t(offset) > it->second.size()) return -1;
    const size_t n = std::min(size_t(size), it->second.size() - size_t(offset));
    std::memcpy(out, it->second.data() + offset, n);
    return int32_t(n);
}

int32_t hostWrite(void *, const char *path, const void *data, int32_t size) {
    gHost.written[path] = std::string(static_cast<const char *>(data), size_t(size));
    return 1;
}

void hostCommand(void *, int32_t argc, const char *const *argv) {
    std::vector<std::string> args;
    for (int32_t i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    gHost.commands.push_back(args);
}

RealmsHostApi makeHost(bool rangeReads) {
    RealmsHostApi h{};
    h.struct_size = sizeof(RealmsHostApi);
    h.version = REALMS_BRIDGE_VERSION;
    h.print = hostPrint;
    h.read_file = hostRead;
    h.free_file = hostFree;
    h.read_file_range = rangeReads ? hostRange : nullptr;
    h.write_file = hostWrite;
    h.command = hostCommand;
    return h;
}

bool sawLine(const std::string &needle) {
    for (const auto &l : gHost.lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

std::string get(const RealmsCoreApi *api, const char *name) {
    char buf[512];
    return api->cvar_get(name, buf, int32_t(sizeof(buf))) < 0 ? std::string("<missing>") : std::string(buf);
}

void put32(std::string &s, int32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((uint32_t(v) >> (8 * i)) & 0xff));
}

// Builds a Quake 2 PAK: "PACK", dirofs, dirlen, data..., 64-byte entries.
std::string makePak(const std::vector<std::pair<std::string, std::string>> &entries) {
    std::string data;
    std::vector<std::pair<int32_t, int32_t>> spans;
    for (const auto &e : entries) {
        spans.emplace_back(int32_t(12 + data.size()), int32_t(e.second.size()));
        data += e.second;
    }
    std::string pak = "PACK";
    put32(pak, int32_t(12 + data.size()));
    put32(pak, int32_t(entries.size() * 64));
    pak += data;
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string name = entries[i].first;
        name.resize(56, '\0');
        pak += name;
        put32(pak, spans[i].first);
        put32(pak, spans[i].second);
    }
    return pak;
}

} // namespace

#include "realms_map_suite.h"
#include "realms_meshmap_suite.h"
#include "realms_walker_suite.h"

namespace {

// ------------------------------------------------------------ ABI suite ---

void runAbiSuite(const RealmsCoreApi *api, bool rangeReads) {
    gHost = FakeHost{};
    gHost.rangeReads = rangeReads;
    const RealmsHostApi host = makeHost(rangeReads);

    CHECK(api->struct_size >= sizeof(RealmsCoreApi));
    CHECK(api->version == REALMS_BRIDGE_VERSION);
    CHECK(api->init(&host) == 1);

    // Registration, values and Quake 2 atof semantics.
    CHECK(api->cvar_register("sensitivity", "3", REALMS_CVAR_ARCHIVE) == 1);
    CHECK(get(api, "sensitivity") == "3");
    CHECK(std::fabs(api->cvar_value("sensitivity") - 3.0f) < 1e-6f);
    CHECK(api->cvar_flags("sensitivity") == REALMS_CVAR_ARCHIVE);
    CHECK(api->cvar_register("SENSITIVITY", "9", REALMS_CVAR_USERINFO) == 1); // OR flags, keep value
    CHECK(get(api, "sensitivity") == "3");
    CHECK(api->cvar_flags("sensitivity") == (REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO));
    CHECK(api->cvar_get("nope", nullptr, 0) == -1);
    CHECK(api->cvar_flags("nope") == -1);
    CHECK(api->cvar_register("bad name", "1", 0) == 0);
    api->cvar_set("num", "3.5abc");
    CHECK(std::fabs(api->cvar_value("num") - 3.5f) < 1e-6f);
    api->cvar_set("num", "-2e2");
    CHECK(std::fabs(api->cvar_value("num") + 200.0f) < 1e-3f);
    api->cvar_set("num", "abc");
    CHECK(api->cvar_value("num") == 0.0f);
    char tiny[4];
    CHECK(api->cvar_get("num", tiny, 4) == 3);
    CHECK(std::string(tiny) == "abc");
    api->cvar_set("num", "abcdef");
    CHECK(api->cvar_get("num", tiny, 4) == 6);
    CHECK(std::string(tiny) == "abc");

    // Modified tracking.
    CHECK(api->cvar_modified("sensitivity", 1) == 1);
    CHECK(api->cvar_modified("sensitivity", 1) == 0);
    CHECK(api->cvar_set("sensitivity", "4") == REALMS_CVAR_SET_OK);
    CHECK(api->cvar_modified("sensitivity", 0) == 1);

    // NOSET / LATCH / CHEAT / info validation.
    api->cvar_register("ro", "1", REALMS_CVAR_NOSET);
    CHECK(api->cvar_set("ro", "2") == REALMS_CVAR_SET_READONLY);
    CHECK(get(api, "ro") == "1");
    CHECK(sawLine("ro is write protected."));
    CHECK(api->cvar_force_set("ro", "2") == REALMS_CVAR_SET_OK);
    CHECK(get(api, "ro") == "2");

    api->cvar_register("game", "baseq2", REALMS_CVAR_LATCH);
    CHECK(api->cvar_set("game", "realms") == REALMS_CVAR_SET_LATCHED);
    CHECK(get(api, "game") == "baseq2");
    CHECK(sawLine("game will be changed for next game."));

    api->cvar_register("god", "0", REALMS_CVAR_CHEAT);
    CHECK(api->cvar_set("god", "1") == REALMS_CVAR_SET_CHEAT);
    CHECK(get(api, "god") == "0");
    CHECK(api->cvar_set("sv_cheats", "1") == REALMS_CVAR_SET_LATCHED); // sv_cheats latches like Q2
    CHECK(api->cvar_force_set("sv_cheats", "1") == REALMS_CVAR_SET_OK);
    CHECK(api->cvar_set("god", "1") == REALMS_CVAR_SET_OK);

    api->cvar_register("name", "player", REALMS_CVAR_USERINFO);
    CHECK(api->cvar_set("name", "bad\\name") == REALMS_CVAR_SET_READONLY);
    CHECK(get(api, "name") == "player");
    CHECK(get(api, "realms_core") == api->backend_name());

    // Command buffer: ';' and newline separators, quotes protect ';'.
    api->cbuf_add_text("set a 1; set b 2\nset c \"x;y\"\n");
    api->cbuf_execute();
    CHECK(get(api, "a") == "1");
    CHECK(get(api, "b") == "2");
    CHECK(get(api, "c") == "x;y");

    // "name" prints, "name value" sets (Cvar_Command).
    gHost.lines.clear();
    api->cmd_execute_string("a");
    CHECK(sawLine("\"a\" is \"1\""));
    api->cmd_execute_string("a 7");
    CHECK(get(api, "a") == "7");

    // wait defers the rest of the buffer to the next frame.
    api->cbuf_add_text("set w 1; wait; set w 2\n");
    api->cbuf_execute();
    CHECK(get(api, "w") == "1");
    api->frame(16);
    CHECK(get(api, "w") == "2");

    // Aliases and the alias loop guard.
    api->cbuf_add_text("alias go \"set g 5; set h 6\"\ngo\n");
    api->cbuf_execute();
    CHECK(get(api, "g") == "5");
    CHECK(get(api, "h") == "6");
    gHost.lines.clear();
    api->cbuf_add_text("alias loop loop\nloop\n");
    api->cbuf_execute();
    CHECK(sawLine("ALIAS_LOOP_COUNT"));

    // $macro expansion outside quotes, // comments.
    api->cbuf_add_text("set who world\nset greet $who\nset q \"$who\"\nset k 1 // set k 2\n");
    api->cbuf_execute();
    CHECK(get(api, "greet") == "world");
    CHECK(get(api, "q") == "$who");
    CHECK(get(api, "k") == "1");

    // toggle / reset / seta / set u.
    api->cbuf_add_text("toggle k\nreset sensitivity\nseta arch \"1 2\"\nset ui 1 u\n");
    api->cbuf_execute();
    CHECK(get(api, "k") == "0");
    CHECK(get(api, "sensitivity") == "9"); // default from the last Cvar_Get
    CHECK(get(api, "arch") == "1 2");
    CHECK((api->cvar_flags("arch") & REALMS_CVAR_ARCHIVE) != 0);
    CHECK(api->cvar_flags("ui") == REALMS_CVAR_USERINFO);

    // Host commands round trip with tokenised arguments.
    CHECK(api->cmd_register("hello") == 1);
    CHECK(api->cmd_register("hello") == 0);
    CHECK(api->cmd_register("arch") == 0); // already a cvar
    CHECK(api->cmd_exists("HELLO") == 1);
    api->cmd_execute_string("hello a \"b c\" $who");
    CHECK(gHost.commands.size() == 1);
    if (gHost.commands.size() == 1) {
        CHECK(gHost.commands[0].size() == 4);
        if (gHost.commands[0].size() == 4) {
            CHECK(gHost.commands[0][0] == "hello");
            CHECK(gHost.commands[0][2] == "b c");
            CHECK(gHost.commands[0][3] == "world");
        }
    }
    api->cmd_unregister("hello");
    CHECK(api->cmd_exists("hello") == 0);
    gHost.lines.clear();
    api->cmd_execute_string("hello");
    CHECK(sawLine("Unknown command \"hello\""));

    // exec from a loose host file.
    gHost.files["autoexec.cfg"] = "set fromcfg 7\necho cfg loaded\n";
    gHost.lines.clear();
    api->cbuf_add_text("exec autoexec.cfg\n");
    api->cbuf_execute();
    CHECK(get(api, "fromcfg") == "7");
    CHECK(sawLine("cfg loaded"));
    api->cmd_execute_string("exec missing.cfg");
    CHECK(sawLine("couldn't exec missing.cfg"));

    // PAK virtual filesystem, newest archive wins, case-insensitive names.
    gHost.files["baseq2/pak0.pak"] = makePak({{"scripts/auto.cfg", "set pakvar 42\n"}, {"maps/readme.txt", "q2dm1"}});
    gHost.files["baseq2/pak1.pak"] = makePak({{"maps/readme.txt", "realms"}});
    gHost.files["maps/readme.txt"] = "loose";
    gHost.files["bad.pak"] = "NOPE....";
    CHECK(api->fs_add_pak("baseq2/pak0.pak") == 2);
    CHECK(api->fs_add_pak("baseq2/pak1.pak") == 1);
    CHECK(api->fs_add_pak("bad.pak") == -1);
    CHECK(api->fs_add_pak("missing.pak") == -1);
    CHECK(api->fs_pak_count() == 2);
    void *data = nullptr;
    int32_t size = 0;
    CHECK(api->fs_load("MAPS\\README.TXT", &data, &size) == 1);
    CHECK(size == 6 && data && std::memcmp(data, "realms", 6) == 0);
    api->fs_free(data);
    CHECK(api->fs_load("scripts/auto.cfg", &data, &size) == 1);
    CHECK(size == 14);
    api->fs_free(data);
    CHECK(api->fs_load("../etc/passwd", &data, &size) == 0);
    CHECK(data == nullptr && size == 0);
    api->cbuf_add_text("exec scripts/auto.cfg\n");
    api->cbuf_execute();
    CHECK(get(api, "pakvar") == "42");
    if (rangeReads) CHECK(gHost.rangeReadCalls > 0);

    // writeconfig emits archived cvars as executable lines.
    api->cmd_execute_string("writeconfig test.cfg");
    CHECK(gHost.written.count("test.cfg") == 1);
    CHECK(gHost.written["test.cfg"].find("seta arch \"1 2\"") != std::string::npos);
    CHECK(gHost.written["test.cfg"].find("seta sensitivity \"9\"") != std::string::npos);
    CHECK(gHost.written["test.cfg"].find("seta god") == std::string::npos);

    int visited = 0;
    api->cvar_foreach([](void *u, const char *, const char *, int32_t) { ++*static_cast<int *>(u); }, &visited);
    CHECK(visited > 15);

    // Shutdown clears all state; re-init starts clean.
    api->shutdown();
    CHECK(api->init(&host) == 1);
    CHECK(api->cvar_get("arch", nullptr, 0) == -1);
    CHECK(api->fs_pak_count() == 0);
    CHECK(api->cmd_exists("exec") == 1);
    api->shutdown();
}

// --------------------------------------------------------- facade suite ---

void runEngineSuite(const std::string &pascalLibrary) {
    namespace fs = std::filesystem;
    // Unique per run: ctest -j runs the C++ and Pascal suites side by side.
    const fs::path tmp = fs::temp_directory_path() /
        ("realms-engine-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         (pascalLibrary.empty() ? "native" : "pascal"));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "assets" / "config", ec);
    fs::create_directories(tmp / "save", ec);
    {
        std::ofstream(tmp / "assets" / "config" / "realms.cfg") << "set cl_test 12\nalias jump \"echo jumped\"\n";
    }

    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {(tmp / "assets").string()};
    cfg.writableRoot = (tmp / "save").string();
    cfg.consoleCapacity = 64;

    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.backendName() == (pascalLibrary.empty() ? "native-cpp" : "pascal-quake2"));
    CHECK(engine.pascalBackend() == !pascalLibrary.empty());

    realms::Engine second;
    CHECK(!second.init(cfg)); // one core per process

    CHECK(engine.registerCvar("sensitivity", "3", REALMS_CVAR_ARCHIVE));
    CHECK(engine.cvarFloat("sensitivity") == 3.0f);
    CHECK(engine.cvarInt("missing", 5) == 5);
    CHECK(engine.cvarString("realms_version") == realms::kEngineVersion);

    int noclipCalls = 0;
    std::string lastArg;
    CHECK(engine.addCommand("noclip", [&](const realms::CommandArgs &args) {
        ++noclipCalls;
        if (args.size() > 1) lastArg = args[1];
        engine.setCvar("from_handler", "yes"); // re-entrancy into the core
    }));
    engine.exec("noclip on; noclip\n");
    CHECK(noclipCalls == 2);
    CHECK(lastArg == "on");
    CHECK(engine.cvarString("from_handler") == "yes");

    engine.exec("exec config/realms.cfg\njump\n");
    CHECK(engine.cvarInt("cl_test") == 12);
    bool jumped = false;
    for (const auto &line : engine.console())
        if (line.text == "jumped") jumped = true;
    CHECK(jumped);

    const std::string script = realms::Engine::commandLineScript(
        {"--assets", "x", "+set", "sensitivity", "6", "+set", "name", "Big Boss", "+noclip"});
    CHECK(script == "set sensitivity 6\nset name \"Big Boss\"\nnoclip\n");
    engine.exec(script);
    CHECK(engine.cvarFloat("sensitivity") == 6.0f);
    CHECK(engine.cvarString("name") == "Big Boss");
    CHECK(noclipCalls == 3);

    engine.exec("writeconfig config.cfg\n");
    std::ifstream written(tmp / "save" / "config.cfg");
    const std::string text((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    CHECK(text.find("seta sensitivity \"6\"") != std::string::npos);

    std::vector<unsigned char> bytes;
    CHECK(engine.loadFile("config/realms.cfg", bytes) && !bytes.empty());

    // Collision model through the facade: a map inside a PAK on disk.
    {
        std::ofstream(tmp / "assets" / "maps.pak", std::ios::binary)
            << makePak({{"maps/test.bsp", bsp_builder::build(map_suite::testMap())}});
        CHECK(engine.addPak("maps.pak") == 1);
        CHECK(!engine.mapLoaded());
        CHECK(!engine.loadMap("maps/nothere.bsp"));
        CHECK(engine.loadMap("maps/test.bsp"));
        CHECK(engine.mapLoaded());
        RealmsMapInfo info{};
        CHECK(engine.mapInfo(info) && info.num_models == 2 && std::string(info.name) == "maps/test.bsp");
        CHECK(engine.mapEntities() == map_suite::entities());
        CHECK(engine.pointContents({0, 0, -32}) == REALMS_CONTENTS_SOLID);
        CHECK(engine.pointContents({0, 0, 32}) == 0);
        const realms::TraceResult t =
            engine.boxTrace({0, 0, 128}, {0, 0, -128}, {-16, -16, -24}, {16, 16, 32}, 0, REALMS_MASK_PLAYERSOLID);
        CHECK(t.hit() && !t.startsolid && t.endpos[2] == 24.03125f && t.normal[2] == 1.0f);
        CHECK(t.surface == "e1u1/floor1_3" && t.contents == REALMS_CONTENTS_SOLID);
        CHECK(!engine.boxTrace({0, 0, 64}, {0, 100, 64}, {}, {}).hit());
        realms::MapModel door;
        CHECK(engine.mapModel(1, door) && door.mins[0] == 99.0f);
        realms::MapModel none;
        CHECK(!engine.mapModel(5, none) && none.headnode == 0);
        CHECK(engine.transformedPointContents({0, 108, 64}, door.headnode, {0, 0, 0}, {0, 90, 0}) ==
              REALMS_CONTENTS_SOLID);
        const realms::TraceResult d = engine.transformedBoxTrace({0, 100, 64}, {256, 100, 64}, {}, {}, door.headnode,
                                                                 REALMS_MASK_SOLID, {0, 100, 0}, {0, 0, 0});
        CHECK(d.hit() && d.surface == "e1u1/door" && d.surfaceValue == 7);
        const int32_t box = engine.headnodeForBox({-8, -8, -8}, {8, 8, 8});
        CHECK(engine.pointContents({0, 0, 0}, box) == REALMS_CONTENTS_MONSTER);
        int32_t contents = -1, cluster = -1, area = -1;
        CHECK(engine.leafInfo(engine.pointLeaf({0, 0, 32}), contents, cluster, area));
        CHECK(contents == 0 && cluster >= 0 && area == 1);
        engine.unloadMap();
        CHECK(!engine.mapLoaded() && engine.mapEntities().empty());
        CHECK(engine.loadMap("maps/test.bsp"));
    }

    // Bridge v3 through the facade: visibility, areas, entities.
    {
        CHECK(engine.mapPath() == "maps/test.bsp" && engine.meshMap().indices.empty());
        const std::vector<uint8_t> pvs = engine.clusterPVS(0); // no vis lump: all visible
        CHECK(!pvs.empty() && pvs[0] == 0xff && realms::Engine::clusterVisible(pvs, 0));
        CHECK(!realms::Engine::clusterVisible(pvs, -1) && !realms::Engine::clusterVisible(pvs, 8 * int32_t(pvs.size())));
        CHECK(engine.clusterPHS(0) == pvs);
        CHECK(engine.headnodeVisible(0, pvs) && !engine.headnodeVisible(0, {}));
        int32_t top = -2;
        CHECK(!engine.boxLeafs({-8, -8, -8}, {8, 8, 40}, 0, &top).empty());
        CHECK(engine.boxLeafs({-8, -8, -8}, {8, 8, 40}, 0, nullptr, 0).empty());
        CHECK(engine.areaPortalState(0) == 0 && engine.areaPortalState(5000) == -1);
        engine.setAreaPortal(0, true);
        CHECK(engine.areaPortalState(0) == 1);
        engine.setAreaPortal(0, false);
        CHECK(!engine.areaBits(0).empty() && engine.areasConnected(1, 1));
        const auto ents = realms::Engine::parseEntities(engine.mapEntities());
        CHECK(!ents.empty() && realms::Engine::entityValue(ents[0], "classname") == "worldspawn");
        const auto parsed = realms::Engine::parseEntities(
            "// comment\n{\n\"classname\" \"light\"\n\"origin\" \"1 2 3\" // trailing\n}\n{ \"a\" \"b\" }\n{ \"unterminated\" ");
        CHECK(parsed.size() == 2 && realms::Engine::entityValue(parsed[0], "origin") == "1 2 3");
        CHECK(realms::Engine::entityValue(parsed[1], "a") == "b" && realms::Engine::entityValue(parsed[1], "x").empty());
    }

    // Map console commands and .glb / .gltf maps.
    {
        std::vector<std::string> changes;
        realms::sandbox::registerMapCommands(engine, [&](const std::string &p) { changes.push_back(p); });
        const std::string room = gltf_builder::roomGlb();
        std::ofstream(tmp / "assets" / "meshes.pak", std::ios::binary)
            << makePak({{"maps/room.glb", room},
                        {"maps/ext.gltf", gltf_builder::roomJson("ext.bin")},
                        {"maps/ext.bin", gltf_builder::quadBin()},
                        {"maps/broken.glb", room.substr(0, 100)}});
        CHECK(engine.addPak("meshes.pak") > 0);
        CHECK(engine.cvarFloat("map_mesh_scale") == 32.0f);

        auto sawConsole = [&](const std::string &needle) {
            for (const auto &line : engine.console())
                if (line.text.find(needle) != std::string::npos) return true;
            return false;
        };
        engine.exec("map test\n");
        CHECK(changes.size() == 1 && changes.back() == "maps/test.bsp" && engine.cvarString("mapname") == "test");
        engine.exec("map nothere\n");
        CHECK(changes.size() == 1 && engine.mapLoaded() && sawConsole("map: can't find nothere"));
        engine.exec("map room\n");
        CHECK(changes.size() == 2 && changes.back() == "maps/room.glb" && engine.mapPath() == "maps/room.glb");
        CHECK(engine.cvarString("mapname") == "room" && engine.meshMap().triangleCount() == 8);
        RealmsMapInfo info{};
        CHECK(engine.mapInfo(info) && std::string(info.name) == "maps/room.glb");
        CHECK(info.num_brushes == 6); // floor, mirrored copy, pool; the nocollide ceiling has none
        CHECK(info.checksum == realms::crc32(reinterpret_cast<const unsigned char *>(room.data()), room.size()));
        const realms::TraceResult t = engine.boxTrace({0, 0, 32}, {0, 0, -32}, {}, {});
        CHECK(t.hit() && t.endpos[2] == 0.03125f && t.normal[2] == 1.0f && t.surface == "concrete" && t.surfaceValue == 5);
        CHECK(!engine.boxTrace({0, 0, 32}, {0, 0, 96}, {}, {}).hit()); // ceiling is render-only
        const realms::TraceResult m = engine.boxTrace({0, 0, 192}, {0, 0, 64}, {}, {});
        CHECK(m.hit() && m.endpos[2] == 128.03125f && m.normal[2] == 1.0f); // mirrored instance still faces up
        CHECK(engine.pointContents({0, 0, -33}) == REALMS_CONTENTS_WATER);
        CHECK(engine.pointContents({0, 0, 16}) == 0);
        const realms::TraceResult hull =
            engine.boxTrace({0, 0, 100}, {0, 0, -100}, {-16, -16, -24}, {16, 16, 32}, 0, REALMS_MASK_PLAYERSOLID);
        CHECK(hull.hit() && hull.endpos[2] == 24.03125f);
        const auto ents = realms::Engine::parseEntities(engine.mapEntities());
        CHECK(ents.size() == 2 && realms::Engine::entityValue(ents[1], "classname") == "info_player_start");
        CHECK(ents.size() == 2 && realms::Engine::entityValue(ents[1], "origin") == "32 -64 16");
        CHECK(engine.clusterPVS(0) == std::vector<uint8_t>{0xff});
        engine.exec("mapinfo\n");
        CHECK(sawConsole("(triangle mesh)") && sawConsole("mesh: 8 triangles, 3 surfaces"));
        CHECK(sawConsole("entities 2, info_player_start x1, worldspawn x1"));

        engine.exec("set map_mesh_scale 64; map maps/ext.gltf\n");
        CHECK(changes.size() == 3 && changes.back() == "maps/ext.gltf" && engine.meshMap().triangleCount() == 8);
        CHECK(!engine.meshMap().vertices.empty() && engine.meshMap().vertices[0] == -256.0f);
        engine.exec("set map_mesh_scale 32\n");

        engine.exec("map broken.glb\n");
        CHECK(changes.size() == 4 && changes.back().empty() && !engine.mapLoaded());
        CHECK(engine.cvarString("mapname").empty() && sawConsole("map_load: maps/broken.glb:"));
        CHECK(engine.meshMap().indices.empty() && engine.mapPath().empty());

        engine.exec("map room; killserver\n");
        CHECK(changes.size() == 6 && changes.back().empty() && !engine.mapLoaded());
        engine.exec("killserver\n"); // nothing loaded: no change notification
        CHECK(changes.size() == 6);
        engine.exec("mapinfo\n");
        CHECK(sawConsole("mapinfo: no map loaded"));
        CHECK(engine.loadMap("maps/test.bsp"));
    }

    // Player movement and drawable geometry on a BSP and a glTF map.
    {
        std::ofstream(tmp / "assets" / "walk.pak", std::ios::binary)
            << makePak({{"maps/walk.bsp", bsp_builder::build(walker_suite::walkMap(true))},
                        {"maps/yard.glb", gltf_builder::yardGlb()},
                        {"maps/walls.glb", gltf_builder::wallsGlb()}});
        CHECK(engine.addPak("walk.pak") > 0);
        walker_suite::runWalkerChecks(engine);
        CHECK(engine.loadMap("maps/test.bsp"));
    }
    CHECK(engine.console().size() <= 64);

    // Frame clock runs deferred commands.
    engine.addText("set later 1; wait; set later 2\n");
    engine.frame(0.016);
    CHECK(engine.cvarInt("later") == 1);
    engine.frame(0.016);
    CHECK(engine.cvarInt("later") == 2);

    engine.removeCommand("noclip");
    CHECK(!engine.commandExists("noclip"));
    engine.shutdown();
    CHECK(!engine.initialized());
    CHECK(second.init(cfg)); // slot released
    second.shutdown();
    fs::remove_all(tmp, ec);
}

} // namespace

int main(int argc, char **argv) {
    std::string pascalLibrary;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--pascal" && i + 1 < argc) pascalLibrary = argv[++i];
    }

    const RealmsCoreApi *api = nullptr;
    if (pascalLibrary.empty()) {
        api = realms_native_get_api(REALMS_BRIDGE_VERSION);
    } else {
#if defined(REALMS_PASCAL_STATIC)
        api = realms_pascal_get_api(REALMS_BRIDGE_VERSION);
        CHECK(realms_pascal_get_api(REALMS_BRIDGE_VERSION + 1) == nullptr);
#elif defined(_WIN32)
        std::fprintf(stderr, "--pascal is not supported on Windows in this test\n");
        return 2;
#else
        void *lib = dlopen(pascalLibrary.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            std::fprintf(stderr, "dlopen(%s) failed: %s\n", pascalLibrary.c_str(), dlerror());
            return 2;
        }
        auto getApi = reinterpret_cast<RealmsGetApiFn>(dlsym(lib, REALMS_PASCAL_ENTRY_SYMBOL));
        if (!getApi) {
            std::fprintf(stderr, "missing %s in %s\n", REALMS_PASCAL_ENTRY_SYMBOL, pascalLibrary.c_str());
            return 2;
        }
        api = getApi(REALMS_BRIDGE_VERSION);
        CHECK(getApi(REALMS_BRIDGE_VERSION + 1) == nullptr);
#endif
    }
    if (!api) {
        std::fprintf(stderr, "core rejected bridge version %u\n", REALMS_BRIDGE_VERSION);
        return 2;
    }

    const std::string backend = api->backend_name();
    std::printf("[realms] conformance: backend=%s\n", backend.c_str());
    runAbiSuite(api, true);
    runAbiSuite(api, false);
    runMapSuite(api);
    meshmap_suite::runMeshMapChecks();
    walker_suite::runGeometryChecks();
    runEngineSuite(pascalLibrary);

    std::printf("[realms] %d checks, %d failures (%s)\n", gChecks, gFailures, backend.c_str());
    return gFailures == 0 ? 0 : 1;
}
