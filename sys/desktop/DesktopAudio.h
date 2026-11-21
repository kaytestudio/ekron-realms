#pragma once
// SDL2 audio output for the game sound mixer (48 kHz float stereo when the
// device allows it; SDL converts otherwise). Failing to open a device (no
// sound card, CI) is not an error: the game just stays silent.

#include "SoundMixer.h"

#include <string>

namespace ekron::desktop {

class DesktopAudio {
public:
    DesktopAudio() = default;
    ~DesktopAudio();
    DesktopAudio(const DesktopAudio &) = delete;
    DesktopAudio &operator=(const DesktopAudio &) = delete;

    bool open(ekron::audio::Mixer &mixer, std::string &info);
    void close();
    bool isOpen() const { return device_ != 0; }

private:
    unsigned device_ = 0; // SDL_AudioDeviceID
    bool subsystem_ = false;
};

} // namespace ekron::desktop
