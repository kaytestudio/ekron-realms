// Realms Engine - drawable geometry of the loaded map.
//
// The collision core (Pascal CModel or the C++ reference) only answers
// traces; renderers need polygons. This module extracts them straight from
// the map file:
//
//   * Quake 2 BSP with a face lump (any qbsp-compiled map): every face
//     (vertexes / edges / surfedges / faces / texinfo), sky and nodraw faces
//     skipped by default.
//   * BSP without faces (hand-built or tool-generated collision maps):
//     brush windings - each brush side clipped by the other sides, like
//     qbsp's CreateBrushWindings.
//   * glTF mesh maps: the triangles realms::MeshMap handed to the core.
//
// Output is in map space (Quake 2 axes, Z up, map units) grouped by
// material, three vertices per triangle, counter-clockwise around the
// normal. Malformed input never reads out of bounds: bad lumps fail with an
// error, bad faces / brushes are skipped and counted.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace realms {

class Engine;
struct MeshMap;

struct MapGeometryBatch {
    std::string material;     // texture / surface name ("" when unknown)
    int32_t surfaceFlags = 0; // REALMS_SURF_*
    int32_t contents = 0;     // brush / surface contents, 0 when unknown
    int32_t model = 0;        // BSP faces: 0 world, n = inline model "*n" (doors, platforms...)
    std::vector<float> positions; // xyz per vertex
    std::vector<float> normals;   // xyz per vertex (flat)
    std::vector<float> uvs;       // texinfo texels (BSP) or map units (planar)
    std::vector<int32_t> faces;   // per triangle: BSP face number (realms::MapLighting), -1 otherwise
    size_t triangleCount() const { return positions.size() / 9; }
};

enum class MapGeometrySource { None, Faces, Brushes, Mesh };

struct MapGeometry {
    MapGeometrySource source = MapGeometrySource::None;
    std::vector<MapGeometryBatch> batches;
    std::array<float, 3> mins{}, maxs{}; // of every emitted vertex
    size_t skipped = 0;                  // malformed faces / brushes dropped
    size_t triangleCount() const;
    bool empty() const { return batches.empty(); }
};

struct MapGeometryOptions {
    // Faces / brush sides with any of these texinfo flags are not emitted
    // (default REALMS_SURF_SKY | REALMS_SURF_NODRAW).
    int32_t skipSurfaceFlags = 0x4 | 0x80;
    // Brush fallback: only brushes with these contents are drawn (default
    // solid, window, lava, slime, water - no clip brushes).
    int32_t brushContents = 0x1 | 0x2 | 0x8 | 0x10 | 0x20;
    // Use the face lump when present (false: always brush windings).
    bool preferFaces = true;
};

// IBSP v38 file contents -> geometry. False with an error for a file that is
// not a usable BSP (bad header, lumps out of range).
bool bspGeometry(const unsigned char *data, size_t size, MapGeometry &out, std::string &error,
                 const MapGeometryOptions &options = {});

// Triangles of a mesh map. Surfaces flagged nodraw are skipped; surfaces
// without collision (contents 0) are still drawn.
void meshMapGeometry(const MeshMap &map, MapGeometry &out, const MapGeometryOptions &options = {});

// Geometry of the map the engine has loaded (Engine::mapPath / meshMap).
bool loadMapGeometry(const Engine &engine, MapGeometry &out, std::string &error,
                     const MapGeometryOptions &options = {});

} // namespace realms
