// Realms Engine - Quake 2 server for the game module.
//
// Ports of Quake 2 server/sv_world.c (area nodes, SV_LinkEdict,
// SV_AreaEdicts, SV_Trace, SV_PointContents), server/sv_game.c (the
// game_import_t functions) and the local-client parts of sv_init.c /
// sv_user.c, GPL (c) 1997-2001 id Software. The collision model calls go
// through realms::Engine, i.e. to the Pascal or the C++ core.
#include "realms/GameServer.h"
#include "realms/GameEffects.h"
#include "realms/GameSave.h"

#include "realms/Q2Pmove.h"

#include "FpcLibraryEnviron.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace realms {
namespace {

constexpr int kAreaDepth = 4;
constexpr int kAreaNodes = 32;
constexpr int kMaxTotalEntLeafs = 128;

// svc_ops_e (qcommon.h): the ops the game writes.
enum { svc_muzzleflash = 1, svc_muzzleflash2 = 2, svc_temp_entity = 3, svc_layout = 4, svc_inventory = 5, svc_stufftext = 11 };
// temp_event_t values whose first field is not a position.

// entity_event_t
enum { EV_ITEM_RESPAWN = 1, EV_FOOTSTEP = 2, EV_FALLSHORT = 3, EV_FALL = 4, EV_FALLFAR = 5, EV_PLAYER_TELEPORT = 6,
       EV_OTHER_TELEPORT = 7 };

struct ModuleState {
    std::mutex mutex;
    Q2GetGameApiFn fn = nullptr;
    std::string origin;
    std::string error;
    bool searched = false;
    bool broken = false; // a game error longjmp'd out of the module: don't re-enter it
};
ModuleState &moduleState() {
    static ModuleState state;
    return state;
}
std::atomic<Q2GetGameApiFn> gLinkedGame{nullptr};
GameServer *gServer = nullptr;

const char *gameLibraryName() {
#if defined(_WIN32)
    return "realms_game.dll";
#elif defined(__APPLE__)
    return "librealms_game.dylib";
#else
    return "librealms_game.so";
#endif
}

void *openLib(const std::string &path, std::string &error) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) error = "LoadLibrary failed (" + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void *>(h);
#else
    const detail::FpcLibraryEnviron fpcEnviron;
    void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *e = dlerror();
        error = e ? e : "dlopen failed";
    }
    return h;
#endif
}

void *libSymbol(void *lib, const char *name) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

bool locateModule(const std::string &executableDir, const std::string &explicitPath) {
    ModuleState &st = moduleState();
    if (Q2GetGameApiFn linked = gLinkedGame.load()) {
        st.fn = linked;
        st.origin = "linked";
        return true;
    }
    if (st.fn) return true;
    std::vector<std::string> candidates;
    if (!explicitPath.empty()) candidates.push_back(explicitPath);
    if (const char *env = std::getenv("REALMS_GAME_LIBRARY"); env && *env) candidates.emplace_back(env);
    if (!executableDir.empty()) {
        const fs::path exe(executableDir);
        candidates.push_back((exe / gameLibraryName()).string());
        candidates.push_back((exe / ".." / "Frameworks" / gameLibraryName()).string());
        candidates.push_back((exe / ".." / "lib" / "ekron-realms" / gameLibraryName()).string());
    }
    candidates.emplace_back(gameLibraryName());
    std::string errors;
    for (const auto &path : candidates) {
        const bool bare = path.find('/') == std::string::npos && path.find('\\') == std::string::npos;
        if (!bare) {
            std::error_code ec;
            if (!fs::exists(path, ec)) continue;
        }
        std::string err;
        void *lib = openLib(path, err);
        if (!lib) {
            if (!bare) errors += path + ": " + err + "; ";
            continue;
        }
        auto fn = reinterpret_cast<Q2GetGameApiFn>(libSymbol(lib, Q2_GAME_ENTRY_SYMBOL));
        if (!fn) {
            errors += path + ": missing " Q2_GAME_ENTRY_SYMBOL "; ";
            continue;
        }
        st.fn = fn;
        st.origin = path;
        return true;
    }
    st.error = errors.empty() ? std::string(gameLibraryName()) + " not found" : errors;
    return false;
}

inline Vec3 V(const float *v) { return v ? Vec3{v[0], v[1], v[2]} : Vec3{0, 0, 0}; }

std::string vformat(const char *fmt, va_list ap) {
    if (!fmt) return {};
    char buf[2048];
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);
    if (n < 0) return {};
    if (size_t(n) < sizeof(buf)) return std::string(buf, size_t(n));
    std::string big(size_t(n) + 1, '\0');
    std::vsnprintf(big.data(), big.size(), fmt, ap);
    big.resize(size_t(n));
    return big;
}

} // namespace

struct GameServer::CvarMirror {
    q2cvar_t cv{};
    std::string name;
    std::string value;
};

struct GameServer::TagBlock {
    TagBlock *prev = nullptr;
    TagBlock *next = nullptr;
    int tag = 0;
    int size = 0;
    alignas(16) unsigned char data[1];
};

// ------------------------------------------------------------ the imports
//
// game_import_t has no user pointer: every thunk talks to gServer, the one
// running GameServer (Quake 2's sv / svs globals).

struct GameImports {
    static void bprintf(int32_t level, const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        const std::string text = vformat(fmt, ap);
        va_end(ap);
        if (gServer) gServer->print(level, text, GameEvent::Kind::Print, 0);
    }
    static void dprintf(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        const std::string text = vformat(fmt, ap);
        va_end(ap);
        if (gServer && gServer->engine_) gServer->engine_->print(text, REALMS_PRINT_DEVELOPER);
    }
    static void cprintf(q2edict_t *ent, int32_t level, const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        const std::string text = vformat(fmt, ap);
        va_end(ap);
        if (!gServer) return;
        if (!ent) {
            if (gServer->engine_) gServer->engine_->print(text);
            return;
        }
        gServer->print(level, text, GameEvent::Kind::Print, gServer->edictIndex(ent));
    }
    static void centerprintf(q2edict_t *ent, const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        const std::string text = vformat(fmt, ap);
        va_end(ap);
        if (gServer) gServer->print(Q2_PRINT_HIGH, text, GameEvent::Kind::CenterPrint, gServer->edictIndex(ent));
    }
    static void error(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        std::string text = vformat(fmt, ap);
        va_end(ap);
        if (gServer) gServer->gameError(text); // longjmps when inside a game call
    }
    static void sound(q2edict_t *ent, int32_t channel, int32_t soundindex, float volume, float attenuation,
                      float timeofs) {
        if (gServer) gServer->queueSound(nullptr, ent, channel, soundindex, volume, attenuation, timeofs);
    }
    static void positionedSound(q2vec3_t origin, q2edict_t *ent, int32_t channel, int32_t soundindex, float volume,
                                float attenuation, float timeofs) {
        if (gServer) gServer->queueSound(origin, ent, channel, soundindex, volume, attenuation, timeofs);
    }
    static void configstring(int32_t num, const char *string) {
        if (!gServer) return;
        if (num < 0 || num >= Q2_MAX_CONFIGSTRINGS) {
            gServer->gameError("configstring: bad index " + std::to_string(num));
            return;
        }
        gServer->setConfigString(num, string ? string : "");
    }
    static int32_t modelindex(const char *name) {
        return gServer ? gServer->findIndex(name, Q2_CS_MODELS, Q2_MAX_MODELS, true) : 0;
    }
    static int32_t soundindex(const char *name) {
        return gServer ? gServer->findIndex(name, Q2_CS_SOUNDS, Q2_MAX_SOUNDS, true) : 0;
    }
    static int32_t imageindex(const char *name) {
        return gServer ? gServer->findIndex(name, Q2_CS_IMAGES, Q2_MAX_IMAGES, true) : 0;
    }
    // PF_setmodel
    static void setmodel(q2edict_t *ent, const char *name) {
        if (!gServer || !ent) return;
        if (!name) {
            gServer->gameError("PF_setmodel: NULL");
            return;
        }
        const int i = gServer->findIndex(name, Q2_CS_MODELS, Q2_MAX_MODELS, true);
        ent->s.modelindex = i;
        // if it is an inline model, get the size information for it
        if (name[0] == '*') {
            MapModel mod;
            if (gServer->engine_->mapModel(std::atoi(name + 1), mod)) {
                for (int k = 0; k < 3; k++) {
                    ent->mins[k] = mod.mins[k];
                    ent->maxs[k] = mod.maxs[k];
                }
            }
            gServer->linkEdict(ent);
        }
    }
    static q2trace_t trace(q2vec3_t start, q2vec3_t mins, q2vec3_t maxs, q2vec3_t end, q2edict_t *passent,
                           int32_t contentmask) {
        return gServer ? gServer->trace(start, mins, maxs, end, passent, contentmask) : q2trace_t{};
    }
    static int32_t pointcontents(q2vec3_t point) { return gServer ? gServer->pointContents(point) : 0; }
    static q2bool inPVS(q2vec3_t p1, q2vec3_t p2) { return gServer && gServer->inPVS(p1, p2, false); }
    static q2bool inPHS(q2vec3_t p1, q2vec3_t p2) { return gServer && gServer->inPVS(p1, p2, true); }
    static void setAreaPortalState(int32_t portalnum, q2bool open) {
        if (gServer) gServer->engine_->setAreaPortal(portalnum, open != 0);
    }
    static q2bool areasConnected(int32_t area1, int32_t area2) {
        return gServer && gServer->engine_->areasConnected(area1, area2);
    }
    static void linkentity(q2edict_t *ent) {
        if (gServer) gServer->linkEdict(ent);
    }
    static void unlinkentity(q2edict_t *ent) {
        if (gServer) gServer->unlinkEdict(ent);
    }
    static int32_t boxEdicts(q2vec3_t mins, q2vec3_t maxs, q2edict_t **list, int32_t maxcount, int32_t areatype) {
        return gServer ? gServer->areaEdicts(mins, maxs, list, maxcount, areatype) : 0;
    }
    static void pmove(q2pmove_t *pm) {
        if (!gServer) return;
        q2::PmoveParams params;
        params.airAccelerate = gServer->engine_->cvarFloat("sv_airaccelerate", 0.0f) *
                               (gServer->engine_->cvarFloat("deathmatch", 0.0f) != 0.0f ? 1.0f : 0.0f);
        q2::Pmove(pm, params);
    }
    static void multicast(q2vec3_t origin, q2multicast_t) {
        if (gServer) gServer->flushMessage(origin, 0);
    }
    static void unicast(q2edict_t *ent, q2bool) {
        if (gServer) gServer->flushMessage(nullptr, gServer->edictIndex(ent));
    }
    static void put(const void *p, size_t n) {
        if (!gServer) return;
        const auto *b = static_cast<const unsigned char *>(p);
        gServer->msg_.insert(gServer->msg_.end(), b, b + n);
    }
    static void writeChar(int32_t c) {
        const int8_t v = int8_t(c);
        put(&v, 1);
    }
    static void writeByte(int32_t c) {
        const uint8_t v = uint8_t(c);
        put(&v, 1);
    }
    static void writeShort(int32_t c) {
        const uint8_t v[2] = {uint8_t(c & 0xff), uint8_t((c >> 8) & 0xff)};
        put(v, 2);
    }
    static void writeLong(int32_t c) {
        const uint8_t v[4] = {uint8_t(c & 0xff), uint8_t((c >> 8) & 0xff), uint8_t((c >> 16) & 0xff),
                              uint8_t((uint32_t(c) >> 24) & 0xff)};
        put(v, 4);
    }
    static void writeFloat(float f) {
        int32_t i;
        std::memcpy(&i, &f, 4);
        writeLong(i);
    }
    static void writeString(const char *s) {
        if (s) put(s, std::strlen(s));
        writeByte(0);
    }
    static void writePosition(q2vec3_t pos) {
        for (int i = 0; i < 3; i++) writeShort(int(pos[i] * 8));
    }
    // MSG_WriteDir: the index of the closest of the 162 anorms.
    static void writeDir(q2vec3_t dir) { writeByte(gameDirToByte(dir)); }
    static void writeAngle(float f) { writeByte(int(f * 256 / 360) & 255); }
    static void *tagMalloc(int32_t size, int32_t tag) { return gServer ? gServer->tagMalloc(size, tag) : nullptr; }
    static void tagFree(void *block) {
        if (gServer) gServer->tagFree(block);
    }
    static void freeTags(int32_t tag) {
        if (gServer) gServer->freeTags(tag);
    }
    static q2cvar_t *cvar(const char *name, const char *value, int32_t flags) {
        return gServer ? gServer->cvarGet(name, value, flags) : nullptr;
    }
    static q2cvar_t *cvarSet(const char *name, const char *value) {
        return gServer ? gServer->cvarSet(name, value, false) : nullptr;
    }
    static q2cvar_t *cvarForceSet(const char *name, const char *value) {
        return gServer ? gServer->cvarSet(name, value, true) : nullptr;
    }
    static int32_t argc() { return gServer ? int32_t(gServer->argv_.size()) : 0; }
    static char *argv(int32_t n) {
        static char empty[1] = {0};
        if (!gServer || n < 0 || n >= int32_t(gServer->argv_.size())) return empty;
        return gServer->argv_[size_t(n)].data();
    }
    static char *args() {
        static char empty[1] = {0};
        return gServer ? gServer->args_.data() : empty;
    }
    static void addCommandString(const char *text) {
        if (gServer && gServer->engine_ && text) gServer->engine_->addText(text);
    }
    static void debugGraph(float, int32_t) {}

    static q2game_import_t table() {
        q2game_import_t gi{};
        gi.bprintf = &bprintf;
        gi.dprintf = &dprintf;
        gi.cprintf = &cprintf;
        gi.centerprintf = &centerprintf;
        gi.sound = &sound;
        gi.positioned_sound = &positionedSound;
        gi.configstring = &configstring;
        gi.error = &error;
        gi.modelindex = &modelindex;
        gi.soundindex = &soundindex;
        gi.imageindex = &imageindex;
        gi.setmodel = &setmodel;
        gi.trace = &trace;
        gi.pointcontents = &pointcontents;
        gi.inPVS = &inPVS;
        gi.inPHS = &inPHS;
        gi.SetAreaPortalState = &setAreaPortalState;
        gi.AreasConnected = &areasConnected;
        gi.linkentity = &linkentity;
        gi.unlinkentity = &unlinkentity;
        gi.BoxEdicts = &boxEdicts;
        gi.Pmove = &pmove;
        gi.multicast = &multicast;
        gi.unicast = &unicast;
        gi.WriteChar = &writeChar;
        gi.WriteByte = &writeByte;
        gi.WriteShort = &writeShort;
        gi.WriteLong = &writeLong;
        gi.WriteFloat = &writeFloat;
        gi.WriteString = &writeString;
        gi.WritePosition = &writePosition;
        gi.WriteDir = &writeDir;
        gi.WriteAngle = &writeAngle;
        gi.TagMalloc = &tagMalloc;
        gi.TagFree = &tagFree;
        gi.FreeTags = &freeTags;
        gi.cvar = &cvar;
        gi.cvar_set = &cvarSet;
        gi.cvar_forceset = &cvarForceSet;
        gi.argc = &argc;
        gi.argv = &argv;
        gi.args = &args;
        gi.AddCommandString = &addCommandString;
        gi.DebugGraph = &debugGraph;
        return gi;
    }
};

namespace {
q2game_import_t gImports; // must outlive the game: GetGameAPI copies it, but keep it stable anyway
}

// ------------------------------------------------------------ module

GameServer::GameServer() = default;

GameServer::~GameServer() {
    stop();
    if (ge_ && gameInitialised_) {
        // The game is shut down by stop(); nothing else to release.
    }
}

void GameServer::setLinkedGame(Q2GetGameApiFn getGameApi) {
    gLinkedGame.store(getGameApi);
    ModuleState &st = moduleState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.fn = getGameApi;
    st.origin = getGameApi ? "linked" : "";
}

bool GameServer::moduleAvailable(const std::string &executableDir, std::string *where, std::string *error) {
    ModuleState &st = moduleState();
    std::lock_guard<std::mutex> lock(st.mutex);
    const bool ok = locateModule(executableDir, {}) && !st.broken;
    if (where) *where = st.origin;
    if (error) *error = st.broken ? "game module stopped after a game error" : st.error;
    return ok;
}

bool GameServer::loadModule(std::string &error) {
    ModuleState &st = moduleState();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.broken) {
        error = "the game module stopped after a game error; restart the program";
        return false;
    }
    if (!locateModule(config_.executableDir, config_.gameLibrary)) {
        error = st.error;
        return false;
    }
    moduleOrigin_ = st.origin;
    if (!ge_) {
        gImports = GameImports::table();
        ge_ = st.fn(&gImports);
        if (!ge_) {
            error = "GetGameAPI returned nothing";
            return false;
        }
        if (ge_->apiversion != Q2_GAME_API_VERSION) {
            error = "game is version " + std::to_string(ge_->apiversion) + ", not " +
                    std::to_string(Q2_GAME_API_VERSION);
            ge_ = nullptr;
            return false;
        }
        if (ge_->edict_size < int32_t(sizeof(q2edict_t))) {
            error = "game edict_size " + std::to_string(ge_->edict_size) + " is smaller than the server's edict_t";
            ge_ = nullptr;
            return false;
        }
    }
    return true;
}

// Quake 2 longjmps out of the game on Com_Error (abortframe). The same
// happens here; afterwards the module is not entered again.
bool GameServer::guarded(const char *what, void (*fn)(GameServer &, void *), void *arg) {
    if (inGameCall_) {
        fn(*this, arg);
        return true;
    }
    syncCvars();
    std::jmp_buf jump;
    abortJump_ = &jump;
    inGameCall_ = true;
    if (setjmp(jump) == 0) {
        fn(*this, arg);
        inGameCall_ = false;
        abortJump_ = nullptr;
        return true;
    }
    inGameCall_ = false;
    abortJump_ = nullptr;
    {
        ModuleState &st = moduleState();
        std::lock_guard<std::mutex> lock(st.mutex);
        st.broken = true;
    }
    if (engine_) engine_->print(std::string("Game error during ") + what + ": " + lastError_ + "\n", REALMS_PRINT_ERROR);
    running_ = false;
    gameInitialised_ = false;
    ge_ = nullptr;
    unregisterCommands();
    if (gServer == this) gServer = nullptr;
    return false;
}

void GameServer::gameError(const std::string &text) {
    lastError_ = text;
    if (abortJump_) {
        // No objects with destructors live between here and the setjmp in
        // guarded(): text is owned by the caller's frame, which is skipped
        // like the Pascal frames above it (as Com_Error does in Quake 2).
        std::longjmp(*static_cast<std::jmp_buf *>(abortJump_), 1);
    }
    if (engine_) engine_->print("Game error: " + text + "\n", REALMS_PRINT_ERROR);
}

// ------------------------------------------------------------ save games

namespace {

bool readFileBytes(const fs::path &path, std::vector<unsigned char> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFileBytes(const fs::path &path, const std::vector<unsigned char> &bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    return f && f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

} // namespace

bool GameServer::prepareLoad(const std::string &dir, std::string &levelFile, std::string &error) {
    // what this build writes (a fresh game: no client or entity in use)
    const fs::path probeGame = fs::path(dir) / "probe.ssv", probeLevel = fs::path(dir) / "probe.sav";
    struct Paths {
        std::string game, level;
    } probe{probeGame.string(), probeLevel.string()};
    const bool probed = guarded(
        "WriteGame",
        [](GameServer &s, void *a) {
            auto *p = static_cast<Paths *>(a);
            s.ge_->WriteGame(p->game.c_str(), 1);
            s.ge_->WriteLevel(p->level.c_str());
        },
        &probe);
    std::vector<unsigned char> nowGame, nowLevel, savedGame, savedLevel;
    const bool readProbes = probed && readFileBytes(probeGame, nowGame) && readFileBytes(probeLevel, nowLevel);
    std::error_code ec;
    fs::remove(probeGame, ec);
    fs::remove(probeLevel, ec);
    if (!probed) {
        error = "the game failed: " + lastError_;
        return false;
    }
    if (!readProbes || !readFileBytes(fs::path(dir) / kGameSaveGameFile, savedGame) ||
        !readFileBytes(fs::path(dir) / kGameSaveLevelFile, savedLevel)) {
        error = "can't read the save game files";
        return false;
    }
    // game.ssv starts with the build date (16 bytes); WriteGame / ReadGame
    if (savedGame.size() < 16 || nowGame.size() < 16 || !std::equal(nowGame.begin(), nowGame.begin() + 16, savedGame.begin())) {
        error = "the game was saved by another build of the game";
        return false;
    }
    // level.sav: int32 sizeof(edict_t), the address of InitGame, then the
    // level. Function pointers are stored relative to InitGame, so the
    // address is only a check (on Windows it moves between runs): take this
    // run's in a copy.
    constexpr size_t kHeader = sizeof(int32_t) + sizeof(void *);
    int32_t nowEdict = 0, savedEdict = 0;
    if (nowLevel.size() >= kHeader) std::memcpy(&nowEdict, nowLevel.data(), sizeof(nowEdict));
    if (savedLevel.size() >= kHeader) std::memcpy(&savedEdict, savedLevel.data(), sizeof(savedEdict));
    if (nowEdict != ge_->edict_size || savedEdict != nowEdict) {
        error = "the level was saved by another build of the game";
        return false;
    }
    std::copy(nowLevel.begin() + sizeof(int32_t), nowLevel.begin() + kHeader, savedLevel.begin() + sizeof(int32_t));
    const fs::path copy = fs::path(dir) / "load.sav";
    if (!writeFileBytes(copy, savedLevel)) {
        error = "can't write " + copy.string();
        return false;
    }
    levelFile = copy.string();
    return true;
}

bool GameServer::saveGame(const std::string &name, std::string &error) {
    if (!running_ || !ge_ || !engine_) {
        error = "You must be in a game to save.";
        return false;
    }
    const q2player_state_t *ps = playerState();
    if (!ps || ps->stats[Q2_STAT_HEALTH] <= 0) {
        error = "Can't savegame while dead!";
        return false;
    }
    const std::string dir = gameSaveDir(*engine_, name);
    if (dir.empty()) {
        error = engine_->writableRoot().empty() ? "no writable folder for save games" : "Bad savedir.";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "can't create " + dir + ": " + ec.message();
        return false;
    }
    // server.ssv last, and gone meanwhile: a half-written save is never loaded
    fs::remove(fs::path(dir) / kGameSaveServerFile, ec);

    GameSaveInfo info;
    char when[32] = "";
    const std::time_t now = std::time(nullptr);
    if (const std::tm *t = std::localtime(&now)) std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", t);
    info.comment = mapName_ + "  " + when;
    info.mapPath = engine_->mapPath();
    info.skill = engine_->cvarInt("skill", 1);
    info.configStrings = configStrings_;
    RealmsMapInfo map{};
    map.struct_size = sizeof(map);
    engine_->mapInfo(map);
    for (int p = 0; p < map.num_area_portals; p++) info.portals.push_back(engine_->areaPortalState(p) == 1 ? 1 : 0);

    // SV_WriteLevelFile, then SV_WriteServerFile's WriteGame (not an autosave)
    struct Paths {
        std::string level, game;
    } paths{(fs::path(dir) / kGameSaveLevelFile).string(), (fs::path(dir) / kGameSaveGameFile).string()};
    if (!guarded(
            "WriteLevel",
            [](GameServer &s, void *a) {
                auto *p = static_cast<Paths *>(a);
                s.ge_->WriteLevel(p->level.c_str());
                s.ge_->WriteGame(p->game.c_str(), 0);
            },
            &paths)) {
        error = "the game failed to save: " + lastError_;
        return false;
    }
    // the module's FileCreate failure check never fires (FPC returns -1)
    for (const std::string &file : {paths.level, paths.game})
        if (fs::file_size(file, ec) == 0 || ec) {
            error = "can't write " + file;
            return false;
        }
    return writeGameSaveInfo(dir, info, error);
}

// ------------------------------------------------------------ start / stop

bool GameServer::start(Engine &engine, const GameServerConfig &config, std::string &error) {
    stop();
    if (gServer && gServer != this) {
        error = "another game server is running";
        return false;
    }
    if (!engine.initialized() || !engine.mapLoaded()) {
        error = "no map loaded";
        return false;
    }
    engine_ = &engine;
    config_ = config;
    lastError_.clear();
    if (!loadModule(error)) {
        engine_ = nullptr;
        return false;
    }
    gServer = this;

    // SV_InitGame: single player.
    engine.registerCvar("deathmatch", "0", REALMS_CVAR_LATCH);
    engine.registerCvar("coop", "0", REALMS_CVAR_LATCH);
    engine.registerCvar("skill", std::to_string(config.skill), REALMS_CVAR_LATCH);
    engine.registerCvar("maxclients", "1", REALMS_CVAR_SERVERINFO | REALMS_CVAR_LATCH);
    engine.forceSetCvar("deathmatch", "0");
    engine.forceSetCvar("coop", "0");
    engine.forceSetCvar("maxclients", "1");
    engine.registerCvar("sv_airaccelerate", "0", 0);
    engine.registerCvar("dedicated", "0", REALMS_CVAR_NOSET);

    // A saved game to read back ("load <name>" set sv_loadgame and loaded
    // its map); a stale request for another map is dropped.
    GameSaveInfo save;
    std::string loadDir = engine.cvarString("sv_loadgame", "");
    if (!loadDir.empty()) {
        engine.forceSetCvar("sv_loadgame", "");
        std::string why;
        if (!gameSaveComplete(loadDir) || !readGameSaveInfo(loadDir, save, why)) {
            engine.print("load: " + (why.empty() ? "incomplete save game in " + loadDir : why) + "\n",
                         REALMS_PRINT_WARNING);
            loadDir.clear();
        } else if (save.mapPath != engine.mapPath()) {
            loadDir.clear();
        } else {
            engine.forceSetCvar("skill", std::to_string(save.skill));
        }
    }

    if (!gameInitialised_) {
        if (!guarded("Init", [](GameServer &s, void *) { s.ge_->Init(); }, nullptr)) {
            error = "game Init failed: " + lastError_;
            return false;
        }
        gameInitialised_ = true;
        // svs.clients: one local client in edict 1
        if (ge_->max_edicts < 2 || !ge_->edicts) {
            error = "game Init left no edicts";
            stop();
            return false;
        }
    }

    // SV_ReadServerFile: the game's cross-level state, after Init. The
    // module's checks (build date, edict size) would end the game for good,
    // so the save is compared with this build first.
    std::string levelFile;
    if (!loadDir.empty()) {
        if (!prepareLoad(loadDir, levelFile, error)) {
            engine.print("load: " + error + "\n", REALMS_PRINT_WARNING);
            if (!ge_) return false; // the probe failed inside the game
            loadDir.clear();
            levelFile.clear();
            error.clear();
        } else {
            engine.print("Loading game: " + save.comment + "\n");
            std::string gameFile = (fs::path(loadDir) / kGameSaveGameFile).string();
            if (!guarded("ReadGame", [](GameServer &s, void *a) { s.ge_->ReadGame(static_cast<std::string *>(a)->c_str()); },
                         &gameFile)) {
                error = "ReadGame failed: " + lastError_;
                return false;
            }
        }
    }

    // SV_SpawnServer
    RealmsMapInfo info{};
    info.struct_size = sizeof(info);
    engine.mapInfo(info);
    std::string path = engine.mapPath();
    std::string name = fs::path(path).stem().string();
    mapName_ = name;
    configStrings_.assign(Q2_MAX_CONFIGSTRINGS, std::string());
    inventory_.fill(0);
    modelHeadnodes_.assign(Q2_MAX_MODELS, -1);
    configStrings_[Q2_CS_MODELS + 1] = path;
    modelHeadnodes_[1] = 0;
    char checksum[32];
    std::snprintf(checksum, sizeof(checksum), "%d", int32_t(info.checksum));
    configStrings_[Q2_CS_MAPCHECKSUM] = checksum;
    configStrings_[Q2_CS_AIRACCEL] = engine.cvarString("sv_airaccelerate", "0");
    configStrings_[Q2_CS_MAXCLIENTS] = "1";
    for (int i = 1; i < info.num_models && i + 1 < Q2_MAX_MODELS; i++) {
        MapModel mod;
        if (!engine.mapModel(i, mod)) break;
        configStrings_[size_t(Q2_CS_MODELS + 1 + i)] = "*" + std::to_string(i);
        modelHeadnodes_[size_t(1 + i)] = mod.headnode;
    }
    for (int p = 0; p < info.num_area_portals; p++) engine.setAreaPortal(p, false);
    clearWorld();
    frameNum_ = 0;
    frameAccumulator_ = 0.0;
    msecAccumulator_ = 0.0;
    lastCmd_ = q2usercmd_t{};
    events_.clear();
    msg_.clear();

    // clients[i].edict
    q2edict_t *client = edictNum(1);
    client->s.number = 1;

    struct SpawnArgs {
        std::string name, entities, spawnpoint;
    } spawn{name, engine.mapEntities(), config.spawnPoint};
    if (!guarded(
            "SpawnEntities",
            [](GameServer &s, void *a) {
                auto *sp = static_cast<SpawnArgs *>(a);
                s.ge_->SpawnEntities(sp->name.c_str(), sp->entities.c_str(), sp->spawnpoint.c_str());
                // run two frames to allow everything to settle
                s.ge_->RunFrame();
                s.ge_->RunFrame();
            },
            &spawn)) {
        error = "SpawnEntities failed: " + lastError_;
        return false;
    }
    frameNum_ = 2;

    // SV_CheckForSavegame / SV_ReadLevelFile: the saved config strings and
    // area portals, then every entity, over the freshly spawned level
    if (!levelFile.empty()) {
        clearWorld();
        for (size_t i = 0; i < configStrings_.size(); i++)
            configStrings_[i] = i < save.configStrings.size() ? save.configStrings[i] : std::string();
        for (int p = 0; p < info.num_area_portals; p++)
            engine.setAreaPortal(p, size_t(p) < save.portals.size() && save.portals[size_t(p)] != 0);
        const bool read = guarded(
            "ReadLevel", [](GameServer &s, void *a) { s.ge_->ReadLevel(static_cast<std::string *>(a)->c_str()); },
            &levelFile);
        std::error_code ec;
        fs::remove(levelFile, ec);
        if (!read) {
            error = "ReadLevel failed: " + lastError_;
            return false;
        }
    }

    // SV_DirectConnect + SV_Begin_f for the local client
    std::snprintf(userinfo_, sizeof(userinfo_), "\\name\\%s\\skin\\male/grunt\\hand\\0\\fov\\90\\rate\\25000\\msg\\1\\ip\\loopback",
                  config.playerName.c_str());
    struct ConnectArgs {
        bool ok = false;
    } connect;
    if (!guarded(
            "ClientConnect",
            [](GameServer &s, void *a) {
                static_cast<ConnectArgs *>(a)->ok = s.ge_->ClientConnect(s.edictNum(1), s.userinfo_) != 0;
            },
            &connect)) {
        error = "ClientConnect failed: " + lastError_;
        return false;
    }
    if (!connect.ok) {
        error = "the game rejected the local client";
        stop();
        return false;
    }
    if (!guarded("ClientBegin", [](GameServer &s, void *) { s.ge_->ClientBegin(s.edictNum(1)); }, nullptr)) {
        error = "ClientBegin failed: " + lastError_;
        return false;
    }
    running_ = true;
    registerCommands();
    return true;
}

void GameServer::stop() {
    if (gServer != this) {
        running_ = false;
        return;
    }
    unregisterCommands();
    if (ge_ && gameInitialised_) {
        if (running_) {
            guarded("ClientDisconnect", [](GameServer &s, void *) { s.ge_->ClientDisconnect(s.edictNum(1)); }, nullptr);
        }
        if (ge_) guarded("Shutdown", [](GameServer &s, void *) { s.ge_->Shutdown(); }, nullptr);
    }
    gameInitialised_ = false;
    // The game keeps its globals; GetGameAPI is called again next time.
    ge_ = nullptr;
    running_ = false;
    freeTags(-1);
    cvars_.clear();
    argv_.clear();
    args_.clear();
    events_.clear();
    numAreaNodes_ = 0;
    areaNodes_.clear();
    gServer = nullptr;
    engine_ = nullptr;
}

// ------------------------------------------------------------ frames

void GameServer::prepWorldFrame() {
    // SV_PrepWorldFrame clears entity events after they were sent; the
    // sandbox gets each one as an EntityEvent (for the particles) and the
    // sound CL_EntityEvent plays first.
    static const char *footsteps[4] = {"player/step1.wav", "player/step2.wav", "player/step3.wav", "player/step4.wav"};
    for (int i = 1; i < numEdicts(); i++) {
        q2edict_t *ent = edictNum(i);
        if (!ent->inuse || !ent->s.event) continue;
        GameEvent ee;
        ee.kind = GameEvent::Kind::EntityEvent;
        ee.level = ent->s.event;
        ee.entity = i;
        ee.origin = V(ent->s.origin);
        ee.angles = V(ent->s.angles);
        GameEvent ev;
        ev.kind = GameEvent::Kind::Sound;
        ev.entity = i;
        ev.origin = ee.origin;
        ev.entityEvent = ent->s.event;
        switch (ent->s.event) {
        case EV_ITEM_RESPAWN:
            ev.text = "sound/items/respawn1.wav";
            ev.level = Q2_CHAN_WEAPON;
            ev.attenuation = 2; // ATTN_IDLE
            break;
        case EV_FOOTSTEP:
            ev.text = std::string("sound/") + footsteps[(frameNum_ + i) & 3];
            ev.level = Q2_CHAN_BODY;
            break;
        case EV_FALLSHORT: ev.text = "sound/player/land1.wav"; break;
        case EV_FALL: ev.text = "players/male/fall2.wav"; break;
        case EV_FALLFAR: ev.text = "players/male/fall1.wav"; break;
        case EV_PLAYER_TELEPORT:
        case EV_OTHER_TELEPORT:
            ev.text = "sound/misc/tele1.wav";
            ev.level = Q2_CHAN_WEAPON;
            ev.attenuation = 2;
            break;
        default: break;
        }
        events_.push_back(std::move(ee));
        if (!ev.text.empty()) events_.push_back(std::move(ev));
        ent->s.event = 0;
    }
}

int GameServer::runFrames(int count) {
    int ran = 0;
    for (int n = 0; n < count && running_; n++) {
        prepWorldFrame();
        frameNum_++;
        if (!guarded("RunFrame", [](GameServer &s, void *) { s.ge_->RunFrame(); }, nullptr)) break;
        ran++;
    }
    return ran;
}

void GameServer::sendUserCmd(int msec, const GameUserCmd &in) {
    q2usercmd_t cmd{};
    cmd.msec = uint8_t(std::clamp(msec, 1, 250));
    // The game views cmd.angles + delta_angles (set at spawn / teleport, see
    // PutClientInServer); GameUserCmd holds the absolute view, so take the
    // delta out like a Quake 2 client's accumulated cl.viewangles would.
    const q2player_state_t *ps = playerState();
    const int dp = ps ? ps->pmove.delta_angles[0] : 0, dy = ps ? ps->pmove.delta_angles[1] : 0;
    cmd.angles[0] = int16_t(uint16_t(q2::angleToShort(in.pitch) - dp));
    cmd.angles[1] = int16_t(uint16_t(q2::angleToShort(in.yaw) - dy));
    cmd.angles[2] = 0;
    auto move = [](float v) { return int16_t(std::lround(std::clamp(v, -400.0f, 400.0f))); };
    cmd.forwardmove = move(in.forward);
    cmd.sidemove = move(in.side);
    cmd.upmove = move(in.up);
    if (in.attack) cmd.buttons |= Q2_BUTTON_ATTACK;
    if (in.use) cmd.buttons |= Q2_BUTTON_USE;
    if (in.attack || in.use || in.up > 0) cmd.buttons |= Q2_BUTTON_ANY;
    cmd.impulse = uint8_t(std::clamp(in.impulse, 0, 255));
    cmd.lightlevel = 128; // g_ai FindTarget ignores players in the dark (light_level <= 5)
    lastCmd_ = cmd;
    guarded("ClientThink", [](GameServer &s, void *) { s.ge_->ClientThink(s.edictNum(1), &s.lastCmd_); }, nullptr);
}

int GameServer::frame(const GameUserCmd &cmd, double dt) {
    if (!running_ || dt <= 0) return 0;
    dt = std::min(dt, 0.5);
    msecAccumulator_ += dt * 1000.0;
    int ms = int(msecAccumulator_);
    GameUserCmd c = cmd;
    while (ms > 0 && running_) {
        const int m = std::min(ms, 250);
        sendUserCmd(m, c);
        c.impulse = 0; // an impulse goes out once
        ms -= m;
        msecAccumulator_ -= m;
    }
    frameAccumulator_ += dt;
    int ran = 0;
    while (frameAccumulator_ >= kFrameSeconds && running_) {
        frameAccumulator_ -= kFrameSeconds;
        ran += runFrames(1);
        if (ran >= 5) {
            frameAccumulator_ = 0;
            break;
        }
    }
    return ran;
}

// ------------------------------------------------------------ commands

void GameServer::tokenize(const std::string &line) {
    // Cmd_TokenizeString: quoted strings, // comments; args = text after argv[0].
    argv_.clear();
    args_.clear();
    size_t i = 0;
    const size_t n = line.size();
    bool first = true;
    while (i < n) {
        while (i < n && (unsigned char)line[i] <= ' ' && line[i] != '\n') i++;
        if (i >= n || line[i] == '\n') break;
        if (!first && args_.empty()) {
            args_ = line.substr(i);
            while (!args_.empty() && (unsigned char)args_.back() <= ' ') args_.pop_back();
        }
        if (line[i] == '/' && i + 1 < n && line[i + 1] == '/') break;
        std::string tok;
        if (line[i] == '"') {
            i++;
            while (i < n && line[i] != '"') tok += line[i++];
            if (i < n) i++;
        } else {
            while (i < n && (unsigned char)line[i] > ' ') tok += line[i++];
        }
        argv_.push_back(tok);
        first = false;
    }
}

void GameServer::clientCommand(const std::string &line) {
    if (!running_) return;
    tokenize(line);
    if (argv_.empty()) return;
    guarded("ClientCommand", [](GameServer &s, void *) { s.ge_->ClientCommand(s.edictNum(1)); }, nullptr);
}

void GameServer::serverCommand(const std::string &line) {
    if (!running_) return;
    tokenize("sv " + line);
    guarded("ServerCommand", [](GameServer &s, void *) { s.ge_->ServerCommand(); }, nullptr);
}

void GameServer::registerCommands() {
    if (!engine_) return;
    // Client commands the game module handles (g_cmds.c ClientCommand),
    // forwarded like Quake 2's "cmd" forwarding of unknown commands.
    static const char *kForwarded[] = {"give", "god", "notarget", "noclip", "kill", "use", "drop", "inven",
                                       "invuse", "invdrop", "invnext", "invprev", "invnextw", "invprevw",
                                       "invnextp", "invprevp", "weapnext", "weapprev", "weaplast", "putaway",
                                       "wave", "help", "score", "players"};
    auto quoteArgs = [](const CommandArgs &args) {
        std::string line;
        for (const auto &a : args) {
            if (!line.empty()) line += ' ';
            const bool quote = a.empty() || a.find_first_of(" \t;") != std::string::npos;
            line += quote ? "\"" + a + "\"" : a;
        }
        return line;
    };
    for (const char *name : kForwarded) {
        if (engine_->commandExists(name)) continue;
        if (engine_->addCommand(name, [this, quoteArgs](const CommandArgs &args) { clientCommand(quoteArgs(args)); }))
            commands_.emplace_back(name);
    }
    if (!engine_->commandExists("cmd") && engine_->addCommand("cmd", [this, quoteArgs](const CommandArgs &args) {
            if (args.size() < 2) return;
            clientCommand(quoteArgs(CommandArgs(args.begin() + 1, args.end())));
        }))
        commands_.emplace_back("cmd");
    if (!engine_->commandExists("sv") && engine_->addCommand("sv", [this, quoteArgs](const CommandArgs &args) {
            if (args.size() < 2) {
                if (engine_) engine_->print("usage: sv <command> (addip, removeip, listip, writeip, test)\n");
                return;
            }
            serverCommand(quoteArgs(CommandArgs(args.begin() + 1, args.end())));
        }))
        commands_.emplace_back("sv");
    // Quake 2's "save <name>" (F6: "save quick"); "load" is a map command
    if (!engine_->commandExists("save") && engine_->addCommand("save", [this](const CommandArgs &args) {
            if (!engine_) return;
            if (args.size() != 2) {
                engine_->print("USAGE: save <directory>\n");
                return;
            }
            engine_->print("Saving game...\n");
            std::string error;
            if (saveGame(args[1], error)) engine_->print("Done.\n");
            else engine_->print(error + "\n", REALMS_PRINT_WARNING);
        }))
        commands_.emplace_back("save");
}

void GameServer::unregisterCommands() {
    if (engine_)
        for (const auto &name : commands_) engine_->removeCommand(name);
    commands_.clear();
}

// ------------------------------------------------------------ queries

int GameServer::numEdicts() const { return ge_ ? ge_->num_edicts : 0; }

q2edict_t *GameServer::edictNum(int n) const {
    return reinterpret_cast<q2edict_t *>(reinterpret_cast<unsigned char *>(ge_->edicts) + size_t(ge_->edict_size) * size_t(n));
}

int GameServer::edictIndex(const q2edict_t *ent) const {
    if (!ent || !ge_ || !ge_->edicts) return 0;
    const auto diff = reinterpret_cast<const unsigned char *>(ent) - reinterpret_cast<const unsigned char *>(ge_->edicts);
    return int(diff / ge_->edict_size);
}

const q2edict_t *GameServer::edict(int index) const {
    if (!ge_ || index < 0 || index >= ge_->num_edicts) return nullptr;
    return edictNum(index);
}

const q2player_state_t *GameServer::playerState() const {
    const q2edict_t *p = player();
    return p && p->client ? &p->client->ps : nullptr;
}

Vec3 GameServer::playerOrigin() const {
    const q2edict_t *p = player();
    return p ? V(p->s.origin) : Vec3{};
}

Vec3 GameServer::viewOrigin() const {
    const q2player_state_t *ps = playerState();
    Vec3 o = playerOrigin();
    if (ps)
        for (int i = 0; i < 3; i++) o[size_t(i)] += ps->viewoffset[i];
    return o;
}

Vec3 GameServer::viewAngles() const {
    const q2player_state_t *ps = playerState();
    if (!ps) return {};
    return {ps->viewangles[0] + ps->kick_angles[0], ps->viewangles[1] + ps->kick_angles[1],
            ps->viewangles[2] + ps->kick_angles[2]};
}

const std::string &GameServer::configString(int index) const {
    static const std::string empty;
    if (index < 0 || size_t(index) >= configStrings_.size()) return empty;
    return configStrings_[size_t(index)];
}

std::string GameServer::modelName(int modelindex) const {
    if (modelindex <= 0 || modelindex >= Q2_MAX_MODELS) return {};
    return configString(Q2_CS_MODELS + modelindex);
}

int GameServer::inlineModel(int modelindex) const {
    const std::string name = modelName(modelindex);
    return name.size() > 1 && name[0] == '*' ? std::atoi(name.c_str() + 1) : 0;
}

double GameServer::lerpFraction() const {
    if (!running_) return 0.0;
    return std::clamp(frameAccumulator_ / kFrameSeconds, 0.0, 1.0);
}

std::vector<GameRenderEntity> GameServer::renderEntities(bool withInline) const {
    std::vector<GameRenderEntity> out;
    if (!running_) return out;
    const int clients = std::max(1, std::atoi(configString(Q2_CS_MAXCLIENTS).c_str()));
    for (int i = 1; i < numEdicts(); i++) {
        const q2edict_t *e = edict(i);
        if (!e || !e->inuse || (e->svflags & Q2_SVF_NOCLIENT)) continue;
        if (i <= clients) continue; // the local player's body (first person)
        if (e->s.modelindex <= 0 || e->s.modelindex >= Q2_MAX_MODELS) continue; // 255: player skins
        if (e->s.renderfx & (Q2_RF_BEAM | Q2_RF_VIEWERMODEL)) continue;
        GameRenderEntity r;
        r.number = i;
        r.modelindex = e->s.modelindex;
        r.model = modelName(e->s.modelindex);
        if (r.model.empty() || r.model[0] == '#') continue; // vwep names are per client
        r.inlineModel = inlineModel(e->s.modelindex);
        if (r.inlineModel > 0 && !withInline) continue;
        for (int k = 0; k < 3; k++) {
            r.origin[size_t(k)] = e->s.origin[k];
            r.oldOrigin[size_t(k)] = e->s.old_origin[k];
            r.angles[size_t(k)] = e->s.angles[k];
            r.mins[size_t(k)] = e->mins[k];
            r.maxs[size_t(k)] = e->maxs[k];
        }
        r.frame = e->s.frame;
        r.skinnum = e->s.skinnum;
        r.effects = e->s.effects;
        r.renderfx = e->s.renderfx;
        r.svflags = e->svflags;
        r.solid = int(e->solid);
        if (e->s.sound > 0 && e->s.sound < Q2_MAX_SOUNDS) {
            const std::string &snd = configString(Q2_CS_SOUNDS + e->s.sound);
            if (!snd.empty()) r.sound = "sound/" + snd;
        }
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<GameBeamEntity> GameServer::beamEntities() const {
    std::vector<GameBeamEntity> out;
    if (!running_) return out;
    for (int i = 1; i < numEdicts(); i++) {
        const q2edict_t *e = edict(i);
        if (!e || !e->inuse || (e->svflags & Q2_SVF_NOCLIENT) || !(e->s.renderfx & Q2_RF_BEAM)) continue;
        if (e->s.modelindex <= 0) continue; // SV_BuildClientFrame: not sent without a model
        GameBeamEntity b;
        b.number = i;
        for (int k = 0; k < 3; k++) {
            b.origin[size_t(k)] = e->s.origin[k];
            b.oldOrigin[size_t(k)] = e->s.old_origin[k];
        }
        b.frame = e->s.frame;
        b.skinnum = e->s.skinnum;
        out.push_back(b);
    }
    return out;
}

std::vector<GameLoopSound> GameServer::loopSounds() const {
    std::vector<GameLoopSound> out;
    if (!running_) return out;
    for (int i = 1; i < numEdicts(); i++) {
        const q2edict_t *e = edict(i);
        if (!e || !e->inuse || (e->svflags & Q2_SVF_NOCLIENT)) continue;
        if (e->s.sound <= 0 || e->s.sound >= Q2_MAX_SOUNDS) continue;
        const std::string &name = configString(Q2_CS_SOUNDS + e->s.sound);
        if (name.empty()) continue;
        GameLoopSound l;
        l.entity = i;
        l.name = "sound/" + name;
        for (int k = 0; k < 3; k++)
            l.origin[size_t(k)] = e->solid == Q2_SOLID_BSP ? e->s.origin[k] + 0.5f * (e->mins[k] + e->maxs[k])
                                                           : e->s.origin[k];
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<GameEvent> GameServer::takeEvents() {
    std::vector<GameEvent> out(std::make_move_iterator(events_.begin()), std::make_move_iterator(events_.end()));
    events_.clear();
    return out;
}

// ------------------------------------------------------------ messages

void GameServer::print(int level, const std::string &text, GameEvent::Kind kind, int entity) {
    if (engine_) engine_->print(text, kind == GameEvent::Kind::CenterPrint ? REALMS_PRINT_DEVELOPER : REALMS_PRINT_INFO);
    GameEvent ev;
    ev.kind = kind;
    ev.level = level;
    ev.entity = entity;
    ev.text = text;
    events_.push_back(std::move(ev));
    if (events_.size() > 1024) events_.pop_front();
}

void GameServer::queueSound(const float *origin, q2edict_t *ent, int channel, int soundindex, float volume,
                            float attenuation, float timeofs) {
    // SV_StartSound
    if (soundindex <= 0 || soundindex >= Q2_MAX_SOUNDS) return;
    const std::string &name = configString(Q2_CS_SOUNDS + soundindex);
    if (name.empty()) return;
    GameEvent ev;
    ev.kind = GameEvent::Kind::Sound;
    ev.level = channel & 7;
    ev.entity = edictIndex(ent);
    ev.volume = volume;
    ev.attenuation = attenuation;
    ev.delay = std::isfinite(timeofs) ? std::clamp(timeofs, 0.0f, 0.255f) : 0.0f; // sent as a byte of ms
    if (origin) {
        ev.origin = V(origin);
    } else if (ent) {
        // the origin can't be sent, so use the center of a brush model
        if (ent->solid == Q2_SOLID_BSP) {
            for (int i = 0; i < 3; i++) ev.origin[size_t(i)] = ent->s.origin[i] + 0.5f * (ent->mins[i] + ent->maxs[i]);
        } else {
            ev.origin = V(ent->s.origin);
        }
    }
    // S_RegisterSound: "*name" are the player model's sexed sounds.
    ev.text = name[0] == '*' ? "players/male/" + name.substr(1) : "sound/" + name;
    events_.push_back(std::move(ev));
    if (events_.size() > 1024) events_.pop_front();
}

namespace {

// CL_ParseTEnt's layouts, for every temp entity of the game API (base game,
// Xatrix and Rogue). False for an unknown type (the client would drop).
template <class Byte, class Short, class Pos>
bool readTempEntity(GameEvent &ev, Byte byte, Short shortv, Pos pos) {
    auto dir = [&]() {
        const int b = byte();
        return b < 162 ? gameByteDir(b) : Vec3{0, 0, 0};
    };
    switch (ev.level) {
    case GTE_BLOOD: case GTE_GUNSHOT: case GTE_SPARKS: case GTE_BULLET_SPARKS: case GTE_SCREEN_SPARKS:
    case GTE_SHIELD_SPARKS: case GTE_SHOTGUN: case GTE_BLASTER: case GTE_GREENBLOOD: case GTE_BLASTER2:
    case GTE_FLECHETTE: case GTE_HEATBEAM_SPARKS: case GTE_HEATBEAM_STEAM: case GTE_MOREBLOOD:
    case GTE_ELECTRIC_SPARKS:
        ev.origin = pos();
        ev.dir = dir();
        break;
    case GTE_SPLASH: case GTE_LASER_SPARKS: case GTE_WELDING_SPARKS: case GTE_TUNNEL_SPARKS:
        ev.count = byte();
        ev.origin = pos();
        ev.dir = dir();
        ev.color = byte();
        break;
    case GTE_BLUEHYPERBLASTER: // the "direction" is a position
        ev.origin = pos();
        ev.dir = pos();
        break;
    case GTE_RAILTRAIL: case GTE_BUBBLETRAIL: case GTE_BFG_LASER: case GTE_DEBUGTRAIL: case GTE_BUBBLETRAIL2:
        ev.origin = pos();
        ev.end = pos();
        break;
    case GTE_EXPLOSION1: case GTE_EXPLOSION2: case GTE_ROCKET_EXPLOSION: case GTE_GRENADE_EXPLOSION:
    case GTE_ROCKET_EXPLOSION_WATER: case GTE_GRENADE_EXPLOSION_WATER: case GTE_BFG_EXPLOSION:
    case GTE_BFG_BIGEXPLOSION: case GTE_BOSSTPORT: case GTE_PLASMA_EXPLOSION: case GTE_PLAIN_EXPLOSION:
    case GTE_CHAINFIST_SMOKE: case GTE_TRACKER_EXPLOSION: case GTE_TELEPORT_EFFECT: case GTE_DBALL_GOAL:
    case GTE_NUKEBLAST: case GTE_WIDOWSPLASH: case GTE_EXPLOSION1_BIG: case GTE_EXPLOSION1_NP:
        ev.origin = pos();
        break;
    case GTE_PARASITE_ATTACK: case GTE_MEDIC_CABLE_ATTACK: case GTE_HEATBEAM: case GTE_MONSTER_HEATBEAM:
        ev.entity = shortv();
        ev.origin = pos();
        ev.end = pos();
        break;
    case GTE_GRAPPLE_CABLE:
        ev.entity = shortv();
        ev.origin = pos();
        ev.end = pos();
        ev.offset = pos();
        break;
    case GTE_LIGHTNING:
        ev.entity = shortv();
        ev.entity2 = shortv();
        ev.origin = pos();
        ev.end = pos();
        break;
    case GTE_FLASHLIGHT:
        ev.origin = pos();
        ev.entity = shortv();
        break;
    case GTE_FORCEWALL:
        ev.origin = pos();
        ev.end = pos();
        ev.color = byte();
        break;
    case GTE_STEAM:
        ev.id = shortv();
        ev.count = byte();
        ev.origin = pos();
        ev.dir = dir();
        ev.color = byte();
        ev.magnitude = shortv();
        if (ev.id != -1) { // sustained: the duration in ms follows
            const int lo = shortv() & 0xffff, hi = shortv() & 0xffff;
            ev.durationMs = int32_t(uint32_t(lo) | (uint32_t(hi) << 16));
        }
        break;
    case GTE_WIDOWBEAMOUT:
        ev.id = shortv();
        ev.origin = pos();
        break;
    default:
        return false;
    }
    return true;
}

} // namespace

void GameServer::flushMessage(const float *origin, int entity) {
    std::vector<unsigned char> m;
    m.swap(msg_);
    if (m.empty()) return;
    size_t p = 1;
    auto byte = [&]() -> int { return p < m.size() ? m[p++] : 0; };
    auto shortv = [&]() -> int {
        const int lo = byte(), hi = byte();
        return int16_t(uint16_t(lo | (hi << 8)));
    };
    auto pos = [&]() -> Vec3 {
        Vec3 v;
        for (auto &c : v) c = shortv() * 0.125f;
        return v;
    };
    auto str = [&]() -> std::string {
        std::string s;
        while (p < m.size() && m[p]) s += char(m[p++]);
        if (p < m.size()) p++;
        return s;
    };
    GameEvent ev;
    ev.entity = entity;
    if (origin) ev.origin = V(origin);
    switch (m[0]) {
    case svc_muzzleflash:
    case svc_muzzleflash2:
        ev.kind = GameEvent::Kind::MuzzleFlash;
        ev.entity = shortv();
        ev.level = byte();
        if (m[0] == svc_muzzleflash2) ev.level += 1000; // monster flash (MZ2_*)
        if (const q2edict_t *e = edict(ev.entity)) {
            ev.origin = V(e->s.origin);
            ev.angles = V(e->s.angles);
        }
        break;
    case svc_temp_entity:
        ev.kind = GameEvent::Kind::TempEntity;
        ev.level = byte();
        if (!readTempEntity(ev, byte, shortv, pos)) return;
        break;
    case svc_layout:
        ev.kind = GameEvent::Kind::Layout;
        ev.text = str();
        break;
    case svc_stufftext:
        ev.kind = GameEvent::Kind::StuffText;
        ev.text = str();
        break;
    case svc_inventory: // CL_ParseInventory
        for (int16_t &count : inventory_) count = int16_t(shortv());
        return;
    default: return;
    }
    events_.push_back(std::move(ev));
    if (events_.size() > 1024) events_.pop_front();
}

// ------------------------------------------------------------ config strings

void GameServer::setConfigString(int index, const std::string &value) {
    if (configStrings_.size() != size_t(Q2_MAX_CONFIGSTRINGS)) configStrings_.assign(Q2_MAX_CONFIGSTRINGS, std::string());
    configStrings_[size_t(index)] = value;
}

// SV_FindIndex
int GameServer::findIndex(const char *name, int start, int max, bool create) {
    if (!name || !name[0]) return 0;
    int i;
    for (i = 1; i < max && !configString(start + i).empty(); i++)
        if (configString(start + i) == name) return i;
    if (!create) return 0;
    if (i == max) {
        gameError(std::string("*Index: overflow for ") + name);
        return 0;
    }
    setConfigString(start + i, std::string(name).substr(0, Q2_MAX_QPATH - 1));
    return i;
}

// ------------------------------------------------------------ cvars

q2cvar_t *GameServer::cvarGet(const char *name, const char *value, int flags) {
    if (!name || !engine_) return nullptr;
    for (auto &m : cvars_)
        if (m->name == name) {
            if (flags) engine_->registerCvar(name, value ? value : "", flags);
            m->cv.flags |= flags;
            return &m->cv;
        }
    engine_->registerCvar(name, value ? value : "", flags);
    auto m = std::make_unique<CvarMirror>();
    m->name = name;
    m->value = engine_->cvarString(name, value ? value : "");
    m->cv.name = m->name.data();
    m->cv.string = m->value.data();
    m->cv.latched_string = nullptr;
    m->cv.flags = flags | engine_->cvarFlags(name);
    m->cv.modified = 1;
    m->cv.value = float(std::atof(m->value.c_str()));
    if (!cvars_.empty()) cvars_.back()->cv.next = &m->cv;
    cvars_.push_back(std::move(m));
    return &cvars_.back()->cv;
}

q2cvar_t *GameServer::cvarSet(const char *name, const char *value, bool force) {
    if (!name || !engine_) return nullptr;
    const std::string v = value ? value : "";
    const int32_t r = force ? engine_->forceSetCvar(name, v) : engine_->setCvar(name, v);
    if (r == REALMS_CVAR_SET_UNKNOWN) engine_->registerCvar(name, v, 0);
    q2cvar_t *cv = cvarGet(name, v.c_str(), 0);
    syncCvars();
    return cv;
}

void GameServer::syncCvars() {
    if (!engine_) return;
    for (auto &m : cvars_) {
        std::string now = engine_->cvarString(m->name, m->value);
        if (now != m->value) {
            m->value = std::move(now);
            m->cv.string = m->value.data();
            m->cv.value = float(std::atof(m->value.c_str()));
            m->cv.modified = 1;
        }
    }
}

// ------------------------------------------------------------ tag memory

void *GameServer::tagMalloc(int size, int tag) {
    if (size < 0) {
        gameError("Z_Malloc: bad size");
        return nullptr;
    }
    const size_t bytes = offsetof(TagBlock, data) + size_t(size) + 1;
    auto *b = static_cast<TagBlock *>(std::calloc(1, bytes));
    if (!b) {
        gameError("Z_Malloc: failed on allocation of " + std::to_string(size) + " bytes");
        return nullptr;
    }
    b->tag = tag;
    b->size = size;
    b->next = tags_;
    if (tags_) tags_->prev = b;
    tags_ = b;
    return b->data;
}

void GameServer::tagFree(void *block) {
    if (!block) return;
    auto *b = reinterpret_cast<TagBlock *>(static_cast<unsigned char *>(block) - offsetof(TagBlock, data));
    if (b->prev) b->prev->next = b->next;
    else tags_ = b->next;
    if (b->next) b->next->prev = b->prev;
    std::free(b);
}

void GameServer::freeTags(int tag) {
    for (TagBlock *b = tags_; b;) {
        TagBlock *next = b->next;
        if (tag < 0 || b->tag == tag) tagFree(b->data);
        b = next;
    }
}

// ------------------------------------------------------------ world (sv_world.c)

namespace {
inline void clearLink(q2link_t *l) { l->prev = l->next = l; }
inline void removeLink(q2link_t *l) {
    l->next->prev = l->prev;
    l->prev->next = l->next;
}
inline void insertLinkBefore(q2link_t *l, q2link_t *before) {
    l->next = before;
    l->prev = before->prev;
    l->prev->next = l;
    l->next->prev = l;
}
inline q2edict_t *edictFromArea(q2link_t *l) {
    return reinterpret_cast<q2edict_t *>(reinterpret_cast<unsigned char *>(l) - offsetof(q2edict_t, area));
}
} // namespace

GameServer::AreaNode *GameServer::createAreaNode(int depth, const Vec3 &mins, const Vec3 &maxs) {
    AreaNode *anode = &areaNodes_[size_t(numAreaNodes_++)];
    clearLink(&anode->triggerEdicts);
    clearLink(&anode->solidEdicts);
    if (depth == kAreaDepth) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = nullptr;
        return anode;
    }
    const Vec3 size{maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]};
    anode->axis = size[0] > size[1] ? 0 : 1;
    anode->dist = 0.5f * (maxs[size_t(anode->axis)] + mins[size_t(anode->axis)]);
    Vec3 mins1 = mins, mins2 = mins, maxs1 = maxs, maxs2 = maxs;
    maxs1[size_t(anode->axis)] = mins2[size_t(anode->axis)] = anode->dist;
    anode->children[0] = createAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = createAreaNode(depth + 1, mins1, maxs1);
    return anode;
}

void GameServer::clearWorld() {
    areaNodes_.assign(kAreaNodes, AreaNode{});
    numAreaNodes_ = 0;
    MapModel world;
    if (!engine_->mapModel(0, world)) {
        RealmsMapInfo info{};
        info.struct_size = sizeof(info);
        engine_->mapInfo(info);
        world.mins = {info.mins[0], info.mins[1], info.mins[2]};
        world.maxs = {info.maxs[0], info.maxs[1], info.maxs[2]};
    }
    createAreaNode(0, world.mins, world.maxs);
}

void GameServer::unlinkEdict(q2edict_t *ent) {
    if (!ent || !ent->area.prev) return; // not linked in anywhere
    removeLink(&ent->area);
    ent->area.prev = ent->area.next = nullptr;
}

void GameServer::linkEdict(q2edict_t *ent) {
    if (!ent || !ge_ || areaNodes_.empty()) return;
    if (ent->area.prev) unlinkEdict(ent); // unlink from old position
    if (ent == ge_->edicts) return;        // don't add the world
    if (!ent->inuse) return;

    // set the size
    for (int i = 0; i < 3; i++) ent->size[i] = ent->maxs[i] - ent->mins[i];

    // encode the size into the entity_state for client prediction
    if (ent->solid == Q2_SOLID_BBOX && !(ent->svflags & Q2_SVF_DEADMONSTER)) {
        // assume that x/y are equal and symetric
        int i = int(ent->maxs[0] / 8);
        if (i < 1) i = 1;
        if (i > 31) i = 31;
        // z is not symetric
        int j = int((-ent->mins[2]) / 8);
        if (j < 1) j = 1;
        if (j > 31) j = 31;
        // and z maxs can be negative...
        int k = int((ent->maxs[2] + 32) / 8);
        if (k < 1) k = 1;
        if (k > 63) k = 63;
        ent->s.solid = (k << 10) | (j << 5) | i;
    } else if (ent->solid == Q2_SOLID_BSP) {
        ent->s.solid = 31; // a solid_bbox will never create this value
    } else {
        ent->s.solid = 0;
    }

    // set the abs box
    if (ent->solid == Q2_SOLID_BSP && (ent->s.angles[0] || ent->s.angles[1] || ent->s.angles[2])) {
        // expand for rotation
        float max = 0;
        for (int i = 0; i < 3; i++) {
            float v = std::fabs(ent->mins[i]);
            if (v > max) max = v;
            v = std::fabs(ent->maxs[i]);
            if (v > max) max = v;
        }
        for (int i = 0; i < 3; i++) {
            ent->absmin[i] = ent->s.origin[i] - max;
            ent->absmax[i] = ent->s.origin[i] + max;
        }
    } else { // normal
        for (int i = 0; i < 3; i++) {
            ent->absmin[i] = ent->s.origin[i] + ent->mins[i];
            ent->absmax[i] = ent->s.origin[i] + ent->maxs[i];
        }
    }
    // because movement is clipped an epsilon away from an actual edge,
    // we must fully check even when bounding boxes don't quite touch
    for (int i = 0; i < 3; i++) {
        ent->absmin[i] -= 1;
        ent->absmax[i] += 1;
    }

    // link to PVS leafs
    ent->num_clusters = 0;
    ent->areanum = 0;
    ent->areanum2 = 0;

    // get all leafs, including solids
    int32_t topnode = 0;
    const std::vector<int32_t> leafs =
        engine_->boxLeafs(V(ent->absmin), V(ent->absmax), 0, &topnode, size_t(kMaxTotalEntLeafs));
    const int num_leafs = int(leafs.size());
    int clusters[kMaxTotalEntLeafs];
    // set areas
    for (int i = 0; i < num_leafs && i < kMaxTotalEntLeafs; i++) {
        int32_t contents = 0, cluster = -1, area = 0;
        engine_->leafInfo(leafs[size_t(i)], contents, cluster, area);
        clusters[i] = cluster;
        if (area) {
            // doors may legally straggle two areas,
            // but nothing should evern need more than that
            if (ent->areanum && ent->areanum != area) ent->areanum2 = area;
            else ent->areanum = area;
        }
    }
    if (num_leafs >= kMaxTotalEntLeafs) {
        // assume we missed some leafs, and mark by headnode
        ent->num_clusters = -1;
        ent->headnode = topnode;
    } else {
        ent->num_clusters = 0;
        for (int i = 0; i < num_leafs; i++) {
            if (clusters[i] == -1) continue; // not a visible leaf
            int j;
            for (j = 0; j < i; j++)
                if (clusters[j] == clusters[i]) break;
            if (j == i) {
                if (ent->num_clusters == Q2_MAX_ENT_CLUSTERS) {
                    // assume we missed some leafs, and mark by headnode
                    ent->num_clusters = -1;
                    ent->headnode = topnode;
                    break;
                }
                ent->clusternums[ent->num_clusters++] = clusters[i];
            }
        }
    }

    // if first time, make sure old_origin is valid
    if (!ent->linkcount)
        for (int i = 0; i < 3; i++) ent->s.old_origin[i] = ent->s.origin[i];
    ent->linkcount++;

    if (ent->solid == Q2_SOLID_NOT) return;

    // find the first node that the ent's box crosses
    AreaNode *node = &areaNodes_[0];
    while (true) {
        if (node->axis == -1) break;
        if (ent->absmin[node->axis] > node->dist) node = node->children[0];
        else if (ent->absmax[node->axis] < node->dist) node = node->children[1];
        else break; // crosses the node
    }
    // link it in
    if (ent->solid == Q2_SOLID_TRIGGER) insertLinkBefore(&ent->area, &node->triggerEdicts);
    else insertLinkBefore(&ent->area, &node->solidEdicts);
}

void GameServer::areaEdictsR(AreaNode *node) {
    // touch linked edicts
    q2link_t *start = areaType_ == Q2_AREA_SOLID ? &node->solidEdicts : &node->triggerEdicts;
    for (q2link_t *l = start->next, *next; l != start; l = next) {
        next = l->next;
        q2edict_t *check = edictFromArea(l);
        if (check->solid == Q2_SOLID_NOT) continue; // deactivated
        if (check->absmin[0] > areaMaxs_[0] || check->absmin[1] > areaMaxs_[1] || check->absmin[2] > areaMaxs_[2] ||
            check->absmax[0] < areaMins_[0] || check->absmax[1] < areaMins_[1] || check->absmax[2] < areaMins_[2])
            continue; // not touching
        if (areaCount_ == areaMaxCount_) {
            if (engine_) engine_->print("SV_AreaEdicts: MAXCOUNT\n", REALMS_PRINT_WARNING);
            return;
        }
        areaList_[areaCount_++] = check;
    }
    if (node->axis == -1) return; // terminal node
    // recurse down both sides
    if (areaMaxs_[node->axis] > node->dist) areaEdictsR(node->children[0]);
    if (areaMins_[node->axis] < node->dist) areaEdictsR(node->children[1]);
}

int GameServer::areaEdicts(const float *mins, const float *maxs, q2edict_t **list, int maxcount, int areatype) {
    if (areaNodes_.empty() || !list || maxcount <= 0) return 0;
    areaMins_ = mins;
    areaMaxs_ = maxs;
    areaList_ = list;
    areaCount_ = 0;
    areaMaxCount_ = maxcount;
    areaType_ = areatype;
    areaEdictsR(&areaNodes_[0]);
    return areaCount_;
}

// SV_HullForEntity: a headnode for clipping against the entity.
int GameServer::hullForEntity(const q2edict_t *ent) {
    // decide which clipping hull to use, based on the size
    if (ent->solid == Q2_SOLID_BSP) {
        // explicit hulls in the BSP model
        const int idx = ent->s.modelindex;
        if (idx <= 0 || idx >= Q2_MAX_MODELS || modelHeadnodes_[size_t(idx)] < 0) {
            gameError("MOVETYPE_PUSH with a non bsp model");
            return 0;
        }
        return modelHeadnodes_[size_t(idx)];
    }
    // create a temp hull from bounding box sizes
    return engine_->headnodeForBox(V(ent->mins), V(ent->maxs));
}

int GameServer::pointContents(const float *p) {
    if (!engine_) return 0;
    // get base contents from world
    int contents = engine_->pointContents(V(p), 0);
    // or in contents from all the other entities
    q2edict_t *touch[Q2_MAX_EDICTS];
    const int num = areaEdicts(p, p, touch, Q2_MAX_EDICTS, Q2_AREA_SOLID);
    for (int i = 0; i < num; i++) {
        q2edict_t *hit = touch[i];
        // might intersect, so do an exact clip
        const int headnode = hullForEntity(hit);
        // (Quake 2 passes the entity angles here even for boxes)
        contents |= engine_->transformedPointContents(V(p), headnode, V(hit->s.origin), V(hit->s.angles));
    }
    return contents;
}

namespace {
struct MoveClip {
    float boxmins[3], boxmaxs[3]; // enclose the test object along entire move
    const float *mins, *maxs;      // size of the moving object
    float mins2[3], maxs2[3];      // size when clipping against mosnters
    const float *start, *end;
    q2trace_t trace;
    q2edict_t *passedict;
    int contentmask;
};
} // namespace

q2trace_t GameServer::trace(const float *start, const float *mins, const float *maxs, const float *end,
                            q2edict_t *passedict, int contentmask) {
    static const float origin[3] = {0, 0, 0};
    if (!mins) mins = origin;
    if (!maxs) maxs = origin;

    auto convert = [this](const TraceResult &tr) {
        q2trace_t t{};
        t.allsolid = tr.allsolid;
        t.startsolid = tr.startsolid;
        t.fraction = tr.fraction;
        for (int i = 0; i < 3; i++) {
            t.endpos[i] = tr.endpos[size_t(i)];
            t.plane.normal[i] = tr.normal[size_t(i)];
            if (tr.normal[size_t(i)] < 0) t.plane.signbits |= uint8_t(1 << i);
        }
        t.plane.dist = tr.planeDist;
        // PlaneTypeForNormal
        const float *n = t.plane.normal;
        if (n[0] == 1.0f) t.plane.type = 0;
        else if (n[1] == 1.0f) t.plane.type = 1;
        else if (n[2] == 1.0f) t.plane.type = 2;
        else {
            const float ax = std::fabs(n[0]), ay = std::fabs(n[1]), az = std::fabs(n[2]);
            t.plane.type = ax >= ay && ax >= az ? 3 : ay >= ax && ay >= az ? 4 : 5;
        }
        t.surface = surface(tr.surface, tr.surfaceFlags, tr.surfaceValue);
        t.contents = tr.contents;
        return t;
    };

    MoveClip clip{};
    // clip to world
    clip.trace = convert(engine_->boxTrace(V(start), V(end), V(mins), V(maxs), 0, contentmask));
    clip.trace.ent = ge_ ? ge_->edicts : nullptr;
    if (clip.trace.fraction == 0) return clip.trace; // blocked by the world

    clip.contentmask = contentmask;
    clip.start = start;
    clip.end = end;
    clip.mins = mins;
    clip.maxs = maxs;
    clip.passedict = passedict;
    for (int i = 0; i < 3; i++) {
        clip.mins2[i] = mins[i];
        clip.maxs2[i] = maxs[i];
    }
    // create the bounding box of the entire move (SV_TraceBounds)
    for (int i = 0; i < 3; i++) {
        if (end[i] > start[i]) {
            clip.boxmins[i] = start[i] + clip.mins2[i] - 1;
            clip.boxmaxs[i] = end[i] + clip.maxs2[i] + 1;
        } else {
            clip.boxmins[i] = end[i] + clip.mins2[i] - 1;
            clip.boxmaxs[i] = start[i] + clip.maxs2[i] + 1;
        }
    }

    // clip to other solid entities (SV_ClipMoveToEntities)
    q2edict_t *touchlist[Q2_MAX_EDICTS];
    const int num = areaEdicts(clip.boxmins, clip.boxmaxs, touchlist, Q2_MAX_EDICTS, Q2_AREA_SOLID);
    // be careful, it is possible to have an entity in this
    // list removed before we get to it (killtriggered)
    for (int i = 0; i < num; i++) {
        q2edict_t *touch = touchlist[i];
        if (touch->solid == Q2_SOLID_NOT) continue;
        if (touch == clip.passedict) continue;
        if (clip.trace.allsolid) return clip.trace;
        if (clip.passedict) {
            if (touch->owner == clip.passedict) continue; // don't clip against own missiles
            if (clip.passedict->owner == touch) continue; // don't clip against owner
        }
        if (!(clip.contentmask & REALMS_CONTENTS_DEADMONSTER) && (touch->svflags & Q2_SVF_DEADMONSTER)) continue;
        // might intersect, so do an exact clip
        const int headnode = hullForEntity(touch);
        Vec3 angles = V(touch->s.angles);
        if (touch->solid != Q2_SOLID_BSP) angles = {0, 0, 0}; // boxes don't rotate
        q2trace_t tr;
        if (touch->svflags & Q2_SVF_MONSTER)
            tr = convert(engine_->transformedBoxTrace(V(clip.start), V(clip.end), V(clip.mins2), V(clip.maxs2), headnode,
                                                      clip.contentmask, V(touch->s.origin), angles));
        else
            tr = convert(engine_->transformedBoxTrace(V(clip.start), V(clip.end), V(clip.mins), V(clip.maxs), headnode,
                                                      clip.contentmask, V(touch->s.origin), angles));
        if (tr.allsolid || tr.startsolid || tr.fraction < clip.trace.fraction) {
            tr.ent = touch;
            if (clip.trace.startsolid) {
                clip.trace = tr;
                clip.trace.startsolid = 1;
            } else {
                clip.trace = tr;
            }
        } else if (tr.startsolid) {
            clip.trace.startsolid = 1;
        }
    }
    return clip.trace;
}

q2csurface_t *GameServer::surface(const std::string &name, int32_t flags, int32_t value) {
    for (auto &s : surfaces_)
        if (s->flags == flags && s->value == value && std::strncmp(s->name, name.c_str(), sizeof(s->name) - 1) == 0)
            return s.get();
    auto s = std::make_unique<q2csurface_t>();
    std::memset(s.get(), 0, sizeof(q2csurface_t));
    std::strncpy(s->name, name.c_str(), sizeof(s->name) - 1);
    s->flags = flags;
    s->value = value;
    surfaces_.push_back(std::move(s));
    return surfaces_.back().get();
}

// PF_inPVS / PF_inPHS. Leafs without a cluster (mesh maps, or outside the
// map) don't take part in the visibility test; areas still do.
bool GameServer::inPVS(const float *p1, const float *p2, bool phs) {
    if (!engine_) return false;
    int32_t contents = 0, cluster1 = -1, area1 = 0, cluster2 = -1, area2 = 0;
    engine_->leafInfo(engine_->pointLeaf(V(p1)), contents, cluster1, area1);
    engine_->leafInfo(engine_->pointLeaf(V(p2)), contents, cluster2, area2);
    if (cluster1 >= 0 && cluster2 >= 0) {
        const std::vector<uint8_t> mask = phs ? engine_->clusterPHS(cluster1) : engine_->clusterPVS(cluster1);
        if (!mask.empty() && !Engine::clusterVisible(mask, cluster2)) return false;
    }
    if (!engine_->areasConnected(area1, area2)) return false; // a door blocks sight
    return true;
}

} // namespace realms
