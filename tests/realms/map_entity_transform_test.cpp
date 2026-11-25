// MapEntityTransform.h: the view weapon points along the camera axes for
// every yaw and pitch, brush model pitch, translation, yaw and winding.
#include "MapEntityTransform.h"
#include "realms/GameView.h"

#include <cmath>
#include <cstdio>
#include <vector>

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
    const auto v = mapview::toRenderModelVertex({x, y, z, 0, 0, 1, 0, 0}, 1.0f / kUpm);
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
    using mapview::entityTransform;
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
        const auto a = mapview::toRenderModelVertex({0, 0, 0, 0, 0, 1, 0, 0}, 1);
        const auto b = mapview::toRenderModelVertex({1, 0, 0, 0, 0, 1, 0, 0}, 1);
        const auto c = mapview::toRenderModelVertex({0, 1, 0, 0, 0, 1, 0, 0}, 1);
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        CHECK(n[1] > 0.99f);
        CHECK(near(a[3], 0, 1e-6f) && near(a[4], 1, 1e-6f) && near(a[5], 0, 1e-6f));
    }
    // hand 1 (realms::mirrorModelVertices): a gun on the right (model -y)
    // moves to the left, its normals follow and the winding is kept (the
    // face normal from the corners still matches the vertex normal)
    {
        std::vector<realms::ModelVertex> v = {{9, 9, 9, 0, 0, 1, 0, 0}, // before `first`: untouched
                                              {0, -4, 0, 0, -1, 0, 0, 0},
                                              {1, -4, 0, 0, -1, 0, 1, 0},
                                              {0, -4, 1, 0, -1, 0, 0, 1}};
        realms::mirrorModelVertices(v, 1);
        CHECK(near(v[0].y, 9, 0));
        bool mirrored = true;
        for (size_t i = 1; i < 4; i++) mirrored &= near(v[i].y, 4, 1e-6f) && near(v[i].ny, 1, 1e-6f);
        CHECK(mirrored);
        const float e1[3] = {v[2].x - v[1].x, v[2].y - v[1].y, v[2].z - v[1].z};
        const float e2[3] = {v[3].x - v[1].x, v[3].y - v[1].y, v[3].z - v[1].z};
        const float ny = e1[2] * e2[0] - e1[0] * e2[2];
        // before: corners (0,-4,0) (1,-4,0) (0,-4,1) face -y (n.y = -1, matching)
        CHECK(ny > 0.5f); // after: faces +y, like the mirrored normals
        CHECK(near(v[2].u, 0, 0) && near(v[2].v, 1, 0)); // corners 2 and 3 swapped
    }
    std::printf("[map entity transform] %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
