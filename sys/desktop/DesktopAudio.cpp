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

#include <algorithm>
#include <cstring>

namespace ekron::desktop {

namespace {
void SDLCALL renderCallback(void *user, Uint8 *stream, int len) {
    auto *audio = static_cast<DesktopAudio *>(user);
    const int frames = len / int(2 * sizeof(float));
    audio->render(reinterpret_cast<float *>(stream), frames);
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
    want.userdata = this;
    mixer_ = &mixer;
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
    rate_ = have.freq;
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
    stream_.clear();
    streamRead_ = 0;
    streamActive_ = false;
    if (subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystem_ = false;
    }
}

void DesktopAudio::render(float *out, int frames) {
    mixer_->render(out, frames);
    const size_t available = stream_.size() - streamRead_;
    const size_t samples = std::min(available, size_t(frames) * 2);
    const float *in = stream_.data() + streamRead_;
    for (size_t i = 0; i < samples; ++i) out[i] = std::clamp(out[i] + in[i], -1.0f, 1.0f);
    streamRead_ += samples;
    if (streamRead_ == stream_.size()) {
        stream_.clear();
        streamRead_ = 0;
    }
    // The stream clock runs from the first queued samples until clearQueue(),
    // through underflows too, like a real device.
    if (streamActive_) played_.fetch_add(frames);
}

void DesktopAudio::queueStereo(const float *samples, int frames) {
    if (device_ == 0 || frames <= 0) return;
    SDL_LockAudioDevice(device_);
    if (streamRead_ > 0 && streamRead_ * 2 >= stream_.size()) { // compact
        stream_.erase(stream_.begin(), stream_.begin() + std::ptrdiff_t(streamRead_));
        streamRead_ = 0;
    }
    stream_.insert(stream_.end(), samples, samples + size_t(frames) * 2);
    streamActive_ = true;
    SDL_UnlockAudioDevice(device_);
}

int64_t DesktopAudio::queuedFrames() const {
    if (device_ == 0) return 0;
    SDL_LockAudioDevice(device_);
    const int64_t frames = int64_t((stream_.size() - streamRead_) / 2);
    SDL_UnlockAudioDevice(device_);
    return frames;
}

void DesktopAudio::clearQueue() {
    if (device_ == 0) return;
    SDL_LockAudioDevice(device_);
    stream_.clear();
    streamRead_ = 0;
    streamActive_ = false;
    SDL_UnlockAudioDevice(device_);
}

} // namespace ekron::desktop
