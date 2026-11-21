// Realms Engine - drawable geometry of the loaded map (see MapGeometry.h).
#include "realms/MapGeometry.h"

#include "realms/Engine.h"
#include "realms/MeshMap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>

// Bit-exact with the Free Pascal core: no fused multiply-add contraction
// (clang contracts a*b+c by default on arm64 / FMA targets).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace realms {

size_t MapGeometry::triangleCount() const {
    size_t n = 0;
    for (const auto &b : batches) n += b.triangleCount();
    return n;
}

namespace {

// qfiles.h lump indices and record sizes.
enum {
    kLumpPlanes = 1,
    kLumpVertexes = 2,
    kLumpFaces = 6,
    kLumpTexinfo = 5,
    kLumpEdges = 11,
    kLumpSurfEdges = 12,
    kLumpBrushes = 14,
    kLumpBrushSides = 15,
    kNumLumps = 19
};
constexpr size_t kPlaneSize = 20, kVertexSize = 12, kFaceSize = 20, kTexinfoSize = 76, kEdgeSize = 4,
                 kSurfEdgeSize = 4, kBrushSize = 12, kBrushSideSize = 4;
constexpr int kMaxFaceEdges = 64;    // qbsp MAXEDGES
constexpr int kMaxBrushSides = 128;  // generous; qbsp caps at 6 + bevels
constexpr double kWorldExtent = 65536.0;
constexpr double kClipEpsilon = 0.01;
constexpr float kMaxCoord = 1.0e6f;

using DVec = std::array<double, 3>;

DVec sub(const DVec &a, const DVec &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
DVec cross(const DVec &a, const DVec &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const DVec &a, const DVec &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

int32_t rdI32(const unsigned char *p) {
    uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
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
    return f;
}

struct Lump {
    const unsigned char *data = nullptr;
    size_t count = 0;
};

struct Texinfo {
    float vecs[2][4] = {};
    int32_t flags = 0;
    int32_t value = 0;
    std::string name;
};

struct Plane {
    DVec normal{};
    double dist = 0;
};

bool finite3(const DVec &v) {
    for (double c : v)
        if (!std::isfinite(c) || std::fabs(c) > kMaxCoord) return false;
    return true;
}

class Emitter {
public:
    explicit Emitter(MapGeometry &out) : out_(out) {}

    // Polygon (any winding) with an outward normal; fan-triangulated so the
    // triangles wind counter-clockwise around `normal`.
    bool polygon(std::vector<DVec> points, const DVec &normal, const std::string &material, int32_t flags,
                 int32_t contents, const Texinfo *tex) {
        // Drop repeated points (qbsp leaves colinear points; harmless).
        std::vector<DVec> pts;
        for (const DVec &p : points) {
            if (!finite3(p)) return false;
            if (!pts.empty() && dot(sub(p, pts.back()), sub(p, pts.back())) < 1e-6) continue;
            pts.push_back(p);
        }
        while (pts.size() > 1 && dot(sub(pts.front(), pts.back()), sub(pts.front(), pts.back())) < 1e-6)
            pts.pop_back();
        if (pts.size() < 3) return false;
        DVec area{0, 0, 0};
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            const DVec c = cross(sub(pts[i], pts[0]), sub(pts[i + 1], pts[0]));
            for (int k = 0; k < 3; ++k) area[k] += c[k];
        }
        const double a = dot(area, normal);
        if (std::fabs(a) < 1e-6) return false; // degenerate (bevel sides, slivers)
        if (a < 0) std::reverse(pts.begin(), pts.end());

        MapGeometryBatch &batch = batchFor(material, flags, contents);
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            for (const DVec *p : {&pts[0], &pts[i], &pts[i + 1]}) vertex(batch, *p, normal, tex);
        }
        return true;
    }

private:
    MapGeometryBatch &batchFor(const std::string &material, int32_t flags, int32_t contents) {
        const auto key = std::make_tuple(material, flags, contents);
        auto it = index_.find(key);
        if (it != index_.end()) return out_.batches[it->second];
        index_.emplace(key, out_.batches.size());
        out_.batches.emplace_back();
        MapGeometryBatch &b = out_.batches.back();
        b.material = material;
        b.surfaceFlags = flags;
        b.contents = contents;
        return b;
    }

    void vertex(MapGeometryBatch &b, const DVec &p, const DVec &n, const Texinfo *tex) {
        for (int k = 0; k < 3; ++k) {
            const float v = float(p[size_t(k)]);
            b.positions.push_back(v);
            b.normals.push_back(float(n[size_t(k)]));
            if (first_ || v < out_.mins[size_t(k)]) out_.mins[size_t(k)] = v;
            if (first_ || v > out_.maxs[size_t(k)]) out_.maxs[size_t(k)] = v;
        }
        first_ = false;
        bool texinfoUv = false;
        if (tex) {
            for (int r = 0; r < 2 && !texinfoUv; ++r)
                for (int k = 0; k < 3; ++k)
                    if (tex->vecs[r][k] != 0.0f) texinfoUv = true;
        }
        if (texinfoUv) {
            for (int r = 0; r < 2; ++r)
                b.uvs.push_back(float(p[0] * tex->vecs[r][0] + p[1] * tex->vecs[r][1] + p[2] * tex->vecs[r][2] +
                                      tex->vecs[r][3]));
        } else {
            // Planar projection on the dominant axis, in map units.
            const double ax = std::fabs(n[0]), ay = std::fabs(n[1]), az = std::fabs(n[2]);
            if (az >= ax && az >= ay) {
                b.uvs.push_back(float(p[0]));
                b.uvs.push_back(float(-p[1]));
            } else if (ax >= ay) {
                b.uvs.push_back(float(p[1]));
                b.uvs.push_back(float(-p[2]));
            } else {
                b.uvs.push_back(float(p[0]));
                b.uvs.push_back(float(-p[2]));
            }
        }
    }

    MapGeometry &out_;
    std::map<std::tuple<std::string, int32_t, int32_t>, size_t> index_;
    bool first_ = true;
};

std::string nameField(const unsigned char *p, size_t n) {
    const auto *end = std::find(p, p + n, static_cast<unsigned char>(0));
    std::string s(reinterpret_cast<const char *>(p), size_t(end - p));
    for (char &c : s)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7f) c = '?';
    return s;
}

std::vector<Texinfo> readTexinfo(const Lump &lump) {
    std::vector<Texinfo> out(lump.count);
    for (size_t i = 0; i < lump.count; ++i) {
        const unsigned char *p = lump.data + i * kTexinfoSize;
        Texinfo &t = out[i];
        for (int r = 0; r < 2; ++r)
            for (int k = 0; k < 4; ++k) {
                const float v = rdF32(p + size_t(r * 16 + k * 4));
                t.vecs[r][k] = std::isfinite(v) ? v : 0.0f;
            }
        t.flags = rdI32(p + 32);
        t.value = rdI32(p + 36);
        t.name = nameField(p + 40, 32);
    }
    return out;
}

bool readPlane(const Lump &planes, size_t index, Plane &out) {
    if (index >= planes.count) return false;
    const unsigned char *p = planes.data + index * kPlaneSize;
    for (int k = 0; k < 3; ++k) out.normal[size_t(k)] = rdF32(p + size_t(k * 4));
    out.dist = rdF32(p + 12);
    if (!finite3(out.normal) || !std::isfinite(out.dist) || std::fabs(out.dist) > kMaxCoord) return false;
    const double len = std::sqrt(dot(out.normal, out.normal));
    if (len < 0.5 || len > 1.5) return false;
    return true;
}

void faceGeometry(const Lump (&lumps)[kNumLumps], const std::vector<Texinfo> &texinfo, Emitter &emit,
                  MapGeometry &out, const MapGeometryOptions &options) {
    const Lump &faces = lumps[kLumpFaces];
    const Lump &verts = lumps[kLumpVertexes];
    const Lump &edges = lumps[kLumpEdges];
    const Lump &surfedges = lumps[kLumpSurfEdges];
    std::vector<DVec> points;
    for (size_t f = 0; f < faces.count; ++f) {
        const unsigned char *p = faces.data + f * kFaceSize;
        const uint16_t planenum = rdU16(p);
        const int16_t side = rdI16(p + 2);
        const int32_t firstEdge = rdI32(p + 4);
        const int16_t numEdges = rdI16(p + 8);
        const int16_t texnum = rdI16(p + 10);
        const Texinfo *tex = texnum >= 0 && size_t(texnum) < texinfo.size() ? &texinfo[size_t(texnum)] : nullptr;
        if (tex && (tex->flags & options.skipSurfaceFlags)) continue;
        Plane plane;
        if (numEdges < 3 || numEdges > kMaxFaceEdges || firstEdge < 0 ||
            size_t(firstEdge) + size_t(numEdges) > surfedges.count || !readPlane(lumps[kLumpPlanes], planenum, plane)) {
            ++out.skipped;
            continue;
        }
        points.clear();
        bool ok = true;
        for (int e = 0; e < numEdges && ok; ++e) {
            const int32_t se = rdI32(surfedges.data + (size_t(firstEdge) + size_t(e)) * kSurfEdgeSize);
            if (se == INT32_MIN) {
                ok = false;
                break;
            }
            const size_t edge = size_t(se < 0 ? -se : se);
            if (edge >= edges.count) {
                ok = false;
                break;
            }
            const unsigned char *ep = edges.data + edge * kEdgeSize;
            const uint16_t v = se < 0 ? rdU16(ep + 2) : rdU16(ep);
            if (v >= verts.count) {
                ok = false;
                break;
            }
            const unsigned char *vp = verts.data + size_t(v) * kVertexSize;
            points.push_back({rdF32(vp), rdF32(vp + 4), rdF32(vp + 8)});
        }
        DVec normal = plane.normal;
        if (side) normal = {-normal[0], -normal[1], -normal[2]};
        if (!ok || !emit.polygon(points, normal, tex ? tex->name : std::string(), tex ? tex->flags : 0, 0, tex))
            ++out.skipped;
    }
}

void brushGeometry(const Lump (&lumps)[kNumLumps], const std::vector<Texinfo> &texinfo, Emitter &emit,
                   MapGeometry &out, const MapGeometryOptions &options) {
    const Lump &brushes = lumps[kLumpBrushes];
    const Lump &sides = lumps[kLumpBrushSides];
    std::vector<Plane> planes;
    std::vector<int16_t> texnums;
    std::vector<DVec> winding, clipped;
    for (size_t b = 0; b < brushes.count; ++b) {
        const unsigned char *p = brushes.data + b * kBrushSize;
        const int32_t firstSide = rdI32(p);
        const int32_t numSides = rdI32(p + 4);
        const int32_t contents = rdI32(p + 8);
        if (!(contents & options.brushContents)) continue;
        if (firstSide < 0 || numSides < 4 || numSides > kMaxBrushSides ||
            size_t(firstSide) + size_t(numSides) > sides.count) {
            ++out.skipped;
            continue;
        }
        planes.assign(size_t(numSides), Plane{});
        texnums.assign(size_t(numSides), -1);
        bool ok = true;
        for (int32_t s = 0; s < numSides && ok; ++s) {
            const unsigned char *sp = sides.data + (size_t(firstSide) + size_t(s)) * kBrushSideSize;
            ok = readPlane(lumps[kLumpPlanes], rdU16(sp), planes[size_t(s)]);
            texnums[size_t(s)] = rdI16(sp + 2);
        }
        if (!ok) {
            ++out.skipped;
            continue;
        }
        for (int32_t s = 0; s < numSides; ++s) {
            const int16_t texnum = texnums[size_t(s)];
            const Texinfo *tex = texnum >= 0 && size_t(texnum) < texinfo.size() ? &texinfo[size_t(texnum)] : nullptr;
            if (tex && (tex->flags & options.skipSurfaceFlags)) continue;
            const Plane &pl = planes[size_t(s)];
            // BaseWindingForPlane: a huge square on the plane.
            int major = 0;
            for (int k = 1; k < 3; ++k)
                if (std::fabs(pl.normal[size_t(k)]) > std::fabs(pl.normal[size_t(major)])) major = k;
            DVec up = major == 2 ? DVec{1, 0, 0} : DVec{0, 0, 1};
            const double d = dot(up, pl.normal);
            for (int k = 0; k < 3; ++k) up[size_t(k)] -= d * pl.normal[size_t(k)];
            const double ul = std::sqrt(dot(up, up));
            for (double &c : up) c /= ul;
            DVec right = cross(up, pl.normal);
            DVec org{pl.normal[0] * pl.dist, pl.normal[1] * pl.dist, pl.normal[2] * pl.dist};
            winding.clear();
            for (int corner = 0; corner < 4; ++corner) {
                const double su = (corner == 0 || corner == 3) ? -kWorldExtent : kWorldExtent;
                const double sr = (corner < 2) ? kWorldExtent : -kWorldExtent;
                winding.push_back({org[0] + up[0] * su + right[0] * sr, org[1] + up[1] * su + right[1] * sr,
                                   org[2] + up[2] * su + right[2] * sr});
            }
            // Keep the part behind every other side plane.
            for (int32_t o = 0; o < numSides && winding.size() >= 3; ++o) {
                if (o == s) continue;
                const Plane &cp = planes[size_t(o)];
                if (dot(cp.normal, pl.normal) > 0.999999 && std::fabs(cp.dist - pl.dist) < 1e-6) continue;
                clipped.clear();
                const size_t n = winding.size();
                for (size_t i = 0; i < n; ++i) {
                    const DVec &a = winding[i];
                    const DVec &c = winding[(i + 1) % n];
                    const double da = dot(a, cp.normal) - cp.dist;
                    const double dc = dot(c, cp.normal) - cp.dist;
                    if (da <= kClipEpsilon) clipped.push_back(a);
                    if ((da > kClipEpsilon && dc < -kClipEpsilon) || (da < -kClipEpsilon && dc > kClipEpsilon)) {
                        const double t = da / (da - dc);
                        clipped.push_back({a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t});
                    }
                }
                winding.swap(clipped);
            }
            if (winding.size() < 3) continue; // fully clipped: bevel or hidden side
            emit.polygon(winding, pl.normal, tex ? tex->name : std::string(), tex ? tex->flags : 0, contents, tex);
        }
    }
}

} // namespace

bool bspGeometry(const unsigned char *data, size_t size, MapGeometry &out, std::string &error,
                 const MapGeometryOptions &options) {
    out = MapGeometry{};
    error.clear();
    const size_t header = 8 + size_t(kNumLumps) * 8;
    if (!data || size < header) {
        error = "file too small for a BSP header";
        return false;
    }
    if (std::memcmp(data, "IBSP", 4) != 0 || rdI32(data + 4) != 38) {
        error = "not an IBSP version 38 map";
        return false;
    }
    static const size_t recordSize[kNumLumps] = {1, kPlaneSize, kVertexSize, 1, 1, kTexinfoSize, kFaceSize, 1, 1, 1,
                                                 1, kEdgeSize, kSurfEdgeSize, 1, kBrushSize, kBrushSideSize, 1, 1, 1};
    Lump lumps[kNumLumps];
    for (int i = 0; i < kNumLumps; ++i) {
        const int32_t ofs = rdI32(data + 8 + size_t(i) * 8);
        const int32_t len = rdI32(data + 12 + size_t(i) * 8);
        if (ofs < 0 || len < 0 || size_t(ofs) > size || size_t(len) > size - size_t(ofs)) {
            error = "lump " + std::to_string(i) + " is out of range";
            return false;
        }
        if (size_t(len) % recordSize[i]) {
            error = "lump " + std::to_string(i) + " has a partial record";
            return false;
        }
        lumps[i].data = data + ofs;
        lumps[i].count = size_t(len) / recordSize[i];
    }
    const std::vector<Texinfo> texinfo = readTexinfo(lumps[kLumpTexinfo]);
    Emitter emit(out);
    if (options.preferFaces && lumps[kLumpFaces].count && lumps[kLumpVertexes].count && lumps[kLumpEdges].count &&
        lumps[kLumpSurfEdges].count) {
        faceGeometry(lumps, texinfo, emit, out, options);
        if (!out.batches.empty()) {
            out.source = MapGeometrySource::Faces;
            return true;
        }
    }
    const size_t skippedFaces = out.skipped;
    out = MapGeometry{};
    out.skipped = skippedFaces;
    Emitter brushEmit(out);
    brushGeometry(lumps, texinfo, brushEmit, out, options);
    out.source = out.batches.empty() ? MapGeometrySource::None : MapGeometrySource::Brushes;
    return true;
}

void meshMapGeometry(const MeshMap &map, MapGeometry &out, const MapGeometryOptions &options) {
    out = MapGeometry{};
    Emitter emit(out);
    const size_t vertexCount = map.vertices.size() / 3;
    const size_t triangles = map.indices.size() / 3;
    std::vector<DVec> tri(3);
    for (size_t t = 0; t < triangles; ++t) {
        const int32_t si = t < map.triangleSurfaces.size() ? map.triangleSurfaces[t] : 0;
        const MeshMapSurface *surf = si >= 0 && size_t(si) < map.surfaces.size() ? &map.surfaces[size_t(si)] : nullptr;
        if (surf && (surf->flags & options.skipSurfaceFlags)) continue;
        bool ok = true;
        for (size_t k = 0; k < 3; ++k) {
            const int32_t idx = map.indices[t * 3 + k];
            if (idx < 0 || size_t(idx) >= vertexCount) {
                ok = false;
                break;
            }
            const float *v = &map.vertices[size_t(idx) * 3];
            tri[k] = {v[0], v[1], v[2]};
        }
        if (!ok) {
            ++out.skipped;
            continue;
        }
        DVec n = cross(sub(tri[1], tri[0]), sub(tri[2], tri[0]));
        const double len = std::sqrt(dot(n, n));
        if (!(len > 1e-9)) {
            ++out.skipped; // degenerate triangle: nothing to draw
            continue;
        }
        for (double &c : n) c /= len;
        if (!emit.polygon(tri, n, surf ? surf->name : std::string(), surf ? surf->flags : 0,
                          surf ? surf->contents : 0, nullptr))
            ++out.skipped;
    }
    out.source = out.batches.empty() ? MapGeometrySource::None : MapGeometrySource::Mesh;
}

bool loadMapGeometry(const Engine &engine, MapGeometry &out, std::string &error, const MapGeometryOptions &options) {
    out = MapGeometry{};
    error.clear();
    if (!engine.mapLoaded() || engine.mapPath().empty()) {
        error = "no map loaded";
        return false;
    }
    if (!engine.meshMap().indices.empty()) {
        meshMapGeometry(engine.meshMap(), out, options);
        return true;
    }
    std::vector<unsigned char> bytes;
    if (!engine.loadFile(engine.mapPath(), bytes)) {
        error = "can't read " + engine.mapPath();
        return false;
    }
    if (!bspGeometry(bytes.data(), bytes.size(), out, error, options)) {
        error = engine.mapPath() + ": " + error;
        return false;
    }
    return true;
}

} // namespace realms
