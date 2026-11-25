// GameSave.cpp: save game names and folders, server.ssv written and read
// back (binary config strings, portals), damaged and foreign files, and the
// "load" command (SandboxBoot.cpp) up to loading the saved map. The game's
// own files (WriteGame / WriteLevel) are covered by quake2_module_test.
#include "realms/Engine.h"
#include "realms/GameSave.h"
#include "realms/SandboxBoot.h"

#include "bsp_builder.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

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

void writeText(const fs::path &path, const std::string &text) {
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(text.data(), std::streamsize(text.size()));
}

std::string readText(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void names() {
    CHECK(realms::validGameSaveName("quick"));
    CHECK(realms::validGameSaveName("save_1-B"));
    CHECK(realms::validGameSaveName(std::string(32, 'a')));
    CHECK(!realms::validGameSaveName(std::string(33, 'a')));
    for (const char *bad : {"", "..", "a/b", "a\\b", "a b", "../x", "c:", "name.sav", "\xd1\x8f"})
        CHECK(!realms::validGameSaveName(bad));
}

void roundTrip(const fs::path &dir) {
    fs::create_directories(dir);
    realms::GameSaveInfo info;
    info.comment = "base1  2026-11-21 14:05\nsecond line";
    info.mapPath = "maps/base1.bsp";
    info.skill = 3;
    info.configStrings.resize(40);
    info.configStrings[0] = "Outer Base";
    info.configStrings[5] = "yb -24 xv 0 hnum\nxv 50 pic 0 "; // newlines and spaces kept
    info.configStrings[6] = std::string("a\0b", 3);            // so are NUL bytes
    info.configStrings[39] = "cs 1 1";                          // looks like a header line
    info.portals = {1, 0, 0, 1, 1};
    std::string error;
    CHECK(realms::writeGameSaveInfo(dir.string(), info, error));
    CHECK(!fs::exists(dir / "server.ssv.tmp"));

    realms::GameSaveInfo back;
    CHECK(realms::readGameSaveInfo(dir.string(), back, error));
    CHECK(back.comment == "base1  2026-11-21 14:05 second line"); // one line
    CHECK(back.mapPath == info.mapPath && back.skill == 3);
    CHECK(back.portals == info.portals);
    CHECK(back.configStrings.size() == 40);
    CHECK(back.configStrings == info.configStrings);

    // complete only with the game's two files as well
    CHECK(!realms::gameSaveComplete(dir.string()));
    writeText(dir / realms::kGameSaveGameFile, "g");
    CHECK(!realms::gameSaveComplete(dir.string()));
    writeText(dir / realms::kGameSaveLevelFile, "l");
    CHECK(realms::gameSaveComplete(dir.string()));

    // rewriting replaces the file
    info.configStrings.assign(3, "x");
    info.portals.clear();
    CHECK(realms::writeGameSaveInfo(dir.string(), info, error));
    CHECK(realms::readGameSaveInfo(dir.string(), back, error));
    CHECK(back.configStrings.size() == 3 && back.portals.empty());
}

void damaged(const fs::path &dir) {
    fs::create_directories(dir);
    realms::GameSaveInfo info;
    std::string error;
    CHECK(!realms::readGameSaveInfo((dir / "missing").string(), info, error) && !error.empty());

    const std::string good = readText(dir.parent_path() / "slot" / "server.ssv");
    CHECK(!good.empty());
    auto rejects = [&](const std::string &text) {
        writeText(dir / "server.ssv", text);
        realms::GameSaveInfo out;
        std::string why;
        const bool ok = realms::readGameSaveInfo(dir.string(), out, why);
        return !ok && !why.empty() && out.mapPath.empty() && out.configStrings.empty();
    };
    CHECK(rejects(""));
    CHECK(rejects("QUAKE2 SAVE\nmap maps/x.bsp\nend\n"));                  // another format
    CHECK(rejects(good.substr(0, good.size() - 4)));                         // no "end": cut short
    CHECK(rejects("EKRON-SAVE 1\nskill 1\nend\n"));                          // no map
    CHECK(rejects("EKRON-SAVE 1\nmap m\ncs 1 99\nshort\nend\n"));            // string past the end
    CHECK(rejects("EKRON-SAVE 1\nmap m\ncs -1 1\nx\nend\n"));                // bad index
    CHECK(rejects("EKRON-SAVE 1\nmap m\ncs 999999 1\nx\nend\n"));            // index out of range
    CHECK(rejects("EKRON-SAVE 1\nmap m\ncs 1 2\nxyz\nend\n"));               // length mismatch
    // unknown keys (a newer version's) are skipped
    writeText(dir / "server.ssv", "EKRON-SAVE 1\nmap m\nfuture stuff\nend\n");
    CHECK(realms::readGameSaveInfo(dir.string(), info, error) && info.mapPath == "m");
}

void loadCommand(const fs::path &root) {
    fs::create_directories(root / "maps");
    {
        bsp_builder::MapDef def;
        def.texinfo = {{"e1u1/floor", 0, 0}};
        def.world.brushes = {{{-128, -128, -64}, {128, 128, 0}, REALMS_CONTENTS_SOLID, 0}};
        def.entities = "{\n\"classname\" \"worldspawn\"\n}\n";
        const std::string bsp = bsp_builder::build(def);
        std::ofstream(root / "maps" / "room.bsp", std::ios::binary).write(bsp.data(), std::streamsize(bsp.size()));
    }
    realms::EngineConfig cfg;
    cfg.backend = realms::BackendPreference::Native;
    cfg.searchRoots = {root.string()};
    cfg.writableRoot = root.string();
    realms::Engine engine;
    CHECK(engine.init(cfg));
    int changes = 0;
    std::string changedTo;
    realms::sandbox::registerMapCommands(engine, [&](const std::string &path) {
        ++changes;
        changedTo = path;
    });
    CHECK(engine.writableRoot() == root.string());
    const std::string dir = realms::gameSaveDir(engine, "quick");
    CHECK(fs::path(dir) == root / "save" / "quick");

    // nothing saved: no map change, no pending load
    engine.exec("load quick\n");
    CHECK(changes == 0 && engine.cvarString("sv_loadgame", "").empty());
    engine.exec("load ../escape\n");
    CHECK(changes == 0);

    fs::create_directories(dir);
    realms::GameSaveInfo info;
    info.comment = "room";
    info.mapPath = "maps/room.bsp";
    info.skill = 2;
    std::string error;
    CHECK(realms::writeGameSaveInfo(dir, info, error));
    writeText(fs::path(dir) / realms::kGameSaveGameFile, "g");
    writeText(fs::path(dir) / realms::kGameSaveLevelFile, "l");
    engine.exec("load quick\n");
    // the map loads right after "load" in the same buffer run, the game reads the save when it starts
    CHECK(changes == 1 && changedTo == "maps/room.bsp" && engine.mapLoaded() && engine.mapPath() == "maps/room.bsp");
    CHECK(engine.cvarString("sv_loadgame", "") == dir);
    CHECK(engine.cvarInt("skill", 0) == 2);
    // sv_loadgame is not for the console
    engine.exec("set sv_loadgame foo\n");
    CHECK(engine.cvarString("sv_loadgame", "") == dir);

    engine.shutdown(); // one engine at a time

    // no writable root: refused
    realms::Engine readOnly;
    realms::EngineConfig ro = cfg;
    ro.writableRoot.clear();
    CHECK(readOnly.init(ro));
    realms::sandbox::registerMapCommands(readOnly);
    CHECK(realms::gameSaveDir(readOnly, "quick").empty());
    readOnly.exec("load quick\n");
    CHECK(!readOnly.mapLoaded());
    readOnly.shutdown();
}

} // namespace

int main() {
    const fs::path tmp = fs::temp_directory_path() /
        ("realms-game-save-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    names();
    roundTrip(tmp / "slot");
    damaged(tmp / "damaged");
    loadCommand(tmp / "root");
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("[game_save] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
