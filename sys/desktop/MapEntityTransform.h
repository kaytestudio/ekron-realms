// Realms map mode (desktop): placing Quake 2 entities in the renderer's
// space. Header-only and free of SDL / GL so tests can check the maths
// (tests/realms/map_entity_transform_test.cpp).
//
// Render space is metres, Y up: a map point (x, y, z) is (x, z, -y) / upm
// (mapview::toRender). entityTransform is R_RotateForEntity in render
// space: yaw about map +Z, pitch about map -Y, roll about map -X, then the
// translation to `origin`, as a column-major matrix for uObjectTransform.
// It acts on render-space vertices: brush models (already converted with
// the map) and alias models after toRenderModelVertex.
#pragma once

#include "realms/Md2Model.h"

#include <array>
#include <cmath>

namespace ekron::desktop {

// Map pose T(origin) * Rz(yaw) * Ry(-pitch) * Rx(-roll), conjugated into
// render space: C * M * C^-1 (rotation only; the translation is scaled).
inline std::array<float, 16> entityTransform(const float origin[3], const float angles[3], float upm) {
    const float s = 1.0f / upm;
    const float y = angles[1] * 3.14159265358979f / 180.0f, p = -angles[0] * 3.14159265358979f / 180.0f, r = -angles[2] * 3.14159265358979f / 180.0f;
    const float cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p), cr = std::cos(r),
                sr = std::sin(r);
    // R = Rz(y) * Ry(p) * Rx(r), row-major map-space rotation
    float R[3][3] = {{cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr},
                     {sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr},
                     {-sp, cp * sr, cp * cr}};
    // C maps (x, y, z) -> (x, z, -y); in render space a = C R C^-1 with
    // C^-1 (X, Y, Z) = (X, -Z, Y). Render axis i takes map axis map[i] with sign sgn[i].
    const int map[3] = {0, 2, 1};
    const float sgn[3] = {1, 1, -1};
    float A[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) A[i][j] = sgn[i] * sgn[j] * R[map[i]][map[j]];
    const float t[3] = {origin[0] * s, origin[2] * s, -origin[1] * s};
    // column-major: m[col * 4 + row]
    return {A[0][0], A[1][0], A[2][0], 0, A[0][1], A[1][1], A[2][1], 0, A[0][2], A[1][2], A[2][2], 0,
            t[0], t[1], t[2], 1};
}

// A .md2 vertex (model space, Quake 2 units) on the renderer's axes and in
// metres: position and normal (x, z, -y), position / upm; uv unchanged.
// The axis change is a proper rotation, so the winding is kept.
inline std::array<float, 8> toRenderModelVertex(const realms::Md2Vertex &v, float metresPerUnit) {
    return {v.x * metresPerUnit, v.z * metresPerUnit, -v.y * metresPerUnit, v.nx, v.nz, -v.ny, v.u, v.v};
}

} // namespace ekron::desktop
