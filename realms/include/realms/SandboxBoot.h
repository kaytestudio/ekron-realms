// Realms Engine - shared sandbox boot sequence for Desktop, Android and iOS.
//
// Every host registers the same cvars and runs the same Quake 2 style
// start-up so a setting behaves identically on all platforms:
//
//   1. pak0.pak ... pak9.pak mounted from the asset roots (if present)
//   2. exec config/realms.cfg   (defaults shipped with the assets)
//   3. exec config.cfg          (archived user settings, writeconfig)
//   4. exec autoexec.cfg        (user overrides)
//   5. command line "+set x y +cmd" script
#pragma once

#include "realms/Engine.h"

#include <functional>
#include <string>

namespace realms::sandbox {

struct BootOptions {
    // "desktop-linux", "desktop-macos", "android", "ios" (NOSET cvar realms_platform).
    std::string platform;
    // Quake 2 command-line script (see Engine::commandLineScript).
    std::string commandLine;
    // Optional existence probe used before mounting PAKs (avoids reading
    // multi-megabyte archives just to check for them).
    std::function<bool(const std::string &path)> fileExists;
    bool execUserConfig = true;
};

// Cvars shared by every platform (sensitivity, m_invert, name...).
void registerSharedCvars(Engine &engine, const std::string &platform);

// Runs the full boot sequence. Returns the number of PAKs mounted.
int boot(Engine &engine, const BootOptions &options);

// Persists archived cvars to config.cfg in the writable root.
void saveUserConfig(Engine &engine);

// Mouse/touch look multiplier: 1.0 at the Quake 2 default sensitivity 3.
float lookScale(const Engine &engine);
// -1 when m_invert is set, +1 otherwise.
float pitchSign(const Engine &engine);

} // namespace realms::sandbox
