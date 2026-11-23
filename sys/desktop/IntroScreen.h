#pragma once
// Startup screens of the desktop client: the intro video (IntroVideo.h),
// then "Loading the world…" until the world has finished loading on its
// worker thread.

#include "DesktopHudLayout.h"
#include "GlLoader.h"
#include "IntroVideo.h"

#include <functional>
#include <memory>

struct SDL_Window;

namespace ekron::desktop {

class DesktopRenderer;

// Plays `intro` (may be null) until it ends or the player skips it (Esc,
// Space, Enter or a mouse click), then releases it (which stops its sound)
// and shows the loading screen while `stillLoading()` returns true.
// Returns false if the window was closed.
bool runStartupScreens(SDL_Window *window, ekron::gl::Api &gl, DesktopRenderer &renderer,
                       std::unique_ptr<IntroVideo> &intro,
                       const std::function<bool()> &stillLoading);

} // namespace ekron::desktop
