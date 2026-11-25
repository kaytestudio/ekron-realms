#pragma once
// The camera's focal length for the sandbox and for map mode. Every client
// projects a view-space point q as (q.x * f / aspect, q.y * f), so f is
// 1 / tan(vertical field of view / 2).

#include <algorithm>
#include <cmath>

namespace realms {

// The sandbox city: a 72 degree vertical field of view (1 / tan 36 deg).
constexpr float kSandboxFocalLength = 1.37638f;

// Quake 2's fov (the "fov" cvar and userinfo, player_state_t::fov), clamped
// like the game's ClientUserinfoChanged (1..160); 90 when it is not a number.
inline float clampFov(float fov) { return std::isfinite(fov) ? std::clamp(fov, 1.0f, 160.0f) : 90.0f; }

// Map mode: Quake 2's fov is the horizontal angle of a 4:3 screen, as
// V_CalcFov sees it on 640x480. The vertical angle follows from it, and a
// wider screen shows more at the sides (Hor+, like Yamagi Quake II and the
// 2023 re-release) instead of cutting the top and bottom. fov 90: 73.7
// degrees vertical, f = 4/3.
inline float mapFocalLength(float fov) {
    return (4.0f / 3.0f) / std::tan(clampFov(fov) * (3.14159265358979323846f / 360.0f));
}

} // namespace realms
