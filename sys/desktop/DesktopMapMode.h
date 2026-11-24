// "map <name>" on desktop: draws mapview::Quake2View with DesktopRenderer
// and runs the game when the module is available.
#pragma once

#include "DesktopRenderer.h"
#include "Quake2View.h"

#include "realms/Engine.h"
#include "realms/Quake2Module.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace ekron::desktop {

class DesktopMapMode {
public:
    static constexpr float kBspUnitsPerMetre = mapview::kBspUnitsPerMetre;

    // Builds and uploads the loaded map's geometry and spawns the player
    // (game server or walker). On failure the mode stays inactive and
    // `error` says why. `executableDir` is where librealms_game is looked for.
    bool open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error,
              const std::string &executableDir = {});
    void close(ekron::gl::Api &gl, DesktopRenderer &renderer);
    bool active() const { return view_.active(); }

    // One frame; yaw / pitch are the renderer's camera angles (radians,
    // yaw 0 = -Z, positive pitch looks up). `attack` fires the game's weapon.
    void update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch, float dt,
                bool attack = false);
    bool respawn(realms::Engine &engine);
    void toggleNoclip();
    bool noclip() const;

    physics::Vec3 eye() const;
    // Camera yaw (renderer convention) facing the spawn point's angle.
    float spawnRenderYaw() const;
    const DesktopGltfModel *model() const { return model_ && model_->ready() ? model_.get() : nullptr; }
    // Inline model poses for DesktopRenderer::setMapModelPoses (empty without a game).
    const std::vector<DesktopRenderer::MapModelPose> &modelPoses() const { return poses_; }
    // Surface effects for DesktopRenderer::setMapSurfaces (updated by update()).
    const DesktopRenderer::MapSurfaces &surfaces() const { return surfaces_; }

    realms::MapWalker &walker() { return view_.session().walker(); }
    const realms::MapWalker &walker() const { return view_.session().walker(); }
    const std::string &path() const { return view_.session().path(); }
    const std::string &summary() const { return view_.session().summary(); }
    float unitsPerMetre() const { return view_.session().unitsPerMetre(); }

    // ---- the Quake 2 module
    mapview::Quake2View &view() { return view_; }
    realms::Quake2Module &quake2() { return view_.module(); }
    const realms::Quake2Module &quake2() const { return view_.module(); }
    bool gameRunning() const { return view_.gameRunning(); }
    const realms::GameServer *game() const { return quake2().game(); }
    // "HP 100  Armor 2  Ammo 50" for the HUD ("" without a game).
    std::string gameStatus() const { return quake2().status(); }
    // The game's screen blend (damage, pickups, powerups, under water), drawn
    // over the 3D view like ref_gl's R_PolyBlend; all zero with gl_polyblend 0.
    std::array<float, 4> screenBlend(realms::Engine &engine) const {
        if (!engine.cvarBool("gl_polyblend", true)) return {};
        return quake2().stats().blend;
    }
    // Centre prints / pickup messages since the last call (for toasts).
    std::vector<std::string> takeGameMessages() { return view_.takeMessages(); }
    // The game's entities for DesktopRenderer::setMapEntities: .md3 models
    // from the VFS with their TGA / PCX skins (uploaded once, kept until close),
    // coloured boxes where a model is missing, and the view weapon glued to
    // the camera (renderer yaw / pitch, radians). Empty without a game.
    void buildEntities(realms::Engine &engine, ekron::gl::Api &gl, float renderYaw, float renderPitch,
                       std::vector<DesktopRenderer::EntityVertex> &vertices,
                       std::vector<DesktopRenderer::EntityBatch> &batches);
    // The client effects for DesktopRenderer::setMapEffects: particles and
    // glows as camera-facing quads, laser beams, and the dynamic lights
    // closest to the camera first. Explosion / smoke / cable models come
    // with buildEntities. Empty without a game.
    void buildEffects(float renderYaw, float renderPitch, std::vector<DesktopRenderer::EffectVertex> &vertices,
                      std::vector<DesktopRenderer::DynamicLight> &lights);
    // Sounds to start since the last call and the entity loop sounds now,
    // for ekron::audio::GameSoundPlayer; the listener is the game's view.
    std::vector<realms::GameEvent> takeGameSounds() { return quake2().takeSounds(); }
    std::vector<realms::GameLoopSound> loopSounds() const { return quake2().loopSounds(); }
    realms::GameListener listener() const { return quake2().listener(); }
    const realms::GameEffects &effects() const { return quake2().effects(); }

    // ---- lightmaps (BSP maps lit by qrad3)
    // Light styles now (the game's CS_LIGHTS at client time, or all normal
    // without a game); re-uploads the atlas when a style used by the map
    // changed, and follows r_lightmaps. Call once per frame before drawing.
    void updateLighting(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer);
    const realms::MapLighting &lighting() const { return quake2().lighting(); }
    bool lightAt(const realms::Vec3 &mapPoint, std::array<float, 3> &rgb) const {
        return quake2().lightAt(mapPoint, rgb);
    }

private:
    void syncPoses();
    void releaseSkins(ekron::gl::Api &gl);
    GLuint skinTexture(realms::Engine &engine, ekron::gl::Api &gl, const std::string &name);

    mapview::Quake2View view_;
    std::unique_ptr<DesktopGltfModel> model_;
    std::vector<DesktopRenderer::MapModelPose> poses_;
    DesktopRenderer::MapSurfaces surfaces_;
    size_t modelCount_ = 1; // world + inline models drawn from the map geometry
    std::unordered_map<std::string, GLuint> skins_; // 0: tried, not loadable
    std::vector<mapview::EntityVertex> entityVertices_;
    std::vector<mapview::EntityBatch> entityBatches_;
    std::vector<mapview::EffectVertex> effectVertices_;
    std::vector<mapview::DynamicLight> lights_;
};

} // namespace ekron::desktop
