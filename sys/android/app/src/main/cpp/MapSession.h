// Realms map mode, renderer independent: shared by the desktop client
// (DesktopMapMode) and Android (AndroidMapMode).
//
// Spaces: the core works in map units, Z up (Quake 2). The renderers work
// in metres, Y up: render = (x, z, -y) / unitsPerMetre, with 32 units per
// metre for BSPs and map_mesh_scale for glTF maps. For glTF maps that makes
// render space exactly the glTF file's own space, so the textured model
// (city::LoadOptions::keepSourceSpace) needs no transform.
//
// Camera convention (both renderers): yaw in radians, 0 looks down -Z and
// positive yaw turns left; positive pitch looks up.
#pragma once

#include "CollisionMesh.h"
#include "MeshGeometry.h"

#include "realms/Engine.h"
#include "realms/MapGeometry.h"
#include "realms/MapWalker.h"

#include <array>
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
