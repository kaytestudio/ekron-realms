// realms/ViewFov.h: map mode's focal length follows Quake 2's fov (Hor+,
// the fov is the horizontal angle of a 4:3 screen), clamped like the game;
// realms/ViewGamma.h: vid_gamma's exponent, and the cvar on the shared list;
// realms/ViewSize.h: Quake 2's viewsize rectangle, the cvar and sizeup / sizedown;
// realms/VideoSync.h: r_vsync's swap interval and when the desktop applies it.
#include "realms/Engine.h"
#include "realms/SandboxBoot.h"
#include "realms/ViewFov.h"
#include "realms/ViewGamma.h"
#include "realms/ViewSize.h"
#include "realms/VideoSync.h"

#include <string>
#include <vector>

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

    // viewsize: SCR_CalcVrect's bounds and truncation, then its rectangle.
    CHECK(clampViewSize(100.0f) == 100 && clampViewSize(70.0f) == 70);
    CHECK(clampViewSize(10.0f) == 40 && clampViewSize(-5.0f) == 40 && clampViewSize(250.0f) == 100);
    CHECK(clampViewSize(55.9f) == 55);
    CHECK(clampViewSize(std::numeric_limits<float>::quiet_NaN()) == 100);
    CHECK((viewRect(640, 480, 100) == ViewRect{0, 0, 640, 480}));
    CHECK((viewRect(640, 480, 50) == ViewRect{160, 120, 320, 240}));
    CHECK((viewRect(640, 480, 70) == ViewRect{96, 72, 448, 336}));  // 640 * 70 / 100 = 448
    CHECK((viewRect(1001, 333, 40) == ViewRect{300, 100, 400, 133})); // integer maths, centred
    CHECK((viewRect(640, 480, 0) == viewRect(640, 480, 40)));
    // On a screen: in Quake 2 pixels times the HUD scale, the whole screen at 100.
    CHECK((screenViewRect(1920, 1080, 3, 100) == ViewRect{0, 0, 1920, 1080}));
    CHECK((screenViewRect(1920, 1080, 3, 50) == ViewRect{480, 270, 960, 540}));
    {
        const ViewRect r = screenViewRect(1923, 1081, 3, 70); // 641 x 360 Quake 2 pixels
        CHECK((r == ViewRect{96 * 3, 54 * 3, 448 * 3, 252 * 3}));
        CHECK(r.x % 3 == 0 && r.y % 3 == 0 && r.width % 3 == 0 && r.height % 3 == 0);
    }
    CHECK((screenViewRect(800, 600, 0, 50) == ViewRect{0, 0, 800, 600}));
    CHECK((scaleViewRect({480, 270, 960, 540}, 1920, 1080, 3840, 2160) == ViewRect{960, 540, 1920, 1080}));
    CHECK((scaleViewRect({1, 1, 3, 3}, 4, 4, 6, 6) == ViewRect{1, 1, 5, 5})); // edges rounded down
    {
        realms::Engine engine;
        realms::EngineConfig cfg;
        cfg.backend = realms::BackendPreference::Native;
        CHECK(engine.init(cfg));
        realms::sandbox::registerSharedCvars(engine, "fov-test");
        CHECK(engine.hasCvar("viewsize") && (engine.cvarFlags("viewsize") & REALMS_CVAR_ARCHIVE));
        CHECK(engine.cvarInt("viewsize", 0) == 100);
        CHECK(engine.commandExists("sizeup") && engine.commandExists("sizedown"));
        engine.executeNow("sizeup"); // already at the top
        CHECK(engine.cvarInt("viewsize", 0) == 100);
        engine.executeNow("sizedown");
        engine.executeNow("sizedown");
        CHECK(engine.cvarInt("viewsize", 0) == 80);
        for (int i = 0; i < 10; i++) engine.executeNow("sizedown");
        CHECK(engine.cvarInt("viewsize", 0) == 40);
        engine.executeNow("viewsize 45");
        engine.executeNow("sizeup");
        CHECK(engine.cvarInt("viewsize", 0) == 55);
        engine.executeNow("viewsize 300"); // set by hand: read bounded
        CHECK(clampViewSize(engine.cvarFloat("viewsize", 100.0f)) == 100);
        engine.executeNow("sizedown"); // 290, bounded as SCR_CalcVrect does
        CHECK(engine.cvarInt("viewsize", 0) == 100);
        engine.shutdown();
    }

    // r_vsync: 1 on, 2 adaptive (-1), anything else off, as GL3_SetVsync reads it.
    CHECK(vsyncSwapInterval(1.0f) == 1);
    CHECK(vsyncSwapInterval(2.0f) == -1);
    CHECK(vsyncSwapInterval(0.0f) == 0);
    CHECK(vsyncSwapInterval(1.5f) == 0 && vsyncSwapInterval(3.0f) == 0 && vsyncSwapInterval(-1.0f) == 0);
    CHECK(vsyncSwapInterval(std::numeric_limits<float>::quiet_NaN()) == 0);
    {
        std::vector<int> calls;
        int failInterval = 99; // the driver refuses this one
        auto set = [&](int interval) {
            calls.push_back(interval);
            return interval == failInterval ? -1 : 0;
        };
        SwapIntervalControl swap;
        CHECK(swap.requested() == 1);
        // the context starts with vsync on: r_vsync 1 changes nothing
        CHECK(swap.update(1.0f, false, set).empty() && calls.empty());
        CHECK(swap.update(0.0f, false, set).empty() && (calls == std::vector<int>{0}) && swap.requested() == 0);
        CHECK(swap.update(0.0f, false, set).empty() && calls.size() == 1); // only on a change
        // a timedemo runs uncapped whatever r_vsync says, then it comes back
        CHECK(swap.update(1.0f, true, set).empty() && calls.size() == 1);
        CHECK(swap.update(1.0f, false, set).empty() && (calls == std::vector<int>{0, 1}));
        CHECK(swap.update(1.0f, true, set).empty() && (calls == std::vector<int>{0, 1, 0}));
        CHECK(swap.update(2.0f, false, set).empty() && (calls == std::vector<int>{0, 1, 0, -1}));
        // no adaptive vsync here: say so once and use normal vsync
        calls.clear();
        swap.update(0.0f, false, set);
        failInterval = -1;
        CHECK(swap.update(2.0f, false, set) == kAdaptiveVsyncFailed && (calls == std::vector<int>{0, -1, 1}));
        CHECK(swap.update(2.0f, false, set).empty() && calls.size() == 3); // not again every frame
        CHECK(swap.requested() == -1);
    }

    std::printf("view_fov: %d/%d checks passed\n", gChecks - gFailures, gChecks);
    return gFailures ? 1 : 0;
}
