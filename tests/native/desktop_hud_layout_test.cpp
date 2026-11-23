#include "../../sys/desktop/DesktopHudLayout.h"

// Keep checks active in the Release builds used by desktop CI.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using ekron::desktop::HudLayout;
    using ekron::desktop::fitHudTextScale;
    const auto normal = HudLayout::forWindow(1280, 720);
    assert(normal.width == 1280 && normal.height == 720);
    assert(normal.mouseScaleX == 1.0f && normal.mouseScaleY == 1.0f);

    for (const auto &size : {std::pair<int, int>{400, 250}, {640, 480}, {320, 700}, {1920, 300}}) {
        const auto layout = HudLayout::forWindow(size.first, size.second);
        assert(layout.width >= 800 && layout.height >= 500);
        // A click on the window center must land on the HUD center.
        assert(std::abs(size.first * 0.5f * layout.mouseScaleX - layout.width * 0.5f) < 0.001f);
        assert(std::abs(size.second * 0.5f * layout.mouseScaleY - layout.height * 0.5f) < 0.001f);
        // Every pause button and the catalog fit into the logical viewport.
        // 8 buttons (including the language switcher), panel height 62 + 8 * 42.
        assert((layout.height - 398.0f) * 0.5f + 62.0f + 7.0f * 42.0f + 34.0f <= layout.height);
    }
    assert(fitHudTextScale(1.3f, 600.0f, 800.0f) == 1.3f);
    assert(std::abs(fitHudTextScale(1.3f, 1200.0f, 600.0f) - 0.65f) < 0.0001f);
    assert(fitHudTextScale(1.3f, 0.0f, 600.0f) == 1.3f);

    // Intro video letterboxing: 16:9 picture, whole picture visible, centred.
    using ekron::desktop::letterbox;
    const auto same = letterbox(1280, 720, 1280.0f, 720.0f);
    assert(same.x == 0.0f && same.y == 0.0f && same.w == 1280.0f && same.h == 720.0f);
    const auto tall = letterbox(1280, 720, 800.0f, 800.0f); // bars top and bottom
    assert(tall.x == 0.0f && tall.w == 800.0f && std::abs(tall.h - 450.0f) < 0.001f);
    assert(std::abs(tall.y - 175.0f) < 0.001f);
    const auto wide = letterbox(1280, 720, 2560.0f, 1080.0f); // bars left and right
    assert(wide.y == 0.0f && wide.h == 1080.0f && std::abs(wide.w - 1920.0f) < 0.001f);
    assert(std::abs(wide.x - 320.0f) < 0.001f);
    const auto none = letterbox(0, 720, 800.0f, 600.0f);
    assert(none.w == 0.0f && none.h == 0.0f);
    std::cout << "Desktop HUD layout checks passed\n";
}
