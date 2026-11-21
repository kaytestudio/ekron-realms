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

#define REALMS_BRIDGE_VERSION 1u

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
} RealmsCoreApi;

typedef const RealmsCoreApi *(*RealmsGetApiFn)(uint32_t requested_version);

/* Reference C++ core, always linked into realms_engine. */
const RealmsCoreApi *realms_native_get_api(uint32_t requested_version);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REALMS_BRIDGE_H */
