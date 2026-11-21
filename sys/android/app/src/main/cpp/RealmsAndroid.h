#pragma once
// Realms Engine host glue for Android.
//
// One realms::Engine lives for the whole process (GameActivity recreates the
// Renderer with every EGL surface). Files resolve from the app's internal
// storage first (config.cfg written by "writeconfig") and then from the APK
// assets through AAssetManager. When librealms_core.so (Free Pascal Quake 2
// core, built with realms/pascal/build_core.sh android-aarch64) is packaged in
// jniLibs it is loaded automatically; otherwise the C++ reference core runs.

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
