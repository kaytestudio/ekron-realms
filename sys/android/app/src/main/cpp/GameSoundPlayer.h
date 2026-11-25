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
    // Stops every game voice and drops the queue (map closed, paused). The
    // music goes on, as in Quake 2 with the menu open.
    void stop(Mixer &mixer);

    // Quake 2's CD music (CS_CDTRACK, worldspawn "sounds") from Ogg Vorbis
    // files, named as Yamagi Quake II reads them: music/trackNN.ogg (the 2023
    // re-release), music/TrackNN.ogg (GOG) or music/NN.ogg, in a search root
    // or its baseq2 folder. The track loops on the music bus (s_musicvolume)
    // until it changes; 0, 1 (the CD's data track) or no file is silence.
    // Call every frame with the game's track (0 without a game).
    void music(realms::Engine &engine, Mixer &mixer, int track);
    void stopMusic(Mixer &mixer);
    bool musicPlaying(const Mixer &mixer) const { return musicVoice_ && mixer.playing(musicVoice_); }
    int musicTrack() const { return musicTrack_; }
    // The file names tried for a track, in order (none for 0, 1 or > 99).
    static std::vector<std::string> musicFiles(int track);
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
    int musicTrack_ = 0;
    int musicClip_ = -1;
    VoiceId musicVoice_ = 0;
};

} // namespace ekron::audio
