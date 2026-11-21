// Builds valid Quake 2 IBSP v38 maps from axis-aligned brushes, for tests.
//
// No id Software map can ship with the repository, so the conformance suite
// generates its own. The builder emits a real BSP tree (a kd split on brush
// faces, nodes in pre-order like qbsp), solid leaf 0, clusters/areas, brush
// sides with texinfo, inline models and the entity lump - everything the
// collision model reads. Unused lumps (faces, lighting, ...) are empty.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bsp_builder {

struct Box {
    float mins[3];
    float maxs[3];
    int32_t contents;
    int32_t texinfo;
};

struct Texinfo {
    std::string name;
    int32_t flags = 0;
    int32_t value = 0;
};

struct ModelDef {
    std::vector<Box> brushes;
    float origin[3] = {0, 0, 0};
};

using Region = std::function<bool(const float *mins, const float *maxs)>;

struct AreaPortalDef {
    int32_t portalnum, a, b;
};

struct MapDef {
    std::vector<Texinfo> texinfo;
    ModelDef world;
    std::vector<ModelDef> inlineModels; // "*1", "*2", ...
    std::string entities;
    int32_t numAreas = 2; // area 0 (solid) and area 1
    float extent = 4096;  // root region half size
    // Area of a non-solid leaf region (default: area 1).
    std::function<int32_t(const float *mins, const float *maxs)> areaOf;
    std::vector<AreaPortalDef> portals;
    // When set, a visibility lump is written: cluster a sees cluster b when
    // pvs(a mins, a maxs, b mins, b maxs). The PHS row is all ones.
    std::function<bool(const float *, const float *, const float *, const float *)> pvs;
    // Write a face lump (vertexes / edges / surfedges / faces) with the six
    // sides of every visible brush, like qbsp does for drawable surfaces.
    bool emitFaces = false;
};

struct Stats {
    int32_t planes = 0, nodes = 0, leafs = 0, leafBrushes = 0, brushes = 0, brushSides = 0, clusters = 0;
    std::vector<int32_t> modelHeadnodes;
    std::vector<std::array<float, 6>> clusterBounds; // mins, maxs per cluster
};

// Lump indices (qfiles.h).
enum {
    kEntities = 0,
    kPlanes = 1,
    kVertexes = 2,
    kVisibility = 3,
    kNodes = 4,
    kTexinfo = 5,
    kFaces = 6,
    kLeafs = 8,
    kLeafBrushes = 10,
    kEdges = 11,
    kSurfEdges = 12,
    kModels = 13,
    kBrushes = 14,
    kBrushSides = 15,
    kAreas = 17,
    kAreaPortals = 18,
    kNumLumps = 19
};

inline int32_t lumpOffset(const std::string &bsp, int lump) {
    int32_t v;
    std::memcpy(&v, bsp.data() + 8 + lump * 8, 4);
    return v;
}
inline int32_t lumpLength(const std::string &bsp, int lump) {
    int32_t v;
    std::memcpy(&v, bsp.data() + 12 + lump * 8, 4);
    return v;
}
inline void poke32(std::string &bsp, size_t at, int32_t v) { std::memcpy(&bsp[at], &v, 4); }
inline void poke16(std::string &bsp, size_t at, int16_t v) { std::memcpy(&bsp[at], &v, 2); }

class Builder {
public:
    explicit Builder(const MapDef &def) : def_(def) {}

    std::string build(Stats *stats = nullptr) {
        // Leaf 0: the dummy solid leaf every Quake 2 map starts with.
        leafs_.push_back(LeafRec{1, -1, 0, {0, 0, 0}, {0, 0, 0}, 0, 0});
        std::vector<const ModelDef *> models{&def_.world};
        for (const auto &m : def_.inlineModels) models.push_back(&m);
        std::vector<int32_t> headnodes;
        for (const ModelDef *m : models) {
            std::vector<int32_t> ids;
            for (const Box &b : m->brushes) ids.push_back(addBrush(b));
            float rmin[3], rmax[3];
            for (int a = 0; a < 3; ++a) {
                rmin[a] = -def_.extent;
                rmax[a] = def_.extent;
            }
            headnodes.push_back(buildNode(rmin, rmax, ids));
        }
        if (stats) {
            stats->planes = int32_t(planes_.size());
            stats->nodes = int32_t(nodes_.size());
            stats->leafs = int32_t(leafs_.size());
            stats->leafBrushes = int32_t(leafBrushes_.size());
            stats->brushes = int32_t(brushes_.size());
            stats->brushSides = int32_t(sides_.size());
            stats->clusters = clusters_;
            stats->modelHeadnodes = headnodes;
            stats->clusterBounds = clusterBounds_;
        }
        return serialize(models, headnodes);
    }

private:
    struct PlaneRec {
        float normal[3];
        float dist;
        int32_t type;
    };
    struct NodeRec {
        int32_t plane;
        int32_t children[2];
        float mins[3], maxs[3];
    };
    struct LeafRec {
        int32_t contents, cluster, area;
        float mins[3], maxs[3];
        int32_t firstLeafBrush, numLeafBrushes;
    };
    struct BrushRec {
        int32_t firstSide, numSides, contents;
    };

    // Planes come in pairs: even = positive axial normal, odd = negated.
    int32_t plane(int axis, float dist, bool negative) {
        const auto key = std::make_pair(axis, dist);
        auto it = planeIndex_.find(key);
        int32_t index;
        if (it == planeIndex_.end()) {
            index = int32_t(planes_.size());
            PlaneRec p{{0, 0, 0}, dist, axis};
            p.normal[axis] = 1;
            PlaneRec q{{0, 0, 0}, -dist, axis};
            q.normal[axis] = -1;
            planes_.push_back(p);
            planes_.push_back(q);
            planeIndex_[key] = index;
        } else {
            index = it->second;
        }
        return negative ? index + 1 : index;
    }

    int32_t addBrush(const Box &b) {
        BrushRec rec{int32_t(sides_.size()), 6, b.contents};
        for (int a = 0; a < 3; ++a) {
            sides_.emplace_back(plane(a, b.mins[a], true), b.texinfo);
            sides_.emplace_back(plane(a, b.maxs[a], false), b.texinfo);
        }
        boxes_.push_back(b);
        brushes_.push_back(rec);
        return int32_t(brushes_.size()) - 1;
    }

    int32_t buildNode(const float *rmin, const float *rmax, const std::vector<int32_t> &ids) {
        for (int a = 0; a < 3; ++a) {
            for (int32_t id : ids) {
                for (float v : {boxes_[size_t(id)].mins[a], boxes_[size_t(id)].maxs[a]}) {
                    if (!(rmin[a] < v && v < rmax[a])) continue;
                    const int32_t index = int32_t(nodes_.size());
                    nodes_.push_back(NodeRec{plane(a, v, false), {0, 0}, {}, {}});
                    for (int k = 0; k < 3; ++k) {
                        nodes_.back().mins[k] = rmin[k];
                        nodes_.back().maxs[k] = rmax[k];
                    }
                    float fmin[3], fmax[3], bmin[3], bmax[3];
                    for (int k = 0; k < 3; ++k) {
                        fmin[k] = bmin[k] = rmin[k];
                        fmax[k] = bmax[k] = rmax[k];
                    }
                    fmin[a] = v; // front: coordinate >= v
                    bmax[a] = v;
                    const int32_t front = buildNode(fmin, fmax, overlapping(fmin, fmax, ids));
                    const int32_t back = buildNode(bmin, bmax, overlapping(bmin, bmax, ids));
                    nodes_[size_t(index)].children[0] = front;
                    nodes_[size_t(index)].children[1] = back;
                    return index;
                }
            }
        }
        // Leaf: every remaining brush contains the whole region.
        LeafRec leaf{0, 0, 0, {}, {}, int32_t(leafBrushes_.size()), 0};
        for (int k = 0; k < 3; ++k) {
            leaf.mins[k] = rmin[k];
            leaf.maxs[k] = rmax[k];
        }
        for (int32_t id : ids) {
            leaf.contents |= boxes_[size_t(id)].contents;
            leafBrushes_.push_back(id);
            ++leaf.numLeafBrushes;
        }
        if (leaf.contents & 1) {
            leaf.cluster = -1;
            leaf.area = 0;
        } else {
            leaf.cluster = clusters_++;
            clusterBounds_.push_back({rmin[0], rmin[1], rmin[2], rmax[0], rmax[1], rmax[2]});
            leaf.area = def_.areaOf ? def_.areaOf(rmin, rmax) : def_.numAreas > 1 ? 1 : 0;
        }
        leafs_.push_back(leaf);
        return -1 - (int32_t(leafs_.size()) - 1);
    }

    std::vector<int32_t> overlapping(const float *rmin, const float *rmax, const std::vector<int32_t> &ids) const {
        std::vector<int32_t> out;
        for (int32_t id : ids) {
            const Box &b = boxes_[size_t(id)];
            bool overlap = true;
            for (int k = 0; k < 3; ++k)
                if (!(b.mins[k] < rmax[k] && b.maxs[k] > rmin[k])) overlap = false;
            if (overlap) out.push_back(id);
        }
        return out;
    }

    static void put32(std::string &s, int32_t v) { s.append(reinterpret_cast<const char *>(&v), 4); }
    static void put16(std::string &s, int16_t v) { s.append(reinterpret_cast<const char *>(&v), 2); }
    static void putf(std::string &s, float v) { s.append(reinterpret_cast<const char *>(&v), 4); }
    static int16_t clampShort(float v) { return int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

    std::string serialize(const std::vector<const ModelDef *> &models, const std::vector<int32_t> &headnodes) {
        std::string lumps[kNumLumps];
        lumps[kEntities] = def_.entities;
        lumps[kEntities].push_back('\0');
        for (const PlaneRec &p : planes_) {
            for (float n : p.normal) putf(lumps[kPlanes], n);
            putf(lumps[kPlanes], p.dist);
            put32(lumps[kPlanes], p.type);
        }
        for (const NodeRec &n : nodes_) {
            std::string &o = lumps[kNodes];
            put32(o, n.plane);
            put32(o, n.children[0]);
            put32(o, n.children[1]);
            for (float v : n.mins) put16(o, clampShort(v));
            for (float v : n.maxs) put16(o, clampShort(v));
            put16(o, 0);
            put16(o, 0);
        }
        for (const Texinfo &t : def_.texinfo) {
            std::string &o = lumps[kTexinfo];
            for (int i = 0; i < 8; ++i) putf(o, 0);
            put32(o, t.flags);
            put32(o, t.value);
            char name[32] = {};
            std::strncpy(name, t.name.c_str(), sizeof(name) - 1);
            o.append(name, sizeof(name));
            put32(o, -1);
        }
        for (const LeafRec &l : leafs_) {
            std::string &o = lumps[kLeafs];
            put32(o, l.contents);
            put16(o, int16_t(l.cluster));
            put16(o, int16_t(l.area));
            for (float v : l.mins) put16(o, clampShort(v));
            for (float v : l.maxs) put16(o, clampShort(v));
            put16(o, 0);
            put16(o, 0);
            put16(o, int16_t(uint16_t(l.firstLeafBrush)));
            put16(o, int16_t(uint16_t(l.numLeafBrushes)));
        }
        for (int32_t b : leafBrushes_) put16(lumps[kLeafBrushes], int16_t(uint16_t(b)));
        for (size_t i = 0; i < models.size(); ++i) {
            std::string &o = lumps[kModels];
            float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
            bool first = true;
            for (const Box &b : models[i]->brushes) {
                for (int k = 0; k < 3; ++k) {
                    if (first || b.mins[k] < mins[k]) mins[k] = b.mins[k];
                    if (first || b.maxs[k] > maxs[k]) maxs[k] = b.maxs[k];
                }
                first = false;
            }
            for (float v : mins) putf(o, v);
            for (float v : maxs) putf(o, v);
            for (float v : models[i]->origin) putf(o, v);
            put32(o, headnodes[i]);
            put32(o, 0);
            put32(o, 0);
        }
        for (const BrushRec &b : brushes_) {
            put32(lumps[kBrushes], b.firstSide);
            put32(lumps[kBrushes], b.numSides);
            put32(lumps[kBrushes], b.contents);
        }
        for (const auto &s : sides_) {
            put16(lumps[kBrushSides], int16_t(uint16_t(s.first)));
            put16(lumps[kBrushSides], int16_t(s.second));
        }
        int32_t portalCount = 0;
        for (int32_t i = 0; i < def_.numAreas; ++i) {
            int32_t num = 0;
            for (const AreaPortalDef &p : def_.portals) {
                if (p.a != i && p.b != i) continue;
                put32(lumps[kAreaPortals], p.portalnum);
                put32(lumps[kAreaPortals], p.a == i ? p.b : p.a);
                ++num;
            }
            put32(lumps[kAreas], num);
            put32(lumps[kAreas], portalCount);
            portalCount += num;
        }
        if (def_.pvs) lumps[kVisibility] = visibility();
        if (def_.emitFaces) faces(lumps);

        std::string out = "IBSP";
        put32(out, 38);
        const size_t header = out.size() + kNumLumps * 8;
        std::string body;
        int32_t ofs[kNumLumps], len[kNumLumps];
        for (int i = 0; i < kNumLumps; ++i) {
            while ((header + body.size()) % 4) body.push_back('\0');
            ofs[i] = int32_t(header + body.size());
            len[i] = int32_t(lumps[i].size());
            body += lumps[i];
        }
        for (int i = 0; i < kNumLumps; ++i) {
            put32(out, ofs[i]);
            put32(out, len[i]);
        }
        return out + body;
    }

    // Six quads per drawable brush. Edge 0 is the reserved dummy edge.
    void faces(std::string *lumps) const {
        constexpr int32_t kDrawable = 0x1 | 0x2 | 0x8 | 0x10 | 0x20;
        put16(lumps[kEdges], 0);
        put16(lumps[kEdges], 0);
        int32_t vertexCount = 0, edgeCount = 1, surfEdgeCount = 0;
        for (const Box &b : boxes_) {
            if (!(b.contents & kDrawable)) continue;
            for (int a = 0; a < 3; ++a) {
                for (int side = 0; side < 2; ++side) {
                    const float v = side ? b.maxs[a] : b.mins[a];
                    const int u = (a + 1) % 3, w = (a + 2) % 3;
                    const float cu[4] = {b.mins[u], b.maxs[u], b.maxs[u], b.mins[u]};
                    const float cw[4] = {b.mins[w], b.mins[w], b.maxs[w], b.maxs[w]};
                    for (int c = 0; c < 4; ++c) {
                        float p[3];
                        p[a] = v;
                        p[u] = cu[c];
                        p[w] = cw[c];
                        for (float f : p) putf(lumps[kVertexes], f);
                    }
                    for (int c = 0; c < 4; ++c) {
                        // Odd edges are stored reversed and referenced with a
                        // negative surfedge (whose polygon vertex is v[1]).
                        const int16_t from = int16_t(uint16_t(vertexCount + c));
                        const int16_t to = int16_t(uint16_t(vertexCount + (c + 1) % 4));
                        put16(lumps[kEdges], c % 2 ? to : from);
                        put16(lumps[kEdges], c % 2 ? from : to);
                        put32(lumps[kSurfEdges], c % 2 ? -(edgeCount + c) : edgeCount + c);
                    }
                    std::string &o = lumps[kFaces];
                    put16(o, int16_t(uint16_t(planeIndex_.at(std::make_pair(a, v)))));
                    put16(o, int16_t(side ? 0 : 1)); // min side faces -axis
                    put32(o, surfEdgeCount);
                    put16(o, 4);
                    put16(o, int16_t(b.texinfo));
                    put32(o, 0); // styles
                    put32(o, -1); // lightofs
                    vertexCount += 4;
                    edgeCount += 4;
                    surfEdgeCount += 4;
                }
            }
        }
    }

    // dvis_t: numclusters, bitofs[numclusters][2], Quake 2 zero-run rows.
    static std::string compressRow(const std::vector<uint8_t> &row) {
        std::string out;
        for (size_t i = 0; i < row.size(); ++i) {
            if (row[i]) {
                out.push_back(char(row[i]));
                continue;
            }
            size_t rep = 1;
            while (i + 1 < row.size() && row[i + 1] == 0 && rep < 255) {
                ++i;
                ++rep;
            }
            out.push_back('\0');
            out.push_back(char(rep));
        }
        return out;
    }

    std::string visibility() const {
        const int32_t n = clusters_;
        const size_t rowBytes = size_t((n + 7) >> 3);
        std::string data;
        std::vector<int32_t> ofs;
        const int32_t header = 4 + n * 8;
        for (int32_t a = 0; a < n; ++a) {
            std::vector<uint8_t> pvsRow(rowBytes, 0), phsRow(rowBytes, 0xff);
            for (int32_t b = 0; b < n; ++b) {
                const auto &A = clusterBounds_[size_t(a)], &B = clusterBounds_[size_t(b)];
                if (def_.pvs(&A[0], &A[3], &B[0], &B[3])) pvsRow[size_t(b >> 3)] |= uint8_t(1 << (b & 7));
            }
            ofs.push_back(header + int32_t(data.size()));
            data += compressRow(pvsRow);
            ofs.push_back(header + int32_t(data.size()));
            data += compressRow(phsRow);
        }
        std::string out;
        put32(out, n);
        for (int32_t v : ofs) put32(out, v);
        return out + data;
    }

    const MapDef &def_;
    std::vector<std::array<float, 6>> clusterBounds_;
    std::vector<PlaneRec> planes_;
    std::map<std::pair<int, float>, int32_t> planeIndex_;
    std::vector<NodeRec> nodes_;
    std::vector<LeafRec> leafs_;
    std::vector<int32_t> leafBrushes_;
    std::vector<BrushRec> brushes_;
    std::vector<std::pair<int32_t, int32_t>> sides_;
    std::vector<Box> boxes_;
    int32_t clusters_ = 0;
};

inline std::string build(const MapDef &def, Stats *stats = nullptr) { return Builder(def).build(stats); }

} // namespace bsp_builder
