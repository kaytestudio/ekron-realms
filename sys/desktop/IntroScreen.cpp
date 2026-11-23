// Startup screens (see IntroScreen.h).

#include "IntroScreen.h"

#include "DesktopFont.h"
#include "DesktopHudLayout.h"
#include "DesktopRenderer.h"
#include "Localization.h"

#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#else
#include <SDL.h>
#endif

#include <string>

namespace ekron::desktop {

namespace {

namespace i18n = ekron::i18n;

// One RGBA8 texture, refilled whenever the video shows a new frame.
class FrameTexture {
public:
    explicit FrameTexture(ekron::gl::Api &gl) : gl_(gl) {}
    ~FrameTexture() {
        if (texture_) gl_.DeleteTextures(1, &texture_);
    }
    FrameTexture(const FrameTexture &) = delete;
    FrameTexture &operator=(const FrameTexture &) = delete;

    void update(const IntroFrame &f) {
        if (!f.pixels || f.width <= 0 || f.height <= 0 || f.serial == serial_) return;
        serial_ = f.serial;
        const GLenum format = f.bgra ? GL_BGRA : GL_RGBA;
        gl_.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl_.PixelStorei(GL_UNPACK_ROW_LENGTH, f.stride / 4);
        if (!texture_) gl_.GenTextures(1, &texture_);
        gl_.BindTexture(GL_TEXTURE_2D, texture_);
        if (f.width != width_ || f.height != height_) {
            width_ = f.width;
            height_ = f.height;
            gl_.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), width_, height_, 0, format,
                           GL_UNSIGNED_BYTE, f.pixels);
            gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
            gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
            gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
            gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        } else {
            gl_.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, format, GL_UNSIGNED_BYTE, f.pixels);
        }
        gl_.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    GLuint id() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    ekron::gl::Api &gl_;
    GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    uint64_t serial_ = 0;
};

bool isSkipKey(SDL_Keycode key) {
    return key == SDLK_ESCAPE || key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER;
}

} // namespace

bool runStartupScreens(SDL_Window *window, ekron::gl::Api &gl, DesktopRenderer &renderer,
                       std::unique_ptr<IntroVideo> &intro, const std::function<bool()> &stillLoading) {
    FrameTexture frame(gl);
    bool playing = intro != nullptr;
    const std::string skipHint = i18n::tr(i18n::Str::IntroSkip);
    const std::string brand = i18n::tr(i18n::Str::BrandName);
    const std::string loadingText = i18n::tr(i18n::Str::LoadingWorld);

    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return false;
            if (!playing) continue;
            if ((ev.type == SDL_KEYDOWN && !ev.key.repeat && isSkipKey(ev.key.keysym.sym)) ||
                ev.type == SDL_MOUSEBUTTONDOWN)
                playing = false;
        }
        if (playing && !intro->update()) playing = false;
        if (!playing && intro) intro.reset(); // ended or skipped: silence, free the decoder
        if (!playing && !stillLoading()) return true;

        int winW = 0, winH = 0, drawW = 0, drawH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &drawW, &drawH);
        const auto layout = HudLayout::forWindow(winW, winH);
        const float W = float(layout.width), H = float(layout.height);

        gl.Viewport(0, 0, drawW, drawH);
        gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer.beginUi(layout.width, layout.height);
        if (playing) {
            frame.update(intro->frame());
            if (frame.id()) {
                const ScreenRect r = letterbox(frame.width(), frame.height(), W, H);
                renderer.drawIcon(gl, frame.id(), r.x, r.y, r.w, r.h);
            }
            const float scale = 2.0f, pad = 8.0f;
            const float textW = ekron::font::measureTextWidth(skipHint, scale);
            const float textH = float(ekron::font::kGlyphH) * scale;
            const float x = W - textW - 24.0f - pad, y = H - textH - 20.0f - pad;
            renderer.drawRect(x - pad, y - pad, textW + 2.0f * pad, textH + 2.0f * pad, {0.0f, 0.0f, 0.0f, 0.55f});
            renderer.drawText(x, y, skipHint, scale, {1.0f, 1.0f, 1.0f, 0.9f});
        } else {
            renderer.drawCenteredText(W * 0.5f, H * 0.5f - 28.0f, brand, 6.0f, {1.0f, 1.0f, 1.0f, 1.0f});
            renderer.drawCenteredText(W * 0.5f, H * 0.5f + 30.0f, loadingText, 2.0f, {0.78f, 0.84f, 0.9f, 1.0f});
        }
        renderer.flushUi(gl, drawW, drawH);
        SDL_GL_SwapWindow(window);
        if (!playing) SDL_Delay(10); // the loading screen doesn't need vsync-rate redraws
    }
}

} // namespace ekron::desktop
