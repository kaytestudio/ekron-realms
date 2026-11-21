// Realms bridge conformance: collision model (map_*), bridge version 2.
// Included by realms_bridge_test.cpp (uses its CHECK, fake host and PAK
// helpers); runs identically against the Free Pascal and C++ cores.
#pragma once

#include "bsp_builder.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace map_suite {

inline bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

inline uint32_t crc32(const std::string &s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

enum { kTexFloor = 0, kTexWater = 1, kTexDoor = 2, kTexSky = 3 };

inline const char *entities() {
    return "{\n\"classname\" \"worldspawn\"\n\"message\" \"Realms test\"\n}\n"
           "{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n}\n";
}

// Floor, a wall at +x, a water pool, a player-clip block, and a door (*1).
inline bsp_builder::MapDef testMap() {
    bsp_builder::MapDef def;
    def.texinfo = {{"e1u1/floor1_3", 0, 0},
                   {"e1u1/water", REALMS_SURF_WARP, 0},
                   {"e1u1/door", 0, 7},
                   {"sky", REALMS_SURF_SKY, 0}};
    def.world.brushes = {
        {{-512, -512, -64}, {512, 512, 0}, REALMS_CONTENTS_SOLID, kTexFloor},
        {{256, -512, 0}, {320, 512, 256}, REALMS_CONTENTS_SOLID, kTexFloor},
        {{-256, -256, 0}, {-128, -128, 64}, REALMS_CONTENTS_WATER, kTexWater},
        {{0, 200, 0}, {64, 264, 128}, REALMS_CONTENTS_PLAYERCLIP, kTexFloor},
    };
    bsp_builder::ModelDef door;
    door.brushes = {{{100, -32, 0}, {116, 32, 128}, REALMS_CONTENTS_SOLID, kTexDoor}};
    def.inlineModels.push_back(door);
    def.entities = entities();
    return def;
}

struct V3 {
    float v[3];
};
inline V3 vec(float x, float y, float z) { return V3{{x, y, z}}; }

inline RealmsTrace trace(const RealmsCoreApi *api, V3 s, V3 e, V3 mn, V3 mx, int32_t head, int32_t mask,
                         const float *origin = nullptr, const float *angles = nullptr) {
    RealmsTrace t;
    std::memset(&t, 0x5a, sizeof(t)); // the core must overwrite everything
    api->map_box_trace(s.v, e.v, mn.v, mx.v, head, mask, origin, angles, &t);
    return t;
}
inline RealmsTrace ptrace(const RealmsCoreApi *api, V3 s, V3 e, int32_t mask, int32_t head = 0) {
    return trace(api, s, e, vec(0, 0, 0), vec(0, 0, 0), head, mask);
}
inline int32_t contents(const RealmsCoreApi *api, V3 p, int32_t head = 0, const float *origin = nullptr,
                        const float *angles = nullptr) {
    return api->map_point_contents(p.v, head, origin, angles);
}
inline bool noMap(const RealmsCoreApi *api) {
    RealmsMapInfo info{};
    info.struct_size = sizeof(info);
    return api->map_info(&info) == 0;
}

} // namespace map_suite

// The core must already be initialised with the fake host.
inline void runMapChecks(const RealmsCoreApi *api) {
    using namespace map_suite;
    const V3 zero = vec(0, 0, 0);
    const V3 pmin = vec(-16, -16, -24), pmax = vec(16, 16, 32); // player hull

    // --- no map loaded: everything is empty space
    CHECK(noMap(api));
    char small[16] = "x";
    CHECK(api->map_entities(small, int32_t(sizeof(small))) == -1);
    CHECK(small[0] == '\0');
    CHECK(contents(api, vec(0, 0, -32)) == 0);
    {
        const RealmsTrace t = ptrace(api, vec(0, 0, 128), vec(0, 0, -128), REALMS_MASK_ALL);
        CHECK(t.fraction == 1.0f && t.allsolid == 0 && t.startsolid == 0 && t.contents == 0);
        CHECK(t.endpos[2] == -128.0f && t.surface_name[0] == '\0');
    }
    CHECK(api->map_model(0, nullptr, nullptr, nullptr, nullptr) == 0);
    CHECK(api->map_point_leaf(zero.v) == 0);
    CHECK(api->map_leaf_info(0, nullptr, nullptr, nullptr) == 0);

    // --- load from a PAK
    bsp_builder::Stats stats;
    const std::string bsp = bsp_builder::build(testMap(), &stats);
    gHost.files["maps.pak"] = makePak({{"maps/test.bsp", bsp}, {"readme.txt", "hi"}});
    CHECK(api->fs_add_pak("maps.pak") == 2);
    CHECK(api->map_load("maps/test.bsp") == 1);

    RealmsMapInfo info{};
    info.struct_size = sizeof(info);
    CHECK(api->map_info(&info) == 1);
    CHECK(info.struct_size == sizeof(RealmsMapInfo));
    CHECK(info.checksum == crc32(bsp));
    CHECK(info.num_planes == stats.planes);
    CHECK(info.num_nodes == stats.nodes);
    CHECK(info.num_leafs == stats.leafs);
    CHECK(info.num_leaf_brushes == stats.leafBrushes);
    CHECK(info.num_brushes == 5 && stats.brushes == 5);
    CHECK(info.num_brush_sides == 30);
    CHECK(info.num_texinfo == 4);
    CHECK(info.num_models == 2);
    CHECK(info.num_clusters == stats.clusters);
    CHECK(info.num_areas == 2);
    CHECK(info.num_area_portals == 0);
    CHECK(info.entity_length == int32_t(std::strlen(entities())));
    CHECK(info.mins[0] == -513.0f && info.mins[1] == -513.0f && info.mins[2] == -65.0f);
    CHECK(info.maxs[0] == 513.0f && info.maxs[1] == 513.0f && info.maxs[2] == 257.0f);
    CHECK(std::string(info.name) == "maps/test.bsp");
    {
        RealmsMapInfo old{};
        old.struct_size = 8; // too small for this bridge version: refused
        CHECK(api->map_info(&old) == 0);
        CHECK(api->map_info(nullptr) == 0);
    }

    // --- entity lump (cvar_get style copy)
    {
        std::string buf(size_t(info.entity_length) + 1, '?');
        CHECK(api->map_entities(&buf[0], int32_t(buf.size())) == info.entity_length);
        CHECK(std::string(buf.c_str()) == entities());
        CHECK(api->map_entities(small, 8) == info.entity_length);
        CHECK(std::string(small) == std::string(entities()).substr(0, 7));
        CHECK(api->map_entities(nullptr, 0) == info.entity_length);
    }

    // --- point contents
    CHECK(contents(api, vec(0, 0, -32)) == REALMS_CONTENTS_SOLID);
    CHECK(contents(api, vec(0, 0, 32)) == 0);
    CHECK(contents(api, vec(-200, -200, 32)) == REALMS_CONTENTS_WATER);
    CHECK(contents(api, vec(32, 232, 64)) == REALMS_CONTENTS_PLAYERCLIP);
    CHECK(contents(api, vec(288, 0, 128)) == REALMS_CONTENTS_SOLID);
    CHECK(contents(api, vec(108, 0, 64)) == 0); // the door is not part of the world model

    // --- point trace onto the floor (exact Quake 2 DIST_EPSILON arithmetic)
    {
        const RealmsTrace t = ptrace(api, vec(0, 0, 128), vec(0, 0, -128), REALMS_MASK_SOLID);
        CHECK(t.allsolid == 0 && t.startsolid == 0);
        CHECK(t.fraction == 4095.0f / 8192.0f);
        CHECK(t.endpos[0] == 0.0f && t.endpos[1] == 0.0f && t.endpos[2] == 0.03125f);
        CHECK(t.plane_normal[0] == 0.0f && t.plane_normal[1] == 0.0f && t.plane_normal[2] == 1.0f);
        CHECK(t.plane_dist == 0.0f);
        CHECK(t.contents == REALMS_CONTENTS_SOLID);
        CHECK(std::string(t.surface_name) == "e1u1/floor1_3");
        CHECK(t.surface_flags == 0 && t.surface_value == 0);
    }
    // --- player hull onto the floor: stops 24 units up
    {
        const RealmsTrace t = trace(api, vec(0, 0, 128), vec(0, 0, -128), pmin, pmax, 0, REALMS_MASK_PLAYERSOLID);
        CHECK(t.allsolid == 0 && t.startsolid == 0);
        CHECK(t.endpos[2] == 24.03125f);
        CHECK(t.plane_normal[2] == 1.0f);
    }
    // --- into the wall: -x face
    {
        const RealmsTrace t = ptrace(api, vec(0, 0, 128), vec(512, 0, 128), REALMS_MASK_SOLID);
        CHECK(t.endpos[0] == 255.96875f && t.endpos[2] == 128.0f);
        CHECK(t.plane_normal[0] == -1.0f && t.plane_dist == -256.0f);
        const RealmsTrace b = trace(api, vec(0, 0, 128), vec(512, 0, 128), pmin, pmax, 0, REALMS_MASK_SOLID);
        CHECK(b.endpos[0] == 239.96875f);
    }
    // --- open air
    {
        const RealmsTrace t = ptrace(api, vec(0, 0, 64), vec(0, 100, 64), REALMS_MASK_ALL);
        CHECK(t.fraction == 1.0f && t.endpos[1] == 100.0f && t.contents == 0 && t.plane_normal[2] == 0.0f);
        CHECK(t.surface_name[0] == '\0' && t.allsolid == 0 && t.startsolid == 0);
    }
    // --- brush masks: water is skipped by MASK_SOLID, hit by MASK_WATER
    {
        const RealmsTrace s = ptrace(api, vec(-200, -200, 128), vec(-200, -200, -128), REALMS_MASK_SOLID);
        CHECK(s.endpos[2] == 0.03125f && s.contents == REALMS_CONTENTS_SOLID);
        const RealmsTrace w = ptrace(api, vec(-200, -200, 128), vec(-200, -200, -128), REALMS_MASK_WATER);
        CHECK(w.endpos[2] == 64.03125f && w.contents == REALMS_CONTENTS_WATER);
        CHECK(std::string(w.surface_name) == "e1u1/water" && w.surface_flags == REALMS_SURF_WARP);
        const RealmsTrace c = ptrace(api, vec(32, 136, 64), vec(32, 392, 64), REALMS_MASK_SOLID);
        CHECK(c.fraction == 1.0f);
        const RealmsTrace p = ptrace(api, vec(32, 136, 64), vec(32, 392, 64), REALMS_MASK_PLAYERSOLID);
        CHECK(p.endpos[1] == 199.96875f && p.contents == REALMS_CONTENTS_PLAYERCLIP);
    }
    // --- starting inside solids
    {
        const RealmsTrace a = ptrace(api, vec(0, 0, -32), vec(0, 0, -48), REALMS_MASK_SOLID);
        CHECK(a.allsolid == 1 && a.startsolid == 1);
        const RealmsTrace b = ptrace(api, vec(0, 0, -32), vec(0, 0, 32), REALMS_MASK_SOLID);
        CHECK(b.allsolid == 0 && b.startsolid == 1);
    }
    // --- position tests (start == end), CM_BoxTrace's early return
    {
        const RealmsTrace a = ptrace(api, vec(0, 0, -32), vec(0, 0, -32), REALMS_MASK_SOLID);
        CHECK(a.allsolid == 1 && a.startsolid == 1 && a.fraction == 0.0f && a.contents == REALMS_CONTENTS_SOLID);
        CHECK(a.endpos[2] == -32.0f);
        const RealmsTrace b = ptrace(api, vec(0, 0, 64), vec(0, 0, 64), REALMS_MASK_SOLID);
        CHECK(b.allsolid == 0 && b.startsolid == 0 && b.fraction == 1.0f && b.endpos[2] == 64.0f);
        const RealmsTrace c = trace(api, vec(0, 0, 20), vec(0, 0, 20), pmin, pmax, 0, REALMS_MASK_SOLID);
        CHECK(c.allsolid == 1 && c.fraction == 0.0f);
        const RealmsTrace d = trace(api, vec(0, 0, 30), vec(0, 0, 30), pmin, pmax, 0, REALMS_MASK_SOLID);
        CHECK(d.allsolid == 0 && d.startsolid == 0 && d.fraction == 1.0f);
    }

    // --- inline model *1 (door)
    float mins[3], maxs[3], origin[3];
    int32_t door = 12345;
    CHECK(api->map_model(1, mins, maxs, origin, &door) == 1);
    CHECK(door == stats.modelHeadnodes[1]);
    CHECK(mins[0] == 99.0f && mins[1] == -33.0f && mins[2] == -1.0f);
    CHECK(maxs[0] == 117.0f && maxs[1] == 33.0f && maxs[2] == 129.0f);
    CHECK(origin[0] == 0.0f && origin[1] == 0.0f && origin[2] == 0.0f);
    int32_t world = 12345;
    CHECK(api->map_model(0, nullptr, nullptr, nullptr, &world) == 1 && world == stats.modelHeadnodes[0]);
    CHECK(api->map_model(2, nullptr, nullptr, nullptr, nullptr) == 0);
    CHECK(api->map_model(-1, nullptr, nullptr, nullptr, nullptr) == 0);
    {
        CHECK(ptrace(api, vec(0, 0, 64), vec(200, 0, 64), REALMS_MASK_SOLID, world).fraction == 1.0f);
        const RealmsTrace t = ptrace(api, vec(0, 0, 64), vec(256, 0, 64), REALMS_MASK_SOLID, door);
        CHECK(t.endpos[0] == 99.96875f && t.contents == REALMS_CONTENTS_SOLID);
        CHECK(std::string(t.surface_name) == "e1u1/door" && t.surface_value == 7);
        CHECK(contents(api, vec(108, 0, 64), door) == REALMS_CONTENTS_SOLID);
    }
    // --- transformed: moved by origin, rotated by angles (CM_Transformed*)
    {
        const float moved[3] = {0, 100, 0};
        const RealmsTrace t = trace(api, vec(0, 100, 64), vec(256, 100, 64), zero, zero, door, REALMS_MASK_SOLID, moved);
        CHECK(near(t.endpos[0], 99.96875f) && t.endpos[1] == 100.0f && t.fraction < 1.0f);
        CHECK(trace(api, vec(0, 0, 64), vec(256, 0, 64), zero, zero, door, REALMS_MASK_SOLID, moved).fraction == 1.0f);
        CHECK(contents(api, vec(108, 100, 64), door, moved) == REALMS_CONTENTS_SOLID);
        CHECK(contents(api, vec(108, 0, 64), door, moved) == 0);

        const float yaw90[3] = {0, 90, 0};
        const RealmsTrace r = trace(api, vec(0, 0, 64), vec(0, 256, 64), zero, zero, door, REALMS_MASK_SOLID, nullptr, yaw90);
        CHECK(r.fraction < 1.0f);
        CHECK(near(r.endpos[0], 0.0f) && near(r.endpos[1], 99.96875f) && near(r.endpos[2], 64.0f));
        CHECK(near(r.plane_normal[0], 0.0f) && near(r.plane_normal[1], -1.0f) && near(r.plane_normal[2], 0.0f));
        CHECK(contents(api, vec(0, 108, 64), door, nullptr, yaw90) == REALMS_CONTENTS_SOLID);
        CHECK(contents(api, vec(0, 108, 64), door) == 0);
    }
    // --- temporary box hull (entities)
    {
        const V3 bmin = vec(-16, -16, 0), bmax = vec(16, 16, 56);
        const int32_t box = api->map_headnode_for_box(bmin.v, bmax.v);
        const RealmsTrace t = ptrace(api, vec(-112, 0, 24), vec(144, 0, 24), REALMS_MASK_ALL, box);
        CHECK(t.endpos[0] == -16.03125f && t.contents == REALMS_CONTENTS_MONSTER);
        CHECK(t.plane_normal[0] == -1.0f && t.surface_name[0] == '\0' && t.surface_flags == 0);
        CHECK(contents(api, vec(0, 0, 24), box) == REALMS_CONTENTS_MONSTER);
        CHECK(contents(api, vec(0, 0, 100), box) == 0);
        const RealmsTrace h = trace(api, vec(-112, 0, 24), vec(144, 0, 24), pmin, pmax, box, REALMS_MASK_ALL);
        CHECK(h.endpos[0] == -32.03125f);
        // Re-shaping the box moves it; the solid world is unaffected.
        const V3 big = vec(64, 64, 64);
        const int32_t box2 = api->map_headnode_for_box(zero.v, big.v);
        CHECK(box2 == box);
        CHECK(contents(api, vec(-8, -8, 24), box) == 0); // outside the moved box
        CHECK(contents(api, vec(32, 32, 32), box) == REALMS_CONTENTS_MONSTER);
        CHECK(contents(api, vec(0, 0, -32)) == REALMS_CONTENTS_SOLID);
    }
    // --- leafs
    {
        int32_t c = -9, cl = -9, ar = -9;
        const int32_t air = api->map_point_leaf(vec(0, 0, 32).v);
        CHECK(air > 0 && air < info.num_leafs);
        CHECK(api->map_leaf_info(air, &c, &cl, &ar) == 1);
        CHECK(c == 0 && cl >= 0 && cl < info.num_clusters && ar == 1);
        const int32_t solid = api->map_point_leaf(vec(0, 0, -32).v);
        CHECK(api->map_leaf_info(solid, &c, &cl, &ar) == 1);
        CHECK(c == REALMS_CONTENTS_SOLID && cl == -1 && ar == 0);
        CHECK(api->map_leaf_info(0, &c, nullptr, nullptr) == 1 && c == REALMS_CONTENTS_SOLID);
        CHECK(api->map_leaf_info(-1, nullptr, nullptr, nullptr) == 0);
        CHECK(api->map_leaf_info(info.num_leafs, nullptr, nullptr, nullptr) == 0); // box leaf is internal
    }
    // --- invalid headnodes behave like empty space
    CHECK(contents(api, vec(0, 0, -32), 99999) == 0);
    CHECK(contents(api, vec(0, 0, -32), -99999) == 0);
    CHECK(contents(api, vec(0, 0, -32), INT32_MIN) == 0);
    {
        const RealmsTrace t = ptrace(api, vec(0, 0, 128), vec(0, 0, -128), REALMS_MASK_ALL, -99999);
        CHECK(t.fraction == 1.0f && t.endpos[2] == -128.0f);
        RealmsTrace n;
        api->map_box_trace(nullptr, vec(1, 2, 3).v, nullptr, nullptr, 0, REALMS_MASK_ALL, nullptr, nullptr, &n);
        CHECK(n.fraction == 1.0f && n.endpos[2] == 3.0f);
        api->map_box_trace(vec(0, 0, 0).v, vec(1, 2, 3).v, nullptr, nullptr, 0, REALMS_MASK_ALL, nullptr, nullptr,
                           nullptr); // must not crash
    }

    // --- unload / loose files / reload
    CHECK(api->map_load("") == 1);
    CHECK(noMap(api));
    CHECK(contents(api, vec(0, 0, -32)) == 0);
    gHost.files["maps/loose.bsp"] = bsp;
    CHECK(api->map_load("maps/loose.bsp") == 1);
    CHECK(api->map_info(&info) == 1 && std::string(info.name) == "maps/loose.bsp");
    CHECK(api->map_load(nullptr) == 1);
    CHECK(noMap(api));

    // --- malformed maps are rejected and leave no map loaded
    auto rejects = [&](const std::string &name, const std::string &data) {
        CHECK(api->map_load("maps/test.bsp") == 1); // something loaded before
        gHost.files[name] = data;
        gHost.lines.clear();
        const bool rejected = api->map_load(name.c_str()) == 0;
        CHECK(rejected);
        CHECK(noMap(api));
        CHECK(sawLine("map_load"));
        if (!rejected) std::fprintf(stderr, "  (accepted malformed %s)\n", name.c_str());
    };
    rejects("maps/empty.bsp", std::string());
    CHECK(api->map_load("maps/missing.bsp") == 0 && noMap(api));
    rejects("maps/tiny.bsp", "IBSP");
    {
        std::string b = bsp;
        b[0] = 'X';
        rejects("maps/ident.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, 4, 39);
        rejects("maps/version.bsp", b);
    }
    rejects("maps/truncated.bsp", bsp.substr(0, bsp.size() / 2));
    {
        std::string b = bsp;
        bsp_builder::poke32(b, 8 + bsp_builder::kPlanes * 8, int32_t(bsp.size()));
        rejects("maps/lumpofs.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, 8 + bsp_builder::kPlanes * 8 + 4, bsp_builder::lumpLength(bsp, bsp_builder::kPlanes) - 1);
        rejects("maps/funnysize.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kNodes)) + 4, stats.nodes + 5);
        rejects("maps/nodechild.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kNodes)), stats.planes);
        rejects("maps/nodeplane.bsp", b);
    }
    {
        std::string b = bsp; // node 1 points back at node 0
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kNodes)) + 28 + 4, 0);
        rejects("maps/cycle.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke16(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kLeafBrushes)), int16_t(9999));
        rejects("maps/leafbrush.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke16(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kBrushSides)), int16_t(-1));
        rejects("maps/sideplane.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke16(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kBrushSides)) + 2, int16_t(100));
        rejects("maps/sidetex.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kBrushes)) + 4, 1000);
        rejects("maps/brushsides.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kLeafs)), 0);
        rejects("maps/leaf0.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kModels)) + 36, -1 - stats.leafs);
        rejects("maps/headnode.bsp", b);
    }
    {
        std::string b = bsp;
        bsp_builder::poke32(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kPlanes)) + 16, 9);
        rejects("maps/planetype.bsp", b);
    }
    {
        // A brush side without texinfo (-1) is legal: null surface.
        std::string b = bsp;
        bsp_builder::poke16(b, size_t(bsp_builder::lumpOffset(bsp, bsp_builder::kBrushSides)) + 2, int16_t(-1));
        gHost.files["maps/notex.bsp"] = b;
        CHECK(api->map_load("maps/notex.bsp") == 1);
    }
    {
        // 600 slabs make the builder's naive kd tree ~1200 levels deep:
        // rejected up front instead of overflowing the stack in a trace.
        bsp_builder::MapDef deep;
        deep.texinfo = {{"x", 0, 0}};
        for (int i = 0; i < 600; ++i) {
            const float x = float(i * 4);
            deep.world.brushes.push_back({{x, 0, 0}, {x + 2, 64, 64}, REALMS_CONTENTS_SOLID, 0});
        }
        rejects("maps/deep.bsp", bsp_builder::build(deep));
    }

    // --- fuzz: corrupted copies must never crash; accepted ones stay usable
    {
        uint32_t seed = 0x5eed1234u;
        auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        int accepted = 0;
        for (int iter = 0; iter < 400; ++iter) {
            std::string b = bsp;
            const int flips = 1 + int(rnd() % 6);
            for (int k = 0; k < flips; ++k) {
                const size_t at = size_t(rnd() % b.size());
                b[at] = char(rnd() & 0xff);
            }
            gHost.files["maps/fuzz.bsp"] = b;
            if (api->map_load("maps/fuzz.bsp") != 1) {
                CHECK(noMap(api));
                continue;
            }
            ++accepted;
            int32_t head = 0;
            api->map_model(0, nullptr, nullptr, nullptr, &head);
            RealmsTrace t;
            api->map_box_trace(vec(0, 0, 128).v, vec(37, -19, -128).v, pmin.v, pmax.v, head, REALMS_MASK_ALL,
                               nullptr, nullptr, &t);
            CHECK(t.fraction >= 0.0f && t.fraction <= 1.0f);
            api->map_point_contents(vec(5, 5, 5).v, head, nullptr, nullptr);
            api->map_box_trace(vec(3, 3, 3).v, vec(3, 3, 3).v, pmin.v, pmax.v, head, REALMS_MASK_ALL, nullptr,
                               nullptr, &t);
        }
        CHECK(accepted > 0); // flips in unused bytes keep the map valid
        gHost.files.erase("maps/fuzz.bsp");
    }
    CHECK(api->map_load("") == 1);
}

#include "realms_vis_mesh_suite.h"

inline void runMapSuite(const RealmsCoreApi *api) {
    const bool complete = api->map_load && api->map_info && api->map_entities && api->map_model &&
                          api->map_headnode_for_box && api->map_point_contents && api->map_box_trace &&
                          api->map_point_leaf && api->map_leaf_info && api->map_cluster_pvs &&
                          api->map_cluster_phs && api->map_set_area_portal && api->map_area_portal_open &&
                          api->map_areas_connected && api->map_write_area_bits && api->map_box_leafs &&
                          api->map_headnode_visible && api->map_load_mesh;
    CHECK(complete);
    if (!complete) return;
    gHost = FakeHost{};
    const RealmsHostApi host = makeHost(true);
    CHECK(api->init(&host) == 1);
    runMapChecks(api);
    runVisChecks(api);
    runMeshChecks(api);
    // shutdown drops the map
    CHECK(api->map_load("maps/test.bsp") == 1);
    api->shutdown();
    CHECK(api->init(&host) == 1);
    CHECK(map_suite::noMap(api));
    api->shutdown();
}
