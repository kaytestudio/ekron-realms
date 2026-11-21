// Desktop drop-down console: line editing, history and command submission
// into the Realms Engine (runs without a window or GL context).
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
#include "realms/SandboxBoot.h"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++failures;                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                             \
    } while (0)

SDL_Event keyDown(SDL_Keycode sym, SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN) {
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = scancode;
    return ev;
}

SDL_Event textInput(const char *text) {
    SDL_Event ev{};
    ev.type = SDL_TEXTINPUT;
    std::strncpy(ev.text.text, text, sizeof(ev.text.text) - 1);
    return ev;
}
} // namespace

int main() {
    realms::Engine engine;
    realms::EngineConfig cfg;
    cfg.backend = realms::BackendPreference::Native;
    CHECK(engine.init(cfg));
    realms::sandbox::registerSharedCvars(engine, "desktop-test");

    int noclips = 0;
    engine.addCommand("noclip", [&](const realms::CommandArgs &) { ++noclips; });

    ekron::desktop::DesktopConsole console;
    CHECK(!console.isOpen());
    CHECK(!console.handleEvent(textInput("w"), engine)); // closed: gameplay keeps input

    // ` opens the console and its own text event is swallowed.
    CHECK(console.handleEvent(keyDown(SDLK_BACKQUOTE, SDL_SCANCODE_GRAVE), engine));
    CHECK(console.isOpen());
    CHECK(console.handleEvent(textInput("`"), engine));
    CHECK(console.input().empty());

    for (const char *t : {"s", "e", "t", " ", "sensitivity", " ", "6"}) console.handleEvent(textInput(t), engine);
    CHECK(console.input() == "set sensitivity 6");
    console.handleEvent(keyDown(SDLK_RETURN), engine);
    CHECK(console.input().empty());
    CHECK(engine.cvarFloat("sensitivity") == 6.0f);
    CHECK(realms::sandbox::lookScale(engine) == 2.0f);

    console.insertText("/noclip");
    console.submit(engine);
    CHECK(noclips == 1);

    // History recall and UTF-8 aware backspace.
    console.historyUp();
    CHECK(console.input() == "/noclip");
    console.historyUp();
    CHECK(console.input() == "set sensitivity 6");
    console.historyDown();
    CHECK(console.input() == "/noclip");
    console.historyDown();
    CHECK(console.input().empty());
    console.insertText("Ол");
    console.backspace();
    CHECK(console.input() == "О");
    console.backspace();
    CHECK(console.input().empty());

    // Tab completes cvar names.
    console.insertText("m_inv");
    console.complete(engine);
    CHECK(console.input() == "m_invert ");

    // Echo of submitted lines lands in the engine console.
    bool echoed = false;
    for (const auto &line : engine.console())
        if (line.text == "]set sensitivity 6") echoed = true;
    CHECK(echoed);

    // Gameplay input is swallowed while open; Escape closes.
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    CHECK(console.handleEvent(motion, engine));
    CHECK(console.handleEvent(keyDown(SDLK_ESCAPE), engine));
    CHECK(!console.isOpen());
    CHECK(!console.handleEvent(motion, engine));

    engine.shutdown();
    std::printf("[realms] desktop console test: %s\n", failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
