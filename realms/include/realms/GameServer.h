// Server half of Quake 2 (sv_game.c, sv_world.c, parts of sv_init.c and
// sv_user.c) on realms::Engine. Loads the game module and calls GetGameAPI
// with a game_import_t whose traces go to the Engine, so it works on both
// cores. RunFrame at 10 Hz.
//
// Clients (svs.clients): client 0 is the local player in edict 1, connected
// by start(). In deathmatch or coop (GameServerConfig) more clients can
// connect, client n in edict n + 1; each has its own usercmds, player
// state, events, inventory and snapshots. Without a client index the
// calls below are for client 0.
//
// Sounds, prints and temp entities from the game are queued per client
// (takeEvents): prints and layouts for one client go to that client only,
// everything else to every client.
// Only one GameServer can run at a time; the game module is process-global.
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
    enum class Kind { Print, CenterPrint, Sound, TempEntity, MuzzleFlash, Layout, StuffText, EntityEvent };
    Kind kind = Kind::Print;
    // Print: PRINT_*; Sound: channel (CHAN_*); TempEntity: TE_*;
    // MuzzleFlash: MZ_* (with MZ_SILENCED), 1000 + MZ2_* for monsters;
    // EntityEvent: EV_*.
    int level = 0;
    int entity = 0;      // edict number (Sound / MuzzleFlash / CenterPrint target, EntityEvent), 0 = world
    Vec3 origin{};       // Sound / TempEntity position, MuzzleFlash / EntityEvent entity origin
    float volume = 1.0f; // Sound
    float attenuation = 1.0f;
    float delay = 0.0f;  // Sound: seconds after the event (timeofs)
    std::string text;    // Print / CenterPrint / Layout / StuffText text, Sound file ("sound/" + name)
    // Sound from an entity event (EV_FOOTSTEP...), 0 otherwise.
    int entityEvent = 0;
    // MuzzleFlash / EntityEvent: the entity's angles (degrees).
    Vec3 angles{};
    // TempEntity, as CL_ParseTEnt reads it (unused fields stay 0):
    Vec3 end{};          // second position: trails, beams, lasers, force walls
    Vec3 dir{};          // impact direction (a byte direction, unit length)
    Vec3 offset{};       // TE_GRAPPLE_CABLE beam offset
    int count = 0;       // particle count (TE_SPLASH, *_SPARKS, TE_STEAM)
    int color = 0;       // palette index or splash type (TE_SPLASH: SPLASH_*)
    int entity2 = 0;     // TE_LIGHTNING destination entity
    int magnitude = 0;   // TE_STEAM
    int id = 0;          // TE_STEAM / TE_WIDOWBEAMOUT sustain id
    int durationMs = 0;  // sustained TE_STEAM
};

// An entity the client would get in its frame (SV_BuildClientFrame, minus
// the PVS cull): it has a model, is not SVF_NOCLIENT and is not the viewing
// player's own body. Other players (modelindex 255) get their
// "players/<model>/tris.md2". `model` is the config string ("*3",
// "models/items/armor/shard/tris.md2", loaded as
// the .md3 at that path); `inlineModel` > 0 for brush models.
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
    std::string sound; // looping sound (s.sound), "" if none
};

// An RF_BEAM entity (target_laser): a beam from `origin` to `oldOrigin`,
// `frame` units wide, `skinnum` packs four palette colours (one is picked
// at random each frame, like cl_ents.c).
struct GameBeamEntity {
    int number = 0;
    std::array<float, 3> origin{}, oldOrigin{};
    int frame = 0;
    int skinnum = 0;
};

// A looping entity sound (s.sound: machinery, target_speaker loops, the
// rocket's whine), at the entity (brush models: their centre).
struct GameLoopSound {
    int entity = 0;
    std::string name; // "sound/" + config string
    std::array<float, 3> origin{};
};

// What the client gets from the server in one sandbox frame: Quake 2's
// svc_frame (player state, packet entities) plus the reliable messages
// since the last one (config strings, inventory, sounds, prints, temp
// entities...). The client side (realms/GameClient.h) draws and plays only
// from these, so a recorded stream of them replays a game (demos) and can
// come over the network.
struct GameSnapshot {
    bool running = false;    // false: the game stopped (the client resets)
    int frameNumber = 0;     // the server frame (10 Hz)
    double lerpFraction = 0; // how far into the next server frame, 0..1
    bool hasPlayer = false;
    q2player_state_t player{};
    Vec3 playerOrigin{}; // moved by every usercmd, not only on server frames
    // Set on a new server frame (and in a full snapshot): the entity lists
    // below are that frame's. Otherwise they are empty and the client keeps
    // the last ones.
    bool entitiesChanged = false;
    std::vector<GameRenderEntity> entities; // renderEntities(true)
    std::vector<GameBeamEntity> beams;
    std::vector<GameLoopSound> loopSounds;
    // Config strings that changed since the last snapshot (every non-empty
    // one in a full snapshot), in index order.
    std::vector<std::pair<int, std::string>> configStrings;
    bool inventoryChanged = false;
    std::array<int16_t, Q2_MAX_ITEMS> inventory{};
    std::vector<GameEvent> events; // takeEvents()
};

struct GameServerConfig {
    std::string executableDir;    // where to look for librealms_game
    std::string gameLibrary;      // explicit path (else REALMS_GAME_LIBRARY, then the search)
    std::string playerName = "Player";
    float fov = 90.0f;            // the userinfo fov (player_state_t::fov), clamped by realms::clampFov
    int hand = 0;                 // the userinfo hand: 0 right, 1 left, 2 centre (GameServer::clampHand)
    int skill = 1;                // 0 easy .. 3 nightmare
    std::string spawnPoint;       // targetname of an info_player_start ("" = the default one)
    // SV_InitGame: deathmatch makes maxclients 1 into 8; coop makes 1 or
    // more than 4 into 4; neither is single player (maxclients 1). At most
    // Q2_MAX_CLIENTS. Deathmatch wins over coop.
    bool deathmatch = false;
    bool coop = false;
    int maxClients = 1;
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
    // Quake 2's "save <name>" (SV_Savegame_f; GameSave.h): the level, the
    // game and the server state to <writable root>/save/<name>/. Refused
    // without a running game, while the player is dead, or for a bad name.
    // "load <name>" makes the next start() read it back.
    bool saveGame(const std::string &name, std::string &error);
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

    // ---- clients
    // SV_InitGame's maxclients for GameServerConfig's three settings.
    static int maxClientsFor(bool deathmatch, bool coop, int requested);
    // The game's client commands (g_cmds.c ClientCommand) the console
    // forwards to it, as Quake 2 forwards commands it doesn't know: "give",
    // "say", "kill"... While the game runs they are console commands.
    static const std::vector<std::string> &forwardedCommands();
    // A console command's arguments back into one command line, quoting
    // the ones with spaces or semicolons.
    static std::string commandLine(const std::vector<std::string> &args, size_t first = 0);
    int maxClients() const { return int(clients_.size()); }
    bool clientActive(int client) const;
    int clientCount() const; // active clients, the local one included
    bool deathmatch() const { return deathmatch_; }
    bool coop() const { return coop_; }
    // SV_DirectConnect + SV_Begin_f: a new client in the first free slot
    // (1 .. maxClients() - 1); -1 + error when the server is full or the
    // game refuses it (ClientConnect's "rejmsg").
    int connectClient(const std::string &name, float fov, int hand, std::string &error,
                      const std::string &ip = "loopback");
    // SV_DropClient: ClientDisconnect. Not for client 0 (stop() ends the game).
    void disconnectClient(int client);
    // A client's usercmds for dt seconds (split into <= 250 ms commands,
    // leftover milliseconds carried), without running the game.
    void clientMove(int client, const GameUserCmd &cmd, double dt);
    // RunFrame for every 100 ms of dt that passed (at most 5); the number run.
    int advance(double dt);
    void clientCommand(int client, const std::string &line);
    void setFov(int client, float fov);
    void setHand(int client, int hand);
    const char *userinfo(int client) const;
    const q2edict_t *player(int client) const;
    const q2player_state_t *playerState(int client) const;
    Vec3 playerOrigin(int client) const;
    // What client `client` draws: every entity but its own body; other
    // players (modelindex 255) as players/<model>/tris.md2 from their
    // CS_PLAYERSKINS string.
    std::vector<GameRenderEntity> renderEntitiesFor(int client, bool withInline = false) const;
    const std::array<int16_t, Q2_MAX_ITEMS> &inventory(int client) const;
    std::vector<GameEvent> takeEvents(int client);
    void snapshot(int client, GameSnapshot &out, bool full);

    // A client command ("give all", "use shotgun", "god", ...) or a server
    // command ("sv addip 1.2.3.4" without the "sv").
    void clientCommand(const std::string &line);
    void serverCommand(const std::string &line);
    // A new userinfo fov for the local client (ClientUserinfoChanged), as
    // Quake 2's client sends when the fov cvar changes.
    void setFov(float fov);
    // A new userinfo hand (the "hand" cvar): the side the game fires
    // projectiles from (P_ProjectSource) and the view weapon is drawn on.
    void setHand(int hand);
    // Quake 2's hand values: 1 left, 2 centre; anything else is right (0),
    // as P_ProjectSource treats it.
    static int clampHand(int hand) { return hand == 1 || hand == 2 ? hand : 0; }
    // The local client's userinfo string ("\\name\\Player\\skin\\...").
    const char *userinfo() const { return userinfo(0); }

    // ---- what the sandbox draws / plays
    int numEdicts() const;
    const q2edict_t *edict(int index) const;
    const q2edict_t *player() const { return edict(1); }
    const q2player_state_t *playerState() const { return playerState(0); }
    Vec3 playerOrigin() const { return playerOrigin(0); }
    Vec3 viewOrigin() const;  // origin + viewoffset
    Vec3 viewAngles() const;  // ps.viewangles + kick_angles (Quake 2 conventions)
    int frameNumber() const { return frameNum_; }
    double time() const { return frameNum_ * kFrameSeconds; }
    // How far the sandbox is into the next 100 ms game frame, 0..1 (the
    // client's lerpfrac: draw entities between the last two frames).
    double lerpFraction() const;
    // Entities to draw, in edict order. `withInline` keeps brush models.
    std::vector<GameRenderEntity> renderEntities(bool withInline = false) const {
        return renderEntitiesFor(0, withInline);
    }
    const std::string &configString(int index) const;
    // The player's item counts from the last svc_inventory, by item index
    // (CS_ITEMS + index names it); the game sends them while its inventory
    // screen is open.
    const std::array<int16_t, Q2_MAX_ITEMS> &inventory() const { return inventory(0); }
    // Model name of an edict ("*3", "models/items/armor/body/tris.md2"), "" if none.
    std::string modelName(int modelindex) const;
    // Inline model number for a "*n" modelindex, 0 otherwise.
    int inlineModel(int modelindex) const;
    std::vector<GameEvent> takeEvents() { return takeEvents(0); }
    // RF_BEAM entities (lasers), which renderEntities leaves out.
    std::vector<GameBeamEntity> beamEntities() const;
    // Every entity with a looping sound, the local player's own included.
    std::vector<GameLoopSound> loopSounds() const;
    // The client's view of this sandbox frame (GameSnapshot), taking the
    // queued events. `full` sends everything (a client starting over);
    // otherwise only what changed since the last snapshot.
    void snapshot(GameSnapshot &out, bool full) { snapshot(0, out, full); }

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
    // svs.clients[n]
    struct Client {
        bool active = false;
        int edict = 0; // n + 1
        std::string name = "Player", ip = "loopback";
        float fov = 90.0f;
        int hand = 0;
        char userinfo[512] = {};
        q2usercmd_t lastCmd{};
        double msecAccumulator = 0.0;
        std::deque<GameEvent> events;
        std::array<int16_t, Q2_MAX_ITEMS> inventory{};
        // what the last snapshot() sent
        std::vector<std::string> sentConfigStrings;
        std::array<int16_t, Q2_MAX_ITEMS> sentInventory{};
        int snapshotFrame = -1;
    };

    friend struct GameImports;

    bool loadModule(std::string &error);
    // Checks a save against this build of the game and writes the level
    // file ReadLevel will read (`levelFile`, deleted afterwards).
    bool prepareLoad(const std::string &dir, std::string &levelFile, std::string &error);
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
    // The engine's cvar values into the game's copies, but those the game
    // registered with CVAR_LATCH (except `alsoLatched`): they keep the values
    // they had at Init until the next game.
    void syncCvars(const char *alsoLatched = nullptr);
    void tokenize(const std::string &line);
    void flushMessage(const float *origin, int entity);
    void queueSound(const float *origin, q2edict_t *ent, int channel, int soundindex, float volume, float attenuation,
                    float timeofs);
    void print(int level, const std::string &text, GameEvent::Kind kind, int entity);
    void gameError(const std::string &text);
    void prepWorldFrame();
    void sendUserCmd(int client, int msec, const GameUserCmd &cmd);
    // The client in edict `edict`, -1 for any other edict.
    int clientOfEdict(int edict) const;
    // To client edict `target`'s queue, or every active client's for 0.
    void pushEvent(GameEvent ev, int target);
    void setInventory(int target, const std::array<int16_t, Q2_MAX_ITEMS> &inventory);
    // userinfo for a client: name, skin, hand, fov, rate, msg, ip.
    void buildUserinfo(Client &c);
    void userinfoChanged(int client);
    bool beginClient(int client, std::string &error);
    // CL_LoadClientinfo: "players/<model>/tris.md2" for a modelindex 255
    // entity's skinnum (CS_PLAYERSKINS + skinnum & 0xff), male if unknown.
    std::string playerModel(int skinnum) const;
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
    std::vector<Client> clients_;
    bool deathmatch_ = false, coop_ = false;
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
    std::vector<std::string> commands_;
    TagBlock *tags_ = nullptr;

    double frameAccumulator_ = 0.0;
    int frameNum_ = 0;
    bool inGameCall_ = false;
    void *abortJump_ = nullptr;
};

} // namespace realms
