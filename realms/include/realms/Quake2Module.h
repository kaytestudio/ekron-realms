// Quake 2 game, entities, effects, sounds, lightmaps, textures and sky for
// one map, with no renderer or audio code. Each sandbox (DesktopMapMode,
// AndroidMapMode, MetalMapMode) owns one and draws/plays what it returns.
// Everything is in Quake 2 map space: units, Z up, angles in degrees.
#pragma once

#include "realms/Engine.h"
#include "realms/GameClient.h"
#include "realms/GameDemo.h"
#include "realms/GameEffects.h"
#include "realms/GameLayout.h"
#include "realms/GameServer.h"
#include "realms/GameSound.h"
#include "realms/GameView.h"
#include "realms/MapGeometry.h"
#include "realms/MapLighting.h"
#include "realms/MapSky.h"
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
    bool sky = true;      // the worldspawn's skybox (MapSky)
};

// Where an inline model is drawn: rotate by `angles` (Quake 2 degrees,
// R_RotateForEntity) about the map origin, then translate by `origin`.
struct Quake2ModelPose {
    bool visible = true;
    std::array<float, 3> origin{}, angles{};
    int frame = 0; // the entity's frame (EF_ANIM_* applied): its texture animation frame
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
    // Restarts the game on the same map (respawn; it ends a playing demo);
    // false + why on failure.
    bool restartGame(Engine &engine, std::string &why);

    // ---- game
    // The game or a demo runs: there is a game to draw and play. Everything
    // below reads the client (client()), so it works the same for both.
    bool gameRunning() const;
    // The server, while the game itself runs (nullptr during a demo).
    const GameServer *game() const;
    GameServer *game();
    // The client's state (realms/GameClient.h): what the sandbox draws and
    // plays comes from it, made from the server's snapshot every frame.
    const GameClient &client() const { return client_; }
    // The game stopped by itself (game error) since the last call: its error.
    std::string takeGameFailure();
    // One frame of the game with this command, then everything above is
    // brought up to date. No-op without a game.
    void frame(const GameUserCmd &cmd, double dt);
    // A GameUserCmd from walk input (-1..1 axes, Quake 2 angles): Quake 2's
    // always-run speeds (cl_forwardspeed / cl_sidespeed / cl_upspeed 200, x2).
    static GameUserCmd userCmd(const WalkInput &mapInput, bool attack);

    // The view the client draws (CL_CalcViewValues): the player's origin plus
    // the interpolated ps.viewoffset (view height, bob, fall).
    Vec3 viewOrigin() const;
    Vec3 viewAngles() const; // the server's: ps.viewangles + kick_angles
    // The camera's angles (Quake 2 degrees) for the player's `look`: plus the
    // interpolated kick angles (weapon kick, damage, fall, run and bob pitch
    // and roll), or the game's death view while dead. `look` without a game.
    Vec3 cameraAngles(const Vec3 &look) const;
    // Yamagi Quake II's cl_kickangles: false drops the kick angles from
    // cameraAngles (the death view stays). On by default.
    void setKickAngles(bool on) { kickAngles_ = on; }
    bool kickAngles() const { return kickAngles_; }
    // CS_CDTRACK: the map's CD track (worldspawn "sounds"), 0 for none or
    // without a game.
    int cdTrack() const;
    bool noclip() const;
    void toggleNoclip();
    // The walker mirrors the game's player (HUD, sound director, saves).
    void mirrorPlayer(MapWalker &walker) const;
    // "HP 100  Armor 2  Ammo 50" ("" without a game).
    std::string status() const;
    struct PlayerStats {
        int health = 0, armor = 0, ammo = 0;
        // The game's full-screen blend (player_state_t::blend, set by
        // SV_CalcBlend in p_view.c): damage flash, item pickups, powerups,
        // under water. r, g, b, a in 0..1; a = 0 draws nothing. Clients
        // draw it over the 3D view, under the HUD, as ref_gl's R_PolyBlend
        // does, unless gl_polyblend is 0.
        std::array<float, 4> blend{};
        // player_state_t::fov: the game's field of view, from the fov cvar
        // (userinfo), clamped by realms::clampFov; 90 without a game.
        float fov = 90.0f;
    };
    PlayerStats stats() const;
    // The fov map mode draws with: the game's (stats().fov) while it runs,
    // else the fov cvar. realms::mapFocalLength turns it into the focal length.
    float viewFov(const Engine &engine) const;
    // Centre prints / pickups since the last call.
    std::vector<std::string> takeMessages();
    // Quake 2's layout screens (realms/GameLayout.h), in screen pixels for a
    // width x height screen; empty without a game. layoutItems: the last
    // svc_layout (help computer, scores) while the game shows it
    // (STAT_LAYOUTS & 1). statusBarItems: the CS_STATUSBAR program.
    std::vector<GameLayoutItem> layoutItems(int width, int height) const;
    std::vector<GameLayoutItem> statusBarItems(int width, int height) const;
    bool layoutShown() const;
    // F1 in Quake 2 ("cmd help"): open or close the help computer.
    void toggleHelp();
    // The inventory screen (Tab, "inven") while the game shows it
    // (STAT_LAYOUTS & 2): the owned items from the last svc_inventory around
    // the selected one; `cursor` draws its blinking marker.
    bool inventoryShown() const;
    std::vector<GameLayoutItem> inventoryItems(int width, int height, bool cursor) const;
    // A Quake 2 client command for the game ("inven", "invnext", "invuse"...).
    void clientCommand(const std::string &line);

    // ---- demos (realms/GameDemo.h)
    // Quake 2's "record <name>": what the client gets from now on goes to
    // <writable root>/demos/<name>.ekd, starting with everything it holds.
    // Only while the game runs (not during a demo); false + why.
    bool startRecording(Engine &engine, const std::string &name, std::string &why);
    // "stop": closes the demo file ("Stopped demo: ..." on the console);
    // false when not recording. A map change or the game stopping stops it too.
    bool stopRecording();
    bool recording() const { return recorder_.isOpen(); }
    // A demo plays instead of the game: open() plays the one demomap named
    // (the NOSET cvar kGameDemoCvar) on its map. Records are applied at
    // the pace they were recorded (frame()'s dt); the walker and the
    // player's look are ignored and the camera follows the recorded player.
    // At its end (or stopDemo) the map is walked as without a game.
    bool demoPlaying() const { return demo_ != nullptr; }
    void stopDemo();

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
    // The skybox (empty without its six env/ images).
    const MapSky &sky() const { return sky_; }
    void disableSky() { sky_.clear(); }

    // R_LightPoint at a map point: the lightmap under it plus the game's
    // dynamic lights, at least RF_MINLIGHT's 0.1; false (and white) without
    // lightmaps.
    bool lightAt(const Vec3 &point, std::array<float, 3> &rgb) const;

private:
    bool serverRunning() const { return game_ && game_->running(); }
    bool startGame(Engine &engine, std::string &why);
    bool startDemo(Engine &engine, const std::string &file, std::string &why);
    void advanceDemo(double dt);
    void finishDemo();
    void resetClientSide();
    // The server's snapshot for this frame: recorded, then received.
    void sync(float seconds);
    // One snapshot (from the server or a demo) into the client, then
    // everything the module derives from it brought up to date.
    void receive(GameSnapshot snapshot);
    MapLightStyles currentLightStyles() const;
    bool lightAt(const Vec3 &point, const std::vector<GameDynamicLight> &dlights, std::array<float, 3> &rgb) const;

    std::string executableDir_;
    Engine *engine_ = nullptr; // open()'s, for console messages
    std::unique_ptr<GameServer> game_;
    GameClient client_;
    GameDemoWriter recorder_;
    std::string recordFile_; // "demos/<name>.ekd", for messages
    std::unique_ptr<GameDemoReader> demo_;
    GameSnapshot demoNext_;   // read, not yet due
    bool demoHasNext_ = false;
    bool demoAtEnd_ = false;  // nothing more to read: the demo ends next frame
    double demoClock_ = 0.0;  // seconds played
    double demoDue_ = 0.0;    // when demoNext_ is due
    std::string failure_;
    std::vector<Quake2ModelPose> poses_;
    std::vector<std::string> messages_;
    int lastPickup_ = 0;
    std::string layout_; // the last svc_layout
    GameEntityLerp lerp_;
    bool kickAngles_ = true;
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
    MapSky sky_;
    MapLightStyles lightStyles_;
    std::vector<uint8_t> lightmapRgba_;
};

} // namespace realms
