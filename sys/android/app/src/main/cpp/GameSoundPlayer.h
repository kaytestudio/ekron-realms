// Quake 2 game sounds on the sandbox mixer, shared by the desktop client,
// Android and iOS: the game's sound events (and the client effects' muzzle
// flash / impact sounds) and the entity loop sounds from realms::Quake2Module,
// loaded as .wav from the VFS (id's pak0.pak) on first use and played
// through Mixer with Quake 2's spatialization
// (realms::GameSoundScheduler). Silent when the data isn't installed.
#pragma once

#include "SoundMixer.h"

#include "realms/Engine.h"
#include "realms/GameServer.h"
#include "realms/GameSound.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ekron::audio {

class GameSoundPlayer {
public:
    // One frame: queue `events` (Sound only), play what is due, keep the
    // loops in step with `loops`. dt in seconds.
    void update(realms::Engine &engine, Mixer &mixer, const std::vector<realms::GameEvent> &events,
                const std::vector<realms::GameLoopSound> &loops, const realms::GameListener &listener, double dt);
    // Stops every game voice and drops the queue (map closed, paused).
    void stop(Mixer &mixer);
    // "12 game sounds loaded, 3 missing" for the console.
    std::string summary() const;
    size_t activeLoops() const { return loops_.size(); }

private:
    int clip(realms::Engine &engine, Mixer &mixer, const std::string &name);

    realms::GameSoundScheduler scheduler_;
    double clockMs_ = 0.0;
    std::unordered_map<std::string, int> clips_;                // name -> mixer clip, -1 missing
    std::unordered_map<int, VoiceId> channels_;   // entity * 8 + channel -> voice
    std::unordered_map<std::string, VoiceId> loops_;
    std::vector<realms::GameSoundPlay> due_, merged_;
};

} // namespace ekron::audio
