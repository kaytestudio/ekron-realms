// Quake 2 game sounds without a mixer (snd_dma.c / snd_mem.c): .wav
// decoding, S_SpatializeOrigin, timeofs delays, channel override and merged
// loop sounds. Played by GameSoundPlayer (sys/android/app/src/main/cpp).
#pragma once

#include "realms/Engine.h"
#include "realms/GameServer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace realms {

// Interleaved 16-bit PCM from a RIFF WAVE (8-bit unsigned or 16-bit
// signed PCM, mono or stereo), as Quake 2's GetWavinfo / ResampleSfx read
// them. `loopStart` is the first frame of the loop (a "cue " chunk), -1
// when the sound doesn't loop by itself.
struct GameWav {
    int rate = 0;
    int channels = 0;
    std::vector<int16_t> samples;
    int loopStart = -1;
    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};
bool decodeGameWav(const uint8_t *data, size_t size, GameWav &out, std::string *error = nullptr);

// The listener: the view origin and right vector (map space), and the
// local player's entity, whose own sounds are never spatialized.
struct GameListener {
    std::array<float, 3> origin{};
    std::array<float, 3> right{0, -1, 0};
    int entity = 1;
};

// volume 0..1 and pan -1 (left) .. +1 (right).
struct GameSpatial {
    float volume = 0.0f;
    float pan = 0.0f;
};

// S_SpatializeOrigin: full volume up to 80 units, then fading by
// attenuation (ATTN_NORM 1: silent past ~2000 units; ATTN_IDLE 2,
// ATTN_STATIC 3: much closer; ATTN_NONE 0: everywhere, centred).
GameSpatial gameSpatialize(const std::array<float, 3> &origin, int entity, float volume, float attenuation,
                           const GameListener &listener);

// S_RegisterSexedSound's fallback: a player model without its own sexed
// sound ("players/female/pain25_1.wav" missing) uses the male one
// ("players/male/pain25_1.wav"). "" when `name` isn't another model's
// sexed sound.
std::string sexedSoundFallback(const std::string &name);

// The right vector of view angles (degrees), as AngleVectors.
std::array<float, 3> gameRightVector(const std::array<float, 3> &angles);

struct GameSoundPlay {
    std::string name;   // VFS path ("sound/weapons/blastf1a.wav")
    int entity = 0;
    int channel = 0;    // CHAN_*; a sound on the same entity + channel (not CHAN_AUTO) replaces it
    std::array<float, 3> origin{};
    float volume = 1.0f; // 0..1 before spatialization
    float attenuation = 1.0f;
    GameSpatial spatial;
};

class GameSoundScheduler {
public:
    static constexpr size_t kMaxPending = 128;
    // Queues the Sound events (others are ignored), `delay` seconds after nowMs.
    void push(const GameEvent &ev, double nowMs);
    void push(const std::vector<GameEvent> &events, double nowMs);
    // The sounds due by nowMs, oldest first, spatialized for the listener.
    void due(double nowMs, const GameListener &listener, std::vector<GameSoundPlay> &out);
    size_t pending() const { return pending_.size(); }
    void clear() { pending_.clear(); }

    // S_AddLoopSounds: every entity with s.sound, the same sound summed into
    // one voice (volume 1, ATTN_STATIC), so ten torches are one loop.
    static void mergeLoops(const std::vector<GameLoopSound> &loops, const GameListener &listener,
                           std::vector<GameSoundPlay> &out);

private:
    struct Pending {
        double at = 0.0;
        GameSoundPlay play;
    };
    std::vector<Pending> pending_;
};

} // namespace realms
