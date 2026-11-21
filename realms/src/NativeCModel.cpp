// Realms Engine - C++ reference collision model (Quake 2 cmodel.c).
// See NativeCModel.h. Keep in lock-step with realms/pascal/rlm_cmodel.pas.

#include "NativeCModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace realms_native {
namespace {

constexpr int32_t kBspIdent = 'I' | ('B' << 8) | ('S' << 16) | ('P' << 24);
constexpr int32_t kBspVersion = 38;
constexpr int kNumLumps = 19;
constexpr size_t kHeaderSize = 8 + kNumLumps * 8;

enum Lump {
    kLumpEntities = 0,
    kLumpPlanes = 1,
    kLumpVisibility = 3,
    kLumpNodes = 4,
    kLumpTexinfo = 5,
    kLumpLeafs = 8,
    kLumpLeafBrushes = 10,
    kLumpModels = 13,
    kLumpBrushes = 14,
    kLumpBrushSides = 15,
    kLumpAreas = 17,
    kLumpAreaPortals = 18
};

// qfiles.h MAX_MAP_* limits.
constexpr int32_t kMaxModels = 1024;
constexpr int32_t kMaxBrushes = 8192;
constexpr int32_t kMaxEntString = 0x40000;
constexpr int32_t kMaxTexinfo = 8192;
constexpr int32_t kMaxAreas = 256;
constexpr int32_t kMaxAreaPortals = 1024;
constexpr int32_t kMaxPlanes = 65536;
constexpr int32_t kMaxNodes = 65536;
constexpr int32_t kMaxBrushSides = 65536;
constexpr int32_t kMaxLeafs = 65536;
constexpr int32_t kMaxLeafBrushes = 65536;
constexpr int32_t kMaxVisibility = 0x100000;
// Real maps are a few dozen levels deep; the limit keeps the recursive
// traces' stack use bounded for hostile files.
constexpr int32_t kMaxTreeDepth = 1024;
constexpr int32_t kMaxBoxLeafs = 1024;

constexpr float kDistEpsilon = 0.03125f;

struct Plane {
    float normal[3] = {0, 0, 0};
    float dist = 0;
    int32_t type = 0;
};

struct Node {
    int32_t plane = 0;
    int32_t children[2] = {0, 0};
};

struct Surface {
    char name[32] = {};
    int32_t flags = 0;
    int32_t value = 0;
};

struct Leaf {
    int32_t contents = 0, cluster = 0, area = 0, firstLeafBrush = 0, numLeafBrushes = 0;
};

struct Brush {
    int32_t contents = 0, firstSide = 0, numSides = 0;
    int32_t checkcount = 0;
};

struct BrushSide {
    int32_t plane = 0;
    int32_t surface = -1; // -1: null surface
};

struct Model {
    float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0}, origin[3] = {0, 0, 0};
    int32_t headnode = 0;
};

struct Area {
    int32_t numPortals = 0, firstPortal = 0;
    int32_t floodnum = 0, floodvalid = 0;
};

struct AreaPortal {
    int32_t portalnum = 0, otherArea = 0;
};

struct MapState {
    bool loaded = false;
    std::string name;
    uint32_t checksum = 0;
    std::vector<Plane> planes;
    std::vector<Node> nodes;
    std::vector<Surface> surfaces;
    std::vector<Leaf> leafs;
    std::vector<int32_t> leafBrushes;
    std::vector<Brush> brushes;
    std::vector<BrushSide> sides;
    std::vector<Model> models;
    std::string entities;
    std::vector<uint8_t> visibility;  // compressed PVS/PHS rows (dvis_t)
    std::vector<int32_t> visOffsets;  // bitofs[cluster][2], flattened
    std::vector<Area> areas;
    std::vector<AreaPortal> areaPortals;
    std::vector<uint8_t> portalOpen = std::vector<uint8_t>(size_t(kMaxAreaPortals), 0);
    int32_t floodValid = 0;
    bool meshMap = false; // triangle mesh map: point contents test the leaf's brushes
    // Counts as stored in the file (the vectors also hold the box hull).
    int32_t numPlanes = 0, numNodes = 0, numLeafs = 0, numLeafBrushes = 0, numBrushes = 0, numSides = 0;
    int32_t numClusters = 0, numAreas = 0, numAreaPortals = 0;
    int32_t emptyLeaf = 0;
    int32_t boxHeadnode = 0, boxPlanes = 0;
    int32_t checkcount = 0;
};

MapState &M() {
    static MapState m;
    return m;
}

// ------------------------------------------------------------ helpers ---

int32_t rd32(const unsigned char *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return int32_t(v);
}
int32_t rd16(const unsigned char *p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
int32_t rdu16(const unsigned char *p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
float rdf(const unsigned char *p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void angleVectors(const float *angles, float *forward, float *right, float *up) {
    const float k = float(3.14159265358979323846 * 2.0 / 360.0);
    float a = angles[1] * k;
    const float sy = std::sin(a), cy = std::cos(a);
    a = angles[0] * k;
    const float sp = std::sin(a), cp = std::cos(a);
    a = angles[2] * k;
    const float sr = std::sin(a), cr = std::cos(a);
    forward[0] = cp * cy;
    forward[1] = cp * sy;
    forward[2] = -sp;
    right[0] = -1 * sr * sp * cy + -1 * cr * -sy;
    right[1] = -1 * sr * sp * sy + -1 * cr * cy;
    right[2] = -1 * sr * cp;
    up[0] = cr * sp * cy + -sr * -sy;
    up[1] = cr * sp * sy + -sr * cy;
    up[2] = cr * cp;
}

void rotateInto(float *v, const float *f, const float *r, const float *u) {
    const float t[3] = {v[0], v[1], v[2]};
    v[0] = dot(t, f);
    v[1] = -dot(t, r);
    v[2] = dot(t, u);
}

bool anyNonZero(const float *v) { return v && (v[0] != 0 || v[1] != 0 || v[2] != 0); }

// ------------------------------------------------------------- loading ---

struct LumpView {
    const unsigned char *data = nullptr;
    int32_t len = 0;
};

class Loader {
public:
    Loader(const unsigned char *data, size_t size, std::string &error) : data_(data), size_(size), error_(error) {}

    bool fail(const std::string &msg) {
        error_ = msg;
        return false;
    }

    bool header() {
        if (!data_ || size_ < kHeaderSize) return fail("file is too small to be a BSP");
        if (rd32(data_) != kBspIdent) return fail("not an IBSP file");
        const int32_t version = rd32(data_ + 4);
        if (version != kBspVersion)
            return fail("wrong version number (" + std::to_string(version) + " should be 38)");
        for (int i = 0; i < kNumLumps; ++i) {
            const int32_t ofs = rd32(data_ + 8 + i * 8), len = rd32(data_ + 12 + i * 8);
            if (ofs < 0 || len < 0 || int64_t(ofs) + int64_t(len) > int64_t(size_))
                return fail("lump " + std::to_string(i) + " is out of range");
            lumps_[i].data = data_ + ofs;
            lumps_[i].len = len;
        }
        return true;
    }

    // Element count of a lump, enforcing whole records and the Quake 2 limit.
    bool count(int lump, int32_t elemSize, int32_t maxCount, int32_t minCount, const char *what, int32_t &out) {
        const int32_t len = lumps_[lump].len;
        if (len % elemSize != 0) return fail(std::string("funny lump size (") + what + ")");
        out = len / elemSize;
        if (out > maxCount) return fail(std::string("too many ") + what);
        if (out < minCount) return fail(std::string("map has no ") + what);
        return true;
    }

    const unsigned char *at(int lump, int32_t index, int32_t elemSize) const {
        return lumps_[lump].data + size_t(index) * size_t(elemSize);
    }
    int32_t lumpLen(int lump) const { return lumps_[lump].len; }

private:
    const unsigned char *data_;
    size_t size_;
    std::string &error_;
    LumpView lumps_[kNumLumps];
};

bool validChild(const MapState &m, int32_t c) {
    if (c >= 0) return c < m.numNodes;
    return -1 - int64_t(c) < int64_t(m.numLeafs);
}

// Rejects node graphs with cycles (they would never terminate) or deeper
// than kMaxTreeDepth. Iterative DFS; leafs have depth 0.
bool checkTree(const MapState &m, std::string &error) {
    const size_t n = size_t(m.numNodes);
    std::vector<uint8_t> state(n, 0); // 0 new, 1 on stack, 2 done
    std::vector<int32_t> depth(n, 0);
    std::vector<std::pair<int32_t, int>> stack;
    for (size_t root = 0; root < n; ++root) {
        if (state[root]) continue;
        state[root] = 1;
        stack.emplace_back(int32_t(root), 0);
        while (!stack.empty()) {
            auto &top = stack.back();
            const int32_t node = top.first;
            if (top.second < 2) {
                const int32_t c = m.nodes[size_t(node)].children[top.second++];
                if (c < 0) continue;
                if (state[size_t(c)] == 1) {
                    error = "BSP node graph has a cycle";
                    return false;
                }
                if (state[size_t(c)] == 0) {
                    state[size_t(c)] = 1;
                    stack.emplace_back(c, 0);
                }
                continue;
            }
            int32_t d = 0;
            for (int32_t c : m.nodes[size_t(node)].children)
                if (c >= 0 && depth[size_t(c)] > d) d = depth[size_t(c)];
            depth[size_t(node)] = d + 1;
            if (depth[size_t(node)] > kMaxTreeDepth) {
                error = "BSP tree is too deep";
                return false;
            }
            state[size_t(node)] = 2;
            stack.pop_back();
        }
    }
    return true;
}

// CM_InitBoxHull: six nodes/planes and one brush/leaf after the map's own.
void initBoxHull(MapState &m) {
    m.boxHeadnode = m.numNodes;
    m.boxPlanes = m.numPlanes;
    Brush box;
    box.contents = REALMS_CONTENTS_MONSTER;
    box.firstSide = m.numSides;
    box.numSides = 6;
    m.brushes.push_back(box);
    Leaf leaf;
    leaf.contents = REALMS_CONTENTS_MONSTER;
    leaf.firstLeafBrush = m.numLeafBrushes;
    leaf.numLeafBrushes = 1;
    m.leafs.push_back(leaf);
    m.leafBrushes.push_back(m.numBrushes);
    m.planes.resize(size_t(m.numPlanes) + 12);
    for (int i = 0; i < 6; ++i) {
        const int side = i & 1;
        BrushSide s;
        s.plane = m.numPlanes + i * 2 + side;
        s.surface = -1;
        m.sides.push_back(s);
        Node node;
        node.plane = m.numPlanes + i * 2;
        node.children[side] = -1 - m.emptyLeaf;
        node.children[side ^ 1] = i != 5 ? m.boxHeadnode + i + 1 : -1 - m.numLeafs;
        m.nodes.push_back(node);
        Plane &p = m.planes[size_t(m.numPlanes + i * 2)];
        p = Plane{};
        p.type = i >> 1;
        p.normal[i >> 1] = 1;
        Plane &q = m.planes[size_t(m.numPlanes + i * 2 + 1)];
        q = Plane{};
        q.type = 3 + (i >> 1);
        q.normal[i >> 1] = -1;
    }
}

void floodAreaConnections(MapState &m);

bool loadInto(MapState &m, const unsigned char *data, size_t size, std::string &error) {
    Loader in(data, size, error);
    if (!in.header()) return false;

    int32_t n = 0;
    // Surfaces (texinfo).
    if (!in.count(kLumpTexinfo, 76, kMaxTexinfo, 1, "surfaces", n)) return false;
    m.surfaces.resize(size_t(n));
    for (int32_t i = 0; i < n; ++i) {
        const unsigned char *p = in.at(kLumpTexinfo, i, 76);
        Surface &s = m.surfaces[size_t(i)];
        s.flags = rd32(p + 32);
        s.value = rd32(p + 36);
        for (int j = 0; j < 31 && p[40 + j]; ++j) s.name[j] = char(p[40 + j]);
    }

    // Leafs.
    if (!in.count(kLumpLeafs, 28, kMaxLeafs, 1, "leafs", m.numLeafs)) return false;
    m.leafs.resize(size_t(m.numLeafs));
    m.numClusters = 0;
    for (int32_t i = 0; i < m.numLeafs; ++i) {
        const unsigned char *p = in.at(kLumpLeafs, i, 28);
        Leaf &l = m.leafs[size_t(i)];
        l.contents = rd32(p);
        l.cluster = rd16(p + 4);
        l.area = rd16(p + 6);
        l.firstLeafBrush = rdu16(p + 24);
        l.numLeafBrushes = rdu16(p + 26);
        if (l.cluster < -1) return in.fail("bad leaf cluster");
        if (l.cluster >= m.numClusters) m.numClusters = l.cluster + 1;
    }
    if (m.leafs[0].contents != REALMS_CONTENTS_SOLID) return in.fail("leaf 0 is not CONTENTS_SOLID");
    m.emptyLeaf = -1;
    for (int32_t i = 1; i < m.numLeafs; ++i) {
        if (m.leafs[size_t(i)].contents == 0) {
            m.emptyLeaf = i;
            break;
        }
    }
    if (m.emptyLeaf < 0) return in.fail("map does not have an empty leaf");

    // Leaf brushes.
    if (!in.count(kLumpLeafBrushes, 2, kMaxLeafBrushes, 1, "leaf brushes", m.numLeafBrushes)) return false;
    m.leafBrushes.resize(size_t(m.numLeafBrushes));
    for (int32_t i = 0; i < m.numLeafBrushes; ++i) m.leafBrushes[size_t(i)] = rdu16(in.at(kLumpLeafBrushes, i, 2));

    // Planes.
    if (!in.count(kLumpPlanes, 20, kMaxPlanes, 1, "planes", m.numPlanes)) return false;
    m.planes.resize(size_t(m.numPlanes));
    for (int32_t i = 0; i < m.numPlanes; ++i) {
        const unsigned char *p = in.at(kLumpPlanes, i, 20);
        Plane &pl = m.planes[size_t(i)];
        for (int j = 0; j < 3; ++j) pl.normal[j] = rdf(p + 4 * j);
        pl.dist = rdf(p + 12);
        pl.type = rd32(p + 16);
        if (pl.type < 0 || pl.type > 5) return in.fail("bad plane type");
    }

    // Brushes.
    if (!in.count(kLumpBrushes, 12, kMaxBrushes, 0, "brushes", m.numBrushes)) return false;
    m.brushes.resize(size_t(m.numBrushes));
    for (int32_t i = 0; i < m.numBrushes; ++i) {
        const unsigned char *p = in.at(kLumpBrushes, i, 12);
        Brush &b = m.brushes[size_t(i)];
        b.firstSide = rd32(p);
        b.numSides = rd32(p + 4);
        b.contents = rd32(p + 8);
    }

    // Brush sides.
    if (!in.count(kLumpBrushSides, 4, kMaxBrushSides, 0, "brush sides", m.numSides)) return false;
    m.sides.resize(size_t(m.numSides));
    const int32_t numSurfaces = int32_t(m.surfaces.size());
    for (int32_t i = 0; i < m.numSides; ++i) {
        const unsigned char *p = in.at(kLumpBrushSides, i, 4);
        BrushSide &s = m.sides[size_t(i)];
        s.plane = rdu16(p);
        s.surface = rd16(p + 2);
        if (s.plane >= m.numPlanes) return in.fail("bad brush side plane");
        if (s.surface < -1 || s.surface >= numSurfaces) return in.fail("bad brush side texinfo");
    }
    for (const Brush &b : m.brushes) {
        if (b.firstSide < 0 || b.numSides < 0 || int64_t(b.firstSide) + b.numSides > m.numSides)
            return in.fail("bad brush sides");
    }
    for (int32_t v : m.leafBrushes)
        if (v >= m.numBrushes) return in.fail("bad leaf brush");
    for (const Leaf &l : m.leafs)
        if (l.firstLeafBrush + l.numLeafBrushes > m.numLeafBrushes) return in.fail("bad leaf brush range");

    // Inline models.
    if (!in.count(kLumpModels, 48, kMaxModels, 1, "models", n)) return false;
    m.models.resize(size_t(n));
    for (int32_t i = 0; i < n; ++i) {
        const unsigned char *p = in.at(kLumpModels, i, 48);
        Model &md = m.models[size_t(i)];
        for (int j = 0; j < 3; ++j) {
            md.mins[j] = rdf(p + 4 * j) - 1; // spread the mins / maxs by a pixel
            md.maxs[j] = rdf(p + 12 + 4 * j) + 1;
            md.origin[j] = rdf(p + 24 + 4 * j);
        }
        md.headnode = rd32(p + 36);
    }

    // Nodes.
    if (!in.count(kLumpNodes, 28, kMaxNodes, 1, "nodes", m.numNodes)) return false;
    m.nodes.resize(size_t(m.numNodes));
    for (int32_t i = 0; i < m.numNodes; ++i) {
        const unsigned char *p = in.at(kLumpNodes, i, 28);
        Node &nd = m.nodes[size_t(i)];
        nd.plane = rd32(p);
        nd.children[0] = rd32(p + 4);
        nd.children[1] = rd32(p + 8);
        if (nd.plane < 0 || nd.plane >= m.numPlanes) return in.fail("bad node plane");
    }
    for (const Node &nd : m.nodes)
        if (!validChild(m, nd.children[0]) || !validChild(m, nd.children[1])) return in.fail("bad node child");
    for (const Model &md : m.models)
        if (!validChild(m, md.headnode)) return in.fail("bad model headnode");
    if (!checkTree(m, error)) return false;

    // Areas and area portals.
    if (!in.count(kLumpAreas, 8, kMaxAreas, 0, "areas", m.numAreas)) return false;
    if (!in.count(kLumpAreaPortals, 8, kMaxAreaPortals, 0, "area portals", m.numAreaPortals)) return false;
    m.areas.resize(size_t(m.numAreas));
    for (int32_t i = 0; i < m.numAreas; ++i) {
        const unsigned char *p = in.at(kLumpAreas, i, 8);
        const int32_t num = rd32(p), first = rd32(p + 4);
        if (num < 0 || first < 0 || int64_t(first) + num > m.numAreaPortals) return in.fail("bad area portals");
        m.areas[size_t(i)].numPortals = num;
        m.areas[size_t(i)].firstPortal = first;
    }
    m.areaPortals.resize(size_t(m.numAreaPortals));
    for (int32_t i = 0; i < m.numAreaPortals; ++i) {
        const unsigned char *p = in.at(kLumpAreaPortals, i, 8);
        const int32_t portal = rd32(p), other = rd32(p + 4);
        if (portal < 0 || portal >= kMaxAreaPortals || other < 0 || other >= m.numAreas)
            return in.fail("bad area portal");
        m.areaPortals[size_t(i)].portalnum = portal;
        m.areaPortals[size_t(i)].otherArea = other;
    }
    const int32_t areaLimit = m.numAreas > 0 ? m.numAreas : 1;
    for (const Leaf &l : m.leafs)
        if (l.area < 0 || l.area >= areaLimit) return in.fail("bad leaf area");

    // Visibility: dvis_t header (numclusters, bitofs[numclusters][2]) and the
    // run-length compressed rows. Rows are decompressed with bounds checks.
    const int32_t visLen = in.lumpLen(kLumpVisibility);
    if (visLen > kMaxVisibility) return in.fail("visibility lump is too large");
    if (visLen > 0) {
        const unsigned char *vis = in.at(kLumpVisibility, 0, 1);
        if (visLen < 4) return in.fail("visibility lump is truncated");
        const int32_t visClusters = rd32(vis);
        if (visClusters < 0 || visClusters > kMaxLeafs || 4 + int64_t(visClusters) * 8 > visLen)
            return in.fail("bad visibility cluster count");
        m.visOffsets.resize(size_t(visClusters) * 2);
        for (int32_t i = 0; i < visClusters * 2; ++i) {
            const int32_t ofs = rd32(vis + 4 + 4 * i);
            if (ofs < 0 || ofs > visLen) return in.fail("bad visibility offset");
            m.visOffsets[size_t(i)] = ofs;
        }
        if (m.numClusters > visClusters) return in.fail("leaf cluster out of range for visibility");
        m.numClusters = visClusters;
        m.visibility.assign(vis, vis + visLen);
    }

    // Entity string, up to the first NUL.
    const int32_t entLen = in.lumpLen(kLumpEntities);
    if (entLen > kMaxEntString) return in.fail("entity lump is too large");
    const unsigned char *ent = in.at(kLumpEntities, 0, 1);
    int32_t used = 0;
    while (used < entLen && ent[used]) ++used;
    m.entities.assign(reinterpret_cast<const char *>(ent), size_t(used));

    initBoxHull(m);
    floodAreaConnections(m);
    return true;
}

// ------------------------------------------------------- triangle meshes ---

constexpr int32_t kMaxMeshVertices = 1 << 24;
constexpr int32_t kMaxMeshTriangles = 1 << 20;
constexpr int32_t kMaxMeshSurfaces = 65536;
constexpr int32_t kMeshLeafBrushes = 8;
constexpr int32_t kMeshMaxDepth = 40;

struct Vec {
    float v[3];
};

Vec vsub(const Vec &a, const Vec &b) { return Vec{{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2]}}; }
Vec vcross(const Vec &a, const Vec &b) {
    return Vec{{a.v[1] * b.v[2] - a.v[2] * b.v[1], a.v[2] * b.v[0] - a.v[0] * b.v[2], a.v[0] * b.v[1] - a.v[1] * b.v[0]}};
}
float vdot(const Vec &a, const Vec &b) { return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2]; }
bool vnormalize(Vec &a) {
    const float len = std::sqrt(vdot(a, a));
    if (!(len > 1e-6f)) return false;
    for (float &x : a.v) x /= len;
    return true;
}

// Plane type as Quake 2 stores it: exact positive axes are axial (0..2) and
// everything else is 3 + the dominant axis (only axial node planes take the
// fast path, brush sides always use the full dot product).
int32_t meshPlaneType(const Vec &n) {
    for (int a = 0; a < 3; ++a)
        if (n.v[a] == 1.0f && n.v[(a + 1) % 3] == 0.0f && n.v[(a + 2) % 3] == 0.0f) return a;
    int major = 0;
    for (int a = 1; a < 3; ++a)
        if (std::fabs(n.v[a]) > std::fabs(n.v[major])) major = a;
    return 3 + major;
}

struct MeshBrush {
    float mins[3], maxs[3], center[3];
};

class MeshBuilder {
public:
    explicit MeshBuilder(MapState &m) : m_(m) {}

    int32_t addPlane(const Vec &n, float dist) {
        Plane p;
        for (int i = 0; i < 3; ++i) p.normal[i] = n.v[i];
        p.dist = dist;
        p.type = meshPlaneType(n);
        m_.planes.push_back(p);
        return int32_t(m_.planes.size()) - 1;
    }

    // Triangle (a, b, c), counter-clockwise seen from the front, becomes a
    // slab brush: front face, back face `thick` units behind, three edge
    // planes, plus Quake 2 axial and edge bevels so box traces stay tight.
    bool addTriangle(const Vec &a, const Vec &b, const Vec &c, float thick, int32_t surface, int32_t contents) {
        Vec n = vcross(vsub(b, a), vsub(c, a));
        if (!vnormalize(n)) return false; // degenerate
        const Vec pts[6] = {a, b, c, Vec{{a.v[0] - n.v[0] * thick, a.v[1] - n.v[1] * thick, a.v[2] - n.v[2] * thick}},
                            Vec{{b.v[0] - n.v[0] * thick, b.v[1] - n.v[1] * thick, b.v[2] - n.v[2] * thick}},
                            Vec{{c.v[0] - n.v[0] * thick, c.v[1] - n.v[1] * thick, c.v[2] - n.v[2] * thick}}};
        std::vector<Vec> normals;
        std::vector<float> dists;
        auto addSide = [&](Vec pn, float d) {
            for (size_t i = 0; i < normals.size(); ++i) // skip duplicates
                if (vdot(normals[i], pn) > 0.99999f && std::fabs(dists[i] - d) < 0.01f) return;
            normals.push_back(pn);
            dists.push_back(d);
        };
        addSide(n, vdot(n, a));
        const Vec back{{-n.v[0], -n.v[1], -n.v[2]}};
        addSide(back, vdot(back, pts[3]));
        const Vec *tri[3] = {&a, &b, &c};
        for (int e = 0; e < 3; ++e) {
            const Vec &p = *tri[e], &q = *tri[(e + 1) % 3];
            Vec en = vcross(vsub(q, p), n);
            if (!vnormalize(en)) return false;
            addSide(en, vdot(en, p));
        }
        // Axial bevels: the brush bounds.
        MeshBrush mb;
        for (int i = 0; i < 3; ++i) {
            mb.mins[i] = mb.maxs[i] = pts[0].v[i];
            for (const Vec &p : pts) {
                mb.mins[i] = std::min(mb.mins[i], p.v[i]);
                mb.maxs[i] = std::max(mb.maxs[i], p.v[i]);
            }
            mb.center[i] = (mb.mins[i] + mb.maxs[i]) * 0.5f;
        }
        for (int i = 0; i < 3; ++i) {
            Vec pos{{0, 0, 0}}, neg{{0, 0, 0}};
            pos.v[i] = 1;
            neg.v[i] = -1;
            addSide(pos, mb.maxs[i]);
            addSide(neg, -mb.mins[i]);
        }
        // Edge bevels: edge direction x axis, kept when every vertex is behind.
        static const int edges[9][2] = {{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {0, 3}, {1, 4}, {2, 5}};
        for (const auto &e : edges) {
            const Vec dir = vsub(pts[e[1]], pts[e[0]]);
            for (int axis = 0; axis < 3; ++axis) {
                for (int sign = -1; sign <= 1; sign += 2) {
                    Vec ax{{0, 0, 0}};
                    ax.v[axis] = float(sign);
                    Vec bn = vcross(dir, ax);
                    if (!vnormalize(bn)) continue;
                    const float d = vdot(bn, pts[e[0]]);
                    bool behind = true;
                    for (const Vec &p : pts)
                        if (vdot(bn, p) - d > 0.01f) behind = false;
                    if (behind) addSide(bn, d);
                }
            }
        }
        Brush brush;
        brush.contents = contents;
        brush.firstSide = int32_t(m_.sides.size());
        brush.numSides = int32_t(normals.size());
        for (size_t i = 0; i < normals.size(); ++i) {
            BrushSide side;
            side.plane = addPlane(normals[i], dists[i]);
            side.surface = surface;
            m_.sides.push_back(side);
        }
        m_.brushes.push_back(brush);
        bounds_.push_back(mb);
        return true;
    }

    // Axial kd-tree; leafs list every brush whose bounds touch them.
    int32_t build(std::vector<int32_t> &ids, int depth) {
        float cmin[3], cmax[3];
        for (int i = 0; i < 3; ++i) {
            cmin[i] = 1e30f;
            cmax[i] = -1e30f;
        }
        for (int32_t id : ids) {
            for (int i = 0; i < 3; ++i) {
                cmin[i] = std::min(cmin[i], bounds_[size_t(id)].center[i]);
                cmax[i] = std::max(cmax[i], bounds_[size_t(id)].center[i]);
            }
        }
        int axis = 0;
        for (int i = 1; i < 3; ++i)
            if (cmax[i] - cmin[i] > cmax[axis] - cmin[axis]) axis = i;
        const float split = (cmin[axis] + cmax[axis]) * 0.5f;
        if (int32_t(ids.size()) > kMeshLeafBrushes && depth < kMeshMaxDepth && cmax[axis] - cmin[axis] > 0.001f) {
            std::vector<int32_t> front, back;
            for (int32_t id : ids) {
                if (bounds_[size_t(id)].maxs[axis] >= split) front.push_back(id);
                if (bounds_[size_t(id)].mins[axis] <= split) back.push_back(id);
            }
            // Splits that duplicate too many brushes (large overlapping
            // triangles) stop here, and a node budget bounds the whole tree.
            if (front.size() < ids.size() && back.size() < ids.size() &&
                front.size() + back.size() <= ids.size() + ids.size() / 2 &&
                int64_t(m_.nodes.size()) < 4 * int64_t(bounds_.size()) + 64) {
                std::vector<int32_t>().swap(ids);
                const int32_t index = int32_t(m_.nodes.size());
                m_.nodes.push_back(Node{});
                Vec n{{0, 0, 0}};
                n.v[axis] = 1;
                m_.nodes[size_t(index)].plane = addPlane(n, split);
                const int32_t f = build(front, depth + 1);
                const int32_t b = build(back, depth + 1);
                m_.nodes[size_t(index)].children[0] = f;
                m_.nodes[size_t(index)].children[1] = b;
                return index;
            }
        }
        Leaf leaf;
        leaf.cluster = 0;
        leaf.area = 1;
        leaf.firstLeafBrush = int32_t(m_.leafBrushes.size());
        leaf.numLeafBrushes = int32_t(ids.size());
        for (int32_t id : ids) {
            leaf.contents |= m_.brushes[size_t(id)].contents;
            m_.leafBrushes.push_back(id);
        }
        m_.leafs.push_back(leaf);
        return -1 - (int32_t(m_.leafs.size()) - 1);
    }

    const std::vector<MeshBrush> &bounds() const { return bounds_; }

private:
    MapState &m_;
    std::vector<MeshBrush> bounds_;
};

bool loadMeshInto(MapState &m, const RealmsMeshDesc &d, std::string &error) {
    auto fail = [&](const char *msg) {
        error = msg;
        return false;
    };
    if (d.num_vertices < 0 || d.num_vertices > kMaxMeshVertices) return fail("bad mesh vertex count");
    if (d.num_triangles < 0 || d.num_triangles > kMaxMeshTriangles) return fail("too many mesh triangles");
    if (d.num_surfaces < 1 || d.num_surfaces > kMaxMeshSurfaces || !d.surfaces) return fail("bad mesh surfaces");
    if ((d.num_vertices > 0 && !d.vertices) || (d.num_triangles > 0 && !d.indices)) return fail("missing mesh data");
    for (int32_t i = 0; i < d.num_vertices * 3; ++i)
        if (!std::isfinite(d.vertices[i]) || std::fabs(d.vertices[i]) > 1e6f) return fail("bad mesh vertex");
    for (int32_t i = 0; i < d.num_triangles * 3; ++i)
        if (d.indices[i] < 0 || d.indices[i] >= d.num_vertices) return fail("bad mesh index");
    if (d.triangle_surfaces)
        for (int32_t i = 0; i < d.num_triangles; ++i)
            if (d.triangle_surfaces[i] < 0 || d.triangle_surfaces[i] >= d.num_surfaces)
                return fail("bad mesh triangle surface");
    if (d.entities && std::strlen(d.entities) > size_t(kMaxEntString)) return fail("entity lump is too large");
    const float thick = d.thickness > 0 && std::isfinite(d.thickness) ? d.thickness : 2.0f;

    m.surfaces.resize(size_t(d.num_surfaces));
    for (int32_t i = 0; i < d.num_surfaces; ++i) {
        Surface &s = m.surfaces[size_t(i)];
        for (int j = 0; j < 31 && d.surfaces[i].name[j]; ++j) s.name[j] = d.surfaces[i].name[j];
        s.flags = d.surfaces[i].flags;
        s.value = d.surfaces[i].value;
    }
    // Leaf 0: the solid dummy leaf; leaf 1: the empty leaf the box hull uses.
    Leaf solid;
    solid.contents = REALMS_CONTENTS_SOLID;
    solid.cluster = -1;
    solid.area = 0;
    Leaf empty;
    empty.cluster = 0;
    empty.area = 1;
    m.leafs = {solid, empty};
    m.emptyLeaf = 1;

    MeshBuilder b(m);
    std::vector<int32_t> ids;
    float mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};
    for (int32_t t = 0; t < d.num_triangles; ++t) {
        const int32_t surface = d.triangle_surfaces ? d.triangle_surfaces[t] : 0;
        const int32_t contents = d.surfaces[surface].contents;
        if (!contents) continue;
        Vec v[3];
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j) v[k].v[j] = d.vertices[size_t(d.indices[t * 3 + k]) * 3 + size_t(j)];
        if (b.addTriangle(v[0], v[1], v[2], thick, surface, contents)) ids.push_back(int32_t(ids.size()));
    }
    for (size_t i = 0; i < b.bounds().size(); ++i) {
        for (int j = 0; j < 3; ++j) {
            mins[j] = i == 0 ? b.bounds()[i].mins[j] : std::min(mins[j], b.bounds()[i].mins[j]);
            maxs[j] = i == 0 ? b.bounds()[i].maxs[j] : std::max(maxs[j], b.bounds()[i].maxs[j]);
        }
    }
    Model world;
    world.headnode = ids.empty() ? -1 - m.emptyLeaf : b.build(ids, 0);
    if (world.headnode < 0) {
        // Headnode 0 must be the world (CM_PointContents(p, 0)), so tiny
        // meshes still get a root node; both sides lead to the same leaf.
        Node root;
        Vec n{{0, 0, 1}};
        root.plane = b.addPlane(n, 0);
        root.children[0] = root.children[1] = world.headnode;
        m.nodes.push_back(root);
        world.headnode = 0;
    }
    for (int j = 0; j < 3; ++j) {
        world.mins[j] = mins[j] - 1;
        world.maxs[j] = maxs[j] + 1;
    }
    m.models = {world};
    m.numPlanes = int32_t(m.planes.size());
    m.numNodes = int32_t(m.nodes.size());
    m.numLeafs = int32_t(m.leafs.size());
    m.numLeafBrushes = int32_t(m.leafBrushes.size());
    m.numBrushes = int32_t(m.brushes.size());
    m.numSides = int32_t(m.sides.size());
    m.numClusters = 1;
    m.numAreas = 2;
    m.numAreaPortals = 0;
    m.areas.assign(2, Area{});
    m.entities = d.entities ? d.entities : "";
    m.meshMap = true;
    initBoxHull(m);
    floodAreaConnections(m);
    return true;
}

// ------------------------------------------------------------ queries ---

bool validHeadnode(int32_t h) {
    const MapState &m = M();
    if (!m.loaded) return false;
    if (h >= 0) return h < m.numNodes + 6;
    return -1 - int64_t(h) < int64_t(m.numLeafs) + 1;
}

int32_t pointLeafnum(const float *p, int32_t num) {
    const MapState &m = M();
    while (num >= 0) {
        const Node &node = m.nodes[size_t(num)];
        const Plane &plane = m.planes[size_t(node.plane)];
        const float d = plane.type < 3 ? p[plane.type] - plane.dist : dot(plane.normal, p) - plane.dist;
        num = d < 0 ? node.children[1] : node.children[0];
    }
    return -1 - num;
}

// BOX_ON_PLANE_SIDE: 1 front, 2 back, 3 both.
int boxOnPlaneSide(const float *mins, const float *maxs, const Plane &p) {
    if (p.type < 3) {
        if (p.dist <= mins[p.type]) return 1;
        if (p.dist >= maxs[p.type]) return 2;
        return 3;
    }
    float d1 = 0, d2 = 0;
    for (int j = 0; j < 3; ++j) {
        if (p.normal[j] < 0) {
            d1 += p.normal[j] * mins[j];
            d2 += p.normal[j] * maxs[j];
        } else {
            d1 += p.normal[j] * maxs[j];
            d2 += p.normal[j] * mins[j];
        }
    }
    int sides = 0;
    if (d1 >= p.dist) sides = 1;
    if (d2 < p.dist) sides |= 2;
    return sides;
}

struct BoxLeafs {
    const float *mins, *maxs;
    int32_t *list;
    int32_t max = 0;
    int32_t count = 0;
    int32_t topnode = -1;
};

// CM_BoxLeafnums_r
void boxLeafnums(BoxLeafs &q, int32_t num) {
    const MapState &m = M();
    for (;;) {
        if (num < 0) {
            if (q.count < q.max) q.list[q.count++] = -1 - num;
            return;
        }
        const Node &node = m.nodes[size_t(num)];
        const int s = boxOnPlaneSide(q.mins, q.maxs, m.planes[size_t(node.plane)]);
        if (s == 1) {
            num = node.children[0];
        } else if (s == 2) {
            num = node.children[1];
        } else { // go down both
            if (q.topnode == -1) q.topnode = num;
            boxLeafnums(q, node.children[0]);
            num = node.children[1];
        }
    }
}

// FloodAreaConnections. Area 0 is the solid/unused area.
void floodArea(MapState &m, int32_t area, int32_t floodnum) {
    Area &a = m.areas[size_t(area)];
    if (a.floodvalid == m.floodValid) return; // already flooded (Quake 2 errors on "reflooded")
    a.floodnum = floodnum;
    a.floodvalid = m.floodValid;
    for (int32_t i = 0; i < a.numPortals; ++i) {
        const AreaPortal &p = m.areaPortals[size_t(a.firstPortal + i)];
        if (m.portalOpen[size_t(p.portalnum)]) floodArea(m, p.otherArea, floodnum);
    }
}

void floodAreaConnections(MapState &m) {
    ++m.floodValid;
    int32_t floodnum = 0;
    for (int32_t i = 1; i < m.numAreas; ++i) {
        if (m.areas[size_t(i)].floodvalid == m.floodValid) continue;
        ++floodnum;
        floodArea(m, i, floodnum);
    }
}

// CM_DecompressVis with bounds checks on the compressed input.
void decompressVis(const MapState &m, int32_t cluster, int which, std::vector<uint8_t> &row) {
    const size_t rowBytes = size_t((m.numClusters + 7) >> 3);
    row.assign(rowBytes, 0);
    if (m.visibility.empty()) { // no vis info, so make all visible
        std::fill(row.begin(), row.end(), uint8_t(0xff));
        return;
    }
    if (cluster < 0 || cluster >= m.numClusters) return;
    size_t in = size_t(m.visOffsets[size_t(cluster) * 2 + size_t(which)]);
    const size_t len = m.visibility.size();
    size_t out = 0;
    while (out < rowBytes) {
        if (in >= len) break; // truncated row: the rest stays invisible
        const uint8_t b = m.visibility[in];
        if (b) {
            row[out++] = b;
            ++in;
            continue;
        }
        if (in + 1 >= len) break;
        size_t c = m.visibility[in + 1];
        in += 2;
        if (c > rowBytes - out) c = rowBytes - out; // "Vis decompression overrun"
        out += c;
    }
}

bool headnodeVisible(int32_t num, const uint8_t *visbits, int32_t size) {
    const MapState &m = M();
    if (num < 0) {
        const int32_t cluster = m.leafs[size_t(-1 - num)].cluster;
        if (cluster < 0 || (cluster >> 3) >= size) return false;
        return (visbits[cluster >> 3] & (1 << (cluster & 7))) != 0;
    }
    const Node &node = m.nodes[size_t(num)];
    return headnodeVisible(node.children[0], visbits, size) || headnodeVisible(node.children[1], visbits, size);
}

// Point contents for mesh maps: their kd-tree leafs are not convex cells, so
// the brushes listed in the leaf decide.
int32_t leafPointContents(int32_t leafnum, const float *p) {
    const MapState &m = M();
    const Leaf &leaf = m.leafs[size_t(leafnum)];
    if (!m.meshMap || leafnum < 2 || leafnum >= m.numLeafs) return leaf.contents;
    int32_t contents = 0;
    for (int32_t k = 0; k < leaf.numLeafBrushes; ++k) {
        const Brush &b = m.brushes[size_t(m.leafBrushes[size_t(leaf.firstLeafBrush + k)])];
        bool inside = true;
        for (int32_t i = 0; i < b.numSides && inside; ++i) {
            const Plane &pl = m.planes[size_t(m.sides[size_t(b.firstSide + i)].plane)];
            if (dot(pl.normal, p) - pl.dist > 0) inside = false;
        }
        if (inside) contents |= b.contents;
    }
    return contents;
}

struct TraceWork {
    float start[3], end[3], mins[3], maxs[3], extents[3];
    int32_t contents = 0;
    bool isPoint = false;
    // trace_t
    bool allsolid = false, startsolid = false;
    float fraction = 1;
    int32_t plane = -1;
    int32_t surface = -1;
    int32_t hitContents = 0;
};

TraceWork gT;

void clipBoxToBrush(const Brush &brush) {
    const MapState &m = M();
    if (brush.numSides == 0) return;
    float enterfrac = -1, leavefrac = 1;
    int32_t clipplane = -1, leadside = -1;
    bool getout = false, startout = false;
    for (int32_t i = 0; i < brush.numSides; ++i) {
        const int32_t sideIndex = brush.firstSide + i;
        const BrushSide &side = m.sides[size_t(sideIndex)];
        const Plane &plane = m.planes[size_t(side.plane)];
        float dist;
        if (!gT.isPoint) {
            float ofs[3];
            for (int j = 0; j < 3; ++j) ofs[j] = plane.normal[j] < 0 ? gT.maxs[j] : gT.mins[j];
            dist = plane.dist - dot(ofs, plane.normal);
        } else {
            dist = plane.dist;
        }
        const float d1 = dot(gT.start, plane.normal) - dist;
        const float d2 = dot(gT.end, plane.normal) - dist;
        if (d2 > 0) getout = true; // endpoint is not in solid
        if (d1 > 0) startout = true;
        // if completely in front of face, no intersection
        if (d1 > 0 && d2 >= d1) return;
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) { // enter
            const float f = (d1 - kDistEpsilon) / (d1 - d2);
            if (f > enterfrac) {
                enterfrac = f;
                clipplane = side.plane;
                leadside = sideIndex;
            }
        } else { // leave
            const float f = (d1 + kDistEpsilon) / (d1 - d2);
            if (f < leavefrac) leavefrac = f;
        }
    }
    if (!startout) { // original point was inside brush
        gT.startsolid = true;
        if (!getout) gT.allsolid = true;
        return;
    }
    if (enterfrac < leavefrac && enterfrac > -1 && enterfrac < gT.fraction) {
        if (enterfrac < 0) enterfrac = 0;
        gT.fraction = enterfrac;
        gT.plane = clipplane;
        gT.surface = m.sides[size_t(leadside)].surface;
        gT.hitContents = brush.contents;
    }
}

void testBoxInBrush(const Brush &brush) {
    const MapState &m = M();
    if (brush.numSides == 0) return;
    for (int32_t i = 0; i < brush.numSides; ++i) {
        const Plane &plane = m.planes[size_t(m.sides[size_t(brush.firstSide + i)].plane)];
        float ofs[3];
        for (int j = 0; j < 3; ++j) ofs[j] = plane.normal[j] < 0 ? gT.maxs[j] : gT.mins[j];
        const float dist = plane.dist - dot(ofs, plane.normal);
        const float d1 = dot(gT.start, plane.normal) - dist;
        if (d1 > 0) return; // if completely in front of face, no intersection
    }
    gT.startsolid = gT.allsolid = true;
    gT.fraction = 0;
    gT.hitContents = brush.contents;
}

template <typename Fn> void forLeafBrushes(int32_t leafnum, Fn fn) {
    MapState &m = M();
    const Leaf &leaf = m.leafs[size_t(leafnum)];
    if (!(leaf.contents & gT.contents)) return;
    for (int32_t k = 0; k < leaf.numLeafBrushes; ++k) {
        Brush &b = m.brushes[size_t(m.leafBrushes[size_t(leaf.firstLeafBrush + k)])];
        if (b.checkcount == m.checkcount) continue; // already checked this brush in another leaf
        b.checkcount = m.checkcount;
        if (!(b.contents & gT.contents)) continue;
        fn(b);
        if (gT.fraction == 0) return;
    }
}

void recursiveHullCheck(int32_t num, float p1f, float p2f, const float *p1, const float *p2) {
    const MapState &m = M();
    if (gT.fraction <= p1f) return; // already hit something nearer
    if (num < 0) {
        forLeafBrushes(-1 - num, clipBoxToBrush);
        return;
    }
    const Node &node = m.nodes[size_t(num)];
    const Plane &plane = m.planes[size_t(node.plane)];
    float t1, t2, offset;
    if (plane.type < 3) {
        t1 = p1[plane.type] - plane.dist;
        t2 = p2[plane.type] - plane.dist;
        offset = gT.extents[plane.type];
    } else {
        t1 = dot(plane.normal, p1) - plane.dist;
        t2 = dot(plane.normal, p2) - plane.dist;
        offset = gT.isPoint ? 0
                            : std::fabs(gT.extents[0] * plane.normal[0]) + std::fabs(gT.extents[1] * plane.normal[1]) +
                                  std::fabs(gT.extents[2] * plane.normal[2]);
    }
    if (t1 >= offset && t2 >= offset) {
        recursiveHullCheck(node.children[0], p1f, p2f, p1, p2);
        return;
    }
    if (t1 < -offset && t2 < -offset) {
        recursiveHullCheck(node.children[1], p1f, p2f, p1, p2);
        return;
    }
    // put the crosspoint DIST_EPSILON pixels on the near side
    int side;
    float frac, frac2;
    if (t1 < t2) {
        const float idist = 1.0f / (t1 - t2);
        side = 1;
        frac2 = (t1 + offset + kDistEpsilon) * idist;
        frac = (t1 - offset + kDistEpsilon) * idist;
    } else if (t1 > t2) {
        const float idist = 1.0f / (t1 - t2);
        side = 0;
        frac2 = (t1 - offset - kDistEpsilon) * idist;
        frac = (t1 + offset + kDistEpsilon) * idist;
    } else {
        side = 0;
        frac = 1;
        frac2 = 0;
    }
    // move up to the node
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float midf = p1f + (p2f - p1f) * frac;
    float mid[3];
    for (int i = 0; i < 3; ++i) mid[i] = p1[i] + frac * (p2[i] - p1[i]);
    recursiveHullCheck(node.children[side], p1f, midf, p1, mid);
    // go past the node
    if (frac2 < 0) frac2 = 0;
    if (frac2 > 1) frac2 = 1;
    midf = p1f + (p2f - p1f) * frac2;
    for (int i = 0; i < 3; ++i) mid[i] = p1[i] + frac2 * (p2[i] - p1[i]);
    recursiveHullCheck(node.children[side ^ 1], midf, p2f, mid, p2);
}

// CM_BoxTrace in the model's local space. Returns the end position.
void boxTrace(const float *start, const float *end, const float *mins, const float *maxs, int32_t headnode,
              int32_t brushmask, float *endpos) {
    MapState &m = M();
    ++m.checkcount; // for multi-check avoidance
    gT = TraceWork{};
    gT.contents = brushmask;
    for (int i = 0; i < 3; ++i) {
        gT.start[i] = start[i];
        gT.end[i] = end[i];
        gT.mins[i] = mins[i];
        gT.maxs[i] = maxs[i];
    }
    // check for position test special case
    if (start[0] == end[0] && start[1] == end[1] && start[2] == end[2]) {
        int32_t leafs[kMaxBoxLeafs];
        BoxLeafs q;
        q.list = leafs;
        q.max = kMaxBoxLeafs;
        float c1[3], c2[3];
        for (int i = 0; i < 3; ++i) {
            c1[i] = start[i] + mins[i] - 1;
            c2[i] = start[i] + maxs[i] + 1;
        }
        q.mins = c1;
        q.maxs = c2;
        boxLeafnums(q, headnode);
        for (int32_t i = 0; i < q.count; ++i) {
            forLeafBrushes(q.list[i], testBoxInBrush);
            if (gT.allsolid) break;
        }
        for (int i = 0; i < 3; ++i) endpos[i] = start[i];
        return;
    }
    // check for point special case
    if (mins[0] == 0 && mins[1] == 0 && mins[2] == 0 && maxs[0] == 0 && maxs[1] == 0 && maxs[2] == 0) {
        gT.isPoint = true;
        gT.extents[0] = gT.extents[1] = gT.extents[2] = 0;
    } else {
        gT.isPoint = false;
        for (int i = 0; i < 3; ++i) gT.extents[i] = -mins[i] > maxs[i] ? -mins[i] : maxs[i];
    }
    recursiveHullCheck(headnode, 0, 1, start, end);
    if (gT.fraction == 1) {
        for (int i = 0; i < 3; ++i) endpos[i] = end[i];
    } else {
        for (int i = 0; i < 3; ++i) endpos[i] = start[i] + gT.fraction * (end[i] - start[i]);
    }
}

} // namespace

// ------------------------------------------------------------------ API ---

uint32_t crc32(const unsigned char *data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool cmLoadMap(const std::string &name, const unsigned char *data, size_t size, std::string &error) {
    cmUnloadMap();
    MapState m;
    if (!loadInto(m, data, size, error)) return false;
    m.loaded = true;
    m.name = name;
    m.checksum = crc32(data, size);
    M() = std::move(m);
    return true;
}

void cmUnloadMap() { M() = MapState{}; }

bool cmLoaded() { return M().loaded; }

int32_t cmMapInfo(RealmsMapInfo *out) {
    if (!out || out->struct_size < sizeof(RealmsMapInfo)) return 0;
    const uint32_t structSize = out->struct_size;
    std::memset(out, 0, sizeof(RealmsMapInfo));
    out->struct_size = structSize;
    const MapState &m = M();
    if (!m.loaded) return 0;
    out->checksum = m.checksum;
    out->num_planes = m.numPlanes;
    out->num_nodes = m.numNodes;
    out->num_leafs = m.numLeafs;
    out->num_leaf_brushes = m.numLeafBrushes;
    out->num_brushes = m.numBrushes;
    out->num_brush_sides = m.numSides;
    out->num_texinfo = int32_t(m.surfaces.size());
    out->num_models = int32_t(m.models.size());
    out->num_clusters = m.numClusters;
    out->num_areas = m.numAreas;
    out->num_area_portals = m.numAreaPortals;
    out->entity_length = int32_t(m.entities.size());
    for (int j = 0; j < 3; ++j) {
        out->mins[j] = m.models[0].mins[j];
        out->maxs[j] = m.models[0].maxs[j];
    }
    const size_t n = std::min(m.name.size(), sizeof(out->name) - 1);
    std::memcpy(out->name, m.name.data(), n);
    out->name[n] = '\0';
    return 1;
}

int32_t cmEntities(char *buffer, int32_t bufferSize) {
    if (buffer && bufferSize > 0) buffer[0] = '\0';
    const MapState &m = M();
    if (!m.loaded) return -1;
    if (buffer && bufferSize > 0) {
        const size_t n = std::min(m.entities.size(), size_t(bufferSize - 1));
        std::memcpy(buffer, m.entities.data(), n);
        buffer[n] = '\0';
    }
    return int32_t(m.entities.size());
}

int32_t cmModel(int32_t index, float *mins, float *maxs, float *origin, int32_t *headnode) {
    const MapState &m = M();
    if (!m.loaded || index < 0 || index >= int32_t(m.models.size())) return 0;
    const Model &md = m.models[size_t(index)];
    for (int j = 0; j < 3; ++j) {
        if (mins) mins[j] = md.mins[j];
        if (maxs) maxs[j] = md.maxs[j];
        if (origin) origin[j] = md.origin[j];
    }
    if (headnode) *headnode = md.headnode;
    return 1;
}

int32_t cmHeadnodeForBox(const float *mins, const float *maxs) {
    MapState &m = M();
    if (!m.loaded || !mins || !maxs) return 0;
    Plane *p = &m.planes[size_t(m.boxPlanes)];
    for (int a = 0; a < 3; ++a) {
        p[a * 4 + 0].dist = maxs[a];
        p[a * 4 + 1].dist = -maxs[a];
        p[a * 4 + 2].dist = mins[a];
        p[a * 4 + 3].dist = -mins[a];
    }
    return m.boxHeadnode;
}

int32_t cmPointContents(const float *p, int32_t headnode, const float *origin, const float *angles) {
    if (!p || !validHeadnode(headnode)) return 0;
    const MapState &m = M();
    float local[3] = {p[0], p[1], p[2]};
    if (origin)
        for (int i = 0; i < 3; ++i) local[i] -= origin[i];
    if (headnode != m.boxHeadnode && anyNonZero(angles)) {
        float f[3], r[3], u[3];
        angleVectors(angles, f, r, u);
        rotateInto(local, f, r, u);
    }
    return leafPointContents(pointLeafnum(local, headnode), local);
}

void cmBoxTrace(const float *start, const float *end, const float *mins, const float *maxs, int32_t headnode,
                int32_t brushmask, const float *origin, const float *angles, RealmsTrace *out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->fraction = 1;
    if (end)
        for (int i = 0; i < 3; ++i) out->endpos[i] = end[i];
    if (!start || !end || !validHeadnode(headnode)) return;
    const MapState &m = M();
    static const float zero[3] = {0, 0, 0};
    if (!mins) mins = zero;
    if (!maxs) maxs = zero;

    const bool transformed = origin != nullptr || angles != nullptr;
    const bool rotated = headnode != m.boxHeadnode && anyNonZero(angles);
    float s[3] = {start[0], start[1], start[2]}, e[3] = {end[0], end[1], end[2]};
    if (origin) {
        for (int i = 0; i < 3; ++i) {
            s[i] -= origin[i];
            e[i] -= origin[i];
        }
    }
    float f[3], r[3], u[3];
    if (rotated) {
        angleVectors(angles, f, r, u);
        rotateInto(s, f, r, u);
        rotateInto(e, f, r, u);
    }
    float endpos[3];
    boxTrace(s, e, mins, maxs, headnode, brushmask, endpos);

    out->allsolid = gT.allsolid ? 1 : 0;
    out->startsolid = gT.startsolid ? 1 : 0;
    out->fraction = gT.fraction;
    out->contents = gT.hitContents;
    if (gT.plane >= 0) {
        const Plane &pl = m.planes[size_t(gT.plane)];
        for (int i = 0; i < 3; ++i) out->plane_normal[i] = pl.normal[i];
        out->plane_dist = pl.dist;
    }
    if (gT.surface >= 0) {
        const Surface &sf = m.surfaces[size_t(gT.surface)];
        std::memcpy(out->surface_name, sf.name, sizeof(sf.name));
        out->surface_flags = sf.flags;
        out->surface_value = sf.value;
    }
    if (rotated && gT.fraction != 1) {
        const float neg[3] = {-angles[0], -angles[1], -angles[2]};
        angleVectors(neg, f, r, u);
        rotateInto(out->plane_normal, f, r, u);
    }
    if (transformed) {
        for (int i = 0; i < 3; ++i) out->endpos[i] = start[i] + gT.fraction * (end[i] - start[i]);
    } else {
        for (int i = 0; i < 3; ++i) out->endpos[i] = endpos[i];
    }
}

bool cmLoadMesh(const RealmsMeshDesc *desc, std::string &error) {
    cmUnloadMap();
    if (!desc || desc->struct_size < sizeof(RealmsMeshDesc)) {
        error = "bad mesh description";
        return false;
    }
    MapState m;
    if (!loadMeshInto(m, *desc, error)) return false;
    m.loaded = true;
    m.name = desc->name ? desc->name : "";
    m.checksum = desc->checksum;
    M() = std::move(m);
    return true;
}

namespace {
int32_t copyRow(const std::vector<uint8_t> &row, uint8_t *buffer, int32_t size) {
    if (buffer && size > 0) std::memcpy(buffer, row.data(), std::min(row.size(), size_t(size)));
    return int32_t(row.size());
}
} // namespace

int32_t cmClusterPVS(int32_t cluster, uint8_t *buffer, int32_t size) {
    const MapState &m = M();
    if (!m.loaded) return 0;
    std::vector<uint8_t> row;
    decompressVis(m, cluster, 0, row);
    return copyRow(row, buffer, size);
}

int32_t cmClusterPHS(int32_t cluster, uint8_t *buffer, int32_t size) {
    const MapState &m = M();
    if (!m.loaded) return 0;
    std::vector<uint8_t> row;
    decompressVis(m, cluster, 1, row);
    return copyRow(row, buffer, size);
}

void cmSetAreaPortal(int32_t portal, int32_t open) {
    MapState &m = M();
    if (!m.loaded || portal < 0 || portal >= kMaxAreaPortals) return;
    m.portalOpen[size_t(portal)] = open ? 1 : 0;
    floodAreaConnections(m);
}

int32_t cmAreaPortalOpen(int32_t portal) {
    const MapState &m = M();
    if (!m.loaded || portal < 0 || portal >= kMaxAreaPortals) return -1;
    return m.portalOpen[size_t(portal)];
}

int32_t cmAreasConnected(int32_t area1, int32_t area2) {
    const MapState &m = M();
    if (!m.loaded || area1 < 0 || area2 < 0 || area1 >= m.numAreas || area2 >= m.numAreas) return 0;
    return m.areas[size_t(area1)].floodnum == m.areas[size_t(area2)].floodnum ? 1 : 0;
}

int32_t cmWriteAreaBits(int32_t area, uint8_t *buffer, int32_t size) {
    const MapState &m = M();
    if (!m.loaded) return 0;
    std::vector<uint8_t> row(size_t((m.numAreas + 7) >> 3), 0);
    const bool all = area <= 0 || area >= m.numAreas;
    const int32_t floodnum = all ? 0 : m.areas[size_t(area)].floodnum;
    for (int32_t i = 0; i < m.numAreas; ++i)
        if (all || m.areas[size_t(i)].floodnum == floodnum) row[size_t(i >> 3)] |= uint8_t(1 << (i & 7));
    return copyRow(row, buffer, size);
}

int32_t cmBoxLeafs(const float *mins, const float *maxs, int32_t *list, int32_t listSize, int32_t headnode,
                   int32_t *topnode) {
    if (topnode) *topnode = -1;
    if (!mins || !maxs || !validHeadnode(headnode)) return 0;
    BoxLeafs q;
    q.mins = mins;
    q.maxs = maxs;
    q.list = list;
    q.max = list ? std::max(listSize, 0) : 0;
    boxLeafnums(q, headnode);
    if (topnode) *topnode = q.topnode;
    return q.count;
}

int32_t cmHeadnodeVisible(int32_t headnode, const uint8_t *visbits, int32_t size) {
    if (!visbits || size <= 0 || !validHeadnode(headnode)) return 0;
    return headnodeVisible(headnode, visbits, size) ? 1 : 0;
}

int32_t cmPointLeaf(const float *p) {
    const MapState &m = M();
    if (!m.loaded || !p) return 0;
    return pointLeafnum(p, m.models[0].headnode);
}

int32_t cmLeafInfo(int32_t leaf, int32_t *contents, int32_t *cluster, int32_t *area) {
    const MapState &m = M();
    if (!m.loaded || leaf < 0 || leaf >= m.numLeafs) return 0;
    const Leaf &l = m.leafs[size_t(leaf)];
    if (contents) *contents = l.contents;
    if (cluster) *cluster = l.cluster;
    if (area) *area = l.area;
    return 1;
}

} // namespace realms_native
