// Realms Engine - C++17 facade used by every sandbox host.
//
// realms::Engine owns the bridge to a Realms core (Free Pascal Quake 2 stack
// or the C++ reference core), installs the host services (console output,
// platform file access, host commands) and exposes an idiomatic C++ API for
// cvars, the command buffer, the PAK virtual filesystem, the BSP collision
// model and the frame clock.
//
// Cores keep Quake 2 style global state, therefore only one Engine may be
// initialised per process at a time, and it must be driven from one thread.
#pragma once

#include "realms/MeshMap.h"
#include "realms/realms_bridge.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace realms {

constexpr const char *kEngineName = "Realms";
constexpr const char *kEngineVersion = "0.1.0";

enum class BackendPreference {
    Auto,   // Pascal core when it can be loaded, otherwise the C++ core
    Native, // always the C++ reference core
    Pascal  // Pascal core or fail
};

struct ConsoleLine {
    int level = REALMS_PRINT_INFO;
    std::string text;
};

using Vec3 = std::array<float, 3>;

// trace_t (see RealmsTrace).
struct TraceResult {
    bool allsolid = false;   // the whole move was inside a solid
    bool startsolid = false; // the start position was inside a solid
    float fraction = 1.0f;   // 1 = moved the whole way
    Vec3 endpos{};
    Vec3 normal{};
    float planeDist = 0.0f;
    int32_t contents = 0;
    int32_t surfaceFlags = 0;
    int32_t surfaceValue = 0;
    std::string surface; // texture name of the face that was hit
    bool hit() const { return fraction < 1.0f; }
};

struct MapModel {
    Vec3 mins{}, maxs{}, origin{};
    int32_t headnode = 0;
};

// One entity of a map's entity lump: key/value pairs in file order.
using MapEntity = std::vector<std::pair<std::string, std::string>>;

using CommandArgs = std::vector<std::string>;
using CommandFn = std::function<void(const CommandArgs &)>;

struct EngineConfig {
    BackendPreference backend = BackendPreference::Auto;
    // Explicit Pascal core library (librealms_core.so/.dylib). When empty the
    // REALMS_CORE_LIBRARY environment variable and standard locations next to
    // the executable are probed. REALMS_CORE=native|pascal|auto overrides
    // `backend` at runtime.
    std::string pascalLibrary;
    // Directory of the running executable (used to locate the Pascal core).
    std::string executableDir;
    // Read-only roots searched for loose files (assets dir, game dir...).
    std::vector<std::string> searchRoots;
    // Writable directory for writeconfig/config.cfg. Empty disables writes.
    std::string writableRoot;
    // Platform file access override (Android AAssetManager, iOS bundle...).
    // When set it replaces the searchRoots disk lookup.
    std::function<bool(const std::string &path, std::vector<unsigned char> &out)> readFile;
    // Optional random access read for large PAKs: returns bytes read or -1.
    std::function<int32_t(const std::string &path, int64_t offset, int32_t size, void *out)> readRange;
    // Mirrors every console line (stdout, logcat, os_log...).
    std::function<void(int level, const std::string &text)> printSink;
    size_t consoleCapacity = 512;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    bool init(EngineConfig config);
    void shutdown();
    bool initialized() const { return core_ != nullptr; }

    // "pascal-quake2" or "native-cpp" ("offline" before init).
    std::string backendName() const;
    bool pascalBackend() const { return pascal_; }
    // Where the core came from: library path, "static" or "built-in".
    const std::string &backendOrigin() const { return origin_; }
    // Why the Pascal core was not used (empty when it was, or not requested).
    const std::string &pascalLoadError() const { return pascalError_; }

    // Console variables -----------------------------------------------------
    bool registerCvar(const std::string &name, const std::string &defaultValue, int32_t flags = 0);
    int32_t setCvar(const std::string &name, const std::string &value);
    int32_t forceSetCvar(const std::string &name, const std::string &value);
    bool hasCvar(const std::string &name) const;
    std::string cvarString(const std::string &name, const std::string &fallback = {}) const;
    float cvarFloat(const std::string &name, float fallback = 0.0f) const;
    int cvarInt(const std::string &name, int fallback = 0) const;
    bool cvarBool(const std::string &name, bool fallback = false) const;
    int32_t cvarFlags(const std::string &name) const;
    bool cvarModified(const std::string &name, bool clear = true);
    void forEachCvar(const std::function<void(const std::string &, const std::string &, int32_t)> &fn) const;

    // Commands --------------------------------------------------------------
    bool addCommand(const std::string &name, CommandFn fn);
    void removeCommand(const std::string &name);
    bool commandExists(const std::string &name) const;
    void addText(const std::string &text);      // Cbuf_AddText
    void insertText(const std::string &text);   // Cbuf_InsertText
    void executeBuffer();                       // Cbuf_Execute
    void executeNow(const std::string &line);   // Cmd_ExecuteString
    void exec(const std::string &text) { addText(text); executeBuffer(); }

    // Quake 2 command line: "+set sensitivity 4 +exec autoexec.cfg" becomes
    // "set sensitivity 4\nexec autoexec.cfg\n". Arguments before the first
    // '+' are ignored.
    static std::string commandLineScript(const std::vector<std::string> &args);

    // Virtual filesystem ----------------------------------------------------
    int32_t addPak(const std::string &path);
    bool loadFile(const std::string &path, std::vector<unsigned char> &out) const;
    int32_t pakCount() const;

    // Collision model (Quake 2 cmodel.c) --------------------------------------
    // Loads an IBSP v38 map through the VFS, or a .glb / .gltf triangle
    // mesh map (see realms/MeshMap.h; cvars map_mesh_scale, default 32 units
    // per metre, and map_mesh_thickness). Malformed maps are rejected (the
    // reason is printed to the console) and leave no map loaded.
    // Headnode 0 is the world model, as in Quake 2's CM_PointContents(p, 0).
    bool loadMap(const std::string &path);
    // Hands a host-built triangle mesh to the core (map_load_mesh).
    bool loadMeshMap(const MeshMap &map, const std::string &name, uint32_t checksum = 0);
    // Resolves a Quake 2 style map name: "base1" -> maps/base1.bsp, then
    // maps/base1.glb and maps/base1.gltf; paths are tried as given first.
    // Returns an empty string when nothing exists.
    std::string findMap(const std::string &name) const;
    // The mesh map last loaded by loadMap (empty for BSP maps).
    const MeshMap &meshMap() const { return meshMap_; }
    // Path given to the last successful loadMap / loadMeshMap.
    const std::string &mapPath() const { return mapPath_; }
    void unloadMap();
    bool mapLoaded() const;
    bool mapInfo(RealmsMapInfo &out) const;
    std::string mapEntities() const;
    bool mapModel(int32_t index, MapModel &out) const; // 0 = world, n = "*n"
    int32_t headnodeForBox(const Vec3 &mins, const Vec3 &maxs);
    int32_t pointContents(const Vec3 &point, int32_t headnode = 0) const;
    int32_t transformedPointContents(const Vec3 &point, int32_t headnode, const Vec3 &origin,
                                     const Vec3 &angles) const;
    TraceResult boxTrace(const Vec3 &start, const Vec3 &end, const Vec3 &mins, const Vec3 &maxs,
                         int32_t headnode = 0, int32_t brushmask = REALMS_MASK_ALL) const;
    TraceResult transformedBoxTrace(const Vec3 &start, const Vec3 &end, const Vec3 &mins, const Vec3 &maxs,
                                    int32_t headnode, int32_t brushmask, const Vec3 &origin,
                                    const Vec3 &angles) const;
    int32_t pointLeaf(const Vec3 &point) const;
    bool leafInfo(int32_t leaf, int32_t &contents, int32_t &cluster, int32_t &area) const;

    // Visibility (bridge v3) ------------------------------------------------
    // Decompressed PVS / PHS rows, (numclusters + 7) / 8 bytes; empty without
    // a map. Maps without visibility data see everything.
    std::vector<uint8_t> clusterPVS(int32_t cluster) const;
    std::vector<uint8_t> clusterPHS(int32_t cluster) const;
    static bool clusterVisible(const std::vector<uint8_t> &row, int32_t cluster);
    // Area portals start closed when a map loads (doors open them).
    void setAreaPortal(int32_t portal, bool open);
    int32_t areaPortalState(int32_t portal) const; // 1 open, 0 closed, -1 invalid / no map
    bool areasConnected(int32_t area1, int32_t area2) const;
    std::vector<uint8_t> areaBits(int32_t area) const; // area <= 0: every area
    // Leafs touched by a box (CM_BoxLeafnums), at most maxLeafs. topnode is
    // the first node that splits the box, -1 when it never splits.
    std::vector<int32_t> boxLeafs(const Vec3 &mins, const Vec3 &maxs, int32_t headnode = 0,
                                  int32_t *topnode = nullptr, size_t maxLeafs = 1024) const;
    bool headnodeVisible(int32_t headnode, const std::vector<uint8_t> &visbits) const;

    // Quake 2 entity lump parser (ED_ParseEdict syntax, // comments).
    static std::vector<MapEntity> parseEntities(const std::string &text);
    static std::string entityValue(const MapEntity &entity, const std::string &key);

    // Frame clock: accumulates real time and advances the core in whole
    // milliseconds, running the command buffer (Qcommon_Frame).
    void frame(double seconds);

    // Console ---------------------------------------------------------------
    const std::deque<ConsoleLine> &console() const { return console_; }
    uint64_t consoleRevision() const { return consoleRevision_; }
    void clearConsole();
    void print(const std::string &text, int level = REALMS_PRINT_INFO);

private:
    static void hostPrint(void *user, int32_t level, const char *text);
    static int32_t hostReadFile(void *user, const char *path, void **outData, int32_t *outSize);
    static void hostFreeFile(void *user, void *data);
    static int32_t hostReadRange(void *user, const char *path, int64_t offset, int32_t size, void *out);
    static int32_t hostWriteFile(void *user, const char *path, const void *data, int32_t size);
    static void hostCommand(void *user, int32_t argc, const char *const *argv);
    static double hostMilliseconds(void *user);

    bool readHostFile(const std::string &path, std::vector<unsigned char> &out) const;
    const RealmsCoreApi *loadPascalCore(const EngineConfig &config);

    EngineConfig config_;
    RealmsHostApi host_{};
    const RealmsCoreApi *core_ = nullptr;
    bool pascal_ = false;
    std::string origin_;
    std::string pascalError_;
    std::map<std::string, CommandFn> commands_;
    std::deque<ConsoleLine> console_;
    uint64_t consoleRevision_ = 0;
    double msecAccumulator_ = 0.0;
    MeshMap meshMap_;
    std::string mapPath_;
};

} // namespace realms
