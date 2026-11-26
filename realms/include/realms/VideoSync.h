#pragma once
// Yamagi Quake II's "r_vsync" (CVAR_ARCHIVE, default 1): 1 waits for the
// display's vertical sync, 2 is adaptive vsync (late frames are shown at once
// instead of waiting for the next refresh), any other value turns it off.
// As GL3_SetVsync does: 2 asks SDL for swap interval -1 and, where the driver
// can't do that, says so and falls back to normal vsync.
//
// The desktop applies it to its GL context whenever the wanted interval
// changes; a timedemo runs with vsync off whatever r_vsync says.

#include <string>

namespace realms {

constexpr const char *kAdaptiveVsyncFailed = "Failed to set adaptive vsync, reverting to normal vsync.";

// SDL_GL_SetSwapInterval's argument for an r_vsync value.
inline int vsyncSwapInterval(float rVsync) {
    if (rVsync == 1.0f) return 1;
    if (rVsync == 2.0f) return -1;
    return 0;
}

class SwapIntervalControl {
public:
    // `set` is SDL_GL_SetSwapInterval (0 on success, -1 on failure). It is
    // only called when the wanted interval changes. Returns the console
    // message for a failed adaptive vsync, otherwise "".
    template <class SetInterval>
    std::string update(float rVsync, bool uncapped, SetInterval &&set) {
        const int want = uncapped ? 0 : vsyncSwapInterval(rVsync);
        if (want == requested_) return std::string();
        requested_ = want;
        if (set(want) != 0 && want == -1) {
            set(1);
            return kAdaptiveVsyncFailed;
        }
        return std::string();
    }
    // The interval last asked for (the context starts with 1).
    int requested() const { return requested_; }

private:
    int requested_ = 1;
};

} // namespace realms
