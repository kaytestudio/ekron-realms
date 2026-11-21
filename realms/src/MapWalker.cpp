// Realms Engine - player movement on the loaded map (see MapWalker.h).
#include "realms/MapWalker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Bit-exact with the Free Pascal core: no fused multiply-add contraction
// (clang contracts a*b+c by default on arm64 / FMA targets).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace realms {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kStopEpsilon = 0.1f;   // PM_ClipVelocity
constexpr float kMinStepNormal = 0.7f; // MIN_STEP_NORMAL
constexpr int kMaxClipPlanes = 5;

float dot3(const Vec3 &a, const Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross3(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 madd(const Vec3 &a, float s, const Vec3 &b) { return {a[0] + s * b[0], a[1] + s * b[1], a[2] + s * b[2]}; }

void clipVelocity(Vec3 &v, const Vec3 &normal, float overbounce) {
    const float backoff = dot3(v, normal) * overbounce;
    for (int i = 0; i < 3; ++i) {
        v[size_t(i)] -= normal[size_t(i)] * backoff;
        if (v[size_t(i)] > -kStopEpsilon && v[size_t(i)] < kStopEpsilon) v[size_t(i)] = 0.0f;
    }
}

void accelerate(Vec3 &velocity, const Vec3 &wishdir, float wishspeed, float accel, float dt) {
    const float current = dot3(velocity, wishdir);
    const float add = wishspeed - current;
    if (add <= 0.0f) return;
    const float speed = std::min(accel * dt * wishspeed, add);
    for (int i = 0; i < 3; ++i) velocity[size_t(i)] += speed * wishdir[size_t(i)];
}

bool parseVec(const std::string &text, Vec3 &out) {
    float x = 0, y = 0, z = 0;
    if (std::sscanf(text.c_str(), "%f %f %f", &x, &y, &z) != 3) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    out = {x, y, z};
    return true;
}

} // namespace

TraceResult MapWalker::trace(const Engine &engine, const Vec3 &start, const Vec3 &end) const {
    return engine.boxTrace(start, end, mins(), maxs(), 0, REALMS_MASK_PLAYERSOLID);
}

bool MapWalker::stuck(const Engine &engine, const Vec3 &at) const { return trace(engine, at, at).startsolid; }

bool MapWalker::spawn(const Engine &engine) {
    velocity = {0, 0, 0};
    onGround = false;
    ducked = false;
    noclip = false;
    jumpHeld_ = false;
    waterLevel = 0;
    spawnYaw = 0.0f;
    spawnSource.clear();
    if (!engine.mapLoaded()) return false;

    // SelectSpawnPoint: info_player_start (an untargeted one first), then
    // the deathmatch / coop spots.
    const std::vector<MapEntity> entities = Engine::parseEntities(engine.mapEntities());
    const MapEntity *best = nullptr;
    for (const char *cls : {"info_player_start", "info_player_deathmatch", "info_player_coop"}) {
        for (const MapEntity &e : entities) {
            if (Engine::entityValue(e, "classname") != cls) continue;
            Vec3 o;
            if (!parseVec(Engine::entityValue(e, "origin"), o)) continue;
            if (!best || (Engine::entityValue(*best, "targetname").size() && Engine::entityValue(e, "targetname").empty()))
                best = &e;
        }
        if (best) {
            spawnSource = cls;
            break;
        }
    }
    if (best) {
        parseVec(Engine::entityValue(*best, "origin"), origin);
        origin[2] += 9.0f;
        Vec3 angles;
        const std::string angle = Engine::entityValue(*best, "angle");
        if (parseVec(Engine::entityValue(*best, "angles"), angles))
            spawnYaw = angles[1];
        else if (!angle.empty())
            spawnYaw = float(std::atof(angle.c_str()));
        if (!std::isfinite(spawnYaw)) spawnYaw = 0.0f;
    } else {
        // No spawn entity (typical for glTF maps): drop from the centre of
        // the map, or onto its top when the centre is solid.
        RealmsMapInfo info{};
        engine.mapInfo(info);
        const Vec3 centre{(info.mins[0] + info.maxs[0]) * 0.5f, (info.mins[1] + info.maxs[1]) * 0.5f,
                          (info.mins[2] + info.maxs[2]) * 0.5f};
        if (!stuck(engine, centre)) {
            const TraceResult down = trace(engine, centre, {centre[0], centre[1], info.mins[2] - 64.0f});
            origin = down.endpos;
            spawnSource = "map centre";
        } else {
            const Vec3 top{centre[0], centre[1], info.maxs[2] + 64.0f};
            const TraceResult down = trace(engine, top, {centre[0], centre[1], info.mins[2] - 64.0f});
            origin = down.endpos;
            spawnSource = "map top";
        }
    }
    for (int i = 0; i < 16 && stuck(engine, origin); ++i) origin[2] += 8.0f;
    categorize(engine);
    return true;
}

void MapWalker::categorize(const Engine &engine) {
    // Ground: a walkable plane within a quarter unit below (and not rising fast).
    const Vec3 below{origin[0], origin[1], origin[2] - 0.25f};
    if (velocity[2] > 180.0f) {
        onGround = false;
    } else {
        const TraceResult tr = trace(engine, origin, below);
        onGround = !(tr.fraction == 1.0f || (tr.normal[2] < kMinStepNormal && !tr.startsolid));
    }
    // Water level from the feet, waist and eyes.
    waterLevel = 0;
    const float sample2 = viewHeight() - kMinsZ;
    const float sample1 = sample2 * 0.5f;
    Vec3 point{origin[0], origin[1], origin[2] + kMinsZ + 1.0f};
    if (engine.pointContents(point) & REALMS_MASK_WATER) {
        waterLevel = 1;
        point[2] = origin[2] + kMinsZ + sample1;
        if (engine.pointContents(point) & REALMS_MASK_WATER) {
            waterLevel = 2;
            point[2] = origin[2] + kMinsZ + sample2;
            if (engine.pointContents(point) & REALMS_MASK_WATER) waterLevel = 3;
        }
    }
}

void MapWalker::checkDuck(const Engine &engine, bool wantDuck) {
    if (wantDuck && onGround) {
        ducked = true;
    } else if (ducked && !wantDuck) {
        // Stand up only when the full hull fits.
        const TraceResult tr = engine.boxTrace(origin, origin, {-kHalfWidth, -kHalfWidth, kMinsZ},
                                               {kHalfWidth, kHalfWidth, kStandMaxsZ}, 0, REALMS_MASK_PLAYERSOLID);
        if (!tr.allsolid) ducked = false;
    }
}

void MapWalker::slideMove(const Engine &engine, float dt) {
    Vec3 planes[kMaxClipPlanes];
    int numPlanes = 0;
    const Vec3 primal = velocity;
    float timeLeft = dt;
    for (int bump = 0; bump < 4; ++bump) {
        const Vec3 end = madd(origin, timeLeft, velocity);
        const TraceResult tr = trace(engine, origin, end);
        if (tr.allsolid) {
            velocity[2] = 0.0f; // entity is trapped in another solid
            return;
        }
        if (tr.fraction > 0.0f) {
            origin = tr.endpos;
            numPlanes = 0;
        }
        if (tr.fraction == 1.0f) break;
        timeLeft -= timeLeft * tr.fraction;
        if (numPlanes >= kMaxClipPlanes) {
            velocity = {0, 0, 0};
            break;
        }
        planes[numPlanes++] = tr.normal;
        // Make the velocity parallel to every clip plane.
        int i = 0;
        for (; i < numPlanes; ++i) {
            clipVelocity(velocity, planes[i], 1.01f);
            int j = 0;
            for (; j < numPlanes; ++j)
                if (j != i && dot3(velocity, planes[j]) < 0.0f) break;
            if (j == numPlanes) break;
        }
        if (i == numPlanes) {
            // Two planes: slide along the crease; otherwise stop.
            if (numPlanes != 2) {
                velocity = {0, 0, 0};
                break;
            }
            const Vec3 dir = cross3(planes[0], planes[1]);
            const float d = dot3(dir, velocity);
            velocity = {dir[0] * d, dir[1] * d, dir[2] * d};
        }
        // Don't turn back into the original direction (avoids corner jitter).
        if (dot3(velocity, primal) <= 0.0f) {
            velocity = {0, 0, 0};
            break;
        }
    }
}

void MapWalker::stepSlideMove(const Engine &engine, float dt) {
    const Vec3 startOrigin = origin, startVelocity = velocity;
    slideMove(engine, dt);
    const Vec3 downOrigin = origin, downVelocity = velocity;

    // Try the same move from one step higher.
    const TraceResult upTrace = trace(engine, startOrigin, {startOrigin[0], startOrigin[1], startOrigin[2] + settings.stepSize});
    if (upTrace.allsolid) return; // can't step up
    origin = upTrace.endpos;
    velocity = startVelocity;
    slideMove(engine, dt);
    // Push back down the amount we stepped up.
    const TraceResult downTrace = trace(engine, origin, {origin[0], origin[1], origin[2] - settings.stepSize});
    if (!downTrace.allsolid) origin = downTrace.endpos;

    const float downDist = (downOrigin[0] - startOrigin[0]) * (downOrigin[0] - startOrigin[0]) +
                           (downOrigin[1] - startOrigin[1]) * (downOrigin[1] - startOrigin[1]);
    const float upDist = (origin[0] - startOrigin[0]) * (origin[0] - startOrigin[0]) +
                         (origin[1] - startOrigin[1]) * (origin[1] - startOrigin[1]);
    if (downDist > upDist || downTrace.normal[2] < kMinStepNormal) {
        origin = downOrigin;
        velocity = downVelocity;
        return;
    }
    velocity[2] = downVelocity[2]; // walking along a plane: keep its z
}

void MapWalker::step(const Engine &engine, const WalkInput &input, float dt) {
    const float yaw = input.yaw * kPi / 180.0f, pitch = input.pitch * kPi / 180.0f;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const Vec3 forward3{cp * cy, cp * sy, -sp};
    const Vec3 flatForward{cy, sy, 0.0f};
    const Vec3 right{sy, -cy, 0.0f};
    const float fmove = std::clamp(input.forward, -1.0f, 1.0f), smove = std::clamp(input.right, -1.0f, 1.0f);

    if (noclip) {
        Vec3 wish = madd(madd({0, 0, std::clamp(input.up, -1.0f, 1.0f)}, fmove, forward3), smove, right);
        const float len = std::sqrt(dot3(wish, wish));
        if (len > 1.0f) wish = {wish[0] / len, wish[1] / len, wish[2] / len};
        velocity = {wish[0] * settings.noclipSpeed, wish[1] * settings.noclipSpeed, wish[2] * settings.noclipSpeed};
        origin = madd(origin, dt, velocity);
        onGround = false;
        waterLevel = 0;
        return;
    }

    checkDuck(engine, input.crouch);
    categorize(engine);

    // Jump (edge triggered, like PMF_JUMP_HELD).
    if (input.jump) {
        if (!jumpHeld_ && onGround && waterLevel < 2) {
            velocity[2] = std::max(velocity[2] + settings.jumpSpeed, settings.jumpSpeed);
            onGround = false;
        }
        jumpHeld_ = true;
    } else {
        jumpHeld_ = false;
    }

    // PM_Friction.
    {
        const float speed = onGround ? std::hypot(velocity[0], velocity[1]) : std::sqrt(dot3(velocity, velocity));
        if (speed < 1.0f) {
            velocity[0] = velocity[1] = 0.0f;
        } else {
            float drop = 0.0f;
            if (onGround) drop += std::max(speed, settings.stopSpeed) * settings.friction * dt;
            if (waterLevel) drop += speed * settings.waterFriction * float(waterLevel) * dt;
            const float scale = std::max(speed - drop, 0.0f) / speed;
            for (float &c : velocity) c *= scale;
        }
    }

    if (waterLevel >= 2) {
        // PM_WaterMove: swim where you look; sink slowly without input.
        const float up = std::clamp(input.up + (input.jump ? 1.0f : 0.0f), -1.0f, 1.0f);
        Vec3 wish = madd(madd({0, 0, up}, fmove, forward3), smove, right);
        for (float &c : wish) c *= settings.maxSpeed;
        if (fmove == 0.0f && smove == 0.0f && up == 0.0f) wish[2] -= 60.0f;
        float wishspeed = std::sqrt(dot3(wish, wish));
        const Vec3 dir = wishspeed > 0.0f ? Vec3{wish[0] / wishspeed, wish[1] / wishspeed, wish[2] / wishspeed} : Vec3{};
        wishspeed = std::min(wishspeed, settings.maxSpeed) * 0.5f;
        accelerate(velocity, dir, wishspeed, settings.waterAccelerate, dt);
        stepSlideMove(engine, dt);
    } else {
        Vec3 wish = madd(madd({0, 0, 0}, fmove, flatForward), smove, right);
        float wishspeed = std::sqrt(dot3(wish, wish));
        const Vec3 dir = wishspeed > 0.0f ? Vec3{wish[0] / wishspeed, wish[1] / wishspeed, 0.0f} : Vec3{};
        wishspeed = std::min(wishspeed, 1.0f) * (ducked ? settings.duckSpeed : settings.maxSpeed);
        if (onGround) {
            velocity[2] = 0.0f;
            accelerate(velocity, dir, wishspeed, settings.accelerate, dt);
            if (velocity[0] != 0.0f || velocity[1] != 0.0f) stepSlideMove(engine, dt);
        } else {
            accelerate(velocity, dir, wishspeed, settings.airAccelerate, dt);
            velocity[2] -= settings.gravity * dt;
            stepSlideMove(engine, dt);
        }
    }
    categorize(engine);
}

void MapWalker::move(const Engine &engine, const WalkInput &input, float dt) {
    if (!(dt > 0.0f) || !engine.mapLoaded()) return;
    dt = std::min(dt, 1.0f); // never simulate more than a second per call
    const float stepLen = settings.maxStep > 0.001f ? settings.maxStep : 0.05f;
    while (dt > 0.0f) {
        const float d = std::min(dt, stepLen);
        step(engine, input, d);
        dt -= d;
    }
}

} // namespace realms
