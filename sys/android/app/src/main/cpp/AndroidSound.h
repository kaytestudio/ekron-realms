#pragma once
// Android sound: the shared SoundSystem played through an AAudio stream
// (float stereo, low latency, usage GAME so the media volume keys apply).
// The stream is closed while the activity is paused and reopened on resume
// or when the output device changes (headphones, Bluetooth).

#include "SoundSystem.h"

struct android_app;

namespace android_sound {

// Starts loading assets/sounds and opens the stream on first call; later
// calls only return the system. Game thread.
ekron::audio::SoundSystem &start(android_app *app);
// The system if start() already ran (any thread), else nullptr.
ekron::audio::SoundSystem *get();
// Activity lifecycle (APP_CMD_RESUME / APP_CMD_PAUSE).
void resume();
void pause();

} // namespace android_sound
