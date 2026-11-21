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

struct MapDef {
    std::vector<Texinfo> texinfo;
    ModelDef world;
    std::vector<ModelDef> inlineModels; // "*1", "*2", ...
    std::string entities;
    int32_t numAreas = 2; // area 0 (solid) and area 1
    float extent = 4096;  // root region half size
};

struct Stats {
    int32_t planes = 0, nodes = 0, leafs = 0, leafBrushes = 0, brushes = 0, brushSides = 0, clusters = 0;
    std::vector<int32_t> modelHeadnodes;
};

// Lump indices (qfiles.h).
enum {
    kEntities = 0,
    kPlanes = 1,
    kVisibility = 3,
    kNodes = 4,
    kTexinfo = 5,
    kLeafs = 8,
    kLeafBrushes = 10,
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
            leaf.area = def_.numAreas > 1 ? 1 : 0;
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
        for (int32_t i = 0; i < def_.numAreas; ++i) {
            put32(lumps[kAreas], 0);
            put32(lumps[kAreas], 0);
        }

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

    const MapDef &def_;
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
