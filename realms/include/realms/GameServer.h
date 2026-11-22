// Realms Engine - the Quake 2 server for the game module.
//
// GameServer is the server half of Quake 2 (sv_game.c, sv_world.c and the
// local-client parts of sv_init.c / sv_user.c) on top of realms::Engine:
//
//   * it loads the game module (game/, the Free Pascal port of gamex86) and
//     calls GetGameAPI with a game_import_t whose collision functions are
//     Engine traces (so it works the same on the Pascal and the C++ core);
//   * world: area nodes, SV_LinkEdict (PVS clusters, areas, abs boxes),
//     SV_AreaEdicts, SV_Trace / SV_PointContents against brush models and
//     entity boxes, inPVS / inPHS, area portals;
//   * config strings and the model / sound / image index tables, cvars
//     mirrored into Quake 2 cvar_t records, tag memory, the argv tokenizer;
//   * one local client: ClientConnect / ClientBegin, a usercmd per sandbox
//     frame (ClientThink, i.e. Pmove), RunFrame at 10 Hz.
//
// Sounds, prints and temp entities the game sends are queued for the
// sandbox (takeEvents). Only one GameServer can run at a time: the game
// module and game_import_t are process-global, as in Quake 2.
#pragma once

#include "realms/Engine.h"
#include "realms/q2game.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace realms {

struct GameUserCmd {
    float forward = 0.0f;  // units / second, + forward (cl_forwardspeed 200, run 400)
    float side = 0.0f;     // + right
    float up = 0.0f;       // + jump / swim up, - crouch
    float pitch = 0.0f;    // absolute view angles, Quake 2 conventions (degrees, + looks down)
    float yaw = 0.0f;      // 0 = +X, 90 = +Y (delta_angles are taken out by the server)
    bool attack = false;
    bool use = false;
    int impulse = 0;
};

struct GameEvent {
    enum class Kind { Print, CenterPrint, Sound, TempEntity, MuzzleFlash, Layout, StuffText };
    Kind kind = Kind::Print;
    int level = 0;       // Print: PRINT_*; Sound: channel; TempEntity: TE_*; MuzzleFlash: MZ_*
    int entity = 0;      // edict number (Sound / MuzzleFlash / CenterPrint target), 0 = world
    Vec3 origin{};       // Sound / TempEntity position
    float volume = 1.0f; // Sound
    float attenuation = 1.0f;
    std::string text;    // Print / CenterPrint / Layout / StuffText text, Sound file ("sound/" + name)
};

// An entity the client would get in its frame (SV_BuildClientFrame, minus
// the PVS cull): it has a model, is not SVF_NOCLIENT and is not the local
// player's own body. `model` is the config string ("*3",
// "models/items/armor/shard/tris.md2"); `inlineModel` > 0 for brush models.
struct GameRenderEntity {
    int number = 0;
    int modelindex = 0;
    std::string model;
    int inlineModel = 0;
    std::array<float, 3> origin{}, oldOrigin{}, angles{};
    std::array<float, 3> mins{}, maxs{};
    int frame = 0;
    int skinnum = 0;
    uint32_t effects = 0;
    int renderfx = 0;
    int svflags = 0;
    int solid = 0; // q2solid_t
};

struct GameServerConfig {
    std::string executableDir;    // where to look for librealms_game
    std::string gameLibrary;      // explicit path (else REALMS_GAME_LIBRARY, then the search)
    std::string playerName = "Player";
    int skill = 1;                // 0 easy .. 3 nightmare
    std::string spawnPoint;       // targetname of an info_player_start ("" = the default one)
};

class GameServer {
public:
    // The server ticks at Quake 2's 10 Hz.
    static constexpr double kFrameSeconds = 0.1;

    GameServer();
    ~GameServer();
    GameServer(const GameServer &) = delete;
    GameServer &operator=(const GameServer &) = delete;

    // realms-pascal links the game module in and registers GetGameAPI here.
    static void setLinkedGame(Q2GetGameApiFn getGameApi);
    // True when a game module is linked or librealms_game can be found
    // (it is opened on the first call).
    static bool moduleAvailable(const std::string &executableDir, std::string *where = nullptr,
                                std::string *error = nullptr);

    // Starts the game on the map the engine has loaded: GetGameAPI + Init
    // (first time), SpawnEntities with the map's entity string, two settle
    // frames, then the local client connects and begins.
    bool start(Engine &engine, const GameServerConfig &config, std::string &error);
    // ShutdownGame and free every tag.
    void stop();
    bool running() const { return running_; }
    const std::string &moduleOrigin() const { return moduleOrigin_; }
    const std::string &mapName() const { return mapName_; }
    const std::string &lastError() const { return lastError_; }

    // One sandbox frame: a usercmd for dt seconds (split into <= 250 ms
    // commands, leftover milliseconds carried), then RunFrame for every
    // 100 ms that passed. Returns the number of game frames run.
    int frame(const GameUserCmd &cmd, double dt);
    // Only the game frames (no player input), for tests and dedicated use.
    int runFrames(int count);

    // A client command ("give all", "use shotgun", "god", ...) or a server
    // command ("sv addip 1.2.3.4" without the "sv").
    void clientCommand(const std::string &line);
    void serverCommand(const std::string &line);

    // ---- what the sandbox draws / plays
    int numEdicts() const;
    const q2edict_t *edict(int index) const;
    const q2edict_t *player() const { return edict(1); }
    const q2player_state_t *playerState() const;
    Vec3 playerOrigin() const;
    Vec3 viewOrigin() const;  // origin + viewoffset
    Vec3 viewAngles() const;  // ps.viewangles + kick_angles (Quake 2 conventions)
    int frameNumber() const { return frameNum_; }
    double time() const { return frameNum_ * kFrameSeconds; }
    // How far the sandbox is into the next 100 ms game frame, 0..1 (the
    // client's lerpfrac: draw entities between the last two frames).
    double lerpFraction() const;
    // Entities to draw, in edict order. `withInline` keeps brush models.
    std::vector<GameRenderEntity> renderEntities(bool withInline = false) const;
    const std::string &configString(int index) const;
    // Model name of an edict ("*3", "models/items/armor/body/tris.md2"), "" if none.
    std::string modelName(int modelindex) const;
    // Inline model number for a "*n" modelindex, 0 otherwise.
    int inlineModel(int modelindex) const;
    std::vector<GameEvent> takeEvents();

    // ---- game_import_t, used by the static thunks (public for the tests)
    q2trace_t trace(const float *start, const float *mins, const float *maxs, const float *end, q2edict_t *passent,
                    int contentmask);
    int pointContents(const float *point);
    void linkEdict(q2edict_t *ent);
    void unlinkEdict(q2edict_t *ent);
    int areaEdicts(const float *mins, const float *maxs, q2edict_t **list, int maxcount, int areatype);
    bool inPVS(const float *p1, const float *p2, bool phs);

private:
    struct AreaNode {
        int axis = -1; // -1 = leaf node
        float dist = 0.0f;
        AreaNode *children[2] = {nullptr, nullptr};
        q2link_t triggerEdicts{};
        q2link_t solidEdicts{};
    };
    struct CvarMirror;
    struct TagBlock;

    friend struct GameImports;

    bool loadModule(std::string &error);
    bool guarded(const char *what, void (*fn)(GameServer &, void *), void *arg);
    void clearWorld();
    AreaNode *createAreaNode(int depth, const Vec3 &mins, const Vec3 &maxs);
    void areaEdictsR(AreaNode *node);
    int hullForEntity(const q2edict_t *ent);
    q2edict_t *edictNum(int n) const;
    int edictIndex(const q2edict_t *ent) const;
    int findIndex(const char *name, int start, int max, bool create);
    void setConfigString(int index, const std::string &value);
    q2csurface_t *surface(const std::string &name, int32_t flags, int32_t value);
    q2cvar_t *cvarGet(const char *name, const char *value, int flags);
    q2cvar_t *cvarSet(const char *name, const char *value, bool force);
    void syncCvars();
    void tokenize(const std::string &line);
    void flushMessage(const float *origin, int entity);
    void queueSound(const float *origin, q2edict_t *ent, int channel, int soundindex, float volume, float attenuation);
    void print(int level, const std::string &text, GameEvent::Kind kind, int entity);
    void gameError(const std::string &text);
    void prepWorldFrame();
    void sendUserCmd(int msec, const GameUserCmd &cmd);
    void *tagMalloc(int size, int tag);
    void tagFree(void *block);
    void freeTags(int tag);
    void registerCommands();
    void unregisterCommands();

    Engine *engine_ = nullptr;
    GameServerConfig config_;
    q2game_export_t *ge_ = nullptr;
    bool gameInitialised_ = false;
    bool running_ = false;
    std::string moduleOrigin_;
    std::string mapName_;
    std::string lastError_;

    std::vector<std::string> configStrings_;
    std::vector<int32_t> modelHeadnodes_; // by modelindex: 0 world, -1 not a brush model
    std::vector<AreaNode> areaNodes_;
    int numAreaNodes_ = 0;

    // SV_AreaEdicts state
    const float *areaMins_ = nullptr;
    const float *areaMaxs_ = nullptr;
    q2edict_t **areaList_ = nullptr;
    int areaCount_ = 0, areaMaxCount_ = 0, areaType_ = 0;

    std::deque<std::unique_ptr<CvarMirror>> cvars_;
    std::deque<std::unique_ptr<q2csurface_t>> surfaces_;
    std::vector<std::string> argv_;
    std::string args_;
    std::vector<unsigned char> msg_;
    std::deque<GameEvent> events_;
    std::vector<std::string> commands_;
    q2usercmd_t lastCmd_{};
    TagBlock *tags_ = nullptr;

    double frameAccumulator_ = 0.0;
    double msecAccumulator_ = 0.0;
    int frameNum_ = 0;
    char userinfo_[512] = {};
    bool inGameCall_ = false;
    void *abortJump_ = nullptr;
};

} // namespace realms
