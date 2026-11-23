// glTF 2.0 (.glb or .gltf) maps converted to a triangle soup in Quake 2 map
// space for map_load_mesh (bridge v3). The core turns each colliding
// triangle into a thin brush.
//
// Axes: glTF (x, y, z) metres -> map (x, -z, y) * scale, 32 units per metre
// by default. One surface per material (name cut to 31 chars) plus
// "default".
//
// Collision tags, from material names or material/node extras:
//   "nocollide" in a name, or {"realms_contents": "none"}: no collision
//   "water" / "slime" / "lava" in a material name: liquid
//   {"realms_contents": "solid|water|slime|lava|playerclip|monsterclip|
//    window|none" or a number, "realms_flags": n, "realms_value": n}
//
// Entities, for the Quake 2 game (Quake2Module) and the walker's spawn:
//   worldspawn: "message" (the file name) plus the scene's extras.
//   One point entity per node named after a Quake 2 classname (lower-cased,
//   Blender ".001" suffixes dropped): info_*, item_*, weapon_*, ammo_*,
//   key_*, monster_*, misc_*, target_*, path_*, point_*, light, light_*,
//   trigger_relay/always/counter/key, func_timer, func_clock, turret_driver.
//   "origin" is the node's world position: place it where Quake 2 would,
//   about 24 units (0.75 m) above the floor for player starts, items and
//   monsters.
//   "angle": extras "angle"/"angles" if set, else {"realms_angle": deg},
//   else the yaw of the node's +X axis (when not 0).
//   Node extras become keys: strings, numbers, booleans (1/0) and arrays of
//   up to 4 numbers ("_color": [1, 0.5, 0]); realms_* tags, classname,
//   origin and inline "model" values ("*N") are dropped.
//   Brush entities: a node named func_* / trigger_* / turret_* (other than
//   the point classes above, e.g. func_door, func_plat, func_button,
//   func_train, trigger_multiple) becomes an inline model "*n" made of its
//   meshes and its children's, with "model" "*n", its extras as keys and
//   "angle" from extras only (for doors it is the move direction: -1 up,
//   -2 down). func_rotating and func_door_rotating turn about the node's
//   position: they get it as "origin" and their triangles are stored
//   relative to it. A brush entity node without triangles is not exported
//   (the game stops on a brush entity without a model); a node used twice
//   is a brush entity the first time only.
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

// An inline model ("*n" is models[n - 1]).
struct MeshMapModel {
    int32_t node = -1;                // glTF node index of the brush entity
    std::string classname;            // "func_door", ...
    std::array<float, 3> origin{};    // map units: the triangles are relative to it
    size_t triangles = 0;
};

struct MeshMap {
    std::vector<float> vertices;           // xyz triples, map space
    std::vector<int32_t> indices;          // 3 per triangle
    std::vector<int32_t> triangleSurfaces; // one per triangle
    std::vector<int32_t> triangleModels;   // one per triangle (0 = world), empty without models
    std::vector<MeshMapModel> models;      // inline models
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
