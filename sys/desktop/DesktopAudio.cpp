// SDL2 audio output (see DesktopAudio.h).

#include "DesktopAudio.h"

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

#include <cstring>

namespace ekron::desktop {

namespace {
void SDLCALL renderCallback(void *user, Uint8 *stream, int len) {
    auto *mixer = static_cast<ekron::audio::Mixer *>(user);
    const int frames = len / int(2 * sizeof(float));
    mixer->render(reinterpret_cast<float *>(stream), frames);
    const int rest = len - frames * int(2 * sizeof(float));
    if (rest > 0) std::memset(stream + (len - rest), 0, size_t(rest));
}
} // namespace

DesktopAudio::~DesktopAudio() { close(); }

bool DesktopAudio::open(ekron::audio::Mixer &mixer, std::string &info) {
    close();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        info = std::string("no audio: ") + SDL_GetError();
        return false;
    }
    subsystem_ = true;
    SDL_AudioSpec want{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = renderCallback;
    want.userdata = &mixer;
    SDL_AudioSpec have{};
    // The mixer resamples to any rate; format and channels stay float stereo
    // (SDL converts if the device wants something else).
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (device_ == 0) {
        info = std::string("no audio device: ") + SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystem_ = false;
        return false;
    }
    mixer.setOutputRate(have.freq);
    SDL_PauseAudioDevice(device_, 0);
    const char *driver = SDL_GetCurrentAudioDriver();
    info = std::string("audio: ") + (driver ? driver : "?") + ", " + std::to_string(have.freq) + " Hz, " +
           std::to_string(have.samples) + " frames";
    return true;
}

void DesktopAudio::close() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    if (subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystem_ = false;
    }
}

} // namespace ekron::desktop
