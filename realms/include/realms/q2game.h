/* Realms Engine - the Quake 2 game module ABI (game.h, GAME_API_VERSION 3).
 *
 * The game module is the Free Pascal port of Quake 2's gamex86 in game/
 * (monsters, items, weapons, triggers, player physics glue). It is built as
 * librealms_game (or linked into realms-pascal) and exports GetGameAPI, like
 * the original DLL. realms::GameServer (GameServer.h) is the server side:
 * it fills game_import_t from realms::Engine and calls game_export_t.
 *
 * The types below are q_shared.h / game.h with the Quake 2 field order. The
 * Pascal records use {$PACKRECORDS C}, so both sides agree on every offset;
 * the static_asserts pin the x86_64 / arm64 layout (the game/ units are checked
 * against the same numbers by tests/realms/game_server_test.cpp).
 */
#ifndef REALMS_Q2GAME_H
#define REALMS_Q2GAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Q2_GAME_API_VERSION 3
#define Q2_GAME_ENTRY_SYMBOL "GetGameAPI"

typedef float q2vec3_t[3];
typedef int32_t q2bool; /* qboolean: LongBool in the Pascal port */

enum {
    Q2_MAX_QPATH = 64,
    Q2_MAX_EDICTS = 1024,
    Q2_MAX_MODELS = 256,
    Q2_MAX_SOUNDS = 256,
    Q2_MAX_IMAGES = 256,
    Q2_MAX_LIGHTSTYLES = 256,
    Q2_MAX_ITEMS = 256,
    Q2_MAX_CLIENTS = 256,
    Q2_MAX_GENERAL = Q2_MAX_CLIENTS * 2,
    Q2_MAX_STATS = 32,
    Q2_MAX_ENT_CLUSTERS = 16,
    Q2_MAXTOUCH = 32
};

/* config strings (q_shared.h) */
enum {
    Q2_CS_NAME = 0,
    Q2_CS_CDTRACK = 1,
    Q2_CS_SKY = 2,
    Q2_CS_SKYAXIS = 3,
    Q2_CS_SKYROTATE = 4,
    Q2_CS_STATUSBAR = 5,
    Q2_CS_AIRACCEL = 29,
    Q2_CS_MAXCLIENTS = 30,
    Q2_CS_MAPCHECKSUM = 31,
    Q2_CS_MODELS = 32,
    Q2_CS_SOUNDS = Q2_CS_MODELS + Q2_MAX_MODELS,
    Q2_CS_IMAGES = Q2_CS_SOUNDS + Q2_MAX_SOUNDS,
    Q2_CS_LIGHTS = Q2_CS_IMAGES + Q2_MAX_IMAGES,
    Q2_CS_ITEMS = Q2_CS_LIGHTS + Q2_MAX_LIGHTSTYLES,
    Q2_CS_PLAYERSKINS = Q2_CS_ITEMS + Q2_MAX_ITEMS,
    Q2_CS_GENERAL = Q2_CS_PLAYERSKINS + Q2_MAX_CLIENTS,
    Q2_MAX_CONFIGSTRINGS = Q2_CS_GENERAL + Q2_MAX_GENERAL
};

/* player_state_t.stats indices used by the HUD (q_shared.h) */
enum {
    Q2_STAT_HEALTH_ICON = 0,
    Q2_STAT_HEALTH = 1,
    Q2_STAT_AMMO_ICON = 2,
    Q2_STAT_AMMO = 3,
    Q2_STAT_ARMOR_ICON = 4,
    Q2_STAT_ARMOR = 5,
    Q2_STAT_SELECTED_ICON = 6,
    Q2_STAT_PICKUP_ICON = 7,
    Q2_STAT_PICKUP_STRING = 8,
    Q2_STAT_TIMER_ICON = 9,
    Q2_STAT_TIMER = 10,
    Q2_STAT_HELPICON = 11,
    Q2_STAT_SELECTED_ITEM = 12,
    Q2_STAT_LAYOUTS = 13,
    Q2_STAT_FRAGS = 14,
    Q2_STAT_FLASHES = 15
};

/* edict_t.svflags */
enum { Q2_SVF_NOCLIENT = 1, Q2_SVF_DEADMONSTER = 2, Q2_SVF_MONSTER = 4 };

/* entity_state_t effects (EF_*) and renderfx (RF_*) the sandbox draws with */
enum { Q2_EF_ROTATE = 0x00000001, Q2_EF_GIB = 0x00000002, Q2_EF_BLASTER = 0x00000008, Q2_EF_ROCKET = 0x00000010,
       Q2_EF_GRENADE = 0x00000020, Q2_EF_HYPERBLASTER = 0x00000040, Q2_EF_BFG = 0x00000080,
       Q2_EF_COLOR_SHELL = 0x00000100, Q2_EF_POWERSCREEN = 0x00000200, Q2_EF_ANIM01 = 0x00000400,
       Q2_EF_ANIM23 = 0x00000800, Q2_EF_ANIM_ALL = 0x00001000, Q2_EF_ANIM_ALLFAST = 0x00002000,
       Q2_EF_FLIES = 0x00004000, Q2_EF_QUAD = 0x00008000, Q2_EF_PENT = 0x00010000, Q2_EF_TELEPORTER = 0x00020000,
       Q2_EF_FLAG1 = 0x00040000, Q2_EF_FLAG2 = 0x00080000, Q2_EF_IONRIPPER = 0x00100000,
       Q2_EF_GREENGIB = 0x00200000, Q2_EF_BLUEHYPERBLASTER = 0x00400000, Q2_EF_SPINNINGLIGHTS = 0x00800000,
       Q2_EF_PLASMA = 0x01000000, Q2_EF_TRAP = 0x02000000, Q2_EF_TRACKER = 0x04000000, Q2_EF_DOUBLE = 0x08000000,
       Q2_EF_SPHERETRANS = 0x10000000, Q2_EF_TAGTRAIL = 0x20000000, Q2_EF_HALF_DAMAGE = 0x40000000 };
#define Q2_EF_TRACKERTRAIL 0x80000000u
enum { Q2_RF_MINLIGHT = 1, Q2_RF_VIEWERMODEL = 2, Q2_RF_WEAPONMODEL = 4, Q2_RF_FULLBRIGHT = 8,
       Q2_RF_DEPTHHACK = 16, Q2_RF_TRANSLUCENT = 32, Q2_RF_FRAMELERP = 64, Q2_RF_BEAM = 128,
       Q2_RF_CUSTOMSKIN = 256, Q2_RF_GLOW = 512, Q2_RF_SHELL_RED = 1024, Q2_RF_SHELL_GREEN = 2048,
       Q2_RF_SHELL_BLUE = 4096 };

/* pmove_state_t.pm_flags */
enum {
    Q2_PMF_DUCKED = 1,
    Q2_PMF_JUMP_HELD = 2,
    Q2_PMF_ON_GROUND = 4,
    Q2_PMF_TIME_WATERJUMP = 8,
    Q2_PMF_TIME_LAND = 16,
    Q2_PMF_TIME_TELEPORT = 32,
    Q2_PMF_NO_PREDICTION = 64
};

/* usercmd_t.buttons */
enum { Q2_BUTTON_ATTACK = 1, Q2_BUTTON_USE = 2, Q2_BUTTON_ANY = 128 };

/* sound channels / attenuation */
enum { Q2_CHAN_AUTO = 0, Q2_CHAN_WEAPON = 1, Q2_CHAN_VOICE = 2, Q2_CHAN_ITEM = 3, Q2_CHAN_BODY = 4 };
enum { Q2_PRINT_LOW = 0, Q2_PRINT_MEDIUM = 1, Q2_PRINT_HIGH = 2, Q2_PRINT_CHAT = 3 };
enum { Q2_ERR_FATAL = 0, Q2_ERR_DROP = 1 };

typedef enum { Q2_SOLID_NOT, Q2_SOLID_TRIGGER, Q2_SOLID_BBOX, Q2_SOLID_BSP } q2solid_t;
typedef enum { Q2_PM_NORMAL, Q2_PM_SPECTATOR, Q2_PM_DEAD, Q2_PM_GIB, Q2_PM_FREEZE } q2pmtype_t;
typedef enum {
    Q2_MULTICAST_ALL,
    Q2_MULTICAST_PHS,
    Q2_MULTICAST_PVS,
    Q2_MULTICAST_ALL_R,
    Q2_MULTICAST_PHS_R,
    Q2_MULTICAST_PVS_R
} q2multicast_t;
enum { Q2_AREA_SOLID = 1, Q2_AREA_TRIGGERS = 2 };

typedef struct q2cvar_s {
    char *name;
    char *string;
    char *latched_string;
    int32_t flags;
    q2bool modified;
    float value;
    struct q2cvar_s *next;
} q2cvar_t;

typedef struct {
    q2vec3_t normal;
    float dist;
    uint8_t type;
    uint8_t signbits;
    uint8_t pad[2];
} q2cplane_t;

typedef struct {
    char name[16];
    int32_t flags;
    int32_t value;
} q2csurface_t;

struct q2edict_s;

typedef struct {
    q2bool allsolid;
    q2bool startsolid;
    float fraction;
    q2vec3_t endpos;
    q2cplane_t plane;
    q2csurface_t *surface;
    int32_t contents;
    struct q2edict_s *ent;
} q2trace_t;

typedef struct {
    q2pmtype_t pm_type;
    int16_t origin[3];   /* 12.3 fixed point */
    int16_t velocity[3]; /* 12.3 fixed point */
    uint8_t pm_flags;
    uint8_t pm_time;     /* each unit = 8 ms */
    int16_t gravity;
    int16_t delta_angles[3];
} q2pmove_state_t;

typedef struct {
    uint8_t msec;
    uint8_t buttons;
    int16_t angles[3];
    int16_t forwardmove, sidemove, upmove;
    uint8_t impulse;
    uint8_t lightlevel;
} q2usercmd_t;

typedef struct {
    q2pmove_state_t s;
    q2usercmd_t cmd;
    q2bool snapinitial;
    int32_t numtouch;
    struct q2edict_s *touchents[Q2_MAXTOUCH];
    q2vec3_t viewangles;
    float viewheight;
    q2vec3_t mins, maxs;
    struct q2edict_s *groundentity;
    int32_t watertype;
    int32_t waterlevel;
    q2trace_t (*trace)(q2vec3_t start, q2vec3_t mins, q2vec3_t maxs, q2vec3_t end);
    int32_t (*pointcontents)(q2vec3_t point);
} q2pmove_t;

typedef struct {
    q2pmove_state_t pmove;
    q2vec3_t viewangles;
    q2vec3_t viewoffset;
    q2vec3_t kick_angles;
    q2vec3_t gunangles;
    q2vec3_t gunoffset;
    int32_t gunindex;
    int32_t gunframe;
    float blend[4];
    float fov;
    int32_t rdflags;
    int16_t stats[Q2_MAX_STATS];
} q2player_state_t;

typedef struct {
    int32_t number;
    q2vec3_t origin;
    q2vec3_t angles;
    q2vec3_t old_origin;
    int32_t modelindex;
    int32_t modelindex2, modelindex3, modelindex4;
    int32_t frame;
    int32_t skinnum;
    uint32_t effects;
    int32_t renderfx;
    int32_t solid;
    int32_t sound;
    int32_t event;
} q2entity_state_t;

/* The part of gclient_t the server sees; the rest belongs to the game. */
typedef struct {
    q2player_state_t ps;
    int32_t ping;
} q2gclient_t;

typedef struct q2link_s {
    struct q2link_s *prev, *next;
} q2link_t;

/* The part of edict_t the server sees; the game's edict is game_export.edict_size bytes. */
typedef struct q2edict_s {
    q2entity_state_t s;
    q2gclient_t *client;
    q2bool inuse;
    int32_t linkcount;
    q2link_t area;
    int32_t num_clusters; /* -1: use headnode */
    int32_t clusternums[Q2_MAX_ENT_CLUSTERS];
    int32_t headnode;
    int32_t areanum, areanum2;
    int32_t svflags;
    q2vec3_t mins, maxs;
    q2vec3_t absmin, absmax, size;
    q2solid_t solid;
    int32_t clipmask;
    struct q2edict_s *owner;
} q2edict_t;

typedef struct {
    void (*bprintf)(int32_t printlevel, const char *fmt, ...);
    void (*dprintf)(const char *fmt, ...);
    void (*cprintf)(q2edict_t *ent, int32_t printlevel, const char *fmt, ...);
    void (*centerprintf)(q2edict_t *ent, const char *fmt, ...);
    void (*sound)(q2edict_t *ent, int32_t channel, int32_t soundindex, float volume, float attenuation, float timeofs);
    void (*positioned_sound)(q2vec3_t origin, q2edict_t *ent, int32_t channel, int32_t soundindex, float volume,
                             float attenuation, float timeofs);
    void (*configstring)(int32_t num, const char *string);
    void (*error)(const char *fmt, ...);
    int32_t (*modelindex)(const char *name);
    int32_t (*soundindex)(const char *name);
    int32_t (*imageindex)(const char *name);
    void (*setmodel)(q2edict_t *ent, const char *name);
    q2trace_t (*trace)(q2vec3_t start, q2vec3_t mins, q2vec3_t maxs, q2vec3_t end, q2edict_t *passent,
                       int32_t contentmask);
    int32_t (*pointcontents)(q2vec3_t point);
    q2bool (*inPVS)(q2vec3_t p1, q2vec3_t p2);
    q2bool (*inPHS)(q2vec3_t p1, q2vec3_t p2);
    void (*SetAreaPortalState)(int32_t portalnum, q2bool open);
    q2bool (*AreasConnected)(int32_t area1, int32_t area2);
    void (*linkentity)(q2edict_t *ent);
    void (*unlinkentity)(q2edict_t *ent);
    int32_t (*BoxEdicts)(q2vec3_t mins, q2vec3_t maxs, q2edict_t **list, int32_t maxcount, int32_t areatype);
    void (*Pmove)(q2pmove_t *pmove);
    void (*multicast)(q2vec3_t origin, q2multicast_t to);
    void (*unicast)(q2edict_t *ent, q2bool reliable);
    void (*WriteChar)(int32_t c);
    void (*WriteByte)(int32_t c);
    void (*WriteShort)(int32_t c);
    void (*WriteLong)(int32_t c);
    void (*WriteFloat)(float f);
    void (*WriteString)(const char *s);
    void (*WritePosition)(q2vec3_t pos);
    void (*WriteDir)(q2vec3_t pos);
    void (*WriteAngle)(float f);
    void *(*TagMalloc)(int32_t size, int32_t tag);
    void (*TagFree)(void *block);
    void (*FreeTags)(int32_t tag);
    q2cvar_t *(*cvar)(const char *var_name, const char *value, int32_t flags);
    q2cvar_t *(*cvar_set)(const char *var_name, const char *value);
    q2cvar_t *(*cvar_forceset)(const char *var_name, const char *value);
    int32_t (*argc)(void);
    char *(*argv)(int32_t n);
    char *(*args)(void);
    void (*AddCommandString)(const char *text);
    void (*DebugGraph)(float value, int32_t color);
} q2game_import_t;

typedef struct {
    int32_t apiversion;
    void (*Init)(void);
    void (*Shutdown)(void);
    void (*SpawnEntities)(const char *mapname, const char *entstring, const char *spawnpoint);
    void (*WriteGame)(const char *filename, q2bool autosave);
    void (*ReadGame)(const char *filename);
    void (*WriteLevel)(const char *filename);
    void (*ReadLevel)(const char *filename);
    q2bool (*ClientConnect)(q2edict_t *ent, char *userinfo);
    void (*ClientBegin)(q2edict_t *ent);
    void (*ClientUserinfoChanged)(q2edict_t *ent, char *userinfo);
    void (*ClientDisconnect)(q2edict_t *ent);
    void (*ClientCommand)(q2edict_t *ent);
    void (*ClientThink)(q2edict_t *ent, q2usercmd_t *cmd);
    void (*RunFrame)(void);
    void (*ServerCommand)(void);
    q2edict_t *edicts;
    int32_t edict_size;
    int32_t num_edicts;
    int32_t max_edicts;
} q2game_export_t;

/* GetGameAPI(const game_import_t &import): the Pascal export takes the import
 * table by reference (a pointer), like the C original took it by value on
 * 32-bit x86 where both are the same on the stack. */
typedef q2game_export_t *(*Q2GetGameApiFn)(q2game_import_t *import);

#ifdef __cplusplus
} /* extern "C" */

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
#include <cstddef>
static_assert(sizeof(q2trace_t) == 72 && offsetof(q2trace_t, surface) == 48 && offsetof(q2trace_t, ent) == 64,
              "trace_t layout");
static_assert(sizeof(q2cvar_t) == 48 && sizeof(q2cplane_t) == 20 && sizeof(q2csurface_t) == 24, "q_shared layout");
static_assert(sizeof(q2pmove_state_t) == 28 && sizeof(q2usercmd_t) == 16, "pmove_state_t / usercmd_t layout");
static_assert(sizeof(q2pmove_t) == 384 && offsetof(q2pmove_t, touchents) == 56 && offsetof(q2pmove_t, viewangles) == 312 &&
                  offsetof(q2pmove_t, groundentity) == 352 && offsetof(q2pmove_t, trace) == 368,
              "pmove_t layout");
static_assert(sizeof(q2player_state_t) == 184 && offsetof(q2player_state_t, stats) == 120, "player_state_t layout");
static_assert(sizeof(q2entity_state_t) == 84 && offsetof(q2gclient_t, ping) == 184, "entity_state_t / gclient_t layout");
static_assert(offsetof(q2edict_t, client) == 88 && offsetof(q2edict_t, area) == 104 && offsetof(q2edict_t, headnode) == 188 &&
                  offsetof(q2edict_t, svflags) == 200 && offsetof(q2edict_t, solid) == 264 && offsetof(q2edict_t, owner) == 272,
              "edict_t server layout");
static_assert(sizeof(q2game_import_t) == 352 && sizeof(q2game_export_t) == 152 && offsetof(q2game_export_t, edicts) == 128,
              "game_import_t / game_export_t layout");
#endif
#endif /* __cplusplus */

#endif /* REALMS_Q2GAME_H */
