// Realms Engine - Quake 2 BSP lighting for renderers (see MapLighting.h).
#include "realms/MapLighting.h"

#include "realms/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace realms {
namespace {

enum {
    kLumpPlanes = 1,
    kLumpVertexes = 2,
    kLumpNodes = 4,
    kLumpTexinfo = 5,
    kLumpFaces = 6,
    kLumpLighting = 7,
    kLumpEdges = 11,
    kLumpSurfEdges = 12,
    kLumpModels = 13,
    kNumLumps = 19
};
constexpr int32_t kSurfSky = 0x4, kSurfWarp = 0x8, kSurfTrans33 = 0x10, kSurfTrans66 = 0x20;
constexpr int kMaxFaceEdges = 64;
constexpr double kMaxCoord = 1.0e6;

int32_t rdI32(const unsigned char *p) {
    const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    int32_t out;
    std::memcpy(&out, &v, 4);
    return out;
}
uint16_t rdU16(const unsigned char *p) { return uint16_t(p[0] | p[1] << 8); }
int16_t rdI16(const unsigned char *p) { return int16_t(rdU16(p)); }
float rdF32(const unsigned char *p) {
    const int32_t bits = rdI32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return std::isfinite(f) && std::fabs(f) < kMaxCoord ? f : 0.0f;
}

struct Lump {
    const unsigned char *data = nullptr;
    size_t count = 0;
};

double texCoord(const float vecs[4], const double p[3]) {
    return p[0] * double(vecs[0]) + p[1] * double(vecs[1]) + p[2] * double(vecs[2]) + double(vecs[3]);
}

} // namespace

MapLightStyles defaultLightStyles() {
    MapLightStyles s;
    s.fill(1.0f);
    return s;
}

float lightStyleValue(const std::string &pattern, double timeMs) {
    if (pattern.empty()) return 1.0f;
    const double frame = std::isfinite(timeMs) && timeMs > 0 ? std::floor(timeMs / 100.0) : 0.0;
    const size_t i = size_t(std::fmod(frame, double(pattern.size())));
    const int c = std::clamp(int(static_cast<unsigned char>(pattern[i])) - 'a', 0, 25);
    return float(c) / float('m' - 'a');
}

void MapLighting::clear() {
    faces_.clear();
    texinfo_.clear();
    nodes_.clear();
    planes_.clear();
    lighting_.clear();
    headnode_ = -1;
    pages_ = 0;
    litFaces_ = dropped_ = 0;
    styles_.clear();
}

bool MapLighting::load(const Engine &engine, std::string &error) {
    clear();
    if (!engine.mapLoaded() || engine.mapPath().empty()) {
        error = "no map loaded";
        return false;
    }
    if (!engine.meshMap().indices.empty()) return true; // glTF: no lightmaps
    std::vector<unsigned char> bytes;
    if (!engine.loadFile(engine.mapPath(), bytes)) {
        error = "can't read " + engine.mapPath();
        return false;
    }
    if (!load(bytes.data(), bytes.size(), error)) {
        error = engine.mapPath() + ": " + error;
        return false;
    }
    return true;
}

bool MapLighting::load(const unsigned char *data, size_t size, std::string &error) {
    clear();
    error.clear();
    if (!data || size < 8 + size_t(kNumLumps) * 8 || std::memcmp(data, "IBSP", 4) != 0 || rdI32(data + 4) != 38) {
        error = "not an IBSP version 38 map";
        return false;
    }
    static const size_t recordSize[kNumLumps] = {1, 20, 12, 1, 28, 76, 20, 1, 1, 1, 1, 4, 4, 48, 1, 1, 1, 1, 1};
    Lump lumps[kNumLumps];
    for (int i = 0; i < kNumLumps; ++i) {
        const int32_t ofs = rdI32(data + 8 + size_t(i) * 8);
        const int32_t len = rdI32(data + 12 + size_t(i) * 8);
        if (ofs < 0 || len < 0 || size_t(ofs) > size || size_t(len) > size - size_t(ofs) ||
            size_t(len) % recordSize[i]) {
            error = "lump " + std::to_string(i) + " is out of range";
            return false;
        }
        lumps[i].data = data + ofs;
        lumps[i].count = size_t(len) / recordSize[i];
    }

    const Lump &pl = lumps[kLumpPlanes];
    planes_.resize(pl.count);
    for (size_t i = 0; i < pl.count; ++i) {
        const unsigned char *p = pl.data + i * 20;
        for (int k = 0; k < 3; ++k) planes_[i].normal[k] = rdF32(p + size_t(k) * 4);
        planes_[i].dist = rdF32(p + 12);
    }
    const Lump &tl = lumps[kLumpTexinfo];
    texinfo_.resize(tl.count);
    for (size_t i = 0; i < tl.count; ++i) {
        const unsigned char *p = tl.data + i * 76;
        for (int r = 0; r < 2; ++r)
            for (int k = 0; k < 4; ++k) texinfo_[i].vecs[r][k] = rdF32(p + size_t(r * 16 + k * 4));
        texinfo_[i].flags = rdI32(p + 32);
    }
    const Lump &nl = lumps[kLumpNodes];
    nodes_.resize(nl.count);
    for (size_t i = 0; i < nl.count; ++i) {
        const unsigned char *p = nl.data + i * 28;
        nodes_[i].plane = rdI32(p);
        nodes_[i].children[0] = rdI32(p + 4);
        nodes_[i].children[1] = rdI32(p + 8);
        nodes_[i].firstFace = rdU16(p + 24);
        nodes_[i].numFaces = rdU16(p + 26);
    }
    if (lumps[kLumpModels].count) headnode_ = rdI32(lumps[kLumpModels].data + 36);
    lighting_.assign(lumps[kLumpLighting].data, lumps[kLumpLighting].data + lumps[kLumpLighting].count);

    // CalcSurfaceExtents + GL_CreateSurfaceLightmap for every face
    const Lump &fl = lumps[kLumpFaces];
    const Lump &verts = lumps[kLumpVertexes];
    const Lump &edges = lumps[kLumpEdges];
    const Lump &surfedges = lumps[kLumpSurfEdges];
    faces_.resize(fl.count);
    std::array<int, kPage> allocated{};
    bool pageOpen = false;
    std::vector<bool> usedStyle(256, false);
    for (size_t f = 0; f < fl.count; ++f) {
        const unsigned char *p = fl.data + f * 20;
        Face &face = faces_[f];
        const int32_t firstEdge = rdI32(p + 4);
        const int16_t numEdges = rdI16(p + 8);
        const int16_t texnum = rdI16(p + 10);
        for (int s = 0; s < 4; ++s) face.styles[size_t(s)] = p[12 + s];
        face.lightofs = rdI32(p + 16);
        if (texnum < 0 || size_t(texnum) >= texinfo_.size()) continue;
        face.texinfo = texnum;
        const Texinfo &tex = texinfo_[size_t(texnum)];
        if (numEdges < 3 || numEdges > kMaxFaceEdges || firstEdge < 0 ||
            size_t(firstEdge) + size_t(numEdges) > surfedges.count)
            continue;
        double mins[2] = {1e30, 1e30}, maxs[2] = {-1e30, -1e30};
        bool ok = true;
        for (int e = 0; e < numEdges && ok; ++e) {
            const int32_t se = rdI32(surfedges.data + (size_t(firstEdge) + size_t(e)) * 4);
            const int64_t edge = se < 0 ? -int64_t(se) : int64_t(se);
            if (size_t(edge) >= edges.count) {
                ok = false;
                break;
            }
            const uint16_t v = se < 0 ? rdU16(edges.data + size_t(edge) * 4 + 2) : rdU16(edges.data + size_t(edge) * 4);
            if (v >= verts.count) {
                ok = false;
                break;
            }
            const unsigned char *vp = verts.data + size_t(v) * 12;
            const double pos[3] = {rdF32(vp), rdF32(vp + 4), rdF32(vp + 8)};
            for (int j = 0; j < 2; ++j) {
                const double val = texCoord(tex.vecs[j], pos);
                mins[j] = std::min(mins[j], val);
                maxs[j] = std::max(maxs[j], val);
            }
        }
        if (!ok) continue;
        for (int j = 0; j < 2; ++j) {
            const double bmin = std::floor(mins[j] / kLuxel), bmax = std::ceil(maxs[j] / kLuxel);
            face.texturemins[j] = float(bmin * kLuxel);
            face.extents[j] = int(std::clamp((bmax - bmin) * kLuxel, 0.0, 1.0e6));
        }
        face.smax = face.extents[0] / kLuxel + 1;
        face.tmax = face.extents[1] / kLuxel + 1;
        // samples: one RGB map per style, in the lighting lump
        const int maps = styleCount(face);
        if (face.lightofs >= 0 && maps > 0) {
            const size_t need = size_t(face.smax) * size_t(face.tmax) * 3 * size_t(maps);
            face.samples = size_t(face.lightofs) <= lighting_.size() && need <= lighting_.size() - size_t(face.lightofs);
        }
        if (tex.flags & (kSurfSky | kSurfTrans33 | kSurfTrans66 | kSurfWarp)) continue; // drawn without lightmap
        if (!face.samples) {
            if (face.lightofs >= 0) ++dropped_;
            continue; // Quake 2 draws these full bright: the renderer's "no lightmap"
        }
        // LM_AllocBlock on the current page, then a new page
        const int w = face.smax, h = face.tmax;
        if (w > kPage || h > kPage) {
            ++dropped_;
            continue;
        }
        bool placed = false;
        for (int attempt = 0; attempt < 2 && !placed; ++attempt) {
            if (!pageOpen) {
                if (pages_ >= kMaxPages) break;
                allocated.fill(0);
                ++pages_;
                pageOpen = true;
            }
            int best = kPage, bestX = -1;
            for (int i = 0; i + w <= kPage; ++i) {
                int best2 = 0, j = 0;
                for (; j < w; ++j) {
                    if (allocated[size_t(i + j)] >= best) break;
                    best2 = std::max(best2, allocated[size_t(i + j)]);
                }
                if (j == w) {
                    bestX = i;
                    best = best2;
                }
            }
            if (bestX < 0 || best + h > kPage) {
                pageOpen = false; // Quake 2 uploads the full block and starts another
                continue;
            }
            for (int i = 0; i < w; ++i) allocated[size_t(bestX + i)] = best + h;
            face.page = pages_ - 1;
            face.x = bestX;
            face.y = best;
            placed = true;
        }
        if (!placed) {
            ++dropped_;
            continue;
        }
        ++litFaces_;
        for (int s = 0; s < maps; ++s) usedStyle[face.styles[size_t(s)]] = true;
    }
    for (int s = 0; s < 255; ++s)
        if (usedStyle[size_t(s)]) styles_.push_back(s);
    return true;
}

int MapLighting::styleCount(const Face &f) const {
    int n = 0;
    while (n < 4 && f.styles[size_t(n)] != 255) ++n;
    return n;
}

bool MapLighting::faceUv(size_t face, const float p[3], float uv[2]) const {
    if (face >= faces_.size() || faces_[face].page < 0 || !p) return false;
    const Face &f = faces_[face];
    const Texinfo &tex = texinfo_[size_t(f.texinfo)];
    const double pos[3] = {p[0], p[1], p[2]};
    const int ox = (f.page % kPagesAcross) * kPage + f.x;
    const int oy = (f.page / kPagesAcross) * kPage + f.y;
    const double w = double(width()) * kLuxel, h = double(height()) * kLuxel;
    // s -= texturemins; s += light_s * 16 + 8; s /= BLOCK_WIDTH * 16
    uv[0] = float((texCoord(tex.vecs[0], pos) - f.texturemins[0] + ox * kLuxel + 8) / w);
    uv[1] = float((texCoord(tex.vecs[1], pos) - f.texturemins[1] + oy * kLuxel + 8) / h);
    return std::isfinite(uv[0]) && std::isfinite(uv[1]);
}

void MapLighting::compose(const MapLightStyles &styles, std::vector<uint8_t> &rgba) const {
    const int W = width(), H = height();
    rgba.assign(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < rgba.size(); i += 4) { // unused texels: mid grey, opaque
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 128;
        rgba[i + 3] = 255;
    }
    for (const Face &f : faces_) {
        if (f.page < 0) continue;
        const int maps = styleCount(f);
        const size_t mapSize = size_t(f.smax) * size_t(f.tmax) * 3;
        float scale[4] = {0, 0, 0, 0};
        for (int m = 0; m < maps; ++m) {
            const float v = styles[f.styles[size_t(m)]];
            scale[m] = std::isfinite(v) ? std::max(v, 0.0f) : 1.0f;
        }
        const int ox = (f.page % kPagesAcross) * kPage + f.x;
        const int oy = (f.page / kPagesAcross) * kPage + f.y;
        for (int t = 0; t < f.tmax; ++t)
            for (int s = 0; s < f.smax; ++s) {
                float c[3] = {0, 0, 0};
                const size_t at = size_t(f.lightofs) + (size_t(t) * size_t(f.smax) + size_t(s)) * 3;
                for (int m = 0; m < maps; ++m)
                    for (int k = 0; k < 3; ++k) c[k] += float(lighting_[at + size_t(m) * mapSize + size_t(k)]) * scale[m];
                // R_BuildLightMap: keep the colour when the brightest channel saturates
                const float mx = std::max({c[0], c[1], c[2]});
                const float k = mx > 255.0f ? 255.0f / mx : 1.0f;
                uint8_t *out = &rgba[(size_t(oy + t) * size_t(W) + size_t(ox + s)) * 4];
                for (int j = 0; j < 3; ++j) out[j] = uint8_t(std::lround(std::clamp(c[j] * k, 0.0f, 255.0f)));
                out[3] = 255;
            }
    }
}

bool MapLighting::stylesDiffer(const MapLightStyles &a, const MapLightStyles &b) const {
    for (int s : styles_)
        if (a[size_t(s)] != b[size_t(s)]) return true;
    return false;
}

int MapLighting::recursiveLightPoint(int32_t node, const float start[3], const float end[3],
                                     const MapLightStyles &styles, float rgb[3], int depth) const {
    if (node < 0 || size_t(node) >= nodes_.size() || depth > 1024) return -1; // a leaf: didn't hit anything
    const Node &n = nodes_[size_t(node)];
    if (n.plane < 0 || size_t(n.plane) >= planes_.size()) return -1;
    const Plane &pl = planes_[size_t(n.plane)];
    const float front = start[0] * pl.normal[0] + start[1] * pl.normal[1] + start[2] * pl.normal[2] - pl.dist;
    const float back = end[0] * pl.normal[0] + end[1] * pl.normal[1] + end[2] * pl.normal[2] - pl.dist;
    const int side = front < 0;
    if ((back < 0) == bool(side)) return recursiveLightPoint(n.children[side], start, end, styles, rgb, depth + 1);
    const float frac = front / (front - back);
    const float mid[3] = {start[0] + (end[0] - start[0]) * frac, start[1] + (end[1] - start[1]) * frac,
                          start[2] + (end[2] - start[2]) * frac};
    // the front side first
    const int r = recursiveLightPoint(n.children[side], start, mid, styles, rgb, depth + 1);
    if (r >= 0) return r;
    // then the faces on this node's plane
    const double pos[3] = {mid[0], mid[1], mid[2]};
    for (size_t i = n.firstFace; i < size_t(n.firstFace) + n.numFaces && i < faces_.size(); ++i) {
        const Face &f = faces_[i];
        if (f.texinfo < 0) continue;
        const Texinfo &tex = texinfo_[size_t(f.texinfo)];
        if (tex.flags & (kSurfWarp | kSurfSky)) continue;
        const double s = texCoord(tex.vecs[0], pos), t = texCoord(tex.vecs[1], pos);
        if (s < f.texturemins[0] || t < f.texturemins[1]) continue;
        const double ds = s - f.texturemins[0], dt = t - f.texturemins[1];
        if (ds > f.extents[0] || dt > f.extents[1]) continue;
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        if (!f.samples) return 0; // hit, no light data: black, like Quake 2
        const int maps = styleCount(f);
        const size_t mapSize = size_t(f.smax) * size_t(f.tmax) * 3;
        const size_t at = size_t(f.lightofs) +
                          (size_t(int(dt) >> 4) * size_t(f.smax) + size_t(int(ds) >> 4)) * 3;
        for (int m = 0; m < maps; ++m) {
            const float v = styles[f.styles[size_t(m)]];
            const float scale = (std::isfinite(v) ? std::max(v, 0.0f) : 1.0f) / 255.0f;
            for (int k = 0; k < 3; ++k) rgb[k] += float(lighting_[at + size_t(m) * mapSize + size_t(k)]) * scale;
        }
        return 1;
    }
    // then the back side
    return recursiveLightPoint(n.children[!side], mid, end, styles, rgb, depth + 1);
}

bool MapLighting::lightPoint(const float p[3], const MapLightStyles &styles, float rgb[3]) const {
    rgb[0] = rgb[1] = rgb[2] = 0.0f;
    if (empty() || !p || headnode_ < 0 || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
        return false;
    const float end[3] = {p[0], p[1], p[2] - 2048.0f};
    return recursiveLightPoint(headnode_, p, end, styles, rgb, 0) >= 0;
}

} // namespace realms
