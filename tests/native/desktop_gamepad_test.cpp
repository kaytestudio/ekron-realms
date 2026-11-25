#include "../../sys/desktop/DesktopGamepad.h"

// Keep checks active in the Release builds used by desktop CI.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using ekron::desktop::PadBinding;
using ekron::desktop::PadButton;
using ekron::desktop::PadContext;
using ekron::desktop::PadKey;
using Kind = PadBinding::Kind;

namespace {

bool isKey(const PadBinding &b, PadKey key) { return b.kind == Kind::Key && b.key == key; }
bool isCommand(const PadBinding &b, const char *line) {
    return b.kind == Kind::GameCommand && b.command && std::strcmp(b.command, line) == 0;
}

} // namespace

int main() {
    using ekron::desktop::padAxis;
    using ekron::desktop::padBinding;
    using ekron::desktop::padLookRate;
    using ekron::desktop::padTriggerDown;

    // Start always pauses / closes, whatever is open.
    for (const PadContext &c : {PadContext{}, PadContext{true, false, false, false}, PadContext{false, true, false, false},
                                PadContext{false, false, true, false}, PadContext{false, false, true, true}})
        assert(isKey(padBinding(PadButton::Start, c), PadKey::Escape));

    // Pause menu: d-pad selects, A presses, B closes, nothing else leaks into the game.
    const PadContext paused{true, false, true, false};
    assert(padBinding(PadButton::DpadUp, paused).kind == Kind::MenuUp);
    assert(padBinding(PadButton::DpadDown, paused).kind == Kind::MenuDown);
    assert(padBinding(PadButton::A, paused).kind == Kind::MenuConfirm);
    assert(isKey(padBinding(PadButton::B, paused), PadKey::Escape));
    for (PadButton b : {PadButton::X, PadButton::Y, PadButton::LeftShoulder, PadButton::RightShoulder,
                        PadButton::RightTrigger, PadButton::DpadLeft})
        assert(padBinding(b, paused).kind == Kind::None);

    // The mouse-only catalog / store: B closes them, nothing else.
    const PadContext panel{false, true, false, false};
    assert(isKey(padBinding(PadButton::B, panel), PadKey::Escape));
    assert(padBinding(PadButton::A, panel).kind == Kind::None);
    assert(padBinding(PadButton::RightTrigger, panel).kind == Kind::None);

    // A Quake 2 map: weapons on the shoulders, the inventory on X / Y / d-pad.
    const PadContext map{false, false, true, false};
    assert(isCommand(padBinding(PadButton::LeftShoulder, map), "weapprev"));
    assert(isCommand(padBinding(PadButton::RightShoulder, map), "weapnext"));
    assert(isKey(padBinding(PadButton::X, map), PadKey::Return));
    assert(isKey(padBinding(PadButton::Y, map), PadKey::Tab));
    assert(isKey(padBinding(PadButton::DpadLeft, map), PadKey::LeftBracket));
    assert(isKey(padBinding(PadButton::DpadRight, map), PadKey::RightBracket));
    assert(isKey(padBinding(PadButton::DpadUp, map), PadKey::F1));
    assert(isCommand(padBinding(PadButton::DpadDown, map), "score"));
    // jump, crouch and attack are held controls there, not presses
    for (PadButton b : {PadButton::A, PadButton::B, PadButton::RightTrigger, PadButton::LeftTrigger})
        assert(padBinding(b, map).kind == Kind::None);
    // Back + shoulders: quick save / load, and nothing else while Back is held
    const PadContext mapBack{false, false, true, true};
    assert(isKey(padBinding(PadButton::LeftShoulder, mapBack), PadKey::F6));
    assert(isKey(padBinding(PadButton::RightShoulder, mapBack), PadKey::F9));
    assert(padBinding(PadButton::X, mapBack).kind == Kind::None);

    // The sandbox city: the keyboard's jump / crouch / interact / rotate, the mouse on the triggers.
    const PadContext city{};
    assert(isKey(padBinding(PadButton::A, city), PadKey::Space));
    assert(isKey(padBinding(PadButton::B, city), PadKey::C));
    assert(isKey(padBinding(PadButton::X, city), PadKey::E));
    assert(isKey(padBinding(PadButton::Y, city), PadKey::R));
    assert(padBinding(PadButton::RightTrigger, city).kind == Kind::MouseLeft);
    assert(padBinding(PadButton::LeftTrigger, city).kind == Kind::MouseRight);
    assert(padBinding(PadButton::LeftShoulder, city).kind == Kind::PrevTool);
    assert(padBinding(PadButton::RightShoulder, city).kind == Kind::NextTool);
    assert(padBinding(PadButton::DpadUp, city).kind == Kind::None); // walks (PadFrame)

    // Sticks: a dead zone, then a ramp to full speed both ways.
    assert(padAxis(0) == 0.0f);
    assert(padAxis(5000) == 0.0f && padAxis(-5000) == 0.0f);
    assert(padAxis(32767) == 1.0f);
    assert(padAxis(-32768) == -1.0f);
    const float half = padAxis(int(0.6f * 32767.0f));
    assert(half > 0.45f && half < 0.55f);
    assert(std::abs(padAxis(-int(0.6f * 32767.0f)) + half) < 1e-6f);
    // look: squared response, signed
    assert(padLookRate(0.0f) == 0.0f);
    assert(std::abs(padLookRate(1.0f) - ekron::desktop::kPadLookSpeed) < 1e-6f);
    assert(std::abs(padLookRate(-0.5f) + 0.25f * ekron::desktop::kPadLookSpeed) < 1e-6f);

    // Triggers: press past half, release below a quarter.
    assert(!padTriggerDown(12000, false));
    assert(padTriggerDown(20000, false));
    assert(padTriggerDown(12000, true));
    assert(!padTriggerDown(4000, true));

    std::cout << "desktop gamepad bindings OK\n";
    return 0;
}
