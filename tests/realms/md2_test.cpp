// Md2Model.cpp: parsing, frame blending, normals and winding, skin
// coordinates, corrupt and fuzzed models, then PCX decoding and corrupt PCX
// files. Test data comes from md2_builder.h.
#include "md2_builder.h"

#include "realms/Md2Model.h"

#include <cmath>
#include <cstdio>
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
void checkOutward(const std::vector<realms::Md2Vertex> &v, const float centre[3], const char *what) {
    bool ccw = true, normals = true;
    for (size_t t = 0; t + 2 < v.size(); t += 3) {
        const realms::Md2Vertex &a = v[t], &b = v[t + 1], &c = v[t + 2];
        const float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float mid[3] = {(a.x + b.x + c.x) / 3 - centre[0], (a.y + b.y + c.y) / 3 - centre[1],
                              (a.z + b.z + c.z) / 3 - centre[2]};
        if (n[0] * mid[0] + n[1] * mid[1] + n[2] * mid[2] <= 0) ccw = false;
        for (const realms::Md2Vertex *p : {&a, &b, &c}) {
            const float rel[3] = {p->x - centre[0], p->y - centre[1], p->z - centre[2]};
            if (p->nx * rel[0] + p->ny * rel[1] + p->nz * rel[2] <= 0) normals = false;
            if (!near(p->nx * p->nx + p->ny * p->ny + p->nz * p->nz, 1, 1e-4f)) normals = false;
        }
    }
    if (!ccw) std::fprintf(stderr, "  (%s: winding)\n", what);
    if (!normals) std::fprintf(stderr, "  (%s: normals)\n", what);
    CHECK(ccw);
    CHECK(normals);
}

void modelChecks() {
    for (bool clockwise : {true, false}) {
        const auto bytes = md2_builder::writeMd2(md2_builder::boxModel(kFrames, clockwise));
        realms::Md2Model m;
        std::string err;
        CHECK(m.parse(bytes, err));
        CHECK(err.empty());
        CHECK(m.valid());
        CHECK(m.skinWidth() == 64 && m.skinHeight() == 32);
        CHECK(m.numVerts() == 8 && m.numTriangles() == 12 && m.numFrames() == 2);
        CHECK(m.skins().size() == 1 && m.skins()[0] == "models/test/skin.pcx");
        CHECK(m.findFrame("frame2") == 1 && m.findFrame("run1") == -1);
        CHECK(m.flippedWinding() == clockwise);
        // decompressed within half a quantisation step
        const auto &f0 = m.frames()[0], &f1 = m.frames()[1];
        CHECK(f0.name == "frame1");
        CHECK(near(f0.positions[6 * 3 + 0], 16, 0.07f) && near(f0.positions[6 * 3 + 2], 32, 0.12f));
        CHECK(near(f1.positions[6 * 3 + 0], 48, 0.13f));
        CHECK(near(f0.mins[2], -24, 1e-4f) && near(f0.maxs[2], 32, 0.12f));

        std::vector<realms::Md2Vertex> v;
        m.buildVertices(0, 0, 0.0f, v);
        CHECK(v.size() == 36);
        const float c0[3] = {0, 0, 4};
        checkOutward(v, c0, clockwise ? "cw frame 0" : "ccw frame 0");
        // skin coordinates: texel centres, 0..1
        bool uvOk = true;
        for (const auto &p : v) uvOk &= p.u > 0 && p.u < 1 && p.v > 0 && p.v < 1;
        CHECK(uvOk);
        CHECK(near(v[0].u, 0.5f / 64, 1e-6f) || near(v[0].u, 63.5f / 64, 1e-6f));

        // halfway between the frames: the +X face is at 32
        std::vector<realms::Md2Vertex> h;
        m.buildVertices(1, 0, 0.5f, h);
        float maxX = -1e9f;
        for (const auto &p : h) maxX = std::max(maxX, p.x);
        CHECK(near(maxX, 32, 0.1f));
        const float c1[3] = {16, 0, 4};
        checkOutward(h, c1, "lerped");
        // backlerp 0 = frame, 1 = oldframe; out of range values clamp
        std::vector<realms::Md2Vertex> a, b;
        m.buildVertices(1, 0, 0.0f, a);
        m.buildVertices(0, 1, 1.0f, b);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); i++) same = near(a[i].x, b[i].x, 1e-5f);
        CHECK(same);
        std::vector<realms::Md2Vertex> c;
        m.buildVertices(1, 0, 7.0f, c); // clamped to 1: frame 0
        maxX = -1e9f;
        for (const auto &p : c) maxX = std::max(maxX, p.x);
        CHECK(near(maxX, 16, 0.1f));
        // "no such frame": frame 0
        std::vector<realms::Md2Vertex> d;
        m.buildVertices(99, -3, std::nanf(""), d);
        CHECK(d.size() == 36);
        maxX = -1e9f;
        for (const auto &p : d) maxX = std::max(maxX, p.x);
        CHECK(near(maxX, 16, 0.1f));
        // appends
        m.buildVertices(0, 0, 0, d);
        CHECK(d.size() == 72);
    }
}

void corruptChecks() {
    const auto good = md2_builder::writeMd2(md2_builder::boxModel(kFrames));
    struct Corruption {
        const char *what;
        size_t offset;
        int32_t value;
    };
    const Corruption cases[] = {
        {"ident", 0, 0x32504449 + 1}, {"version", 4, 7},          {"skin width", 8, 0},
        {"skin height", 12, -4},      {"skin width big", 8, 9000}, {"frame size", 16, 8},
        {"skins", 20, 33},            {"skins neg", 20, -1},       {"verts", 24, 0},
        {"verts big", 24, 4096},      {"st", 28, 0},               {"tris", 32, 0},
        {"tris big", 32, 5000},       {"frames", 40, 0},           {"frames big", 40, 600},
        {"ofs skins", 44, 1 << 30},   {"ofs st", 48, -8},          {"ofs tris", 52, 1 << 20},
        {"ofs frames", 56, 1 << 24},  {"ofs end", 64, 1 << 24},
    };
    for (const Corruption &c : cases) {
        auto b = good;
        md2_builder::putInt(b, c.offset, c.value);
        realms::Md2Model m;
        std::string err;
        const bool ok = m.parse(b, err);
        if (ok) std::fprintf(stderr, "  (accepted corrupt model: %s)\n", c.what);
        CHECK(!ok && !err.empty() && !m.valid());
    }
    // a triangle pointing past the vertices / the st table
    {
        auto def = md2_builder::boxModel(kFrames);
        def.tris[3][1] = 8;
        realms::Md2Model m;
        std::string err;
        CHECK(!m.parse(md2_builder::writeMd2(def), err) && err.find("vertex index") != std::string::npos);
        def = md2_builder::boxModel(kFrames);
        def.tris[5][4] = 4;
        CHECK(!m.parse(md2_builder::writeMd2(def), err) && err.find("st index") != std::string::npos);
    }
    // NaN scale
    {
        auto b = good;
        md2_builder::putFloat(b, 68 + 64 + 16 + 12 * 12, std::nanf(""));
        realms::Md2Model m;
        std::string err;
        CHECK(!m.parse(b, err));
    }
    // truncated anywhere, and random corruption: never crashes, and a model
    // that is accepted is self-consistent
    int accepted = 0;
    for (size_t n = 0; n < good.size(); n += 7) {
        realms::Md2Model m;
        std::string err;
        CHECK(!m.parse(good.data(), n, err));
    }
    std::mt19937 rng(1234);
    for (int i = 0; i < 2000; i++) {
        auto b = good;
        const int flips = 1 + int(rng() % 6);
        for (int k = 0; k < flips; k++) b[rng() % b.size()] = uint8_t(rng());
        realms::Md2Model m;
        std::string err;
        if (m.parse(b, err)) {
            accepted++;
            std::vector<realms::Md2Vertex> v;
            m.buildVertices(int(rng() % 4), int(rng() % 4), 0.3f, v);
            CHECK(v.size() == size_t(m.numTriangles()) * 3);
        }
    }
    CHECK(accepted > 0); // flips in names / coordinates are harmless
    realms::Md2Model m;
    std::string err;
    CHECK(!m.parse(nullptr, 100, err));
}

void pcxChecks() {
    const int w = 70, h = 5;
    std::vector<uint8_t> idx(size_t(w * h));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) idx[size_t(y * w + x)] = uint8_t(x < 66 ? 200 : (x * 3 + y) & 0xff); // long runs + literals >= 0xC0
    for (bool pad : {false, true}) {
        const auto bytes = md2_builder::writePcx(w, h, idx, pad);
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
    const auto good = md2_builder::writePcx(w, h, idx);
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
    pcxChecks();
    std::printf("[md2] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
