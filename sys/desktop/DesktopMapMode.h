// Realms map mode for the desktop client: "map <name>" swaps the sandbox for
// a Quake 2 BSP or a glTF map loaded into the Realms core, walked with
// realms::MapWalker (collision by the Pascal / C++ CModel) and drawn by
// DesktopRenderer::renderMapWorld.
//
// Spaces: the core works in map units, Z up (Quake 2). The renderer works in
// metres, Y up. render = (x, z, -y) / unitsPerMetre, with 32 units per metre
// for BSPs and map_mesh_scale for glTF maps - which makes render space
// exactly the glTF file's own space, so the textured model needs no
// transform.
#pragma once

#include "DesktopRenderer.h"

#include "realms/Engine.h"
#include "realms/MapGeometry.h"
#include "realms/MapWalker.h"

#include <memory>
#include <string>
#include <vector>

namespace ekron::desktop {

class DesktopMapMode {
public:
    static constexpr float kBspUnitsPerMetre = 32.0f;

    // Builds and uploads the loaded map's geometry and spawns the walker.
    // On failure the mode stays inactive and `error` says why.
    bool open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error);
    void close(ekron::gl::Api &gl, DesktopRenderer &renderer);
    bool active() const { return active_; }

    // Walk one frame; yaw / pitch are the renderer's camera angles (radians,
    // yaw 0 = -Z, positive pitch looks up).
    void update(const realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                float dt);
    bool respawn(const realms::Engine &engine);

    physics::Vec3 eye() const;
    // Camera yaw (renderer convention) facing the spawn point's angle.
    float spawnRenderYaw() const;
    const DesktopGltfModel *model() const { return model_ && model_->ready() ? model_.get() : nullptr; }

    realms::MapWalker &walker() { return walker_; }
    const realms::MapWalker &walker() const { return walker_; }
    const std::string &path() const { return path_; }
    const std::string &summary() const { return summary_; }
    float unitsPerMetre() const { return unitsPerMetre_; }

    // Exposed for tests: conversion of extracted geometry to renderer
    // vertices (material layer, tint and UVs).
    static void toRenderVertices(const realms::MapGeometry &geometry, float unitsPerMetre,
                                 std::vector<sandbox::MeshVertex> &out);
    static int materialLayer(const std::string &name, int32_t surfaceFlags, int32_t contents);

private:
    bool active_ = false;
    std::string path_;
    std::string summary_;
    float unitsPerMetre_ = kBspUnitsPerMetre;
    realms::MapWalker walker_;
    std::unique_ptr<DesktopGltfModel> model_;
};

} // namespace ekron::desktop
