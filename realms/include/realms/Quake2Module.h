// Quake 2 game, entities, effects, sounds, lightmaps and textures for one
// map, with no renderer or audio code. Each sandbox (DesktopMapMode,
// AndroidMapMode, MetalMapMode) owns one and draws/plays what it returns.
// Everything is in Quake 2 map space: units, Z up, angles in degrees.
#pragma once

#include "realms/Engine.h"
#include "realms/GameEffects.h"
#include "realms/GameServer.h"
#include "realms/GameSound.h"
#include "realms/GameView.h"
#include "realms/MapGeometry.h"
#include "realms/MapLighting.h"
#include "realms/MapTextures.h"
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
    bool textures = true; // the map's .wal textures (MapTextures)
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
    // ---- textures
    // The .wal textures of the map's faces (empty without id's data).
    const MapTextures &textures() const { return textures_; }
    void disableTextures() { textures_.clear(); }

    // R_LightPoint at a map point: the lightmap under it plus the game's
    // dynamic lights, at least RF_MINLIGHT's 0.1; false (and white) without
    // lightmaps.
    bool lightAt(const Vec3 &point, std::array<float, 3> &rgb) const;

private:
    bool startGame(Engine &engine, std::string &why);
    void sync();
    MapLightStyles currentLightStyles() const;
    bool lightAt(const Vec3 &point, const std::vector<GameDynamicLight> &dlights, std::array<float, 3> &rgb) const;

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
    std::vector<GameDynamicLight> fxLights_;
    MapLighting lighting_;
    MapTextures textures_;
    MapLightStyles lightStyles_;
    std::vector<uint8_t> lightmapRgba_;
};

} // namespace realms
