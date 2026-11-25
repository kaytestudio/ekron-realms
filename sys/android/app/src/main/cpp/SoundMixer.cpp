// Software mixer (see SoundMixer.h).

#include "SoundMixer.h"

#include <algorithm>
#include <cmath>

namespace ekron::audio {

float clampVolume(float v, float fallback) {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, 0.0f, 1.0f);
}

struct Mixer::Clip {
    std::string name;
    std::shared_ptr<const Pcm> pcm;                      // decoded clip, or
    std::shared_ptr<const std::vector<uint8_t>> ogg;     // streamed .ogg
    int rate = 0, channels = 0;
    int64_t frames = 0;
};

struct Mixer::Voice {
    VoiceId id = 0;
    std::shared_ptr<const Clip> clip;
    Bus bus = Bus::Effects;
    bool loop = false;
    // Target and current gains; current ones glide to the target per block.
    float volume = 1.0f, pitch = 1.0f, pan = 0.0f;
    float gainL = 0.0f, gainR = 0.0f, curPitch = 1.0f;
    float fade = 1.0f, fadeStep = 0.0f; // fade envelope (in / out)
    bool stopping = false, ended = false;
    // Resampler: current frame a, next frame b, fraction between them.
    float a[2] = {0, 0}, b[2] = {0, 0};
    double frac = 0.0;
    int64_t cursor = 0; // next PCM frame to read
    bool primed = false;
    // Streaming.
    std::unique_ptr<OggStream> stream;
    std::vector<float> block;
    int blockFrames = 0, blockPos = 0;
    uint64_t started = 0;
};

Mixer::Mixer() = default;
Mixer::~Mixer() = default;

int Mixer::addClip(const std::string &name, Pcm pcm) {
    if (pcm.channels < 1 || pcm.channels > 2 || pcm.rate <= 0 || pcm.samples.empty()) return -1;
    auto clip = std::make_shared<Clip>();
    clip->name = name;
    clip->rate = pcm.rate;
    clip->channels = pcm.channels;
    clip->frames = int64_t(pcm.frames());
    clip->pcm = std::make_shared<const Pcm>(std::move(pcm));
    std::lock_guard<std::mutex> lock(mutex_);
    clips_.push_back(std::move(clip));
    return int(clips_.size()) - 1;
}

int Mixer::addStream(const std::string &name, std::vector<uint8_t> ogg) {
    int rate = 0, channels = 0;
    int64_t frames = 0;
    if (!probeOgg(ogg.data(), ogg.size(), rate, channels, frames)) return -1;
    auto clip = std::make_shared<Clip>();
    clip->name = name;
    clip->rate = rate;
    clip->channels = channels;
    clip->frames = frames;
    clip->ogg = std::make_shared<const std::vector<uint8_t>>(std::move(ogg));
    std::lock_guard<std::mutex> lock(mutex_);
    clips_.push_back(std::move(clip));
    return int(clips_.size()) - 1;
}

int Mixer::findClip(const std::string &name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < clips_.size(); ++i)
        if (clips_[i]->name == name) return int(i);
    return -1;
}

void Mixer::dropClip(int clip) {
    auto empty = std::make_shared<Clip>();
    empty->pcm = std::make_shared<const Pcm>();
    std::lock_guard<std::mutex> lock(mutex_);
    if (clip >= 0 && size_t(clip) < clips_.size()) clips_[size_t(clip)] = std::move(empty);
}

int Mixer::clipCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return int(clips_.size());
}

std::string Mixer::clipName(int clip) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clip >= 0 && size_t(clip) < clips_.size() ? clips_[size_t(clip)]->name : std::string();
}

double Mixer::clipSeconds(int clip) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (clip < 0 || size_t(clip) >= clips_.size()) return 0.0;
    const auto &c = *clips_[size_t(clip)];
    return c.rate > 0 ? double(c.frames) / double(c.rate) : 0.0;
}

Mixer::Voice *Mixer::findVoice(VoiceId id) const {
    if (id == 0) return nullptr;
    for (const auto &v : voices_)
        if (v->id == id) return v.get();
    return nullptr;
}

namespace {
void panGains(float pan, float volume, float &l, float &r) {
    const float p = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
    const float angle = (p + 1.0f) * 0.78539816f; // 0 .. pi/2, equal power
    l = std::cos(angle) * 1.41421356f * volume;
    r = std::sin(angle) * 1.41421356f * volume;
    l = std::min(l, volume);
    r = std::min(r, volume);
}
float safePitch(float pitch) { return std::isfinite(pitch) ? std::clamp(pitch, 0.25f, 4.0f) : 1.0f; }
} // namespace

VoiceId Mixer::play(int clip, const PlayParams &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (clip < 0 || size_t(clip) >= clips_.size()) return 0;
    if (!clips_[size_t(clip)]->ogg && clips_[size_t(clip)]->frames <= 0) return 0; // dropped
    auto voice = std::make_unique<Voice>();
    voice->clip = clips_[size_t(clip)];
    if (voice->clip->ogg) {
        voice->stream = std::make_unique<OggStream>();
        if (!voice->stream->open(voice->clip->ogg)) return 0;
        voice->block.assign(size_t(1024) * size_t(voice->clip->channels), 0.0f);
    }
    voice->id = nextId_++;
    if (nextId_ == 0) nextId_ = 1;
    voice->bus = params.bus;
    voice->loop = params.loop;
    voice->volume = clampVolume(params.volume);
    voice->pitch = voice->curPitch = safePitch(params.pitch);
    voice->pan = std::isfinite(params.pan) ? std::clamp(params.pan, -1.0f, 1.0f) : 0.0f;
    panGains(voice->pan, voice->volume, voice->gainL, voice->gainR);
    if (params.fadeIn > 0.0f) {
        voice->fade = 0.0f;
        voice->fadeStep = 1.0f / (params.fadeIn * float(outputRate_.load()));
    }
    voice->started = voice->id;
    const VoiceId id = voice->id;
    if (voices_.size() >= size_t(kMaxVoices)) {
        // Steal the oldest one-shot; loops (engine, music, ambience) stay.
        auto victim = voices_.end();
        for (auto it = voices_.begin(); it != voices_.end(); ++it)
            if (!(*it)->loop && (victim == voices_.end() || (*it)->started < (*victim)->started)) victim = it;
        if (victim == voices_.end()) return 0;
        voices_.erase(victim);
    }
    voices_.push_back(std::move(voice));
    return id;
}

void Mixer::set(VoiceId id, float volume, float pitch, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice *v = findVoice(id)) {
        v->volume = clampVolume(volume, v->volume);
        v->pitch = safePitch(pitch);
        v->pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : v->pan;
    }
}

void Mixer::stop(VoiceId id, float fadeSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice *v = findVoice(id)) {
        v->stopping = true;
        const float rate = float(outputRate_.load());
        v->fadeStep = -1.0f / std::max(1.0f, (std::isfinite(fadeSeconds) ? std::max(fadeSeconds, 0.0f) : 0.0f) * rate);
    }
}

void Mixer::stopAll(float fadeSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    const float rate = float(outputRate_.load());
    for (auto &v : voices_) {
        v->stopping = true;
        v->fadeStep = -1.0f / std::max(1.0f, std::max(fadeSeconds, 0.0f) * rate);
    }
}

bool Mixer::playing(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice *v = findVoice(id);
    return v && !v->ended && !v->stopping;
}

int Mixer::activeVoices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return int(voices_.size());
}

void Mixer::setBusVolume(Bus bus, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    bus_[size_t(bus)] = clampVolume(volume);
}

float Mixer::busVolume(Bus bus) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bus_[size_t(bus)];
}

void Mixer::setMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    master_ = clampVolume(volume);
}

void Mixer::setPaused(bool paused) { paused_.store(paused); }

void Mixer::setOutputRate(int rate) {
    if (rate >= 8000 && rate <= 384000) outputRate_.store(rate);
}

// Next source frame of a voice (stereo). False at the end of a one-shot.
bool Mixer::fetch(Voice &v, float frame[2]) {
    const Clip &c = *v.clip;
    if (v.stream) {
        if (v.blockPos >= v.blockFrames) {
            v.blockFrames = v.stream->read(v.block.data(), int(v.block.size()) / c.channels);
            v.blockPos = 0;
            if (v.blockFrames <= 0 && v.loop) {
                v.stream->rewind();
                v.blockFrames = v.stream->read(v.block.data(), int(v.block.size()) / c.channels);
            }
            if (v.blockFrames <= 0) return false;
        }
        const float *s = v.block.data() + size_t(v.blockPos) * size_t(c.channels);
        frame[0] = s[0];
        frame[1] = c.channels == 2 ? s[1] : s[0];
        ++v.blockPos;
        return true;
    }
    if (v.cursor >= c.frames) {
        if (!v.loop || c.frames == 0) return false;
        v.cursor = 0;
    }
    const int16_t *s = c.pcm->samples.data() + size_t(v.cursor) * size_t(c.channels);
    constexpr float k = 1.0f / 32768.0f;
    frame[0] = float(s[0]) * k;
    frame[1] = c.channels == 2 ? float(s[1]) * k : frame[0];
    ++v.cursor;
    return true;
}

void Mixer::render(float *out, int frames) {
    if (!out || frames <= 0) return;
    std::fill(out, out + size_t(frames) * 2, 0.0f);
    std::lock_guard<std::mutex> lock(mutex_);
    const float rate = float(outputRate_.load());
    const bool paused = paused_.load();
    // Pausing / resuming glides over ~20 ms instead of clicking.
    const float pauseTarget = paused ? 0.0f : 1.0f;
    if (paused && pauseGain_ <= 0.0f) return; // voices hold their position
    const float pauseStep = 1.0f / (0.02f * rate);

    for (auto &vp : voices_) {
        Voice &v = *vp;
        if (v.ended) continue;
        const Clip &c = *v.clip;
        float targetL = 0, targetR = 0;
        panGains(v.pan, v.volume * bus_[size_t(v.bus)] * master_, targetL, targetR);
        if (!v.primed) {
            v.gainL = targetL;
            v.gainR = targetR;
            if (!fetch(v, v.a)) { v.ended = true; continue; }
            if (!fetch(v, v.b)) v.b[0] = v.b[1] = 0.0f;
            v.primed = true;
        }
        const float startL = v.gainL, startR = v.gainR, startPitch = v.curPitch;
        const float inv = 1.0f / float(frames);
        const double ratio = double(c.rate) / double(rate);
        float pg = pauseGain_;
        for (int i = 0; i < frames; ++i) {
            const float t = float(i + 1) * inv;
            const float gl = startL + (targetL - startL) * t;
            const float gr = startR + (targetR - startR) * t;
            const float pitch = startPitch + (v.pitch - startPitch) * t;
            if (v.fadeStep != 0.0f) {
                v.fade += v.fadeStep;
                if (v.fade >= 1.0f) { v.fade = 1.0f; v.fadeStep = 0.0f; }
                if (v.fade <= 0.0f) { v.fade = 0.0f; v.ended = true; break; }
            }
            pg = std::clamp(pg + (pauseTarget > pg ? pauseStep : -pauseStep), 0.0f, 1.0f);
            const float f = float(v.frac);
            const float sl = v.a[0] + (v.b[0] - v.a[0]) * f;
            const float sr = v.a[1] + (v.b[1] - v.a[1]) * f;
            out[2 * i] += sl * gl * v.fade * pg;
            out[2 * i + 1] += sr * gr * v.fade * pg;
            v.frac += ratio * double(pitch);
            bool more = true;
            while (v.frac >= 1.0) {
                v.frac -= 1.0;
                v.a[0] = v.b[0];
                v.a[1] = v.b[1];
                if (!fetch(v, v.b)) {
                    v.b[0] = v.b[1] = 0.0f;
                    if (v.a[0] == 0.0f && v.a[1] == 0.0f) more = false;
                }
            }
            if (!more) { v.ended = true; break; }
        }
        v.gainL = targetL;
        v.gainR = targetR;
        v.curPitch = v.pitch;
    }
    // Advance the pause envelope once per block (all voices used the same ramp).
    pauseGain_ = std::clamp(pauseGain_ + (pauseTarget > pauseGain_ ? 1.0f : -1.0f) * pauseStep * float(frames),
                            0.0f, 1.0f);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const std::unique_ptr<Voice> &v) { return v->ended; }),
                  voices_.end());
    // Soft limiter: transparent below 0.8, never past 1.
    for (int i = 0; i < frames * 2; ++i) {
        const float x = out[i];
        const float ax = std::fabs(x);
        if (ax > 0.8f) out[i] = std::copysign(0.8f + 0.2f * std::tanh((ax - 0.8f) / 0.2f), x);
    }
}

} // namespace ekron::audio
