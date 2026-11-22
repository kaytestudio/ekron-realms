// Loaded map, walker and coordinate conversion, shared by the map modes.
// The core uses map units, Z up; renderers use metres, Y up:
// render = (x, z, -y) / unitsPerMetre (32 for BSPs, map_mesh_scale for glTF,
// which makes render space the glTF file's own space).
// Camera: yaw in radians, 0 looks down -Z, positive turns left; positive
// pitch looks up.
#pragma once

#include "CollisionMesh.h"
#include "MeshGeometry.h"

#include "realms/Engine.h"
#include "realms/MapGeometry.h"
#include "realms/MapWalker.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace mapview {

constexpr float kBspUnitsPerMetre = 32.0f;

// Sandbox material layer for a map surface: 0 asphalt, 1 concrete, 2 brick,
// 3 metal, 4 water, 5 wood, 6 fabric, 7 glass.
int materialLayer(const std::string &name, int32_t surfaceFlags, int32_t contents);

// Extracted map geometry as sandbox renderer vertices (render space, tint,
// one texture repeat per 64 texels / units, material layer).
void toRenderVertices(const realms::MapGeometry &geometry, float unitsPerMetre,
                      std::vector<sandbox::MeshVertex> &out);

// Walker conversions between map units and the renderers' camera.
realms::WalkInput walkInput(const realms::WalkInput &input, float renderYaw, float renderPitch);
physics::Vec3 toRender(const realms::Vec3 &mapPoint, float unitsPerMetre);
float renderYawFromMap(float mapYawDegrees);

// Map names from a platform UI (asset / storage relative paths, quoted on
// the "map" command line): letters, digits, "_-./" and spaces; no quotes,
// absolute paths or parent references.
bool safeMapName(const std::string &name);

// Maps imported through the platform file pickers (Android
// OpenMultipleDocuments, iOS UIDocumentPicker) are copied into the writable
// root's maps/ folder, where the pickers list them and "map" finds them.
// A .gltf may come with its .bin buffers and .png / .jpg textures (same
// folder only: the copies are flat).
constexpr std::size_t kMaxImportBytes = std::size_t(128) << 20;
// The stored file name for a picked file ("" = not importable): the last
// path component, characters outside safeMapName's set replaced by "_",
// no "..", one of .bsp .glb .gltf .bin .png .jpg .jpeg, at most 120 bytes.
std::string importFileName(const std::string &pickedName);
// The first bytes of a picked file match its extension (glTF / IBSP / JSON
// object / PNG / JPEG signature; .bin is free-form).
bool importHeaderOk(const std::string &fileName, const unsigned char *head, std::size_t size);
// .bsp, .glb or .gltf (the files the pickers list and load).
bool isMapFileName(const std::string &fileName);

// A point in a level-mode city::Scene built from the same file (the
// sandbox city): level = (source - sourceOffset) * sourceScale.
physics::Vec3 toLevelScene(const physics::Vec3 &source, float sourceScale, const std::array<float, 3> &sourceOffset);

// One loaded map being walked: geometry source, units and the walker.
class MapSession {
public:
    // Extracts the engine's loaded map into `geometry`, picks the units and
    // spawns the walker. False (and inactive) without a map.
    bool open(realms::Engine &engine, realms::MapGeometry &geometry, std::string &error);
    void close();
    bool active() const { return active_; }
    // glTF / mesh map (render space is the file's own space).
    bool mesh() const { return mesh_; }

    void update(const realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                float dt);
    bool respawn(const realms::Engine &engine);
    // Noclip on / off (stops the walker either way).
    void toggleNoclip();

    physics::Vec3 eye() const { return toRender(walker_.eye(), unitsPerMetre_); }
    float spawnRenderYaw() const { return renderYawFromMap(walker_.spawnYaw); }

    realms::MapWalker &walker() { return walker_; }
    const realms::MapWalker &walker() const { return walker_; }
    const std::string &path() const { return path_; }
    float unitsPerMetre() const { return unitsPerMetre_; }
    // "BSP faces, 1234 triangles" etc.; renderers may append to it.
    std::string &summary() { return summary_; }
    const std::string &summary() const { return summary_; }

private:
    bool active_ = false;
    bool mesh_ = false;
    std::string path_;
    std::string summary_;
    float unitsPerMetre_ = kBspUnitsPerMetre;
    realms::MapWalker walker_;
};

} // namespace mapview
