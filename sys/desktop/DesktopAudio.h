#pragma once
// SDL2 audio output for the game sound mixer (48 kHz float stereo when the
// device allows it; SDL converts otherwise). Failing to open a device (no
// sound card, CI) is not an error: the game just stays silent.
//
// It also plays the intro video's soundtrack (IntroAudioSink): queued PCM
// mixed over the game sound, at the device rate.

#include "IntroVideo.h"
#include "SoundMixer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ekron::desktop {

class DesktopAudio final : public IntroAudioSink {
public:
    DesktopAudio() = default;
    ~DesktopAudio() override;
    DesktopAudio(const DesktopAudio &) = delete;
    DesktopAudio &operator=(const DesktopAudio &) = delete;

    bool open(ekron::audio::Mixer &mixer, std::string &info);
    void close();
    bool isOpen() const { return device_ != 0; }

    // IntroAudioSink
    int sampleRate() const override { return device_ != 0 ? rate_ : 0; }
    void queueStereo(const float *samples, int frames) override;
    int64_t playedFrames() const override { return played_.load(); }
    int64_t queuedFrames() const override;
    void clearQueue() override;

    // Audio thread: game mix plus queued stream into `out` (interleaved stereo).
    void render(float *out, int frames);

private:
    unsigned device_ = 0; // SDL_AudioDeviceID
    bool subsystem_ = false;
    int rate_ = 0;
    ekron::audio::Mixer *mixer_ = nullptr;
    // Guarded by SDL_LockAudioDevice (the callback runs with the device locked).
    std::vector<float> stream_;
    size_t streamRead_ = 0; // samples (not frames) already played
    bool streamActive_ = false;
    std::atomic<int64_t> played_{0};
};

} // namespace ekron::desktop
