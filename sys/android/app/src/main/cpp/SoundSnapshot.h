#pragma once
// Builds the SoundDirector snapshots from the shared game state, so the
// desktop client, Android and iOS hear exactly the same events.

#include "FpvDrone.h"
#include "PlayerPhysics.h"
#include "SandboxWorld.h"
#include "SoundDirector.h"
#include "ToolInteraction.h"
#include "realms/MapWalker.h"
#include "realms/Engine.h"

#include <cmath>

namespace ekron::audio {

inline int bodyKind(const sandbox::Object &o) {
    if (o.type == 8 || o.type == 11 || o.surface == sandbox::Rust) return 2;
    if (o.surface == sandbox::Wood || o.type == 3 || o.type == 4 || o.type == 5 || o.type == 6 || o.type == 9) return 1;
    return 0;
}

inline Surface surfaceForMaterial(int material) {
    if (material == sandbox::Wood) return Surface::Wood;
    if (material == sandbox::Fabric) return Surface::Carpet;
    return Surface::Concrete;
}

// What the feet stand on: the top of a placed object, otherwise the street.
inline Surface sandboxSurface(const sandbox::World &w, float fx, float fy, float fz) {
    for (const auto &o : w.objects) {
        if (o.destroyed) continue;
        const auto b = sandbox::World::bounds(o);
        const float top = b.y + b.h / 2;
        if (std::abs(fy - top) > 0.15f || std::abs(fx - b.x) > b.w / 2 + 0.1f || std::abs(fz - b.z) > b.d / 2 + 0.1f)
            continue;
        if (o.surface >= 0) return surfaceForMaterial(o.surface);
        const int fixed = sandbox::World::fixedSurface(o.type);
        return surfaceForMaterial(fixed >= 0 ? fixed : sandbox::Concrete);
    }
    return Surface::Concrete;
}

inline bool droneRunning(const sandbox::FpvDrone &d) {
    return d.mode == sandbox::FpvDrone::Flying || d.mode == sandbox::FpvDrone::Entering ||
           d.mode == sandbox::FpvDrone::Leaving || d.mode == sandbox::FpvDrone::Hover ||
           d.mode == sandbox::FpvDrone::Returning;
}

// `listener` is the camera: the player's eye, or the drone when piloting.
inline SandboxFrame sandboxFrame(bool active, const Listener &listener, const sandbox::World &w,
                                 const sandbox::ToolInteraction &tools, const sandbox::FpvDrone &drone,
                                 const physics::PlayerController &player, int seated, bool flying,
                                 float droneThrottle) {
    SandboxFrame f;
    f.active = active;
    f.listener = listener;
    f.onFoot = seated < 0 && !flying && !drone.piloting();
    f.grounded = player.grounded();
    const physics::Vec3 feet = player.feet();
    f.feetX = feet.x;
    f.feetY = feet.y;
    f.feetZ = feet.z;
    f.verticalSpeed = player.verticalVelocity();
    f.surface = f.onFoot ? sandboxSurface(w, feet.x, feet.y, feet.z) : Surface::Concrete;
    f.tool = int(tools.tool);
    f.held = tools.held;
    f.seated = seated;
    f.seatedCar = seated >= 0 && size_t(seated) < w.objects.size() && w.objects[size_t(seated)].type == 8;
    f.droneRunning = droneRunning(drone);
    f.droneX = drone.position.x;
    f.droneY = drone.position.y;
    f.droneZ = drone.position.z;
    f.droneThrottle = droneThrottle;
    f.bodies.reserve(w.objects.size());
    for (const auto &o : w.objects) {
        SandboxBody b;
        b.x = o.x;
        b.y = o.y;
        b.z = o.z;
        b.vy = o.vy;
        b.kind = bodyKind(o);
        b.frozen = o.frozen;
        b.door = o.type == 3;
        b.doorOpen = o.doorTarget > 0.5f;
        b.car = o.type == 8;
        f.bodies.push_back(b);
    }
    return f;
}

// Map mode: Quake units (32 per metre), z up; the listener uses render
// space like the camera ((x, z, -y) / 32).
inline MapFrame mapFrame(bool active, const realms::Engine &engine, const realms::MapWalker &walker, float yaw) {
    constexpr float upm = 32.0f;
    MapFrame f;
    f.active = active;
    f.onFoot = !walker.noclip;
    f.grounded = walker.onGround;
    f.feetX = walker.origin[0] / upm;
    f.feetY = (walker.origin[2] + realms::MapWalker::kMinsZ) / upm;
    f.feetZ = -walker.origin[1] / upm;
    f.verticalSpeed = walker.velocity[2] / upm;
    f.listener.x = f.feetX;
    f.listener.y = f.feetY + 1.6f;
    f.listener.z = f.feetZ;
    f.listener.yaw = yaw;
    if (walker.onGround && engine.mapLoaded()) {
        realms::Vec3 start = walker.origin;
        realms::Vec3 end = walker.origin;
        end[2] -= 64.0f;
        const realms::TraceResult tr = engine.boxTrace(start, end, walker.mins(), walker.maxs(), 0, REALMS_MASK_PLAYERSOLID);
        if (tr.hit()) f.surface = surfaceFromTexture(tr.surface);
    }
    return f;
}

} // namespace ekron::audio
