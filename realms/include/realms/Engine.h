// Realms Engine - C++17 facade used by every sandbox host.
//
// realms::Engine owns the bridge to a Realms core (Free Pascal Quake 2 stack
// or the C++ reference core), installs the host services (console output,
// platform file access, host commands) and exposes an idiomatic C++ API for
// cvars, the command buffer, the PAK virtual filesystem and the frame clock.
//
// Cores keep Quake 2 style global state, therefore only one Engine may be
// initialised per process at a time, and it must be driven from one thread.
#pragma once

#include "realms/realms_bridge.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
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
};

} // namespace realms
