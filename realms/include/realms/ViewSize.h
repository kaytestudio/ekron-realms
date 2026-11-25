#pragma once
// Quake 2's "viewsize" (cl_screen.c): the 3D view shrunk to 40 .. 100 per
// cent of the screen and centred (SCR_CalcVrect), the rest of the screen
// tiled with pics/backtile.pcx (SCR_TileClear). The status bar, the layout
// screens and the inventory stay where they are. "sizeup" / "sizedown" step
// it by 10 (registered with the shared cvars, realms/SandboxBoot.h).

#include <algorithm>
#include <cmath>

namespace realms {

// Top-left origin, y down.
struct ViewRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const ViewRect &o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
};

// The cvar bounded to 40 .. 100 and truncated, as SCR_CalcVrect reads it;
// 100 when it is not a number.
inline int clampViewSize(float viewsize) {
    return std::isfinite(viewsize) ? int(std::clamp(viewsize, 40.0f, 100.0f)) : 100;
}

// SCR_CalcVrect on a width x height screen.
inline ViewRect viewRect(int width, int height, int viewsize) {
    const int size = std::clamp(viewsize, 40, 100);
    ViewRect r;
    r.width = width * size / 100;
    r.height = height * size / 100;
    r.x = (width - r.width) / 2;
    r.y = (height - r.height) / 2;
    return r;
}

// The view on a screen whose 2D HUD has `scale` pixels per Quake 2 pixel
// (GameHudCanvas::scaleFor): worked out in Quake 2 pixels, so that it meets
// the tiles drawn at that scale exactly. The whole screen at 100.
inline ViewRect screenViewRect(int width, int height, int scale, int viewsize) {
    if (viewsize >= 100 || scale < 1 || width <= 0 || height <= 0)
        return {0, 0, std::max(width, 0), std::max(height, 0)};
    const ViewRect r = viewRect(width / scale, height / scale, viewsize);
    return {r.x * scale, r.y * scale, r.width * scale, r.height * scale};
}

// `r` on a screen of toW x toH from one of fromW x fromH (the desktop's
// logical HUD size to its drawable pixels).
inline ViewRect scaleViewRect(const ViewRect &r, int fromW, int fromH, int toW, int toH) {
    if (fromW <= 0 || fromH <= 0) return r;
    const long long x0 = (long long)r.x * toW / fromW, y0 = (long long)r.y * toH / fromH;
    const long long x1 = (long long)(r.x + r.width) * toW / fromW, y1 = (long long)(r.y + r.height) * toH / fromH;
    return {int(x0), int(y0), int(x1 - x0), int(y1 - y0)};
}

} // namespace realms
