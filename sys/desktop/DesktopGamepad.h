#pragma once
// Game controller bindings for the desktop client, so it plays with a gamepad
// alone (handhelds such as the Anbernic RG40XXH have no keyboard or mouse).
// SDL-independent: tests/native/desktop_gamepad_test.cpp checks it, and
// main.cpp turns a binding into the key / mouse event the client already
// handles, so every pad action does exactly what its keyboard key does.
// docs/CONTROLS.md lists the layout.

#include <algorithm>
#include <cmath>

namespace ekron::desktop {

// SDL_GameControllerButton values (main.cpp static_asserts them), plus the
// two triggers, which SDL reports as axes.
enum class PadButton : int {
    A = 0,
    B,
    X,
    Y,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftTrigger = 100,
    RightTrigger,
};

// The keyboard keys a binding stands for (main.cpp maps them to SDL keycodes).
enum class PadKey { Escape, Space, C, E, R, Tab, Return, LeftBracket, RightBracket, F1, F6, F9 };

struct PadBinding {
    enum class Kind {
        None,
        Key,         // press `key`
        MouseLeft,   // a left click (sandbox: grab / place / use the tool)
        MouseRight,  // a right click (sandbox: freeze / cancel)
        GameCommand, // a Quake 2 client command (`command`)
        MenuUp,      // pause menu: select the previous button
        MenuDown,    // pause menu: select the next button
        MenuConfirm, // pause menu: press the selected button
        PrevTool,    // sandbox: the previous hand tool (keys 1-3)
        NextTool,    // sandbox: the next hand tool
    };
    Kind kind = Kind::None;
    PadKey key = PadKey::Escape;
    const char *command = nullptr;
};

struct PadContext {
    bool pauseOpen = false; // the pause menu
    bool panelOpen = false; // the build catalog or the store (mouse only: B closes them)
    bool mapMode = false;   // a Quake 2 map
    bool backHeld = false;  // Back / Select held: shoulder buttons save and load
};

// What pressing `button` does. Held controls (move, look, jump and crouch in
// a map, attack) are read every frame instead: see PadFrame.
inline PadBinding padBinding(PadButton button, const PadContext &context) {
    using Kind = PadBinding::Kind;
    const auto key = [](PadKey k) { return PadBinding{Kind::Key, k, nullptr}; };
    const auto command = [](const char *line) { return PadBinding{Kind::GameCommand, PadKey::Escape, line}; };
    const auto kind = [](Kind k) { return PadBinding{k, PadKey::Escape, nullptr}; };

    if (button == PadButton::Start) return key(PadKey::Escape); // pause / close
    if (context.pauseOpen) {
        switch (button) {
        case PadButton::DpadUp: return kind(Kind::MenuUp);
        case PadButton::DpadDown: return kind(Kind::MenuDown);
        case PadButton::A: return kind(Kind::MenuConfirm);
        case PadButton::B: return key(PadKey::Escape);
        default: return {};
        }
    }
    if (context.panelOpen) return button == PadButton::B ? key(PadKey::Escape) : PadBinding{};
    if (context.mapMode) {
        if (context.backHeld) { // Quake 2's quick save / load (F6 / F9)
            if (button == PadButton::LeftShoulder) return key(PadKey::F6);
            if (button == PadButton::RightShoulder) return key(PadKey::F9);
            return {};
        }
        switch (button) {
        case PadButton::LeftShoulder: return command("weapprev");
        case PadButton::RightShoulder: return command("weapnext");
        case PadButton::X: return key(PadKey::Return);           // invuse
        case PadButton::Y: return key(PadKey::Tab);              // inven
        case PadButton::DpadLeft: return key(PadKey::LeftBracket);   // invprev
        case PadButton::DpadRight: return key(PadKey::RightBracket); // invnext
        case PadButton::DpadUp: return key(PadKey::F1);          // the help computer
        case PadButton::DpadDown: return command("score");
        default: return {}; // A / B / triggers: held (PadFrame)
        }
    }
    switch (button) { // the sandbox city
    case PadButton::A: return key(PadKey::Space); // jump
    case PadButton::B: return key(PadKey::C);     // crouch
    case PadButton::X: return key(PadKey::E);     // interact
    case PadButton::Y: return key(PadKey::R);     // rotate
    case PadButton::RightTrigger: return kind(Kind::MouseLeft);
    case PadButton::LeftTrigger: return kind(Kind::MouseRight);
    case PadButton::LeftShoulder: return kind(Kind::PrevTool);
    case PadButton::RightShoulder: return kind(Kind::NextTool);
    default: return {};
    }
}

// A stick axis (-32768..32767) as -1..1: nothing inside the dead zone, then
// a linear ramp so the stick still reaches full speed.
inline float padAxis(int raw, float deadZone = 0.2f) {
    const float v = std::clamp(float(raw) / 32767.0f, -1.0f, 1.0f);
    const float a = std::abs(v);
    if (a <= deadZone) return 0.0f;
    return std::copysign(std::min(1.0f, (a - deadZone) / (1.0f - deadZone)), v);
}

// Right stick look speed at full tilt, radians per second (before the
// sensitivity cvar); squared so small tilts aim finely.
constexpr float kPadLookSpeed = 2.8f;
inline float padLookRate(float axis) { return std::copysign(axis * axis, axis) * kPadLookSpeed; }

// A trigger (0..32767) as a button, with hysteresis so it doesn't chatter.
inline bool padTriggerDown(int raw, bool wasDown) { return wasDown ? raw > 8000 : raw > 16000; }

// The held controls, sampled every frame from every open controller.
struct PadFrame {
    float moveRight = 0.0f, moveForward = 0.0f; // left stick (and the d-pad in the sandbox)
    float lookRight = 0.0f, lookUp = 0.0f;      // right stick, radians per second
    bool jump = false;                          // A (and LT in a map)
    bool crouch = false;                        // B
    bool attack = false;                        // RT
    bool any() const {
        return moveRight != 0.0f || moveForward != 0.0f || lookRight != 0.0f || lookUp != 0.0f || jump || crouch ||
               attack;
    }
};

} // namespace ekron::desktop
