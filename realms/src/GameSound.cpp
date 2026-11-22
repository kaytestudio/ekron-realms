// Quake 2 game sounds for a sandbox client (see GameSound.h).
#include "realms/GameSound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace realms {
namespace {

uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }

bool fail(std::string *error, const char *why) {
    if (error) *error = why;
    return false;
}

constexpr float kFullVolume = 80.0f;     // SOUND_FULLVOLUME
constexpr float kLoopAttenuate = 0.003f; // SOUND_LOOPATTENUATE

float spatialDistMult(float attenuation) {
    // S_StartSound: ATTN_STATIC sounds fade faster
    if (!std::isfinite(attenuation) || attenuation <= 0.0f) return 0.0f;
    return attenuation == 3.0f ? attenuation * 0.001f : attenuation * 0.0005f;
}

GameSpatial spatializeMult(const std::array<float, 3> &origin, float volume, float distMult,
                           const GameListener &listener) {
    GameSpatial s;
    volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
    if (distMult <= 0.0f || !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2])) {
        s.volume = volume;
        return s;
    }
    std::array<float, 3> v{origin[0] - listener.origin[0], origin[1] - listener.origin[1],
                           origin[2] - listener.origin[2]};
    float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (dist > 0.0f)
        for (float &c : v) c /= dist;
    dist = std::max(dist - kFullVolume, 0.0f) * distMult;
    const float dot = v[0] * listener.right[0] + v[1] * listener.right[1] + v[2] * listener.right[2];
    s.volume = std::clamp(volume * (1.0f - dist), 0.0f, 1.0f);
    s.pan = std::clamp(dot, -1.0f, 1.0f);
    return s;
}

} // namespace

bool decodeGameWav(const uint8_t *data, size_t size, GameWav &out, std::string *error) {
    out = GameWav{};
    if (!data || size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return fail(error, "not a RIFF WAVE file");
    int format = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t *pcm = nullptr;
    size_t pcmSize = 0;
    int cue = -1;
    size_t at = 12;
    while (at + 8 <= size) {
        const uint8_t *chunk = data + at;
        const uint32_t len = le32(chunk + 4);
        const size_t body = at + 8;
        const size_t avail = size - body;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (len < 16 || avail < 16) return fail(error, "truncated fmt chunk");
            format = le16(data + body);
            channels = le16(data + body + 2);
            rate = int(le32(data + body + 4));
            bits = le16(data + body + 14);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            pcm = data + body;
            pcmSize = std::min<size_t>(len, avail); // tolerate a short last chunk
        } else if (std::memcmp(chunk, "cue ", 4) == 0 && len >= 28 && avail >= 28) {
            cue = int(le32(data + body + 24)); // the first cue point's sample offset
        }
        if (len > avail) break;
        at = body + len + (len & 1);
    }
    if (format != 1) return fail(error, "not PCM (Microsoft PCM format 1 only)");
    if (channels != 1 && channels != 2) return fail(error, "mono or stereo only");
    if (bits != 8 && bits != 16) return fail(error, "8 or 16 bit samples only");
    if (rate < 1000 || rate > 192000) return fail(error, "bad sample rate");
    if (!pcm) return fail(error, "no data chunk");
    const size_t frameBytes = size_t(channels) * size_t(bits / 8);
    const size_t frames = pcmSize / frameBytes;
    if (frames == 0) return fail(error, "empty data chunk");
    out.rate = rate;
    out.channels = channels;
    out.samples.resize(frames * size_t(channels));
    for (size_t i = 0; i < out.samples.size(); i++)
        out.samples[i] = bits == 8 ? int16_t((int(pcm[i]) - 128) << 8) : int16_t(le16(pcm + i * 2));
    out.loopStart = cue >= 0 && size_t(cue) < frames ? cue : -1;
    return true;
}

std::array<float, 3> gameRightVector(const std::array<float, 3> &angles) {
    constexpr float d = 3.14159265358979f / 180.0f;
    const float sy = std::sin(angles[1] * d), cy = std::cos(angles[1] * d);
    const float sp = std::sin(angles[0] * d), cp = std::cos(angles[0] * d);
    const float sr = std::sin(angles[2] * d), cr = std::cos(angles[2] * d);
    return {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
}

GameSpatial gameSpatialize(const std::array<float, 3> &origin, int entity, float volume, float attenuation,
                           const GameListener &listener) {
    // S_Spatialize: the local player's sounds are always full volume, centred
    if (entity != 0 && entity == listener.entity) return spatializeMult(origin, volume, 0.0f, listener);
    return spatializeMult(origin, volume, spatialDistMult(attenuation), listener);
}

void GameSoundScheduler::push(const GameEvent &ev, double nowMs) {
    if (ev.kind != GameEvent::Kind::Sound || ev.text.empty()) return;
    Pending p;
    const float delay = std::isfinite(ev.delay) ? std::clamp(ev.delay, 0.0f, 1.0f) : 0.0f;
    p.at = nowMs + double(delay) * 1000.0;
    p.play.name = ev.text;
    p.play.entity = ev.entity;
    p.play.channel = ev.level & 7;
    p.play.origin = ev.origin;
    p.play.volume = ev.volume;
    p.play.attenuation = ev.attenuation;
    if (pending_.size() >= kMaxPending) pending_.erase(pending_.begin());
    // stable by time: sounds queued later never play before earlier ones due at the same time
    auto it = std::upper_bound(pending_.begin(), pending_.end(), p.at,
                               [](double at, const Pending &q) { return at < q.at; });
    pending_.insert(it, std::move(p));
}

void GameSoundScheduler::push(const std::vector<GameEvent> &events, double nowMs) {
    for (const GameEvent &ev : events) push(ev, nowMs);
}

void GameSoundScheduler::due(double nowMs, const GameListener &listener, std::vector<GameSoundPlay> &out) {
    out.clear();
    size_t n = 0;
    while (n < pending_.size() && pending_[n].at <= nowMs) n++;
    for (size_t i = 0; i < n; i++) {
        GameSoundPlay p = std::move(pending_[i].play);
        p.spatial = gameSpatialize(p.origin, p.entity, p.volume, p.attenuation, listener);
        out.push_back(std::move(p));
    }
    pending_.erase(pending_.begin(), pending_.begin() + std::ptrdiff_t(n));
}

void GameSoundScheduler::mergeLoops(const std::vector<GameLoopSound> &loops, const GameListener &listener,
                                    std::vector<GameSoundPlay> &out) {
    out.clear();
    std::vector<float> left, right; // summed per name, like the left / right volumes
    for (const GameLoopSound &l : loops) {
        if (l.name.empty()) continue;
        const GameSpatial s = spatializeMult(l.origin, 1.0f, kLoopAttenuate, listener);
        if (s.volume <= 0.0f) continue;
        size_t i = 0;
        while (i < out.size() && out[i].name != l.name) i++;
        if (i == out.size()) {
            GameSoundPlay p;
            p.name = l.name;
            p.entity = l.entity;
            p.origin = l.origin;
            p.attenuation = 3.0f;
            out.push_back(std::move(p));
            left.push_back(0.0f);
            right.push_back(0.0f);
        }
        left[i] += s.volume * 0.5f * (1.0f - s.pan);
        right[i] += s.volume * 0.5f * (1.0f + s.pan);
    }
    for (size_t i = 0; i < out.size(); i++) {
        const float sum = left[i] + right[i];
        out[i].spatial.volume = std::min(sum, 1.0f);
        out[i].spatial.pan = sum > 0.0f ? std::clamp((right[i] - left[i]) / sum, -1.0f, 1.0f) : 0.0f;
    }
}

} // namespace realms
