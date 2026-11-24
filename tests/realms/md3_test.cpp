// Md3Model.cpp: parsing, frame blending, normals and winding, texture
// coordinates, surfaces and their skins, tags, corrupt and fuzzed models;
// GameModelCache (the game's .md2 names load .md3 files, TGA / PCX skins);
// then PCX decoding and corrupt PCX files. Test data comes from
// md3_builder.h.
#include "md3_builder.h"

#include "realms/Engine.h"
#include "realms/GameView.h"
#include "realms/Image.h"
#include "realms/Md3Model.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++gChecks;                                                                      \
        if (!(cond)) {                                                                  \
            ++gFailures;                                                                \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                               \
    } while (0)

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

using Box = std::pair<std::array<float, 3>, std::array<float, 3>>;
const std::vector<Box> kFrames = {{{-16, -16, -24}, {16, 16, 32}}, {{-16, -16, -24}, {48, 16, 32}}};

// every triangle counter-clockwise seen from outside: its face normal points
// away from the box centre, and agrees with the vertex normals
void checkOutward(const std::vector<realms::ModelVertex> &v, const float centre[3], const char *what) {
    bool ccw = true, normals = true;
    for (size_t t = 0; t + 2 < v.size(); t += 3) {
        const realms::ModelVertex &a = v[t], &b = v[t + 1], &c = v[t + 2];
        const float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float mid[3] = {(a.x + b.x + c.x) / 3 - centre[0], (a.y + b.y + c.y) / 3 - centre[1],
                              (a.z + b.z + c.z) / 3 - centre[2]};
        if (n[0] * mid[0] + n[1] * mid[1] + n[2] * mid[2] <= 0) ccw = false;
        for (const realms::ModelVertex *p : {&a, &b, &c}) {
            // flat face normals: along the face normal, unit length
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0 && (p->nx * n[0] + p->ny * n[1] + p->nz * n[2]) / len < 0.99f) normals = false;
            if (!near(p->nx * p->nx + p->ny * p->ny + p->nz * p->nz, 1, 1e-4f)) normals = false;
        }
    }
    if (!ccw) std::fprintf(stderr, "  (%s: winding)\n", what);
    if (!normals) std::fprintf(stderr, "  (%s: normals)\n", what);
    CHECK(ccw);
    CHECK(normals);
}

float maxX(const std::vector<realms::ModelVertex> &v) {
    float m = -1e9f;
    for (const auto &p : v) m = std::max(m, p.x);
    return m;
}

void modelChecks() {
    for (bool clockwise : {true, false}) {
        auto def = md3_builder::boxModel(kFrames, clockwise);
        def.tagNames = {"tag_weapon", "tag_flash"};
        def.tagOrigins = {{{8, 0, 4}, {24, 0, 4}}, {{40, 0, 4}, {56, 0, 4}}};
        const auto bytes = md3_builder::writeMd3(def);
        realms::Md3Model m;
        std::string err;
        CHECK(m.parse(bytes, err));
        CHECK(err.empty());
        CHECK(m.valid());
        CHECK(m.name() == "models/test/tris.md3");
        CHECK(m.numSurfaces() == 1 && m.numTriangles() == 12 && m.numFrames() == 2 && m.numTags() == 2);
        CHECK(m.surfaces()[0].numVerts == 24 && m.surfaces()[0].name == "body");
        CHECK(m.surfaces()[0].shaders.size() == 1 && m.skin(0, 0) == "models/test/skin.pcx");
        CHECK(m.findFrame("frame2") == 1 && m.findFrame("run1") == -1);
        CHECK(m.flippedWinding() == clockwise);
        const auto &f0 = m.frames()[0], &f1 = m.frames()[1];
        CHECK(f0.name == "frame1" && f1.name == "frame2");
        CHECK(near(f0.mins[2], -24, 1e-6f) && near(f0.maxs[2], 32, 1e-6f) && near(f1.maxs[0], 48, 1e-6f));
        CHECK(near(f0.radius, 64, 1e-6f));
        // positions are stored in 1/64 units: whole units come back exactly
        CHECK(near(m.surfaces()[0].positions[6 * 3 + 0], 16, 1e-6f));

        // tags per frame; a frame out of range is frame 0
        const auto *t = m.tag(1, "tag_flash");
        CHECK(t && near(t->origin[0], 56, 1e-6f) && near(t->axis[0][0], 1, 1e-6f) && near(t->axis[2][2], 1, 1e-6f));
        t = m.tag(9, "tag_weapon");
        CHECK(t && near(t->origin[0], 8, 1e-6f));
        CHECK(m.tag(0, "tag_head") == nullptr);

        std::vector<realms::ModelVertex> v;
        m.buildVertices(0, 0, 0.0f, v);
        CHECK(v.size() == 36);
        const float c0[3] = {0, 0, 4};
        checkOutward(v, c0, clockwise ? "cw frame 0" : "ccw frame 0");
        // texture coordinates as stored
        bool uvOk = true;
        for (const auto &p : v) uvOk &= (p.u == 0 || p.u == 1) && (p.v == 0 || p.v == 1);
        CHECK(uvOk);

        // halfway between the frames: the +X face is at 32
        std::vector<realms::ModelVertex> h;
        m.buildVertices(1, 0, 0.5f, h);
        CHECK(near(maxX(h), 32, 1e-4f));
        const float c1[3] = {16, 0, 4};
        checkOutward(h, c1, "lerped");
        // backlerp 0 = frame, 1 = oldframe; out of range values clamp
        std::vector<realms::ModelVertex> a, b;
        m.buildVertices(1, 0, 0.0f, a);
        m.buildVertices(0, 1, 1.0f, b);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); i++) same = near(a[i].x, b[i].x, 1e-5f);
        CHECK(same);
        std::vector<realms::ModelVertex> c;
        m.buildVertices(1, 0, 7.0f, c); // clamped to 1: frame 0
        CHECK(near(maxX(c), 16, 1e-4f));
        // "no such frame": frame 0
        std::vector<realms::ModelVertex> d;
        m.buildVertices(99, -3, std::nanf(""), d);
        CHECK(d.size() == 36 && near(maxX(d), 16, 1e-4f));
        // appends; a surface out of range adds nothing
        m.buildVertices(0, 0, 0, 0, d);
        CHECK(d.size() == 72);
        m.buildVertices(1, 0, 0, 0, d);
        m.buildVertices(-1, 0, 0, 0, d);
        CHECK(d.size() == 72);
    }

    // two surfaces with their own shaders: Quake 2 skin numbers pick a
    // surface's shader, else its first; texture coordinates may repeat
    {
        auto def = md3_builder::boxModel(kFrames);
        def.surfaces[0].shaders = {"models/monsters/x/skin.pcx", "models/monsters/x/pain.pcx"};
        auto gun = md3_builder::boxSurface({{{16, -2, -2}, {40, 2, 2}}, {{16, -2, -2}, {44, 2, 2}}});
        gun.name = "gun";
        gun.shaders = {"models/monsters/x/gun.tga"};
        for (auto &st : gun.st) st = {st[0] * 3, st[1] * 2};
        def.surfaces.push_back(gun);
        auto bare = md3_builder::boxSurface({{{-1, -1, 40}, {1, 1, 42}}, {{-1, -1, 40}, {1, 1, 42}}});
        bare.shaders.clear();
        def.surfaces.push_back(bare);
        realms::Md3Model m;
        std::string err;
        CHECK(m.parse(md3_builder::writeMd3(def), err));
        CHECK(m.numSurfaces() == 3 && m.numTriangles() == 36);
        CHECK(m.skin(0, 0) == "models/monsters/x/skin.pcx" && m.skin(0, 1) == "models/monsters/x/pain.pcx");
        CHECK(m.skin(0, 5) == "models/monsters/x/skin.pcx" && m.skin(0, -1) == "models/monsters/x/skin.pcx");
        CHECK(m.skin(1, 1) == "models/monsters/x/gun.tga");
        CHECK(m.skin(2, 0).empty() && m.skin(3, 0).empty());
        std::vector<realms::ModelVertex> all, gunOnly;
        m.buildVertices(0, 0, 0, all);
        CHECK(all.size() == 108);
        m.buildVertices(1, 1, 0, 0, gunOnly);
        CHECK(gunOnly.size() == 36 && near(maxX(gunOnly), 44, 1e-4f));
        float maxU = 0;
        for (const auto &p : gunOnly) maxU = std::max(maxU, p.u);
        CHECK(near(maxU, 3, 1e-6f));
    }

    // names
    CHECK(realms::md3PathFor("models/items/armor/body/tris.md2") == "models/items/armor/body/tris.md3");
    CHECK(realms::md3PathFor("models/x/TRIS.MD2") == "models/x/TRIS.MD3");
    CHECK(realms::md3PathFor("models/x/tris.md3") == "models/x/tris.md3");
    CHECK(realms::md3PathFor("sprites/s_bfg2.sp2") == "sprites/s_bfg2.sp2");
    CHECK(realms::md3PathFor("md2") == "md2");
    CHECK(realms::isAliasModelName("a/tris.md2") && realms::isAliasModelName("a/tris.MD3"));
    CHECK(!realms::isAliasModelName("*3") && !realms::isAliasModelName("a.sp2") && !realms::isAliasModelName(""));
}

void corruptChecks() {
    const auto good = md3_builder::writeMd3(md3_builder::boxModel(kFrames));
    constexpr size_t kSurf = 108 + 2 * 56; // first surface: no tags
    struct Corruption {
        const char *what;
        size_t offset;
        int32_t value;
    };
    const Corruption cases[] = {
        {"ident", 0, 0x33504449 + 1},
        {"version", 4, 8},
        {"frames", 76, 0},
        {"frames big", 76, 1025},
        {"tags neg", 80, -1},
        {"tags big", 80, 17},
        {"tags past end", 80, 16},
        {"surfaces", 84, 0},
        {"surfaces big", 84, 33},
        {"surfaces past end", 84, 2},
        {"ofs frames", 92, 1 << 30},
        {"ofs tags", 96, -8},
        {"ofs surfaces", 100, 1 << 20},
        {"ofs end", 104, 1 << 24},
        {"ofs end small", 104, 20},
        {"surface ident", kSurf, 0},
        {"surface frames", kSurf + 72, 1},
        {"surface shaders", kSurf + 76, 257},
        {"surface shaders past end", kSurf + 76, 200},
        {"surface verts", kSurf + 80, 0},
        {"surface verts big", kSurf + 80, 4097},
        {"surface verts past end", kSurf + 80, 1000},
        {"surface tris", kSurf + 84, 0},
        {"surface tris big", kSurf + 84, 8193},
        {"surface ofs tris", kSurf + 88, -4},
        {"surface ofs shaders", kSurf + 92, 1 << 20},
        {"surface ofs st", kSurf + 96, 1 << 24},
        {"surface ofs xyz", kSurf + 100, 1 << 16},
        {"surface end", kSurf + 104, 1 << 16},
        {"surface end small", kSurf + 104, 0},
    };
    for (const Corruption &c : cases) {
        auto b = good;
        md3_builder::putInt(b, c.offset, c.value);
        realms::Md3Model m;
        std::string err;
        const bool ok = m.parse(b, err);
        if (ok) std::fprintf(stderr, "  (accepted corrupt model: %s)\n", c.what);
        CHECK(!ok && !err.empty() && !m.valid());
    }
    // a triangle pointing past the vertices
    {
        auto def = md3_builder::boxModel(kFrames);
        def.surfaces[0].tris[3][1] = 24;
        realms::Md3Model m;
        std::string err;
        CHECK(!m.parse(md3_builder::writeMd3(def), err) && err.find("vertex index") != std::string::npos);
        def = md3_builder::boxModel(kFrames);
        def.surfaces[0].tris[5][2] = -1;
        CHECK(!m.parse(md3_builder::writeMd3(def), err) && err.find("vertex index") != std::string::npos);
    }
    // NaN bounds, NaN texture coordinates
    {
        auto b = good;
        md3_builder::putFloat(b, 108 + 56 + 4, std::nanf(""));
        realms::Md3Model m;
        std::string err;
        CHECK(!m.parse(b, err) && err.find("bounds") != std::string::npos);
        auto def = md3_builder::boxModel(kFrames);
        def.surfaces[0].st[7][1] = std::nanf("");
        CHECK(!m.parse(md3_builder::writeMd3(def), err) && err.find("texture") != std::string::npos);
    }
    // truncated anywhere, and random corruption: never crashes, and a model
    // that is accepted is self-consistent
    for (size_t n = 0; n < good.size(); n += 7) {
        realms::Md3Model m;
        std::string err;
        CHECK(!m.parse(good.data(), n, err));
    }
    int accepted = 0;
    std::mt19937 rng(1234);
    for (int i = 0; i < 3000; i++) {
        auto b = good;
        const int flips = 1 + int(rng() % 6);
        for (int k = 0; k < flips; k++) b[rng() % b.size()] = uint8_t(rng());
        realms::Md3Model m;
        std::string err;
        if (m.parse(b, err)) {
            accepted++;
            std::vector<realms::ModelVertex> v;
            m.buildVertices(int(rng() % 4), int(rng() % 4), 0.3f, v);
            CHECK(v.size() == size_t(m.numTriangles()) * 3);
            bool finite = true;
            for (const auto &p : v) finite &= std::isfinite(p.x) && std::isfinite(p.nx) && std::isfinite(p.u);
            CHECK(finite);
        }
    }
    CHECK(accepted > 0); // flips in names / coordinates are harmless
    realms::Md3Model m;
    std::string err;
    CHECK(!m.parse(nullptr, 100, err));
}

void writeFile(const std::filesystem::path &p, const std::vector<unsigned char> &bytes) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// An uncompressed 24-bit TGA, every pixel (r, g, b).
std::vector<unsigned char> tga(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<unsigned char> t(18, 0);
    t[2] = 2;
    t[12] = uint8_t(w), t[13] = uint8_t(w >> 8), t[14] = uint8_t(h), t[15] = uint8_t(h >> 8);
    t[16] = 24;
    for (int i = 0; i < w * h; i++) t.insert(t.end(), {b, g, r});
    return t;
}

// The game's model names load .md3 files from the VFS; .md2 files are not
// read. Skins: TGA or PCX, falling back to the other format.
void cacheChecks() {
    namespace fs = std::filesystem;
    const fs::path tmp =
        fs::temp_directory_path() / ("realms-md3-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    writeFile(tmp / "models/items/armor/body/tris.md3", md3_builder::writeMd3(md3_builder::boxModel(kFrames)));
    writeFile(tmp / "models/items/old/tris.md2", std::vector<unsigned char>(200, 7)); // only an .md2
    writeFile(tmp / "models/items/bad/tris.md3", std::vector<unsigned char>(50, 0));
    writeFile(tmp / "models/test/skin.tga", tga(4, 2, 200, 100, 50));
    writeFile(tmp / "models/test/other.pcx", md3_builder::writePcx(8, 8, std::vector<uint8_t>(64, 9)));

    realms::EngineConfig cfg;
    cfg.backend = realms::BackendPreference::Native;
    cfg.searchRoots = {tmp.string()};
    realms::Engine engine;
    CHECK(engine.init(cfg));
    realms::GameModelCache cache;
    const realms::Md3Model *m = cache.model(engine, "models/items/armor/body/tris.md2");
    CHECK(m && m->numTriangles() == 12);
    CHECK(cache.model(engine, "models/items/armor/body/tris.md2") == m); // cached
    CHECK(cache.model(engine, "models/items/armor/body/tris.md3") != nullptr);
    CHECK(cache.model(engine, "models/items/old/tris.md2") == nullptr);
    CHECK(cache.model(engine, "models/items/bad/tris.md2") == nullptr);
    CHECK(cache.model(engine, "models/items/bad/tris.md2") == nullptr); // reported once
    CHECK(cache.loaded() == 2 && cache.missing() == 2);
    const auto errs = cache.takeErrors();
    CHECK(errs.size() == 2);
    if (errs.size() == 2) {
        CHECK(errs[0] == "models/items/old/tris.md3: not found");
        CHECK(errs[1].rfind("models/items/bad/tris.md3: ", 0) == 0);
    }
    CHECK(cache.takeErrors().empty());

    realms::RgbaImage img;
    std::string err;
    // named .pcx, only a .tga there: the .tga
    CHECK(realms::GameModelCache::loadSkin(engine, "models/test/skin.pcx", img, err));
    CHECK(img.width == 4 && img.height == 2 && img.rgba[0] == 200 && img.rgba[1] == 100 && img.rgba[2] == 50);
    CHECK(realms::GameModelCache::loadSkin(engine, "models/test/skin.tga", img, err) && img.width == 4);
    CHECK(realms::GameModelCache::loadSkin(engine, "models/test/skin", img, err) && img.width == 4); // Quake 3 style, no extension
    // named .tga, only a .pcx there: the .pcx
    CHECK(realms::GameModelCache::loadSkin(engine, "models/test/other.tga", img, err) && img.width == 8);
    CHECK(!realms::GameModelCache::loadSkin(engine, "models/test/none.tga", img, err) && err == "not found");
    CHECK(!realms::GameModelCache::loadSkin(engine, "", img, err) && img.rgba.empty());

    std::error_code ec;
    fs::remove_all(tmp, ec);
}

void pcxChecks() {
    const int w = 70, h = 5;
    std::vector<uint8_t> idx(size_t(w * h));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) idx[size_t(y * w + x)] = uint8_t(x < 66 ? 200 : (x * 3 + y) & 0xff); // long runs + literals >= 0xC0
    for (bool pad : {false, true}) {
        const auto bytes = md3_builder::writePcx(w, h, idx, pad);
        realms::RgbaImage img;
        std::string err;
        CHECK(realms::decodePcx(bytes, img, err));
        CHECK(img.width == w && img.height == h && img.rgba.size() == size_t(w * h * 4));
        bool ok = true;
        for (int i = 0; i < w * h; i++) {
            const uint8_t v = idx[size_t(i)];
            ok &= img.rgba[size_t(i) * 4] == v && img.rgba[size_t(i) * 4 + 1] == 255 - v &&
                  img.rgba[size_t(i) * 4 + 2] == v / 2 && img.rgba[size_t(i) * 4 + 3] == 255;
        }
        CHECK(ok);
        realms::RgbaImage t;
        CHECK(realms::decodePcx(bytes, t, err, 200));
        CHECK(t.rgba[3] == 0 && t.rgba[size_t(68) * 4 + 3] == 255);
    }
    const auto good = md3_builder::writePcx(w, h, idx);
    auto bad = [&](size_t at, uint8_t v) {
        auto b = good;
        b[at] = v;
        realms::RgbaImage img;
        std::string err;
        return !realms::decodePcx(b, img, err) && !err.empty() && img.rgba.empty();
    };
    CHECK(bad(0, 0x0b));               // manufacturer
    CHECK(bad(3, 4));                  // 4 bits per pixel
    CHECK(bad(65, 3));                 // 3 planes
    CHECK(bad(66, 10));                // bytes per line < width
    CHECK(bad(good.size() - 769, 0));  // palette marker
    CHECK(bad(9, 0x7f));               // xmax far too big: truncated data
    {
        realms::RgbaImage img;
        std::string err;
        std::vector<unsigned char> cut(good.begin(), good.begin() + 140);
        cut.insert(cut.end(), good.end() - 769, good.end());
        CHECK(!realms::decodePcx(cut, img, err)); // image data truncated
        CHECK(!realms::decodePcx(good.data(), 100, img, err));
    }
    std::mt19937 rng(99);
    for (int i = 0; i < 1000; i++) {
        auto b = good;
        for (int k = 0; k < 4; k++) b[rng() % b.size()] = uint8_t(rng());
        realms::RgbaImage img;
        std::string err;
        if (realms::decodePcx(b, img, err)) CHECK(img.rgba.size() == size_t(img.width) * size_t(img.height) * 4);
    }
}

} // namespace

int main() {
    modelChecks();
    corruptChecks();
    cacheChecks();
    pcxChecks();
    std::printf("[md3] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
