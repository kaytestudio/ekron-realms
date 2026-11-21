// Realms map mode for the desktop client: "map <name>" swaps the sandbox for
// a Quake 2 BSP or a glTF map loaded into the Realms core, walked with
// realms::MapWalker (collision by the Pascal / C++ CModel) and drawn by
// DesktopRenderer::renderMapWorld. The renderer-independent part (spaces,
// walker, material layers) is mapview::MapSession, shared with Android.
#pragma once

#include "DesktopRenderer.h"
#include "MapSession.h"

#include "realms/Engine.h"

#include <memory>
#include <string>

namespace ekron::desktop {

class DesktopMapMode {
public:
    static constexpr float kBspUnitsPerMetre = mapview::kBspUnitsPerMetre;

    // Builds and uploads the loaded map's geometry and spawns the walker.
    // On failure the mode stays inactive and `error` says why.
    bool open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error);
    void close(ekron::gl::Api &gl, DesktopRenderer &renderer);
    bool active() const { return session_.active(); }

    // Walk one frame; yaw / pitch are the renderer's camera angles (radians,
    // yaw 0 = -Z, positive pitch looks up).
    void update(const realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                float dt) {
        session_.update(engine, input, renderYaw, renderPitch, dt);
    }
    bool respawn(const realms::Engine &engine) { return session_.respawn(engine); }

    physics::Vec3 eye() const { return session_.eye(); }
    // Camera yaw (renderer convention) facing the spawn point's angle.
    float spawnRenderYaw() const { return session_.spawnRenderYaw(); }
    const DesktopGltfModel *model() const { return model_ && model_->ready() ? model_.get() : nullptr; }

    realms::MapWalker &walker() { return session_.walker(); }
    const realms::MapWalker &walker() const { return session_.walker(); }
    const std::string &path() const { return session_.path(); }
    const std::string &summary() const { return session_.summary(); }
    float unitsPerMetre() const { return session_.unitsPerMetre(); }

private:
    mapview::MapSession session_;
    std::unique_ptr<DesktopGltfModel> model_;
};

} // namespace ekron::desktop
