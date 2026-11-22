// Quake2Module and Quake2View on a generated, lightmapped arena.
//
//   realms_quake2_module_test [--game librealms_game.so|linked] [--pascal librealms_core.so|static]
//
// "linked" needs -DREALMS_GAME_STATIC and librealms_game.a. Without a game
// module the game part is skipped and the exit code is 77.
#include "bsp_builder.h"
#include "md2_builder.h"

#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/Quake2Module.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(REALMS_GAME_STATIC)
extern "C" q2game_export_t *GetGameAPI(q2game_import_t *import);
#endif

namespace {

int gChecks = 0, gFailures = 0;

#define CHECK(cond)                                                                                                \
    do {                                                                                                           \
        ++gChecks;                                                                                                 \
        if (!(cond)) {                                                                                             \
            ++gFailures;                                                                                           \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                                          \
    } while (0)

namespace fs = std::filesystem;

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// shows how far a crashed run got (stdout is line buffered)
void step(const char *what) { std::printf("[module] step: %s\n", what); }

// A stand-in for id's blaster view model (the shard stays a fallback box).
void writeBlaster(const fs::path &dir) {
    auto gun = md2_builder::boxModel({{{0, -4, -4}, {24, 4, 4}}});
    const std::vector<unsigned char> bytes = md2_builder::writeMd2(gun);
    const fs::path path = dir / "models/weapons/v_blast/tris.md2";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// The floor's light: red 128, green 64, blue 32 everywhere (style slot 0,
// "normal"); slot 1 (style 1, FLICKER in the game's worldspawn) adds blue.
std::array<uint8_t, 3> arenaLight(const float *, int slot) {
    if (slot == 1) return {0, 0, 64};
    return {128, 64, 32};
}

// 1024 x 1024 room lit by qrad-style lightmaps, a door (*1) in the middle,
// an armor shard on the way to it, the player start at -320 0 facing +X.
bsp_builder::MapDef arena() {
    bsp_builder::MapDef def;
    def.texinfo = {{"e1u1/floor", 0, 0}, {"e1u1/wall", 0, 0}, {"e1u1/door", 0, 0}};
    def.world.brushes = {
        {{-512, -512, -64}, {512, 512, 0}, REALMS_CONTENTS_SOLID, 0},   // floor
        {{-528, -512, 0}, {-512, 512, 256}, REALMS_CONTENTS_SOLID, 1},  // walls
        {{512, -512, 0}, {528, 512, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, -528, 0}, {528, -512, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, 512, 0}, {528, 528, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, -528, 256}, {528, 528, 272}, REALMS_CONTENTS_SOLID, 1}, // ceiling
    };
    bsp_builder::ModelDef door;
    door.brushes = {{{0, -64, 0}, {16, 64, 128}, REALMS_CONTENTS_SOLID, 2}};
    def.inlineModels.push_back(door);
    def.entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Realms module arena\"\n}\n"
                   "{\n\"classname\" \"info_player_start\"\n\"origin\" \"-320 0 24\"\n\"angle\" \"0\"\n}\n"
                   "{\n\"classname\" \"item_armor_shard\"\n\"origin\" \"-160 0 16\"\n}\n"
                   "{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n\"angle\" \"-1\"\n\"speed\" \"200\"\n}\n";
    def.emitFaces = true;
    def.light = arenaLight;
    def.faceStyles = {{0, 1, 255, 255}};
    return def;
}

void moduleWithoutGame(realms::Engine &engine) {
    realms::MapGeometry geometry;
    std::string error;
    CHECK(realms::loadMapGeometry(engine, geometry, error));
    CHECK(geometry.source == realms::MapGeometrySource::Faces);

    realms::Quake2Module q2;
    realms::Quake2ModuleConfig config;
    config.startGame = false;
    std::string why = "stale";
    q2.open(engine, geometry, config, why);
    CHECK(why.empty());
    CHECK(!q2.gameRunning() && q2.game() == nullptr);
    CHECK(q2.lightmapped());
    CHECK(q2.lighting().width() > 0 && q2.lighting().height() > 0);
    CHECK(q2.lightmap().size() == size_t(q2.lighting().width()) * size_t(q2.lighting().height()) * 4);

    std::vector<float> uvs;
    CHECK(q2.lightmapUvs(geometry, uvs));
    CHECK(uvs.size() == geometry.triangleCount() * 3 * 2);
    size_t inside = 0;
    for (size_t i = 0; i + 1 < uvs.size(); i += 2)
        if (uvs[i] >= 0.0f && uvs[i] <= 1.0f && uvs[i + 1] >= 0.0f && uvs[i + 1] <= 1.0f) inside++;
    CHECK(inside == uvs.size() / 2); // every face of this map is lit

    // R_LightPoint on the floor: both styles at "normal" (1.0), no game
    std::array<float, 3> rgb{};
    CHECK(q2.lightAt({-200, 0, 40}, rgb));
    CHECK(near(rgb[0], 128.0f / 255.0f, 1e-3f) && near(rgb[1], 64.0f / 255.0f, 1e-3f) &&
          near(rgb[2], 96.0f / 255.0f, 1e-3f));
    CHECK(!q2.lightAt({2000, 2000, 40}, rgb) || rgb[0] >= 0.1f);
    CHECK(!q2.updateLightStyles()); // no game: the styles never change

    // nothing from a game that isn't there
    CHECK(q2.status().empty());
    CHECK(q2.modelPoses().empty());
    CHECK(q2.takeSounds().empty() && q2.loopSounds().empty() && q2.takeMessages().empty());
    CHECK(q2.buildScene(engine, {0, 0, 0}, {0, 0, 0}, true).batches.empty());
    std::vector<realms::Quake2EffectVertex> quads;
    std::vector<realms::GameDynamicLight> lights;
    q2.buildEffects({0, 0, 0}, {0, 0, 0}, 8, quads, lights);
    CHECK(quads.empty() && lights.empty());
    q2.frame({}, 0.1);
    CHECK(q2.takeGameFailure().empty());

    // the walk input -> usercmd mapping (always run, absolute angles)
    realms::WalkInput in;
    in.forward = 1.0f;
    in.right = -0.5f;
    in.jump = true;
    in.yaw = 90.0f;
    in.pitch = -10.0f;
    const realms::GameUserCmd cmd = realms::Quake2Module::userCmd(in, true);
    CHECK(near(cmd.forward, 400, 1e-3f) && near(cmd.side, -200, 1e-3f) && near(cmd.up, 400, 1e-3f));
    CHECK(near(cmd.yaw, 90, 1e-3f) && near(cmd.pitch, -10, 1e-3f) && cmd.attack);
    in.jump = false;
    in.crouch = true;
    in.forward = 3.0f; // clamped
    CHECK(near(realms::Quake2Module::userCmd(in, false).up, -400, 1e-3f));
    CHECK(near(realms::Quake2Module::userCmd(in, false).forward, 400, 1e-3f));

    q2.disableLightmaps();
    CHECK(!q2.lightmapped() && q2.lightmap().empty() && !q2.lightmapUvs(geometry, uvs) && uvs.empty());
    q2.close();
    CHECK(!q2.lightmapped());
}

void viewWithoutGame(realms::Engine &engine) {
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    options.startGame = false;
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    CHECK(view.active() && !view.gameRunning());
    std::vector<sandbox::MeshVertex> vertices;
    std::vector<mapview::ModelRange> ranges;
    std::vector<float> lightUvs;
    view.triangles(geometry, vertices, ranges, lightUvs);
    CHECK(!vertices.empty());
    CHECK(ranges.size() == 2 && ranges[0].model == 0 && ranges[1].model == 1);
    if (ranges.size() == 2) {
        CHECK(ranges[0].first == 0 && ranges[1].first == ranges[0].count);
        CHECK(size_t(ranges[1].first + ranges[1].count) == vertices.size());
        CHECK(ranges[1].count == 36); // the door: a box, 12 triangles
    }
    CHECK(lightUvs.size() == vertices.size() * 2);

    // walking: the walker moves the camera (render space: map +X is render +X)
    const physics::Vec3 start = view.eye();
    const float yaw = view.spawnRenderYaw(); // facing +X
    realms::WalkInput in;
    in.forward = 1.0f;
    for (int i = 0; i < 30; i++) view.update(engine, in, yaw, 0.0f, 1.0f / 30.0f, false);
    const physics::Vec3 moved = view.eye();
    CHECK(moved.x > start.x + 1.0f);
    CHECK(near(moved.z, start.z, 0.1f));
    CHECK(view.modelPoses().empty());
    std::vector<mapview::EntityVertex> ev;
    std::vector<mapview::EntityBatch> eb;
    view.entities(engine, yaw, 0.0f, true, ev, eb);
    CHECK(ev.empty() && eb.empty());
    CHECK(!view.noclip());
    view.toggleNoclip();
    CHECK(view.noclip());
    CHECK(view.respawn(engine));
    CHECK(near(view.eye().x, start.x, 0.05f));
    view.close();
    CHECK(!view.active());
}

bool hasSound(const std::vector<realms::GameEvent> &events, const std::string &name) {
    for (const auto &e : events)
        if (e.kind == realms::GameEvent::Kind::Sound && e.text == name) return true;
    return false;
}

void viewWithGame(realms::Engine &engine) {
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options; // startGame
    realms::MapGeometry geometry;
    std::string error;
    step("open with game");
    CHECK(view.open(engine, options, geometry, error));
    CHECK(view.gameRunning());
    if (!view.gameRunning()) return;
    std::printf("[module] game: %s\n", view.module().game()->moduleOrigin().c_str());
    CHECK(view.session().summary().find(", game") != std::string::npos);
    realms::Quake2Module &q2 = view.module();
    CHECK(q2.status() == "HP 100  Armor 0  Ammo 0" || q2.status().rfind("HP 100  Armor 0", 0) == 0);
    // the walker mirrors the game's player: spawn + 9 + 1, eye 22 above
    const realms::MapWalker &w = view.session().walker();
    CHECK(near(w.origin[0], -320, 0.2f) && near(w.origin[2], 34, 0.2f));
    const physics::Vec3 eye0 = view.eye();
    CHECK(near(eye0.x, -320.0f / 32.0f, 0.01f) && near(eye0.y, (34.0f + 22.0f) / 32.0f, 0.05f));
    CHECK(near(view.spawnRenderYaw(), mapview::renderYawFromMap(0.0f), 1e-3f));

    // the door is model 1, visible, at rest
    CHECK(view.modelPoses().size() == 2);
    if (view.modelPoses().size() == 2) {
        CHECK(view.modelPoses()[1].visible);
        CHECK(near(view.modelPoses()[1].transform[13], 0.0f, 1e-4f));
    }

    // entities: the shard (no model data: a fallback box) and the view weapon
    step("entities");
    const float yaw = view.spawnRenderYaw();
    std::vector<mapview::EntityVertex> ev;
    std::vector<mapview::EntityBatch> eb;
    view.entities(engine, yaw, 0.0f, true, ev, eb);
    bool shard = false, weapon = false, inRange = true;
    for (const auto &b : eb) {
        inRange &= b.first >= 0 && b.count > 0 && size_t(b.first + b.count) <= ev.size();
        if (b.viewWeapon) weapon = true;
        // placed in render space: map (-160, 0, 15) -> (-5, 0.47, 0)
        if (!b.viewWeapon && near(b.transform[12], -5.0f, 0.01f) && near(b.transform[14], 0.0f, 0.01f)) {
            shard = true;
            CHECK(b.fallbackBox);
            CHECK(b.lit && near(b.light[0], 128.0f / 255.0f, 0.02f)); // lit from the floor's lightmap
        }
    }
    CHECK(inRange && shard && weapon);
    view.entities(engine, yaw, 0.0f, false, ev, eb);
    for (const auto &b : eb) CHECK(!b.viewWeapon); // cl_gun 0

    step("walk");
    // walk +X: pick up the shard (message + sound), then open the door
    realms::WalkInput in;
    in.forward = 1.0f;
    bool picked = false, pickupSound = false, styleChanged = false, doorMoved = false;
    for (int i = 0; i < 40; i++) {
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        for (const std::string &m : view.takeMessages()) picked |= m == "Armor Shard";
        pickupSound |= hasSound(q2.takeSounds(), "sound/misc/ar2_pkup.wav");
        styleChanged |= q2.updateLightStyles();
        if (view.modelPoses().size() == 2 && view.modelPoses()[1].transform[13] > 0.1f) doorMoved = true;
    }
    CHECK(picked);
    CHECK(pickupSound);
    CHECK(styleChanged); // style 1 flickers (worldspawn's CS_LIGHTS + 1)
    CHECK(doorMoved);    // the door rises (render +Y) as the game moves it
    CHECK(view.session().walker().origin[0] > -250.0f);
    CHECK(q2.stats().armor == 2);

    // firing: the blaster's muzzle flash lights the view and the view weapon
    // (yellow, through R_LightPoint), its sound plays
    step("fire");
    auto weaponLight = [&](std::array<float, 3> &rgb) {
        view.entities(engine, yaw, 0.0f, true, ev, eb);
        for (const auto &b : eb)
            if (b.viewWeapon && b.lit) {
                rgb = b.light;
                return true;
            }
        return false;
    };
    std::array<float, 3> gunBefore{}, gunFlash{};
    CHECK(weaponLight(gunBefore));
    view.update(engine, {}, yaw, 0.0f, 0.1f, true);
    std::vector<mapview::EffectVertex> quads;
    std::vector<mapview::DynamicLight> lights;
    view.effects(yaw, 0.0f, 1, quads, lights);
    CHECK(lights.size() == 1);
    if (!lights.empty()) CHECK(lights[0].radius > 1.0f); // metres
    CHECK(weaponLight(gunFlash));
    // red and green only come from style 0 ("normal"); style 1 flickers blue
    CHECK(gunFlash[0] > gunBefore[0] + 0.3f);
    CHECK(near(gunFlash[0] - gunBefore[0], gunFlash[1] - gunBefore[1], 1e-3f));
    CHECK(hasSound(q2.takeSounds(), "sound/weapons/blastf1a.wav"));
    for (int i = 0; i < 5; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    const realms::GameListener listener = q2.listener();
    CHECK(listener.entity == 1 && near(listener.origin[0], view.session().walker().origin[0], 0.5f));

    // noclip through the game (spectator pmove), and back
    step("noclip");
    view.toggleNoclip();
    view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(view.noclip() && view.session().walker().noclip);
    view.toggleNoclip();
    view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(!view.noclip());

    step("respawn");
    // respawn: the game restarts on the same map (the shard is back)
    CHECK(view.respawn(engine));
    CHECK(view.gameRunning());
    CHECK(near(view.session().walker().origin[0], -320, 0.2f));
    CHECK(q2.stats().armor == 0);
    step("close");
    view.close();
    CHECK(!view.active() && !view.gameRunning());
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::string pascalLibrary, gameLibrary;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--pascal" && i + 1 < argc) pascalLibrary = argv[++i];
        else if (a == "--game" && i + 1 < argc) gameLibrary = argv[++i];
    }
    const fs::path tmp = fs::temp_directory_path() /
        ("realms-q2module-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         (pascalLibrary.empty() ? "native" : "pascal"));
    std::error_code ec;
    fs::create_directories(tmp / "maps", ec);
    {
        const std::string bsp = bsp_builder::Builder(arena()).build();
        std::ofstream(tmp / "maps" / "arena.bsp", std::ios::binary).write(bsp.data(), std::streamsize(bsp.size()));
    }
    writeBlaster(tmp);
    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {tmp.string()};
    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.loadMap("maps/arena.bsp"));
    const std::string backend = engine.backendName();
    if (!pascalLibrary.empty()) CHECK(engine.pascalBackend());

    step("module without game");
    moduleWithoutGame(engine);
    step("view without game");
    viewWithoutGame(engine);

    if (gameLibrary == "linked") {
#if defined(REALMS_GAME_STATIC)
        realms::GameServer::setLinkedGame(&GetGameAPI);
#else
        std::fprintf(stderr, "--game linked needs a build with -DREALMS_GAME_STATIC\n");
        return 2;
#endif
    } else if (!gameLibrary.empty()) {
#if defined(_WIN32)
        _putenv_s("REALMS_GAME_LIBRARY", gameLibrary.c_str());
#else
        setenv("REALMS_GAME_LIBRARY", gameLibrary.c_str(), 1);
#endif
    }
    std::string where, why;
    int status = 0;
    if (realms::GameServer::moduleAvailable({}, &where, &why)) {
        viewWithGame(engine);
        std::printf("[module] %d checks, %d failures (%s)\n", gChecks, gFailures, backend.c_str());
        status = gFailures ? 1 : 0;
    } else {
        std::printf("[module] %d checks, %d failures; game module skipped: %s\n", gChecks, gFailures, why.c_str());
        status = gFailures ? 1 : 77;
    }
    step("shutdown");
    engine.shutdown();
    fs::remove_all(tmp, ec);
    step("exit");
    return status;
}
