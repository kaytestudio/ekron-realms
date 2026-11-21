// Realms conformance: map geometry extraction (MapGeometry) and player
// movement (MapWalker). The geometry checks are pure C++; the walker checks
// run through realms::Engine, so they exercise whichever core the engine
// suite selected (Free Pascal or C++), on a BSP and on a glTF mesh map.
#pragma once

#include "bsp_builder.h"
#include "gltf_builder.h"

#include "realms/Engine.h"
#include "realms/MapGeometry.h"
#include "realms/MapWalker.h"
#include "realms/MeshMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace walker_suite {

// A closed 1024 x 1024 room (floor top z = 0, sky ceiling at 256) with a
// 16 unit step, a 40 unit block, a water pool, a crawl space under a 40 unit
// gap and a player clip brush.
inline bsp_builder::MapDef walkMap(bool faces) {
    bsp_builder::MapDef def;
    def.texinfo = {{"e1u1/floor", 0, 0},
                   {"e1u1/wall", 0, 0},
                   {"e1u1/water", REALMS_SURF_WARP, 0},
                   {"e1u1/sky", REALMS_SURF_SKY, 0},
                   {"e1u1/clip", REALMS_SURF_NODRAW, 0}};
    def.world.brushes = {
        {{-512, -512, -64}, {512, 512, 0}, REALMS_CONTENTS_SOLID, 0},       // floor
        {{-528, -512, 0}, {-512, 512, 256}, REALMS_CONTENTS_SOLID, 1},      // walls
        {{512, -512, 0}, {528, 512, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, -528, 0}, {528, -512, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, 512, 0}, {528, 528, 256}, REALMS_CONTENTS_SOLID, 1},
        {{-528, -528, 256}, {528, 528, 272}, REALMS_CONTENTS_SOLID, 3},     // sky ceiling
        {{128, -64, 0}, {192, 64, 16}, REALMS_CONTENTS_SOLID, 0},           // step
        {{128, 128, 0}, {192, 256, 40}, REALMS_CONTENTS_SOLID, 1},          // block
        {{-384, -384, 0}, {-256, -256, 64}, REALMS_CONTENTS_WATER, 2},      // pool
        {{-200, 100, 40}, {-100, 200, 256}, REALMS_CONTENTS_SOLID, 1},      // crawl space roof
        {{300, -300, 0}, {310, -200, 256}, REALMS_CONTENTS_PLAYERCLIP, 4},  // clip
    };
    def.entities = "{\n\"classname\" \"worldspawn\"\n}\n"
                   "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"256 256 24\"\n}\n"
                   "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 24\"\n\"angle\" \"90\"\n}\n";
    def.emitFaces = faces;
    return def;
}

inline bool near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

inline const realms::MapGeometryBatch *batchNamed(const realms::MapGeometry &g, const std::string &name) {
    for (const auto &b : g.batches)
        if (b.material == name) return &b;
    return nullptr;
}

// Every triangle winds counter-clockwise around its stored normal and the
// normal is unit length.
inline bool windingConsistent(const realms::MapGeometry &g) {
    for (const auto &b : g.batches) {
        if (b.positions.size() != b.normals.size() || b.uvs.size() * 3 != b.positions.size() * 2) return false;
        for (size_t t = 0; t + 9 <= b.positions.size(); t += 9) {
            const float *a = &b.positions[t], *p = &b.positions[t + 3], *c = &b.positions[t + 6];
            const float e1[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
            const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                e1[0] * e2[1] - e1[1] * e2[0]};
            const float *sn = &b.normals[t];
            if (n[0] * sn[0] + n[1] * sn[1] + n[2] * sn[2] <= 0.0f) return false;
            if (!near(sn[0] * sn[0] + sn[1] * sn[1] + sn[2] * sn[2], 1.0f, 1e-3f)) return false;
        }
    }
    return true;
}

inline void checkWalkGeometry(const realms::MapGeometry &g, realms::MapGeometrySource source) {
    CHECK(g.source == source);
    // 9 drawable brushes x 6 quads x 2 triangles: the sky ceiling is skipped
    // (SURF_SKY), the clip brush is never drawn.
    CHECK(g.triangleCount() == 108);
    CHECK(g.skipped == 0);
    CHECK(windingConsistent(g));
    CHECK(!batchNamed(g, "e1u1/sky") && !batchNamed(g, "e1u1/clip"));
    const auto *floor = batchNamed(g, "e1u1/floor");
    CHECK(floor && floor->triangleCount() == 24); // floor + step
    const auto *water = batchNamed(g, "e1u1/water");
    CHECK(water && water->triangleCount() == 12 && water->surfaceFlags == REALMS_SURF_WARP);
    if (source == realms::MapGeometrySource::Brushes && water) CHECK(water->contents == REALMS_CONTENTS_WATER);
    CHECK(g.mins[0] == -528.0f && g.mins[1] == -528.0f && g.mins[2] == -64.0f);
    CHECK(g.maxs[0] == 528.0f && g.maxs[1] == 528.0f && g.maxs[2] == 256.0f);
    // The floor's top face is the only upward face at z = 0 spanning x = 512.
    bool sawFloorTop = false;
    if (floor)
        for (size_t t = 0; t + 9 <= floor->positions.size(); t += 9) {
            const float *n = &floor->normals[t];
            if (n[2] == 1.0f && floor->positions[t + 2] == 0.0f &&
                (floor->positions[t] == 512.0f || floor->positions[t + 3] == 512.0f || floor->positions[t + 6] == 512.0f))
                sawFloorTop = true;
        }
    CHECK(sawFloorTop);
}

inline void runGeometryChecks() {
    std::string error;
    realms::MapGeometry g;

    const std::string withFaces = bsp_builder::build(walkMap(true));
    const std::string brushesOnly = bsp_builder::build(walkMap(false));
    const auto *fd = reinterpret_cast<const unsigned char *>(withFaces.data());
    const auto *bd = reinterpret_cast<const unsigned char *>(brushesOnly.data());

    CHECK(realms::bspGeometry(fd, withFaces.size(), g, error) && error.empty());
    checkWalkGeometry(g, realms::MapGeometrySource::Faces);
    CHECK(realms::bspGeometry(bd, brushesOnly.size(), g, error) && error.empty());
    checkWalkGeometry(g, realms::MapGeometrySource::Brushes);
    realms::MapGeometryOptions brushOpts;
    brushOpts.preferFaces = false;
    CHECK(realms::bspGeometry(fd, withFaces.size(), g, error, brushOpts));
    checkWalkGeometry(g, realms::MapGeometrySource::Brushes);
    realms::MapGeometryOptions keepSky;
    keepSky.skipSurfaceFlags = 0;
    CHECK(realms::bspGeometry(fd, withFaces.size(), g, error, keepSky) && g.triangleCount() == 120);

    // Bad headers fail cleanly.
    CHECK(!realms::bspGeometry(fd, 40, g, error) && !error.empty() && g.empty());
    std::string bad = withFaces;
    bad[4] = 37;
    CHECK(!realms::bspGeometry(reinterpret_cast<const unsigned char *>(bad.data()), bad.size(), g, error));
    bad = withFaces;
    bsp_builder::poke32(bad, 8 + 6 * 8 + 4, int32_t(withFaces.size())); // face lump length past the end
    CHECK(!realms::bspGeometry(reinterpret_cast<const unsigned char *>(bad.data()), bad.size(), g, error) &&
          error.find("lump 6") != std::string::npos);
    bad = withFaces;
    bsp_builder::poke32(bad, 8 + 6 * 8 + 4, bsp_builder::lumpLength(withFaces, 6) - 3); // partial face record
    CHECK(!realms::bspGeometry(reinterpret_cast<const unsigned char *>(bad.data()), bad.size(), g, error));

    // A face pointing at a missing edge is skipped, the rest still draws.
    bad = withFaces;
    bsp_builder::poke32(bad, size_t(bsp_builder::lumpOffset(withFaces, 6)) + 4, 1 << 30);
    CHECK(realms::bspGeometry(reinterpret_cast<const unsigned char *>(bad.data()), bad.size(), g, error));
    CHECK(g.source == realms::MapGeometrySource::Faces && g.skipped == 1 && g.triangleCount() == 106);

    // Mutation fuzz: random byte flips never crash (ASan / UBSan builds) and
    // only ever produce finite geometry.
    uint32_t seed = 0x5eed1234u;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    int parsed = 0;
    bool allFinite = true;
    for (int iter = 0; iter < 3000; ++iter) {
        std::string m = (iter & 1) ? withFaces : brushesOnly;
        const int flips = 1 + int(next() % 8);
        for (int f = 0; f < flips; ++f) {
            const size_t at = (iter % 3 == 0) ? next() % std::min<size_t>(m.size(), 160) : next() % m.size();
            m[at] = char(next() & 0xff);
        }
        if (realms::bspGeometry(reinterpret_cast<const unsigned char *>(m.data()), m.size(), g, error)) {
            ++parsed;
            for (const auto &b : g.batches)
                for (float v : b.positions)
                    if (!std::isfinite(v)) allFinite = false;
        }
    }
    CHECK(parsed > 1000 && allFinite);

    // Mesh maps: every triangle is drawn, including the render-only ceiling.
    realms::MeshMap mesh;
    const std::string room = gltf_builder::roomGlb();
    CHECK(realms::gltfToMeshMap(reinterpret_cast<const unsigned char *>(room.data()), room.size(), "maps/room.glb", {},
                                realms::GltfMapOptions{}, mesh, error));
    realms::meshMapGeometry(mesh, g);
    CHECK(g.source == realms::MapGeometrySource::Mesh && g.triangleCount() == 8 && g.batches.size() == 3);
    CHECK(windingConsistent(g));
    CHECK(g.mins[0] == -128.0f && g.maxs[0] == 128.0f);
    const auto *concrete = batchNamed(g, "concrete"); // floor + mirrored floor (solid batch comes first)
    CHECK(concrete && concrete->contents == REALMS_CONTENTS_SOLID && concrete->triangleCount() == 4);
    if (!mesh.surfaces.empty()) {
        mesh.surfaces[0].flags |= REALMS_SURF_NODRAW;
        realms::meshMapGeometry(mesh, g);
        CHECK(g.triangleCount() == 4);
    }
    mesh.indices.push_back(int32_t(mesh.vertices.size())); // dangling triangle
    mesh.indices.push_back(0);
    mesh.indices.push_back(1);
    mesh.triangleSurfaces.push_back(0);
    realms::meshMapGeometry(mesh, g, realms::MapGeometryOptions{0, 0, true});
    CHECK(g.triangleCount() == 8 && g.skipped == 1);
}

// Runs a walker for `seconds` with fixed input.
inline void walk(const realms::Engine &engine, realms::MapWalker &w, const realms::WalkInput &in, float seconds,
                 float *maxZ = nullptr) {
    for (float t = 0.0f; t < seconds; t += 0.016f) {
        w.move(engine, in, 0.016f);
        if (maxZ && w.origin[2] > *maxZ) *maxZ = w.origin[2];
    }
}

// Expects maps/walk.bsp (walkMap(true)) and maps/room.glb in the search path.
inline void runWalkerChecks(realms::Engine &engine) {
    CHECK(engine.loadMap("maps/walk.bsp"));
    realms::MapGeometry g;
    std::string error;
    CHECK(realms::loadMapGeometry(engine, g, error) && error.empty());
    checkWalkGeometry(g, realms::MapGeometrySource::Faces);

    realms::MapWalker w;
    CHECK(w.spawn(engine));
    CHECK(w.spawnSource == "info_player_start" && w.spawnYaw == 90.0f);
    CHECK(w.origin[0] == 0.0f && w.origin[1] == 0.0f && w.origin[2] == 33.0f && !w.onGround);
    CHECK(w.eye()[2] == 33.0f + realms::MapWalker::kStandView);

    // Falls 9 units onto the floor; traces stop 1/32 off the surface.
    realms::WalkInput idle;
    walk(engine, w, idle, 0.5f);
    CHECK(w.onGround && near(w.origin[2], 24.03125f, 0.01f) && w.velocity[2] == 0.0f);

    // Up the 16 unit step (yaw 0 = +X).
    realms::WalkInput fwd;
    fwd.forward = 1.0f;
    for (int i = 0; i < 200 && w.origin[0] < 160.0f; ++i) w.move(engine, fwd, 0.016f);
    CHECK(w.origin[0] >= 160.0f && w.onGround && near(w.origin[2], 40.03125f, 0.01f));
    CHECK(near(std::hypot(w.velocity[0], w.velocity[1]), 300.0f, 1.0f)); // pm_maxspeed

    // The 40 unit block stops the walker 16 + 1/32 units short.
    w.origin = {-64.0f, 192.0f, 24.03125f};
    w.velocity = {0, 0, 0};
    walk(engine, w, fwd, 1.0f);
    CHECK(near(w.origin[0], 111.96875f, 0.01f) && near(w.origin[2], 24.03125f, 0.01f) && w.onGround);
    // Walls: north wall at y = 512, walking diagonally slides along it.
    w.origin = {0.0f, 400.0f, 24.03125f};
    w.velocity = {0, 0, 0};
    realms::WalkInput diag = fwd;
    diag.yaw = 45.0f;
    walk(engine, w, diag, 1.0f);
    CHECK(near(w.origin[1], 495.96875f, 0.01f) && w.origin[0] > 100.0f && w.origin[0] < 300.0f);
    // The player clip brush blocks walking too (MASK_PLAYERSOLID).
    w.origin = {200.0f, -250.0f, 24.03125f};
    w.velocity = {0, 0, 0};
    walk(engine, w, fwd, 1.0f);
    CHECK(near(w.origin[0], 283.96875f, 0.01f));

    // Jump: ~ v^2 / 2g = 45.6 units up, then lands again.
    w.origin = {0.0f, -200.0f, 24.03125f};
    w.velocity = {0, 0, 0};
    walk(engine, w, idle, 0.1f);
    realms::WalkInput jump;
    jump.jump = true;
    float apex = w.origin[2];
    walk(engine, w, jump, 1.0f, &apex);
    CHECK(apex > 24.03125f + 42.0f && apex < 24.03125f + 47.0f);
    CHECK(w.onGround && near(w.origin[2], 24.03125f, 0.01f)); // holding jump doesn't re-jump
    walk(engine, w, idle, 0.05f);
    walk(engine, w, jump, 0.05f);
    CHECK(!w.onGround && w.velocity[2] > 0.0f);
    walk(engine, w, idle, 1.0f);

    // Crouching under the 40 unit gap: can't stand up until out of it.
    w.origin = {-150.0f, 150.0f, 24.03125f};
    w.velocity = {0, 0, 0};
    w.ducked = true;
    CHECK(!w.stuck(engine, w.origin));
    walk(engine, w, idle, 0.2f);
    CHECK(w.ducked && w.viewHeight() == realms::MapWalker::kDuckView);
    realms::WalkInput back;
    back.forward = -1.0f;
    walk(engine, w, back, 1.0f);
    CHECK(!w.ducked && w.origin[0] < -216.0f);
    CHECK(std::hypot(w.velocity[0], w.velocity[1]) > 250.0f); // standing speed again

    // Water: fully submerged in the pool, sinking slowly without input.
    w.origin = {-320.0f, -320.0f, 30.0f};
    w.velocity = {0, 0, 0};
    walk(engine, w, idle, 0.05f);
    CHECK(w.waterLevel == 3 && !w.onGround);
    const float sinkStart = w.origin[2];
    walk(engine, w, idle, 0.25f);
    CHECK(w.origin[2] < sinkStart && w.origin[2] > 24.0f && w.velocity[2] > -80.0f);

    // Noclip flies through the east wall.
    w.origin = {400.0f, 0.0f, 24.03125f};
    w.velocity = {0, 0, 0};
    w.noclip = true;
    walk(engine, w, fwd, 0.5f);
    CHECK(w.origin[0] > 650.0f && near(w.origin[2], 24.03125f, 0.01f));
    w.noclip = false;

    // Respawn picks info_player_start, not the deathmatch spot listed first.
    CHECK(w.spawn(engine) && w.origin[2] == 33.0f && !w.noclip);

    // glTF mesh map: info_player_start from the node, floor at z = 0.
    CHECK(engine.loadMap("maps/room.glb"));
    CHECK(realms::loadMapGeometry(engine, g, error) && g.source == realms::MapGeometrySource::Mesh &&
          g.triangleCount() == 8);
    CHECK(w.spawn(engine) && w.spawnSource == "info_player_start" && w.spawnYaw == 90.0f);
    CHECK(w.origin[0] == 32.0f && w.origin[1] == -64.0f && w.origin[2] == 25.0f);
    walk(engine, w, idle, 0.5f);
    CHECK(w.onGround && near(w.origin[2], 24.03125f, 0.01f));
    realms::WalkInput east = fwd;
    walk(engine, w, east, 0.3f);
    CHECK(w.onGround && w.origin[0] > 80.0f && near(w.origin[2], 24.03125f, 0.01f));

    engine.unloadMap();
    CHECK(!w.spawn(engine) && !realms::loadMapGeometry(engine, g, error) && error == "no map loaded");
    const realms::Vec3 before = w.origin;
    w.move(engine, fwd, 0.1f); // no map: no movement
    CHECK(w.origin == before);
}

} // namespace walker_suite
