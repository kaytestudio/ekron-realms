/*
 * Realms Engine bridge ABI
 * ------------------------
 * The single, language-neutral contract between the Quake 2 derived core
 * (Free Pascal, realms/pascal) and the native C++17 sandbox hosts
 * (Desktop SDL2/OpenGL, Android GameActivity/GLES, iOS UIKit/Metal).
 *
 *   +-----------------------------+        +------------------------------+
 *   |  C++ host (sys/desktop,     |  host  |  Realms core                 |
 *   |  sys/android, sys/ios)      | -----> |  - Pascal: librealms_core    |
 *   |  realms::Engine facade      | <----- |  - C++ reference: NativeCore |
 *   +-----------------------------+  core  +------------------------------+
 *
 * Rules that keep the ABI portable across FPC and Clang/GCC/MSVC:
 *   - Plain C, fixed-width integers, `const char *` UTF-8 strings, no
 *     exceptions or C++ types cross the boundary.
 *   - Every callback uses the platform C calling convention (`cdecl` in
 *     Pascal).
 *   - Memory is always released by the side that allocated it: strings are
 *     copied into caller-provided buffers; file blobs returned by the host are
 *     released with host->free_file, blobs returned by the core with
 *     core->fs_free.
 *   - Both structs start with struct_size/version so either side can be
 *     extended without breaking older binaries.
 *
 * Keep realms/pascal/rlm_bridge.pas in sync with this header.
 */
#ifndef REALMS_BRIDGE_H
#define REALMS_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 2: collision model (map_*) appended to RealmsCoreApi. */
#define REALMS_BRIDGE_VERSION 3u

/* Symbol exported by the Free Pascal core (librealms_core / realms_core.a). */
#define REALMS_PASCAL_ENTRY_SYMBOL "realms_pascal_get_api"

/* Console message levels routed through RealmsHostApi::print. */
enum {
    REALMS_PRINT_INFO = 0,
    REALMS_PRINT_WARNING = 1,
    REALMS_PRINT_ERROR = 2,
    REALMS_PRINT_DEVELOPER = 3
};

/* Quake 2 cvar flags (q_shared.h CVAR_*). */
enum {
    REALMS_CVAR_ARCHIVE = 1,    /* saved to config.cfg */
    REALMS_CVAR_USERINFO = 2,   /* sent to server on connect/change */
    REALMS_CVAR_SERVERINFO = 4, /* sent in response to front-end requests */
    REALMS_CVAR_NOSET = 8,      /* only settable from the command line / host */
    REALMS_CVAR_LATCH = 16,     /* applied on the next engine restart */
    REALMS_CVAR_CHEAT = 32      /* Realms addition: requires sv_cheats 1 */
};

/* Return codes for cvar_set. */
enum {
    REALMS_CVAR_SET_OK = 1,
    REALMS_CVAR_SET_UNKNOWN = 0,
    REALMS_CVAR_SET_READONLY = -1,
    REALMS_CVAR_SET_LATCHED = -2,
    REALMS_CVAR_SET_CHEAT = -3
};

/* Quake 2 brush contents (q_shared.h CONTENTS_*) and trace masks. */
enum {
    REALMS_CONTENTS_SOLID = 0x1,
    REALMS_CONTENTS_WINDOW = 0x2,
    REALMS_CONTENTS_AUX = 0x4,
    REALMS_CONTENTS_LAVA = 0x8,
    REALMS_CONTENTS_SLIME = 0x10,
    REALMS_CONTENTS_WATER = 0x20,
    REALMS_CONTENTS_MIST = 0x40,
    REALMS_CONTENTS_AREAPORTAL = 0x8000,
    REALMS_CONTENTS_PLAYERCLIP = 0x10000,
    REALMS_CONTENTS_MONSTERCLIP = 0x20000,
    REALMS_CONTENTS_ORIGIN = 0x1000000,
    REALMS_CONTENTS_MONSTER = 0x2000000,
    REALMS_CONTENTS_DEADMONSTER = 0x4000000,
    REALMS_CONTENTS_DETAIL = 0x8000000,
    REALMS_CONTENTS_TRANSLUCENT = 0x10000000,
    REALMS_CONTENTS_LADDER = 0x20000000,

    REALMS_MASK_ALL = -1,
    REALMS_MASK_SOLID = REALMS_CONTENTS_SOLID | REALMS_CONTENTS_WINDOW,
    REALMS_MASK_PLAYERSOLID = REALMS_CONTENTS_SOLID | REALMS_CONTENTS_PLAYERCLIP | REALMS_CONTENTS_WINDOW |
                              REALMS_CONTENTS_MONSTER,
    REALMS_MASK_WATER = REALMS_CONTENTS_WATER | REALMS_CONTENTS_LAVA | REALMS_CONTENTS_SLIME
};

/* Quake 2 surface flags (q_shared.h SURF_*). */
enum {
    REALMS_SURF_LIGHT = 0x1,
    REALMS_SURF_SLICK = 0x2,
    REALMS_SURF_SKY = 0x4,
    REALMS_SURF_WARP = 0x8,
    REALMS_SURF_TRANS33 = 0x10,
    REALMS_SURF_TRANS66 = 0x20,
    REALMS_SURF_FLOWING = 0x40,
    REALMS_SURF_NODRAW = 0x80
};

/* Result of RealmsCoreApi::map_box_trace (trace_t). */
typedef struct RealmsTrace {
    int32_t allsolid;      /* the whole move was inside a solid */
    int32_t startsolid;    /* the start position was inside a solid */
    float fraction;        /* 1.0 = moved the whole way */
    float endpos[3];       /* final position (world space) */
    float plane_normal[3]; /* surface normal at impact (world space) */
    float plane_dist;
    int32_t contents;      /* contents of the brush that was hit */
    int32_t surface_flags; /* REALMS_SURF_* of the face that was hit */
    int32_t surface_value;
    char surface_name[32]; /* texture name of that face, "" if none */
} RealmsTrace;

/* Summary of the loaded map (RealmsCoreApi::map_info). Counts exclude the
 * internal box hull. Bounds are the world model's, spread by one unit like
 * CM_LoadMap does. */
typedef struct RealmsMapInfo {
    uint32_t struct_size; /* set by the caller to sizeof(RealmsMapInfo) */
    uint32_t checksum;    /* CRC-32 (IEEE) of the .bsp file */
    int32_t num_planes;
    int32_t num_nodes;
    int32_t num_leafs;
    int32_t num_leaf_brushes;
    int32_t num_brushes;
    int32_t num_brush_sides;
    int32_t num_texinfo;
    int32_t num_models;   /* world + inline models "*1".."*n" */
    int32_t num_clusters;
    int32_t num_areas;
    int32_t num_area_portals;
    int32_t entity_length;
    float mins[3];
    float maxs[3];
    char name[64];        /* VFS path the map was loaded from */
} RealmsMapInfo;

/* One surface of a triangle mesh map (RealmsCoreApi::map_load_mesh). */
typedef struct RealmsMeshSurface {
    char name[32];    /* reported as RealmsTrace::surface_name */
    int32_t flags;    /* REALMS_SURF_* */
    int32_t value;
    int32_t contents; /* REALMS_CONTENTS_*; 0 = no collision */
} RealmsMeshSurface;

/* Triangle mesh map (glTF levels, see realms::Engine::loadMap). The core
 * copies everything; each triangle becomes a beveled brush (a slab of
 * `thickness` units behind its front face, counter-clockwise winding) in a
 * kd-tree, so traces, point contents and box hulls behave like BSP maps. */
typedef struct RealmsMeshDesc {
    uint32_t struct_size;            /* sizeof(RealmsMeshDesc) */
    uint32_t checksum;               /* reported by map_info (e.g. CRC-32 of the .glb) */
    const char *name;                /* reported by map_info */
    const float *vertices;           /* xyz triples in map units, Z up */
    int32_t num_vertices;
    const int32_t *indices;          /* 3 per triangle */
    int32_t num_triangles;
    const int32_t *triangle_surfaces; /* per triangle surface index; NULL = surface 0 */
    const RealmsMeshSurface *surfaces;
    int32_t num_surfaces;            /* at least 1 */
    float thickness;                 /* <= 0 selects 2 units */
    const char *entities;            /* entity lump text, may be NULL */
} RealmsMeshDesc;

/* Services the C++ host provides to the core. */
typedef struct RealmsHostApi {
    uint32_t struct_size;
    uint32_t version;
    void *user;

    /* Console output (one logical line or fragment, UTF-8). */
    void (*print)(void *user, int32_t level, const char *text);

    /* Read a whole file through the platform (disk, Android AAssetManager,
     * iOS bundle). Returns 1 on success and hands ownership of *out_data to
     * the caller, who must release it with free_file. */
    int32_t (*read_file)(void *user, const char *path, void **out_data, int32_t *out_size);
    void (*free_file)(void *user, void *data);

    /* Optional random access read used for large PAK archives so they never
     * have to be resident in memory (important on Android/iOS). Copies up to
     * size bytes from offset into out_buffer and returns the byte count, or
     * -1 on error. May be NULL, in which case the whole file is read once. */
    int32_t (*read_file_range)(void *user, const char *path, int64_t offset, int32_t size, void *out_buffer);

    /* Persist a text file into the platform's writable config location
     * (used by "writeconfig"). Returns 1 on success. May be NULL. */
    int32_t (*write_file)(void *user, const char *path, const void *data, int32_t size);

    /* Dispatch a host-registered command (see RealmsCoreApi::cmd_register).
     * argv[0] is the command name. */
    void (*command)(void *user, int32_t argc, const char *const *argv);

    /* Monotonic milliseconds. May be NULL. */
    double (*milliseconds)(void *user);
} RealmsHostApi;

typedef void (*RealmsCvarVisitor)(void *user, const char *name, const char *value, int32_t flags);

/* Services the core provides to the host. */
typedef struct RealmsCoreApi {
    uint32_t struct_size;
    uint32_t version;

    /* "pascal-quake2" or "native-cpp". */
    const char *(*backend_name)(void);

    /* Lifecycle. init returns 1 on success. host must outlive the core. */
    int32_t (*init)(const RealmsHostApi *host);
    void (*shutdown)(void);
    /* Advances core time and runs the command buffer (Qcommon_Frame). */
    void (*frame)(int32_t msec);

    /* Console variables (cvar.c). */
    int32_t (*cvar_register)(const char *name, const char *default_value, int32_t flags);
    int32_t (*cvar_set)(const char *name, const char *value);
    /* Host-side set that bypasses NOSET/LATCH/CHEAT (Cvar_ForceSet). */
    int32_t (*cvar_force_set)(const char *name, const char *value);
    /* Copies the value into buffer (always NUL terminated when size > 0) and
     * returns the full value length, or -1 if the cvar does not exist. */
    int32_t (*cvar_get)(const char *name, char *buffer, int32_t buffer_size);
    float (*cvar_value)(const char *name);
    int32_t (*cvar_flags)(const char *name);
    /* Returns 1 if modified since the last call with clear=1. */
    int32_t (*cvar_modified)(const char *name, int32_t clear);
    void (*cvar_foreach)(RealmsCvarVisitor visitor, void *user);

    /* Command system (cmd.c). */
    int32_t (*cmd_register)(const char *name);
    void (*cmd_unregister)(const char *name);
    int32_t (*cmd_exists)(const char *name);
    void (*cbuf_add_text)(const char *text);
    void (*cbuf_insert_text)(const char *text);
    void (*cbuf_execute)(void);
    /* Runs one line immediately (Cmd_ExecuteString). */
    void (*cmd_execute_string)(const char *text);

    /* Virtual filesystem (files.c): PAK archives mounted over host files. */
    int32_t (*fs_add_pak)(const char *path);   /* returns number of entries or -1 */
    int32_t (*fs_load)(const char *path, void **out_data, int32_t *out_size);
    void (*fs_free)(void *data);
    int32_t (*fs_pak_count)(void);

    /* Collision model (cmodel.c), bridge version 2. Quake 2 IBSP v38 maps are
     * read through the virtual filesystem and fully validated; a map that
     * fails validation is rejected and leaves no map loaded. Headnodes come
     * from map_model / map_headnode_for_box (negative values are leafs);
     * invalid headnodes and calls without a map behave like empty space.
     * origin/angles (degrees: pitch, yaw, roll) may be NULL for untransformed
     * queries. Not thread-safe, like the rest of the core. */
    int32_t (*map_load)(const char *path);      /* 1 on success; NULL or "" unloads */
    int32_t (*map_info)(RealmsMapInfo *out);    /* 1 if a map is loaded */
    /* Copies the entity lump like cvar_get; returns its length or -1. */
    int32_t (*map_entities)(char *buffer, int32_t buffer_size);
    /* Inline model index (0 = world, n = "*n"). Outputs may be NULL. */
    int32_t (*map_model)(int32_t index, float *mins3, float *maxs3, float *origin3, int32_t *headnode);
    /* Temporary box hull (CM_HeadnodeForBox), valid until the next call. */
    int32_t (*map_headnode_for_box)(const float *mins3, const float *maxs3);
    int32_t (*map_point_contents)(const float *point3, int32_t headnode, const float *origin3,
                                  const float *angles3);
    void (*map_box_trace)(const float *start3, const float *end3, const float *mins3, const float *maxs3,
                          int32_t headnode, int32_t brushmask, const float *origin3, const float *angles3,
                          RealmsTrace *out);
    /* World leaf containing point (0 without a map) and its properties. */
    int32_t (*map_point_leaf)(const float *point3);
    int32_t (*map_leaf_info)(int32_t leaf, int32_t *contents, int32_t *cluster, int32_t *area);

    /* Visibility (CM_ClusterPVS / CM_ClusterPHS): copies the decompressed
     * row ((num_clusters + 7) / 8 bytes, bit c = cluster c) and returns the
     * row size, or 0 without a map. Cluster -1 (or out of range) gives an
     * empty row; maps without visibility data see everything. */
    int32_t (*map_cluster_pvs)(int32_t cluster, uint8_t *buffer, int32_t buffer_size);
    int32_t (*map_cluster_phs)(int32_t cluster, uint8_t *buffer, int32_t buffer_size);
    /* Area portals (CM_SetAreaPortalState / CM_AreasConnected /
     * CM_WriteAreaBits). Portals start closed when a map loads. */
    void (*map_set_area_portal)(int32_t portal, int32_t open);
    int32_t (*map_area_portal_open)(int32_t portal); /* 1 open, 0 closed, -1 invalid */
    int32_t (*map_areas_connected)(int32_t area1, int32_t area2);
    /* Bit a = area a reachable from `area` (area 0 or invalid: all bits).
     * Returns the byte count ((num_areas + 7) / 8), 0 without a map. */
    int32_t (*map_write_area_bits)(int32_t area, uint8_t *buffer, int32_t buffer_size);
    /* CM_BoxLeafnums under headnode: writes up to list_size leafs and
     * returns how many were written; *topnode (may be NULL) receives the
     * first node that splits the box, or -1. */
    int32_t (*map_box_leafs)(const float *mins3, const float *maxs3, int32_t *list, int32_t list_size,
                             int32_t headnode, int32_t *topnode);
    /* CM_HeadnodeVisible: 1 if any leaf under headnode is in visbits. */
    int32_t (*map_headnode_visible)(int32_t headnode, const uint8_t *visbits, int32_t visbits_size);
    /* Loads a triangle mesh map (see RealmsMeshDesc); 1 on success.
     * Invalid descriptions are rejected and leave no map loaded. */
    int32_t (*map_load_mesh)(const RealmsMeshDesc *desc);
} RealmsCoreApi;

typedef const RealmsCoreApi *(*RealmsGetApiFn)(uint32_t requested_version);

/* Reference C++ core, always linked into realms_engine. */
const RealmsCoreApi *realms_native_get_api(uint32_t requested_version);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REALMS_BRIDGE_H */
