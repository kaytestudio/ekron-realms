// Realms Engine - shared sandbox boot sequence (see SandboxBoot.h).

#include "realms/GameDemo.h"
#include "realms/GameSave.h"
#include "realms/SandboxBoot.h"
#include "realms/ViewSize.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

// Bit-exact with the Free Pascal core: no fused multiply-add contraction
// (clang contracts a*b+c by default on arm64 / FMA targets).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace realms::sandbox {

void registerSharedCvars(Engine &engine, const std::string &platform) {
    engine.registerCvar("sensitivity", "3", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("m_invert", "0", REALMS_CVAR_ARCHIVE);
    // Quake 2's field of view (map mode; realms/ViewFov.h), sent to the game as userinfo.
    engine.registerCvar("fov", "90", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    engine.registerCvar("name", "Player", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    // Quake 2's crosshair (GameHudCanvas::crosshairStyle): 0 none, 1 .. 3 the
    // game's pics ch1 .. ch3 in maps. On by default, unlike Quake 2.
    engine.registerCvar("crosshair", "1", REALMS_CVAR_ARCHIVE);
    // Quake 2's handedness (userinfo): 0 right, 1 left (the view weapon mirrored),
    // 2 centre (no view weapon); the game fires from that side.
    engine.registerCvar("hand", "0", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    // cl_main.c: the player model and skin ("<model>/<skin>"), and the gender
    // the game's obituaries read; gender_auto makes it follow the skin's
    // model (Quake2Module::fixUpGender).
    engine.registerCvar("skin", "male/grunt", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    engine.registerCvar("gender", "male", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    engine.registerCvar("gender_auto", "1", REALMS_CVAR_ARCHIVE);
    // Quake 2's screen size (realms/ViewSize.h): the 3D view in maps shrunk to
    // 40 .. 100 per cent, the rest tiled; "sizeup" / "sizedown" step it by 10
    // (SCR_SizeUp_f / SCR_SizeDown_f), within those bounds.
    engine.registerCvar("viewsize", "100", REALMS_CVAR_ARCHIVE);
    // Yamagi Quake II's frame rate counter (realms/FrameCounter.h) at the top
    // right: 0 off, 1 the average, 2 slowest / fastest / average, 3 and the
    // frame times.
    engine.registerCvar("cl_showfps", "0", REALMS_CVAR_ARCHIVE);
    // Quake 2's r_drawentities (map mode, mapview::Quake2View): 0 draws none of
    // the game's entities (models, the view weapon, doors and platforms).
    // Not saved, as in Quake 2.
    engine.registerCvar("r_drawentities", "1", 0);
    Engine *e = &engine;
    const auto resize = [e](int step) {
        const float now = e->cvarFloat("viewsize", 100.0f);
        e->setCvar("viewsize", std::to_string(clampViewSize((std::isfinite(now) ? now : 100.0f) + float(step))));
    };
    engine.addCommand("sizeup", [resize](const CommandArgs &) { resize(10); });
    engine.addCommand("sizedown", [resize](const CommandArgs &) { resize(-10); });
    // Brightness of the 3D view (realms/ViewGamma.h), as in Yamagi Quake II:
    // higher is brighter, 0.1 .. 2.0. 1 leaves the view as it is.
    engine.registerCvar("vid_gamma", "1", REALMS_CVAR_ARCHIVE);
    // Yamagi Quake II's cl_kickangles (map mode): 0 drops the view kicks (weapon
    // recoil, hits, falls, the run and bob tilt). Archived here, not a cheat
    // as in yquake2: it is there for players who get motion sick.
    engine.registerCvar("cl_kickangles", "1", REALMS_CVAR_ARCHIVE);
    // Game sound (sys/android/app/src/main/cpp/SoundSystem): master and music volume, 0..1.
    engine.registerCvar("s_volume", "0.8", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("s_musicvolume", "0.4", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("realms_platform", platform.empty() ? "unknown" : platform, REALMS_CVAR_NOSET);
    engine.forceSetCvar("realms_platform", platform.empty() ? "unknown" : platform);
}

namespace {

std::string fmtVec(const float *v) {
    return "(" + std::to_string(std::lround(v[0])) + " " + std::to_string(std::lround(v[1])) + " " +
           std::to_string(std::lround(v[2])) + ")";
}

std::string baseName(const std::string &path) {
    std::string n = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    const size_t dot = n.rfind('.');
    if (dot != std::string::npos && dot > 0) n.resize(dot);
    return n;
}

} // namespace

void registerMapCommands(Engine &engine, MapChangedFn onChange) {
    engine.registerCvar("map_mesh_scale", "32", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("map_mesh_thickness", "0", 0);
    engine.registerCvar("mapname", "", REALMS_CVAR_NOSET | REALMS_CVAR_SERVERINFO);
    Engine *e = &engine;
    auto changed = std::make_shared<MapChangedFn>(std::move(onChange));
    engine.addCommand("map", [e, changed](const CommandArgs &args) {
        if (args.size() < 2) {
            e->print("USAGE: map <mapname>");
            if (e->mapLoaded()) e->print("current map: " + e->mapPath());
            return;
        }
        const std::string path = e->findMap(args[1]);
        if (path.empty()) {
            e->print("map: can't find " + args[1], REALMS_PRINT_WARNING);
            return;
        }
        const bool ok = e->loadMap(path);
        e->forceSetCvar("mapname", ok ? baseName(path) : "");
        if (ok) {
            RealmsMapInfo info{};
            e->mapInfo(info);
            e->print("map: " + path + " (" + (e->meshMap().indices.empty() ? "bsp" : "mesh") + ", " +
                     std::to_string(info.num_leafs) + " leafs, " + std::to_string(info.num_brushes) + " brushes)");
        }
        if (*changed) (*changed)(ok ? path : std::string());
    });
    // Quake 2's "load <name>" (SV_Loadgame_f; F9: "load quick"): the saved
    // map is loaded again and the game started on it reads the save back
    // (GameServer::start, through the NOSET cvar sv_loadgame).
    engine.registerCvar("sv_loadgame", "", REALMS_CVAR_NOSET);
    engine.addCommand("load", [e](const CommandArgs &args) {
        if (args.size() != 2) {
            e->print("USAGE: load <directory>");
            return;
        }
        const std::string dir = gameSaveDir(*e, args[1]);
        if (dir.empty()) {
            e->print(e->writableRoot().empty() ? "load: no writable folder for save games" : "Bad savedir.",
                     REALMS_PRINT_WARNING);
            return;
        }
        GameSaveInfo info;
        std::string error;
        if (!gameSaveComplete(dir) || !readGameSaveInfo(dir, info, error)) {
            e->print("load: " + (error.empty() ? "no saved game \"" + args[1] + "\"" : error), REALMS_PRINT_WARNING);
            return;
        }
        e->print("Loading " + args[1] + ": " + info.comment);
        e->forceSetCvar("sv_loadgame", dir);
        e->forceSetCvar("skill", std::to_string(info.skill));
        std::string quoted;
        for (char c : info.mapPath)
            if (c != '"') quoted += c;
        e->insertText("map \"" + quoted + "\"\n");
    });
    // Quake 2's "demomap <demo>" (SV_DemoMap_f) for Ekron demos
    // (realms/GameDemo.h): the demo's map is loaded and Quake2Module plays
    // the demo on it (through the NOSET cvar cl_demofile).
    engine.registerCvar(kGameDemoCvar, "", REALMS_CVAR_NOSET);
    // Quake 2's timedemo: 1 makes demomap play its demo as fast as frames
    // are drawn and print the frame rate at the end (Quake2Module).
    engine.registerCvar("timedemo", "0", 0);
    engine.addCommand("demomap", [e](const CommandArgs &args) {
        if (args.size() != 2) {
            e->print("USAGE: demomap <demoname>");
            return;
        }
        const std::string file = gameDemoFile(args[1]);
        std::vector<unsigned char> bytes;
        if (file.empty() || !e->loadFile(file, bytes)) {
            e->print("demomap: " + (file.empty() ? "bad demo name \"" + args[1] + "\"" : "can't find " + file),
                     REALMS_PRINT_WARNING);
            return;
        }
        GameDemoHeader header;
        std::string error;
        if (!GameDemoReader::readHeader(std::vector<uint8_t>(bytes.begin(), bytes.end()), header, error)) {
            e->print("demomap: " + file + ": " + error, REALMS_PRINT_WARNING);
            return;
        }
        if (e->findMap(header.mapPath).empty()) {
            e->print("demomap: " + file + " needs " + header.mapPath, REALMS_PRINT_WARNING);
            return;
        }
        e->forceSetCvar(kGameDemoCvar, file);
        std::string quoted;
        for (char c : header.mapPath)
            if (c != '"') quoted += c;
        e->insertText("map \"" + quoted + "\"\n");
    });
    engine.addCommand("killserver", [e, changed](const CommandArgs &) {
        const bool had = e->mapLoaded();
        e->unloadMap();
        e->forceSetCvar("mapname", "");
        if (had && *changed) (*changed)(std::string());
    });
    engine.addCommand("mapinfo", [e](const CommandArgs &) {
        RealmsMapInfo info{};
        if (!e->mapInfo(info)) {
            e->print("mapinfo: no map loaded");
            return;
        }
        char crc[16];
        std::snprintf(crc, sizeof crc, "%08x", unsigned(info.checksum));
        const bool mesh = !e->meshMap().indices.empty();
        e->print("map: " + std::string(info.name) + (mesh ? " (triangle mesh)" : " (IBSP v38)") + " checksum " + crc);
        e->print("planes " + std::to_string(info.num_planes) + ", nodes " + std::to_string(info.num_nodes) +
                 ", leafs " + std::to_string(info.num_leafs) + ", brushes " + std::to_string(info.num_brushes) +
                 ", brush sides " + std::to_string(info.num_brush_sides) + ", texinfo " +
                 std::to_string(info.num_texinfo));
        e->print("models " + std::to_string(info.num_models) + ", clusters " + std::to_string(info.num_clusters) +
                 ", areas " + std::to_string(info.num_areas) + ", area portals " +
                 std::to_string(info.num_area_portals));
        e->print("bounds " + fmtVec(info.mins) + " - " + fmtVec(info.maxs));
        if (mesh)
            e->print("mesh: " + std::to_string(e->meshMap().triangleCount()) + " triangles, " +
                     std::to_string(e->meshMap().surfaces.size()) + " surfaces");
        const auto entities = Engine::parseEntities(e->mapEntities());
        std::map<std::string, int> classes;
        for (const auto &ent : entities) ++classes[Engine::entityValue(ent, "classname")];
        std::string summary = "entities " + std::to_string(entities.size());
        for (const auto &c : classes) summary += ", " + c.first + " x" + std::to_string(c.second);
        e->print(summary);
    });
}

int boot(Engine &engine, const BootOptions &options) {
    registerSharedCvars(engine, options.platform);
    if (options.mapCommands) registerMapCommands(engine, options.onMapChanged);

    // pak0.pak .. pak9.pak beside the assets or in Quake 2's baseq2 folder;
    // later ones win, as in FS_AddGameDirectory.
    int paks = 0;
    for (int i = 0; i < 10; ++i) {
        for (const std::string &dir : {std::string(), std::string(kBaseGameDir) + "/"}) {
            const std::string pak = dir + "pak" + std::to_string(i) + ".pak";
            if (options.fileExists && options.fileExists(pak) && engine.addPak(pak) >= 0) ++paks;
        }
    }

    std::vector<unsigned char> probe;
    if (engine.loadFile("config/realms.cfg", probe)) engine.addText("exec config/realms.cfg\n");
    if (options.execUserConfig) {
        if (engine.loadFile("config.cfg", probe)) engine.addText("exec config.cfg\n");
        if (engine.loadFile("autoexec.cfg", probe)) engine.addText("exec autoexec.cfg\n");
    }
    if (!options.commandLine.empty()) engine.addText(options.commandLine);
    engine.executeBuffer();
    // CL_Init: gender->modified = false, so with gender_auto the gender
    // follows the configured skin until it is set itself
    engine.cvarModified("gender");
    return paks;
}

void saveUserConfig(Engine &engine) {
    if (engine.initialized()) engine.executeNow("writeconfig config.cfg");
}

float lookScale(const Engine &engine) {
    const float s = engine.cvarFloat("sensitivity", 3.0f);
    return std::clamp(std::isfinite(s) ? s : 3.0f, 0.05f, 50.0f) / 3.0f;
}

float pitchSign(const Engine &engine) {
    return engine.cvarBool("m_invert") ? -1.0f : 1.0f;
}

} // namespace realms::sandbox
