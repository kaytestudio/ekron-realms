// Realms map mode (desktop): Quake 2 entities placed in render space
// (sys/desktop/MapEntityTransform.h), the way DesktopMapMode feeds
// DesktopRenderer's uObjectTransform.
//
//   * the view weapon, posed like GameDrawBatch (camera angles from
//     mapview::walkInput, alias pitch negated), points along the camera's
//     forward / up / left axes for every yaw and pitch, at 1 m per 32 units;
//   * a brush model (no negation) pitches like R_RotateForEntity;
//   * translation, yaw 90 and the winding of a converted triangle.
#include "MapEntityTransform.h"

#include <cmath>
#include <cstdio>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++gChecks;                                                                      \
        if (!(cond)) {                                                                  \
            ++gFailures;                                                                \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                               \
    } while (0)

constexpr float kPi = 3.14159265358979f;
constexpr float kUpm = 32.0f;

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// model-space point -> render space, as buildEntities + the vertex shader do
void place(const std::array<float, 16> &m, float x, float y, float z, float out[3]) {
    const auto v = ekron::desktop::toRenderModelVertex({x, y, z, 0, 0, 1, 0, 0}, 1.0f / kUpm);
    for (int r = 0; r < 3; r++) out[r] = m[size_t(r)] * v[0] + m[size_t(4 + r)] * v[1] + m[size_t(8 + r)] * v[2] + m[size_t(12 + r)];
}

void axis(const std::array<float, 16> &m, float x, float y, float z, float out[3]) {
    float o[3], p[3];
    place(m, 0, 0, 0, o);
    place(m, x, y, z, p);
    for (int k = 0; k < 3; k++) out[k] = p[k] - o[k];
}

float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

} // namespace

int main() {
    using ekron::desktop::entityTransform;
    int misaligned = 0;
    for (float ry = -3.1f; ry < 3.15f; ry += 0.23f) {
        for (float rp = -1.4f; rp < 1.41f; rp += 0.2f) {
            // DesktopMapMode::buildEntities: cameraAngles = {pitch, yaw, 0}
            // from walkInput; GameDrawBatch negates the alias pitch
            const float q2yaw = ry * 180 / kPi + 90, q2pitch = -rp * 180 / kPi;
            const float org[3] = {100, 50, 30}, ang[3] = {-q2pitch, q2yaw, 0};
            const auto m = entityTransform(org, ang, kUpm);
            float f[3], u[3], l[3];
            axis(m, kUpm, 0, 0, f);
            axis(m, 0, 0, kUpm, u);
            axis(m, 0, kUpm, 0, l);
            // DesktopRenderer camera: yaw 0 looks down -Z, positive pitch looks up
            const float cf[3] = {-std::sin(ry) * std::cos(rp), std::sin(rp), -std::cos(ry) * std::cos(rp)};
            const float cu[3] = {std::sin(ry) * std::sin(rp), std::cos(rp), std::cos(ry) * std::sin(rp)};
            const float cl[3] = {-std::cos(ry), 0, std::sin(ry)};
            if (!near(dot(f, cf), 1, 1e-4f) || !near(dot(u, cu), 1, 1e-4f) || !near(dot(l, cl), 1, 1e-4f)) misaligned++;
        }
    }
    CHECK(misaligned == 0);

    // brush models: R_RotateForEntity's glRotatef(-pitch, 0, 1, 0) tips map
    // +X *up* for pitch 30 (the reason alias models negate their pitch)
    {
        const float org[3] = {0, 0, 0}, ang[3] = {30, 0, 0};
        const auto m = entityTransform(org, ang, kUpm);
        // brush vertices are already in render space: render +X (map +X)
        const float x = m[0], y = m[1], z = m[2];
        CHECK(near(x, std::cos(30 * kPi / 180), 1e-5f) && near(y, std::sin(30 * kPi / 180), 1e-5f) && near(z, 0, 1e-6f));
    }
    // translation: map (100, 50, 30) -> render (100, 30, -50) / 32
    {
        const float org[3] = {100, 50, 30}, ang[3] = {0, 0, 0};
        const auto m = entityTransform(org, ang, kUpm);
        float p[3];
        place(m, 0, 0, 0, p);
        CHECK(near(p[0], 100 / kUpm, 1e-5f) && near(p[1], 30 / kUpm, 1e-5f) && near(p[2], -50 / kUpm, 1e-5f));
        float q[3];
        place(m, 0, 0, kUpm, q); // one metre up
        CHECK(near(q[1] - p[1], 1, 1e-5f));
    }
    // yaw 90: model forward along map +Y = render -Z
    {
        const float org[3] = {0, 0, 0}, ang[3] = {0, 90, 0};
        float f[3];
        axis(entityTransform(org, ang, kUpm), kUpm, 0, 0, f);
        CHECK(near(f[2], -1, 1e-5f) && near(f[0], 0, 1e-5f));
    }
    // winding: a counter-clockwise model triangle seen from +Z (above)
    // stays counter-clockwise seen from render +Y; normals follow
    {
        const auto a = ekron::desktop::toRenderModelVertex({0, 0, 0, 0, 0, 1, 0, 0}, 1);
        const auto b = ekron::desktop::toRenderModelVertex({1, 0, 0, 0, 0, 1, 0, 0}, 1);
        const auto c = ekron::desktop::toRenderModelVertex({0, 1, 0, 0, 0, 1, 0, 0}, 1);
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        CHECK(n[1] > 0.99f);
        CHECK(near(a[3], 0, 1e-6f) && near(a[4], 1, 1e-6f) && near(a[5], 0, 1e-6f));
    }
    std::printf("[map entity transform] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
