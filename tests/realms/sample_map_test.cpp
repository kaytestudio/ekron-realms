// The sample glTF map the apps ship (assets/maps/courtyard.glb and its
// skybox env/courtyard_*.tga, built by tools/build_sample_map.py), read from
// the shared assets directory as the apps read it:
//   - the map: brush entities, entities and keys, the water surface;
//   - the skybox: six faces, joined without seams the way MapSky samples them;
//   - with the game: through the bunker's sliding door to the armour, then up
//     the lift to the button that opens the vault.
//
//   realms_sample_map_test --assets <assets dir> [--game librealms_game.so] [--pascal librealms_core.so]
//
// Without a game module the game part is skipped and the exit code is 77.
#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/MapSky.h"
#include "realms/Quake2Module.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
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

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// Inline models in node order (tools/build_sample_map.py)
enum Model { BunkerDoor = 1, Lift = 2, Button = 3, VaultDoor = 4, ModelCount = 5 };

void mapChecks(realms::Engine &engine) {
    const realms::MeshMap &mesh = engine.meshMap();
    CHECK(mesh.models.size() == ModelCount - 1);
    if (mesh.models.size() == ModelCount - 1) {
        CHECK(mesh.models[BunkerDoor - 1].classname == "func_door");
        CHECK(mesh.models[Lift - 1].classname == "func_plat");
        CHECK(mesh.models[Button - 1].classname == "func_button");
        CHECK(mesh.models[VaultDoor - 1].classname == "func_door");
        for (const realms::MeshMapModel &m : mesh.models) CHECK(m.triangles == 10); // boxes without a bottom
    }
    bool water = false;
    for (const realms::MeshMapSurface &s : mesh.surfaces)
        if (s.name == "water") water = (s.contents & REALMS_CONTENTS_WATER) != 0 && !(s.contents & REALMS_CONTENTS_SOLID);
    CHECK(water);

    std::map<std::string, int> count;
    bool vaultDoor = false, button = false, start = false;
    const std::vector<realms::MapEntity> ents = realms::Engine::parseEntities(engine.mapEntities());
    for (const realms::MapEntity &e : ents) {
        const std::string cls = realms::Engine::entityValue(e, "classname");
        count[cls]++;
        if (cls == "func_door" && realms::Engine::entityValue(e, "targetname") == "vault")
            vaultDoor = realms::Engine::entityValue(e, "model") == "*4" && realms::Engine::entityValue(e, "wait") == "-1";
        if (cls == "func_button")
            button = realms::Engine::entityValue(e, "target") == "vault" && realms::Engine::entityValue(e, "angle") == "-2";
        if (cls == "info_player_start")
            start = realms::Engine::entityValue(e, "origin") == "0 -448 24" && realms::Engine::entityValue(e, "angle") == "90";
    }
    CHECK(count["worldspawn"] == 1 && count["info_player_start"] == 1 && count["info_player_deathmatch"] == 3);
    CHECK(count["func_door"] == 2 && count["func_plat"] == 1 && count["func_button"] == 1);
    CHECK(count["item_armor_jacket"] == 1 && count["item_quad"] == 1 && count["misc_explobox"] == 2);
    CHECK(vaultDoor && button && start);
}

void skyChecks(realms::Engine &engine) {
    const realms::MapSkySettings settings = realms::skySettings(engine.mapEntities());
    CHECK(settings.named && settings.name == "courtyard_" && settings.rotate == 0.0f);
    realms::MapSky sky;
    std::string error;
    CHECK(sky.load(engine, settings, &error));
    if (!error.empty()) std::fprintf(stderr, "  sky: %s\n", error.c_str());
    CHECK(sky.size() == 512);
    if (sky.size() != 512) return;
    const int n = sky.size();
    auto texel = [&](const float d[3]) {
        int layer;
        float u, v;
        realms::MapSky::faceCoords(d, layer, u, v);
        const int x = std::min(n - 1, int(u * float(n))), y = std::min(n - 1, int(v * float(n)));
        return sky.pixels().data() + (size_t(layer) * size_t(n) * size_t(n) + size_t(y) * size_t(n) + size_t(x)) * 4;
    };
    const float up[3] = {0, 0, 1}, down[3] = {0, 0, -1};
    const uint8_t *zenith = texel(up), *ground = texel(down);
    CHECK(zenith[2] > 150 && zenith[2] > zenith[0] + 60);              // blue overhead
    CHECK(ground[0] < 100 && ground[1] < 100 && ground[2] < 100);      // dark ground below
    // Directions straight across each face edge: the faces must join (the
    // generator inverts faceCoords). Count matching texels on all 12 edges.
    int pairs = 0, matching = 0;
    const float eps = 1e-3f;
    for (int axis = 0; axis < 3; axis++)
        for (int other = 0; other < 3; other++) {
            if (other == axis) continue;
            const int free = 3 - axis - other;
            for (float sa : {-1.0f, 1.0f})
                for (float so : {-1.0f, 1.0f})
                    for (int i = 0; i < 64; i++) {
                        float a[3], b[3];
                        a[axis] = sa;
                        a[other] = so * (1.0f - eps);
                        a[free] = -0.98f + 1.96f * float(i) / 63.0f;
                        b[axis] = sa * (1.0f - eps);
                        b[other] = so;
                        b[free] = a[free];
                        const uint8_t *p = texel(a), *q = texel(b);
                        pairs++;
                        matching += std::abs(int(p[0]) - int(q[0])) + std::abs(int(p[1]) - int(q[1])) +
                                        std::abs(int(p[2]) - int(q[2])) <= 30;
                    }
        }
    CHECK(pairs == 3 * 2 * 4 * 64 && matching >= pairs * 95 / 100);
    std::printf("[courtyard] sky edges: %d of %d texel pairs match\n", matching, pairs);
}

// Turns the walker towards map (x, y) each frame and runs there.
bool walkTo(realms::Engine &engine, mapview::Quake2View &view, float x, float y, float radius, int frames,
            std::vector<std::string> &messages) {
    const realms::MapWalker &w = view.session().walker();
    realms::WalkInput in;
    in.forward = 1.0f;
    for (int i = 0; i < frames; i++) {
        const float dx = x - w.origin[0], dy = y - w.origin[1];
        if (std::sqrt(dx * dx + dy * dy) <= radius) return true;
        const float yaw = mapview::renderYawFromMap(std::atan2(dy, dx) * 57.29578f);
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        for (std::string &m : view.takeMessages()) messages.push_back(std::move(m));
    }
    return false;
}

bool heard(const std::vector<std::string> &messages, const std::string &text) {
    for (const std::string &m : messages)
        if (m == text) return true;
    return false;
}

void gameChecks(realms::Engine &engine) {
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    CHECK(view.gameRunning());
    if (!view.gameRunning()) return;
    realms::Quake2Module &q2 = view.module();
    CHECK(!q2.sky().empty() && q2.sky().settings().name == "courtyard_");
    CHECK(view.modelPoses().size() == ModelCount);
    if (view.modelPoses().size() != ModelCount) return;
    const realms::MapWalker &w = view.session().walker();
    CHECK(near(w.origin[0], 0, 0.5f) && near(w.origin[1], -448, 0.5f));

    // Brush entity bounds include the 2-unit slabs (1 unit each side, as qbsp
    // pads inline models), so a move is the box size + 2 - lip.
    // The lift waits at the bottom: 96 + 2 - 8 = 90 units = 2.8125 m down.
    for (int i = 0; i < 4; i++) view.update(engine, {}, view.spawnRenderYaw(), 0.0f, 0.1f, false);
    CHECK(near(view.modelPoses()[Lift].transform[13], -2.8125f, 0.01f));
    CHECK(near(view.modelPoses()[VaultDoor].transform[14], 0.0f, 1e-4f));

    // Straight ahead: the bunker door slides open (+X, 64 + 2 - 8 units) and the
    // jacket armour lies inside.
    std::vector<std::string> messages;
    float slide = 0.0f;
    realms::WalkInput in;
    in.forward = 1.0f;
    const float yaw = view.spawnRenderYaw();
    for (int i = 0; i < 200 && w.origin[1] < 430.0f; i++) {
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        slide = std::max(slide, view.modelPoses()[BunkerDoor].transform[12]);
        for (std::string &m : view.takeMessages()) messages.push_back(std::move(m));
    }
    CHECK(slide > 1.7f);
    CHECK(w.origin[1] > 300.0f); // inside the bunker
    CHECK(heard(messages, "Jacket Armor") && q2.stats().armor == 25);

    // From the spawn again: onto the lift, ride it up, press the button.
    CHECK(view.respawn(engine) && view.gameRunning());
    messages.clear();
    CHECK(walkTo(engine, view, 288, 32, 12, 200, messages));            // on the lowered lift
    bool up = false;
    for (int i = 0; i < 80 && !up; i++) {                               // the plat carries the player
        view.update(engine, {}, yaw, 0.0f, 0.05f, false);
        up = w.origin[2] > 96.0f + 20.0f;
    }
    CHECK(up);
    CHECK(walkTo(engine, view, 416, 32, 12, 120, messages));            // across the platform
    CHECK(walkTo(engine, view, 480, 96, 10, 120, messages));            // onto the button
    float vault = 0.0f;
    for (int i = 0; i < 80; i++) {
        view.update(engine, {}, yaw, 0.0f, 0.05f, false);
        for (std::string &m : view.takeMessages()) messages.push_back(std::move(m));
        vault = std::min(vault, view.modelPoses()[VaultDoor].transform[14]);
    }
    CHECK(heard(messages, "The vault is open."));
    CHECK(near(vault, -1.8125f, 0.01f)); // slid 58 units towards Quake +Y and stays open
    CHECK(view.gameRunning());
    view.close();
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::string assets, pascalLibrary, gameLibrary;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--assets" && i + 1 < argc) assets = argv[++i];
        else if (a == "--pascal" && i + 1 < argc) pascalLibrary = argv[++i];
        else if (a == "--game" && i + 1 < argc) gameLibrary = argv[++i];
    }
    if (assets.empty()) {
        std::fprintf(stderr, "usage: %s --assets <dir> [--game lib] [--pascal lib]\n", argv[0]);
        return 2;
    }
    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {assets};
    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.loadMap("maps/courtyard.glb"));
    if (!pascalLibrary.empty()) CHECK(engine.pascalBackend());
    const std::string backend = engine.backendName();

    mapChecks(engine);
    skyChecks(engine);

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
        gameChecks(engine);
        std::printf("[courtyard] %d checks, %d failures (%s)\n", gChecks, gFailures, backend.c_str());
        status = gFailures ? 1 : 0;
    } else {
        std::printf("[courtyard] %d checks, %d failures; game module skipped: %s\n", gChecks, gFailures, why.c_str());
        status = gFailures ? 1 : 77;
    }
    engine.shutdown();
    return status;
}
