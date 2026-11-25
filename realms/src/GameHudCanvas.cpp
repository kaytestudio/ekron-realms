#include "realms/GameHudCanvas.h"

#include "realms/Engine.h"
#include "realms/PixelFont.h"
#include "realms/Quake2Module.h"

#include <algorithm>
#include <cmath>

namespace realms {

namespace {

// The layout's dim panels, in Quake 2 pixels (as the desktop draws them).
struct Panel {
    int x, y, w, h;
};
Panel layoutPanel(int w, int h) { return {w / 2 - 168, h / 2 - 128, 336, 256}; }
Panel inventoryPanel(int w, int h) { return {(w - 256) / 2 - 8, (h - 240) / 2, 272, 256}; }

void appendSignature(std::string &sig, const std::vector<GameLayoutItem> &items) {
    for (const GameLayoutItem &it : items) {
        sig += char('0' + int(it.kind));
        sig += it.alt ? '!' : ' ';
        sig += std::to_string(it.x);
        sig += ',';
        sig += std::to_string(it.y);
        sig += ':';
        sig += it.text;
        sig += '\n';
    }
}

} // namespace

int GameHudCanvas::scaleFor(int width, int height) {
    return std::max(1, int(std::min(width / 400.0f, height / 300.0f)));
}

void GameHudCanvas::begin(int width, int height) {
    image_.width = std::max(0, width);
    image_.height = std::max(0, height);
    image_.rgba.assign(size_t(image_.width) * size_t(image_.height) * 4, 0);
}

void GameHudCanvas::blend(int x, int y, const Rgba &c) {
    if (x < 0 || y < 0 || x >= image_.width || y >= image_.height || c[3] == 0) return;
    unsigned char *d = &image_.rgba[(size_t(y) * size_t(image_.width) + size_t(x)) * 4];
    if (c[3] == 255 || d[3] == 0) {
        std::copy(c.begin(), c.end(), d);
        return;
    }
    // straight-alpha "over"
    const float sa = c[3] / 255.0f, da = d[3] / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    for (int i = 0; i < 3; i++)
        d[i] = (unsigned char)std::lround((c[size_t(i)] * sa + d[i] * da * (1.0f - sa)) / oa);
    d[3] = (unsigned char)std::lround(oa * 255.0f);
}

void GameHudCanvas::fillRect(int x, int y, int w, int h, const Rgba &color) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(image_.width, x + w), y1 = std::min(image_.height, y + h);
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) blend(px, py, color);
}

void GameHudCanvas::blit(const RgbaImage &src, int x, int y) {
    for (int sy = 0; sy < src.height; sy++)
        for (int sx = 0; sx < src.width; sx++) {
            const unsigned char *s = &src.rgba[(size_t(sy) * size_t(src.width) + size_t(sx)) * 4];
            blend(x + sx, y + sy, {s[0], s[1], s[2], s[3]});
        }
}

void GameHudCanvas::drawText(int x, int y, const std::string &text, const Rgba &color, int size) {
    const auto &glyphs = ekron::font::glyphBitmaps();
    size_t i = 0;
    for (int cx = x; i < text.size(); cx += ekron::font::kGlyphW * size) {
        const uint64_t bits = glyphs[size_t(ekron::font::glyphSlot(ekron::font::nextUtf8Codepoint(text, i)))];
        for (int row = 0; row < ekron::font::kGlyphH; row++)
            for (int col = 0; col < ekron::font::kGlyphW; col++) {
                if (!((bits >> (63 - (row * 8 + col))) & 1)) continue;
                for (int py = 0; py < size; py++)
                    for (int px = 0; px < size; px++) blend(cx + col * size + px, y + row * size + py, color);
            }
    }
}

const RgbaImage &GameHudCanvas::pic(const std::string &name, const FileLoader &load) {
    const auto it = pics_.find(name);
    if (it != pics_.end()) return it->second;
    static const RgbaImage kNone;
    if (pics_.size() >= 256) return kNone; // full: drawn as missing, not cached
    RgbaImage img;
    std::vector<unsigned char> bytes;
    std::string error;
    if (!load || !load(gameLayoutPicPath(name), bytes) || !decodePcx(bytes, img, error, 255) || img.width <= 0 ||
        img.height <= 0)
        img = RgbaImage{};
    return pics_[name] = std::move(img);
}

void GameHudCanvas::drawItems(const std::vector<GameLayoutItem> &items, const FileLoader &load, int offsetY) {
    for (const GameLayoutItem &item : items) {
        const int x = item.x, y = item.y + offsetY;
        switch (item.kind) {
        case GameLayoutItem::Kind::Pic: {
            const RgbaImage &p = pic(item.text, load);
            if (p.width > 0) blit(p, x, y);
            break;
        }
        case GameLayoutItem::Kind::Text:
            drawText(x, y, item.text, item.alt ? kAltText : kText, 1);
            break;
        case GameLayoutItem::Kind::Number:
            for (size_t i = 0; i < item.text.size(); i++) {
                const int dx = x + int(i) * 24;
                const RgbaImage &p = pic(gameLayoutDigitPic(item.text[i], item.alt), load);
                if (p.width > 0) blit(p, dx, y);
                else drawText(dx, y, std::string(1, item.text[i]), item.alt ? kAltDigits : kDigits, 3);
            }
            break;
        }
    }
}

int GameHudCanvas::viewSize(const Engine &engine) { return clampViewSize(engine.cvarFloat("viewsize", 100.0f)); }

ViewRect GameHudCanvas::viewRect(const Engine &engine, const Quake2Module &q2, int width, int height) {
    const int size = q2.gameRunning() ? viewSize(engine) : 100;
    return screenViewRect(width, height, scaleFor(std::max(width, 1), std::max(height, 1)), size);
}

void GameHudCanvas::tileClear(const ViewRect &view, const FileLoader &load) {
    const RgbaImage &tile = pic("backtile", load);
    const int w = image_.width, h = image_.height;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (x >= view.x && x < view.x + view.width && y >= view.y && y < view.y + view.height) {
                x = view.x + view.width - 1; // skip the view's row
                continue;
            }
            if (tile.width <= 0) {
                blend(x, y, kBackFill);
                continue;
            }
            const unsigned char *s =
                &tile.rgba[(size_t(y % tile.height) * size_t(tile.width) + size_t(x % tile.width)) * 4];
            blend(x, y, {s[0], s[1], s[2], s[3]});
        }
}

int GameHudCanvas::crosshairStyle(const Engine &engine) {
    const float v = engine.cvarFloat("crosshair", 1.0f);
    if (!std::isfinite(v)) return 3;
    const int style = int(v); // Cvar_Get's value, truncated by the "ch%i" format
    return style < 0 || style > 3 ? 3 : style;
}

bool GameHudCanvas::drawCrosshair(int style, const FileLoader &load) {
    if (style < 1 || style > 3) return false;
    const RgbaImage &img = pic("ch" + std::to_string(style), load);
    if (img.rgba.empty()) return false;
    // ((width - pic width) >> 1, (height - pic height) >> 1), as in Quake 2
    blit(img, (image_.width - img.width) >> 1, (image_.height - img.height) >> 1);
    return true;
}

bool GameHudCanvas::update(const Engine &engine, const Quake2Module &q2, int width, int height, int barBottom,
                           bool statusBar, double seconds) {
    if (!q2.gameRunning() || width <= 0 || height <= 0) {
        crosshairDrawn_ = false;
        if (empty_) return false;
        empty_ = true;
        signature_.clear();
        begin(0, 0);
        return true;
    }
    const int s = scaleFor(width, height);
    const int w = width / s, h = height / s;
    std::vector<GameLayoutItem> bar, layout, inventory;
    if (statusBar && barBottom > 0) bar = q2.statusBarItems(w, std::min(barBottom, height) / s);
    const bool layoutOn = q2.layoutShown(), inventoryOn = q2.inventoryShown();
    if (layoutOn) layout = q2.layoutItems(w, h);
    if (inventoryOn) inventory = q2.inventoryItems(w, h, std::fmod(seconds * 10.0, 2.0) >= 1.0);
    const FileLoader load = [&engine](const std::string &path, std::vector<unsigned char> &out) {
        return engine.loadFile(path, out);
    };
    const int style = crosshairStyle(engine);
    const bool crosshair = style > 0 && !pic("ch" + std::to_string(style), load).rgba.empty();
    const int size = viewSize(engine);

    std::string sig = std::to_string(w) + 'x' + std::to_string(h) + (layoutOn ? 'L' : '-') + (inventoryOn ? 'I' : '-');
    sig += crosshair ? char('0' + style) : '-';
    sig += 'V' + std::to_string(size);
    sig += '\n';
    appendSignature(sig, bar);
    sig += "--\n";
    appendSignature(sig, layout);
    sig += "--\n";
    appendSignature(sig, inventory);
    const bool nothing = bar.empty() && !layoutOn && !inventoryOn && !crosshair && size >= 100;
    crosshairDrawn_ = crosshair;
    if (sig == signature_ && empty_ == nothing) return false;
    signature_ = std::move(sig);
    scale_ = s;
    empty_ = nothing;
    if (nothing) {
        begin(0, 0);
        return true;
    }

    begin(w, h);
    if (size < 100) tileClear(realms::viewRect(w, h, size), load); // SCR_TileClear, before the 2D
    if (crosshair) drawCrosshair(style, load); // under the HUD, as SCR_DrawCrosshair runs first
    drawItems(bar, load);
    if (layoutOn) {
        const Panel p = layoutPanel(w, h);
        fillRect(p.x, p.y, p.w, p.h, kPanel);
        drawItems(layout, load);
    }
    if (inventoryOn) { // over the layout, as Quake 2 draws it
        const Panel p = inventoryPanel(w, h);
        fillRect(p.x, p.y, p.w, p.h, kPanel);
        drawItems(inventory, load);
    }
    return true;
}

} // namespace realms
