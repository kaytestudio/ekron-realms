#define SDL_MAIN_HANDLED
#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#else
#include <SDL.h>
#endif

#include "CityGraffiti.h"
#include "DebugPlayer.h"
#include "DesktopAudio.h"
#include "DesktopConsole.h"
#include "DesktopMapMode.h"
#include "DesktopRenderer.h"
#include "ekron_sandbox.h"
#include "DesktopHudLayout.h"
#include "DesktopOnlineClient.h"
#include "FpvDrone.h"
#include "ShopCatalog.h"
#include "SoundSnapshot.h"
#include "SoundSystem.h"
#include "DesktopGameSound.h"
#include "GravityBeam.h"
#include "Interaction.h"
#include "LevelPlacement.h"
#include "Localization.h"
#include "ObjectDynamics.h"
#include "ObjectMesh.h"
#include "PlayerPhysics.h"
#include "RaycastPlacement.h"
#include "SandboxWorld.h"
#include "ToolInteraction.h"
#include "WorldRecovery.h"
#include "third_party/json.hpp"
#include "realms/Engine.h"
#include "realms/GameServer.h"
#include "realms/SandboxBoot.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace i18n = ekron::i18n;
using i18n::Str;

namespace {

constexpr const char *kAppVersion = "1.0.0";

const char *platformName() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#else
    return "POSIX";
#endif
}

const char *archName() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "generic";
#endif
}

bool hasExpectedAssets(const fs::path &dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "models" / "abandoned_city_street_for_games.glb", ec) &&
           fs::is_regular_file(dir / "config" / "spawn.json", ec);
}

std::string resolveAssetRoot(const std::string &cliOverride, const char *argv0) {
    std::vector<fs::path> candidates;
    if (!cliOverride.empty()) {
        candidates.emplace_back(cliOverride);
    }
    if (const char *env = std::getenv("EKRON_ASSETS_DIR"); env && env[0] != '\0') {
        candidates.emplace_back(env);
    }
    if (char *sdlBase = SDL_GetBasePath()) {
        const fs::path base(sdlBase);
        SDL_free(sdlBase);
        candidates.push_back(base);
        candidates.push_back(base / ".." / "Resources");
        candidates.push_back(base / ".." / "share" / "ekron-realms");
        candidates.push_back(base / "assets");
    }
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        const fs::path exeDir = fs::weakly_canonical(fs::path(argv0), ec).parent_path();
        if (!exeDir.empty()) {
            candidates.push_back(exeDir);
            candidates.push_back(exeDir / ".." / "Resources");
            candidates.push_back(exeDir / ".." / "share" / "ekron-realms");
            candidates.push_back(exeDir / "sys" / "android" / "app" / "src" / "main" / "assets");
            candidates.push_back(exeDir / ".." / "sys" / "android" / "app" / "src" / "main" / "assets");
        }
    }
    candidates.emplace_back("sys/android/app/src/main/assets");
    candidates.emplace_back("../sys/android/app/src/main/assets");
    candidates.emplace_back("/usr/share/ekron-realms");
    candidates.emplace_back("/usr/local/share/ekron-realms");

    for (const auto &c : candidates) {
        if (hasExpectedAssets(c)) {
            std::error_code ec;
            return fs::weakly_canonical(c, ec).string();
        }
    }
    return {};
}

std::string resolveSavePath() {
    if (char *pref = SDL_GetPrefPath("KayteStudio", "EkronRealms")) {
        fs::path dir(pref);
        SDL_free(pref);
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "city-glb-v1.txt").string();
    }
    if (const char *home = std::getenv("HOME"); home && home[0] != '\0') {
        fs::path dir = fs::path(home) / ".local" / "share" / "ekron-realms";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "city-glb-v1.txt").string();
    }
    return (fs::temp_directory_path() / "ekron-city-glb-v1.txt").string();
}

// Persistent store wallet + ownership (weapon skins / unlocks). Lives next to
// the city save so it survives across sessions.
std::string resolveStorePath() {
    if (char *pref = SDL_GetPrefPath("KayteStudio", "EkronRealms")) {
        fs::path dir(pref);
        SDL_free(pref);
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "store.json").string();
    }
    if (const char *home = std::getenv("HOME"); home && home[0] != '\0') {
        fs::path dir = fs::path(home) / ".local" / "share" / "ekron-realms";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "store.json").string();
    }
    return (fs::temp_directory_path() / "ekron-store.json").string();
}

// Desktop preferences (currently the UI language chosen in the pause menu).
std::string resolveSettingsPath() {
    if (char *pref = SDL_GetPrefPath("KayteStudio", "EkronRealms")) {
        fs::path dir(pref);
        SDL_free(pref);
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "settings.json").string();
    }
    if (const char *home = std::getenv("HOME"); home && home[0] != '\0') {
        fs::path dir = fs::path(home) / ".local" / "share" / "ekron-realms";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "settings.json").string();
    }
    return (fs::temp_directory_path() / "ekron-settings.json").string();
}

std::optional<i18n::Language> loadSavedLanguage(const std::string &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() <= 0 || in.tellg() > 65536) return std::nullopt;
    in.seekg(0, std::ios::beg);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("language") || !j["language"].is_string()) return std::nullopt;
    return i18n::parseLanguageTag(j["language"].get<std::string>());
}

void saveLanguage(const std::string &path, i18n::Language language) {
    nlohmann::json j = nlohmann::json::object();
    {
        // Preserve unrelated keys written by newer versions.
        std::ifstream in(path);
        if (in) {
            auto existing = nlohmann::json::parse(in, nullptr, false);
            if (!existing.is_discarded() && existing.is_object()) j = std::move(existing);
        }
    }
    j["language"] = i18n::languageCode(language);
    std::ofstream out(path, std::ios::trunc);
    if (out) out << j.dump(2) << '\n';
}

// First supported language in the OS preferred-locale list.
std::optional<i18n::Language> systemLanguage() {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_Locale *locales = SDL_GetPreferredLocales()) {
        std::optional<i18n::Language> found;
        for (const SDL_Locale *l = locales; l->language != nullptr; ++l) {
            if ((found = i18n::parseLanguageTag(l->language))) break;
        }
        SDL_free(locales);
        if (found) return found;
    }
#endif
    return i18n::languageFromEnvironment();
}

city::SpawnConfig loadSpawnConfig(const std::string &assetRoot) {
    city::SpawnConfig cfg;
    std::ifstream f(assetRoot + "/config/spawn.json", std::ios::binary | std::ios::ate);
    if (!f || f.tellg() <= 0 || f.tellg() > 65536) return cfg;
    f.seekg(0, std::ios::beg);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string err;
    city::parseSpawnConfig(text, cfg, err);
    return cfg;
}

// ---------------------------------------------------------------------------
// Realms Engine (Quake 2 stack: Free Pascal core <-> C++ bridge) integration
// ---------------------------------------------------------------------------

std::string executableDir(const char *argv0) {
    if (char *base = SDL_GetBasePath()) {
        std::string dir(base);
        SDL_free(base);
        return dir;
    }
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        return fs::weakly_canonical(fs::path(argv0), ec).parent_path().string();
    }
    return {};
}

const char *realmsPlatformTag() {
#if defined(__APPLE__)
    return "desktop-macos";
#else
    return "desktop-linux";
#endif
}

realms::EngineConfig makeEngineConfig(const std::string &assetRoot, const std::string &writableRoot,
                                      const char *argv0) {
    realms::EngineConfig cfg;
    cfg.executableDir = executableDir(argv0);
    if (!assetRoot.empty()) cfg.searchRoots.push_back(assetRoot);
    cfg.writableRoot = writableRoot;
    cfg.printSink = [](int level, const std::string &text) {
        if (level == REALMS_PRINT_DEVELOPER && !std::getenv("REALMS_DEVELOPER")) return;
        auto &out = level == REALMS_PRINT_INFO ? std::cout : std::cerr;
        out << "[Realms] " << text << "\n";
    };
    return cfg;
}

realms::sandbox::BootOptions makeBootOptions(const realms::EngineConfig &cfg, std::string commandLine,
                                             bool execUserConfig, realms::sandbox::MapChangedFn onMapChanged = {}) {
    realms::sandbox::BootOptions boot;
    boot.onMapChanged = std::move(onMapChanged);
    boot.platform = realmsPlatformTag();
    boot.commandLine = std::move(commandLine);
    boot.execUserConfig = execUserConfig;
    std::vector<std::string> roots = cfg.searchRoots;
    if (!cfg.writableRoot.empty()) roots.insert(roots.begin(), cfg.writableRoot);
    boot.fileExists = [roots](const std::string &path) {
        std::error_code ec;
        for (const auto &root : roots)
            if (fs::is_regular_file(fs::path(root) / path, ec)) return true;
        return false;
    };
    return boot;
}

int printEngineInfo(const std::string &assetRoot, const char *argv0) {
    realms::Engine engine;
    realms::EngineConfig cfg = makeEngineConfig(assetRoot, {}, argv0);
    cfg.printSink = nullptr;
    if (!engine.init(cfg)) {
        std::cerr << "Realms engine failed to start: "
                  << (engine.pascalLoadError().empty() ? "core init failed" : engine.pascalLoadError()) << "\n";
        return 1;
    }
    realms::sandbox::boot(engine, makeBootOptions(cfg, {}, false));
    std::cout << realms::kEngineName << " " << realms::kEngineVersion << "\n"
              << "Core:     " << engine.backendName() << (engine.pascalBackend() ? " (Free Pascal)" : " (C++)") << "\n"
              << "Origin:   " << engine.backendOrigin() << "\n"
              << "Platform: " << engine.cvarString("realms_platform") << "\n"
              << "Assets:   " << (assetRoot.empty() ? "<not found>" : assetRoot) << "\n";
    if (!engine.pascalBackend() && !engine.pascalLoadError().empty())
        std::cout << "Pascal:   " << engine.pascalLoadError() << "\n";
    // The Quake 2 game module realms::GameServer runs BSP maps with.
    std::string gameWhere, gameWhy;
    if (realms::GameServer::moduleAvailable(cfg.executableDir, &gameWhere, &gameWhy))
        std::cout << "Game:     " << gameWhere << " (Quake 2 game API " << Q2_GAME_API_VERSION << ")\n";
    else
        std::cout << "Game:     none (" << gameWhy << ")\n";
    return 0;
}

std::array<float, 16> computeCarMatrix(const sandbox::Object &o,
                                       const ekron::desktop::DesktopGltfModel &carModel,
                                       bool held,
                                       physics::Vec3 heldDisplay) {
    auto b = sandbox::World::bounds(o);
    physics::Vec3 target{b.x, b.y, b.z};
    if (held) target = heldDisplay;
    const auto &lo = carModel.scene().minimum;
    const auto &hi = carModel.scene().maximum;
    physics::Vec3 source{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};

    const float ax = o.tiltX;
    const float az = o.tiltZ;
    const float ay = float(o.rotation) * 1.57079632679f;
    auto rotate = [&](physics::Vec3 p) {
        float x = std::cos(ay) * p.x + std::sin(ay) * p.z;
        float z = -std::sin(ay) * p.x + std::cos(ay) * p.z;
        float y = std::cos(ax) * p.y - std::sin(ax) * z;
        z = std::sin(ax) * p.y + std::cos(ax) * z;
        return physics::Vec3{std::cos(az) * x - std::sin(az) * y, std::sin(az) * x + std::cos(az) * y, z};
    };
    const auto x = rotate({1, 0, 0});
    const auto y = rotate({0, 1, 0});
    const auto z = rotate({0, 0, 1});
    const auto t = target - rotate(source);
    return {
        x.x, x.y, x.z, 0.0f,
        y.x, y.y, y.z, 0.0f,
        z.x, z.y, z.z, 0.0f,
        t.x, t.y, t.z, 1.0f
    };
}

std::array<float, 16> computeDroneMatrix(const sandbox::FpvDrone &drone,
                                         const ekron::desktop::DesktopGltfModel &droneModel) {
    const auto &lo = droneModel.scene().minimum;
    const auto &hi = droneModel.scene().maximum;
    const physics::Vec3 center{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    auto rotate = [&](physics::Vec3 p) {
        const float cr = std::cos(drone.roll), sr = std::sin(drone.roll);
        const float cp = std::cos(drone.pitch), sp = std::sin(drone.pitch);
        const float cy = std::cos(drone.yaw), sy = std::sin(drone.yaw);
        float x = cr * p.x + sr * p.y;
        float y = -sr * p.x + cr * p.y;
        float z = sp * y + cp * p.z;
        y = cp * y - sp * p.z;
        return physics::Vec3{cy * x + sy * z, y, -sy * x + cy * z};
    };
    const auto x = rotate({1, 0, 0});
    const auto y = rotate({0, 1, 0});
    const auto z = rotate({0, 0, 1});
    const auto t = drone.position - rotate(center);
    return {
        x.x, x.y, x.z, 0.0f,
        y.x, y.y, y.z, 0.0f,
        z.x, z.y, z.z, 0.0f,
        t.x, t.y, t.z, 1.0f
    };
}

int runHeadlessSmoke(const std::string &assetRoot, const char *argv0) {
    if (assetRoot.empty()) {
        std::cerr << "[Ekron] Headless smoke failed: assets directory not found\n";
        return 1;
    }

    // Realms Engine: core must start, run the shared config and its aliases.
    realms::Engine engine;
    const realms::EngineConfig engineCfg = makeEngineConfig(assetRoot, {}, argv0);
    if (!engine.init(engineCfg)) {
        std::cerr << "[Ekron] Headless smoke failed: Realms engine did not start ("
                  << engine.pascalLoadError() << ")\n";
        return 1;
    }
    realms::sandbox::boot(engine, makeBootOptions(engineCfg, {}, false));
    if (engine.cvarString("sv_hostname") != "Ekron Realms") {
        std::cerr << "[Ekron] Headless smoke failed: config/realms.cfg was not executed by the Realms core\n";
        return 1;
    }
    int smokeCalls = 0;
    engine.addCommand("smoke_probe", [&](const realms::CommandArgs &args) { smokeCalls += int(args.size()); });
    engine.exec("alias probe \"smoke_probe a b\"; probe; set sensitivity 4.5\n");
    if (smokeCalls != 3 || std::fabs(realms::sandbox::lookScale(engine) - 1.5f) > 1e-4f) {
        std::cerr << "[Ekron] Headless smoke failed: Realms command bridge round trip\n";
        return 1;
    }

    const city::SpawnConfig spawn = loadSpawnConfig(assetRoot);
    if (!spawn.enabled) {
        std::cerr << "[Ekron] Headless smoke failed: invalid config/spawn.json in " << assetRoot << "\n";
        return 1;
    }

    std::string log;
    ekron::desktop::DesktopGltfModel cityModel;
    if (!cityModel.loadFromDisk(assetRoot, "models/abandoned_city_street_for_games.glb", log)) {
        std::cerr << "[Ekron] Headless smoke failed loading city GLB: " << log << "\n";
        return 1;
    }

    city::LoadOptions carOpts;
    carOpts.level = false;
    carOpts.buildPhysics = false;
    carOpts.targetExtent = 4.025f;
    carOpts.yaw = 1.57079632679f;
    ekron::desktop::DesktopGltfModel carModel;
    if (!carModel.loadFromDisk(assetRoot, "models/uaz_469_1973_classic.glb", log, carOpts)) {
        std::cerr << "[Ekron] Headless smoke failed loading UAZ-469 GLB: " << log << "\n";
        return 1;
    }

    city::LoadOptions remoteOpts;
    remoteOpts.level = false;
    remoteOpts.buildPhysics = false;
    remoteOpts.targetExtent = 0.38f;
    ekron::desktop::DesktopGltfModel remoteModel;
    if (!remoteModel.loadFromDisk(assetRoot, "models/chasing_m2_rc_underwater_drone_remote.glb", log, remoteOpts)) {
        std::cerr << "[Ekron] Headless smoke failed loading FPV remote GLB: " << log << "\n";
        return 1;
    }

    remoteOpts.targetExtent = 0.50f;
    ekron::desktop::DesktopGltfModel droneModel;
    if (!droneModel.loadFromDisk(assetRoot, "models/fpv_drone.glb", log, remoteOpts)) {
        std::cerr << "[Ekron] Headless smoke failed loading FPV drone GLB: " << log << "\n";
        return 1;
    }

    for (const char *texFile : {"textures/asphalt.png", "textures/concrete.png", "textures/brick.png",
                                "textures/rust.png", "textures/wood.png", "textures/fabric.png", "textures/glass.png"}) {
        std::ifstream f(assetRoot + "/" + texFile, std::ios::binary | std::ios::ate);
        if (!f || f.tellg() <= 0) {
            std::cerr << "[Ekron] Missing texture: " << texFile << "\n";
            return 1;
        }
        const auto sz = static_cast<size_t>(f.tellg());
        std::vector<unsigned char> png(sz);
        f.seekg(0, std::ios::beg);
        f.read(reinterpret_cast<char *>(png.data()), static_cast<std::streamsize>(sz));
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;
        if (!ekron::desktop::decodeImageBytes(png.data(), png.size(), w, h, rgba) || w <= 0 || h <= 0) {
            std::cerr << "[Ekron] Failed decoding texture " << texFile << "\n";
            return 1;
        }
    }

    sandbox::World world;
    world.scenery.clear();
    world.walls = cityModel.scene().colliders;
    world.wallSegments = cityModel.scene().wallSegments;
    world.x = spawn.x;
    world.y = spawn.y;
    world.z = spawn.z;
    world.yaw = spawn.yawDegrees * 0.01745329252f;
    world.pitch = 0.0f;

    physics::PlayerController player;
    player.reset(world.x, world.y, world.z);
    physics::CollisionMesh builtCollision;
    sandbox::ObjectDynamics dynamics;
    sandbox::FpvDrone drone;

    // Place one test object and simulate 15 physics + player + drone frames
    sandbox::Object tableObj{4, 0, world.x + 1.5f, world.z - 2.5f};
    tableObj.y = world.y - 1.0f;
    tableObj.surface = sandbox::Wood;
    world.objects.push_back(tableObj);
    for (const auto &obj : world.objects) {
        sandbox::addObjectCollision(builtCollision, obj);
    }
    builtCollision.build();

    int held = -1;
    int seated = -1;
    for (int step = 0; step < 15; ++step) {
        dynamics.update(world, *cityModel.scene().collision, 1.0f / 60.0f, held,
                        cityModel.scene().minimum, cityModel.scene().maximum, &held, &seated);
        player.update(*cityModel.scene().collision, 0.0f, 0.25f, world.yaw, 1.0f / 60.0f,
                      false, world.x, world.y, world.z, &builtCollision, false,
                      cityModel.scene().fenceBarriers.get());
        drone.update(1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, *cityModel.scene().collision, builtCollision);
    }

    // Verify save/load round-trip
    const fs::path tmpSave = fs::temp_directory_path() / "ekron-desktop-smoke-save.txt";
    if (!world.save(tmpSave.string())) {
        std::cerr << "[Ekron] Headless smoke failed: world.save() returned false\n";
        return 1;
    }
    sandbox::World reloaded;
    if (!reloaded.load(tmpSave.string()) || reloaded.objects.size() != world.objects.size()) {
        std::cerr << "[Ekron] Headless smoke failed: world.load() mismatch\n";
        return 1;
    }
    std::error_code ec;
    fs::remove(tmpSave, ec);

    std::cout << "[Ekron] Headless smoke OK (" << platformName() << " " << archName() << "): "
              << "assets='" << assetRoot << "', "
              << "cityBatches=" << cityModel.scene().batches.size() << ", "
              << "colliders=" << cityModel.scene().colliders.size() << ", "
              << "uazBatches=" << carModel.scene().batches.size() << ", "
              << "droneBatches=" << droneModel.scene().batches.size() << ", "
              << "realmsCore=" << engine.backendName() << "\n";
    return 0;
}

struct CatalogEntry {
    int itemType; // -1 for FPV drone action
    Str title;
    Str subtitle;
    const char *iconFile;
};

struct CatalogCategory {
    Str name;
    std::vector<CatalogEntry> items;
};

const std::vector<CatalogCategory> &catalogCategories() {
    static const std::vector<CatalogCategory> kCats = {
        {Str::CatalogMaterials, {
            {9,  Str::ItemBoards,    Str::ItemBoardsSub,    "boards.png"},
            {10, Str::ItemBrick,     Str::ItemBrickSub,     "brick_pack.png"},
            {11, Str::ItemConcrete,  Str::ItemConcreteSub,  "concrete_block.png"},
            {12, Str::ItemFoamBlock, Str::ItemFoamBlockSub, "foam_block.png"},
        }},
        {Str::CatalogConstruction, {
            {0,  Str::ItemWall,      Str::ItemWallSub,      "wall.png"},
            {1,  Str::ItemFloor,     Str::ItemFloorSub,     "floor.png"},
            {2,  Str::ItemWindow,    Str::ItemWindowSub,    "window.png"},
            {3,  Str::ItemDoor,      Str::ItemDoorSub,      "door.png"},
        }},
        {Str::CatalogFurniture, {
            {4,  Str::ItemTable,     Str::ItemTableSub,     "table.png"},
            {5,  Str::ItemChair,     Str::ItemChairSub,     "chair.png"},
            {6,  Str::ItemCabinet,   Str::ItemCabinetSub,   "cabinet.png"},
            {7,  Str::ItemSofa,      Str::ItemSofaSub,      "sofa.png"},
        }},
        {Str::CatalogEquipment, {
            {8,  Str::ItemUaz,       Str::ItemUazSub,       "uaz.png"},
            {-1, Str::ItemFpvDrone,  Str::ItemFpvDroneSub,  "fpv_drone.png"},
        }}
    };
    return kCats;
}

const char *itemTitleForId(int id) {
    for (const auto &cat : catalogCategories()) {
        for (const auto &it : cat.items) {
            if (it.itemType == id) return i18n::tr(it.title);
        }
    }
    return i18n::tr(Str::ItemObject);
}

// Pause menu geometry, shared by rendering and hit-testing.
constexpr int kPauseMenuButtons = 8;
constexpr float kPauseMenuWidth = 380.0f;
constexpr float kPauseMenuHeight = 62.0f + float(kPauseMenuButtons) * 42.0f; // 398

// Shrink a label's scale so translated text never overflows its box.
float fitText(std::string_view text, float preferredScale, float availableWidth) {
    return ekron::desktop::fitHudTextScale(preferredScale, ekron::font::measureTextWidth(text, preferredScale),
                                           availableWidth);
}

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

void printHelp(const char *exe) {
    std::cout
        << "Ekron Realms Desktop " << kAppVersion << " (" << platformName() << " " << archName() << ")\n"
        << "Usage: " << exe << " [options]\n\n"
        << "Options:\n"
        << "  --version           Print version and exit\n"
        << "  --headless-smoke    Verify assets, GLB models, textures, physics, and save/load without GUI\n"
        << "  --assets <dir>      Override path to Ekron assets directory\n"
        << "  --online-host <name>  Connect to experimental TLS presence server\n"
        << "  --online-port <port>  Server port (default 38475)\n"
        << "  --online-name <name>  Player name (default Player)\n"
        << "                      Set EKRON_JOIN_TOKEN in the environment; players only, no shared world\n"
        << "  --lang <code>       UI language: en, ru or pt-PT (default: saved choice, then system locale)\n"
        << "                      The EKRON_LANG environment variable is honoured too\n"
        << "  --engine-info       Show which Realms core is active (Free Pascal or C++) and exit\n"
        << "  +<command> [args]   Quake 2 style console commands, e.g. +set sensitivity 4 +exec my.cfg\n"
        << "                      REALMS_CORE=pascal|native|auto selects the Realms core\n"
        << "  --help              Show this help message\n\n"
        << "Desktop Controls:\n"
        << "  WASD                Move (or FPV drone throttle/yaw; Arrows/Mouse for pitch/roll)\n"
        << "  Mouse               Look around\n"
        << "  Space / C / Z       Jump (Ascend) / Crouch (Descend) / Prone\n"
        << "  1 / 2 / 3           Gravity Gun / FPV Remote / Empty Hands\n"
        << "  Q or Tab or B       Open/close Build & Equipment Catalog\n"
        << "  G                   Open/close Store (weapon skins & gold-coin unlocks)\n"
        << "  LMB                 Grab/Drop (Gravity Gun), Place item (Build), Connect FPV\n"
        << "  RMB or F            Freeze/Unfreeze object or cancel Build mode\n"
        << "  R or MouseWheel     Rotate preview or held object 90 deg\n"
        << "  X or Delete         Remove aimed placed object\n"
        << "  E                   Interact (door, UAZ-469, sofa, FPV drone)\n"
        << "  V / H               Toggle FPV/Chase camera / Return drone home\n"
        << "  N / P / F11 / Esc   Noclip flight / Copy spawn.json / Fullscreen / Pause menu\n"
        << "  L                   Cycle UI language (English / Русский / Português)\n"
        << "  `                   Realms console (cvarlist, cmdlist, set, alias, exec, noclip, quit...)\n";
}

} // namespace

// The desktop executable's entry point. In libekron_sandbox (the C++ sandbox
// as a shared library, driven by the Free Pascal `realms-pascal` program from
// Projects/realms.lpr) the same body runs from ekron_sandbox_run() below.
#if defined(EKRON_SANDBOX_LIBRARY)
static int sandboxMain(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    std::string cliAssets;
    std::string onlineHost, onlineName = "Player";
    bool onlineNameSet = false;
    bool engineInfo = false;
    int onlinePort = 38475;
    bool headlessSmoke = false;
    std::optional<i18n::Language> cliLanguage;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << "Ekron Realms Desktop " << kAppVersion
                      << " (" << platformName() << " " << archName() << ")\n"
                      << "Realms engine " << realms::kEngineVersion << " (Quake 2 stack, Free Pascal core bridge)\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return 0;
        }
        if (arg == "--headless-smoke") {
            headlessSmoke = true;
        } else if (arg == "--engine-info") {
            engineInfo = true;
        } else if (arg == "--assets" && i + 1 < argc) {
            cliAssets = argv[++i];
        } else if (arg == "--online-host" && i + 1 < argc) {
            onlineHost = argv[++i];
        } else if (arg == "--online-name" && i + 1 < argc) {
            onlineName = argv[++i];
            onlineNameSet = true;
        } else if (arg == "--lang" && i + 1 < argc) {
            const std::string value = argv[++i];
            cliLanguage = i18n::parseLanguageTag(value);
            if (!cliLanguage) {
                std::cerr << "Unsupported language '" << value << "' (expected en, ru or pt-PT)\n";
                return 1;
            }
        } else if (arg == "--online-port" && i + 1 < argc) {
            try {
                std::string value = argv[++i]; size_t used = 0;
                onlinePort = std::stoi(value, &used);
                if (used != value.size() || onlinePort < 1 || onlinePort > 65535) throw std::invalid_argument("port");
            } catch (const std::exception &) { std::cerr << "Invalid online port\n"; return 1; }
        }
    }

    SDL_SetMainReady();
    const std::string assetRoot = resolveAssetRoot(cliAssets, argc > 0 ? argv[0] : nullptr);
    if (engineInfo) {
        return printEngineInfo(assetRoot, argc > 0 ? argv[0] : nullptr);
    }
    if (headlessSmoke) {
        return runHeadlessSmoke(assetRoot, argc > 0 ? argv[0] : nullptr);
    }

    if (assetRoot.empty()) {
        std::cerr << "[Ekron] Could not locate Ekron assets directory (models/abandoned_city_street_for_games.glb).\n"
                  << "Pass --assets <dir> or set EKRON_ASSETS_DIR.\n";
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::cerr << "[Ekron] SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // UI language: --lang > EKRON_LANG > saved pause-menu choice > OS locale > English.
    const std::string settingsPath = resolveSettingsPath();
    {
        std::optional<i18n::Language> lang = cliLanguage;
        if (!lang) {
            if (const char *env = std::getenv("EKRON_LANG"); env && env[0] != '\0') lang = i18n::parseLanguageTag(env);
        }
        if (!lang) lang = loadSavedLanguage(settingsPath);
        if (!lang) lang = systemLanguage();
        i18n::setLanguage(lang.value_or(i18n::Language::English));
    }

    // Realms Engine: Quake 2 cvars/commands/console/PAK filesystem through the
    // Free Pascal core (librealms_core) or the C++ reference core.
    realms::Engine engine;
    const realms::EngineConfig engineCfg =
        makeEngineConfig(assetRoot, fs::path(settingsPath).parent_path().string(), argc > 0 ? argv[0] : nullptr);
    if (!engine.init(engineCfg)) {
        std::cerr << "[Ekron] Realms engine failed to start: " << engine.pascalLoadError() << "\n";
        SDL_Quit();
        return 1;
    }
    engine.registerCvar("vid_fullscreen", "0", REALMS_CVAR_ARCHIVE);
    // "map" / "killserver" may run before the GL context exists (+map on the
    // command line, autoexec.cfg): the main loop applies the latest change.
    std::optional<std::string> pendingMapChange;
    {
        std::vector<std::string> args(argv + (argc > 0 ? 1 : 0), argv + argc);
        realms::sandbox::boot(engine, makeBootOptions(engineCfg, realms::Engine::commandLineScript(args), true,
                                                      [&pendingMapChange](const std::string &path) {
                                                          pendingMapChange = path;
                                                      }));
    }
    if (onlineNameSet) engine.setCvar("name", onlineName);
    ekron::desktop::DesktopConsole console;
    ekron::desktop::DesktopMapMode mapMode;
    std::vector<ekron::desktop::DesktopRenderer::EntityVertex> mapEntityVertices; // reused every frame
    std::vector<ekron::desktop::DesktopRenderer::EntityBatch> mapEntityBatches;
    std::vector<ekron::desktop::DesktopRenderer::EffectVertex> mapEffectVertices;
    std::vector<ekron::desktop::DesktopRenderer::DynamicLight> mapLights;
    // Quake 2 game sounds (.wav from the VFS) on the same mixer
    ekron::desktop::DesktopGameSound gameSound;

    // Game sound: assets/sounds (CC0, credited in LICENSE.md there), decoded
    // on a worker thread while the rest loads. No audio device = silence.
    ekron::audio::SoundSystem sound;
    sound.registerCommands(engine);
    sound.loadAsync([assetRoot](const std::string &path, std::vector<uint8_t> &out) {
        std::ifstream f(fs::path(assetRoot) / path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    });
    ekron::desktop::DesktopAudio audio;
    {
        std::string audioInfo;
        audio.open(sound.mixer(), audioInfo);
        engine.print(audioInfo);
    }
    bool lastPauseOpen = false, lastCatalogOpen = false, lastShopOpen = false;
    float sandboxYaw = 0.0f, sandboxPitch = 0.0f; // camera to restore after killserver

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        i18n::tr(Str::WindowTitle),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        std::cerr << "[Ekron] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx) {
        std::cerr << "[Ekron] SDL_GL_CreateContext (OpenGL 3.3 Core) failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, glCtx);
    SDL_GL_SetSwapInterval(1);

    ekron::gl::Api gl;
    if (!gl.load()) {
        std::cerr << "[Ekron] Failed to resolve OpenGL 3.3 Core entry points.\n";
        SDL_GL_DeleteContext(glCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    ekron::desktop::DesktopRenderer renderer;
    std::string renderErr;
    if (!renderer.init(gl, assetRoot, renderErr)) {
        std::cerr << "[Ekron] Renderer initialization failed:\n" << renderErr << "\n";
        SDL_GL_DeleteContext(glCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Load 3D GLB models & world state
    std::string loadLog;
    ekron::desktop::DesktopGltfModel cityModel;
    ekron::desktop::DesktopGltfModel carModel;
    ekron::desktop::DesktopGltfModel remoteModel;
    ekron::desktop::DesktopGltfModel droneModel;

    if (!cityModel.loadFromDisk(assetRoot, "models/abandoned_city_street_for_games.glb", loadLog)) {
        std::cerr << "[Ekron] Failed loading city GLB: " << loadLog << "\n";
        renderer.shutdown(gl);
        SDL_GL_DeleteContext(glCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    cityModel.upload(gl);

    city::LoadOptions carOpts;
    carOpts.level = false;
    carOpts.buildPhysics = false;
    carOpts.targetExtent = 4.025f;
    carOpts.yaw = 1.57079632679f;
    if (carModel.loadFromDisk(assetRoot, "models/uaz_469_1973_classic.glb", loadLog, carOpts)) {
        carModel.upload(gl);
    }

    city::LoadOptions remoteOpts;
    remoteOpts.level = false;
    remoteOpts.buildPhysics = false;
    remoteOpts.targetExtent = 0.38f;
    if (remoteModel.loadFromDisk(assetRoot, "models/chasing_m2_rc_underwater_drone_remote.glb", loadLog, remoteOpts)) {
        remoteModel.upload(gl);
    }
    remoteOpts.targetExtent = 0.50f;
    if (droneModel.loadFromDisk(assetRoot, "models/fpv_drone.glb", loadLog, remoteOpts)) {
        droneModel.upload(gl);
    }

    sandbox::World world;
    world.scenery.clear();
    world.walls = cityModel.scene().colliders;
    world.wallSegments = cityModel.scene().wallSegments;

    const std::string savePath = resolveSavePath();
    const bool loadedSave = world.load(savePath);

    // In-game store: weapon skins + gold-coin unlocks. Persisted locally and
    // synchronised with the online relay while connected.
    const std::string storePath = resolveStorePath();
    ekron::desktop::ShopState shop;
    shop.load(storePath);

    const city::SpawnConfig spawnCfg = loadSpawnConfig(assetRoot);
    sandbox::World SpawnNav = world;
    city::SpawnPoint roadAnchor{};
    bool hasRoadAnchor = false;
    bool hasCarParking = false;
    sandbox::Object carParkingTemplate{};

    if (city::findRoadSpawn(SpawnNav, cityModel.scene().minimum, cityModel.scene().maximum, -3.4f, 32.7f, roadAnchor)) {
        hasRoadAnchor = true;
        float parkX = 0.0f, parkZ = 0.0f;
        if (carModel.ready() &&
            city::findCarParking(SpawnNav, cityModel.scene().minimum, cityModel.scene().maximum,
                                 roadAnchor.x + 3.0f, roadAnchor.z - 9.0f, parkX, parkZ)) {
            const float gy = cityModel.scene().collision->groundHeight(parkX, parkZ, 2.0f, -10.0f);
            carParkingTemplate = sandbox::Object{8, 0, parkX, parkZ};
            carParkingTemplate.y = std::isfinite(gy) ? gy : 0.0f;
            hasCarParking = true;
            const bool anyCar = std::any_of(world.objects.begin(), world.objects.end(),
                                            [](const sandbox::Object &o) { return o.type == 8; });
            if (!loadedSave && !anyCar) {
                sandbox::Object initialCar{8, 0, parkX, parkZ};
                initialCar.y = carParkingTemplate.y + 0.5f;
                world.objects.push_back(initialCar);
            }
        }
    }

    physics::Vec3 spawnPos{0.0f, 1.7f, 4.0f};
    float spawnYaw = 0.0f;
    float spawnPitch = 0.0f;
    if (spawnCfg.enabled) {
        spawnPos = physics::Vec3{spawnCfg.x, spawnCfg.y, spawnCfg.z};
        spawnYaw = spawnCfg.yawDegrees * 0.01745329252f;
        spawnPitch = 0.0f;
    } else if (hasRoadAnchor) {
        const float gy = cityModel.scene().collision->groundHeight(roadAnchor.x, roadAnchor.z, 2.0f, -10.0f);
        spawnPos = physics::Vec3{roadAnchor.x, std::isfinite(gy) ? (gy + 1.7f) : 1.7f, roadAnchor.z};
        spawnYaw = 0.0f;
    }
    world.x = spawnPos.x;
    world.y = spawnPos.y;
    world.z = spawnPos.z;
    world.yaw = spawnYaw;
    world.pitch = spawnPitch;

    physics::PlayerController player;
    player.reset(world.x, world.y, world.z);
    physics::CollisionMesh builtCollision;
    sandbox::ObjectDynamics dynamics;
    sandbox::ToolInteraction tools;
    sandbox::FpvDrone drone;
    std::optional<sandbox::Object> placementPreview;

    auto rebuildBuiltCollision = [&]() {
        builtCollision = physics::CollisionMesh{};
        for (size_t i = 0; i < world.objects.size(); ++i) {
            if (int(i) == tools.held) continue;
            sandbox::addObjectCollision(builtCollision, world.objects[i]);
        }
        builtCollision.build();
    };
    rebuildBuiltCollision();

    ekron::desktop::DesktopOnlineClient online;
    if (!onlineHost.empty()) {
        const char *token = std::getenv("EKRON_JOIN_TOKEN");
        online.connect(onlineHost, onlinePort, engine.cvarString("name", onlineName), token ? token : "");
    }
    bool running = true;
    bool catalogOpen = false;
    bool pauseOpen = false;
    bool shopOpen = false;
    bool flying = spawnCfg.enabled && spawnCfg.startFlying;
    // "Start with Flight" unlock grants noclip flight on (re)spawn.
    if (shop.hasExtra(106)) flying = true;
    bool crouched = false;
    bool prone = false;
    bool building = false;
    bool fullscreen = false;
    sandbox::HandTool spawnReturnTool = sandbox::HandTool::GRAVITY_GUN;
    int selectedCategory = 1;
    int selectedShopCategory = 0;
    int seatedCar = -1;
    bool carWasAnchored = false;
    physics::Vec3 seatFrom{};
    float seatTime = 1.0f;
    bool saveDirty = false;
    float saveTimer = 0.0f;
    bool shopDirty = false;
    float shopSaveTimer = 0.0f;
    long long pendingShopToastReq = -1;
    std::string toastText = i18n::tr(Str::ToastWelcome);
    float toastTimer = 5.5f;

    auto showToast = [&](std::string msg, float secs = 2.6f) {
        toastText = std::move(msg);
        toastTimer = secs;
    };

    // "Start with Flight" unlock: grant noclip flight once on spawn/purchase.
    // Deliberately not applied every frame so the player can still toggle
    // noclip off manually from the pause menu.
    auto grantStartWithFlight = [&]() {
        if (shop.hasExtra(106) && !flying) {
            flying = true;
            showToast(i18n::tr(Str::ToastStartFlightActivated));
        }
    };

    auto syncMouseCapture = [&]() {
        const bool capture = !catalogOpen && !pauseOpen && !shopOpen && !console.isOpen();
        SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
    };
    syncMouseCapture();

    auto respawnPlayer = [&]() {
        seatedCar = -1;
        flying = (spawnCfg.enabled && spawnCfg.startFlying) || shop.hasExtra(106);
        crouched = false;
        prone = false;
        world.x = spawnPos.x;
        world.y = spawnPos.y;
        world.z = spawnPos.z;
        world.yaw = spawnYaw;
        world.pitch = spawnPitch;
        player.reset(world.x, world.y, world.z);
        showToast(i18n::tr(Str::ToastPlayerRespawned));
    };

    auto teleportToRoad = [&]() {
        sandbox::World nav;
        nav.walls = world.walls;
        nav.wallSegments = world.wallSegments;
        nav.objects = world.objects;
        city::SpawnPoint p{};
        if (city::findRoadSpawn(nav, cityModel.scene().minimum, cityModel.scene().maximum, -3.4f, 32.7f, p)) {
            seatedCar = -1;
            flying = false;
            world.x = p.x;
            const float gy = cityModel.scene().collision->groundHeight(p.x, p.z, 2.0f, -10.0f);
            world.y = std::isfinite(gy) ? gy + 1.7f : 1.7f;
            world.z = p.z;
            world.yaw = 0.0f;
            player.reset(world.x, world.y, world.z);
            showToast(i18n::tr(Str::ToastTeleportRoad));
        }
    };

    auto respawnCar = [&]() {
        if (!hasCarParking) return;
        tools.cancel(world);
        sandbox::World parkingWorld = world;
        auto prev = std::find_if(parkingWorld.objects.begin(), parkingWorld.objects.end(),
                                 [](const sandbox::Object &o) { return o.type == 8; });
        if (prev != parkingWorld.objects.end()) parkingWorld.objects.erase(prev);
        auto restored = sandbox::carRespawn(parkingWorld, carParkingTemplate, *cityModel.scene().collision,
                                            cityModel.scene().minimum, cityModel.scene().maximum);
        if (restored) {
            auto first = std::find_if(world.objects.begin(), world.objects.end(),
                                      [](const sandbox::Object &o) { return o.type == 8; });
            if (first != world.objects.end()) {
                *first = *restored;
            } else if (world.objects.size() < sandbox::World::limit) {
                world.objects.push_back(*restored);
            }
            rebuildBuiltCollision();
            saveDirty = true;
            showToast(i18n::tr(Str::ToastCarRestored));
        } else {
            showToast(i18n::tr(Str::ToastCarParkingBlocked));
        }
    };

    auto copySpawnJson = [&]() {
        city::SpawnConfig config;
        config.enabled = true;
        config.x = world.x;
        config.y = world.y;
        config.z = world.z;
        config.yawDegrees = world.yaw * (180.0f / 3.14159265358979323846f);
        config.startFlying = flying;
        const std::string json = city::formatSpawnConfig(config);
        if (SDL_SetClipboardText(json.c_str()) == 0)
            showToast(i18n::tr(Str::ToastSpawnCopied));
        else
            showToast(i18n::tr(Str::ToastSpawnCopyFailed));
    };

    auto selectBuildItem = [&](int itemType) {
        if (itemType == -1) {
            // Take FPV drone in hands
            tools.select(world, sandbox::HandTool::HAND_EMPTY);
            building = false;
            drone.take({world.x, world.y, world.z}, world.yaw, *cityModel.scene().collision, builtCollision);
            rebuildBuiltCollision();
            catalogOpen = false;
            syncMouseCapture();
            showToast(i18n::tr(Str::ToastDroneInHands));
            return;
        }
        if (!building) {
            spawnReturnTool = (tools.tool == sandbox::HandTool::GRAVITY_GUN || tools.tool == sandbox::HandTool::DRONE_REMOTE)
                ? tools.tool
                : sandbox::HandTool::HAND_EMPTY;
        }
        tools.select(world, sandbox::HandTool::SPAWN_GUN);
        building = true;
        world.selected = itemType;
        world.surface = sandbox::World::fixedSurface(itemType);
        placementPreview.reset();
        rebuildBuiltCollision();
        catalogOpen = false;
        syncMouseCapture();
        showToast(i18n::format(Str::ToastItemSelected, {itemTitleForId(itemType)}));
    };

    // Cycle the UI language and persist the choice for the next launch.
    auto cycleLanguage = [&]() {
        const i18n::Language next = i18n::nextLanguage(i18n::currentLanguage());
        i18n::setLanguage(next);
        saveLanguage(settingsPath, next);
        SDL_SetWindowTitle(window, i18n::tr(Str::WindowTitle));
        showToast(i18n::format(Str::ToastLanguageChanged, {i18n::languageNativeName(next)}));
    };

    auto triggerInteract = [&]() {
        if (drone.piloting()) {
            drone.leave();
            showToast(i18n::tr(Str::ToastDroneLeft));
            return;
        }
        if (drone.mode == sandbox::FpvDrone::InHands) {
            drone.place(*cityModel.scene().collision, builtCollision);
            showToast(i18n::tr(Str::ToastDronePlaced));
            return;
        }
        if (tools.tool == sandbox::HandTool::DRONE_REMOTE) {
            if (drone.connect({world.x, world.y, world.z}, world.yaw, world.pitch, *cityModel.scene().collision, builtCollision)) {
                showToast(i18n::tr(Str::ToastDroneConnected));
                return;
            }
        }
        if (seatedCar >= 0) {
            auto exit = sandbox::carExit(world, seatedCar, *cityModel.scene().collision, builtCollision,
                                         cityModel.scene().minimum, cityModel.scene().maximum);
            if (exit) {
                world.objects[size_t(seatedCar)].anchored = carWasAnchored;
                world.x = exit->x;
                world.y = exit->y;
                world.z = exit->z;
                seatedCar = -1;
                player.reset(world.x, world.y, world.z);
            } else {
                showToast(i18n::tr(Str::ToastExitBlocked));
            }
            return;
        }
        if (!tools.holding()) {
            const int target = sandbox::interactionTarget(world, *cityModel.scene().collision);
            if (target >= 0) {
                auto &o = world.objects[size_t(target)];
                if (o.type == 3) {
                    o.doorTarget = o.doorTarget > 0.5f ? 0.0f : 1.57079633f;
                } else {
                    tools.cancel(world);
                    seatedCar = target;
                    seatFrom = {world.x, world.y, world.z};
                    seatTime = 0.0f;
                    carWasAnchored = o.anchored;
                    o.anchored = true;
                    o.vx = o.vy = o.vz = 0.0f;
                    flying = false;
                    world.yaw = float(o.rotation) * 1.57079633f + (o.type == 7 ? 3.14159265f : -1.57079633f);
                    world.pitch = 0.0f;
                }
            }
        }
    };

    auto toggleNoclip = [&]() {
        if (mapMode.active()) {
            const bool wasNoclip = mapMode.noclip();
            mapMode.toggleNoclip(); // the game's "noclip" when the game module runs the map
            showToast(i18n::tr(!wasNoclip ? Str::FlightOn : Str::FlightOff));
            return;
        }
        if (drone.piloting() || seatedCar >= 0) return;
        flying = !flying;
        player.reset(world.x, world.y, world.z);
        showToast(i18n::tr(flying ? Str::FlightOn : Str::FlightOff));
    };

    // Sandbox actions exposed as Realms console commands (also usable from
    // config/realms.cfg aliases, autoexec.cfg and +command line arguments).
    engine.addCommand("quit", [&](const realms::CommandArgs &) { running = false; });
    engine.addCommand("noclip", [&](const realms::CommandArgs &) { toggleNoclip(); });
    engine.addCommand("respawn", [&](const realms::CommandArgs &) {
        if (!mapMode.respawn(engine)) respawnPlayer();
    });
    engine.addCommand("teleport_road", [&](const realms::CommandArgs &) { teleportToRoad(); });
    engine.addCommand("restore_car", [&](const realms::CommandArgs &) { respawnCar(); });
    engine.addCommand("copy_spawn", [&](const realms::CommandArgs &) { copySpawnJson(); });
    engine.addCommand("lang", [&](const realms::CommandArgs &args) {
        if (args.size() < 2) { cycleLanguage(); return; }
        if (auto lang = i18n::parseLanguageTag(args[1])) {
            i18n::setLanguage(*lang);
            saveLanguage(settingsPath, *lang);
            SDL_SetWindowTitle(window, i18n::tr(Str::WindowTitle));
        } else {
            engine.print("lang: expected en, ru or pt-PT", REALMS_PRINT_WARNING);
        }
    });
    engine.addCommand("pos", [&](const realms::CommandArgs &) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "x=%.2f y=%.2f z=%.2f yaw=%.1f", world.x, world.y, world.z,
                      world.yaw * (180.0f / 3.14159265358979323846f));
        engine.print(buf);
    });
    engine.addCommand("realms_info", [&](const realms::CommandArgs &) {
        engine.print(std::string(realms::kEngineName) + " " + realms::kEngineVersion + " core=" +
                     engine.backendName() + " origin=" + engine.backendOrigin());
    });

    if (engine.cvarBool("vid_fullscreen")) {
        fullscreen = true;
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    engine.cvarModified("vid_fullscreen", true);
    engine.print(std::string("Realms engine ready: ") + engine.backendName() + " (" + engine.backendOrigin() +
                 "). Press ` for the console.");

    auto lastTick = std::chrono::steady_clock::now();

    while (running) {
        int winW = 1280, winH = 720;
        int drawW = 1280, drawH = 720;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &drawW, &drawH);
        const auto hudLayout = ekron::desktop::HudLayout::forWindow(winW, winH);
        const int hudW = hudLayout.width, hudH = hudLayout.height;

        float mousePitchInput = 0.0f;
        float mouseRollInput = 0.0f;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_WINDOWEVENT && (ev.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                                                      ev.window.event == SDL_WINDOWEVENT_RESTORED)) {
                sound.mixer().setPaused(ev.window.event == SDL_WINDOWEVENT_MINIMIZED);
            } else if (console.handleEvent(ev, engine)) {
                syncMouseCapture();
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                const SDL_Keycode key = ev.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    if (shopOpen) {
                        shopOpen = false;
                    } else if (catalogOpen) {
                        catalogOpen = false;
                    } else if (drone.piloting()) {
                        drone.leave();
                    } else {
                        pauseOpen = !pauseOpen;
                    }
                    syncMouseCapture();
                } else if (key == SDLK_F11) {
                    engine.setCvar("vid_fullscreen", fullscreen ? "0" : "1");
                } else if (mapMode.active()) {
                    // Map mode: sandbox tools, catalog and shop are off.
                    if (!pauseOpen && key == SDLK_n) toggleNoclip();
                    else if (!pauseOpen && key == SDLK_r) mapMode.respawn(engine);
                } else if (!pauseOpen && key == SDLK_g) {
                    if (!drone.piloting() && seatedCar < 0) {
                        shopOpen = !shopOpen;
                        syncMouseCapture();
                    }
                } else if (!pauseOpen && (key == SDLK_q || key == SDLK_TAB || key == SDLK_b)) {
                    if (!drone.piloting() && seatedCar < 0) {
                        catalogOpen = !catalogOpen;
                        syncMouseCapture();
                    }
                } else if (!pauseOpen && !catalogOpen && !shopOpen) {
                    if (key == SDLK_1) {
                        tools.select(world, sandbox::HandTool::GRAVITY_GUN);
                        building = false;
                        rebuildBuiltCollision();
                        showToast(i18n::format(Str::ToastTool, {i18n::tr(Str::ToolGravityGun)}));
                    } else if (key == SDLK_2) {
                        tools.select(world, sandbox::HandTool::DRONE_REMOTE);
                        building = false;
                        rebuildBuiltCollision();
                        showToast(i18n::format(Str::ToastTool, {i18n::tr(Str::ToolFpvRemote)}));
                    } else if (key == SDLK_3) {
                        tools.select(world, sandbox::HandTool::HAND_EMPTY);
                        building = false;
                        rebuildBuiltCollision();
                        showToast(i18n::format(Str::ToastTool, {i18n::tr(Str::ToolEmptyHands)}));
                    } else if (key == SDLK_SPACE && !flying && !drone.piloting() && seatedCar < 0) {
                        player.jump();
                    } else if (key == SDLK_c && !flying && !drone.piloting()) {
                        crouched = !crouched;
                        prone = false;
                    } else if (key == SDLK_z && !flying && !drone.piloting()) {
                        prone = !prone;
                        crouched = false;
                    } else if (key == SDLK_n && !drone.piloting() && seatedCar < 0) {
                        toggleNoclip();
                    } else if (key == SDLK_p) {
                        copySpawnJson();
                    } else if (key == SDLK_l) {
                        cycleLanguage();
                    } else if (key == SDLK_e) {
                        triggerInteract();
                    } else if (key == SDLK_v && drone.piloting()) {
                        drone.toggleView(*cityModel.scene().collision, builtCollision);
                    } else if (key == SDLK_h && (drone.piloting() || tools.tool == sandbox::HandTool::DRONE_REMOTE)) {
                        drone.returnHome({world.x, world.y, world.z}, world.yaw, *cityModel.scene().collision, builtCollision);
                        showToast(i18n::tr(Str::ToastDroneReturning));
                    } else if (key == SDLK_r) {
                        if (building) {
                            world.rotation = (world.rotation + 1) % 4;
                        } else if (tools.tool == sandbox::HandTool::GRAVITY_GUN) {
                            tools.rotate(world, cityModel.scene().collision.get());
                        }
                    } else if (key == SDLK_f) {
                        if (building) {
                            tools.select(world, spawnReturnTool);
                            building = false;
                            placementPreview.reset();
                            showToast(i18n::tr(Str::ToastBuildCancelled));
                        } else if (tools.tool == sandbox::HandTool::GRAVITY_GUN) {
                            if (tools.freeze(world, cityModel.scene().collision.get())) {
                                rebuildBuiltCollision();
                                saveDirty = true;
                            }
                        }
                    } else if (key == SDLK_x || key == SDLK_DELETE || key == SDLK_BACKSPACE) {
                        if (world.remove()) {
                            rebuildBuiltCollision();
                            saveDirty = true;
                            showToast(i18n::tr(Str::ToastObjectRemoved));
                        }
                    }
                }
            } else if (ev.type == SDL_MOUSEMOTION && !catalogOpen && !pauseOpen && !shopOpen) {
                if (mapMode.active()) {
                    const float look = 0.003f * realms::sandbox::lookScale(engine);
                    world.yaw -= float(ev.motion.xrel) * look;
                    world.pitch = std::clamp(world.pitch - realms::sandbox::pitchSign(engine) * float(ev.motion.yrel) * look,
                                             -1.55f, 1.55f);
                } else if (drone.mode == sandbox::FpvDrone::Flying) {
                    drone.look(float(ev.motion.xrel), float(ev.motion.yrel));
                    mouseRollInput = std::clamp(float(ev.motion.xrel) * 0.035f, -1.0f, 1.0f);
                    mousePitchInput = std::clamp(-float(ev.motion.yrel) * 0.035f, -1.0f, 1.0f);
                } else if (!drone.piloting()) {
                    const float look = 0.003f * realms::sandbox::lookScale(engine);
                    world.yaw -= float(ev.motion.xrel) * look;
                    world.pitch = std::clamp(world.pitch - realms::sandbox::pitchSign(engine) * float(ev.motion.yrel) * look,
                                             -1.3f, 1.3f);
                }
            } else if (ev.type == SDL_MOUSEWHEEL && !catalogOpen && !pauseOpen && !shopOpen && !mapMode.active()) {
                if (ev.wheel.y != 0) {
                    if (building) {
                        world.rotation = (world.rotation + (ev.wheel.y > 0 ? 1 : 3)) % 4;
                    } else if (tools.tool == sandbox::HandTool::GRAVITY_GUN) {
                        tools.rotate(world, cityModel.scene().collision.get());
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                const float mx = float(ev.button.x) * hudLayout.mouseScaleX;
                const float my = float(ev.button.y) * hudLayout.mouseScaleY;

                if (pauseOpen && ev.button.button == SDL_BUTTON_LEFT) {
                    const float pw = kPauseMenuWidth, ph = kPauseMenuHeight;
                    const float px = (float(hudW) - pw) * 0.5f;
                    const float py = (float(hudH) - ph) * 0.5f;
                    for (int idx = 0; idx < kPauseMenuButtons; ++idx) {
                        Rect btn{px + 28.0f, py + 62.0f + float(idx) * 42.0f, pw - 56.0f, 34.0f};
                        if (btn.contains(mx, my)) {
                            sound.ui("ui_click");
                            if (idx == 0) { pauseOpen = false; syncMouseCapture(); }
                            else if (idx == 1) {
                                if (mapMode.active()) toggleNoclip();
                                else { flying = !flying; player.reset(world.x, world.y, world.z); }
                                pauseOpen = false; syncMouseCapture();
                            }
                            else if (idx == 2) { if (!mapMode.active()) teleportToRoad(); pauseOpen = false; syncMouseCapture(); }
                            else if (idx == 3) { if (!mapMode.active()) respawnCar(); pauseOpen = false; syncMouseCapture(); }
                            else if (idx == 4) { if (!mapMode.respawn(engine)) respawnPlayer(); pauseOpen = false; syncMouseCapture(); }
                            else if (idx == 5) { if (!mapMode.active()) copySpawnJson(); pauseOpen = false; syncMouseCapture(); }
                            else if (idx == 6) { cycleLanguage(); } // keep the menu open to show the new language
                            else if (idx == 7) { running = false; }
                        }
                    }
                } else if (catalogOpen && ev.button.button == SDL_BUTTON_LEFT) {
                    const float cw = std::min(760.0f, float(hudW) - 60.0f);
                    const float ch = std::min(470.0f, float(hudH) - 60.0f);
                    const float cx = (float(hudW) - cw) * 0.5f;
                    const float cy = (float(hudH) - ch) * 0.5f;
                    const auto &cats = catalogCategories();

                    // Close button
                    if (Rect{cx + cw - 44.0f, cy + 12.0f, 32.0f, 30.0f}.contains(mx, my)) {
                        catalogOpen = false;
                        syncMouseCapture();
                    }
                    // Category tabs
                    const float tabW = (cw - 32.0f - 3.0f * 8.0f) / 4.0f;
                    for (size_t t = 0; t < cats.size(); ++t) {
                        if (Rect{cx + 16.0f + float(t) * (tabW + 8.0f), cy + 52.0f, tabW, 34.0f}.contains(mx, my)) {
                            sound.ui("ui_click");
                            selectedCategory = int(t);
                        }
                    }
                    // Cards
                    const auto &items = cats[size_t(selectedCategory)].items;
                    const float cardW = (cw - 32.0f - 3.0f * 12.0f) / 4.0f;
                    const float cardH = ch - 118.0f;
                    for (size_t k = 0; k < items.size(); ++k) {
                        Rect card{cx + 16.0f + float(k) * (cardW + 12.0f), cy + 100.0f, cardW, cardH};
                        if (card.contains(mx, my)) {
                            sound.ui("ui_click");
                            selectBuildItem(items[k].itemType);
                            break;
                        }
                    }
                } else if (shopOpen && ev.button.button == SDL_BUTTON_LEFT) {
                    const float sw = std::min(760.0f, float(hudW) - 60.0f);
                    const float sh = std::min(540.0f, float(hudH) - 60.0f);
                    const float sx = (float(hudW) - sw) * 0.5f;
                    const float sy = (float(hudH) - sh) * 0.5f;
                    const auto &scats = ekron::desktop::shopCategories();

                    // Close button
                    if (Rect{sx + sw - 44.0f, sy + 12.0f, 32.0f, 30.0f}.contains(mx, my)) {
                        shopOpen = false;
                        syncMouseCapture();
                    }
                    // Category tabs (Skins / Extras)
                    const float stabW = (sw - 32.0f - 8.0f) / 2.0f;
                    for (size_t t = 0; t < scats.size(); ++t) {
                        if (Rect{sx + 16.0f + float(t) * (stabW + 8.0f), sy + 60.0f, stabW, 32.0f}.contains(mx, my)) {
                            sound.ui("ui_click");
                            selectedShopCategory = int(t);
                        }
                    }
                    // Cards + action buttons
                    const auto &sitems = scats[size_t(selectedShopCategory)].items;
                    const float scardW = (sw - 32.0f - 3.0f * 12.0f) / 4.0f;
                    const float scardH = sh - 150.0f;
                    for (size_t k = 0; k < sitems.size(); ++k) {
                        const float kx = sx + 16.0f + float(k) * (scardW + 12.0f);
                        const float ky = sy + 104.0f;
                        Rect btn{kx + 8.0f, ky + scardH - 40.0f, scardW - 16.0f, 30.0f};
                        if (!btn.contains(mx, my)) continue;
                        const int id = sitems[k].id;
                        if (shop.owns(id)) {
                            if (sitems[k].kind == ekron::desktop::ShopKind::Skin && shop.equipSkin(id)) {
                                sound.ui("ui_confirm");
                                shopDirty = true;
                                shop.save(storePath);
                                showToast(i18n::format(Str::ToastSkinApplied, {ekron::desktop::shopItemName(sitems[k])}));
                            }
                        } else if (online.shopSynced()) {
                            pendingShopToastReq = online.requestPurchase(id);
                            showToast(i18n::tr(Str::ToastPurchaseRequesting));
                        } else {
                            std::string reason;
                            if (shop.tryBuy(id, reason)) {
                                sound.ui("ui_confirm");
                                grantStartWithFlight();
                                shopDirty = true;
                                pendingShopToastReq = id; // local echo for the toast
                                shop.save(storePath);
                                showToast(i18n::format(Str::ToastPurchased, {ekron::desktop::shopItemName(sitems[k])}));
                            } else {
                                sound.ui("ui_error");
                                showToast(i18n::localizeShopError(reason));
                            }
                        }
                        break;
                    }
                } else if (!catalogOpen && !pauseOpen && !shopOpen && !mapMode.active()) {
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        if (drone.mode == sandbox::FpvDrone::InHands) {
                            drone.place(*cityModel.scene().collision, builtCollision);
                            showToast(i18n::tr(Str::ToastDronePlaced));
                        } else if (tools.tool == sandbox::HandTool::DRONE_REMOTE && !drone.piloting()) {
                            if (drone.connect({world.x, world.y, world.z}, world.yaw, world.pitch,
                                              *cityModel.scene().collision, builtCollision)) {
                                showToast(i18n::tr(Str::ToastDronePilotingActive));
                            }
                        } else if (building && seatedCar < 0) {
                            auto committed = sandbox::RaycastPlacement::commitPreview(
                                world, placementPreview, *cityModel.scene().collision, builtCollision,
                                cityModel.scene().minimum, cityModel.scene().maximum);
                            if (committed && (committed->type != 8 || carModel.ready())) {
                                world.objects.push_back(*committed);
                                placementPreview.reset();
                                tools.select(world, spawnReturnTool);
                                building = false;
                                rebuildBuiltCollision();
                                saveDirty = true;
                            }
                        } else if (tools.tool == sandbox::HandTool::GRAVITY_GUN) {
                            if (tools.holding()) {
                                if (tools.release(world)) saveDirty = true;
                            } else {
                                tools.grab(world, cityModel.scene().collision.get());
                            }
                            rebuildBuiltCollision();
                        }
                    } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                        if (building) {
                            tools.select(world, spawnReturnTool);
                            building = false;
                            placementPreview.reset();
                            showToast(i18n::tr(Str::ToastBuildCancelled));
                        } else if (tools.tool == sandbox::HandTool::GRAVITY_GUN) {
                            if (tools.freeze(world, cityModel.scene().collision.get())) {
                                rebuildBuiltCollision();
                                saveDirty = true;
                            }
                        } else if (drone.piloting()) {
                            drone.leave();
                        }
                    }
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::clamp(std::chrono::duration<float>(now - lastTick).count(), 0.0f, 0.05f);
        lastTick = now;

        // Realms frame: advances the core clock and runs the command buffer.
        engine.frame(dt);
        if (engine.cvarModified("vid_fullscreen", true)) {
            fullscreen = engine.cvarBool("vid_fullscreen");
            SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        if (!running) break;

        // Apply map / killserver (deferred: may come from boot or the console).
        if (pendingMapChange) {
            const std::string target = *pendingMapChange;
            pendingMapChange.reset();
            const bool wasActive = mapMode.active();
            sound.director().reset();
            if (wasActive) engine.print("map: " + gameSound.summary() + "\n", REALMS_PRINT_DEVELOPER);
            gameSound.stop(sound.mixer());
            if (target.empty()) {
                if (wasActive) {
                    mapMode.close(gl, renderer);
                    world.yaw = sandboxYaw;
                    world.pitch = sandboxPitch;
                    showToast("Realms: back to the sandbox");
                }
            } else {
                if (!wasActive) {
                    sandboxYaw = world.yaw;
                    sandboxPitch = world.pitch;
                }
                std::string mapError;
                if (mapMode.open(engine, gl, renderer, mapError, engineCfg.executableDir)) {
                    world.yaw = mapMode.spawnRenderYaw();
                    world.pitch = 0.0f;
                    engine.print("map: " + std::string(mapMode.gameRunning() ? "playing " : "walking ") + mapMode.path() +
                                 " (" + mapMode.summary() + ")" +
                                 (mapMode.gameRunning() ? std::string() : ", spawn: " + mapMode.walker().spawnSource) +
                                 ". killserver returns to the sandbox.\n");
                    showToast("Realms: " + mapMode.path());
                } else {
                    engine.print("map: can't show " + target + ": " + mapError + "\n", REALMS_PRINT_WARNING);
                    if (wasActive) {
                        world.yaw = sandboxYaw;
                        world.pitch = sandboxPitch;
                    }
                }
            }
            syncMouseCapture();
        }

        if (toastTimer > 0.0f) {
            toastTimer = std::max(0.0f, toastTimer - dt);
        }

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        float moveRight = 0.0f;
        float moveForward = 0.0f;
        float verticalMove = 0.0f;
        float droneThrottle = 0.0f;
        float droneYaw = 0.0f;
        float dronePitch = mousePitchInput;
        float droneRoll = mouseRollInput;

        if (!catalogOpen && !pauseOpen && !shopOpen && !console.isOpen()) {
            if (drone.piloting()) {
                if (keys[SDL_SCANCODE_W]) droneThrottle += 1.0f;
                if (keys[SDL_SCANCODE_S]) droneThrottle -= 1.0f;
                if (keys[SDL_SCANCODE_D]) droneYaw += 1.0f;
                if (keys[SDL_SCANCODE_A]) droneYaw -= 1.0f;
                if (keys[SDL_SCANCODE_UP]) dronePitch += 0.85f;
                if (keys[SDL_SCANCODE_DOWN]) dronePitch -= 0.85f;
                if (keys[SDL_SCANCODE_RIGHT]) droneRoll += 0.85f;
                if (keys[SDL_SCANCODE_LEFT]) droneRoll -= 0.85f;
                dronePitch = std::clamp(dronePitch, -1.0f, 1.0f);
                droneRoll = std::clamp(droneRoll, -1.0f, 1.0f);
            } else {
                if (keys[SDL_SCANCODE_D]) moveRight += 1.0f;
                if (keys[SDL_SCANCODE_A]) moveRight -= 1.0f;
                if (keys[SDL_SCANCODE_W]) moveForward += 1.0f;
                if (keys[SDL_SCANCODE_S]) moveForward -= 1.0f;
                if (keys[SDL_SCANCODE_SPACE]) verticalMove += 1.0f;
                if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] || (flying && keys[SDL_SCANCODE_C])) verticalMove -= 1.0f;
                const float len = std::hypot(moveRight, moveForward);
                if (len > 1.0f) {
                    moveRight /= len;
                    moveForward /= len;
                }
            }
        }

        if (mapMode.active() && !pauseOpen) {
            realms::WalkInput walkInput;
            if (!catalogOpen && !shopOpen && !console.isOpen()) {
                walkInput.forward = float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]);
                walkInput.right = float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
                const bool down = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] || keys[SDL_SCANCODE_C];
                walkInput.jump = keys[SDL_SCANCODE_SPACE] != 0;
                walkInput.crouch = down;
                walkInput.up = float(walkInput.jump) - float(down);
            }
            // Left mouse fires the game's weapon (game module maps).
            const bool attack = !catalogOpen && !shopOpen && !console.isOpen() && SDL_GetRelativeMouseMode() &&
                                (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
            mapMode.update(engine, walkInput, world.yaw, world.pitch, dt, attack);
            for (const std::string &message : mapMode.takeGameMessages()) showToast(message);
        }

        if (!pauseOpen && !mapMode.active()) {
            const size_t beforeCount = world.objects.size();
            const bool moving = dynamics.update(world, *cityModel.scene().collision, dt, tools.held,
                                                cityModel.scene().minimum, cityModel.scene().maximum,
                                                &tools.held, &seatedCar);
            if (world.objects.size() != beforeCount || moving) {
                rebuildBuiltCollision();
                saveDirty = true;
            }

            // Animate doors
            bool doorsChanged = false;
            for (auto &o : world.objects) {
                if (o.type == 3 && !o.frozen && std::abs(o.doorAngle - o.doorTarget) > 0.0001f) {
                    const float prevAngle = o.doorAngle;
                    o.doorAngle += std::clamp(o.doorTarget - o.doorAngle, -dt * 2.2f, dt * 2.2f);
                    physics::CollisionMesh sweep;
                    sandbox::addObjectCollision(sweep, o);
                    sweep.build();
                    if (player.overlaps(sweep, world.x, world.y, world.z)) {
                        o.doorAngle = prevAngle;
                        o.doorTarget = prevAngle;
                    } else {
                        doorsChanged = true;
                    }
                }
            }
            if (doorsChanged) rebuildBuiltCollision();

            if (seatedCar >= 0 && size_t(seatedCar) < world.objects.size()) {
                const auto seat = sandbox::driverSeat(world.objects[size_t(seatedCar)]);
                seatTime = std::min(1.0f, seatTime + dt / 0.65f);
                const float t = seatTime * seatTime * (3.0f - 2.0f * seatTime);
                const auto p = seatFrom + (seat - seatFrom) * t;
                world.x = p.x;
                world.y = p.y;
                world.z = p.z;
            } else if (!drone.piloting()) {
                if (flying) {
                    city::fly(world, moveRight, moveForward, verticalMove, dt);
                } else {
                    player.update(*cityModel.scene().collision, moveRight, moveForward, world.yaw, dt,
                                  crouched, world.x, world.y, world.z, &builtCollision, prone,
                                  cityModel.scene().fenceBarriers.get());
                }
            }

            if (seatedCar < 0 && !flying &&
                sandbox::needsPlayerRecovery(world, cityModel.scene().minimum, cityModel.scene().maximum)) {
                respawnPlayer();
            }
            world.x = std::clamp(world.x, cityModel.scene().minimum[0] + 0.25f, cityModel.scene().maximum[0] - 0.25f);
            world.z = std::clamp(world.z, cityModel.scene().minimum[2] + 0.25f, cityModel.scene().maximum[2] - 0.25f);

            if (drone.mode == sandbox::FpvDrone::InHands) {
                if (tools.tool != sandbox::HandTool::HAND_EMPTY) {
                    drone.mode = sandbox::FpvDrone::Absent;
                } else {
                    const physics::Vec3 p{
                        world.x - std::sin(world.yaw) * 0.75f,
                        world.y - 0.35f,
                        world.z - std::cos(world.yaw) * 0.75f
                    };
                    if (!drone.blocked(p, *cityModel.scene().collision, builtCollision)) {
                        drone.position = p;
                        drone.yaw = world.yaw;
                    }
                }
            }
            drone.update(dt, droneRoll, dronePitch, droneThrottle, droneYaw, 0.0f,
                         *cityModel.scene().collision, builtCollision);
            drone.updateChase(*cityModel.scene().collision, builtCollision);

            tools.pull(world, dt, cityModel.scene().collision.get());
        }

        // Sound: the listener is the camera (the drone while piloting it).
        sound.syncCvars(engine);
        if (mapMode.active()) {
            sound.director().map(ekron::audio::mapFrame(!pauseOpen, engine, mapMode.walker(), world.yaw), dt, sound);
            // the game's own sounds, the effects' and the entity loops
            std::vector<realms::GameEvent> gameSounds = mapMode.takeGameSounds();
            if (pauseOpen) gameSound.stop(sound.mixer());
            else gameSound.update(engine, sound.mixer(), gameSounds, mapMode.loopSounds(), mapMode.listener(), dt);
        } else {
            ekron::audio::Listener ear;
            const physics::Vec3 earAt = drone.piloting() ? drone.camera() : physics::Vec3{world.x, world.y, world.z};
            ear.x = earAt.x;
            ear.y = earAt.y;
            ear.z = earAt.z;
            ear.yaw = drone.piloting() ? drone.viewYaw() : world.yaw;
            sound.director().sandbox(ekron::audio::sandboxFrame(!pauseOpen, ear, world, tools, drone, player, seatedCar,
                                                                flying, droneThrottle),
                                     dt, sound);
        }
        if ((pauseOpen && !lastPauseOpen) || (catalogOpen && !lastCatalogOpen) || (shopOpen && !lastShopOpen))
            sound.ui("ui_open");
        else if ((!pauseOpen && lastPauseOpen) || (!catalogOpen && lastCatalogOpen) || (!shopOpen && lastShopOpen))
            sound.ui("ui_close");
        lastPauseOpen = pauseOpen;
        lastCatalogOpen = catalogOpen;
        lastShopOpen = shopOpen;

        // Autosave debounce
        if (saveDirty) {
            saveTimer += dt;
            if (saveTimer >= 1.2f) {
                sandbox::World saved;
                saved.objects = world.objects;
                if (seatedCar >= 0 && size_t(seatedCar) < saved.objects.size()) {
                    saved.objects[size_t(seatedCar)].anchored = carWasAnchored;
                }
                if (tools.holding() && tools.held >= 0 && size_t(tools.held) < saved.objects.size()) {
                    saved.objects[size_t(tools.held)] = tools.original;
                }
                saved.save(savePath);
                saveDirty = false;
                saveTimer = 0.0f;
            }
        }

        // Store economy: passive coins, online wallet sync, purchase feedback
        // and debounced local persistence.
        if (!pauseOpen) {
            if (online.shopSynced()) {
                // Online: the relay is authoritative. Mirror its wallet and
                // ownership, then resolve a pending purchase into a toast.
                shop.applyRemote(online.walletCoins(), online.shopOwned(), shop.equippedSkinId);
                if (pendingShopToastReq >= 0 && online.lastPurchaseReq() == pendingShopToastReq) {
                    if (online.lastPurchaseOk()) {
                        grantStartWithFlight();
                        showToast(i18n::tr(Str::ToastPurchasedFromServer));
                    } else {
                        const std::string reason = i18n::localizeShopError(online.lastPurchaseReason());
                        showToast(reason.empty() ? std::string(i18n::tr(Str::ToastPurchaseRejected)) : reason);
                    }
                    pendingShopToastReq = -1;
                }
            } else {
                // Offline: accrue coins locally (with the Double Coins multiplier).
                shop.accrue(dt, ekron::desktop::shopCoinMultiplier(shop));
                if (pendingShopToastReq >= 0) pendingShopToastReq = -1;
            }
        }
        if (shopDirty) {
            shopSaveTimer += dt;
            if (shopSaveTimer >= 1.0f) {
                shop.save(storePath);
                shopDirty = false;
                shopSaveTimer = 0.0f;
            }
        }

        // Build world vertex stream
        std::vector<sandbox::MeshVertex> worldVertices;
        worldVertices.reserve((world.objects.size() * 10 + 4) * 36);
        online.updatePose(world.x, world.y, world.z, world.yaw, world.pitch);
        sandbox::appendRemotePlayers(worldVertices, online.players());
        sandbox::appendCityGraffiti(worldVertices, *cityModel.scene().collision);
        std::vector<std::array<float, 16>> carTransforms;

        for (size_t i = 0; i < world.objects.size(); ++i) {
            if (world.objects[i].type == 8) {
                if (carModel.ready()) {
                    carTransforms.push_back(computeCarMatrix(world.objects[i], carModel, int(i) == tools.held, tools.display));
                }
                continue;
            }
            const size_t objBegin = worldVertices.size();
            if (int(i) == tools.held) {
                auto visual = world.objects[i];
                visual.x = tools.display.x;
                visual.z = tools.display.z;
                sandbox::appendObject(worldVertices, visual);
                const float dy = tools.display.y - sandbox::World::bounds(visual).y;
                for (size_t j = objBegin; j < worldVertices.size(); ++j) worldVertices[j].y += dy;
            } else {
                sandbox::appendObject(worldVertices, world.objects[i]);
            }
            sandbox::animateDoorMesh(world.objects[i], worldVertices, objBegin);

            const auto &o = world.objects[i];
            auto b = sandbox::World::bounds(o);
            if (int(i) == tools.held) {
                b.x = tools.display.x;
                b.y = tools.display.y;
                b.z = tools.display.z;
            }
            const float cx = std::cos(o.tiltX), sx = std::sin(o.tiltX);
            const float cz = std::cos(o.tiltZ), sz = std::sin(o.tiltZ);
            for (size_t j = objBegin; j < worldVertices.size(); ++j) {
                auto &v = worldVertices[j];
                const float x = v.x - b.x, y = v.y - b.y, z = v.z - b.z;
                const float py = cx * y - sx * z, pz = sx * y + cx * z;
                v.x = b.x + cz * x - sz * py;
                v.y = b.y + sz * x + cz * py;
                v.z = b.z + pz;
                const float ny = cx * v.ny - sx * v.nz, nz = sx * v.ny + cx * v.nz;
                const float nx = cz * v.nx - sz * ny;
                v.ny = sz * v.nx + cz * ny;
                v.nx = nx;
                v.nz = nz;
            }
        }

        if (tools.holding() && tools.tool == sandbox::HandTool::GRAVITY_GUN) {
            // Apply the equipped weapon-skin tint to the gravity beam.
            const ekron::desktop::ShopItem *skin = shop.equippedSkin();
            sandbox::appendGravityBeam(worldVertices, world, tools.display,
                                       skin ? skin->tintR : 0.15f,
                                       skin ? skin->tintG : 1.0f,
                                       skin ? skin->tintB : 0.45f);
        }

        placementPreview.reset();
        std::vector<sandbox::MeshVertex> ghostVertices;
        std::optional<std::array<float, 16>> ghostCarTransform;
        if (building && seatedCar < 0) {
            placementPreview = sandbox::RaycastPlacement::preview(
                world, *cityModel.scene().collision, builtCollision,
                cityModel.scene().minimum, cityModel.scene().maximum);
            if (placementPreview) {
                if (placementPreview->type == 8 && carModel.ready()) {
                    ghostCarTransform = computeCarMatrix(*placementPreview, carModel, false, {});
                } else if (placementPreview->type != 8) {
                    sandbox::appendObject(ghostVertices, *placementPreview, true, true);
                }
            }
        }

        const bool showRemoteViewmodel = tools.tool == sandbox::HandTool::DRONE_REMOTE &&
                                         !drone.piloting() && seatedCar < 0 && remoteModel.ready();
        const bool showRemoteMonitor = showRemoteViewmodel &&
                                       drone.mode != sandbox::FpvDrone::Absent &&
                                       drone.mode != sandbox::FpvDrone::InHands;

        if (showRemoteMonitor && !mapMode.active()) {
            renderer.renderMonitorPass(gl, worldVertices, cityModel, carModel, carTransforms,
                                       drone.feedEye(), drone.feedYaw(), drone.feedPitch(), drone.roll);
        }

        const physics::Vec3 camEye = drone.piloting() ? drone.camera() : physics::Vec3{world.x, world.y, world.z};
        const float camYaw = drone.piloting() ? drone.viewYaw() : world.yaw;
        const float camPitch = drone.piloting() ? drone.viewPitch() : world.pitch;
        const float camRoll = drone.piloting() ? drone.viewRoll() : 0.0f;

        const bool showWorldDrone = drone.mode != sandbox::FpvDrone::Absent &&
                                    (drone.mode != sandbox::FpvDrone::Flying || drone.viewWeight() > 0.15f) &&
                                    droneModel.ready();
        std::optional<std::array<float, 16>> droneMat;
        if (showWorldDrone) {
            droneMat = computeDroneMatrix(drone, droneModel);
        }

        if (mapMode.active()) {
            renderer.setMapModelPoses(mapMode.modelPoses()); // doors / platforms moved by the game
            // items, monsters, projectiles and the view weapon (.md2, or boxes without id's data)
            mapMode.buildEntities(engine, gl, world.yaw, world.pitch, mapEntityVertices, mapEntityBatches);
            renderer.setMapEntities(gl, mapEntityVertices, std::move(mapEntityBatches));
            // particles, trails, laser beams and dynamic lights (cl_fx.c / cl_tent.c)
            mapMode.buildEffects(world.yaw, world.pitch, mapEffectVertices, mapLights);
            renderer.setMapEffects(gl, mapEffectVertices, mapLights);
            renderer.renderMapWorld(gl, drawW, drawH, mapMode.model(), mapMode.eye(), world.yaw, world.pitch, 0.0f);
        } else renderer.renderMainWorld(
            gl, drawW, drawH,
            worldVertices, ghostVertices,
            cityModel, carModel, carTransforms,
            ghostCarTransform ? &(*ghostCarTransform) : nullptr,
            showWorldDrone ? &droneModel : nullptr,
            droneMat ? &(*droneMat) : nullptr,
            showRemoteViewmodel ? &remoteModel : nullptr,
            showRemoteMonitor,
            camEye, camYaw, camPitch, camRoll
        );

        // 2D Desktop HUD
        renderer.beginUi(hudW, hudH);

        // Center crosshair (cosmetic skins from the store)
        ekron::desktop::Color crossColor{0.92f, 0.98f, 0.95f, 0.85f};
        if (shop.hasExtra(103)) crossColor = {1.00f, 0.84f, 0.20f, 0.95f};      // Golden Crosshair
        else if (shop.hasExtra(105)) crossColor = {0.40f, 1.00f, 0.95f, 0.95f}; // Neon Sight
        if (!catalogOpen && !pauseOpen && !shopOpen) {
            const float cx = float(hudW) * 0.5f;
            const float cy = float(hudH) * 0.5f;
            renderer.drawRect(cx - 8.0f, cy - 1.0f, 16.0f, 2.0f, crossColor);
            renderer.drawRect(cx - 1.0f, cy - 8.0f, 2.0f, 16.0f, crossColor);
        }

        if (mapMode.active()) {
            // Realms map HUD: map, position (map units) and controls.
            renderer.drawRect(14.0f, 14.0f, 520.0f, 54.0f, {0.06f, 0.09f, 0.12f, 0.78f});
            renderer.drawBorder(14.0f, 14.0f, 520.0f, 54.0f, 1.5f, {0.62f, 0.48f, 0.20f, 0.85f});
            const std::string mapTitle = "Realms map  •  " + mapMode.path();
            renderer.drawText(26.0f, 23.0f, mapTitle,
                              ekron::desktop::fitHudTextScale(1.75f, ekron::font::measureTextWidth(mapTitle, 1.75f), 496.0f),
                              {0.95f, 0.97f, 1.0f, 1.0f});
            const realms::MapWalker &walker = mapMode.walker();
            char mapBuf[200]{};
            const std::string gameStatus = mapMode.gameStatus();
            std::snprintf(mapBuf, sizeof(mapBuf), "%.0f %.0f %.0f  %s  %s%s%s", walker.origin[0], walker.origin[1],
                          walker.origin[2], mapMode.summary().c_str(),
                          walker.noclip ? "[NOCLIP]" : (walker.waterLevel >= 2 ? "[SWIM]" : (walker.ducked ? "[DUCK]" : "")),
                          gameStatus.empty() ? "" : "  ", gameStatus.c_str());
            renderer.drawText(26.0f, 46.0f, mapBuf,
                              ekron::desktop::fitHudTextScale(1.35f, ekron::font::measureTextWidth(mapBuf, 1.35f), 496.0f),
                              {0.86f, 0.80f, 0.62f, 0.95f});
            renderer.drawRect(14.0f, float(hudH) - 42.0f, float(hudW) - 28.0f, 28.0f, {0.05f, 0.08f, 0.11f, 0.72f});
            const std::string mapControls = mapMode.gameRunning()
                ? "WASD move  •  Space jump  •  Ctrl crouch  •  Mouse fire  •  N noclip  •  R restart  •  ` console: give / use / killserver"
                : "WASD move  •  Space jump  •  Ctrl crouch  •  N noclip  •  R respawn  •  ` console: killserver";
            renderer.drawCenteredText(float(hudW) * 0.5f, float(hudH) - 28.0f, mapControls,
                                      ekron::desktop::fitHudTextScale(1.30f, ekron::font::measureTextWidth(mapControls, 1.30f),
                                                                      float(hudW) - 48.0f),
                                      {0.82f, 0.95f, 0.90f, 1.0f});
        } else {
        // Top-left status bar
        ekron::desktop::Color hudAccent{0.18f, 0.68f, 0.58f, 0.85f};
        if (shop.hasExtra(104)) hudAccent = {0.38f, 0.62f, 0.32f, 0.85f}; // Camo HUD
        renderer.drawRect(14.0f, 14.0f, 430.0f, 54.0f, {0.06f, 0.09f, 0.12f, 0.78f});
        renderer.drawBorder(14.0f, 14.0f, 430.0f, 54.0f, 1.5f, hudAccent);
        const std::string toolLabel = building ? std::string(i18n::tr(Str::ToolBuilding))
            : (tools.tool == sandbox::HandTool::GRAVITY_GUN ? std::string("1: ") + i18n::tr(Str::ToolGravityGun)
            : (tools.tool == sandbox::HandTool::DRONE_REMOTE ? std::string("2: ") + i18n::tr(Str::ToolFpvRemote)
                                                              : std::string("3: ") + i18n::tr(Str::ToolEmptyHands)));
        const std::string statusTitle = std::string(i18n::tr(Str::BrandName)) + "  •  " + toolLabel;
        renderer.drawText(26.0f, 23.0f, statusTitle,
                          ekron::desktop::fitHudTextScale(1.75f, ekron::font::measureTextWidth(statusTitle, 1.75f), 406.0f),
                          {0.95f, 0.97f, 1.0f, 1.0f});
        char coordBuf[160]{};
        std::snprintf(coordBuf, sizeof(coordBuf), "X: %.1f  Y: %.1f  Z: %.1f  %s",
                      world.x, world.y, world.z,
                      flying ? "[NOCLIP]" : (prone ? i18n::tr(Str::StatusProne) : (crouched ? i18n::tr(Str::StatusCrouched) : "")));
        renderer.drawText(26.0f, 46.0f, coordBuf, 1.35f, {0.68f, 0.86f, 0.82f, 0.95f});

        // Bottom controls bar
        renderer.drawRect(14.0f, float(hudH) - 42.0f, float(hudW) - 28.0f, 28.0f, {0.05f, 0.08f, 0.11f, 0.72f});
        const std::string controlsText = i18n::tr(drone.piloting() ? Str::ControlsDrone : Str::ControlsWalk);
        const float preferredControlsScale = drone.piloting() ? 1.35f : 1.30f;
        const float controlsScale = ekron::desktop::fitHudTextScale(
            preferredControlsScale, ekron::font::measureTextWidth(controlsText, preferredControlsScale), float(hudW) - 48.0f);
        renderer.drawCenteredText(float(hudW) * 0.5f, float(hudH) - 28.0f, controlsText, controlsScale,
                                  {0.82f, 0.95f, 0.90f, 1.0f});

        // Contextual interaction prompt
        std::string prompt;
        if (seatedCar >= 0) {
            prompt = i18n::tr(Str::PromptExit);
        } else if (drone.mode == sandbox::FpvDrone::InHands) {
            prompt = i18n::tr(Str::PromptPlaceDrone);
        } else if (tools.tool == sandbox::HandTool::DRONE_REMOTE && !drone.piloting() &&
                   (drone.linked || drone.aimed({world.x, world.y, world.z}, world.yaw, world.pitch,
                                                *cityModel.scene().collision, builtCollision))) {
            prompt = i18n::tr(Str::PromptConnectDrone);
        } else if (!tools.holding()) {
            const int target = sandbox::interactionTarget(world, *cityModel.scene().collision);
            if (target >= 0) {
                const auto &o = world.objects[size_t(target)];
                prompt = i18n::tr((o.type == 8) ? Str::PromptEnterCar
                       : (o.type == 7 ? Str::PromptSitSofa
                       : (o.doorTarget > 0.5f ? Str::PromptCloseDoor : Str::PromptOpenDoor)));
            }
        }
        if (!prompt.empty() && !catalogOpen && !pauseOpen && !shopOpen) {
            const float promptScale = ekron::desktop::fitHudTextScale(
                1.65f, ekron::font::measureTextWidth(prompt, 1.65f), float(hudW) - 64.0f);
            const float pw = ekron::font::measureTextWidth(prompt, promptScale) + 28.0f;
            renderer.drawRect((float(hudW) - pw) * 0.5f, float(hudH) * 0.62f, pw, 32.0f, {0.06f, 0.14f, 0.14f, 0.85f});
            renderer.drawBorder((float(hudW) - pw) * 0.5f, float(hudH) * 0.62f, pw, 32.0f, 1.5f, {0.22f, 0.78f, 0.66f, 0.9f});
            renderer.drawCenteredText(float(hudW) * 0.5f, float(hudH) * 0.62f + 16.0f, prompt, promptScale, {0.95f, 1.0f, 0.97f, 1.0f});
        }
        } // sandbox HUD

        // Toast banner
        if (toastTimer > 0.0f && !toastText.empty()) {
            const float alpha = std::min(1.0f, toastTimer * 1.5f);
            const float toastScale = ekron::desktop::fitHudTextScale(
                1.5f, ekron::font::measureTextWidth(toastText, 1.5f), float(hudW) - 64.0f);
            const float tw = ekron::font::measureTextWidth(toastText, toastScale) + 32.0f;
            const float tx = (float(hudW) - tw) * 0.5f;
            renderer.drawRect(tx, 78.0f, tw, 30.0f, {0.08f, 0.13f, 0.16f, 0.84f * alpha});
            renderer.drawBorder(tx, 78.0f, tw, 30.0f, 1.0f, {0.25f, 0.75f, 0.65f, 0.85f * alpha});
            renderer.drawCenteredText(float(hudW) * 0.5f, 93.0f, toastText, toastScale, {0.95f, 0.98f, 1.0f, alpha});
        }

        // Build & Equipment Catalog modal
        if (catalogOpen) {
            renderer.drawRect(0.0f, 0.0f, float(hudW), float(hudH), {0.02f, 0.03f, 0.05f, 0.62f});
            const float cw = std::min(760.0f, float(hudW) - 60.0f);
            const float ch = std::min(470.0f, float(hudH) - 60.0f);
            const float cx = (float(hudW) - cw) * 0.5f;
            const float cy = (float(hudH) - ch) * 0.5f;

            renderer.drawRect(cx, cy, cw, ch, {0.08f, 0.11f, 0.14f, 0.96f});
            renderer.drawBorder(cx, cy, cw, ch, 2.0f, {0.20f, 0.72f, 0.62f, 0.95f});
            renderer.drawText(cx + 18.0f, cy + 18.0f, i18n::tr(Str::CatalogTitle),
                              fitText(i18n::tr(Str::CatalogTitle), 1.85f, cw - 80.0f), {0.95f, 0.98f, 1.0f, 1.0f});

            // Close button
            renderer.drawRect(cx + cw - 44.0f, cy + 12.0f, 32.0f, 30.0f, {0.42f, 0.14f, 0.16f, 0.95f});
            renderer.drawCenteredText(cx + cw - 28.0f, cy + 27.0f, "X", 1.75f, {1, 1, 1, 1});

            const auto &cats = catalogCategories();
            const float tabW = (cw - 32.0f - 3.0f * 8.0f) / 4.0f;
            for (size_t t = 0; t < cats.size(); ++t) {
                const bool active = int(t) == selectedCategory;
                const float tx = cx + 16.0f + float(t) * (tabW + 8.0f);
                renderer.drawRect(tx, cy + 52.0f, tabW, 34.0f,
                                  active ? ekron::desktop::Color{0.16f, 0.52f, 0.45f, 0.98f}
                                         : ekron::desktop::Color{0.13f, 0.17f, 0.22f, 0.95f});
                renderer.drawBorder(tx, cy + 52.0f, tabW, 34.0f, 1.0f,
                                    active ? ekron::desktop::Color{0.35f, 0.92f, 0.80f, 1.0f}
                                           : ekron::desktop::Color{0.25f, 0.32f, 0.38f, 0.8f});
                const char *tabName = i18n::tr(cats[t].name);
                renderer.drawCenteredText(tx + tabW * 0.5f, cy + 69.0f, tabName, fitText(tabName, 1.5f, tabW - 12.0f),
                                          {0.96f, 0.98f, 1.0f, 1.0f});
            }

            const auto &items = cats[size_t(selectedCategory)].items;
            const float cardW = (cw - 32.0f - 3.0f * 12.0f) / 4.0f;
            const float cardH = ch - 118.0f;
            for (size_t k = 0; k < items.size(); ++k) {
                const float kx = cx + 16.0f + float(k) * (cardW + 12.0f);
                const float ky = cy + 100.0f;
                const bool sel = building && world.selected == items[k].itemType;
                renderer.drawRect(kx, ky, cardW, cardH,
                                  sel ? ekron::desktop::Color{0.14f, 0.28f, 0.26f, 0.98f}
                                      : ekron::desktop::Color{0.11f, 0.15f, 0.19f, 0.96f});
                renderer.drawBorder(kx, ky, cardW, cardH, 1.5f,
                                    sel ? ekron::desktop::Color{0.30f, 0.90f, 0.75f, 1.0f}
                                        : ekron::desktop::Color{0.22f, 0.30f, 0.36f, 0.9f});
                GLuint icon = renderer.loadCatalogIcon(gl, assetRoot, items[k].iconFile);
                const float iconSz = std::min(cardW - 28.0f, cardH - 92.0f);
                const float ix = kx + (cardW - iconSz) * 0.5f;
                const float iy = ky + 16.0f;
                if (icon) {
                    renderer.drawIcon(gl, icon, ix, iy, iconSz, iconSz);
                } else {
                    // Crisp procedural preview art per catalog category/item
                    renderer.drawRect(ix, iy, iconSz, iconSz, {0.07f, 0.10f, 0.13f, 0.95f});
                    renderer.drawBorder(ix, iy, iconSz, iconSz, 1.0f, {0.20f, 0.34f, 0.40f, 0.85f});
                    const int t = items[k].itemType;
                    const float m = iconSz * 0.18f;
                    if (t == 9) { // Boards
                        for (int r = 0; r < 3; ++r)
                            renderer.drawRect(ix + m, iy + m + float(r) * (iconSz * 0.22f), iconSz - 2 * m, iconSz * 0.16f, {0.68f, 0.45f, 0.24f, 1.0f});
                    } else if (t == 10) { // Brick
                        renderer.drawRect(ix + m, iy + m * 1.2f, iconSz - 2 * m, iconSz - 2.4f * m, {0.68f, 0.30f, 0.22f, 1.0f});
                    } else if (t == 11 || t == 12) { // Concrete / Foam block
                        const float shade = (t == 11) ? 0.58f : 0.76f;
                        renderer.drawRect(ix + m, iy + m * 1.2f, iconSz - 2 * m, iconSz - 2.4f * m, {shade, shade, shade + 0.02f, 1.0f});
                    } else if (t == 0) { // Wall
                        renderer.drawRect(ix + m * 0.8f, iy + m, iconSz - 1.6f * m, iconSz - 2 * m, {0.62f, 0.34f, 0.25f, 1.0f});
                    } else if (t == 1) { // Floor
                        renderer.drawRect(ix + m * 0.7f, iy + iconSz * 0.55f, iconSz - 1.4f * m, iconSz * 0.22f, {0.55f, 0.56f, 0.58f, 1.0f});
                    } else if (t == 2) { // Window
                        renderer.drawBorder(ix + m, iy + m, iconSz - 2 * m, iconSz - 2 * m, 4.0f, {0.68f, 0.69f, 0.63f, 1.0f});
                        renderer.drawRect(ix + m + 4.0f, iy + m + 4.0f, iconSz - 2 * m - 8.0f, iconSz - 2 * m - 8.0f, {0.42f, 0.72f, 0.82f, 0.65f});
                    } else if (t == 3) { // Door
                        renderer.drawRect(ix + iconSz * 0.28f, iy + m * 0.8f, iconSz * 0.44f, iconSz - 1.6f * m, {0.56f, 0.36f, 0.20f, 1.0f});
                        renderer.drawRect(ix + iconSz * 0.63f, iy + iconSz * 0.5f, 5.0f, 5.0f, {0.88f, 0.78f, 0.42f, 1.0f});
                    } else if (t == 8) { // UAZ-469
                        renderer.drawRect(ix + m * 0.8f, iy + iconSz * 0.44f, iconSz - 1.6f * m, iconSz * 0.28f, {0.26f, 0.38f, 0.24f, 1.0f});
                        renderer.drawRect(ix + iconSz * 0.26f, iy + iconSz * 0.26f, iconSz * 0.46f, iconSz * 0.20f, {0.30f, 0.44f, 0.28f, 1.0f});
                    } else if (t == -1) { // FPV drone
                        renderer.drawRect(ix + iconSz * 0.22f, iy + iconSz * 0.46f, iconSz * 0.56f, iconSz * 0.08f, {0.20f, 0.82f, 0.70f, 1.0f});
                        renderer.drawRect(ix + iconSz * 0.46f, iy + iconSz * 0.22f, iconSz * 0.08f, iconSz * 0.56f, {0.20f, 0.82f, 0.70f, 1.0f});
                    } else { // Furniture
                        renderer.drawRect(ix + m, iy + iconSz * 0.36f, iconSz - 2 * m, iconSz * 0.22f, {0.60f, 0.42f, 0.25f, 1.0f});
                        renderer.drawRect(ix + m + 4.0f, iy + iconSz * 0.58f, 6.0f, iconSz * 0.24f, {0.48f, 0.33f, 0.19f, 1.0f});
                        renderer.drawRect(ix + iconSz - m - 10.0f, iy + iconSz * 0.58f, 6.0f, iconSz * 0.24f, {0.48f, 0.33f, 0.19f, 1.0f});
                    }
                }
                const char *cardTitle = i18n::tr(items[k].title);
                const char *cardSubtitle = i18n::tr(items[k].subtitle);
                renderer.drawCenteredText(kx + cardW * 0.5f, ky + cardH - 44.0f, cardTitle,
                                          fitText(cardTitle, 1.65f, cardW - 12.0f), {0.96f, 0.99f, 1.0f, 1.0f});
                renderer.drawCenteredText(kx + cardW * 0.5f, ky + cardH - 20.0f, cardSubtitle,
                                          fitText(cardSubtitle, 1.25f, cardW - 12.0f), {0.68f, 0.80f, 0.84f, 0.95f});
            }
        }

        // Store (shop) modal: weapon skins + gold-coin unlocks.
        if (shopOpen) {
            renderer.drawRect(0.0f, 0.0f, float(hudW), float(hudH), {0.02f, 0.03f, 0.05f, 0.66f});
            const float sw = std::min(760.0f, float(hudW) - 60.0f);
            const float sh = std::min(540.0f, float(hudH) - 60.0f);
            const float sx = (float(hudW) - sw) * 0.5f;
            const float sy = (float(hudH) - sh) * 0.5f;

            renderer.drawRect(sx, sy, sw, sh, {0.08f, 0.11f, 0.15f, 0.97f});
            renderer.drawBorder(sx, sy, sw, sh, 2.0f, {0.72f, 0.58f, 0.16f, 0.98f});
            renderer.drawText(sx + 18.0f, sy + 18.0f, i18n::tr(Str::ShopTitle), 1.85f, {1.00f, 0.92f, 0.55f, 1.0f});

            const std::string balanceText = i18n::format(Str::ShopGold, {std::to_string(static_cast<long long>(shop.coins))});
            renderer.drawText(sx + 18.0f, sy + 44.0f, balanceText, 1.45f, {1.00f, 0.84f, 0.32f, 1.0f});
            if (!onlineHost.empty()) {
                const std::string onlineStatus = i18n::localizeOnlineStatus(online.status());
                renderer.drawText(sx + sw - 18.0f - ekron::font::measureTextWidth(onlineStatus, 1.05f),
                                  sy + 44.0f, onlineStatus, 1.05f, {0.68f, 0.80f, 0.84f, 1.0f});
            }

            // Close button
            renderer.drawRect(sx + sw - 44.0f, sy + 12.0f, 32.0f, 30.0f, {0.42f, 0.14f, 0.16f, 0.95f});
            renderer.drawCenteredText(sx + sw - 28.0f, sy + 27.0f, "X", 1.75f, {1, 1, 1, 1});

            const auto &scats = ekron::desktop::shopCategories();
            const float stabW = (sw - 32.0f - 8.0f) / 2.0f;
            for (size_t t = 0; t < scats.size(); ++t) {
                const bool active = int(t) == selectedShopCategory;
                const float tx = sx + 16.0f + float(t) * (stabW + 8.0f);
                renderer.drawRect(tx, sy + 60.0f, stabW, 32.0f,
                                  active ? ekron::desktop::Color{0.55f, 0.44f, 0.14f, 0.98f}
                                         : ekron::desktop::Color{0.13f, 0.17f, 0.22f, 0.95f});
                renderer.drawBorder(tx, sy + 60.0f, stabW, 32.0f, 1.0f,
                                    active ? ekron::desktop::Color{1.00f, 0.86f, 0.40f, 1.0f}
                                           : ekron::desktop::Color{0.25f, 0.32f, 0.38f, 0.8f});
                const char *shopTabName = ekron::desktop::shopCategoryName(scats[t]);
                renderer.drawCenteredText(tx + stabW * 0.5f, sy + 77.0f, shopTabName,
                                          fitText(shopTabName, 1.45f, stabW - 12.0f), {0.98f, 0.96f, 0.90f, 1.0f});
            }

            const auto &sitems = scats[size_t(selectedShopCategory)].items;
            const float scardW = (sw - 32.0f - 3.0f * 12.0f) / 4.0f;
            const float scardH = sh - 150.0f;
            for (size_t k = 0; k < sitems.size(); ++k) {
                const float kx = sx + 16.0f + float(k) * (scardW + 12.0f);
                const float ky = sy + 104.0f;
                const ekron::desktop::ShopItem &it = sitems[k];
                const bool owned = shop.owns(it.id);
                const bool equipped = it.kind == ekron::desktop::ShopKind::Skin && shop.isEquippedSkin(it.id);
                renderer.drawRect(kx, ky, scardW, scardH,
                                  equipped ? ekron::desktop::Color{0.20f, 0.24f, 0.14f, 0.98f}
                                           : ekron::desktop::Color{0.11f, 0.15f, 0.19f, 0.96f});
                renderer.drawBorder(kx, ky, scardW, scardH, 1.5f,
                                    equipped ? ekron::desktop::Color{1.00f, 0.90f, 0.45f, 1.0f}
                                             : ekron::desktop::Color{0.22f, 0.30f, 0.36f, 0.9f});
                // Icon: skin tint square for skins, gold coin for extras.
                const float iconSz = std::min(scardW - 28.0f, scardH - 110.0f);
                const float ix = kx + (scardW - iconSz) * 0.5f;
                const float iy = ky + 14.0f;
                renderer.drawRect(ix, iy, iconSz, iconSz,
                                  it.kind == ekron::desktop::ShopKind::Skin
                                      ? ekron::desktop::Color{it.tintR, it.tintG, it.tintB, 1.0f}
                                      : ekron::desktop::Color{0.70f, 0.56f, 0.20f, 1.0f});
                renderer.drawBorder(ix, iy, iconSz, iconSz, 1.0f, {0.20f, 0.28f, 0.34f, 0.9f});

                const char *itemName = ekron::desktop::shopItemName(it);
                const char *itemDescription = ekron::desktop::shopItemDescription(it);
                renderer.drawCenteredText(kx + scardW * 0.5f, ky + scardH - 66.0f, itemName,
                                          fitText(itemName, 1.45f, scardW - 10.0f), {0.98f, 0.99f, 1.0f, 1.0f});
                renderer.drawCenteredText(kx + scardW * 0.5f, ky + scardH - 46.0f, itemDescription,
                                          fitText(itemDescription, 1.15f, scardW - 10.0f), {0.70f, 0.80f, 0.84f, 0.95f});

                // Action button
                const float bx = kx + 8.0f, by = ky + scardH - 40.0f, bw = scardW - 16.0f, bh = 30.0f;
                const ekron::desktop::Color bcol = owned
                    ? (equipped ? ekron::desktop::Color{0.35f, 0.60f, 0.40f, 0.98f}
                                : ekron::desktop::Color{0.16f, 0.30f, 0.26f, 0.98f})
                    : ekron::desktop::Color{0.52f, 0.42f, 0.12f, 0.98f};
                renderer.drawRect(bx, by, bw, bh, bcol);
                renderer.drawBorder(bx, by, bw, bh, 1.0f,
                                    owned ? ekron::desktop::Color{0.55f, 0.85f, 0.65f, 1.0f}
                                          : ekron::desktop::Color{1.00f, 0.88f, 0.45f, 1.0f});
                const std::string btnLabel = owned
                    ? std::string(i18n::tr(equipped ? Str::ShopButtonEquipped
                                                    : (it.kind == ekron::desktop::ShopKind::Skin ? Str::ShopButtonEquip
                                                                                                 : Str::ShopButtonActive)))
                    : i18n::format(Str::ShopButtonBuy, {std::to_string(it.price)});
                renderer.drawCenteredText(bx + bw * 0.5f, by + 15.0f, btnLabel, fitText(btnLabel, 1.30f, bw - 8.0f),
                                          {0.98f, 0.99f, 1.0f, 1.0f});
            }
        }

        // Pause menu modal
        if (pauseOpen) {
            renderer.drawRect(0.0f, 0.0f, float(hudW), float(hudH), {0.02f, 0.03f, 0.05f, 0.68f});
            const float pw = kPauseMenuWidth, ph = kPauseMenuHeight;
            const float px = (float(hudW) - pw) * 0.5f;
            const float py = (float(hudH) - ph) * 0.5f;
            renderer.drawRect(px, py, pw, ph, {0.08f, 0.11f, 0.15f, 0.97f});
            renderer.drawBorder(px, py, pw, ph, 2.0f, {0.20f, 0.72f, 0.62f, 0.95f});
            renderer.drawCenteredText(px + pw * 0.5f, py + 25.0f, i18n::tr(Str::PauseTitle), 1.95f, {0.96f, 0.99f, 1.0f, 1.0f});
            if (!onlineHost.empty())
                renderer.drawCenteredText(px + pw * 0.5f, py + 48.0f, i18n::localizeOnlineStatus(online.status()), 1.0f,
                                          {0.68f, 0.80f, 0.84f, 1.0f});

            const std::array<std::string, kPauseMenuButtons> labels = {
                i18n::tr(Str::PauseResume),
                i18n::tr(flying ? Str::FlightOn : Str::FlightOff),
                i18n::tr(Str::PauseGoToRoad),
                i18n::tr(Str::PauseRestoreCar),
                i18n::tr(Str::PauseRespawn),
                i18n::tr(Str::PauseCopySpawn),
                i18n::format(Str::PauseLanguage, {i18n::languageNativeName(i18n::currentLanguage())}),
                i18n::tr(Str::PauseQuit)
            };
            for (size_t i = 0; i < labels.size(); ++i) {
                const float by = py + 62.0f + float(i) * 42.0f;
                const bool quitBtn = (i + 1 == labels.size());
                renderer.drawRect(px + 28.0f, by, pw - 56.0f, 34.0f,
                                  quitBtn ? ekron::desktop::Color{0.40f, 0.13f, 0.15f, 0.95f}
                                          : ekron::desktop::Color{0.14f, 0.20f, 0.25f, 0.95f});
                renderer.drawBorder(px + 28.0f, by, pw - 56.0f, 34.0f, 1.0f,
                                    quitBtn ? ekron::desktop::Color{0.78f, 0.30f, 0.32f, 0.95f}
                                            : ekron::desktop::Color{0.26f, 0.58f, 0.52f, 0.9f});
                renderer.drawCenteredText(px + pw * 0.5f, by + 17.0f, labels[i], fitText(labels[i], 1.45f, pw - 72.0f),
                                          {0.96f, 0.98f, 1.0f, 1.0f});
            }
        }

        console.draw(renderer, hudW, hudH, engine, dt);

        renderer.flushUi(gl, drawW, drawH);
        SDL_GL_SwapWindow(window);
    }

    realms::sandbox::saveUserConfig(engine);
    engine.shutdown();

    // Final synchronous save on clean exit
    {
        sandbox::World saved;
        saved.objects = world.objects;
        if (seatedCar >= 0 && size_t(seatedCar) < saved.objects.size()) {
            saved.objects[size_t(seatedCar)].anchored = carWasAnchored;
        }
        if (tools.holding() && tools.held >= 0 && size_t(tools.held) < saved.objects.size()) {
            saved.objects[size_t(tools.held)] = tools.original;
        }
        saved.save(savePath);
    }

    gameSound.stop(sound.mixer());
    audio.close();
    sound.unregisterCommands(engine);
    droneModel.release(gl);
    remoteModel.release(gl);
    carModel.release(gl);
    mapMode.close(gl, renderer);
    cityModel.release(gl);
    renderer.shutdown(gl);
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#if defined(EKRON_SANDBOX_LIBRARY)
// C entry point of libekron_sandbox (see sys/desktop/ekron_sandbox.h).
// `pascalCore` is the caller's own realms_pascal_get_api: the Realms core
// then runs inside the calling program instead of being loaded from
// librealms_core. No C++ exception may unwind into the (Pascal) caller.
extern "C" __attribute__((visibility("default")))
int ekron_sandbox_run(int argc, char **argv, RealmsGetApiFn pascalCore) {
    realms::Engine::setLinkedPascalCore(pascalCore);
    int rc = 1;
    try {
        rc = sandboxMain(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "[Ekron] sandbox stopped: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[Ekron] sandbox stopped: unknown exception\n";
    }
    std::cout.flush();
    std::cerr.flush();
    realms::Engine::setLinkedPascalCore(nullptr);
    return rc;
}
#endif
