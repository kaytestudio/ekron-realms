#pragma once
// Yamagi Quake II's "vid_maxfps" (CVAR_ARCHIVE, default 300): the most frames
// a second the client draws. As Qcommon_Frame reads it:
//   - a value above 999 or below 1 is set back to 999;
//   - with vsync on, it only applies when it is clearly below the display's
//     refresh rate (vid_maxfps < refresh - 2); otherwise the target is
//     refresh * 1.2 and vsync does the pacing;
//   - with vsync off it is the target;
//   - a timedemo is never held back.
// The desktop waits before a frame until 1 / target seconds have passed
// since the last one started.

#include <algorithm>
#include <cmath>

namespace realms {

constexpr float kDefaultMaxFps = 300.0f;
constexpr float kMaxMaxFps = 999.0f;
constexpr float kDefaultRefreshRate = 60.0f; // GLimp_GetRefreshRate when the display doesn't say

// True when vid_maxfps has to be set back to 999 (NaN too).
inline bool maxFpsOutOfRange(float vidMaxFps) { return !(vidMaxFps >= 1.0f && vidMaxFps <= kMaxMaxFps); }

// The render frame rate to aim for: rfps in Qcommon_Frame. `refreshRate`
// <= 0 (unknown) counts as 60.
inline float renderFrameRate(float vidMaxFps, bool vsyncActive, float refreshRate) {
    if (maxFpsOutOfRange(vidMaxFps)) vidMaxFps = kMaxMaxFps;
    if (!vsyncActive) return vidMaxFps;
    if (!(refreshRate > 0.0f) || !std::isfinite(refreshRate)) refreshRate = kDefaultRefreshRate;
    return vidMaxFps < refreshRate - 2.0f ? vidMaxFps : refreshRate * 1.2f;
}

class FrameLimiter {
public:
    // When the frame that is ready at `now` (seconds, any steady clock) may
    // start, at `fps` frames a second: at least 1 / fps after the last one.
    // A late frame starts at once and the next one counts from it, so frames
    // never bunch up to catch up. `fps` <= 0 doesn't wait.
    double frameStart(double now, float fps) {
        double start = now;
        if (fps > 0.0f && std::isfinite(fps) && last_ >= 0.0 && now >= last_)
            start = std::max(now, last_ + 1.0 / double(fps));
        last_ = start;
        return start;
    }
    void reset() { last_ = -1.0; }

private:
    double last_ = -1.0;
};

} // namespace realms
