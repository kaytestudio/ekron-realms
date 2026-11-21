// Realms Engine - player movement on the loaded map.
//
// A compact port of Quake 2's pmove (PM_StepSlideMove / PM_Friction /
// PM_Accelerate / PM_CheckDuck) running on Engine::boxTrace, so it walks
// identically on the Pascal and the C++ core, on BSP and glTF mesh maps.
// Units are map units (Quake 2: 32 units per metre); angles are degrees with
// Quake 2 conventions (yaw 0 = +X, 90 = +Y; positive pitch looks down).
#pragma once

#include "realms/Engine.h"

#include <cstdint>
#include <string>

namespace realms {

struct WalkInput {
    float forward = 0.0f; // -1..1
    float right = 0.0f;   // -1..1
    float up = 0.0f;      // -1..1 (noclip / swimming)
    bool jump = false;
    bool crouch = false;
    float yaw = 0.0f;     // degrees
    float pitch = 0.0f;   // degrees, used by noclip and swimming
};

struct WalkSettings {
    float gravity = 800.0f;       // sv_gravity
    float maxSpeed = 300.0f;      // pm_maxspeed
    float duckSpeed = 100.0f;     // pm_duckspeed
    float waterSpeed = 400.0f;    // pm_waterspeed
    float accelerate = 10.0f;     // pm_accelerate
    float airAccelerate = 1.0f;   // PM_Accelerate(.., 1) in the air
    float waterAccelerate = 10.0f;
    float friction = 6.0f;        // pm_friction
    float waterFriction = 1.0f;
    float stopSpeed = 100.0f;     // pm_stopspeed
    float jumpSpeed = 270.0f;
    float stepSize = 18.0f;       // STEPSIZE
    float noclipSpeed = 600.0f;
    float maxStep = 0.05f;        // seconds per pmove step
};

class MapWalker {
public:
    // Player hull (g_local.h / p_client.c).
    static constexpr float kMinsZ = -24.0f, kStandMaxsZ = 32.0f, kDuckMaxsZ = 4.0f, kHalfWidth = 16.0f;
    static constexpr float kStandView = 22.0f, kDuckView = -2.0f;

    WalkSettings settings;
    Vec3 origin{};
    Vec3 velocity{};
    bool onGround = false;
    bool ducked = false;
    bool noclip = false;
    int32_t waterLevel = 0; // 0 dry, 1 feet, 2 waist, 3 eyes (PM_CatagorizePosition)
    float spawnYaw = 0.0f;
    std::string spawnSource; // "info_player_start", "open floor", "map centre", ...

    // Places the walker at info_player_start (falling back to
    // info_player_deathmatch / coop), nudged up out of solids like
    // SelectSpawnPoint's +9. Maps without a spawn entity get the spot with
    // the longest walkable runs on the map's ground level (so a street beats
    // a rooftop and the open yard beats the hut at the map's centre); the
    // centre / top only when no such floor exists. False without a map.
    bool spawn(const Engine &engine);
    // Advances by dt seconds (split into steps of settings.maxStep).
    void move(const Engine &engine, const WalkInput &input, float dt);

    Vec3 mins() const { return {-kHalfWidth, -kHalfWidth, kMinsZ}; }
    Vec3 maxs() const { return {kHalfWidth, kHalfWidth, ducked ? kDuckMaxsZ : kStandMaxsZ}; }
    float viewHeight() const { return ducked ? kDuckView : kStandView; }
    Vec3 eye() const { return {origin[0], origin[1], origin[2] + viewHeight()}; }
    // True when the hull at `at` is inside a solid (MASK_PLAYERSOLID).
    bool stuck(const Engine &engine, const Vec3 &at) const;

private:
    bool findOpenFloor(const Engine &engine, Vec3 &out) const;
    void step(const Engine &engine, const WalkInput &input, float dt);
    void categorize(const Engine &engine);
    void checkDuck(const Engine &engine, bool wantDuck);
    void slideMove(const Engine &engine, float dt);
    void stepSlideMove(const Engine &engine, float dt);
    TraceResult trace(const Engine &engine, const Vec3 &start, const Vec3 &end) const;
    bool jumpHeld_ = false;
};

} // namespace realms
