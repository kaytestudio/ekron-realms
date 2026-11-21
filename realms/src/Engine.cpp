// Realms Engine - C++ facade over the Pascal/C++ cores (see Engine.h).

#include "realms/Engine.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(REALMS_PASCAL_STATIC)
// iOS (and any static link): the Pascal core is linked into the binary.
extern "C" const RealmsCoreApi *realms_pascal_get_api(uint32_t requested_version);
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace realms {
namespace {

std::atomic<bool> gEngineActive{false};

std::string lower(std::string s) {
    for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool safeRelative(const std::string &path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\') return false;
    if (path.find("..") != std::string::npos || path.find(':') != std::string::npos) return false;
    return true;
}

std::string normalizeSlashes(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

BackendPreference envBackend(BackendPreference fallback) {
    const char *env = std::getenv("REALMS_CORE");
    if (!env || !*env) return fallback;
    const std::string v = lower(env);
    if (v == "native" || v == "cpp" || v == "c++") return BackendPreference::Native;
    if (v == "pascal" || v == "fpc") return BackendPreference::Pascal;
    return BackendPreference::Auto;
}

#if !defined(REALMS_PASCAL_STATIC)
const char *libraryFileName() {
#if defined(_WIN32)
    return "realms_core.dll";
#elif defined(__APPLE__)
    return "librealms_core.dylib";
#else
    return "librealms_core.so";
#endif
}

// Libraries are intentionally never unloaded: FPC runtimes register thread
// and exit hooks that must stay valid for the lifetime of the process.
void *openLibrary(const std::string &path, std::string &error) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) error = "LoadLibrary failed (" + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void *>(h);
#else
    void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *e = dlerror();
        error = e ? e : "dlopen failed";
    }
    return h;
#endif
}

void *findSymbol(void *lib, const char *name) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}
#endif

} // namespace

Engine::Engine() = default;

Engine::~Engine() { shutdown(); }

const RealmsCoreApi *Engine::loadPascalCore(const EngineConfig &config) {
    pascalError_.clear();
#if defined(REALMS_PASCAL_STATIC)
    (void)config;
    const RealmsCoreApi *api = realms_pascal_get_api(REALMS_BRIDGE_VERSION);
    if (!api) pascalError_ = "static Pascal core rejected bridge version";
    else origin_ = "static";
    return api;
#else
    std::vector<std::string> candidates;
    if (!config.pascalLibrary.empty()) candidates.push_back(config.pascalLibrary);
    if (const char *env = std::getenv("REALMS_CORE_LIBRARY"); env && *env) candidates.emplace_back(env);
    if (!config.executableDir.empty()) {
        const fs::path exe(config.executableDir);
        candidates.push_back((exe / libraryFileName()).string());
        candidates.push_back((exe / ".." / "Frameworks" / libraryFileName()).string());       // macOS .app
        candidates.push_back((exe / ".." / "lib" / "ekron-realms" / libraryFileName()).string()); // .deb
    }
    // Bare name: LD_LIBRARY_PATH / DYLD paths / Android's app native lib dir.
    candidates.emplace_back(libraryFileName());

    std::string errors;
    for (const auto &path : candidates) {
        const bool bare = path.find('/') == std::string::npos && path.find('\\') == std::string::npos;
        if (!bare) {
            std::error_code ec;
            if (!fs::exists(path, ec)) continue;
        }
        std::string err;
        void *lib = openLibrary(path, err);
        if (!lib) {
            errors += path + ": " + err + "; ";
            continue;
        }
        auto getApi = reinterpret_cast<RealmsGetApiFn>(findSymbol(lib, REALMS_PASCAL_ENTRY_SYMBOL));
        if (!getApi) {
            errors += path + ": missing " REALMS_PASCAL_ENTRY_SYMBOL "; ";
            continue;
        }
        const RealmsCoreApi *api = getApi(REALMS_BRIDGE_VERSION);
        if (!api || api->version != REALMS_BRIDGE_VERSION || api->struct_size < sizeof(RealmsCoreApi)) {
            errors += path + ": incompatible bridge version; ";
            continue;
        }
        origin_ = path;
        return api;
    }
    pascalError_ = errors.empty() ? std::string(libraryFileName()) + " not found" : errors;
    return nullptr;
#endif
}

bool Engine::init(EngineConfig config) {
    shutdown();
    bool expected = false;
    if (!gEngineActive.compare_exchange_strong(expected, true)) {
        if (config.printSink) config.printSink(REALMS_PRINT_ERROR, "Realms: another Engine is already running");
        return false;
    }

    config_ = std::move(config);
    if (config_.consoleCapacity == 0) config_.consoleCapacity = 1;
    msecAccumulator_ = 0.0;

    const BackendPreference pref = envBackend(config_.backend);
    const RealmsCoreApi *api = nullptr;
    pascal_ = false;
    origin_.clear();
    if (pref != BackendPreference::Native) {
        api = loadPascalCore(config_);
        pascal_ = api != nullptr;
        if (!api && pref == BackendPreference::Pascal) {
            print("Realms: Pascal core required but unavailable: " + pascalError_, REALMS_PRINT_ERROR);
            gEngineActive = false;
            return false;
        }
    }
    if (!api) {
        api = realms_native_get_api(REALMS_BRIDGE_VERSION);
        origin_ = "built-in";
    }

    host_ = RealmsHostApi{};
    host_.struct_size = sizeof(RealmsHostApi);
    host_.version = REALMS_BRIDGE_VERSION;
    host_.user = this;
    host_.print = &Engine::hostPrint;
    host_.read_file = &Engine::hostReadFile;
    host_.free_file = &Engine::hostFreeFile;
    host_.read_file_range = &Engine::hostReadRange;
    host_.write_file = &Engine::hostWriteFile;
    host_.command = &Engine::hostCommand;
    host_.milliseconds = &Engine::hostMilliseconds;

    core_ = api;
    if (core_->init(&host_) != 1) {
        print("Realms: core initialisation failed", REALMS_PRINT_ERROR);
        core_ = nullptr;
        gEngineActive = false;
        return false;
    }
    if (!pascal_ && pref == BackendPreference::Auto && !pascalError_.empty()) {
        print("Realms: Pascal core not loaded (" + pascalError_ + "), using C++ reference core",
              REALMS_PRINT_DEVELOPER);
    }
    registerCvar("realms_version", kEngineVersion, REALMS_CVAR_NOSET);
    return true;
}

void Engine::shutdown() {
    if (!core_) return;
    for (const auto &entry : commands_) core_->cmd_unregister(entry.first.c_str());
    commands_.clear();
    core_->shutdown();
    core_ = nullptr;
    pascal_ = false;
    gEngineActive = false;
}

std::string Engine::backendName() const {
    return core_ ? std::string(core_->backend_name()) : std::string("offline");
}

// ------------------------------------------------------------------ cvars

bool Engine::registerCvar(const std::string &name, const std::string &defaultValue, int32_t flags) {
    return core_ && core_->cvar_register(name.c_str(), defaultValue.c_str(), flags) == 1;
}

int32_t Engine::setCvar(const std::string &name, const std::string &value) {
    return core_ ? core_->cvar_set(name.c_str(), value.c_str()) : REALMS_CVAR_SET_UNKNOWN;
}

int32_t Engine::forceSetCvar(const std::string &name, const std::string &value) {
    return core_ ? core_->cvar_force_set(name.c_str(), value.c_str()) : REALMS_CVAR_SET_UNKNOWN;
}

bool Engine::hasCvar(const std::string &name) const {
    return core_ && core_->cvar_get(name.c_str(), nullptr, 0) >= 0;
}

std::string Engine::cvarString(const std::string &name, const std::string &fallback) const {
    if (!core_) return fallback;
    char small[256];
    const int32_t len = core_->cvar_get(name.c_str(), small, int32_t(sizeof(small)));
    if (len < 0) return fallback;
    if (len < int32_t(sizeof(small))) return std::string(small, size_t(len));
    std::string big(size_t(len) + 1, '\0');
    core_->cvar_get(name.c_str(), big.data(), int32_t(big.size()));
    big.resize(size_t(len));
    return big;
}

float Engine::cvarFloat(const std::string &name, float fallback) const {
    if (!hasCvar(name)) return fallback;
    const float v = core_->cvar_value(name.c_str());
    return std::isfinite(v) ? v : fallback;
}

int Engine::cvarInt(const std::string &name, int fallback) const {
    return hasCvar(name) ? int(core_->cvar_value(name.c_str())) : fallback;
}

bool Engine::cvarBool(const std::string &name, bool fallback) const {
    return hasCvar(name) ? core_->cvar_value(name.c_str()) != 0.0f : fallback;
}

int32_t Engine::cvarFlags(const std::string &name) const {
    return core_ ? core_->cvar_flags(name.c_str()) : -1;
}

bool Engine::cvarModified(const std::string &name, bool clear) {
    return core_ && core_->cvar_modified(name.c_str(), clear ? 1 : 0) == 1;
}

void Engine::forEachCvar(const std::function<void(const std::string &, const std::string &, int32_t)> &fn) const {
    if (!core_ || !fn) return;
    struct Ctx { const std::function<void(const std::string &, const std::string &, int32_t)> *fn; };
    Ctx ctx{&fn};
    core_->cvar_foreach(
        [](void *user, const char *name, const char *value, int32_t flags) {
            auto *c = static_cast<Ctx *>(user);
            (*c->fn)(name ? name : "", value ? value : "", flags);
        },
        &ctx);
}

// --------------------------------------------------------------- commands

bool Engine::addCommand(const std::string &name, CommandFn fn) {
    if (!core_ || !fn) return false;
    if (core_->cmd_register(name.c_str()) != 1) return false;
    commands_[lower(name)] = std::move(fn);
    return true;
}

void Engine::removeCommand(const std::string &name) {
    if (!core_) return;
    const auto it = commands_.find(lower(name));
    if (it == commands_.end()) return;
    core_->cmd_unregister(name.c_str());
    commands_.erase(it);
}

bool Engine::commandExists(const std::string &name) const {
    return core_ && core_->cmd_exists(name.c_str()) == 1;
}

void Engine::addText(const std::string &text) {
    if (core_) core_->cbuf_add_text(text.c_str());
}

void Engine::insertText(const std::string &text) {
    if (core_) core_->cbuf_insert_text(text.c_str());
}

void Engine::executeBuffer() {
    if (core_) core_->cbuf_execute();
}

void Engine::executeNow(const std::string &line) {
    if (core_) core_->cmd_execute_string(line.c_str());
}

std::string Engine::commandLineScript(const std::vector<std::string> &args) {
    std::string script, line;
    bool active = false;
    for (const auto &arg : args) {
        if (!arg.empty() && arg[0] == '+' && arg.size() > 1) {
            if (active && !line.empty()) script += line + "\n";
            line = arg.substr(1);
            active = true;
        } else if (active) {
            const bool quote = arg.find_first_of(" \t") != std::string::npos;
            line += quote ? " \"" + arg + "\"" : " " + arg;
        }
    }
    if (active && !line.empty()) script += line + "\n";
    return script;
}

// ------------------------------------------------------------- filesystem

int32_t Engine::addPak(const std::string &path) {
    return core_ ? core_->fs_add_pak(path.c_str()) : -1;
}

bool Engine::loadFile(const std::string &path, std::vector<unsigned char> &out) const {
    out.clear();
    if (!core_) return false;
    void *data = nullptr;
    int32_t size = 0;
    if (core_->fs_load(path.c_str(), &data, &size) != 1) return false;
    if (data && size > 0) out.assign(static_cast<unsigned char *>(data), static_cast<unsigned char *>(data) + size);
    if (data) core_->fs_free(data);
    return true;
}

int32_t Engine::pakCount() const {
    return core_ ? core_->fs_pak_count() : 0;
}

// -------------------------------------------------------- collision model

namespace {

TraceResult toTraceResult(const RealmsTrace &t) {
    TraceResult r;
    r.allsolid = t.allsolid != 0;
    r.startsolid = t.startsolid != 0;
    r.fraction = t.fraction;
    for (int i = 0; i < 3; ++i) {
        r.endpos[size_t(i)] = t.endpos[i];
        r.normal[size_t(i)] = t.plane_normal[i];
    }
    r.planeDist = t.plane_dist;
    r.contents = t.contents;
    r.surfaceFlags = t.surface_flags;
    r.surfaceValue = t.surface_value;
    const char *nameEnd = std::find(t.surface_name, t.surface_name + sizeof(t.surface_name), '\0');
    r.surface.assign(t.surface_name, nameEnd);
    return r;
}

} // namespace

bool Engine::loadMap(const std::string &path) {
    return core_ && !path.empty() && core_->map_load(path.c_str()) == 1;
}

void Engine::unloadMap() {
    if (core_) core_->map_load("");
}

bool Engine::mapLoaded() const {
    RealmsMapInfo info{};
    return mapInfo(info);
}

bool Engine::mapInfo(RealmsMapInfo &out) const {
    out = RealmsMapInfo{};
    out.struct_size = sizeof(RealmsMapInfo);
    return core_ && core_->map_info(&out) == 1;
}

std::string Engine::mapEntities() const {
    if (!core_) return {};
    const int32_t len = core_->map_entities(nullptr, 0);
    if (len <= 0) return {};
    std::string text(size_t(len) + 1, '\0');
    core_->map_entities(&text[0], int32_t(text.size()));
    text.resize(size_t(len));
    return text;
}

bool Engine::mapModel(int32_t index, MapModel &out) const {
    out = MapModel{};
    return core_ && core_->map_model(index, out.mins.data(), out.maxs.data(), out.origin.data(), &out.headnode) == 1;
}

int32_t Engine::headnodeForBox(const Vec3 &mins, const Vec3 &maxs) {
    return core_ ? core_->map_headnode_for_box(mins.data(), maxs.data()) : 0;
}

int32_t Engine::pointContents(const Vec3 &point, int32_t headnode) const {
    return core_ ? core_->map_point_contents(point.data(), headnode, nullptr, nullptr) : 0;
}

int32_t Engine::transformedPointContents(const Vec3 &point, int32_t headnode, const Vec3 &origin,
                                         const Vec3 &angles) const {
    return core_ ? core_->map_point_contents(point.data(), headnode, origin.data(), angles.data()) : 0;
}

TraceResult Engine::boxTrace(const Vec3 &start, const Vec3 &end, const Vec3 &mins, const Vec3 &maxs,
                             int32_t headnode, int32_t brushmask) const {
    if (!core_) {
        TraceResult r;
        r.endpos = end;
        return r;
    }
    RealmsTrace t{};
    core_->map_box_trace(start.data(), end.data(), mins.data(), maxs.data(), headnode, brushmask, nullptr, nullptr, &t);
    return toTraceResult(t);
}

TraceResult Engine::transformedBoxTrace(const Vec3 &start, const Vec3 &end, const Vec3 &mins, const Vec3 &maxs,
                                        int32_t headnode, int32_t brushmask, const Vec3 &origin,
                                        const Vec3 &angles) const {
    if (!core_) {
        TraceResult r;
        r.endpos = end;
        return r;
    }
    RealmsTrace t{};
    core_->map_box_trace(start.data(), end.data(), mins.data(), maxs.data(), headnode, brushmask, origin.data(),
                         angles.data(), &t);
    return toTraceResult(t);
}

int32_t Engine::pointLeaf(const Vec3 &point) const {
    return core_ ? core_->map_point_leaf(point.data()) : 0;
}

bool Engine::leafInfo(int32_t leaf, int32_t &contents, int32_t &cluster, int32_t &area) const {
    contents = cluster = area = 0;
    return core_ && core_->map_leaf_info(leaf, &contents, &cluster, &area) == 1;
}

// ------------------------------------------------------------------ frame

void Engine::frame(double seconds) {
    if (!core_) return;
    if (!(seconds > 0.0) || !std::isfinite(seconds)) seconds = 0.0;
    msecAccumulator_ += std::min(seconds, 0.25) * 1000.0;
    const auto msec = int32_t(msecAccumulator_);
    msecAccumulator_ -= msec;
    core_->frame(msec);
}

// ---------------------------------------------------------------- console

void Engine::clearConsole() {
    console_.clear();
    ++consoleRevision_;
}

void Engine::print(const std::string &text, int level) {
    // Split multi-line output so the console shows one entry per line.
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
        if (start == text.size()) break;
    }
    for (auto &line : lines) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        console_.push_back({level, line});
        while (console_.size() > config_.consoleCapacity) console_.pop_front();
        ++consoleRevision_;
        if (config_.printSink) config_.printSink(level, line);
    }
}

// ------------------------------------------------------------ host bridge

void Engine::hostPrint(void *user, int32_t level, const char *text) {
    static_cast<Engine *>(user)->print(text ? text : "", level);
}

bool Engine::readHostFile(const std::string &rawPath, std::vector<unsigned char> &out) const {
    const std::string path = normalizeSlashes(rawPath);
    if (config_.readFile) return config_.readFile(path, out);
    if (!safeRelative(path)) {
        // Absolute paths are only honoured for explicit host-side mounts.
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    std::vector<std::string> roots = config_.searchRoots;
    if (!config_.writableRoot.empty()) roots.insert(roots.begin(), config_.writableRoot);
    for (const auto &root : roots) {
        std::ifstream f(fs::path(root) / path, std::ios::binary);
        if (!f) continue;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    return false;
}

int32_t Engine::hostReadFile(void *user, const char *path, void **outData, int32_t *outSize) {
    if (!path || !outData || !outSize) return 0;
    *outData = nullptr;
    *outSize = 0;
    std::vector<unsigned char> bytes;
    if (!static_cast<Engine *>(user)->readHostFile(path, bytes)) return 0;
    if (bytes.size() > size_t(INT32_MAX)) return 0;
    void *mem = std::malloc(bytes.empty() ? 1 : bytes.size());
    if (!mem) return 0;
    if (!bytes.empty()) std::memcpy(mem, bytes.data(), bytes.size());
    *outData = mem;
    *outSize = int32_t(bytes.size());
    return 1;
}

void Engine::hostFreeFile(void *, void *data) { std::free(data); }

int32_t Engine::hostReadRange(void *user, const char *rawPath, int64_t offset, int32_t size, void *out) {
    auto *self = static_cast<Engine *>(user);
    if (!rawPath || !out || size < 0 || offset < 0) return -1;
    const std::string path = normalizeSlashes(rawPath);
    if (self->config_.readRange) return self->config_.readRange(path, offset, size, out);
    if (self->config_.readFile) {
        std::vector<unsigned char> bytes;
        if (!self->config_.readFile(path, bytes) || uint64_t(offset) > bytes.size()) return -1;
        const size_t n = std::min(size_t(size), bytes.size() - size_t(offset));
        std::memcpy(out, bytes.data() + offset, n);
        return int32_t(n);
    }
    std::vector<std::string> roots;
    if (!safeRelative(path)) roots.emplace_back();
    else {
        if (!self->config_.writableRoot.empty()) roots.push_back(self->config_.writableRoot);
        roots.insert(roots.end(), self->config_.searchRoots.begin(), self->config_.searchRoots.end());
    }
    for (const auto &root : roots) {
        std::ifstream f(root.empty() ? fs::path(path) : fs::path(root) / path, std::ios::binary);
        if (!f) continue;
        f.seekg(offset, std::ios::beg);
        if (!f) return -1;
        f.read(static_cast<char *>(out), size);
        return int32_t(f.gcount());
    }
    return -1;
}

int32_t Engine::hostWriteFile(void *user, const char *rawPath, const void *data, int32_t size) {
    auto *self = static_cast<Engine *>(user);
    if (!rawPath || size < 0 || self->config_.writableRoot.empty()) return 0;
    const std::string path = normalizeSlashes(rawPath);
    if (!safeRelative(path)) return 0;
    const fs::path target = fs::path(self->config_.writableRoot) / path;
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    std::ofstream f(target, std::ios::binary | std::ios::trunc);
    if (!f) return 0;
    if (size > 0) f.write(static_cast<const char *>(data), size);
    return f ? 1 : 0;
}

void Engine::hostCommand(void *user, int32_t argc, const char *const *argv) {
    auto *self = static_cast<Engine *>(user);
    if (argc <= 0 || !argv || !argv[0]) return;
    const auto it = self->commands_.find(lower(argv[0]));
    if (it == self->commands_.end()) return;
    CommandArgs args;
    args.reserve(size_t(argc));
    for (int32_t i = 0; i < argc; ++i) args.emplace_back(argv[i] ? argv[i] : "");
    const CommandFn fn = it->second; // copy: the handler may remove itself
    fn(args);
}

double Engine::hostMilliseconds(void *) {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace realms
