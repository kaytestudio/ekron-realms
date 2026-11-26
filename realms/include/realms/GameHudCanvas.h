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

#include "realms/FrameCounter.h"
#include "realms/GameLayout.h"
#include "realms/Image.h"
#include "realms/ViewSize.h"

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

    // Redraws the HUD of a width x height screen (pixels): the tiles around a
    // view shrunk by "viewsize" (viewRect), the status bar (when `statusBar`)
    // with its bottom edge at `barBottom`, then the layout and the inventory
    // when the game shows them, and cl_showfps's frame rate (also without
    // the game). `seconds`, a steady clock read once per frame, drives the
    // inventory cursor's 5 Hz blink and times the frames. True when the image
    // changed and needs uploading again; an unchanged HUD is not redrawn.
    // The frame rate shown changes kFpsRefresh seconds apart, not every frame.
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
    // Quake 2's crosshair (SCR_DrawCrosshair): pics/ch1.pcx .. ch3.pcx
    // centred on the image. True when the pic was there and drawn.
    bool drawCrosshair(int style, const FileLoader &load);
    // The "crosshair" cvar as Quake 2 reads it (SCR_TouchPics): 0 none,
    // 1 .. 3 the pic ch1 .. ch3, anything else 3. Ekron's default is 1.
    static int crosshairStyle(const Engine &engine);
    // Quake 2's "viewsize" (realms/ViewSize.h), 40 .. 100; 100 unregistered.
    static int viewSize(const Engine &engine);
    // Where the 3D view goes on a width x height screen (pixels): shrunk by
    // viewsize while the game runs, to meet the tiles update() draws around
    // it at scaleFor(width, height); the whole screen otherwise.
    static ViewRect viewRect(const Engine &engine, const Quake2Module &q2, int width, int height);
    // SCR_TileClear: everything outside `view` (image pixels) tiled with
    // pics/backtile.pcx from the top-left corner, or kBackFill without it.
    void tileClear(const ViewRect &view, const FileLoader &load);
    // Yamagi Quake II's cl_showfps (realms/FrameCounter.h); 0 unregistered.
    static float showFps(const Engine &engine);
    // FrameCounter lines at the top right, one per 10 rows (SCR_Framecounter).
    void drawFps(const std::vector<std::string> &lines);
    static constexpr double kFpsRefresh = 0.25;
    // update() drew the game's crosshair pic: the client's own one stays off.
    bool crosshairDrawn() const { return crosshairDrawn_; }
    // Releases the pics (a new game's data may differ).
    void clearPics() { pics_.clear(); }

    static constexpr Rgba kText{237, 237, 224, 255}, kAltText{140, 255, 140, 255};
    static constexpr Rgba kDigits{255, 209, 82, 255}, kAltDigits{255, 82, 64, 255};
    static constexpr Rgba kPanel{5, 10, 8, 209};
    static constexpr Rgba kBackFill{38, 34, 30, 255}; // the tiles without pics/backtile.pcx

private:
    const RgbaImage &pic(const std::string &name, const FileLoader &load);
    void blit(const RgbaImage &src, int x, int y);
    void drawText(int x, int y, const std::string &text, const Rgba &color, int size);
    void blend(int x, int y, const Rgba &color);

    std::unordered_map<std::string, RgbaImage> pics_; // empty image: missing
    RgbaImage image_;
    int scale_ = 1;
    bool empty_ = true;
    bool crosshairDrawn_ = false;
    std::string signature_;
    FrameCounter fps_;
    std::vector<std::string> fpsLines_; // shown now
    double fpsShownAt_ = -1.0;
    int fpsLevel_ = 0;
};

} // namespace realms
