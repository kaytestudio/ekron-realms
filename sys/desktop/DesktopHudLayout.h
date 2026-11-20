#pragma once

#include <algorithm>
#include <cmath>

namespace ekron::desktop {

// Keep the catalog and pause menu usable in small windows. Rendering and SDL
// hit-testing must use the same logical coordinates (also on HiDPI displays).
struct HudLayout {
    int width;
    int height;
    float mouseScaleX;
    float mouseScaleY;

    static HudLayout forWindow(int windowWidth, int windowHeight) {
        const int w = std::max(windowWidth, 1);
        const int h = std::max(windowHeight, 1);
        const float scale = std::min({1.0f, float(w) / 800.0f, float(h) / 500.0f});
        const int logicalWidth = int(std::ceil(float(w) / scale));
        const int logicalHeight = int(std::ceil(float(h) / scale));
        return {logicalWidth, logicalHeight,
                float(logicalWidth) / float(w), float(logicalHeight) / float(h)};
    }
};

inline float fitHudTextScale(float preferredScale, float measuredWidth, float availableWidth) {
    if (measuredWidth <= 0.0f) return preferredScale;
    return preferredScale * std::min(1.0f, std::max(0.0f, availableWidth) / measuredWidth);
}

} // namespace ekron::desktop
