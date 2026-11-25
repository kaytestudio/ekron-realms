// realms/ViewFov.h: map mode's focal length follows Quake 2's fov (Hor+,
// the fov is the horizontal angle of a 4:3 screen), clamped like the game;
// realms/ViewGamma.h: vid_gamma's exponent, and the cvar on the shared list.
#include "realms/Engine.h"
#include "realms/SandboxBoot.h"
#include "realms/ViewFov.h"
#include "realms/ViewGamma.h"

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

    // vid_gamma: 1 leaves the colour, higher is brighter (a smaller exponent),
    // clamped to the slider's 0.1 .. 2.0.
    CHECK(near(gammaExponent(1.0f), 1.0f));
    CHECK(near(gammaExponent(2.0f), 0.5f));
    CHECK(near(gammaExponent(0.5f), 2.0f));
    CHECK(near(gammaExponent(5.0f), 0.5f));
    CHECK(near(gammaExponent(0.0f), 10.0f));
    CHECK(near(gammaExponent(-1.0f), 10.0f));
    CHECK(near(gammaExponent(std::numeric_limits<float>::quiet_NaN()), 1.0f));
    CHECK(std::pow(0.5f, gammaExponent(1.5f)) > 0.5f); // brighter mid grey
    {
        realms::Engine engine;
        realms::EngineConfig cfg;
        cfg.backend = realms::BackendPreference::Native;
        CHECK(engine.init(cfg));
        realms::sandbox::registerSharedCvars(engine, "fov-test");
        CHECK(engine.hasCvar("vid_gamma") && (engine.cvarFlags("vid_gamma") & REALMS_CVAR_ARCHIVE));
        CHECK(near(engine.cvarFloat("vid_gamma", 0.0f), 1.0f));
        engine.executeNow("vid_gamma 1.3");
        CHECK(near(gammaExponent(engine.cvarFloat("vid_gamma", 1.0f)), 1.0f / 1.3f));
        engine.shutdown();
    }

    std::printf("view_fov: %d/%d checks passed\n", gChecks - gFailures, gChecks);
    return gFailures ? 1 : 0;
}
