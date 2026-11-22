// Quake 2 game sounds on the desktop mixer (see DesktopGameSound.h).
#include "DesktopGameSound.h"

#include <algorithm>
#include <cstdio>

namespace ekron::desktop {

int DesktopGameSound::clip(realms::Engine &engine, ekron::audio::Mixer &mixer, const std::string &name) {
    const auto it = clips_.find(name);
    if (it != clips_.end()) return it->second;
    const std::string key = "q2:" + name; // kept by the mixer across maps
    int id = mixer.findClip(key);
    if (id < 0) {
        std::vector<unsigned char> bytes;
        realms::GameWav wav;
        std::string error;
        if (!engine.loadFile(name, bytes)) {
            error = "not found";
        } else if (realms::decodeGameWav(bytes.data(), bytes.size(), wav, &error)) {
            ekron::audio::Pcm pcm;
            pcm.rate = wav.rate;
            pcm.channels = wav.channels;
            pcm.samples = std::move(wav.samples);
            id = mixer.addClip(key, std::move(pcm));
            if (id < 0) error = "mixer refused it";
        }
        if (id < 0) engine.print("sound: " + name + ": " + error + "\n", REALMS_PRINT_DEVELOPER);
    }
    clips_[name] = id;
    return id;
}

void DesktopGameSound::update(realms::Engine &engine, ekron::audio::Mixer &mixer,
                              const std::vector<realms::GameEvent> &events,
                              const std::vector<realms::GameLoopSound> &loops, const realms::GameListener &listener,
                              double dt) {
    clockMs_ += std::clamp(dt, 0.0, 1.0) * 1000.0;
    scheduler_.push(events, clockMs_);
    scheduler_.due(clockMs_, listener, due_);
    for (const realms::GameSoundPlay &p : due_) {
        if (p.spatial.volume <= 0.0f) continue;
        const int c = clip(engine, mixer, p.name);
        if (c < 0) continue;
        // S_PickChannel: a sound on an entity's channel cuts the previous one
        const int key = p.entity * 8 + p.channel;
        if (p.channel != 0 && p.entity != 0) {
            const auto it = channels_.find(key);
            if (it != channels_.end()) mixer.stop(it->second, 0.02f);
        }
        ekron::audio::PlayParams params;
        params.volume = p.spatial.volume;
        params.pan = p.spatial.pan;
        const ekron::audio::VoiceId v = mixer.play(c, params);
        if (v && p.channel != 0 && p.entity != 0) channels_[key] = v;
    }
    for (auto it = channels_.begin(); it != channels_.end();)
        it = mixer.playing(it->second) ? std::next(it) : channels_.erase(it);

    realms::GameSoundScheduler::mergeLoops(loops, listener, merged_);
    std::unordered_map<std::string, ekron::audio::VoiceId> next;
    for (const realms::GameSoundPlay &p : merged_) {
        const auto it = loops_.find(p.name);
        if (it != loops_.end() && mixer.playing(it->second)) {
            mixer.set(it->second, p.spatial.volume, 1.0f, p.spatial.pan);
            next[p.name] = it->second;
            loops_.erase(it);
            continue;
        }
        const int c = clip(engine, mixer, p.name);
        if (c < 0) continue;
        ekron::audio::PlayParams params;
        params.volume = p.spatial.volume;
        params.pan = p.spatial.pan;
        params.loop = true;
        params.fadeIn = 0.05f;
        if (const ekron::audio::VoiceId v = mixer.play(c, params)) next[p.name] = v;
    }
    for (const auto &kv : loops_) mixer.stop(kv.second, 0.1f); // no entity plays it any more
    loops_.swap(next);
}

void DesktopGameSound::stop(ekron::audio::Mixer &mixer) {
    for (const auto &kv : channels_) mixer.stop(kv.second, 0.05f);
    for (const auto &kv : loops_) mixer.stop(kv.second, 0.1f);
    channels_.clear();
    loops_.clear();
    scheduler_.clear();
}

std::string DesktopGameSound::summary() const {
    size_t loaded = 0, missing = 0;
    for (const auto &kv : clips_) (kv.second >= 0 ? loaded : missing)++;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%zu game sounds loaded, %zu missing, %zu loops", loaded, missing, loops_.size());
    return buf;
}

} // namespace ekron::desktop
