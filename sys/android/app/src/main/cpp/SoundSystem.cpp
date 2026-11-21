// Game sound system (see SoundSystem.h).

#include "SoundSystem.h"

#include "realms/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ekron::audio {

SoundSystem::~SoundSystem() {
    cancel_.store(true);
    if (loader_.joinable()) loader_.join();
}

void SoundSystem::loadAsync(ReadFile reader) {
    if (loader_.joinable() || ready_.load()) return;
    loader_ = std::thread([this, reader = std::move(reader)]() { loadNow(reader); });
}

int SoundSystem::loadNow(const ReadFile &reader) {
    std::vector<std::vector<int>> table(kSoundCount);
    int failed = 0, files = 0;
    size_t bytes = 0;
    std::string firstError;
    for (size_t i = 0; i < kSoundCount && !cancel_.load(); ++i) {
        const SoundDef &def = kSounds[i];
        const int count = std::max(def.variants, 1);
        for (int v = 1; v <= count && !cancel_.load(); ++v) {
            const std::string path = soundFile(def, v);
            std::vector<uint8_t> data;
            int clip = -1;
            std::string error = "missing";
            if (reader && reader(path, data) && !data.empty()) {
                bytes += data.size();
                if (def.stream) {
                    clip = mixer_.addStream(soundClipName(def, v), std::move(data));
                    if (clip < 0) error = "not a valid .ogg";
                } else {
                    Pcm pcm;
                    if (decodeOgg(data.data(), data.size(), pcm, &error))
                        clip = mixer_.addClip(soundClipName(def, v), std::move(pcm));
                }
            }
            if (clip >= 0) {
                table[i].push_back(clip);
                ++files;
            } else {
                ++failed;
                if (firstError.empty()) firstError = path + ": " + error;
            }
        }
    }
    char text[256];
    std::snprintf(text, sizeof(text), "sound: %d files (%.1f MiB)%s%d missing%s%s", files,
                  double(bytes) / (1024.0 * 1024.0), ", ", failed, firstError.empty() ? "" : ", first: ",
                  firstError.c_str());
    {
        std::lock_guard<std::mutex> lock(tableMutex_);
        clips_ = std::move(table);
        lastVariant_.assign(kSoundCount, -1);
        summary_ = text;
    }
    ready_.store(true);
    return failed;
}

std::string SoundSystem::summary() const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    return ready_.load() ? summary_ : std::string("sound: loading");
}

int SoundSystem::pickClip(size_t sound) {
    // tableMutex_ held by the caller.
    const auto &list = clips_[sound];
    if (list.empty()) return -1;
    if (list.size() == 1) return list[0];
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    int pick = int(rng_ % uint32_t(list.size()));
    if (pick == lastVariant_[sound]) pick = (pick + 1) % int(list.size()); // never the same step twice
    lastVariant_[sound] = pick;
    return list[size_t(pick)];
}

SoundHandle SoundSystem::play(const std::string &name, float volume, float pitch, float pan, bool loop,
                              float fadeIn) {
    if (!ready_.load()) return 0;
    if (!(volume > 0.001f)) return 0;
    int clip = -1;
    const SoundDef *def = nullptr;
    {
        std::lock_guard<std::mutex> lock(tableMutex_);
        for (size_t i = 0; i < kSoundCount; ++i) {
            if (name == kSounds[i].name) {
                def = &kSounds[i];
                clip = pickClip(i);
                break;
            }
            // A single variant by its file name: "step_wood_3".
            if (kSounds[i].variants > 0 && name.rfind(std::string(kSounds[i].name) + "_", 0) == 0) {
                const int v = std::atoi(name.c_str() + std::char_traits<char>::length(kSounds[i].name) + 1);
                if (v >= 1 && size_t(v) <= clips_[i].size() && name == soundClipName(kSounds[i], v)) {
                    def = &kSounds[i];
                    clip = clips_[i][size_t(v - 1)];
                    break;
                }
            }
        }
    }
    if (!def || clip < 0) return 0;
    PlayParams p;
    p.volume = clampVolume(volume * def->volume, 0.0f);
    p.pitch = pitch;
    p.pan = pan;
    p.loop = loop;
    p.fadeIn = fadeIn;
    p.bus = def->bus == 1 ? Bus::Music : def->bus == 2 ? Bus::Ui : Bus::Effects;
    const SoundHandle handle = mixer_.play(clip, p);
    if (handle && loop) {
        std::lock_guard<std::mutex> lock(tableMutex_);
        loops_.erase(std::remove_if(loops_.begin(), loops_.end(),
                                    [this](const std::pair<SoundHandle, float> &l) { return !mixer_.playing(l.first); }),
                     loops_.end());
        loops_.emplace_back(handle, def->volume);
    }
    return handle;
}

void SoundSystem::set(SoundHandle handle, float volume, float pitch, float pan) {
    if (!handle) return;
    float base = 1.0f;
    {
        // Loops keep their catalog volume.
        std::lock_guard<std::mutex> lock(tableMutex_);
        for (const auto &l : loops_)
            if (l.first == handle) base = l.second;
    }
    mixer_.set(handle, clampVolume(volume * base, 0.0f), pitch, pan);
}

void SoundSystem::stop(SoundHandle handle, float fadeSeconds) {
    if (handle) mixer_.stop(handle, fadeSeconds);
}

bool SoundSystem::playing(SoundHandle handle) const { return handle && mixer_.playing(handle); }

void SoundSystem::syncCvars(const realms::Engine &engine) {
    const float volume = clampVolume(engine.cvarFloat("s_volume", 0.8f), 0.8f);
    const float musicVolume = clampVolume(engine.cvarFloat("s_musicvolume", 0.4f), 0.4f);
    if (volume != lastVolume_ || musicVolume != lastMusic_) {
        mixer_.setMasterVolume(volume);
        mixer_.setBusVolume(Bus::Music, musicVolume);
        lastVolume_ = volume;
        lastMusic_ = musicVolume;
    }
}

void SoundSystem::registerCommands(realms::Engine &engine) {
    realms::Engine *e = &engine;
    engine.addCommand("play", [this, e](const realms::CommandArgs &args) {
        if (args.size() < 2) {
            e->print("USAGE: play <sound> [volume 0-1] [pitch]   (soundlist shows the names)");
            return;
        }
        if (!ready()) {
            e->print("play: sounds are still loading");
            return;
        }
        std::string name = args[1];
        // Accept "sounds/jump.ogg" and "jump.ogg" too.
        if (name.rfind("sounds/", 0) == 0) name = name.substr(7);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".ogg") == 0) name.resize(name.size() - 4);
        const float volume = args.size() > 2 ? float(std::atof(args[2].c_str())) : 1.0f;
        const float pitch = args.size() > 3 ? float(std::atof(args[3].c_str())) : 1.0f;
        if (!play(name, volume, pitch > 0.0f ? pitch : 1.0f, 0.0f, false, 0.0f))
            e->print("play: unknown sound " + args[1] + " (soundlist shows the names)", REALMS_PRINT_WARNING);
    });
    engine.addCommand("stopsound", [this](const realms::CommandArgs &) {
        mixer_.stopAll(0.1f);
        director_.stopLoops(*this);
    });
    engine.addCommand("soundlist", [this, e](const realms::CommandArgs &) {
        for (const auto &s : kSounds) {
            std::string line = std::string("  ") + s.name;
            if (s.variants > 0) line += " (x" + std::to_string(s.variants) + ")";
            if (s.stream) line += " [stream]";
            e->print(line);
        }
        e->print(summary() + ", " + std::to_string(mixer_.activeVoices()) + " playing");
    });
    engine.addCommand("music", [this, e](const realms::CommandArgs &) {
        director_.startMusicNow();
        e->print("music: starting (s_musicvolume " + e->cvarString("s_musicvolume", "0.4") + ")");
    });
}

void SoundSystem::unregisterCommands(realms::Engine &engine) {
    for (const char *name : {"play", "stopsound", "soundlist", "music"}) engine.removeCommand(name);
}

} // namespace ekron::audio
