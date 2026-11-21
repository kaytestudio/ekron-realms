#pragma once
// Quake 2 style drop-down console for the desktop sandbox (toggle with `).
// Displays the Realms Engine console log and feeds typed lines into the
// engine's command buffer (Pascal or C++ core through the Realms bridge).

#include "DesktopRenderer.h"
#include "realms/Engine.h"

#include <string>
#include <vector>

union SDL_Event;

namespace ekron::desktop {

class DesktopConsole {
public:
    bool isOpen() const { return open_; }
    void setOpen(bool open);
    void toggle() { setOpen(!open_); }

    // Returns true when the event was consumed by the console.
    bool handleEvent(const SDL_Event &ev, realms::Engine &engine);

    void draw(DesktopRenderer &renderer, int hudW, int hudH, const realms::Engine &engine, float dt);

    // Line editing helpers (SDL independent, unit tested).
    void insertText(const std::string &utf8);
    void backspace();
    void submit(realms::Engine &engine);
    void historyUp();
    void historyDown();
    const std::string &input() const { return input_; }
    void complete(const realms::Engine &engine);

private:
    bool open_ = false;
    bool swallowNextText_ = false;
    std::string input_;
    std::vector<std::string> history_;
    int historyIndex_ = -1;
    int scroll_ = 0;
    float caretTime_ = 0.0f;
};

} // namespace ekron::desktop
