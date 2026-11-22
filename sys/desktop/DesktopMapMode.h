// Realms map mode for the desktop client: "map <name>" swaps the sandbox for
// a Quake 2 BSP or a glTF map loaded into the Realms core and drawn by
// DesktopRenderer::renderMapWorld. The renderer-independent part (spaces,
// walker, material layers) is mapview::MapSession, shared with Android.
//
// When the Quake 2 game module (librealms_game, game/) is available and the
// map has a worldspawn entity string, the map runs a realms::GameServer:
// the player is the game's client (ClientThink / Pmove), doors, platforms
// and other brush entities move as the game drives them, items can be
// picked up, and the HUD shows the game's player stats. Otherwise (or with
// sv_game 0) the map is walked with realms::MapWalker as before.
#pragma once

#include "DesktopRenderer.h"
#include "MapSession.h"

#include "realms/Engine.h"
#include "realms/GameEffects.h"
#include "realms/GameServer.h"
#include "realms/GameSound.h"
#include "realms/GameView.h"
#include "realms/MapLighting.h"

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
    bool active() const { return session_.active(); }

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

    realms::MapWalker &walker() { return session_.walker(); }
    const realms::MapWalker &walker() const { return session_.walker(); }
    const std::string &path() const { return session_.path(); }
    const std::string &summary() const { return session_.summary(); }
    float unitsPerMetre() const { return session_.unitsPerMetre(); }

    // ---- game server
    bool gameRunning() const { return game_ && game_->running(); }
    const realms::GameServer *game() const { return gameRunning() ? game_.get() : nullptr; }
    // "HP 100  Armor 2  Ammo 50" for the HUD ("" without a game).
    std::string gameStatus() const;
    // Centre prints / pickup messages since the last call (for toasts).
    std::vector<std::string> takeGameMessages();
    // The game's entities for DesktopRenderer::setMapEntities: .md2 models
    // from the VFS with their PCX skins (uploaded once, kept until close),
    // coloured boxes where a model is missing, and the view weapon glued to
    // the camera (renderer yaw / pitch, radians). Empty without a game.
    void buildEntities(realms::Engine &engine, ekron::gl::Api &gl, float renderYaw, float renderPitch,
                       std::vector<DesktopRenderer::EntityVertex> &vertices,
                       std::vector<DesktopRenderer::EntityBatch> &batches);
    // The client effects (realms::GameEffects) for DesktopRenderer::setMapEffects:
    // particles and glows as camera-facing quads, laser beams, and the
    // dynamic lights closest to the camera first. Explosion / smoke / cable
    // models come with buildEntities. Empty without a game.
    void buildEffects(float renderYaw, float renderPitch, std::vector<DesktopRenderer::EffectVertex> &vertices,
                      std::vector<DesktopRenderer::DynamicLight> &lights);
    // Sounds to start since the last call (the game's, and the effects'
    // muzzle flash / impact sounds) and the entity loop sounds now, for
    // DesktopGameSound; the listener is the game's view.
    std::vector<realms::GameEvent> takeGameSounds();
    std::vector<realms::GameLoopSound> loopSounds() const;
    realms::GameListener listener() const;
    const realms::GameEffects &effects() const { return effects_; }

    // ---- lightmaps (BSP maps lit by qrad3)
    // Light styles now (the game's CS_LIGHTS at client time, or all normal
    // without a game); re-uploads the atlas when a style used by the map
    // changed, and follows r_lightmaps. Call once per frame before drawing.
    void updateLighting(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer);
    const realms::MapLighting &lighting() const { return lighting_; }
    // The light (R_LightPoint, 0..~2 per channel) at a map point, never
    // darker than Quake 2's RF_MINLIGHT 0.1. False without lightmaps.
    bool lightAt(const realms::Vec3 &mapPoint, std::array<float, 3> &rgb) const;

private:
    bool startGame(realms::Engine &engine, std::string &why);
    void syncFromGame();

    mapview::MapSession session_;
    std::unique_ptr<DesktopGltfModel> model_;
    std::unique_ptr<realms::GameServer> game_;
    std::string executableDir_;
    std::vector<DesktopRenderer::MapModelPose> poses_;
    std::vector<std::string> messages_;
    realms::GameEntityLerp lerp_;
    realms::GameModelCache models_;
    realms::GameScene scene_;
    std::unordered_map<std::string, GLuint> skins_; // 0: tried, not loadable
    void releaseSkins(ekron::gl::Api &gl);
    GLuint skinTexture(realms::Engine &engine, ekron::gl::Api &gl, const std::string &name);
    int lastPickup_ = 0;
    realms::GameEffects effects_;
    std::vector<realms::GameEvent> gameSounds_;
    std::vector<realms::GameEffectModel> fxModels_;
    std::vector<realms::GameParticle> fxParticles_;
    std::vector<realms::GameDynamicLight> fxLights_;
    std::vector<realms::GameBeam> fxBeams_;
    realms::MapLighting lighting_;
    realms::MapLightStyles lightStyles_ = realms::defaultLightStyles();
    std::vector<uint8_t> lightmapRgba_;
    realms::MapLightStyles currentLightStyles() const;
};

} // namespace ekron::desktop
