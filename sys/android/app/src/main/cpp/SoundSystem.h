#pragma once
// The game's sound: loads assets/sounds/ (SoundCatalog.h) into a Mixer on a
// worker thread, plays catalog sounds by name, keeps s_volume /
// s_musicvolume applied, and adds the console commands play, stopsound,
// soundlist and music. The platform owns the audio device and calls
// mixer().render() from it (DesktopAudio, AndroidAudio, IosAudio).

#include "SoundCatalog.h"
#include "SoundDirector.h"
#include "SoundMixer.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace realms {
class Engine;
}

namespace ekron::audio {

class SoundSystem : public SoundOut {
public:
    // Reads an asset ("sounds/jump.ogg") into memory. Called on the loader
    // thread, so it must not touch the engine.
    using ReadFile = std::function<bool(const std::string &path, std::vector<uint8_t> &out)>;

    SoundSystem() = default;
    ~SoundSystem() override;
    SoundSystem(const SoundSystem &) = delete;
    SoundSystem &operator=(const SoundSystem &) = delete;

    void loadAsync(ReadFile reader);
    // Synchronous load; returns the number of files that failed.
    int loadNow(const ReadFile &reader);
    bool ready() const { return ready_.load(); }
    // "loaded 56 files (2.0 MiB), 0 missing" once ready.
    std::string summary() const;

    Mixer &mixer() { return mixer_; }
    SoundDirector &director() { return director_; }

    // Console: play <sound> [volume] [pitch], stopsound, soundlist, music.
    void registerCommands(realms::Engine &engine);
    void unregisterCommands(realms::Engine &engine);
    // Applies s_volume / s_musicvolume (call once per frame).
    void syncCvars(const realms::Engine &engine);

    // Interface sounds (menus): "ui_click", "ui_open", ...
    void ui(const std::string &name) { play(name, 1.0f, 1.0f, 0.0f, false, 0.0f); }

    // SoundOut
    SoundHandle play(const std::string &name, float volume, float pitch, float pan, bool loop,
                     float fadeIn) override;
    void set(SoundHandle handle, float volume, float pitch, float pan) override;
    void stop(SoundHandle handle, float fadeSeconds) override;
    bool playing(SoundHandle handle) const override;

private:
    Mixer mixer_;
    SoundDirector director_;
    std::thread loader_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> cancel_{false};
    mutable std::mutex tableMutex_;
    std::vector<std::vector<int>> clips_; // per kSounds entry: mixer clip per variant
    std::vector<int> lastVariant_;
    std::vector<std::pair<SoundHandle, float>> loops_; // loop handle -> catalog volume
    std::string summary_;
    uint32_t rng_ = 0x2545f491u;
    float lastVolume_ = -1.0f, lastMusic_ = -1.0f;

    int pickClip(size_t sound);
};

} // namespace ekron::audio
