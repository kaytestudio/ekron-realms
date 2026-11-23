// A map compiled by id's qbsp3, qvis3 and qrad3 (tests/realms/maps/q2room.bsp,
// rebuilt by tools/build_q2_test_map.sh): collision, areas and areaportals,
// PVS, qrad lightmaps and light styles, then a walk through it with the game.
//
//   realms_q2_compiled_map_test --map q2room.bsp [--game librealms_game.so] [--pascal librealms_core.so]
//
// Without a game module the game part is skipped and the exit code is 77.
#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/Quake2Module.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

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
float sum(const std::array<float, 3> &c) { return c[0] + c[1] + c[2]; }

// Room A is x -512..0, room B x 64..576 (y -256..256, z 0..256). The door
// (*1, x 16..48) fills the opening in the wall between them and holds
// areaportal 1. The pool in room B is x 384..512, y -128..128, z 0..32.
struct Leaf {
    int32_t contents = -1, cluster = -1, area = -1;
};
Leaf leafAt(const realms::Engine &engine, const realms::Vec3 &p) {
    Leaf l;
    engine.leafInfo(engine.pointLeaf(p), l.contents, l.cluster, l.area);
    return l;
}

void mapChecks(realms::Engine &engine) {
    RealmsMapInfo info{};
    info.struct_size = sizeof(info);
    CHECK(engine.mapInfo(info));
    CHECK(info.num_models == 2);
    CHECK(info.num_areas == 3 && info.num_area_portals == 3); // both include the unused slot 0

    // qbsp3 writes the keys in reverse and numbers the areaportal ("style")
    const std::vector<realms::MapEntity> ents = realms::Engine::parseEntities(engine.mapEntities());
    CHECK(ents.size() == 8);
    int lights = 0;
    for (const auto &e : ents) {
        const std::string cls = realms::Engine::entityValue(e, "classname");
        if (cls == "light") lights++;
        if (cls == "func_areaportal") CHECK(realms::Engine::entityValue(e, "style") == "1");
        if (cls == "func_door") CHECK(realms::Engine::entityValue(e, "model") == "*1");
    }
    CHECK(lights == 3);

    // contents: open air, the walls, the pool, the areaportal brush
    CHECK(engine.pointContents({-256, 0, 64}) == 0);
    CHECK(engine.pointContents({32, 128, 64}) & REALMS_CONTENTS_SOLID);
    CHECK(engine.pointContents({448, 0, 16}) & REALMS_CONTENTS_WATER);
    CHECK(engine.pointContents({448, 0, 48}) == 0);
    CHECK(engine.pointContents({32, 0, 64}) & REALMS_CONTENTS_AREAPORTAL);

    // a player box across both rooms: through the opening (the door is a
    // separate model), stopped by the far wall
    const realms::TraceResult t =
        engine.boxTrace({-384, 0, 25}, {700, 0, 25}, {-16, -16, -24}, {16, 16, 32}, 0, REALMS_MASK_PLAYERSOLID);
    CHECK(!t.startsolid && t.hit());
    CHECK(near(t.endpos[0], 560.0f, 0.5f) && near(t.normal[0], -1.0f, 1e-4f));
    CHECK(t.surface == "e1u1/wall");
    // and onto the floor
    const realms::TraceResult down =
        engine.boxTrace({-384, 0, 100}, {-384, 0, -100}, {-16, -16, -24}, {16, 16, 32}, 0, REALMS_MASK_PLAYERSOLID);
    CHECK(near(down.endpos[2], 24.0f, 0.2f) && near(down.normal[2], 1.0f, 1e-4f));
    // the door model is solid where it stands
    realms::MapModel door;
    CHECK(engine.mapModel(1, door));
    CHECK(engine.pointContents({32, 0, 64}, door.headnode) & REALMS_CONTENTS_SOLID);

    // areas: one per room, joined only while the areaportal is open
    const Leaf a = leafAt(engine, {-256, 0, 64}), b = leafAt(engine, {320, 0, 64});
    CHECK(a.area > 0 && b.area > 0 && a.area != b.area);
    CHECK(engine.areaPortalState(1) == 0);
    CHECK(!engine.areasConnected(a.area, b.area));
    engine.setAreaPortal(1, true);
    CHECK(engine.areasConnected(a.area, b.area) && engine.areasConnected(b.area, a.area));
    engine.setAreaPortal(1, false);
    CHECK(!engine.areasConnected(a.area, b.area));

    // PVS from qvis3: each room sees itself and, through the opening, the other
    CHECK(a.cluster >= 0 && b.cluster >= 0 && a.cluster != b.cluster);
    const std::vector<uint8_t> pvsA = engine.clusterPVS(a.cluster);
    CHECK(pvsA.size() == size_t((info.num_clusters + 7) / 8));
    CHECK(realms::Engine::clusterVisible(pvsA, a.cluster) && realms::Engine::clusterVisible(pvsA, b.cluster));
    const std::vector<uint8_t> phsB = engine.clusterPHS(b.cluster);
    CHECK(realms::Engine::clusterVisible(phsB, a.cluster));
}

void lightingChecks(realms::Engine &engine) {
    realms::MapGeometry geometry;
    std::string error;
    CHECK(realms::loadMapGeometry(engine, geometry, error));
    // qbsp3 gives water SURF_WARP | SURF_TRANS33 (the renderers' warp and
    // translucent pass); nothing in this map is animated
    size_t waterTriangles = 0, animated = 0;
    for (const realms::MapGeometryBatch &b : geometry.batches) {
        if (b.material == "e1u1/water") {
            CHECK(b.surfaceFlags == (REALMS_SURF_WARP | REALMS_SURF_TRANS33));
            waterTriangles += b.triangleCount();
        }
        if (!b.frames.empty()) animated++;
    }
    CHECK(waterTriangles == 36 && animated == 0);
    realms::Quake2Module q2;
    realms::Quake2ModuleConfig config;
    config.startGame = false;
    std::string why;
    q2.open(engine, geometry, config, why);
    CHECK(why.empty() && q2.lightmapped());
    const realms::MapLighting &lit = q2.lighting();
    // 144 faces; qrad3 leaves the 16 warp (water) faces without a lightmap
    CHECK(lit.faceCount() == 144);
    CHECK(lit.litFaces() == 128 && lit.droppedFaces() == 0);

    // R_LightPoint: bright under room A's light, dim in its far corner
    std::array<float, 3> under{}, corner{}, east{}, pool{};
    CHECK(q2.lightAt({-256, 0, 40}, under));
    CHECK(q2.lightAt({-496, -240, 40}, corner));
    CHECK(sum(under) > 2.0f * sum(corner) && under[0] > 0.3f);
    CHECK(near(under[0], under[1], 0.02f) && near(under[1], under[2], 0.02f)); // a white light
    // room B's light is "_color" "1 0.5 0.25"
    CHECK(q2.lightAt({320, 0, 40}, east));
    CHECK(east[0] > 1.5f * east[1] && east[1] > 1.5f * east[2]);
    // above the pool: the water has no lightmap, the light comes from the
    // floor below it
    CHECK(q2.lightAt({448, 0, 64}, pool) && pool[0] > 0.05f);

    // the style 1 light in room A's corner: its faces lose light when style 1
    // is off, room B's don't
    realms::MapLightStyles off = realms::defaultLightStyles();
    off[1] = 0.0f;
    const float nook[3] = {-448, 192, 40}, far[3] = {320, 0, 40};
    float on1[3], off1[3], on2[3], off2[3];
    CHECK(lit.lightPoint(nook, realms::defaultLightStyles(), on1) && lit.lightPoint(nook, off, off1));
    CHECK(on1[0] > off1[0] + 0.05f);
    CHECK(lit.lightPoint(far, realms::defaultLightStyles(), on2) && lit.lightPoint(far, off, off2));
    CHECK(near(on2[0], off2[0], 1e-6f));
    CHECK(lit.stylesDiffer(realms::defaultLightStyles(), off));

    std::vector<float> uvs;
    CHECK(q2.lightmapUvs(geometry, uvs) && uvs.size() == geometry.triangleCount() * 6);
    q2.close();
}

void walkChecks(realms::Engine &engine) {
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    CHECK(view.gameRunning());
    if (!view.gameRunning()) return;
    realms::Quake2Module &q2 = view.module();
    const realms::MapWalker &w = view.session().walker();
    CHECK(near(w.origin[0], -384, 0.2f) && near(w.origin[2], 34, 0.2f));
    CHECK(view.modelPoses().size() == 2);
    CHECK(engine.areaPortalState(1) == 0);

    // walk east: the shard, the door (its trigger field opens it and the
    // areaportal), room B, through the pool to the far wall. The walker only
    // gets RDF_UNDERWATER from the game; the knee-deep pool shows as pmove's
    // water friction instead.
    const float yaw = view.spawnRenderYaw();
    realms::WalkInput in;
    in.forward = 1.0f;
    bool picked = false, portalOpened = false, doorRose = false;
    float dry = 0.0f, wet = 0.0f;
    int dryN = 0, wetN = 0;
    for (int i = 0; i < 120; i++) {
        const float x0 = w.origin[0];
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        const float dx = w.origin[0] - x0;
        if (x0 > 100 && x0 < 300) dry += dx, dryN++;
        if (x0 > 400 && x0 < 500) wet += dx, wetN++;
        for (const std::string &m : view.takeMessages()) picked |= m == "Armor Shard";
        portalOpened |= engine.areaPortalState(1) == 1;
        if (view.modelPoses().size() == 2) doorRose |= view.modelPoses()[1].transform[13] > 1.0f;
    }
    CHECK(picked && q2.stats().armor == 2);
    CHECK(portalOpened && doorRose);
    CHECK(dryN > 0 && wetN > 0);
    if (dryN > 0 && wetN > 0) CHECK(wet / float(wetN) < 0.75f * dry / float(dryN));
    CHECK(w.origin[0] > 500.0f); // past the pool, at the far wall
    CHECK(leafAt(engine, {w.origin[0], w.origin[1], w.origin[2]}).area == leafAt(engine, {320, 0, 64}).area);

    // the door closes behind (wait 3 s) and the game closes the areaportal
    bool portalClosed = false;
    for (int i = 0; i < 120 && !portalClosed; i++) {
        view.update(engine, {}, yaw, 0.0f, 0.05f, false);
        portalClosed = engine.areaPortalState(1) == 0;
    }
    CHECK(portalClosed);

    // the shard lit by room A's lightmap at respawn
    CHECK(view.respawn(engine));
    std::vector<mapview::EntityVertex> ev;
    std::vector<mapview::EntityBatch> eb;
    view.entities(engine, yaw, 0.0f, false, ev, eb);
    bool shardLit = false;
    for (const auto &b : eb)
        if (!b.viewWeapon && b.lit && b.light[0] > 0.1f) shardLit = true;
    CHECK(shardLit);
    view.close();
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::string mapFile, pascalLibrary, gameLibrary;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--map" && i + 1 < argc) mapFile = argv[++i];
        else if (a == "--pascal" && i + 1 < argc) pascalLibrary = argv[++i];
        else if (a == "--game" && i + 1 < argc) gameLibrary = argv[++i];
    }
    if (mapFile.empty()) {
        std::fprintf(stderr, "usage: %s --map q2room.bsp [--game lib] [--pascal lib]\n", argv[0]);
        return 2;
    }
    const fs::path tmp = fs::temp_directory_path() /
        ("realms-q2room-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(tmp / "maps", ec);
    fs::copy_file(mapFile, tmp / "maps" / "q2room.bsp", ec);
    CHECK(!ec);

    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {tmp.string()};
    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.loadMap("maps/q2room.bsp"));
    if (!pascalLibrary.empty()) CHECK(engine.pascalBackend());
    const std::string backend = engine.backendName();

    mapChecks(engine);
    lightingChecks(engine);

    if (!gameLibrary.empty()) {
#if defined(_WIN32)
        _putenv_s("REALMS_GAME_LIBRARY", gameLibrary.c_str());
#else
        setenv("REALMS_GAME_LIBRARY", gameLibrary.c_str(), 1);
#endif
    }
    std::string where, why;
    int status;
    if (realms::GameServer::moduleAvailable({}, &where, &why)) {
        walkChecks(engine);
        std::printf("[q2room] %d checks, %d failures (%s)\n", gChecks, gFailures, backend.c_str());
        status = gFailures ? 1 : 0;
    } else {
        std::printf("[q2room] %d checks, %d failures; game module skipped: %s\n", gChecks, gFailures, why.c_str());
        status = gFailures ? 1 : 77;
    }
    engine.shutdown();
    fs::remove_all(tmp, ec);
    return status;
}
