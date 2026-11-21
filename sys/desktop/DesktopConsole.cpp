#define SDL_MAIN_HANDLED
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

#include "DesktopConsole.h"
#include "DesktopFont.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ekron::desktop {

namespace {

bool isConsoleToggle(const SDL_KeyboardEvent &key) {
    return key.keysym.scancode == SDL_SCANCODE_GRAVE || key.keysym.sym == SDLK_BACKQUOTE;
}

[[maybe_unused]] Color levelColor(int level) {
    switch (level) {
        case REALMS_PRINT_WARNING: return {1.0f, 0.82f, 0.36f, 1.0f};
        case REALMS_PRINT_ERROR: return {1.0f, 0.42f, 0.40f, 1.0f};
        case REALMS_PRINT_DEVELOPER: return {0.55f, 0.66f, 0.72f, 1.0f};
        default: return {0.86f, 0.95f, 0.90f, 1.0f};
    }
}

} // namespace

void DesktopConsole::setOpen(bool open) {
    if (open_ == open) return;
    open_ = open;
    scroll_ = 0;
    historyIndex_ = -1;
    if (open_) {
        swallowNextText_ = true; // the ` that opened us also produces SDL_TEXTINPUT
        SDL_StartTextInput();
    }
}

void DesktopConsole::insertText(const std::string &utf8) {
    for (char c : utf8) {
        if (c == '`' || c == '~' || c == '\n' || c == '\r') continue;
        if (input_.size() >= 240) break;
        input_ += c;
    }
}

void DesktopConsole::backspace() {
    if (input_.empty()) return;
    // Drop one UTF-8 code point.
    size_t i = input_.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(input_[i]) & 0xC0) == 0x80) --i;
    input_.erase(i);
}

void DesktopConsole::submit(realms::Engine &engine) {
    std::string line = input_;
    input_.clear();
    historyIndex_ = -1;
    scroll_ = 0;
    engine.print("]" + line);
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    line = line.substr(first);
    if (history_.empty() || history_.back() != line) history_.push_back(line);
    if (history_.size() > 64) history_.erase(history_.begin());
    // Quake 2 accepts an optional leading slash or backslash.
    if (line[0] == '/' || line[0] == '\\') line.erase(0, 1);
    engine.exec(line + "\n");
}

void DesktopConsole::historyUp() {
    if (history_.empty()) return;
    if (historyIndex_ < 0) historyIndex_ = int(history_.size()) - 1;
    else if (historyIndex_ > 0) --historyIndex_;
    input_ = history_[size_t(historyIndex_)];
}

void DesktopConsole::historyDown() {
    if (historyIndex_ < 0) return;
    if (historyIndex_ + 1 < int(history_.size())) {
        ++historyIndex_;
        input_ = history_[size_t(historyIndex_)];
    } else {
        historyIndex_ = -1;
        input_.clear();
    }
}

void DesktopConsole::complete(const realms::Engine &engine) {
    // Tab completion over cvar names (commands live in the core's registry).
    if (input_.empty() || input_.find(' ') != std::string::npos) return;
    std::string prefix = input_;
    for (auto &c : prefix) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::vector<std::string> matches;
    engine.forEachCvar([&](const std::string &name, const std::string &, int32_t) {
        std::string lower = name;
        for (auto &c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lower.rfind(prefix, 0) == 0) matches.push_back(name);
    });
    if (matches.size() == 1) input_ = matches[0] + " ";
}

bool DesktopConsole::handleEvent(const SDL_Event &ev, realms::Engine &engine) {
    if (ev.type == SDL_KEYDOWN && isConsoleToggle(ev.key)) {
        if (!ev.key.repeat) toggle();
        return true;
    }
    if (!open_) return false;

    switch (ev.type) {
        case SDL_TEXTINPUT:
            if (swallowNextText_) {
                swallowNextText_ = false;
                const std::string t = ev.text.text;
                if (t == "`" || t == "~") return true;
            }
            insertText(ev.text.text);
            return true;
        case SDL_KEYDOWN: {
            swallowNextText_ = false;
            const SDL_Keycode key = ev.key.keysym.sym;
            if (key == SDLK_ESCAPE) setOpen(false);
            else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) submit(engine);
            else if (key == SDLK_BACKSPACE) backspace();
            else if (key == SDLK_UP) historyUp();
            else if (key == SDLK_DOWN) historyDown();
            else if (key == SDLK_TAB) complete(engine);
            else if (key == SDLK_PAGEUP) scroll_ += 4;
            else if (key == SDLK_PAGEDOWN) scroll_ = std::max(0, scroll_ - 4);
            else if (key == SDLK_l && (ev.key.keysym.mod & KMOD_CTRL)) engine.clearConsole();
            return true;
        }
        case SDL_MOUSEWHEEL:
            scroll_ = std::max(0, scroll_ + (ev.wheel.y > 0 ? 2 : -2));
            return true;
        case SDL_KEYUP:
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            return true;
        default:
            return false;
    }
}

#if !defined(EKRON_CONSOLE_TEST_NO_RENDERER)
void DesktopConsole::draw(DesktopRenderer &renderer, int hudW, int hudH, const realms::Engine &engine, float dt) {
    if (!open_) return;
    caretTime_ += dt;

    const float scale = 1.5f;
    const float lineH = float(font::kGlyphH) * scale + 4.0f;
    const float height = std::floor(float(hudH) * 0.5f);
    const float w = float(hudW);

    renderer.drawRect(0.0f, 0.0f, w, height, {0.02f, 0.04f, 0.05f, 0.90f});
    renderer.drawRect(0.0f, height - 2.0f, w, 2.0f, {0.22f, 0.78f, 0.66f, 0.95f});

    const std::string banner = std::string(realms::kEngineName) + " " + realms::kEngineVersion + "  core: " +
                               engine.backendName() + (engine.pascalBackend() ? " (Free Pascal)" : " (C++)");
    const float bannerW = font::measureTextWidth(banner, 1.25f);
    renderer.drawText(w - bannerW - 12.0f, height - 16.0f, banner, 1.25f, {0.40f, 0.78f, 0.68f, 0.95f});

    // Input line.
    const float inputY = height - 22.0f - lineH;
    std::string prompt = "]" + input_;
    if (std::fmod(caretTime_, 1.0f) < 0.55f) prompt += "_";
    renderer.drawText(12.0f, inputY, prompt, scale, {1.0f, 1.0f, 1.0f, 1.0f});

    // Log, newest at the bottom.
    const auto &lines = engine.console();
    const int visible = std::max(1, int((inputY - 10.0f) / lineH));
    const int maxScroll = std::max(0, int(lines.size()) - visible);
    scroll_ = std::min(scroll_, maxScroll);
    int idx = int(lines.size()) - 1 - scroll_;
    float y = inputY - lineH - 4.0f;
    const float maxTextW = w - 24.0f;
    for (; idx >= 0 && y > 4.0f; --idx, y -= lineH) {
        std::string text = lines[size_t(idx)].text;
        while (!text.empty() && font::measureTextWidth(text, scale) > maxTextW) text.pop_back();
        renderer.drawText(12.0f, y, text, scale, levelColor(lines[size_t(idx)].level));
    }
    if (scroll_ > 0) renderer.drawText(w - 60.0f, 6.0f, "^ ^ ^", scale, {0.4f, 0.8f, 0.7f, 0.9f});
}
#endif

} // namespace ekron::desktop
