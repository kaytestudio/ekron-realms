// Realms Engine - the Quake 2 stack as one module for the sandboxes.
//
// realms::Quake2Module is everything a sandbox needs to play a Quake 2 map,
// without any renderer, window or audio API in it. The desktop client
// (DesktopMapMode), Android (AndroidMapMode) and iOS (MetalMapMode) each own
// one and only draw / play what it hands out:
//
//   * the game: realms::GameServer running librealms_game (game/, the Free
//     Pascal gamex86) on the map the Engine has loaded, fed with the player's
//     GameUserCmd every frame (or a linked GetGameAPI on platforms that can't
//     load libraries, see GameServer::setLinkedGame);
//   * brush entities: where each inline model ("*n") is drawn this frame,
//     interpolated between game frames, hidden when nothing uses it;
//   * entities: .md2 models, fallback boxes, the view weapon and the effect
//     models as one GameScene, each batch lit from the lightmaps;
//   * client effects: particles / glows / beams as camera-facing quads and
//     the dynamic lights closest to the camera (GameEffects);
//   * sounds: the game's and the effects' sounds to start, the entity loop
//     sounds and the listener (GameSound);
//   * lightmaps: the atlas, per-vertex atlas coordinates for MapGeometry and
//     light styles animated from the game's CS_LIGHTS (MapLighting).
//
// Everything is in Quake 2 map space (units, Z up, Quake 2 angles in
// degrees); each sandbox converts to its own render space.
#pragma once

#include "realms/Engine.h"
#include "realms/GameEffects.h"
#include "realms/GameServer.h"
#include "realms/GameSound.h"
#include "realms/GameView.h"
#include "realms/MapGeometry.h"
#include "realms/MapLighting.h"
#include "realms/MapWalker.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace realms {

struct Quake2ModuleConfig {
    std::string executableDir; // where librealms_game is looked for
    bool startGame = true;     // false: lightmaps only (sv_game 0)
    bool lightmaps = true;
};

// Where an inline model is drawn: rotate by `angles` (Quake 2 degrees,
// R_RotateForEntity) about the map origin, then translate by `origin`.
struct Quake2ModelPose {
    bool visible = true;
    std::array<float, 3> origin{}, angles{};
};

// One corner of an effect quad, map space. shape 0: a round particle with
// (u, v) in -1..1 across it; 1: a solid quad (beams).
struct Quake2EffectVertex {
    float x, y, z, r, g, b, a, u, v, shape;
};

class Quake2Module {
public:
    Quake2Module();
    ~Quake2Module();
    Quake2Module(const Quake2Module &) = delete;
    Quake2Module &operator=(const Quake2Module &) = delete;

    // The engine's loaded map: lightmaps (BSP face geometry), then the game
    // when it is wanted, available and the map starts with a worldspawn.
    // `geometry` is what the sandbox draws (from loadMapGeometry). Never
    // fails: without a game the sandbox walks the map itself; `why` says
    // why the game isn't running ("" when it is, or wasn't wanted).
    void open(Engine &engine, const MapGeometry &geometry, const Quake2ModuleConfig &config, std::string &why);
    void close();
    // Restarts the game on the same map (respawn); false + why on failure.
    bool restartGame(Engine &engine, std::string &why);

    // ---- game
    bool gameRunning() const;
    const GameServer *game() const;
    GameServer *game();
    // The game stopped by itself (game error) since the last call: its error.
    std::string takeGameFailure();
    // One frame of the game with this command, then everything above is
    // brought up to date. No-op without a game.
    void frame(const GameUserCmd &cmd, double dt);
    // A GameUserCmd from walk input (-1..1 axes, Quake 2 angles): Quake 2's
    // always-run speeds (cl_forwardspeed / cl_sidespeed / cl_upspeed 200, x2).
    static GameUserCmd userCmd(const WalkInput &mapInput, bool attack);

    Vec3 viewOrigin() const;
    Vec3 viewAngles() const;
    bool noclip() const;
    void toggleNoclip();
    // The walker mirrors the game's player (HUD, sound director, saves).
    void mirrorPlayer(MapWalker &walker) const;
    // "HP 100  Armor 2  Ammo 50" ("" without a game).
    std::string status() const;
    struct PlayerStats {
        int health = 0, armor = 0, ammo = 0;
    };
    PlayerStats stats() const;
    // Centre prints / pickups since the last call.
    std::vector<std::string> takeMessages();

    // ---- drawing (map space)
    // Indexed by inline model number (0 unused); empty without a game.
    const std::vector<Quake2ModelPose> &modelPoses() const { return poses_; }
    // Entities, view weapon and effect models for a camera (map origin,
    // Quake 2 angles). Batches not RF_FULLBRIGHT carry the light under them
    // when the map has lightmaps (GameDrawBatch::lit / light).
    const GameScene &buildScene(Engine &engine, const Vec3 &cameraOrigin, const Vec3 &cameraAngles, bool viewWeapon);
    const GameScene &scene() const { return scene_; }
    // Particle / glow / beam quads (6 vertices each) facing the camera, and
    // up to `maxLights` dynamic lights, the closest to the camera first.
    void buildEffects(const Vec3 &cameraOrigin, const Vec3 &cameraAngles, size_t maxLights,
                      std::vector<Quake2EffectVertex> &quads, std::vector<GameDynamicLight> &lights);
    const GameEffects &effects() const { return effects_; }

    // ---- sound
    std::vector<GameEvent> takeSounds();
    std::vector<GameLoopSound> loopSounds() const;
    GameListener listener() const;

    // ---- lightmaps
    const MapLighting &lighting() const { return lighting_; }
    bool lightmapped() const { return !lighting_.empty(); }
    // Drops the lightmaps (the sandbox can't use them: no BSP faces drawn).
    void disableLightmaps();
    // Atlas coordinates (u, v per vertex, -1 = no lightmap) for `geometry`'s
    // vertices in batch order; false (and empty) without lightmaps.
    bool lightmapUvs(const MapGeometry &geometry, std::vector<float> &uvs) const;
    // The RGBA8 atlas (lighting().width() x height()) for the light styles now.
    const std::vector<uint8_t> &lightmap() const { return lightmapRgba_; }
    // Light styles from the game's CS_LIGHTS at client time; true when the
    // atlas changed (re-upload lightmap()).
    bool updateLightStyles();
    // R_LightPoint at a map point, at least RF_MINLIGHT's 0.1; false (and
    // white) without lightmaps.
    bool lightAt(const Vec3 &point, std::array<float, 3> &rgb) const;

private:
    bool startGame(Engine &engine, std::string &why);
    void sync();
    MapLightStyles currentLightStyles() const;

    std::string executableDir_;
    std::unique_ptr<GameServer> game_;
    std::string failure_;
    std::vector<Quake2ModelPose> poses_;
    std::vector<std::string> messages_;
    int lastPickup_ = 0;
    GameEntityLerp lerp_;
    GameModelCache models_;
    GameScene scene_;
    GameEffects effects_;
    std::vector<GameEvent> sounds_;
    std::vector<GameEffectModel> fxModels_;
    std::vector<GameParticle> fxParticles_;
    std::vector<GameBeam> fxBeams_;
    MapLighting lighting_;
    MapLightStyles lightStyles_;
    std::vector<uint8_t> lightmapRgba_;
};

} // namespace realms
