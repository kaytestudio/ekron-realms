// Realms Engine - triangle mesh maps (.glb / .gltf).
//
// A mesh map is a triangle soup in Quake 2 map space (Z up, map units) with a
// surface table, handed to the core through map_load_mesh (bridge v3). The
// core turns every colliding triangle into a thin brush, so traces, point
// contents and the player hull behave exactly like on a BSP map.
//
// gltfToMeshMap() reads glTF 2.0 (binary .glb or JSON .gltf with embedded or
// external buffers) without third-party code:
//   * scene graph: the default scene's nodes with matrix / TRS transforms,
//     meshes instanced by several nodes are emitted once per node
//   * primitives: TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN; POSITION as float
//     or (normalized) integer VEC3; u8/u16/u32 indices or none
//   * axes: glTF is Y up, right handed, metres -> map (x, -z, y) * scale
//     (Quake 2 player: 56 units tall, so 32 units per metre by default)
//   * surfaces: one per glTF material (name truncated to 31 chars) plus a
//     "default" surface for primitives without a material
//   * collision tags (material name, material extras or node extras):
//       "nocollide" in a name, or extras {"realms_contents": "none"}
//                               -> rendered only, no collision
//       "water" / "slime" / "lava" in a material name -> liquid contents
//       extras {"realms_contents": "solid|water|slime|lava|playerclip|
//               monsterclip|window|none" or a number,
//               "realms_flags": n, "realms_value": n}
//   * entities: worldspawn, plus one entity per node whose name starts with
//     "info_" (Blender ".001" suffixes are dropped), e.g. info_player_start,
//     with its map-space "origin"; node extras {"realms_angle": deg} adds an
//     "angle" key
#pragma once

#include "realms/realms_bridge.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace realms {

struct MeshMapSurface {
    std::string name;
    int32_t flags = 0;
    int32_t value = 0;
    int32_t contents = REALMS_CONTENTS_SOLID; // 0 = no collision
};

struct MeshMap {
    std::vector<float> vertices;           // xyz triples, map space
    std::vector<int32_t> indices;          // 3 per triangle
    std::vector<int32_t> triangleSurfaces; // one per triangle
    std::vector<MeshMapSurface> surfaces;  // at least one
    std::string entities;                  // Quake 2 entity lump text
    float thickness = 0.0f;                // <= 0: core default (2 units)
    // Bounds of every triangle in glTF space before the axis change and
    // scale (renderers use them to line up their own glTF import).
    std::array<float, 3> sourceMins{}, sourceMaxs{};
    size_t triangleCount() const { return indices.size() / 3; }
};

struct GltfMapOptions {
    float scale = 32.0f; // map units per glTF unit (metre)
    // Limits that keep hostile files bounded (the core accepts at most
    // 2^20 triangles).
    size_t maxTriangles = size_t(1) << 20;
    size_t maxVertices = size_t(1) << 24;
};

using MeshMapReadFn = std::function<bool(const std::string &path, std::vector<unsigned char> &out)>;

// Converts a .glb / .gltf file. `path` is used to resolve external buffers
// (relative to its directory) through `readFile`, which may be empty when
// only self-contained files are expected. Returns false with `error` set on
// malformed input.
bool gltfToMeshMap(const unsigned char *data, size_t size, const std::string &path, const MeshMapReadFn &readFile,
                   const GltfMapOptions &options, MeshMap &out, std::string &error);

// True for paths ending in .glb or .gltf (case-insensitive).
bool isMeshMapPath(const std::string &path);

// glTF (Y up) -> Quake 2 map space (Z up), without scale.
inline std::array<float, 3> gltfToMapAxes(float x, float y, float z) { return {x, -z, y}; }

// Fills a RealmsMeshDesc that points into `map` (valid while `map` lives).
RealmsMeshDesc meshMapDesc(const MeshMap &map, std::vector<RealmsMeshSurface> &surfaceStorage, const char *name,
                           uint32_t checksum);

// CRC-32 (IEEE 802.3), the checksum map_info reports for mesh maps.
uint32_t crc32(const unsigned char *data, size_t size);

} // namespace realms
