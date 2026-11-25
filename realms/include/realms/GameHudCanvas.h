// Quake 2's 2D game HUD -- the status bar (CS_STATUSBAR), the layout screens
// (help computer, scores) and the inventory -- drawn into one RGBA image in
// Quake 2 pixels, for sandboxes that show it as a single texture stretched
// over the 3D view with nearest filtering (Android, iOS). The desktop draws
// the same items with its own renderer (sys/desktop/DesktopMapMode.cpp);
// both use the same placement, panels and colours.
//
// Pics come from the game data (pics/<name>.pcx, index 255 transparent);
// without them, text uses the built-in pixel font (PixelFont.h) and numbers
// its digits at three times the size.
#pragma once

#include "realms/GameLayout.h"
#include "realms/Image.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace realms {

class Engine;
class Quake2Module;

class GameHudCanvas {
public:
    using Rgba = std::array<uint8_t, 4>;
    // Reads a game file (Engine::loadFile); false when missing.
    using FileLoader = std::function<bool(const std::string &path, std::vector<unsigned char> &out)>;

    // Whole screen pixels per Quake 2 pixel for a width x height HUD: the
    // 320 x 240 layout area fills most of the smaller side.
    static int scaleFor(int width, int height);

    // Redraws the HUD of a width x height screen (pixels): the status bar
    // (when `statusBar`) with its bottom edge at `barBottom`, then the layout
    // and the inventory when the game shows them. `seconds` drives the
    // inventory cursor's 5 Hz blink. True when the image changed and needs
    // uploading again; an unchanged HUD is not redrawn.
    bool update(const Engine &engine, const Quake2Module &q2, int width, int height, int barBottom, bool statusBar,
                double seconds);

    // (width / scale) x (height / scale), straight alpha, top row first.
    const RgbaImage &image() const { return image_; }
    int scale() const { return scale_; }
    // Nothing to draw (no game, or the HUD is off): skip the overlay.
    bool empty() const { return empty_; }

    // Building blocks of update(). begin() clears to transparent.
    void begin(int width, int height);
    void fillRect(int x, int y, int w, int h, const Rgba &color);
    void drawItems(const std::vector<GameLayoutItem> &items, const FileLoader &load, int offsetY = 0);
    // Releases the pics (a new game's data may differ).
    void clearPics() { pics_.clear(); }

    static constexpr Rgba kText{237, 237, 224, 255}, kAltText{140, 255, 140, 255};
    static constexpr Rgba kDigits{255, 209, 82, 255}, kAltDigits{255, 82, 64, 255};
    static constexpr Rgba kPanel{5, 10, 8, 209};

private:
    const RgbaImage &pic(const std::string &name, const FileLoader &load);
    void blit(const RgbaImage &src, int x, int y);
    void drawText(int x, int y, const std::string &text, const Rgba &color, int size);
    void blend(int x, int y, const Rgba &color);

    std::unordered_map<std::string, RgbaImage> pics_; // empty image: missing
    RgbaImage image_;
    int scale_ = 1;
    bool empty_ = true;
    std::string signature_;
};

} // namespace realms
