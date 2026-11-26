// Quake2Module and Quake2View on a generated, lightmapped arena.
//
//   realms_quake2_module_test [--game librealms_game.so|linked] [--pascal librealms_core.so|static]
//
// "linked" needs -DREALMS_GAME_STATIC and librealms_game.a. Without a game
// module the game part is skipped and the exit code is 77.
#include "bsp_builder.h"
#include "gltf_builder.h"
#include "md3_builder.h"

#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/GameDemo.h"
#include "realms/GameHudCanvas.h"
#include "realms/GameNetClient.h"
#include "realms/GameNetServer.h"
#include "realms/GameSave.h"
#include "realms/SandboxBoot.h"
#include "realms/MapSky.h"
#include "realms/MapTextures.h"
#include "realms/Quake2Module.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
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
    auto gun = md3_builder::boxModel({{{0, -4, -4}, {24, 4, 4}}});
    const std::vector<unsigned char> bytes = md3_builder::writeMd3(gun);
    const fs::path path = dir / "models/weapons/v_blast/tris.md3";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// A .wal (miptex_t header, then mip 0 only) whose texel (x, y) has palette
// index pixel(x, y).
template <class F> std::vector<unsigned char> walBytes(uint32_t w, uint32_t h, F pixel) {
    std::vector<unsigned char> bytes(100 + size_t(w) * h, 0);
    auto put32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; i++) bytes[at + size_t(i)] = uint8_t(v >> (8 * i));
    };
    put32(32, w);
    put32(36, h);
    put32(40, 100);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) bytes[100 + size_t(y) * w + x] = uint8_t(pixel(x, y));
    return bytes;
}

template <class F> void writeWal(const fs::path &dir, const std::string &name, uint32_t w, uint32_t h, F pixel) {
    const std::vector<unsigned char> bytes = walBytes(w, h, pixel);
    const fs::path path = dir / "textures" / (name + ".wal");
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

bool rgbIs(const uint8_t *p, int r, int g, int b, int tolerance = 0) {
    return std::abs(p[0] - r) <= tolerance && std::abs(p[1] - g) <= tolerance && std::abs(p[2] - b) <= tolerance &&
           p[3] == 255;
}

// decodeWal and resampleTiled on their own.
void walDecoding() {
    const realms::Quake2Palette &pal = realms::builtinQuake2Palette();
    CHECK(pal[0xd0 * 3] == 0 && pal[0xd0 * 3 + 1] == 255 && pal[0xd0 * 3 + 2] == 0);
    realms::RgbaImage img;
    std::string error;
    const auto wal = walBytes(4, 2, [](uint32_t x, uint32_t y) { return y == 0 ? (x == 0 ? 0xd0 : 0x0f) : 0xf0; });
    CHECK(realms::decodeWal(wal.data(), wal.size(), pal, img, error));
    CHECK(img.width == 4 && img.height == 2 && img.rgba.size() == 4 * 2 * 4);
    if (img.rgba.size() == 32) {
        CHECK(rgbIs(&img.rgba[0], 0, 255, 0));      // 0xd0
        CHECK(rgbIs(&img.rgba[4], 235, 235, 235));  // 0x0f
        CHECK(rgbIs(&img.rgba[16], 239, 0, 0));     // 0xf0, second row
    }
    CHECK(!realms::decodeWal(wal.data(), 99, pal, img, error) && !error.empty() && img.rgba.empty());
    CHECK(!realms::decodeWal(wal.data(), wal.size() - 1, pal, img, error)); // pixels cut short
    auto zero = wal;
    zero[32] = 0; // width 0
    CHECK(!realms::decodeWal(zero.data(), zero.size(), pal, img, error));
    auto far = wal;
    far[41] = 1; // mip 0 at 356, past the end
    CHECK(!realms::decodeWal(far.data(), far.size(), pal, img, error));

    // enlarging wraps: 2 x 1 (black, white) to 4 x 4, texel centres at 0.25,
    // 0.75 of a source texel; the left column is 3/4 black, 1/4 white (wrapped)
    const uint8_t two[8] = {0, 0, 0, 255, 255, 255, 255, 255};
    std::vector<uint8_t> big(4 * 4 * 4);
    realms::resampleTiled(two, 2, 1, 4, big.data());
    CHECK(rgbIs(&big[0], 64, 64, 64, 1) && rgbIs(&big[4], 64, 64, 64, 1));
    CHECK(rgbIs(&big[8], 191, 191, 191, 1) && rgbIs(&big[12], 191, 191, 191, 1));
    CHECK(rgbIs(&big[3 * 16], 64, 64, 64, 1)); // every row the same
    // shrinking averages: 4 x 4 columns 0, 100, 200, 40 to 2 x 2
    std::vector<uint8_t> four(4 * 4 * 4);
    const int cols[4] = {0, 100, 200, 40};
    for (int i = 0; i < 16; i++) four[size_t(i) * 4] = four[size_t(i) * 4 + 1] = four[size_t(i) * 4 + 2] = uint8_t(cols[i % 4]),
                                  four[size_t(i) * 4 + 3] = 255;
    std::vector<uint8_t> small(2 * 2 * 4);
    realms::resampleTiled(four.data(), 4, 4, 2, small.data());
    CHECK(rgbIs(&small[0], 50, 50, 50) && rgbIs(&small[4], 120, 120, 120) && rgbIs(&small[8], 50, 50, 50));
}

// A TGA of one colour: type 2 (24-bit, bottom row first) or 10 (RLE, 32-bit,
// top row first, in runs of at most 128).
std::vector<unsigned char> tgaBytes(int w, int h, uint8_t r, uint8_t g, uint8_t b, bool rle) {
    std::vector<unsigned char> t(18, 0);
    t[2] = rle ? 10 : 2;
    t[12] = uint8_t(w), t[13] = uint8_t(w >> 8), t[14] = uint8_t(h), t[15] = uint8_t(h >> 8);
    t[16] = rle ? 32 : 24;
    t[17] = rle ? 0x28 : 0x00; // top-down, 8 alpha bits
    for (int left = w * h; left > 0;) {
        if (rle) {
            const int run = std::min(left, 128);
            t.insert(t.end(), {uint8_t(0x80 | (run - 1)), b, g, r, 255});
            left -= run;
        } else {
            t.insert(t.end(), {b, g, r});
            left--;
        }
    }
    return t;
}

void writeFileBytes(const fs::path &path, const std::vector<unsigned char> &bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// env/testsky_<face>: one colour each; dn is a .pcx (index 200 = 200 55 100).
const uint8_t kSkyColours[5][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {0, 255, 255}};
void writeSky(const fs::path &dir) {
    for (int i = 0; i < 5; i++)
        writeFileBytes(dir / "env" / (std::string("testsky_") + realms::MapSky::kSuffixes[i] + ".tga"),
                       tgaBytes(8, 8, kSkyColours[i][0], kSkyColours[i][1], kSkyColours[i][2], i % 2 == 1));
    writeFileBytes(dir / "env/testsky_dn.pcx", md3_builder::writePcx(8, 8, std::vector<uint8_t>(64, 200)));
    // SP_worldspawn's default, grey: maps without "sky" must only get it when they are BSP maps
    for (const char *suffix : realms::MapSky::kSuffixes)
        writeFileBytes(dir / "env" / (std::string("unit1_") + suffix + ".tga"), tgaBytes(4, 4, 90, 90, 90, false));
}

// decodeTga, skySettings, the face lookup against R_DrawSkyBox, renderToSky.
void skyMath() {
    // a 2 x 2 TGA, bottom row first: red green / blue white on screen
    std::vector<unsigned char> tga(18, 0);
    tga[2] = 2, tga[12] = 2, tga[14] = 2, tga[16] = 24;
    tga.insert(tga.end(), {255, 0, 0, 255, 255, 255, 0, 0, 255, 0, 255, 0}); // BGR: blue white, red green
    realms::RgbaImage img;
    std::string error;
    CHECK(realms::decodeTga(tga.data(), tga.size(), img, error));
    CHECK(img.width == 2 && img.height == 2 && img.rgba.size() == 16);
    if (img.rgba.size() == 16) {
        CHECK(rgbIs(&img.rgba[0], 255, 0, 0) && rgbIs(&img.rgba[4], 0, 255, 0));
        CHECK(rgbIs(&img.rgba[8], 0, 0, 255) && rgbIs(&img.rgba[12], 255, 255, 255));
    }
    CHECK(!realms::decodeTga(tga.data(), tga.size() - 1, img, error) && error == "pixels cut short");
    const auto rle = tgaBytes(20, 10, 1, 2, 3, true); // 200 pixels: runs of 128 and 72
    CHECK(realms::decodeTga(rle.data(), rle.size(), img, error) && img.width == 20 && img.height == 10);
    CHECK(img.rgba.size() == 800 && rgbIs(&img.rgba[796], 1, 2, 3));
    CHECK(!realms::decodeTga(rle.data(), rle.size() - 3, img, error));
    auto mapped = tga;
    mapped[2] = 1; // colour-mapped
    CHECK(!realms::decodeTga(mapped.data(), mapped.size(), img, error));

    realms::MapSkySettings s = realms::skySettings("{\n\"classname\" \"worldspawn\"\n}\n");
    CHECK(s.name == "unit1_" && s.rotate == 0.0f && !s.named);
    s = realms::skySettings("{\n\"sky\" \"space1_\"\n\"skyrotate\" \"-4.5\"\n\"skyaxis\" \"0 1 2\"\n"
                            "\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"light\"\n\"sky\" \"x\"\n}\n");
    CHECK(s.name == "space1_" && near(s.rotate, -4.5f, 1e-6f) && s.axis[0] == 0 && s.axis[1] == 1 && s.axis[2] == 2);
    CHECK(s.named);

    // MakeSkyVec(s, t, axis): every point of every face maps back
    const int stToVec[6][3] = {{3, -1, 2}, {-3, 1, 2}, {1, 3, 2}, {-1, -3, 2}, {-2, -1, 3}, {2, -1, -3}};
    int ok = 0, total = 0;
    for (int axis = 0; axis < 6; axis++)
        for (float st0 = -0.9f; st0 < 0.95f; st0 += 0.3f)
            for (float st1 = -0.9f; st1 < 0.95f; st1 += 0.3f) {
                const float b[3] = {st0 * 2300, st1 * 2300, 2300};
                float v[3];
                for (int j = 0; j < 3; j++) {
                    const int k = stToVec[axis][j];
                    v[j] = k < 0 ? -b[-k - 1] : b[k - 1];
                }
                int layer = -1;
                float u = 0, vv = 0;
                realms::MapSky::faceCoords(v, layer, u, vv);
                total++;
                if (layer == axis && near(u, (st0 + 1) * 0.5f, 1e-5f) && near(vv, 1.0f - (st1 + 1) * 0.5f, 1e-5f)) ok++;
            }
    CHECK(total == 6 * 49 && ok == total);

    auto toSky = [](const std::array<float, 9> &m, float x, float y, float z, std::array<float, 3> &d) {
        for (int r = 0; r < 3; r++) d[size_t(r)] = m[size_t(r)] * x + m[size_t(3 + r)] * y + m[size_t(6 + r)] * z;
    };
    realms::MapSky sky;
    std::array<float, 3> d{};
    toSky(sky.renderToSky(5.0), 0, 1, 0, d); // render up = Quake +Z
    CHECK(near(d[0], 0, 1e-6f) && near(d[1], 0, 1e-6f) && near(d[2], 1, 1e-6f));
    toSky(sky.renderToSky(5.0), 0, 0, -1, d); // render -Z = Quake +Y
    CHECK(near(d[0], 0, 1e-6f) && near(d[1], 1, 1e-6f) && near(d[2], 0, 1e-6f));
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
    def.entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"Realms module arena\"\n\"sky\" \"testsky_\"\n"
                   "\"skyrotate\" \"90\"\n\"skyaxis\" \"0 0 1\"\n\"sounds\" \"7\"\n}\n"
                   "{\n\"classname\" \"info_player_start\"\n\"origin\" \"-320 0 24\"\n\"angle\" \"0\"\n}\n"
                   "{\n\"classname\" \"item_armor_shard\"\n\"origin\" \"-160 0 16\"\n}\n"
                   "{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n\"angle\" \"-1\"\n\"speed\" \"200\"\n}\n";
    def.emitFaces = true;
    def.light = arenaLight;
    def.faceStyles = {{0, 1, 255, 255}};
    return def;
}

// Surface effects: water (SURF_WARP + TRANS33), a conveyor (FLOWING), glass
// (TRANS66), an animated texture (anim0 -> anim1 -> anim2 -> anim0) on a
// world box and a brush model (*1), an open chain and a chain with a frame
// that has no texture (both stay still). Lightmapped, so the faces use the
// builder's per-axis texinfo copies and their chains.
bsp_builder::MapDef surfaces() {
    bsp_builder::MapDef def;
    def.texinfo = {{"e1u1/floor"},
                   {"e1u1/water", REALMS_SURF_WARP | REALMS_SURF_TRANS33, 0},
                   {"e1u1/conveyor", REALMS_SURF_FLOWING, 0},
                   {"e1u1/glass", REALMS_SURF_TRANS66, 0},
                   {"e1u1/anim0", 0, 0, 5},
                   {"e1u1/anim1", 0, 0, 6},
                   {"e1u1/anim2", 0, 0, 4},
                   {"e1u1/open0", 0, 0, 8},
                   {"e1u1/open1", 0, 0, -1},
                   {"e1u1/missing0", 0, 0, 10},
                   {"e1u1/missing1", 0, 0, 9}};
    def.world.brushes = {
        {{-256, -256, -64}, {256, 256, 0}, REALMS_CONTENTS_SOLID, 0}, // floor
        {{-272, -256, 0}, {-256, 256, 128}, REALMS_CONTENTS_SOLID, 0}, // walls
        {{256, -256, 0}, {272, 256, 128}, REALMS_CONTENTS_SOLID, 0},
        {{-272, -272, 0}, {272, -256, 128}, REALMS_CONTENTS_SOLID, 0},
        {{-272, 256, 0}, {272, 272, 128}, REALMS_CONTENTS_SOLID, 0},
        {{-272, -272, 128}, {272, 272, 144}, REALMS_CONTENTS_SOLID, 0}, // ceiling
        {{-192, -192, 0}, {-128, -128, 16}, REALMS_CONTENTS_SOLID, 1}, // water
        {{-64, -192, 0}, {0, -128, 16}, REALMS_CONTENTS_SOLID, 2},     // conveyor
        {{64, -192, 0}, {128, -128, 64}, REALMS_CONTENTS_SOLID, 3},    // glass
        {{-192, 64, 0}, {-128, 128, 64}, REALMS_CONTENTS_SOLID, 4},    // animated
        {{-64, 64, 0}, {0, 128, 64}, REALMS_CONTENTS_SOLID, 7},        // open chain
        {{64, 64, 0}, {128, 128, 64}, REALMS_CONTENTS_SOLID, 9},       // missing frame
    };
    bsp_builder::ModelDef wall;
    wall.brushes = {{{160, -32, 0}, {192, 32, 64}, REALMS_CONTENTS_SOLID, 4}};
    def.inlineModels.push_back(wall);
    def.entities = "{\n\"classname\" \"worldspawn\"\n}\n"
                   "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 24\"\n}\n"
                   "{\n\"classname\" \"func_wall\"\n\"model\" \"*1\"\n}\n";
    def.emitFaces = true;
    def.light = arenaLight;
    def.faceStyles = {{0, 255, 255, 255}};
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
    CHECK(q2.cdTrack() == 0); // no game, no music
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

    // the map's textures: floor and wall have a .wal, the door doesn't
    const realms::MapTextures &tex = q2.textures();
    CHECK(tex.count() == 2 && tex.missing() == 1 && !tex.fromGamePalette());
    CHECK(tex.layerSize() == 128); // the wall's 128, a power of two
    const int floor = tex.find("e1u1/floor"), wall = tex.find("e1u1/wall");
    CHECK(floor >= 0 && wall >= 0 && floor != wall && tex.find("e1u1/door") == -1);
    CHECK(tex.pixels().size() == 2 * 128 * 128 * 4);
    if (floor >= 0 && wall >= 0 && tex.pixels().size() == 2 * 128 * 128 * 4) {
        CHECK(tex.texture(size_t(floor)).width == 64 && tex.texture(size_t(floor)).height == 64);
        CHECK(tex.texture(size_t(wall)).width == 32 && tex.texture(size_t(wall)).height == 128);
        const uint8_t *f = tex.pixels().data() + size_t(floor) * 128 * 128 * 4;
        CHECK(rgbIs(f, 0, 255, 0) && rgbIs(f + (128 * 128 - 1) * 4, 0, 255, 0)); // all 0xd0
        // the wall: red rows 0..63, white 64..127, stretched across
        const uint8_t *w = tex.pixels().data() + size_t(wall) * 128 * 128 * 4;
        CHECK(rgbIs(w + (10 * 128 + 70) * 4, 239, 0, 0) && rgbIs(w + (100 * 128 + 3) * 4, 235, 235, 235));
    }

    // the skybox: five .tga faces and a .pcx, 8 x 8 each (layers of 16)
    const realms::MapSky &sky = q2.sky();
    CHECK(!sky.empty() && sky.size() == 16 && sky.pixels().size() == 6 * 16 * 16 * 4);
    CHECK(sky.settings().name == "testsky_" && near(sky.settings().rotate, 90.0f, 1e-6f));
    if (sky.pixels().size() == 6 * 16 * 16 * 4) {
        for (int i = 0; i < 5; i++)
            CHECK(rgbIs(sky.pixels().data() + size_t(i) * 1024 + 4 * 37, kSkyColours[i][0], kSkyColours[i][1],
                        kSkyColours[i][2]));
        CHECK(rgbIs(sky.pixels().data() + 5 * 1024 + 4 * 200, 200, 55, 100));
    }
    // 90 degrees a second about Z: after one second, Quake +X looks at the
    // box's -Y face (ft), render-space +X being Quake +X
    std::array<float, 9> m = sky.renderToSky(1.0);
    float dir[3] = {m[0], m[1], m[2]}; // the image of render +X
    int face = -1;
    float fu = 0, fv = 0;
    realms::MapSky::faceCoords(dir, face, fu, fv);
    CHECK(face == 3 && near(fu, 0.5f, 1e-4f) && near(fv, 0.5f, 1e-4f));
    m = sky.renderToSky(4.0); // a full turn
    CHECK(near(m[0], 1, 1e-4f) && near(m[1], 0, 1e-4f) && near(m[2], 0, 1e-4f));
    realms::MapSky missing;
    std::string skyError;
    realms::MapSkySettings other;
    other.name = "nosuch_";
    CHECK(!missing.load(engine, other, &skyError) && missing.empty() && skyError == "env/nosuch_rt: not found");

    q2.disableLightmaps();
    CHECK(!q2.lightmapped() && q2.lightmap().empty() && !q2.lightmapUvs(geometry, uvs) && uvs.empty());
    q2.disableSky();
    CHECK(q2.sky().empty());
    q2.disableTextures();
    CHECK(q2.textures().empty() && q2.textures().find("e1u1/floor") == -1);
    q2.close();
    CHECK(!q2.lightmapped());

    realms::Quake2ModuleConfig plain;
    plain.startGame = false;
    plain.textures = false;
    plain.sky = false;
    q2.open(engine, geometry, plain, why);
    CHECK(q2.textures().empty() && q2.sky().empty() && q2.lightmapped());
    q2.close();
}

bool consoleHas(const realms::Engine &engine, const std::string &text) {
    for (const realms::ConsoleLine &l : engine.console())
        if (l.text.find(text) != std::string::npos) return true;
    return false;
}

// A demo made here (no game module needed) played back through Quake2View:
// "demomap" loads its map, the view plays it at the recorded pace, the
// camera follows the recorded player, then the map is walked again.
void demoWithoutGame(realms::Engine &engine) {
    realms::sandbox::registerMapCommands(engine);
    const std::string mapPath = engine.mapPath();
    CHECK(mapPath == "maps/arena.bsp");
    RealmsMapInfo info{};
    CHECK(engine.mapInfo(info));
    {
        realms::GameDemoWriter w;
        realms::GameDemoHeader header;
        header.mapPath = mapPath;
        header.mapChecksum = info.checksum;
        std::string error;
        CHECK(w.open(realms::gameDemoPath(engine, "synthetic"), header, error));
        // the door (model 1) rises 8 units a server frame; the player walks
        // +X and turns 3 degrees a frame
        for (int i = 0; i <= 10; i++) {
            realms::GameSnapshot s;
            s.running = true;
            s.frameNumber = 1 + i;
            s.hasPlayer = true;
            s.player.viewangles[0] = 10.0f;
            s.player.viewangles[1] = 30.0f + 3.0f * float(i);
            s.player.viewoffset[2] = 22.0f;
            s.player.fov = 90.0f;
            s.player.stats[Q2_STAT_HEALTH] = 100 - i;
            s.playerOrigin = {-320.0f + 10.0f * float(i), 0.0f, 34.0f};
            s.entitiesChanged = true;
            realms::GameRenderEntity door;
            door.number = 2;
            door.modelindex = 2;
            door.model = "*1";
            door.inlineModel = 1;
            door.origin = {0.0f, 0.0f, 8.0f * float(i)};
            door.oldOrigin = door.origin;
            door.solid = 3;
            s.entities = {door};
            if (i == 0) {
                s.configStrings = {{Q2_CS_NAME, "Synthetic"},
                                   {Q2_CS_CDTRACK, "7"},
                                   {Q2_CS_MODELS + 1, mapPath},
                                   {Q2_CS_MODELS + 2, "*1"}};
                realms::GameEvent print;
                print.kind = realms::GameEvent::Kind::Print;
                print.text = "hello from the demo\n";
                realms::GameEvent sound;
                sound.kind = realms::GameEvent::Kind::Sound;
                sound.text = "sound/world/klaxon2.wav";
                s.events = {print, sound};
            }
            CHECK(w.write(s, i == 0 ? 0.0f : 0.1f));
        }
    }

    // no game, no demo: nothing to record, nothing to stop
    engine.clearConsole();
    {
        realms::Quake2Module idle; // registers record / stop
        realms::MapGeometry g;
        std::string why;
        CHECK(realms::loadMapGeometry(engine, g, why));
        realms::Quake2ModuleConfig config;
        config.startGame = false;
        idle.open(engine, g, config, why);
        engine.exec("record nogame\n");
        CHECK(consoleHas(engine, "record: You must be in a level to record."));
        CHECK(!idle.recording() && !std::filesystem::exists(realms::gameDemoPath(engine, "nogame")));
        engine.exec("stop\n");
        CHECK(consoleHas(engine, "Not recording a demo."));
        engine.exec("record\n");
        CHECK(consoleHas(engine, "USAGE: record <demoname>"));
    }

    // demomap's mistakes leave the cvar alone
    engine.exec("demomap nosuch\n");
    CHECK(consoleHas(engine, "demomap: can't find demos/nosuch.ekd"));
    engine.exec("demomap ../up\n");
    CHECK(consoleHas(engine, "demomap: bad demo name \"../up\""));
    CHECK(engine.cvarString(realms::kGameDemoCvar, "").empty());

    engine.exec("demomap synthetic\n");
    CHECK(engine.mapLoaded() && engine.mapPath() == mapPath);
    CHECK(engine.cvarString(realms::kGameDemoCvar, "") == "demos/synthetic.ekd");
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    options.startGame = false; // no game module needed to play a demo
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    realms::Quake2Module &q2 = view.module();
    CHECK(engine.cvarString(realms::kGameDemoCvar, "").empty()); // used up
    CHECK(q2.demoPlaying() && view.gameRunning() && q2.game() == nullptr);
    CHECK(view.session().summary().find(", demo") != std::string::npos);
    CHECK(consoleHas(engine, "playing demo demos/synthetic.ekd"));
    CHECK(consoleHas(engine, "hello from the demo")); // the demo's prints
    CHECK(q2.cdTrack() == 7 && q2.client().configString(Q2_CS_NAME) == "Synthetic");
    CHECK(q2.stats().health == 100);
    bool klaxon = false;
    for (const realms::GameEvent &e : q2.takeSounds()) klaxon |= e.text == "sound/world/klaxon2.wav";
    CHECK(klaxon);
    CHECK(view.modelPoses().size() == 2 && view.modelPoses()[1].visible);
    // the camera is the recorded player's, whatever look the host passes
    realms::Vec3 cam = view.cameraAngles(1.0f, 0.5f);
    CHECK(near(cam[0], 10.0f, 1e-4f) && near(cam[1], 30.0f, 1e-4f) && near(cam[2], 0.0f, 1e-4f));
    physics::Vec3 eye = view.eye();
    CHECK(near(eye.x, -320.0f / 32.0f, 1e-3f) && near(eye.y, 56.0f / 32.0f, 1e-3f));
    // input is ignored: the recorded player walks, not ours
    realms::WalkInput back;
    back.forward = -1.0f;
    engine.exec("record during\n");
    CHECK(consoleHas(engine, "record: can't record while a demo plays") && !q2.recording());

    // half a second: the records due at 0.1 .. 0.5 s
    view.update(engine, back, 2.0f, 0.0f, 0.25f, true);
    view.update(engine, back, 2.0f, 0.0f, 0.25f, true);
    CHECK(q2.demoPlaying() && q2.client().frameNumber() == 6 && q2.stats().health == 95);
    cam = view.cameraAngles(1.0f, 0.5f);
    CHECK(near(cam[1], 45.0f, 1e-4f));
    eye = view.eye();
    CHECK(near(eye.x, -270.0f / 32.0f, 1e-3f));
    CHECK(near(view.session().walker().origin[0], -270.0f, 1e-3f)); // the walker follows
    CHECK(view.modelPoses().size() == 2 && view.modelPoses()[1].transform[13] > 30.0f / 32.0f &&
          view.modelPoses()[1].transform[13] < 41.0f / 32.0f); // risen 32 .. 40 units
    // the rest, then the end: back to walking where the demo left off
    for (int i = 0; i < 5; i++) view.update(engine, {}, 0.0f, 0.0f, 0.1f, false);
    CHECK(q2.demoPlaying() && q2.client().frameNumber() == 11);
    engine.clearConsole();
    view.update(engine, {}, 0.0f, 0.0f, 0.1f, false);
    CHECK(!q2.demoPlaying() && !view.gameRunning());
    CHECK(consoleHas(engine, "Demo finished: 11 frames, 1.0 seconds."));
    CHECK(view.modelPoses().empty() && q2.modelPoses().empty() && q2.status().empty());
    CHECK(near(view.session().walker().origin[0], -220.0f, 1e-3f));
    view.update(engine, {}, 0.0f, 0.0f, 0.1f, false); // walking
    CHECK(!view.gameRunning());

    // timedemo 1: one record a frame, whatever the time between frames,
    // then the frame rate (CL_FinishTimeDemo)
    engine.exec("set timedemo 1\n");
    engine.clearConsole();
    engine.exec("demomap synthetic\n");
    CHECK(view.open(engine, options, geometry, error) && q2.demoPlaying() && q2.timeDemoRunning());
    CHECK(consoleHas(engine, "timing demo demos/synthetic.ekd"));
    CHECK(q2.client().frameNumber() == 1 && q2.lastTimeDemo().frames == 0);
    view.update(engine, {}, 0.0f, 0.0f, 5.0f, false); // a long frame: still one record
    CHECK(q2.client().frameNumber() == 2);
    view.update(engine, {}, 0.0f, 0.0f, 0.0f, false); // no time at all: one record
    CHECK(q2.client().frameNumber() == 3);
    for (int i = 0; i < 8; i++) view.update(engine, {}, 0.0f, 0.0f, 0.001f, false);
    CHECK(q2.timeDemoRunning() && q2.client().frameNumber() == 11);
    CHECK(near(view.session().walker().origin[0], -220.0f, 1e-3f));
    view.update(engine, {}, 0.0f, 0.0f, 0.001f, false);
    CHECK(!q2.demoPlaying() && !q2.timeDemoRunning());
    const realms::Quake2Module::TimeDemoResult timed = q2.lastTimeDemo();
    CHECK(timed.frames == 10 && timed.seconds > 0.0 && std::fabs(timed.fps * timed.seconds - 10.0) < 1e-6);
    {
        char line[96];
        std::snprintf(line, sizeof line, "%i frames, %3.1f seconds: %3.1f fps", timed.frames, timed.seconds, timed.fps);
        CHECK(consoleHas(engine, line));
    }
    engine.exec("set timedemo 0\n");
    engine.exec("demomap synthetic\n");
    CHECK(view.open(engine, options, geometry, error) && q2.demoPlaying() && !q2.timeDemoRunning());
    CHECK(q2.lastTimeDemo().frames == 0); // forgotten with the map
    view.update(engine, {}, 0.0f, 0.0f, 0.05f, false);
    CHECK(q2.client().frameNumber() == 1); // back to the recorded pace

    // "stop" ends a playing demo too
    engine.exec("demomap synthetic.ekd\n");
    CHECK(view.open(engine, options, geometry, error) && q2.demoPlaying());
    engine.exec("stop\n");
    CHECK(!q2.demoPlaying() && consoleHas(engine, "Stopped demo playback."));

    // a demo for another map is refused (the game starts as usual)
    engine.forceSetCvar(realms::kGameDemoCvar, "demos/synthetic.ekd");
    CHECK(engine.loadMap("maps/surfaces.bsp"));
    CHECK(view.open(engine, options, geometry, error) && !q2.demoPlaying());
    CHECK(consoleHas(engine, "demomap: demos/synthetic.ekd was recorded on maps/arena.bsp, not maps/surfaces.bsp"));
    CHECK(engine.cvarString(realms::kGameDemoCvar, "").empty());
    // a damaged one plays up to the damage
    {
        std::vector<unsigned char> bytes;
        CHECK(engine.loadFile("demos/synthetic.ekd", bytes));
        bytes.resize(bytes.size() - 3); // cut short in the last record
        std::ofstream(realms::gameDemoPath(engine, "cut"), std::ios::binary)
            .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    }
    engine.clearConsole();
    engine.exec("demomap cut\n");
    CHECK(view.open(engine, options, geometry, error) && q2.demoPlaying());
    for (int i = 0; i < 12 && q2.demoPlaying(); i++) view.update(engine, {}, 0.0f, 0.0f, 0.1f, false);
    CHECK(!q2.demoPlaying() && consoleHas(engine, "Demo finished: 10 frames, 0.9 seconds."));
    view.close();
    CHECK(engine.loadMap(mapPath));
}

// A stand-in game behind a GameNetServer (no game module needed): one
// remote player walking along x, a door, a print on entering.
struct NetStandIn : realms::GameNetServerGame {
    bool active = false;
    float x = -320.0f, fov = 90.0f;
    int hand = 0, frame = 0;
    std::vector<std::string> commands;
    std::vector<realms::GameEvent> events;
    bool sentStrings = false;
    int maxClients() const override { return 4; }
    int connectClient(const std::string &name, float f, int h, const std::string &, const std::string &,
                      const std::string &, const std::string &, const std::string &, std::string &) override {
        active = true;
        fov = f;
        hand = h;
        sentStrings = false;
        realms::GameEvent ev;
        ev.kind = realms::GameEvent::Kind::Print;
        ev.text = name + " entered the stand-in\n";
        events.push_back(ev);
        return 1;
    }
    void disconnectClient(int) override { active = false; }
    void clientMove(int, const realms::GameUserCmd &cmd, double seconds) override { x += float(cmd.forward * seconds); }
    void clientCommand(int, const std::string &line) override { commands.push_back(line); }
    void setFov(int, float f) override { fov = f; }
    void setHand(int, int h) override { hand = h; }
    void setSkin(int, const std::string &) override {}
    void setGender(int, const std::string &) override {}
    void setSpectator(int, const std::string &) override {}
    void setPassword(int, const std::string &) override {}
    void snapshot(int, realms::GameSnapshot &s, bool full) override {
        s = realms::GameSnapshot{};
        if (!active) return;
        s.running = true;
        s.frameNumber = frame;
        s.hasPlayer = true;
        s.player.viewangles[1] = 45.0f;
        s.player.viewoffset[2] = 22.0f;
        s.player.fov = fov;
        s.player.stats[Q2_STAT_HEALTH] = 77;
        s.playerOrigin = {x, 0.0f, 34.0f};
        s.entitiesChanged = true;
        realms::GameRenderEntity door;
        door.number = 9;
        door.modelindex = 2;
        door.model = "*1";
        door.inlineModel = 1;
        door.solid = 3;
        s.entities = {door};
        if (full || !sentStrings) {
            s.configStrings = {{Q2_CS_NAME, "Stand-in"}, {Q2_CS_MODELS + 2, "*1"}};
            sentStrings = true;
        }
        s.events.swap(events);
    }
};

// Quake2View / Quake2Module as the client of a server: "connect" loads the
// server's map, the view plays the server's game (its prints, the
// player's slot, usercmds, commands, userinfo, a demo of it), the server
// changing maps, "disconnect", the server quitting, another map.
void netWithoutGame(realms::Engine &engine) {
    realms::registerGameNetCommands(engine);
    const std::string mapPath = engine.mapPath();
    RealmsMapInfo info{};
    CHECK(engine.mapInfo(info));
    NetStandIn game;
    realms::GameNetServer server;
    std::string error;
    CHECK(server.open(0, error, true));
    if (!server.isOpen()) {
        std::fprintf(stderr, "  no UDP socket: %s\n", error.c_str());
        return;
    }
    server.setMap(mapPath, info.checksum);
    const std::string address = "127.0.0.1:" + std::to_string(server.port());
    realms::GameNetClient &net = realms::GameNetClient::instance();
    const auto serve = [&]() {
        const double now = realms::GameNetClient::clock();
        server.receive(game, now);
        game.frame++;
        server.send(game, now);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    // the console's connect up to the map load (the hook runs "map")
    const auto handshake = [&]() {
        for (int i = 0; i < 300 && net.state() != realms::GameNetClient::State::Loading; i++) {
            serve();
            engine.frame(0.02);
        }
        engine.frame(0.02); // runs the "map" the hook queued
    };
    engine.clearConsole();
    engine.exec("connect " + address + "\n");
    handshake();
    CHECK(net.state() == realms::GameNetClient::State::Loading && net.mapPath() == mapPath);
    CHECK(consoleHas(engine, "Connected to " + address + ": " + mapPath) && consoleHas(engine, "map: " + mapPath));

    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    options.startGame = false;
    realms::MapGeometry geometry;
    CHECK(view.open(engine, options, geometry, error));
    realms::Quake2Module &q2 = view.module();
    CHECK(q2.connected() && q2.game() == nullptr && net.state() == realms::GameNetClient::State::Active);
    CHECK(view.session().summary().find(", online") != std::string::npos);
    CHECK(consoleHas(engine, "map: playing on " + address));
    realms::WalkInput forward;
    forward.forward = 1.0f;
    // render yaw 0 looks along the map's +X (Quake2View::walkInput)
    const auto update = [&](const realms::WalkInput &in) {
        serve();
        view.update(engine, in, 0.0f, 0.0f, 0.02f, false);
    };
    // the client alone, until it has let go of the server (loopback
    // datagrams aren't always there at once: macOS queues them)
    const auto untilOffline = [&]() {
        for (int i = 0; i < 500 && q2.connected(); i++) {
            view.update(engine, {}, 0.0f, 0.0f, 0.02f, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        view.update(engine, {}, 0.0f, 0.0f, 0.02f, false); // the view's own message about it
    };
    for (int i = 0; i < 100 && !view.gameRunning(); i++) update({});
    CHECK(view.gameRunning() && q2.client().configString(Q2_CS_NAME) == "Stand-in" && q2.stats().health == 77);
    CHECK(consoleHas(engine, "Player entered the stand-in")); // the server's prints
    CHECK(q2.listener().entity == 2);                        // slot 1: edict 2
    CHECK(view.modelPoses().size() == 2 && view.modelPoses()[1].visible);
    // the player's own look, not the server's (no waiting for the server)
    const realms::Vec3 cam = view.cameraAngles(0.0f, 0.0f);
    CHECK(near(cam[1], realms::Quake2Module::userCmd(mapview::walkInput({}, 0.0f, 0.0f), false).yaw, 1e-3f));
    // walking: usercmds to the server, its origin back
    const float x0 = game.x;
    for (int i = 0; i < 50; i++) update(forward);
    // (both ends send at most 60 times a second: frames here are quicker)
    for (int i = 0; i < 100 && !near(q2.client().playerOrigin()[0], game.x, 0.01f); i++) update({});
    std::printf("[module] remote walk: %.1f units\n", double(game.x - x0));
    CHECK(game.x - x0 > 200.0f);
    CHECK(near(q2.client().playerOrigin()[0], game.x, 0.01f));
    // client commands and userinfo go to the server
    q2.clientCommand("inven");
    view.toggleNoclip();
    engine.exec("set fov 100\n");
    std::vector<mapview::EntityVertex> ev;
    std::vector<mapview::EntityBatch> eb;
    view.entities(engine, 0.0f, 0.0f, true, ev, eb); // sends the changed userinfo
    for (int i = 0; i < 100 && (game.commands.size() < 2 || !near(q2.stats().fov, 100.0f, 1e-3f)); i++) update({});
    CHECK(game.commands.size() == 2 && game.commands[0] == "inven" && game.commands[1] == "noclip");
    // the game's console commands go to the server's game too
    CHECK(engine.commandExists("give") && engine.commandExists("say") && engine.commandExists("cmd"));
    engine.exec("say hello  there\ngive \"rocket launcher\"\ncmd playerlist all\n");
    for (int i = 0; i < 500 && game.commands.size() < 5; i++) update({});
    CHECK(game.commands.size() == 5 && game.commands[2] == "say hello there" &&
          game.commands[3] == "give \"rocket launcher\"" && game.commands[4] == "playerlist all");
    CHECK(near(game.fov, 100.0f, 1e-3f) && near(q2.stats().fov, 100.0f, 1e-3f));
    engine.exec("set fov 90\n");
    // no restart on someone else's server
    std::string why;
    CHECK(!view.respawn(engine) && !q2.restartGame(engine, why) && why.find("disconnect first") != std::string::npos);
    // a demo of a network game
    engine.exec("record netgame\n");
    CHECK(q2.recording() && consoleHas(engine, "recording to demos/netgame.ekd."));
    for (int i = 0; i < 10; i++) update({});
    engine.exec("stop\n");
    CHECK(!q2.recording() && consoleHas(engine, "Stopped demo: demos/netgame.ekd"));

    // the server changes maps: the client walks, then joins again
    const uint16_t port = server.port();
    server.close("reconnect");
    game.active = false;
    untilOffline();
    CHECK(!q2.connected() && !view.gameRunning());
    CHECK(net.state() == realms::GameNetClient::State::Challenging);
    for (const std::string &m : view.takeMessages()) CHECK(m.find("Game stopped") == std::string::npos);
    CHECK(server.open(port, error, true));
    server.setMap(mapPath, info.checksum);
    handshake();
    CHECK(view.open(engine, options, geometry, error) && q2.connected());
    for (int i = 0; i < 100 && !view.gameRunning(); i++) update({});
    CHECK(view.gameRunning() && game.active);

    // "disconnect": back to walking, the server lets go
    engine.exec("disconnect\n");
    for (int i = 0; i < 500 && (game.active || server.clientCount() > 0 || q2.connected()); i++) update({});
    CHECK(!q2.connected() && !view.gameRunning() && !game.active && server.clientCount() == 0);
    CHECK(!engine.commandExists("give") && !engine.commandExists("say") && !engine.commandExists("cmd"));
    for (const std::string &m : view.takeMessages()) CHECK(m.find("Game stopped") == std::string::npos);

    // the server quitting: the view says why
    engine.exec("reconnect\n");
    handshake();
    CHECK(view.open(engine, options, geometry, error) && q2.connected());
    for (int i = 0; i < 100 && !view.gameRunning(); i++) update({});
    CHECK(view.gameRunning());
    server.close();
    untilOffline();
    CHECK(!q2.connected() && !view.gameRunning());
    {
        bool told = false;
        for (const std::string &m : view.takeMessages()) told |= m == "Game stopped: Server disconnected: Server quit";
        CHECK(told);
    }

    // another map opened while playing on a server ends the connection
    CHECK(server.open(port, error, true));
    server.setMap(mapPath, info.checksum);
    engine.exec("reconnect\n");
    handshake();
    CHECK(view.open(engine, options, geometry, error) && q2.connected());
    for (int i = 0; i < 100 && !view.gameRunning(); i++) update({});
    engine.clearConsole();
    CHECK(engine.loadMap("maps/surfaces.bsp"));
    CHECK(view.open(engine, options, geometry, error) && !q2.connected() && !net.connected());
    for (int i = 0; i < 500 && (game.active || server.clientCount() > 0); i++) serve();
    CHECK(!game.active && server.clientCount() == 0);
    // the server's map isn't the one here: refused
    server.setMap(mapPath, info.checksum ^ 1u);
    CHECK(engine.loadMap(mapPath));
    engine.clearConsole();
    engine.exec("reconnect\n");
    handshake();
    CHECK(view.open(engine, options, geometry, error) && !q2.connected() && !net.connected());
    CHECK(consoleHas(engine, "connect: your " + mapPath + " differs from the server's"));
    view.close();
    server.close();
    CHECK(engine.loadMap(mapPath));
}

// CL_FixUpGender on the cvars: skin, gender, gender_auto.
void genderChecks(realms::Engine &engine) {
    using realms::Quake2Module;
    CHECK(engine.registerCvar("skin", "male/grunt", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO));
    CHECK(engine.registerCvar("gender", "male", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO));
    CHECK(engine.registerCvar("gender_auto", "1", REALMS_CVAR_ARCHIVE));
    // the skin changes, the gender didn't: it follows the model
    engine.setCvar("skin", "female/athena");
    CHECK(Quake2Module::fixUpGender(engine, false) && engine.cvarString("gender") == "female");
    CHECK(!engine.cvarModified("gender")); // cleared, as gender->modified = false
    CHECK(!Quake2Module::fixUpGender(engine, false) && engine.cvarString("gender") == "female");
    engine.setCvar("skin", "cyborg/oni911");
    CHECK(Quake2Module::fixUpGender(engine, false) && engine.cvarString("gender") == "male");
    engine.setCvar("skin", "crakhor/x");
    CHECK(Quake2Module::fixUpGender(engine, false) && engine.cvarString("gender") == "none");
    // the gender set itself: kept
    engine.setCvar("skin", "female/venus");
    engine.setCvar("gender", "male");
    CHECK(!Quake2Module::fixUpGender(engine, engine.cvarModified("gender")) && engine.cvarString("gender") == "male");
    // gender_auto 0: never touched
    engine.setCvar("gender_auto", "0");
    engine.setCvar("skin", "female/athena");
    CHECK(!Quake2Module::fixUpGender(engine, false) && engine.cvarString("gender") == "male");
    engine.setCvar("gender_auto", "1");
    engine.setCvar("skin", "male/grunt");
    engine.setCvar("gender", "male");
    engine.cvarModified("skin");
    engine.cvarModified("gender");
}

// svc_stufftext: only "spectator <value>" is run.
void stuffTextChecks(realms::Engine &engine) {
    using realms::Quake2Module;
    CHECK(engine.registerCvar("spectator", "0", REALMS_CVAR_USERINFO));
    engine.registerCvar("hand", "0", REALMS_CVAR_ARCHIVE | REALMS_CVAR_USERINFO);
    engine.setCvar("hand", "0");
    engine.setCvar("spectator", "1");
    CHECK(Quake2Module::stuffText(engine, "spectator 0\n") == 1 && engine.cvarString("spectator") == "0");
    CHECK(Quake2Module::stuffText(engine, "SPECTATOR \"secret word\"\n") == 1 &&
          engine.cvarString("spectator") == "secret word");
    // anything else is ignored, also after a ';' or in a later line
    CHECK(Quake2Module::stuffText(engine, "hand 2\n") == 0 && engine.cvarString("hand") == "0");
    CHECK(Quake2Module::stuffText(engine, "spectator 1; hand 2\nquit\n") == 1);
    CHECK(engine.cvarString("spectator") == "1" && engine.cvarString("hand") == "0");
    // a quoted ';' stays in the value, which a userinfo cvar refuses
    CHECK(Quake2Module::stuffText(engine, "spectator \"a;hand 2\"\n") == 0 && engine.cvarString("spectator") == "1");
    CHECK(engine.cvarString("hand") == "0");
    CHECK(Quake2Module::stuffText(engine, "spectator\n") == 0 && Quake2Module::stuffText(engine, "spectator 1 2\n") == 0);
    CHECK(Quake2Module::stuffText(engine, "") == 0 && Quake2Module::stuffText(engine, "\n;\n") == 0);
    engine.setCvar("spectator", "0");
    engine.cvarModified("spectator");
    engine.cvarModified("hand");
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

    // floor and wall faces sample the map's textures (mapTextureLayer, white,
    // coordinates over the texture size); the door keeps its material layer
    std::vector<sandbox::MeshVertex> plain;
    std::vector<mapview::ModelRange> plainRanges;
    std::vector<float> plainUvs;
    const realms::MapTextures &tex = view.module().textures();
    const int floorIndex = tex.find("e1u1/floor"), wallIndex = tex.find("e1u1/wall");
    CHECK(floorIndex >= 0 && wallIndex >= 0);
    view.disableTextures();
    view.triangles(geometry, plain, plainRanges, plainUvs);
    CHECK(plain.size() == vertices.size());
    size_t textured = 0, layerOk = 0, uvOk = 0, doorPlain = 0;
    for (size_t i = 0; i < vertices.size() && i < plain.size(); i++) {
        const int layer = int(vertices[i].layer + 0.5f);
        CHECK(plain[i].layer < mapview::kMapTextureLayer);
        if (layer < mapview::kMapTextureLayer) {
            if (ranges.size() == 2 && i >= size_t(ranges[1].first)) doorPlain++;
            continue;
        }
        textured++;
        const int index = (layer >> 3) - 1;
        if ((layer & 7) == int(plain[i].layer + 0.5f) && (index == floorIndex || index == wallIndex) &&
            vertices[i].r == 1.0f && vertices[i].g == 1.0f && vertices[i].b == 1.0f)
            layerOk++;
        // plain: texels / 64; textured: texels / the texture's size
        const float w = index == wallIndex ? 32.0f : 64.0f, h = index == wallIndex ? 128.0f : 64.0f;
        if (near(vertices[i].u * w, plain[i].u * 64.0f, 1e-2f) && near(vertices[i].v * h, plain[i].v * 64.0f, 1e-2f))
            uvOk++;
    }
    CHECK(textured > 0 && layerOk == textured && uvOk == textured);
    CHECK(ranges.size() == 2 && doorPlain == size_t(ranges.size() == 2 ? ranges[1].count : 0));
    CHECK(tex.empty());

    // walking: the walker moves the camera (render space: map +X is render +X)
    const physics::Vec3 start = view.eye();
    const float yaw = view.spawnRenderYaw(); // facing +X
    realms::WalkInput in;
    in.forward = 1.0f;
    for (int i = 0; i < 30; i++) view.update(engine, in, yaw, 0.0f, 1.0f / 30.0f, false);
    const physics::Vec3 moved = view.eye();
    CHECK(moved.x > start.x + 1.0f);
    {
        // one second of update(): the sky turned 90 degrees (skyrotate 90)
        const std::array<float, 9> m = view.skyMatrix();
        const float dir[3] = {m[0], m[1], m[2]};
        int face = -1;
        float u = 0, v = 0;
        realms::MapSky::faceCoords(dir, face, u, v);
        CHECK(face == 3 && near(u, 0.5f, 1e-3f));
    }
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

// Surface flags and texture animation through the view: the packed layers,
// the animation slots and frames, the motion uniforms.
void surfaceEffects(realms::Engine &engine) {
    // the encoding
    CHECK(mapview::mapEffects(0) == 0);
    CHECK(mapview::mapEffects(REALMS_SURF_WARP | REALMS_SURF_FLOWING) ==
          (mapview::kMapEffectWarp | mapview::kMapEffectFlowing));
    CHECK(mapview::mapEffects(REALMS_SURF_TRANS33 | REALMS_SURF_TRANS66) == mapview::kMapEffectTrans33);
    CHECK(mapview::mapEffects(REALMS_SURF_TRANS66 | REALMS_SURF_SKY) == mapview::kMapEffectTrans66);
    const mapview::MapLayerParts parts =
        mapview::mapLayerParts(float(4093 | (mapview::kMapEffectWarp << mapview::kMapEffectShift) |
                                     (32 << mapview::kMapAnimationShift)));
    CHECK(parts.layer == 4093 && parts.effects == mapview::kMapEffectWarp && parts.animation == 32);
    std::array<float, 4> motion = mapview::mapSurfaceMotion(0.0);
    CHECK(motion[0] == 0.0f && motion[1] == 0.0f && motion[2] == 0.0f && motion[3] == 0.0f);
    motion = mapview::mapSurfaceMotion(1.0); // flowing: -64 * 1/40 = -1.6; warp flow: -0.5
    CHECK(near(motion[0], 1.0f, 1e-6f) && near(motion[1], 0.4f, 1e-5f) && near(motion[2], 0.5f, 1e-6f));
    motion = mapview::mapSurfaceMotion(10.0);
    CHECK(near(motion[0], float(10.0 - 6.283185307179586), 1e-5f) && motion[1] >= 0.0f && motion[1] < 1.0f);
    motion = mapview::mapSurfaceMotion(std::nan(""));
    CHECK(motion[0] == 0.0f && motion[1] == 0.0f && motion[2] == 0.0f);

    CHECK(engine.loadMap("maps/surfaces.bsp"));
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    options.startGame = false;
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    if (!view.active()) return;
    // no "sky" in this worldspawn: a BSP map falls back to unit1_
    CHECK(!view.module().sky().empty() && view.module().sky().settings().name == "unit1_");

    // the geometry: flags and closed chains (frames), per model
    const std::vector<std::string> cycle{"e1u1/anim0", "e1u1/anim1", "e1u1/anim2"};
    const std::vector<std::string> missingCycle{"e1u1/missing0", "e1u1/missing1"};
    int water = 0, worldAnim = 0, modelAnim = 0, open = 0, missing = 0, unexpectedFrames = 0;
    for (const realms::MapGeometryBatch &b : geometry.batches) {
        if (b.material == "e1u1/water") {
            water++;
            CHECK((b.surfaceFlags & REALMS_SURF_WARP) && (b.surfaceFlags & REALMS_SURF_TRANS33));
        }
        if (b.material == "e1u1/anim0") {
            (b.model > 0 ? modelAnim : worldAnim)++;
            CHECK(b.frames == cycle);
        } else if (b.material == "e1u1/missing0") {
            missing++;
            CHECK(b.frames == missingCycle);
        } else {
            if (b.material == "e1u1/open0") open++;
            if (!b.frames.empty()) unexpectedFrames++;
        }
    }
    CHECK(water > 0 && worldAnim > 0 && modelAnim > 0 && open > 0 && missing > 0 && unexpectedFrames == 0);

    // textures: frames no face starts with load too; open chains stop at the face
    const realms::MapTextures &tex = view.module().textures();
    const int anim0 = tex.find("e1u1/anim0"), anim1 = tex.find("e1u1/anim1"), anim2 = tex.find("e1u1/anim2");
    const int waterIndex = tex.find("e1u1/water"), conveyor = tex.find("e1u1/conveyor"), glass = tex.find("e1u1/glass");
    CHECK(anim0 >= 0 && anim1 >= 0 && anim2 >= 0 && waterIndex >= 0 && conveyor >= 0 && glass >= 0);
    CHECK(tex.find("e1u1/open1") < 0 && tex.find("e1u1/missing0") >= 0 && tex.find("e1u1/missing1") < 0);

    std::vector<sandbox::MeshVertex> vertices;
    std::vector<mapview::ModelRange> ranges;
    std::vector<float> lightUvs;
    view.triangles(geometry, vertices, ranges, lightUvs);
    CHECK(ranges.size() == 2 && lightUvs.size() == vertices.size() * 2);
    CHECK(view.animationCount() == 1); // the world's and the model's anim0 share a slot
    CHECK(view.hasTranslucentFaces());
    const size_t modelFirst = ranges.size() == 2 ? size_t(ranges[1].first) : vertices.size();
    size_t warpVertices = 0, flowing = 0, trans66 = 0, animatedWorld = 0, animatedModel = 0, still = 0, wrong = 0;
    int animClass = -1;
    float warpSpan = 0.0f;
    for (size_t i = 0; i < vertices.size(); i++) {
        const mapview::MapLayerParts p = mapview::mapLayerParts(vertices[i].layer);
        const int index = p.layer >= mapview::kMapTextureLayer ? (p.layer >> 3) - 1 : -1;
        if (index == waterIndex) {
            warpVertices++;
            if (p.effects != (mapview::kMapEffectWarp | mapview::kMapEffectTrans33) || p.animation) wrong++;
            if (i % 3 == 0 && i + 2 < vertices.size()) // texels / 64, not / the texture's 128
                for (size_t a = i; a < i + 3; a++)
                    for (size_t b = i; b < i + 3; b++) warpSpan = std::max(warpSpan, vertices[a].u - vertices[b].u);
        } else if (index == conveyor) {
            flowing++;
            if (p.effects != mapview::kMapEffectFlowing || p.animation) wrong++;
        } else if (index == glass) {
            trans66++;
            if (p.effects != mapview::kMapEffectTrans66 || p.animation) wrong++;
        } else if (index == anim0) {
            (i >= modelFirst ? animatedModel : animatedWorld)++;
            if (p.effects || p.animation != 1) wrong++;
            animClass = p.layer & 7;
        } else {
            still++;
            if (p.effects || p.animation) wrong++;
        }
    }
    CHECK(warpVertices > 0 && flowing > 0 && trans66 > 0 && animatedWorld > 0 && animatedModel > 0 && still > 0);
    CHECK(wrong == 0);
    CHECK(near(warpSpan, 1.0f, 1e-4f));

    // the world at 2 frames a second, models at their entity's frame (none
    // without the game: frame 0)
    std::vector<int> layers;
    const auto worldFrame = [&]() {
        view.animationLayers(0, layers);
        return layers.size() == 1 ? (layers[0] >> 3) - 1 : -2;
    };
    CHECK(worldFrame() == anim0 && animClass >= 0 && (layers.empty() || (layers[0] & 7) == animClass));
    view.update(engine, realms::WalkInput{}, 0.0f, 0.0f, 0.5f, false);
    CHECK(worldFrame() == anim1);
    view.update(engine, realms::WalkInput{}, 0.0f, 0.0f, 0.5f, false);
    CHECK(worldFrame() == anim2);
    view.update(engine, realms::WalkInput{}, 0.0f, 0.0f, 0.5f, false);
    CHECK(worldFrame() == anim0);
    view.animationLayers(1, layers);
    CHECK(layers.size() == 1 && !layers.empty() && (layers[0] >> 3) - 1 == anim0);
    motion = view.surfaceMotion();
    CHECK(near(motion[0], 1.5f, 1e-4f));

    // without the map's textures: effects stay, animation slots don't
    view.disableTextures();
    view.triangles(geometry, vertices, ranges, lightUvs);
    CHECK(view.animationCount() == 0 && view.hasTranslucentFaces());
    size_t slots = 0, effects = 0, textured = 0;
    for (const sandbox::MeshVertex &v : vertices) {
        const mapview::MapLayerParts p = mapview::mapLayerParts(v.layer);
        if (p.animation) slots++;
        if (p.effects) effects++;
        if (p.layer >= mapview::kMapTextureLayer) textured++;
    }
    CHECK(slots == 0 && textured == 0 && effects == warpVertices + flowing + trans66);
    view.close();
    CHECK(view.animationCount() == 0 && !view.hasTranslucentFaces());
    CHECK(engine.loadMap("maps/arena.bsp"));
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
    CHECK(q2.cdTrack() == 7); // worldspawn "sounds" -> CS_CDTRACK
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
    // r_drawentities 0: no entities, the door hidden too (after an update;
    // no time passes); back with 1
    {
        std::array<float, 16> placed{};
        engine.executeNow("set r_drawentities 0");
        view.entities(engine, yaw, 0.0f, true, ev, eb);
        CHECK(eb.empty() && ev.empty());
        view.update(engine, {}, yaw, 0.0f, 0.0f, false);
        CHECK(view.modelPoses().size() == 2 && !view.modelPoses()[1].visible);
        CHECK(!view.placedTransform(1, placed));
        engine.executeNow("set r_drawentities 1");
        view.update(engine, {}, yaw, 0.0f, 0.0f, false);
        CHECK(view.modelPoses().size() == 2 && view.modelPoses()[1].visible);
        CHECK(view.placedTransform(1, placed));
        view.entities(engine, yaw, 0.0f, true, ev, eb);
        CHECK(!eb.empty());
    }
    // hand 1: the view weapon mirrored to the left (render z is map -y), the
    // userinfo sent again; hand 2: no view weapon; hand 0: back on the right
    {
        // the view weapon's corners, sorted, with z negated when `mirror`
        auto weaponCorners = [&](bool mirror) {
            view.entities(engine, yaw, 0.0f, true, ev, eb);
            std::vector<std::array<float, 3>> corners;
            for (const auto &b : eb)
                if (b.viewWeapon)
                    for (int i = b.first; i < b.first + b.count; i++)
                        corners.push_back({ev[size_t(i)].x, ev[size_t(i)].y, mirror ? -ev[size_t(i)].z : ev[size_t(i)].z});
            std::sort(corners.begin(), corners.end());
            return corners;
        };
        const std::vector<std::array<float, 3>> right = weaponCorners(false);
        CHECK(std::string(q2.game()->userinfo()).find("\\hand\\0\\") != std::string::npos);
        engine.executeNow("set hand 1");
        const std::vector<std::array<float, 3>> left = weaponCorners(true);
        CHECK(std::string(q2.game()->userinfo()).find("\\hand\\1\\") != std::string::npos);
        CHECK(!right.empty() && left.size() == right.size());
        bool mirrored = left.size() == right.size();
        for (size_t i = 0; mirrored && i < left.size(); i++)
            for (int k = 0; k < 3; k++) mirrored &= near(left[i][size_t(k)], right[i][size_t(k)], 1e-4f);
        CHECK(mirrored);
        engine.executeNow("set hand 2");
        CHECK(weaponCorners(false).empty());
        CHECK(std::string(q2.game()->userinfo()).find("\\hand\\2\\") != std::string::npos);
        engine.executeNow("set hand 0");
        CHECK(weaponCorners(false) == right);
        CHECK(realms::GameServer::clampHand(3) == 0 && realms::GameServer::clampHand(-1) == 0 &&
              realms::GameServer::clampHand(1) == 1 && realms::GameServer::clampHand(2) == 2);
    }

    step("walk");
    // walk +X: pick up the shard (message + sound), then open the door
    realms::WalkInput in;
    in.forward = 1.0f;
    bool picked = false, pickupSound = false, styleChanged = false, doorMoved = false;
    CHECK(q2.stats().blend[3] == 0.0f); // dry and unhurt: no screen blend
    std::array<float, 4> pickupBlend{};
    // the view kick (p_view.c SV_CalcViewOffset: run_pitch, bob_pitch,
    // bob_roll): the camera tilts while walking, drawn as it is computed
    constexpr float d2r = 3.14159265358979f / 180.0f;
    float maxKick = 0.0f;
    bool kickDrawn = true;
    for (int i = 0; i < 40; i++) {
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        const realms::Vec3 cam = view.cameraAngles(yaw, 0.0f);
        maxKick = std::max(maxKick, std::fabs(cam[0]) + std::fabs(cam[2]));
        const mapview::Quake2View::DrawAngles drawn = view.drawAngles(yaw, 0.0f);
        kickDrawn &= near(drawn.pitch, -cam[0] * d2r, 1e-4f) && near(drawn.roll, cam[2] * d2r, 1e-4f) &&
                     near(drawn.yaw, mapview::renderYawFromMap(cam[1]), 1e-4f);
        // the pickup flash: bonus_alpha 0.25 in Touch_Item, SV_AddBlend (0.85, 0.7, 0.3) in p_view.c
        if (const std::array<float, 4> b = q2.stats().blend; b[3] > pickupBlend[3]) pickupBlend = b;
        for (const std::string &m : view.takeMessages()) picked |= m == "Armor Shard";
        pickupSound |= hasSound(q2.takeSounds(), "sound/misc/ar2_pkup.wav");
        styleChanged |= q2.updateLightStyles();
        if (view.modelPoses().size() == 2 && view.modelPoses()[1].transform[13] > 0.1f) doorMoved = true;
    }
    CHECK(picked);
    CHECK(pickupSound);
    CHECK(maxKick > 0.1f && maxKick < 15.0f);
    CHECK(kickDrawn);
    // cl_kickangles 0 (Yamagi Quake II): the camera is the player's look alone
    engine.executeNow("set cl_kickangles 0");
    float offKick = 0.0f;
    for (int i = 0; i < 10; i++) {
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        const realms::Vec3 cam = view.cameraAngles(yaw, 0.0f);
        offKick = std::max(offKick, std::fabs(cam[0]) + std::fabs(cam[2]));
        for (const std::string &m : view.takeMessages()) picked |= m == "Armor Shard";
        pickupSound |= hasSound(q2.takeSounds(), "sound/misc/ar2_pkup.wav");
        if (const std::array<float, 4> b = q2.stats().blend; b[3] > pickupBlend[3]) pickupBlend = b;
    }
    CHECK(!q2.kickAngles() && offKick < 1e-4f);
    engine.executeNow("set cl_kickangles 1");
    view.update(engine, in, yaw, 0.0f, 0.05f, false);
    CHECK(q2.kickAngles());
    CHECK(styleChanged); // style 1 flickers (worldspawn's CS_LIGHTS + 1)
    CHECK(doorMoved);    // the door rises (render +Y) as the game moves it
    CHECK(view.session().walker().origin[0] > -250.0f);
    CHECK(q2.stats().armor == 2);
    CHECK(near(pickupBlend[3], 0.25f, 0.01f));
    CHECK(near(pickupBlend[0], 0.85f, 0.01f) && near(pickupBlend[1], 0.7f, 0.01f) && near(pickupBlend[2], 0.3f, 0.01f));
    CHECK(q2.stats().blend[3] == 0.0f); // bonus_alpha fades by 0.1 a game frame

    // F1 ("cmd help"): the game sends its help computer layout and shows it
    // (STAT_LAYOUTS & 1); a second F1 closes it
    step("help computer");
    CHECK(!q2.layoutShown() && q2.layoutItems(640, 480).empty());
    q2.toggleHelp();
    for (int i = 0; i < 3; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(q2.layoutShown());
    {
        const std::vector<realms::GameLayoutItem> help = q2.layoutItems(640, 480);
        bool skill = false, header = false, background = false;
        for (const realms::GameLayoutItem &it : help) {
            skill |= it.kind == realms::GameLayoutItem::Kind::Text && it.text == "medium" && it.x == 362 && it.y == 132;
            header |= it.kind == realms::GameLayoutItem::Kind::Text && it.text == " kills     goals    secrets";
            background |= it.kind == realms::GameLayoutItem::Kind::Pic && it.text == "help";
        }
        CHECK(skill && header && background);
    }
    CHECK(!q2.statusBarItems(640, 480).empty()); // the single-player status bar program
    q2.toggleHelp();
    for (int i = 0; i < 3; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(!q2.layoutShown());

    // Tab ("inven"): the game sends svc_inventory and shows the inventory
    // (STAT_LAYOUTS & 2); the Blaster the player starts with is selected
    step("inventory");
    CHECK(!q2.inventoryShown() && q2.inventoryItems(640, 480, true).empty());
    q2.clientCommand("inven");
    for (int i = 0; i < 3; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(q2.inventoryShown());
    {
        bool blaster = false, frame = false;
        for (const realms::GameLayoutItem &it : q2.inventoryItems(640, 480, true)) {
            blaster |= it.kind == realms::GameLayoutItem::Kind::Text && !it.alt &&
                       it.text.find("  1 Blaster") != std::string::npos;
            frame |= it.kind == realms::GameLayoutItem::Kind::Pic && it.text == "inventory";
        }
        CHECK(blaster && frame);
    }
    // the mobile HUD image: 1280 x 960 at 3 screen pixels per Quake 2 pixel;
    // the status bar's numbers (font digits without the paks) at the bottom,
    // the inventory's panel; unchanged frames are not redrawn
    realms::GameHudCanvas hud;
    auto hudAlpha = [&hud](int x, int y) {
        const realms::RgbaImage &img = hud.image();
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) return -1;
        return int(img.rgba[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3]);
    };
    CHECK(hud.update(engine, q2, 1280, 960, 960, true, 0.0));
    CHECK(!hud.empty() && hud.scale() == 3 && hud.image().width == 426 && hud.image().height == 320);
    {
        int barLit = 0;
        for (int y = 320 - 24; y < 320; y++)
            for (int x = 0; x < 426; x++) barLit += hudAlpha(x, y) == 255;
        CHECK(barLit > 0);
    }
    const int panelX = (426 - 256) / 2 - 8 + 1, panelY = (320 - 240) / 2 + 1;
    CHECK(hudAlpha(panelX, panelY) == int(realms::GameHudCanvas::kPanel[3]));
    CHECK(!hud.update(engine, q2, 1280, 960, 960, true, 0.0));
    q2.clientCommand("inven");
    for (int i = 0; i < 3; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(!q2.inventoryShown());
    CHECK(hud.update(engine, q2, 1280, 960, 960, true, 0.0) && hudAlpha(panelX, panelY) == 0);
    // the status bar off: nothing left to draw
    CHECK(hud.update(engine, q2, 1280, 960, 960, false, 0.0) && hud.empty());
    // cl_showfps over the game's HUD, at the top right
    engine.registerCvar("cl_showfps", "0", REALMS_CVAR_ARCHIVE);
    engine.setCvar("cl_showfps", "1");
    CHECK(hud.update(engine, q2, 1280, 960, 960, false, 0.05) && !hud.empty());
    {
        int fpsLit = 0, elsewhere = 0;
        for (int y = 0; y < 320; y++)
            for (int x = 0; x < 426; x++) (y < 8 && x >= 426 - 66 ? fpsLit : elsewhere) += hudAlpha(x, y) > 0;
        CHECK(fpsLit > 0 && elsewhere == 0); // "20.00fps" alone: the status bar is off
    }
    engine.setCvar("cl_showfps", "0");
    CHECK(hud.update(engine, q2, 1280, 960, 960, false, 0.1) && hud.empty());

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

    // Quake 2's save / load (F6 "save quick", F9 "load quick"): the player
    // walks away and dies after saving; loading puts them back, alive. The
    // map is loaded again by "load" (a host restarts the game on the map
    // change; respawn does that here).
    step("save and load");
    realms::sandbox::registerMapCommands(engine);
    const std::string slot = realms::gameSaveDir(engine, "slot1");
    CHECK(!slot.empty() && realms::gameSaveDir(engine, "../up").empty() && realms::gameSaveDir(engine, "").empty());
    const realms::Vec3 savedAt = q2.game()->playerOrigin();
    engine.exec("save slot1\n");
    CHECK(realms::gameSaveComplete(slot));
    {
        realms::GameSaveInfo info;
        std::string why;
        CHECK(realms::readGameSaveInfo(slot, info, why));
        CHECK(info.mapPath == engine.mapPath() && info.comment.rfind("arena  ", 0) == 0);
        CHECK(info.configStrings.size() > size_t(Q2_CS_MODELS + 1) && info.configStrings[Q2_CS_MODELS + 1] == engine.mapPath());
    }
    realms::WalkInput walk;
    walk.forward = 1.0f;
    for (int i = 0; i < 10; i++) view.update(engine, walk, yaw, 0.0f, 0.1f, false);
    const realms::Vec3 walkedTo = q2.game()->playerOrigin();
    CHECK(std::fabs(walkedTo[0] - savedAt[0]) + std::fabs(walkedTo[1] - savedAt[1]) > 32.0f);
    // Cmd_Kill_f ignores "kill" within 5 s of a respawn
    for (int i = 0; i < 55; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    engine.exec("kill\n");
    for (int i = 0; i < 3; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(q2.stats().health <= 0);
    // the death view (SV_CalcViewOffset): roll 40, pitch -15, whatever the
    // player's look, no kick
    {
        const realms::Vec3 dead = view.cameraAngles(yaw + 1.0f, 0.5f);
        CHECK(near(dead[2], 40.0f, 0.01f) && near(dead[0], -15.0f, 0.01f));
        const mapview::Quake2View::DrawAngles drawn = view.drawAngles(yaw + 1.0f, 0.5f);
        CHECK(near(drawn.roll, 40.0f * d2r, 1e-3f) && near(drawn.pitch, 15.0f * d2r, 1e-3f));
    }
    std::string refused;
    CHECK(!q2.game()->saveGame("dead", refused) && refused == "Can't savegame while dead!");
    CHECK(!realms::gameSaveComplete(realms::gameSaveDir(engine, "dead")));
    engine.exec("load nosuchsave\n");
    CHECK(engine.cvarString("sv_loadgame", "").empty());
    engine.exec("load slot1\n");
    CHECK(engine.mapLoaded() && engine.cvarString("sv_loadgame", "") == slot);
    CHECK(view.respawn(engine));
    CHECK(view.gameRunning() && engine.cvarString("sv_loadgame", "").empty());
    if (view.gameRunning()) {
        const realms::Vec3 loadedAt = q2.game()->playerOrigin();
        CHECK(near(loadedAt[0], savedAt[0], 0.5f) && near(loadedAt[1], savedAt[1], 0.5f) &&
              near(loadedAt[2], savedAt[2], 0.5f));
        CHECK(q2.stats().health == 100);
        view.update(engine, {}, yaw, 0.0f, 0.1f, false); // the loaded game runs on
        CHECK(view.gameRunning() && near(view.session().walker().origin[0], savedAt[0], 1.0f));
    }
    // the level copy and the build probes are gone
    CHECK(!std::filesystem::exists(std::filesystem::path(slot) / "load.sav") &&
          !std::filesystem::exists(std::filesystem::path(slot) / "probe.ssv"));
    // Demos: "record" a walk (turning, firing), "stop", then "demomap" plays
    // it back frame for frame: the same player, camera, door and HUD at
    // every step, without the player's input.
    step("record and play back");
    if (view.gameRunning()) {
        engine.clearConsole();
        engine.exec("record walk1\n");
        CHECK(q2.recording() && consoleHas(engine, "recording to demos/walk1.ekd."));
        engine.exec("record walk2\n");
        CHECK(consoleHas(engine, "record: Already recording."));
        struct Frame {
            realms::Vec3 origin, camera;
            int health = 0, armor = 0;
            size_t poses = 0;
            float door = 0.0f;
        };
        auto frameNow = [&](float lookYaw, float lookPitch) {
            Frame f;
            f.origin = view.session().walker().origin;
            f.camera = view.cameraAngles(lookYaw, lookPitch);
            f.health = q2.stats().health;
            f.armor = q2.stats().armor;
            f.poses = view.modelPoses().size();
            f.door = f.poses > 1 ? view.modelPoses()[1].transform[13] : 0.0f;
            return f;
        };
        std::vector<Frame> recorded;
        recorded.push_back(frameNow(yaw, 0.0f));
        realms::WalkInput walk2;
        walk2.forward = 1.0f;
        walk2.right = 0.3f;
        for (int i = 0; i < 20; i++) {
            const float look = yaw + 0.02f * float(i);
            view.update(engine, walk2, look, 0.0f, 0.05f, i % 5 == 0);
            recorded.push_back(frameNow(look, 0.0f));
        }
        engine.exec("stop\n");
        CHECK(!q2.recording() && consoleHas(engine, "Stopped demo: demos/walk1.ekd, 21 frames"));
        CHECK(std::filesystem::exists(realms::gameDemoPath(engine, "walk1")));
        view.close();

        engine.exec("demomap walk1\n");
        CHECK(engine.mapPath() == "maps/arena.bsp" && engine.cvarString(realms::kGameDemoCvar, "") == "demos/walk1.ekd");
        CHECK(view.open(engine, options, geometry, error));
        CHECK(q2.demoPlaying() && view.gameRunning() && q2.game() == nullptr);
        auto same = [](const Frame &a, const Frame &b) {
            bool s = a.health == b.health && a.armor == b.armor && a.poses == b.poses && near(a.door, b.door, 1e-4f);
            for (size_t k = 0; k < 3; k++) {
                // the game keeps its view angles in -180 .. 180 (SHORT2ANGLE)
                const float turn = std::remainder(a.camera[k] - b.camera[k], 360.0f);
                s = s && near(a.origin[k], b.origin[k], 1e-3f) && std::fabs(turn) <= 0.05f;
            }
            return s;
        };
        int matches = same(frameNow(yaw + 1.0f, 0.4f), recorded[0]) ? 1 : 0;
        for (int i = 0; i < 20; i++) {
            // other input, other look: ignored
            view.update(engine, {}, yaw + 1.0f, 0.4f, 0.05f, true);
            if (same(frameNow(yaw + 1.0f, 0.4f), recorded[size_t(i) + 1])) matches++;
            else std::fprintf(stderr, "  playback frame %d differs\n", i + 1);
        }
        CHECK(matches == 21);
        CHECK(q2.demoPlaying());
        view.update(engine, {}, yaw, 0.0f, 0.05f, false);
        CHECK(!q2.demoPlaying() && !view.gameRunning() && consoleHas(engine, "Demo finished: 21 frames, 1.0 seconds."));

        // timedemo: the same frames, one per update, as fast as they come
        engine.exec("set timedemo 1\n");
        view.close();
        engine.exec("demomap walk1\n");
        CHECK(view.open(engine, options, geometry, error) && q2.timeDemoRunning());
        int timedMatches = same(frameNow(yaw, 0.0f), recorded[0]) ? 1 : 0;
        for (int i = 0; i < 20; i++) {
            view.update(engine, {}, yaw, 0.0f, 0.001f, false);
            if (same(frameNow(yaw, 0.0f), recorded[size_t(i) + 1])) timedMatches++;
        }
        CHECK(timedMatches == 21);
        view.update(engine, {}, yaw, 0.0f, 0.001f, false);
        CHECK(!q2.demoPlaying() && q2.lastTimeDemo().frames == 20 && q2.lastTimeDemo().fps > 0.0);
        engine.exec("set timedemo 0\n");

        // a map change stops a recording
        view.close();
        CHECK(view.open(engine, options, geometry, error) && view.gameRunning() && !q2.demoPlaying());
        engine.exec("record walk3\n");
        view.update(engine, {}, yaw, 0.0f, 0.05f, false);
        CHECK(q2.recording());
        view.close();
        CHECK(!q2.recording() && consoleHas(engine, "Stopped demo: demos/walk3.ekd, 2 frames"));
    }

    step("close");
    view.close();
    CHECK(!view.active() && !view.gameRunning());
}

} // namespace

struct ModelPoseCheck {
    bool visible = false, placedEqualsTransform = false;
};

ModelPoseCheck poseCheck(const mapview::Quake2View &view, size_t model) {
    ModelPoseCheck c;
    if (model >= view.modelPoses().size()) return c;
    const mapview::ModelPose &p = view.modelPoses()[model];
    c.visible = p.visible;
    c.placedEqualsTransform = p.placed == p.transform;
    return c;
}

// The game on a glTF map: node names spawn the player, items and monsters
// (realms/MeshMap.h); the player picks the shotgun up walking +X.
void gameOnGltf(realms::Engine &engine) {
    CHECK(engine.loadMap("maps/outpost.glb"));
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    CHECK(view.session().mesh() && view.gameRunning());
    if (!view.gameRunning()) return;
    CHECK(view.session().summary().find("glTF mesh") != std::string::npos);
    CHECK(view.session().summary().find(", game") != std::string::npos);
    realms::Quake2Module &q2 = view.module();
    CHECK(q2.status().rfind("HP 100", 0) == 0);
    const realms::MapWalker &w = view.session().walker();
    CHECK(near(w.origin[0], -96, 0.2f) && near(w.origin[1], 0, 0.2f));
    CHECK(view.modelPoses().size() <= 1); // no brush models on mesh maps
    // no "sky" in the scene extras: no skybox, though env/unit1_ exists
    CHECK(q2.sky().empty());
    CHECK(view.session().summary().find(", sky") == std::string::npos);

    // a couple of frames: items and monsters drop to the floor
    const float yaw = view.spawnRenderYaw();
    for (int i = 0; i < 4; i++) view.update(engine, {}, yaw, 0.0f, 0.1f, false);
    CHECK(view.gameRunning());
    bool shotgun = false, health = false, soldier = false, barrel = false;
    float floorTop = 0.0f;
    for (const realms::GameRenderEntity &e : q2.game()->renderEntities()) {
        if (e.model == "models/weapons/g_shotg/tris.md2") {
            shotgun = near(e.origin[0], -32, 0.5f) && near(e.origin[1], 0, 0.5f);
            floorTop = e.origin[2] + e.mins[2];
        } else if (e.model == "models/items/healing/medium/tris.md2") {
            health = near(e.origin[0], 32, 0.5f) && near(e.origin[1], -64, 0.5f); // not "*1"
        } else if (e.model == "models/monsters/soldier/tris.md2") {
            soldier = near(e.origin[0], 160, 0.5f) && near(e.origin[1], 0, 0.5f) && near(e.angles[1], 90, 0.5f);
            CHECK(near(e.origin[2] + e.mins[2], 0.0f, 2.5f)); // stands on the floor
        } else if (e.model == "models/objects/barrels/tris.md2") {
            barrel = near(e.origin[0], 64, 0.5f) && near(e.origin[1], 64, 0.5f);
        }
    }
    CHECK(shotgun && health && soldier && barrel);
    CHECK(near(floorTop, 0.0f, 2.5f)); // dropped from 24 units up onto the slab

    // walk +X onto the shotgun
    realms::WalkInput in;
    in.forward = 1.0f;
    bool picked = false;
    for (int i = 0; i < 40 && !picked; i++) {
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        for (const std::string &m : view.takeMessages()) picked |= m == "Shotgun";
    }
    CHECK(picked);
    CHECK(view.gameRunning());

    // respawn restarts the game on the same map
    CHECK(view.respawn(engine) && view.gameRunning());
    CHECK(near(view.session().walker().origin[0], -96, 0.2f));
    view.close();
}

// glTF brush entities: the door opens when walked into and lets the player
// through, the fan turns about its origin.
void doorsOnGltf(realms::Engine &engine) {
    CHECK(engine.loadMap("maps/gate.glb"));
    mapview::Quake2View view;
    mapview::Quake2ViewOptions options;
    realms::MapGeometry geometry;
    std::string error;
    CHECK(view.open(engine, options, geometry, error));
    CHECK(view.gameRunning());
    if (!view.gameRunning()) return;
    // world + door + fan; the door without meshes is not exported
    CHECK(engine.meshMap().models.size() == 2);
    CHECK(view.modelPoses().size() == 3);
    if (view.modelPoses().size() != 3) return;
    // geometry batches per model, the fan relative to its origin
    bool fanLocal = false, doorBatch = false;
    for (const realms::MapGeometryBatch &b : geometry.batches) {
        doorBatch |= b.model == 1;
        if (b.model == 2 && !b.positions.empty()) fanLocal = std::fabs(b.positions[0]) <= 64.5f;
    }
    CHECK(doorBatch && fanLocal);
    // the scene extras name a sky: drawn behind the glTF scene, turning
    const realms::MapSky &sky = view.module().sky();
    CHECK(!sky.empty() && sky.settings().name == "testsky_" && near(sky.settings().rotate, 20.0f, 1e-6f));
    CHECK(sky.size() == 16 && sky.pixels().size() == 6 * 16 * 16 * 4);
    if (sky.pixels().size() == 6 * 16 * 16 * 4)
        CHECK(rgbIs(sky.pixels().data() + 4 * 37, kSkyColours[0][0], kSkyColours[0][1], kSkyColours[0][2]));
    std::vector<sandbox::MeshVertex> vertices;
    std::vector<mapview::ModelRange> ranges;
    std::vector<float> lightUvs;
    view.triangles(geometry, vertices, ranges, lightUvs);
    CHECK(ranges.size() == 3);

    const float yaw = view.spawnRenderYaw();
    realms::WalkInput in;
    in.forward = 1.0f;
    float doorRise = 0.0f;
    std::array<float, 16> fanStart = view.modelPoses()[2].transform;
    for (int i = 0; i < 80; i++) {
        view.update(engine, in, yaw, 0.0f, 0.05f, false);
        doorRise = std::max(doorRise, view.modelPoses()[1].transform[13]);
    }
    CHECK(doorRise > 2.0f);                                  // render metres: 90 units up
    CHECK(view.session().walker().origin[0] > 32.0f);        // through the doorway
    const ModelPoseCheck door = poseCheck(view, 1);
    CHECK(door.visible && door.placedEqualsTransform);       // no origin: the same matrix
    const mapview::ModelPose &fan = view.modelPoses()[2];
    CHECK(fan.visible);
    // the fan turns about map (160, -160, 32) = render (5, 1, 5)
    CHECK(near(fan.transform[12], 5.0f, 1e-4f) && near(fan.transform[13], 1.0f, 1e-4f) &&
          near(fan.transform[14], 5.0f, 1e-4f));
    CHECK(std::fabs(fan.transform[0] - fanStart[0]) > 0.01f || std::fabs(fan.transform[2] - fanStart[2]) > 0.01f);
    // placed: the file's own vertices; the origin itself stays put
    std::array<float, 16> placed{};
    CHECK(view.placedTransform(2, placed));
    const float o[3] = {5.0f, 1.0f, 5.0f};
    for (int i = 0; i < 3; i++)
        CHECK(near(placed[size_t(i)] * o[0] + placed[size_t(4 + i)] * o[1] + placed[size_t(8 + i)] * o[2] +
                       placed[size_t(12 + i)],
                   o[i], 1e-4f));
    CHECK(view.placedTransform(0, placed) && placed[12] == 0.0f && placed[0] == 1.0f);
    CHECK(view.placedTransform(9, placed) && placed[13] == 0.0f);
    CHECK(view.gameRunning());
    view.close();
}

int main(int argc, char **argv) {
#if defined(_WIN32)
    // The Windows CRT has no line buffering and rejects _IOLBF with size 0
    // (invalid parameter: the process is terminated); unbuffered instead.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
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
        const std::string pool = bsp_builder::Builder(surfaces()).build();
        std::ofstream(tmp / "maps" / "surfaces.bsp", std::ios::binary)
            .write(pool.data(), std::streamsize(pool.size()));
    }
    {
        const std::string outpost = gltf_builder::outpostGlb();
        std::ofstream(tmp / "maps" / "outpost.glb", std::ios::binary)
            .write(outpost.data(), std::streamsize(outpost.size()));
        const std::string gate = gltf_builder::gateGlb();
        std::ofstream(tmp / "maps" / "gate.glb", std::ios::binary).write(gate.data(), std::streamsize(gate.size()));
    }
    writeBlaster(tmp);
    writeSky(tmp);
    writeWal(tmp, "e1u1/floor", 64, 64, [](uint32_t, uint32_t) { return 0xd0; });
    writeWal(tmp, "e1u1/wall", 32, 128, [](uint32_t, uint32_t y) { return y < 64 ? 0xf0 : 0x0f; });
    writeWal(tmp, "e1u1/water", 128, 32, [](uint32_t x, uint32_t) { return 0x60 + (x & 7); });
    for (const char *name : {"e1u1/conveyor", "e1u1/glass", "e1u1/anim0", "e1u1/anim1", "e1u1/anim2", "e1u1/open0",
                             "e1u1/open1", "e1u1/missing0"})
        writeWal(tmp, name, 64, 64, [name](uint32_t, uint32_t) { return uint8_t(0x20 + std::strlen(name)); });
    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {tmp.string()};
    cfg.writableRoot = tmp.string(); // save games
    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.loadMap("maps/arena.bsp"));
    const std::string backend = engine.backendName();
    if (!pascalLibrary.empty()) CHECK(engine.pascalBackend());

    step("wal decoding");
    walDecoding();
    step("gender");
    genderChecks(engine);
    step("stufftext");
    stuffTextChecks(engine);
    step("sky math");
    skyMath();
    step("module without game");
    moduleWithoutGame(engine);
    step("view without game");
    viewWithoutGame(engine);
    step("demo without game");
    demoWithoutGame(engine);
    step("network client without game");
    netWithoutGame(engine);
    step("surface effects");
    surfaceEffects(engine);

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
        step("game on a glTF map");
        gameOnGltf(engine);
        step("doors on a glTF map");
        doorsOnGltf(engine);
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
