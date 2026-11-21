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

struct android_app;

namespace realms_android {

// Initialises the engine on first use and returns it. Must be called from
// the native game thread.
realms::Engine &ensureEngine(android_app *app);

// Persists archived cvars (config.cfg in internal storage).
void saveConfig();

} // namespace realms_android
