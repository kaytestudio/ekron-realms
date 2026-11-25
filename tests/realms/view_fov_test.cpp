// realms/ViewFov.h: map mode's focal length follows Quake 2's fov (Hor+,
// the fov is the horizontal angle of a 4:3 screen), clamped like the game.
#include "realms/ViewFov.h"

#include <cmath>
#include <cstdio>
#include <limits>

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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
constexpr float kDegrees = 180.0f / 3.14159265358979f;

} // namespace

int main() {
    using namespace realms;
    CHECK(near(clampFov(90.0f), 90.0f));
    CHECK(near(clampFov(0.0f), 1.0f));
    CHECK(near(clampFov(-30.0f), 1.0f));
    CHECK(near(clampFov(200.0f), 160.0f));
    CHECK(near(clampFov(std::numeric_limits<float>::quiet_NaN()), 90.0f));
    CHECK(near(clampFov(std::numeric_limits<float>::infinity()), 90.0f));

    // fov 90: f = 4/3, 73.74 degrees vertical; 4:3 shows exactly 90 across.
    CHECK(near(mapFocalLength(90.0f), 4.0f / 3.0f));
    CHECK(near(2.0f * std::atan(1.0f / mapFocalLength(90.0f)) * kDegrees, 73.7398f, 1e-3f));
    CHECK(near(2.0f * std::atan((4.0f / 3.0f) / mapFocalLength(90.0f)) * kDegrees, 90.0f, 1e-3f));
    CHECK(near(2.0f * std::atan((4.0f / 3.0f) / mapFocalLength(110.0f)) * kDegrees, 110.0f, 1e-3f));
    // Hor+: 16:9 at fov 90 is wider than 90 degrees (106.26).
    CHECK(near(2.0f * std::atan((16.0f / 9.0f) / mapFocalLength(90.0f)) * kDegrees, 106.26f, 1e-2f));
    // Wider fov, shorter focal length; out-of-range values clamp.
    CHECK(mapFocalLength(120.0f) < mapFocalLength(90.0f));
    CHECK(near(mapFocalLength(500.0f), mapFocalLength(160.0f)));
    CHECK(std::isfinite(mapFocalLength(0.0f)) && mapFocalLength(0.0f) > 0.0f);
    // The sandbox keeps its 72 degree vertical view.
    CHECK(near(2.0f * std::atan(1.0f / kSandboxFocalLength) * kDegrees, 72.0f, 1e-3f));

    std::printf("view_fov: %d/%d checks passed\n", gChecks - gFailures, gChecks);
    return gFailures ? 1 : 0;
}
