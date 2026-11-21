// Realms bridge conformance: visibility (PVS/PHS), area portals, BoxLeafnums,
// HeadnodeVisible and triangle mesh maps. Included by realms_map_suite.h;
// runs identically against the Free Pascal and C++ cores.
#pragma once

#include "bsp_builder.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vis_suite {

// Two rooms (x < -8 and x > 8) split by a wall at x = 0 with a doorway that
// holds an areaportal brush. Areas: 0 solid, 1 left + doorway, 2 right.
inline int sideOf(const float *mins, const float *maxs) {
    const float cx = (mins[0] + maxs[0]) * 0.5f;
    return cx < -8 ? 0 : cx > 8 ? 2 : 1;
}

inline bsp_builder::MapDef visMap() {
    bsp_builder::MapDef def;
    def.texinfo = {{"e1u1/wall", 0, 0}, {"e1u1/trigger", REALMS_SURF_NODRAW, 0}};
    def.world.brushes = {
        {{-512, -512, -64}, {512, 512, 0}, REALMS_CONTENTS_SOLID, 0},
        {{-8, -512, 0}, {8, -32, 4096}, REALMS_CONTENTS_SOLID, 0},
        {{-8, 32, 0}, {8, 512, 4096}, REALMS_CONTENTS_SOLID, 0},
        {{-8, -32, 0}, {8, 32, 4096}, REALMS_CONTENTS_AREAPORTAL, 1},
    };
    def.numAreas = 3;
    def.areaOf = [](const float *mins, const float *maxs) { return sideOf(mins, maxs) == 2 ? 2 : 1; };
    def.portals = {{1, 1, 2}};
    def.pvs = [](const float *amin, const float *amax, const float *bmin, const float *bmax) {
        const int a = sideOf(amin, amax), b = sideOf(bmin, bmax);
        return a == b || a == 1 || b == 1;
    };
    def.entities = "{\n\"classname\" \"worldspawn\"\n}\n";
    return def;
}

inline bool bit(const std::vector<uint8_t> &row, int32_t i) {
    return i >= 0 && size_t(i >> 3) < row.size() && (row[size_t(i >> 3)] & (1 << (i & 7))) != 0;
}

inline std::vector<uint8_t> pvs(const RealmsCoreApi *api, int32_t cluster, bool phs = false) {
    std::vector<uint8_t> row(64, 0xcd);
    const int32_t n = phs ? api->map_cluster_phs(cluster, row.data(), int32_t(row.size()))
                          : api->map_cluster_pvs(cluster, row.data(), int32_t(row.size()));
    row.resize(size_t(n > 0 ? n : 0));
    return row;
}

struct LeafAt {
    int32_t leaf = -1, contents = -1, cluster = -9, area = -9;
};
inline LeafAt leafAt(const RealmsCoreApi *api, float x, float y, float z) {
    LeafAt r;
    const float p[3] = {x, y, z};
    r.leaf = api->map_point_leaf(p);
    api->map_leaf_info(r.leaf, &r.contents, &r.cluster, &r.area);
    return r;
}

} // namespace vis_suite

namespace mesh_suite {

struct Mesh {
    std::vector<float> vertices;
    std::vector<int32_t> indices;
    std::vector<int32_t> triSurfaces;
    std::vector<RealmsMeshSurface> surfaces;

    int32_t surface(const char *name, int32_t contents, int32_t flags = 0, int32_t value = 0) {
        RealmsMeshSurface s{};
        std::strncpy(s.name, name, sizeof(s.name) - 1);
        s.contents = contents;
        s.flags = flags;
        s.value = value;
        surfaces.push_back(s);
        return int32_t(surfaces.size()) - 1;
    }
    int32_t vertex(float x, float y, float z) {
        vertices.insert(vertices.end(), {x, y, z});
        return int32_t(vertices.size() / 3) - 1;
    }
    void tri(int32_t a, int32_t b, int32_t c, int32_t surf) {
        indices.insert(indices.end(), {a, b, c});
        triSurfaces.push_back(surf);
    }
    // Quad p0..p3 (a convex loop), wound so its front faces `facing`.
    void quad(const float (&p)[4][3], const float (&facing)[3], int32_t surf) {
        const float e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const float e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        int32_t v[4];
        for (int i = 0; i < 4; ++i) v[i] = vertex(p[i][0], p[i][1], p[i][2]);
        if (n[0] * facing[0] + n[1] * facing[1] + n[2] * facing[2] >= 0) {
            tri(v[0], v[1], v[2], surf);
            tri(v[0], v[2], v[3], surf);
        } else {
            tri(v[0], v[2], v[1], surf);
            tri(v[0], v[3], v[2], surf);
        }
    }
    RealmsMeshDesc desc(const char *name, uint32_t checksum = 0, const char *entities = nullptr) const {
        RealmsMeshDesc d{};
        d.struct_size = sizeof(d);
        d.checksum = checksum;
        d.name = name;
        d.vertices = vertices.data();
        d.num_vertices = int32_t(vertices.size() / 3);
        d.indices = indices.data();
        d.num_triangles = int32_t(indices.size() / 3);
        d.triangle_surfaces = triSurfaces.data();
        d.surfaces = surfaces.data();
        d.num_surfaces = int32_t(surfaces.size());
        d.thickness = 2;
        d.entities = entities;
        return d;
    }
};

// A 512 x 512 x 256 room (inward faces), a 45 degree ramp, a player clip
// panel, a non-colliding panel and one degenerate triangle.
inline Mesh room() {
    Mesh m;
    const int32_t floor = m.surface("floor", REALMS_CONTENTS_SOLID);
    const int32_t wall = m.surface("wall", REALMS_CONTENTS_SOLID, 0, 3);
    const int32_t ramp = m.surface("ramp", REALMS_CONTENTS_SOLID);
    const int32_t clip = m.surface("clip", REALMS_CONTENTS_PLAYERCLIP, REALMS_SURF_NODRAW);
    const int32_t ghost = m.surface("ghost", 0);
    const float L = -256, H = 256, T = 256;
    m.quad({{L, L, 0}, {H, L, 0}, {H, H, 0}, {L, H, 0}}, {0, 0, 1}, floor);
    m.quad({{L, L, T}, {H, L, T}, {H, H, T}, {L, H, T}}, {0, 0, -1}, wall);
    m.quad({{H, L, 0}, {H, H, 0}, {H, H, T}, {H, L, T}}, {-1, 0, 0}, wall);
    m.quad({{L, L, 0}, {L, H, 0}, {L, H, T}, {L, L, T}}, {1, 0, 0}, wall);
    m.quad({{L, H, 0}, {H, H, 0}, {H, H, T}, {L, H, T}}, {0, -1, 0}, wall);
    m.quad({{L, L, 0}, {H, L, 0}, {H, L, T}, {L, L, T}}, {0, 1, 0}, wall);
    m.quad({{-200, -64, 0}, {-72, -64, 128}, {-72, 64, 128}, {-200, 64, 0}}, {-1, 0, 1}, ramp);
    m.quad({{-64, 128, 0}, {64, 128, 0}, {64, 128, 128}, {-64, 128, 128}}, {0, -1, 0}, clip);
    m.quad({{64, -32, 0}, {64, 32, 0}, {64, 32, 128}, {64, -32, 128}}, {-1, 0, 0}, ghost);
    const int32_t a = m.vertex(0, 0, 50), b = m.vertex(10, 0, 50), c = m.vertex(20, 0, 50);
    m.tri(a, b, c, floor); // degenerate: skipped
    return m;
}

} // namespace mesh_suite

inline void runVisChecks(const RealmsCoreApi *api) {
    using namespace vis_suite;
    bsp_builder::Stats stats;
    const std::string bsp = bsp_builder::build(visMap(), &stats);
    gHost.files["maps/vis.bsp"] = bsp;
    CHECK(api->map_load("maps/vis.bsp") == 1);
    RealmsMapInfo info{};
    info.struct_size = sizeof(info);
    CHECK(api->map_info(&info) == 1);
    CHECK(info.num_clusters == stats.clusters && stats.clusters > 8);
    CHECK(info.num_areas == 3 && info.num_area_portals == 2);
    const int32_t rowBytes = (info.num_clusters + 7) / 8;

    const LeafAt left = leafAt(api, -100, 0, 64), right = leafAt(api, 100, 0, 64), door = leafAt(api, 0, 0, 64);
    CHECK(left.contents == 0 && left.area == 1 && left.cluster >= 0);
    CHECK(right.contents == 0 && right.area == 2 && right.cluster >= 0);
    CHECK(door.contents == REALMS_CONTENTS_AREAPORTAL && door.area == 1 && door.cluster >= 0);

    // PVS rows: each room sees itself and the doorway, not the other room.
    const std::vector<uint8_t> pl = pvs(api, left.cluster), pr = pvs(api, right.cluster), pd = pvs(api, door.cluster);
    CHECK(int32_t(pl.size()) == rowBytes);
    CHECK(bit(pl, left.cluster) && bit(pl, door.cluster) && !bit(pl, right.cluster));
    CHECK(bit(pr, right.cluster) && bit(pr, door.cluster) && !bit(pr, left.cluster));
    CHECK(bit(pd, left.cluster) && bit(pd, right.cluster));
    for (int32_t c = 0; c < info.num_clusters; ++c) {
        const auto &b = stats.clusterBounds[size_t(c)];
        const int side = sideOf(&b[0], &b[3]);
        CHECK(bit(pl, c) == (side != 2));
        CHECK(bit(pr, c) == (side != 0));
    }
    // PHS rows are all ones in this map; cluster -1 sees nothing.
    const std::vector<uint8_t> hl = pvs(api, left.cluster, true);
    CHECK(int32_t(hl.size()) == rowBytes && bit(hl, right.cluster));
    for (uint8_t b : pvs(api, -1)) CHECK(b == 0);
    for (uint8_t b : pvs(api, info.num_clusters)) CHECK(b == 0);
    CHECK(api->map_cluster_pvs(left.cluster, nullptr, 0) == rowBytes);
    {
        uint8_t one = 0xcd;
        CHECK(api->map_cluster_pvs(left.cluster, &one, 1) == rowBytes); // truncated copy
        CHECK(one == pl[0]);
    }

    // Area portals start closed; opening portal 1 joins areas 1 and 2.
    uint8_t bits[4] = {0, 0, 0, 0};
    CHECK(api->map_area_portal_open(1) == 0);
    CHECK(api->map_area_portal_open(-1) == -1 && api->map_area_portal_open(1024) == -1);
    CHECK(api->map_areas_connected(1, 1) == 1);
    CHECK(api->map_areas_connected(1, 2) == 0);
    CHECK(api->map_areas_connected(1, 3) == 0 && api->map_areas_connected(-1, 1) == 0);
    CHECK(api->map_write_area_bits(1, bits, 4) == 1 && bits[0] == 0x02);
    CHECK(api->map_write_area_bits(2, bits, 4) == 1 && bits[0] == 0x04);
    CHECK(api->map_write_area_bits(0, bits, 4) == 1 && bits[0] == 0x07); // area 0: everything
    api->map_set_area_portal(1, 1);
    CHECK(api->map_area_portal_open(1) == 1);
    CHECK(api->map_areas_connected(1, 2) == 1 && api->map_areas_connected(2, 1) == 1);
    CHECK(api->map_write_area_bits(1, bits, 4) == 1 && bits[0] == 0x06);
    api->map_set_area_portal(7, 1); // a portal no area uses changes nothing
    api->map_set_area_portal(1, 0);
    CHECK(api->map_areas_connected(1, 2) == 0);
    api->map_set_area_portal(5000, 1); // ignored
    CHECK(api->map_areas_connected(1, 2) == 0);

    // BoxLeafnums: a small box inside one leaf, a box across the doorway.
    {
        int32_t list[64], top = 77;
        const float smin[3] = {-101, -1, 63}, smax[3] = {-99, 1, 65};
        CHECK(api->map_box_leafs(smin, smax, list, 64, 0, &top) == 1);
        CHECK(list[0] == left.leaf && top == -1);
        const float dmin[3] = {-100, -20, 10}, dmax[3] = {100, 20, 100};
        const int32_t n = api->map_box_leafs(dmin, dmax, list, 64, 0, &top);
        CHECK(n >= 3 && top >= 0);
        bool sawLeft = false, sawRight = false, sawDoor = false;
        for (int32_t i = 0; i < n; ++i) {
            sawLeft |= list[i] == left.leaf;
            sawRight |= list[i] == right.leaf;
            sawDoor |= list[i] == door.leaf;
        }
        CHECK(sawLeft && sawRight && sawDoor);
        CHECK(api->map_box_leafs(dmin, dmax, list, 1, 0, nullptr) == 1); // capped like leaf_maxcount
        CHECK(api->map_box_leafs(dmin, dmax, nullptr, 64, 0, &top) == 0);
        CHECK(api->map_box_leafs(dmin, dmax, list, 64, 999999, &top) == 0 && top == -1);

        // HeadnodeVisible (SV_LinkEdict headnode + PVS culling).
        const float rmin[3] = {50, -100, -10}, rmax[3] = {300, 100, 100}; // straddles the floor
        int32_t rightTop = -1;
        CHECK(api->map_box_leafs(rmin, rmax, list, 64, 0, &rightTop) > 1 && rightTop >= 0);
        CHECK(api->map_headnode_visible(rightTop, pr.data(), int32_t(pr.size())) == 1);
        CHECK(api->map_headnode_visible(rightTop, pl.data(), int32_t(pl.size())) == 0);
        CHECK(api->map_headnode_visible(-1 - right.leaf, pr.data(), int32_t(pr.size())) == 1);
        CHECK(api->map_headnode_visible(-1 - right.leaf, pl.data(), int32_t(pl.size())) == 0);
        CHECK(api->map_headnode_visible(-1, pr.data(), int32_t(pr.size())) == 0); // leaf 0: cluster -1
        CHECK(api->map_headnode_visible(0, nullptr, 4) == 0);
        CHECK(api->map_headnode_visible(rightTop, pr.data(), 0) == 0);
    }

    // Visibility lump validation and bounded decompression.
    const size_t visOfs = size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kVisibility));
    const int32_t visLen = bsp_builder::lumpLength(bsp, bsp_builder::kVisibility);
    auto rejectsVis = [&](const char *name, const std::string &data) {
        gHost.files[name] = data;
        CHECK(api->map_load(name) == 0);
        CHECK(map_suite::noMap(api));
    };
    {
        std::string b = bsp;
        bsp_builder::poke32(b, visOfs + 4, visLen + 1);
        rejectsVis("maps/visofs.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, visOfs, 1); // fewer vis clusters than leafs use
        rejectsVis("maps/visclusters.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, visOfs, -5);
        rejectsVis("maps/visneg.bsp", b);
    }
    {
        std::string b = bsp; // the left cluster's row starts at the end of the lump
        bsp_builder::poke32(b, visOfs + 4 + size_t(left.cluster) * 8, visLen);
        gHost.files["maps/vistrunc.bsp"] = b;
        CHECK(api->map_load("maps/vistrunc.bsp") == 1);
        for (uint8_t v : pvs(api, left.cluster)) CHECK(v == 0);
        CHECK(bit(pvs(api, right.cluster), right.cluster));
    }
    {
        // Corrupted rows must decode without reading outside the lump.
        uint32_t seed = 0xabcdef01u;
        auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        for (int iter = 0; iter < 200; ++iter) {
            std::string b = bsp;
            for (int k = 0; k < 4; ++k) b[visOfs + size_t(rnd() % uint32_t(visLen))] = char(rnd() & 0xff);
            gHost.files["maps/visfuzz.bsp"] = b;
            if (api->map_load("maps/visfuzz.bsp") != 1) continue;
            RealmsMapInfo fi{};
            fi.struct_size = sizeof(fi);
            api->map_info(&fi);
            for (int32_t c = -1; c <= fi.num_clusters; ++c) {
                CHECK(int32_t(pvs(api, c).size()) == (fi.num_clusters + 7) / 8);
                pvs(api, c, true);
            }
        }
        gHost.files.erase("maps/visfuzz.bsp");
    }

    // Maps without a visibility lump see everything.
    CHECK(api->map_load("maps/test.bsp") == 1);
    for (uint8_t b : pvs(api, 0)) CHECK(b == 0xff);
    api->map_load("");
    CHECK(api->map_cluster_pvs(0, bits, 4) == 0 && api->map_write_area_bits(1, bits, 4) == 0);
    CHECK(api->map_areas_connected(0, 0) == 0);
}

inline void runMeshChecks(const RealmsCoreApi *api) {
    using namespace mesh_suite;
    using map_suite::contents;
    using map_suite::ptrace;
    using map_suite::trace;
    using map_suite::vec;
    const map_suite::V3 zero = vec(0, 0, 0), pmin = vec(-16, -16, -24), pmax = vec(16, 16, 32);

    const Mesh m = room();
    const char *ents = "{\n\"classname\" \"worldspawn\"\n}\n";
    RealmsMeshDesc d = m.desc("maps/room.glb", 0x1234abcdu, ents);
    gHost.lines.clear();
    CHECK(api->map_load_mesh(&d) == 1);
    RealmsMapInfo info{};
    info.struct_size = sizeof(info);
    CHECK(api->map_info(&info) == 1);
    CHECK(std::string(info.name) == "maps/room.glb" && info.checksum == 0x1234abcdu);
    CHECK(info.num_models == 1 && info.num_texinfo == 5 && info.num_clusters == 1 && info.num_areas == 2);
    CHECK(info.num_brushes == 16); // 16 colliding triangles (ghost panel and degenerate one skipped)
    CHECK(info.entity_length == int32_t(std::strlen(ents)));
    CHECK(info.mins[0] == -259.0f && info.maxs[2] == 259.0f); // slabs are 2 units thick
    {
        char buf[64];
        CHECK(api->map_entities(buf, 64) == info.entity_length && std::string(buf) == ents);
    }

    // Exact Quake 2 trace arithmetic against triangle slabs.
    {
        const RealmsTrace t = ptrace(api, vec(0, 0, 128), vec(0, 0, -128), REALMS_MASK_SOLID);
        CHECK(t.fraction == 4095.0f / 8192.0f && t.endpos[2] == 0.03125f && t.plane_normal[2] == 1.0f);
        CHECK(std::string(t.surface_name) == "floor" && t.contents == REALMS_CONTENTS_SOLID);
        const RealmsTrace h = trace(api, vec(0, 0, 128), vec(0, 0, -128), pmin, pmax, 0, REALMS_MASK_PLAYERSOLID);
        CHECK(h.endpos[2] == 24.03125f && !h.startsolid);
        const RealmsTrace w = ptrace(api, vec(0, 0, 64), vec(512, 0, 64), REALMS_MASK_SOLID);
        CHECK(w.endpos[0] == 255.96875f && w.plane_normal[0] == -1.0f && w.plane_dist == -256.0f);
        CHECK(std::string(w.surface_name) == "wall" && w.surface_value == 3);
        const RealmsTrace wb = trace(api, vec(0, 0, 64), vec(512, 0, 64), pmin, pmax, 0, REALMS_MASK_SOLID);
        CHECK(wb.endpos[0] == 239.96875f);
    }
    // Slanted ramp: normal (-0.7071, 0, 0.7071), surface 64 units up mid-way.
    {
        const RealmsTrace t = ptrace(api, vec(-136, 0, 200), vec(-136, 0, 0), REALMS_MASK_SOLID);
        CHECK(t.fraction < 1 && map_suite::near(t.endpos[2], 64.0442f, 0.01f));
        CHECK(map_suite::near(t.plane_normal[0], -0.70710678f) && map_suite::near(t.plane_normal[2], 0.70710678f));
        CHECK(std::string(t.surface_name) == "ramp");
    }
    // Contents masks: clip only stops players; the ghost panel never collides.
    {
        const RealmsTrace s = ptrace(api, vec(0, 0, 64), vec(0, 256, 64), REALMS_MASK_SOLID);
        CHECK(s.endpos[1] == 255.96875f);
        const RealmsTrace p = ptrace(api, vec(0, 0, 64), vec(0, 256, 64), REALMS_MASK_PLAYERSOLID);
        CHECK(p.endpos[1] == 127.96875f && p.contents == REALMS_CONTENTS_PLAYERCLIP);
        CHECK(std::string(p.surface_name) == "clip" && p.surface_flags == REALMS_SURF_NODRAW);
        CHECK(ptrace(api, vec(0, 0, 64), vec(200, 0, 64), REALMS_MASK_ALL).fraction == 1.0f);
    }
    // Point contents test the slabs themselves, not the kd-tree cells.
    CHECK(contents(api, vec(0, 0, -1)) == REALMS_CONTENTS_SOLID);
    CHECK(contents(api, vec(0, 0, 1)) == 0);
    CHECK(contents(api, vec(0, 0, -3)) == 0);
    CHECK(contents(api, vec(0, 129, 64)) == REALMS_CONTENTS_PLAYERCLIP);
    CHECK(contents(api, vec(0, 120, 64)) == 0);
    CHECK(contents(api, vec(5000, 0, 0)) == 0);
    // Position tests and starting inside a slab.
    CHECK(trace(api, vec(0, 0, 20), vec(0, 0, 20), pmin, pmax, 0, REALMS_MASK_SOLID).allsolid == 1);
    CHECK(trace(api, vec(0, 0, 30), vec(0, 0, 30), pmin, pmax, 0, REALMS_MASK_SOLID).fraction == 1.0f);
    CHECK(ptrace(api, vec(0, 0, -1), vec(0, 0, 50), REALMS_MASK_SOLID).startsolid == 1);
    // Box hulls, leafs and visibility on mesh maps.
    {
        const float bmin[3] = {-8, -8, -8}, bmax[3] = {8, 8, 8};
        const int32_t box = api->map_headnode_for_box(bmin, bmax);
        CHECK(contents(api, vec(0, 0, 0), box) == REALMS_CONTENTS_MONSTER);
        CHECK(ptrace(api, vec(-100, 0, 0), vec(100, 0, 0), REALMS_MASK_ALL, box).contents == REALMS_CONTENTS_MONSTER);
        int32_t c = -1, cl = -1, ar = -1;
        const float p[3] = {0, 0, 64};
        const int32_t leaf = api->map_point_leaf(p);
        CHECK(leaf >= 2 && leaf < info.num_leafs && api->map_leaf_info(leaf, &c, &cl, &ar) == 1 && cl == 0 && ar == 1);
        uint8_t row[4] = {0, 0, 0, 0};
        CHECK(api->map_cluster_pvs(0, row, 4) == 1 && row[0] == 0xff);
        CHECK(api->map_write_area_bits(1, row, 4) == 1 && row[0] == 0x02);
        CHECK(api->map_areas_connected(1, 1) == 1);
        int32_t list[256], top = 0;
        const float wmin[3] = {-300, -300, -10}, wmax[3] = {300, 300, 300};
        CHECK(api->map_box_leafs(wmin, wmax, list, 256, 0, &top) > 1 && top == 0);
    }
    // Moving through the room with origin/angles works like inline models.
    {
        const float moved[3] = {0, 0, 100};
        const RealmsTrace t = trace(api, vec(0, 0, 228), vec(0, 0, -28), zero, zero, 0, REALMS_MASK_SOLID, moved);
        CHECK(t.fraction == 4095.0f / 8192.0f && map_suite::near(t.endpos[2], 100.03125f));
    }

    // Invalid descriptions are rejected and leave no map loaded.
    auto rejects = [&](RealmsMeshDesc bad) {
        CHECK(api->map_load_mesh(&d) == 1);
        gHost.lines.clear();
        CHECK(api->map_load_mesh(&bad) == 0);
        CHECK(map_suite::noMap(api));
        CHECK(sawLine("map_load_mesh"));
    };
    {
        RealmsMeshDesc b = d;
        b.struct_size = 8;
        rejects(b);
    }
    {
        std::vector<int32_t> idx = m.indices;
        idx[4] = int32_t(m.vertices.size() / 3);
        RealmsMeshDesc b = d;
        b.indices = idx.data();
        rejects(b);
        idx[4] = -1;
        rejects(b);
    }
    {
        std::vector<float> v = m.vertices;
        v[7] = std::nanf("");
        RealmsMeshDesc b = d;
        b.vertices = v.data();
        rejects(b);
        v[7] = INFINITY;
        rejects(b);
    }
    {
        std::vector<int32_t> ts = m.triSurfaces;
        ts[3] = 99;
        RealmsMeshDesc b = d;
        b.triangle_surfaces = ts.data();
        rejects(b);
    }
    {
        RealmsMeshDesc b = d;
        b.num_surfaces = 0;
        rejects(b);
        b = d;
        b.surfaces = nullptr;
        rejects(b);
        b = d;
        b.num_triangles = -1;
        rejects(b);
        b = d;
        b.indices = nullptr;
        rejects(b);
    }
    CHECK(api->map_load_mesh(nullptr) == 0 && map_suite::noMap(api));
    {
        // No triangles: an empty but valid map; NULL surfaces table means surface 0.
        RealmsMeshDesc b = d;
        b.num_triangles = 0;
        b.triangle_surfaces = nullptr;
        CHECK(api->map_load_mesh(&b) == 1);
        int32_t head = -1;
        CHECK(api->map_model(0, nullptr, nullptr, nullptr, &head) == 1 && head == 0); // headnode 0 is the world
        CHECK(ptrace(api, vec(0, 0, 128), vec(0, 0, -128), REALMS_MASK_ALL).fraction == 1.0f);
        CHECK(contents(api, vec(0, 0, -1)) == 0);
    }

    // The kd-tree must give exactly the answer of testing every triangle:
    // compare against the minimum over single-triangle maps.
    {
        uint32_t seed = 0x13572468u;
        auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return float(seed >> 8) / float(1 << 24);
        };
        auto range = [&](float lo, float hi) { return lo + (hi - lo) * rnd(); };
        Mesh soup;
        const int32_t s = soup.surface("soup", REALMS_CONTENTS_SOLID);
        for (int i = 0; i < 48; ++i) {
            const float size = i % 8 == 0 ? 160.0f : range(8, 48);
            const float cx = range(-200, 200), cy = range(-200, 200), cz = range(-200, 200);
            const int32_t a = soup.vertex(cx + range(-size, size), cy + range(-size, size), cz + range(-size, size));
            const int32_t b = soup.vertex(cx + range(-size, size), cy + range(-size, size), cz + range(-size, size));
            const int32_t c = soup.vertex(cx + range(-size, size), cy + range(-size, size), cz + range(-size, size));
            soup.tri(a, b, c, s);
        }
        struct Query {
            map_suite::V3 a, b, mn, mx;
        };
        std::vector<Query> queries;
        for (int i = 0; i < 120; ++i) {
            const float e = i % 3 == 0 ? 0.0f : range(2, 20);
            Query q{vec(range(-260, 260), range(-260, 260), range(-260, 260)),
                    vec(range(-260, 260), range(-260, 260), range(-260, 260)), vec(-e, -e, -e * 1.5f), vec(e, e, e)};
            if (i % 10 == 0) q.b = q.a; // position test
            queries.push_back(q);
        }
        std::vector<RealmsTrace> full;
        RealmsMeshDesc sd = soup.desc("maps/soup.glb");
        CHECK(api->map_load_mesh(&sd) == 1);
        RealmsMapInfo si{};
        si.struct_size = sizeof(si);
        CHECK(api->map_info(&si) == 1 && si.num_nodes > 2);
        for (const Query &q : queries) full.push_back(trace(api, q.a, q.b, q.mn, q.mx, 0, REALMS_MASK_ALL));
        std::vector<float> best(queries.size(), 1.0f);
        std::vector<int> startsolid(queries.size(), 0), allsolid(queries.size(), 0);
        for (int t = 0; t < 48; ++t) {
            Mesh one;
            one.surface("soup", REALMS_CONTENTS_SOLID);
            for (int k = 0; k < 3; ++k) {
                const int32_t vi = soup.indices[size_t(t * 3 + k)];
                one.vertex(soup.vertices[size_t(vi) * 3], soup.vertices[size_t(vi) * 3 + 1],
                           soup.vertices[size_t(vi) * 3 + 2]);
            }
            one.tri(0, 1, 2, 0);
            RealmsMeshDesc od = one.desc("maps/one.glb");
            CHECK(api->map_load_mesh(&od) == 1);
            for (size_t i = 0; i < queries.size(); ++i) {
                const RealmsTrace r = trace(api, queries[i].a, queries[i].b, queries[i].mn, queries[i].mx, 0,
                                            REALMS_MASK_ALL);
                if (r.fraction < best[i]) best[i] = r.fraction;
                startsolid[i] |= r.startsolid;
                allsolid[i] |= r.allsolid;
            }
        }
        int mismatches = 0, hits = 0;
        for (size_t i = 0; i < queries.size(); ++i) {
            if (full[i].fraction < 1) ++hits;
            // Once a trace reaches fraction 0 Quake 2 stops testing brushes, so
            // the solid flags are only comparable before that.
            if (full[i].fraction != best[i] ||
                (best[i] > 0 && (full[i].startsolid != startsolid[i] || full[i].allsolid != allsolid[i])))
                ++mismatches;
        }
        CHECK(mismatches == 0);
        CHECK(hits > 10);
        if (mismatches) std::fprintf(stderr, "  kd-tree mismatches: %d of %zu\n", mismatches, queries.size());
    }
    CHECK(api->map_load("") == 1);
}
