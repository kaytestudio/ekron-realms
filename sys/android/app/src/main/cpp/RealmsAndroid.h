#pragma once
// The Android realms::Engine, one per process (the Renderer is recreated with
// each EGL surface). Files come from internal storage first, then the APK
// assets.

#include "realms/Engine.h"

#include <string>
#include <vector>

struct android_app;

namespace realms_android {

// Initialises the engine on first use and returns it. Must be called from
// the native game thread.
realms::Engine &ensureEngine(android_app *app);

// Persists archived cvars (config.cfg in internal storage).
void saveConfig();

// Reads internal storage, then the APK assets (not PAKs). Thread safe, for
// worker threads that must not touch the engine.
bool readFile(const std::string &path, std::vector<unsigned char> &out);

// "map" / "killserver" notifications (BootOptions::onMapChanged), queued on
// the game thread while the engine runs commands. Returns true and the map
// path ("" = unloaded) once per change.
bool takeMapChange(std::string &path);

} // namespace realms_android
