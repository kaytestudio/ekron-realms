// Quake 2 client effects (cl_fx.c, cl_tent.c, effect parts of cl_ents.c)
// without renderer or audio types: temp entities, muzzle flashes (MZ_* and
// MZ2_*), entity events, per-frame trails and lights, RF_BEAM lasers.
//
// Particles are evaluated from their spawn time like CL_AddParticles, so
// they don't depend on frame rate. Palette colours come from
// pics/colormap.pcx when available, otherwise from a copy of Quake 2's.
// Times are client milliseconds (clientTimeMs(server)).
//
// Not drawn: Rogue's sustained effects, heat beams, tracker, force walls.
#pragma once

#include "realms/GameServer.h"
#include "realms/GameView.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace realms {

// temp_event_t (q_shared.h), base game, Xatrix and Rogue
enum GameTempEntity {
    GTE_GUNSHOT, GTE_BLOOD, GTE_BLASTER, GTE_RAILTRAIL, GTE_SHOTGUN, GTE_EXPLOSION1, GTE_EXPLOSION2,
    GTE_ROCKET_EXPLOSION, GTE_GRENADE_EXPLOSION, GTE_SPARKS, GTE_SPLASH, GTE_BUBBLETRAIL, GTE_SCREEN_SPARKS,
    GTE_SHIELD_SPARKS, GTE_BULLET_SPARKS, GTE_LASER_SPARKS, GTE_PARASITE_ATTACK, GTE_ROCKET_EXPLOSION_WATER,
    GTE_GRENADE_EXPLOSION_WATER, GTE_MEDIC_CABLE_ATTACK, GTE_BFG_EXPLOSION, GTE_BFG_BIGEXPLOSION, GTE_BOSSTPORT,
    GTE_BFG_LASER, GTE_GRAPPLE_CABLE, GTE_WELDING_SPARKS, GTE_GREENBLOOD, GTE_BLUEHYPERBLASTER,
    GTE_PLASMA_EXPLOSION, GTE_TUNNEL_SPARKS,
    GTE_BLASTER2, GTE_RAILTRAIL2, GTE_FLAME, GTE_LIGHTNING, GTE_DEBUGTRAIL, GTE_PLAIN_EXPLOSION, GTE_FLASHLIGHT,
    GTE_FORCEWALL, GTE_HEATBEAM, GTE_MONSTER_HEATBEAM, GTE_STEAM, GTE_BUBBLETRAIL2, GTE_MOREBLOOD,
    GTE_HEATBEAM_SPARKS, GTE_HEATBEAM_STEAM, GTE_CHAINFIST_SMOKE, GTE_ELECTRIC_SPARKS, GTE_TRACKER_EXPLOSION,
    GTE_TELEPORT_EFFECT, GTE_DBALL_GOAL, GTE_WIDOWBEAMOUT, GTE_NUKEBLAST, GTE_WIDOWSPLASH, GTE_EXPLOSION1_BIG,
    GTE_EXPLOSION1_NP, GTE_FLECHETTE
};

// player muzzle flashes (MZ_*), sent with svc_muzzleflash
enum GameMuzzleFlash {
    GMZ_BLASTER = 0, GMZ_MACHINEGUN = 1, GMZ_SHOTGUN = 2, GMZ_CHAINGUN1 = 3, GMZ_CHAINGUN2 = 4, GMZ_CHAINGUN3 = 5,
    GMZ_RAILGUN = 6, GMZ_ROCKET = 7, GMZ_GRENADE = 8, GMZ_LOGIN = 9, GMZ_LOGOUT = 10, GMZ_RESPAWN = 11, GMZ_BFG = 12,
    GMZ_SSHOTGUN = 13, GMZ_HYPERBLASTER = 14, GMZ_ITEMRESPAWN = 15, GMZ_IONRIPPER = 16, GMZ_BLUEHYPERBLASTER = 17,
    GMZ_PHALANX = 18, GMZ_ETF_RIFLE = 30, GMZ_SHOTGUN2 = 32, GMZ_HEATBEAM = 33, GMZ_BLASTER2 = 34, GMZ_TRACKER = 35,
    GMZ_NUKE1 = 36, GMZ_NUKE2 = 37, GMZ_NUKE4 = 38, GMZ_NUKE8 = 39, GMZ_SILENCED = 128
};

// entity_event_t
enum GameEntityEvent {
    GEV_NONE, GEV_ITEM_RESPAWN, GEV_FOOTSTEP, GEV_FALLSHORT, GEV_FALL, GEV_FALLFAR, GEV_PLAYER_TELEPORT,
    GEV_OTHER_TELEPORT
};

// splash types (TE_SPLASH colour byte)
enum GameSplash { GSPLASH_UNKNOWN, GSPLASH_SPARKS, GSPLASH_BLUE_WATER, GSPLASH_BROWN_WATER, GSPLASH_SLIME,
                  GSPLASH_LAVA, GSPLASH_BLOOD };

// MSG_WriteDir / MSG_ReadDir: the 162 directions of anorms.h.
Vec3 gameByteDir(int index);
int gameDirToByte(const float dir[3]);

// The client clock a drawn frame interpolates at, in ms: the previous game
// frame plus lerpFraction (cl.time between two server frames).
double clientTimeMs(const GameServer &server);

struct GameParticle {
    std::array<float, 3> origin{};      // map space
    std::array<float, 4> color{};       // RGB 0..1, alpha
    float size = 1.0f;                  // 1: a Quake 2 particle; > 1: a glow (sprites)
};

struct GameDynamicLight {
    std::array<float, 3> origin{};
    float radius = 0.0f;                // units: lights what is closer than this
    std::array<float, 3> color{1, 1, 1};
};

// R_LightPoint's dynamic light term: each light adds (radius - distance) / 256
// of its colour to `rgb` at `point` (map space).
void addDynamicLights(const std::array<float, 3> &point, const std::vector<GameDynamicLight> &lights,
                      std::array<float, 3> &rgb);

// An explosion, smoke puff, muzzle flash or cable segment model.
struct GameEffectModel {
    std::string model;
    std::array<float, 3> origin{}, angles{}; // angles like an entity's (alias pitch not negated yet)
    int frame = 0, oldframe = 0;
    float backlerp = 0.0f;
    int skinnum = 0;
    float alpha = 1.0f;
    bool fullbright = false;
};

struct GameBeam {
    std::array<float, 3> start{}, end{};
    float width = 4.0f;                 // units
    std::array<float, 4> color{};
};

class GameEffects {
public:
    static constexpr int kMaxParticles = 4096; // MAX_PARTICLES
    static constexpr int kMaxDlights = 32;     // MAX_DLIGHTS
    static constexpr int kMaxExplosions = 32;  // MAX_EXPLOSIONS
    static constexpr int kMaxLasers = 32;
    static constexpr int kMaxBeams = 32;
    // How long a muzzle flash lights its surroundings. Quake 2 keeps it for
    // one client frame, which is nothing at today's frame rates.
    static constexpr double kMuzzleFlashMs = 50.0;

    GameEffects();
    // Everything off (map change, restart); `seed` makes the random spreads
    // reproducible (tests).
    void reset(uint32_t seed = 0x2545f491u);

    // A 768-byte RGB palette, or loadPalette from pics/colormap.pcx (its
    // last 768 bytes). Without either (nullptr), Quake 2's built-in palette.
    void setPalette(const uint8_t *rgb768);
    bool loadPalette(const Engine &engine, std::string *error = nullptr);
    bool paletteFromGame() const { return paletteFromGame_; }
    std::array<float, 3> paletteColor(int index) const;

    // TempEntity, MuzzleFlash and EntityEvent events (others are ignored).
    void handleEvent(const GameEvent &ev, double timeMs);
    // Once per drawn frame, after the events: trails from where each entity
    // was last drawn, its lights, flies and BFG particles. `snapshot` is
    // GameEntityLerp::snapshots(): teleporter pads sparkle once per game
    // frame like CL_FireEntityEvents. Sprite models (.sp2) get a glow.
    void addEntityEffects(const std::vector<GameLerpedEntity> &entities, int snapshot, double timeMs);
    // RF_BEAM entities to draw this frame (GameServer::beamEntities).
    void setBeamEntities(std::vector<GameBeamEntity> beams) { beamEntities_ = std::move(beams); }
    // Frees what has faded or ended by timeMs.
    void update(double timeMs);

    // What to draw at timeMs.
    void particles(double timeMs, std::vector<GameParticle> &out) const;
    void lights(double timeMs, std::vector<GameDynamicLight> &out) const;
    void models(double timeMs, std::vector<GameEffectModel> &out) const;
    // Lasers and beam entities; each picks one of its four colours per call.
    void beams(double timeMs, std::vector<GameBeam> &out);
    // The sounds the client starts (weapon fire, impacts, explosions), oldest first.
    std::vector<GameEvent> takeSounds();

    size_t particleCount() const { return particles_.size(); }
    size_t explosionCount() const;
    size_t dlightCount(double timeMs) const;
    int droppedParticles() const { return droppedParticles_; }

    // Quake 2's rand() family, exposed for the tests.
    int rand15();
    float frand() { return float(rand15() & 32767) / 32767.0f; }
    float crand() { return float(rand15() & 32767) * (2.0f / 32767.0f) - 1.0f; }

private:
    struct Particle {
        double time = 0.0;
        std::array<float, 3> org{}, vel{}, accel{};
        int color = 0;
        float alpha = 1.0f, alphavel = 0.0f;
    };
    enum class ExType { Free, Misc, Flash, Poly, Poly2 };
    struct Explosion {
        ExType type = ExType::Free;
        GameEffectModel ent;
        bool translucent = false;
        int frames = 0, baseframe = 0;
        double start = 0.0;
        float light = 0.0f;
        std::array<float, 3> lightcolor{};
    };
    struct Dlight {
        int key = 0;
        std::array<float, 3> origin{}, color{};
        float radius = 0.0f;
        double die = 0.0;
    };
    struct Laser {
        GameBeam beam;
        int colors = 0;
        double endtime = -1.0;
    };
    struct Cable { // CL_ParseBeam: parasite / medic cable, grapple
        int entity = 0;
        std::string model;
        std::array<float, 3> start{}, end{}, offset{};
        double endtime = -1.0;
    };
    struct EntityState {
        int modelindex = 0;
        std::array<float, 3> lerpOrigin{};
        int trailcount = 1024;
        double flyStop = -1.0;
        bool seen = false;
    };

    Particle *alloc(double timeMs);
    Explosion &allocExplosion();
    Dlight &allocDlight(int key, double timeMs);
    void sound(const std::string &name, int entity, const std::array<float, 3> &origin, float volume,
               float attenuation, int channel = 0, float delay = 0.0f);

    void tempEntity(const GameEvent &ev, double t);
    void muzzleFlash(const GameEvent &ev, double t);
    void monsterFlash(const GameEvent &ev, double t);
    void entityEvent(const GameEvent &ev, double t);
    void smokeAndFlash(const std::array<float, 3> &org, double t);
    void addLaser(const GameEvent &ev, int colors, double t);
    void addCable(const GameEvent &ev, const char *model, double t);
    void polyExplosion(const std::array<float, 3> &org, const char *model, int frames, int baseframe, double t);

    // cl_fx.c
    void particleEffect(const std::array<float, 3> &org, const std::array<float, 3> &dir, int color, int count,
                        double t, int variant = 1);
    void logoutEffect(const std::array<float, 3> &org, int type, double t);
    void itemRespawnParticles(const std::array<float, 3> &org, double t);
    void explosionParticles(const std::array<float, 3> &org, int color, double t);
    void bigTeleportParticles(const std::array<float, 3> &org, double t);
    void blasterParticles(const std::array<float, 3> &org, const std::array<float, 3> &dir, int color, double t);
    void blasterTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, double t);
    void flagTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, int color, double t);
    void diminishingTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, EntityState &old,
                          uint32_t flags, double t);
    void rocketTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, EntityState &old, double t);
    void railTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, double t);
    void bubbleTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, double t);
    void flyEffect(EntityState &state, const std::array<float, 3> &origin, double t);
    void flyParticles(const std::array<float, 3> &origin, int count, double t);
    void bfgParticles(const std::array<float, 3> &origin, double t);
    void teleporterParticles(const std::array<float, 3> &origin, double t);
    void teleportParticles(const std::array<float, 3> &org, double t);
    void ionripperTrail(const std::array<float, 3> &start, const std::array<float, 3> &end, double t);
    void trapParticles(std::array<float, 3> origin, double t);
    void frameLight(const std::array<float, 3> &origin, float radius, float r, float g, float b);

    uint32_t rng_ = 0x2545f491u;
    std::array<std::array<float, 3>, 256> palette_{};
    bool paletteFromGame_ = false;
    std::vector<Particle> particles_;
    int droppedParticles_ = 0;
    std::array<Explosion, kMaxExplosions> explosions_{};
    std::array<Dlight, kMaxDlights> dlights_{};
    std::array<Laser, kMaxLasers> lasers_{};
    std::array<Cable, kMaxBeams> cables_{};
    std::vector<GameDynamicLight> frameLights_;  // V_AddLight from entity effects, this frame
    std::vector<GameParticle> frameGlows_;       // sprite entities, this frame
    std::vector<GameBeamEntity> beamEntities_;
    std::unordered_map<int, EntityState> entities_;
    std::array<std::array<float, 3>, 162> avelocities_{}; // flies / BFG particle spin
    bool avelocitiesSet_ = false;
    int lastSnapshot_ = -1;
    std::vector<GameEvent> sounds_;
};

// Explosion, smoke, flash and cable models as batches of a GameScene
// (built with buildGameScene): .md2 from the cache, posed like an entity;
// a missing model is simply not drawn (the particles and light remain).
void appendEffectModels(const Engine &engine, GameModelCache &models, const std::vector<GameEffectModel> &effects,
                        GameScene &scene);

} // namespace realms
