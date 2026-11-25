#pragma once
// Software mixer for the game sounds: any number of decoded clips and
// streamed .ogg files, resampled (with a pitch factor) to one float stereo
// output. The platform backend (SDL on desktop, AAudio on Android, an audio
// unit on iOS) calls render() from its audio thread; everything else is
// called from the game thread. One mutex guards the voices.

#include "SoundDecode.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ekron::audio {

enum class Bus { Effects = 0, Music = 1, Ui = 2 };

struct PlayParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f; // -1 left .. +1 right
    bool loop = false;
    Bus bus = Bus::Effects;
    float fadeIn = 0.0f; // seconds
};

using VoiceId = uint32_t; // 0 = none

class Mixer {
public:
    static constexpr int kMaxVoices = 40;

    Mixer();
    ~Mixer();
    Mixer(const Mixer &) = delete;
    Mixer &operator=(const Mixer &) = delete;

    // Clips. A clip is either decoded PCM or the bytes of an .ogg to stream.
    int addClip(const std::string &name, Pcm pcm);
    int addStream(const std::string &name, std::vector<uint8_t> ogg);
    int findClip(const std::string &name) const;
    // Frees a clip's data (a long streamed track); its number stays taken and
    // plays nothing. Voices already playing it finish normally.
    void dropClip(int clip);
    int clipCount() const;
    std::string clipName(int clip) const;
    double clipSeconds(int clip) const;

    VoiceId play(int clip, const PlayParams &params);
    // Smoothly moves a playing voice to new settings (engine pitch, ...).
    void set(VoiceId voice, float volume, float pitch, float pan);
    void stop(VoiceId voice, float fadeSeconds = 0.06f);
    void stopAll(float fadeSeconds = 0.06f);
    bool playing(VoiceId voice) const;
    int activeVoices() const;

    void setBusVolume(Bus bus, float volume);
    float busVolume(Bus bus) const;
    void setMasterVolume(float volume);
    // Silences the output (app in the background) without losing voices.
    void setPaused(bool paused);
    bool paused() const { return paused_.load(); }

    // Audio thread: `frames` interleaved stereo float frames at outputRate().
    void setOutputRate(int rate);
    int outputRate() const { return outputRate_.load(); }
    void render(float *out, int frames);

private:
    struct Clip;
    struct Voice;
    std::vector<std::shared_ptr<const Clip>> clips_;
    std::vector<std::unique_ptr<Voice>> voices_;
    mutable std::mutex mutex_;
    std::array<float, 3> bus_{{1.0f, 1.0f, 1.0f}};
    float master_ = 1.0f;
    std::atomic<int> outputRate_{48000};
    std::atomic<bool> paused_{false};
    uint32_t nextId_ = 1;
    float pauseGain_ = 1.0f;

    Voice *findVoice(VoiceId id) const;
    bool fetch(Voice &v, float frame[2]);
};

// Clamped 0..1 volume (NaN -> fallback).
float clampVolume(float v, float fallback = 1.0f);

} // namespace ekron::audio
