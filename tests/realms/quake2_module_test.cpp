// Quake2Module and Quake2View on a generated, lightmapped arena.
//
//   realms_quake2_module_test [--game librealms_game.so|linked] [--pascal librealms_core.so|static]
//
// "linked" needs -DREALMS_GAME_STATIC and librealms_game.a. Without a game
// module the game part is skipped and the exit code is 77.
#include "bsp_builder.h"
#include "gltf_builder.h"
#include "md2_builder.h"

#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/MapSky.h"
#include "realms/MapTextures.h"
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
    writeFileBytes(dir / "env/testsky_dn.pcx", md2_builder::writePcx(8, 8, std::vector<uint8_t>(64, 200)));
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
    CHECK(s.name == "unit1_" && s.rotate == 0.0f);
    s = realms::skySettings("{\n\"sky\" \"space1_\"\n\"skyrotate\" \"-4.5\"\n\"skyaxis\" \"0 1 2\"\n"
                            "\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"light\"\n\"sky\" \"x\"\n}\n");
    CHECK(s.name == "space1_" && near(s.rotate, -4.5f, 1e-6f) && s.axis[0] == 0 && s.axis[1] == 1 && s.axis[2] == 2);

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
                   "\"skyrotate\" \"90\"\n\"skyaxis\" \"0 0 1\"\n}\n"
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
    {
        const std::string outpost = gltf_builder::outpostGlb();
        std::ofstream(tmp / "maps" / "outpost.glb", std::ios::binary)
            .write(outpost.data(), std::streamsize(outpost.size()));
    }
    writeBlaster(tmp);
    writeSky(tmp);
    writeWal(tmp, "e1u1/floor", 64, 64, [](uint32_t, uint32_t) { return 0xd0; });
    writeWal(tmp, "e1u1/wall", 32, 128, [](uint32_t, uint32_t y) { return y < 64 ? 0xf0 : 0x0f; });
    realms::EngineConfig cfg;
    cfg.backend = pascalLibrary.empty() ? realms::BackendPreference::Native : realms::BackendPreference::Pascal;
    cfg.pascalLibrary = pascalLibrary;
    cfg.searchRoots = {tmp.string()};
    realms::Engine engine;
    CHECK(engine.init(cfg));
    CHECK(engine.loadMap("maps/arena.bsp"));
    const std::string backend = engine.backendName();
    if (!pascalLibrary.empty()) CHECK(engine.pascalBackend());

    step("wal decoding");
    walDecoding();
    step("sky math");
    skyMath();
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
        step("game on a glTF map");
        gameOnGltf(engine);
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
