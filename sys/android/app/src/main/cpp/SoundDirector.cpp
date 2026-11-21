// Game events -> sounds (see SoundDirector.h).

#include "SoundDirector.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ekron::audio {

const char *stepSound(Surface s) {
    switch (s) {
    case Surface::Wood: return "step_wood";
    case Surface::Grass: return "step_grass";
    case Surface::Carpet: return "step_carpet";
    case Surface::Snow: return "step_snow";
    case Surface::Concrete: break;
    }
    return "step_concrete";
}

Surface surfaceFromTexture(const std::string &texture) {
    std::string t(texture);
    for (char &c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
    auto has = [&](const char *word) { return t.find(word) != std::string::npos; };
    if (has("snow") || has("ice")) return Surface::Snow;
    if (has("grass") || has("dirt") || has("mud") || has("moss") || has("leaf") || has("leaves") || has("soil") ||
        has("sand") || has("gravel") || has("ground"))
        return Surface::Grass;
    if (has("wood") || has("plank") || has("crate") || has("board") || has("bark") || has("parquet"))
        return Surface::Wood;
    if (has("carpet") || has("rug") || has("cloth") || has("fabric")) return Surface::Carpet;
    return Surface::Concrete;
}

SoundDirector::SoundDirector(uint32_t seed) : musicTimer_(musicDelay), rng_(seed ? seed : 1u) {}

float SoundDirector::random(float lo, float hi) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return lo + (hi - lo) * float(rng_ & 0xffffffu) / float(0x1000000);
}

float SoundDirector::distanceGain(const Listener &l, float x, float y, float z) {
    const float d = std::sqrt((x - l.x) * (x - l.x) + (y - l.y) * (y - l.y) + (z - l.z) * (z - l.z));
    if (!std::isfinite(d)) return 0.0f;
    const float falloff = 1.0f / (1.0f + 0.15f * std::max(0.0f, d - 1.5f));
    const float edge = std::clamp((45.0f - d) / 10.0f, 0.0f, 1.0f);
    return falloff * edge;
}

float SoundDirector::panFor(const Listener &l, float x, float z) {
    const float dx = x - l.x, dz = z - l.z;
    const float d = std::sqrt(dx * dx + dz * dz);
    if (!(d > 0.5f)) return 0.0f;
    const float rightX = std::cos(l.yaw), rightZ = -std::sin(l.yaw);
    return std::clamp((dx * rightX + dz * rightZ) / d, -1.0f, 1.0f) * 0.7f;
}

void SoundDirector::reset() {
    haveSandbox_ = haveMap_ = false;
    tracks_.clear();
    pending_.clear();
    stepDistance_ = airTime_ = lowestSpeed_ = 0.0f;
    carSampled_ = droneSampled_ = false;
}

void SoundDirector::stopLoops(SoundOut &out) {
    for (SoundHandle *h : {&engine_, &drone_, &ambience_, &music_}) {
        if (*h) out.stop(*h, 0.3f);
        *h = 0;
    }
    musicWasPlaying_ = false;
}

void SoundDirector::runPending(float dt, SoundOut &out) {
    for (auto &p : pending_) p.delay -= dt;
    for (const auto &p : pending_)
        if (p.delay <= 0.0f) out.play(p.name, p.volume, random(0.96f, 1.04f), 0.0f, false, 0.0f);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](const Pending &p) { return p.delay <= 0.0f; }),
                   pending_.end());
}

void SoundDirector::ambience(float volume, SoundOut &out) {
    if (volume <= 0.0f) {
        if (ambience_) out.stop(ambience_, 1.0f);
        ambience_ = 0;
        return;
    }
    if (!ambience_ || !out.playing(ambience_)) ambience_ = out.play("ambience_city", volume, 1.0f, 0.0f, true, 2.0f);
    else out.set(ambience_, volume, 1.0f, 0.0f);
}

void SoundDirector::music(float dt, SoundOut &out) {
    const bool playing = music_ && out.playing(music_);
    if (musicWasPlaying_ && !playing) {
        music_ = 0;
        musicTimer_ = random(musicGapMin, musicGapMax);
    }
    musicWasPlaying_ = playing;
    if (playing) return;
    musicTimer_ -= dt;
    if (musicTimer_ <= 0.0f) {
        music_ = out.play("music_infinite_peace", 1.0f, 1.0f, 0.0f, false, 1.5f);
        musicWasPlaying_ = music_ != 0;
        if (!music_) musicTimer_ = 30.0f; // not loaded yet: try again later
    }
}

void SoundDirector::footsteps(bool onFoot, bool grounded, float fx, float fy, float fz, float lastX, float lastZ,
                              bool hadLast, float verticalSpeed, bool lastGrounded, Surface surface, float dt,
                              SoundOut &out) {
    (void)fy;
    if (!onFoot || !hadLast) {
        stepDistance_ = 0.0f;
        airTime_ = 0.0f;
        lowestSpeed_ = 0.0f;
        return;
    }
    if (!grounded) {
        if (lastGrounded && verticalSpeed > 1.5f) out.play("jump", 1.0f, random(0.95f, 1.05f), 0.0f, false, 0.0f);
        airTime_ += dt;
        lowestSpeed_ = std::min(lowestSpeed_, verticalSpeed);
        return;
    }
    if (!lastGrounded && airTime_ > 0.12f && lowestSpeed_ < -3.0f) {
        const float v = std::clamp((-lowestSpeed_ - 3.0f) / 8.0f + 0.35f, 0.35f, 1.0f);
        out.play("land", v, random(0.92f, 1.04f), 0.0f, false, 0.0f);
        out.play(stepSound(surface), 0.8f, random(0.9f, 1.0f), 0.0f, false, 0.0f);
        stepDistance_ = 0.0f;
    }
    airTime_ = 0.0f;
    lowestSpeed_ = 0.0f;
    float moved = std::hypot(fx - lastX, fz - lastZ);
    if (!std::isfinite(moved) || moved > 3.0f) moved = 0.0f; // teleport / respawn
    const float speed = dt > 0.0f ? moved / dt : 0.0f;
    if (speed < 0.4f) {
        stepDistance_ = std::min(stepDistance_, 0.6f); // first step comes quickly when starting off
        return;
    }
    stepDistance_ += moved;
    const float stride = std::clamp(0.9f + 0.17f * speed, 0.9f, 2.0f);
    if (stepDistance_ >= stride) {
        stepDistance_ = std::fmod(stepDistance_, stride);
        const float v = 0.65f + 0.35f * std::min(speed / 5.0f, 1.0f);
        out.play(stepSound(surface), v, random(0.92f, 1.08f), random(-0.08f, 0.08f), false, 0.0f);
    }
}

void SoundDirector::sandbox(const SandboxFrame &f, float dt, SoundOut &out) {
    dt = std::clamp(std::isfinite(dt) ? dt : 0.0f, 0.0f, 0.25f);
    runPending(dt, out);
    music(dt, out);
    ambience(f.active ? 1.0f : 0.6f, out);
    haveMap_ = false;

    if (!f.active) {
        // Paused: the engine and the propellers stop, nothing new starts.
        if (engine_) out.stop(engine_, 0.25f);
        if (drone_) out.stop(drone_, 0.25f);
        engine_ = drone_ = 0;
        carSampled_ = droneSampled_ = false;
        last_ = f;
        haveSandbox_ = false;
        return;
    }

    const bool had = haveSandbox_;
    footsteps(f.onFoot, f.grounded, f.feetX, f.feetY, f.feetZ, last_.feetX, last_.feetZ, had && last_.onFoot,
              f.verticalSpeed, last_.grounded, f.surface, dt, out);

    if (had) {
        if (f.tool != last_.tool) out.play("tool_switch", 1.0f, 1.0f, 0.0f, false, 0.0f);

        const bool sameBodies = f.bodies.size() == last_.bodies.size();
        if (f.bodies.size() > last_.bodies.size() && f.bodies.size() - last_.bodies.size() <= 3) {
            const auto &b = f.bodies.back();
            out.play("spawn", std::max(0.4f, distanceGain(f.listener, b.x, b.y, b.z)), random(0.95f, 1.05f),
                     panFor(f.listener, b.x, b.z), false, 0.0f);
        } else if (f.bodies.size() < last_.bodies.size() && last_.bodies.size() - f.bodies.size() <= 3) {
            out.play("delete", 1.0f, random(0.95f, 1.05f), 0.0f, false, 0.0f);
        }

        // Gravity gun: grab, throw; freeze / unfreeze with or without holding.
        if (f.held >= 0 && last_.held < 0) out.play("grab", 1.0f, random(0.97f, 1.03f), 0.0f, false, 0.0f);
        if (sameBodies) {
            int freezes = 0, thaws = 0;
            for (size_t i = 0; i < f.bodies.size(); ++i) {
                if (f.bodies[i].frozen && !last_.bodies[i].frozen) ++freezes;
                if (!f.bodies[i].frozen && last_.bodies[i].frozen) ++thaws;
            }
            if (freezes) out.play("freeze", 1.0f, 1.25f, 0.0f, false, 0.0f);
            if (thaws) out.play("freeze", 0.8f, 0.8f, 0.0f, false, 0.0f);
            if (last_.held >= 0 && f.held < 0 && size_t(last_.held) < f.bodies.size() && !freezes && !thaws)
                out.play("throw", 1.0f, random(0.95f, 1.05f), 0.0f, false, 0.0f);
        } else if (last_.held >= 0 && f.held < 0) {
            out.play("throw", 1.0f, random(0.95f, 1.05f), 0.0f, false, 0.0f);
        }

        // Car doors.
        if (f.seated >= 0 && last_.seated < 0 && f.seatedCar) {
            out.play("car_door_open", 1.0f, random(0.97f, 1.03f), 0.0f, false, 0.0f);
            pending_.push_back({"car_door_close", 0.55f, 1.0f});
        } else if (f.seated < 0 && last_.seated >= 0 && last_.seatedCar) {
            out.play("car_door_open", 1.0f, random(0.97f, 1.03f), 0.0f, false, 0.0f);
            pending_.push_back({"car_door_close", 0.45f, 1.0f});
        }
    }

    // Doors and impacts, tracked per body while the list keeps its order.
    if (tracks_.size() != f.bodies.size() || !had) {
        tracks_.assign(f.bodies.size(), Track{});
        for (size_t i = 0; i < f.bodies.size(); ++i) {
            tracks_[i].x = f.bodies[i].x;
            tracks_[i].z = f.bodies[i].z;
            tracks_[i].vy = f.bodies[i].vy;
            tracks_[i].frozen = f.bodies[i].frozen;
            tracks_[i].doorOpen = f.bodies[i].doorOpen;
        }
    } else {
        int impacts = 0;
        for (size_t i = 0; i < f.bodies.size(); ++i) {
            const auto &b = f.bodies[i];
            Track &t = tracks_[i];
            t.cooldown = std::max(0.0f, t.cooldown - dt);
            if (b.door && b.doorOpen != t.doorOpen) {
                out.play(b.doorOpen ? "door_open" : "door_close", distanceGain(f.listener, b.x, b.y, b.z),
                         random(0.96f, 1.04f), panFor(f.listener, b.x, b.z), false, 0.0f);
            }
            const bool skip = int(i) == f.held || int(i) == f.seated || b.frozen;
            if (!skip && t.vy < -2.0f && b.vy > t.vy * 0.5f && t.cooldown <= 0.0f && impacts < 3) {
                const float gain = distanceGain(f.listener, b.x, b.y, b.z);
                if (gain > 0.02f) {
                    const float v = std::clamp((-t.vy - 2.0f) / 6.0f + 0.3f, 0.3f, 1.0f) * gain;
                    const char *name = b.kind == 2 ? "impact_metal" : b.kind == 1 ? "impact_wood" : "impact_generic";
                    out.play(name, v, random(0.9f, 1.1f), panFor(f.listener, b.x, b.z), false, 0.0f);
                    ++impacts;
                }
                t.cooldown = 0.2f;
            }
            t.x = b.x;
            t.z = b.z;
            t.vy = b.vy;
            t.frozen = b.frozen;
            t.doorOpen = b.doorOpen;
        }
    }

    // Car engine: pitch and level follow the speed of the car you sit in.
    if (f.seated >= 0 && f.seatedCar && size_t(f.seated) < f.bodies.size()) {
        const auto &car = f.bodies[size_t(f.seated)];
        if (carSampled_ && dt > 0.0f) {
            float speed = std::hypot(car.x - carX_, car.z - carZ_) / dt;
            if (!std::isfinite(speed) || speed > 80.0f) speed = carSpeed_;
            carSpeed_ += (speed - carSpeed_) * (1.0f - std::exp(-4.0f * dt));
        }
        carX_ = car.x;
        carZ_ = car.z;
        carSampled_ = true;
        const float s = std::clamp(carSpeed_, 0.0f, 25.0f);
        const float pitch = 0.75f + s / 25.0f * 1.1f;
        const float volume = 0.55f + 0.45f * std::min(s / 15.0f, 1.0f);
        if (!engine_ || !out.playing(engine_)) engine_ = out.play("engine", volume, pitch, 0.0f, true, 0.4f);
        else out.set(engine_, volume, pitch, 0.0f);
    } else {
        if (engine_) out.stop(engine_, 0.5f);
        engine_ = 0;
        carSampled_ = false;
        carSpeed_ = 0.0f;
    }

    // Drone propellers: whine rises with throttle and speed.
    if (f.droneRunning) {
        if (droneSampled_ && dt > 0.0f) {
            float speed = std::sqrt((f.droneX - droneLastX_) * (f.droneX - droneLastX_) +
                                    (f.droneY - droneLastY_) * (f.droneY - droneLastY_) +
                                    (f.droneZ - droneLastZ_) * (f.droneZ - droneLastZ_)) / dt;
            if (!std::isfinite(speed) || speed > 60.0f) speed = droneSpeed_;
            droneSpeed_ += (speed - droneSpeed_) * (1.0f - std::exp(-5.0f * dt));
        }
        droneLastX_ = f.droneX;
        droneLastY_ = f.droneY;
        droneLastZ_ = f.droneZ;
        droneSampled_ = true;
        const float target = 0.85f + 0.25f * std::min(std::fabs(f.droneThrottle), 1.0f) +
                             std::min(droneSpeed_, 15.0f) / 15.0f * 0.45f;
        dronePitch_ += (target - dronePitch_) * (1.0f - std::exp(-6.0f * dt));
        const float gain = distanceGain(f.listener, f.droneX, f.droneY, f.droneZ);
        const float pan = panFor(f.listener, f.droneX, f.droneZ);
        if (!drone_ || !out.playing(drone_)) drone_ = out.play("drone", gain, dronePitch_, pan, true, 0.3f);
        else out.set(drone_, gain, dronePitch_, pan);
    } else {
        if (drone_) out.stop(drone_, 0.6f);
        drone_ = 0;
        droneSampled_ = false;
        droneSpeed_ = 0.0f;
        dronePitch_ = 0.85f;
    }

    last_ = f;
    haveSandbox_ = true;
}

void SoundDirector::map(const MapFrame &f, float dt, SoundOut &out) {
    dt = std::clamp(std::isfinite(dt) ? dt : 0.0f, 0.0f, 0.25f);
    runPending(dt, out);
    music(dt, out);
    ambience(f.active ? 0.3f : 0.2f, out);
    if (engine_) out.stop(engine_, 0.3f);
    if (drone_) out.stop(drone_, 0.3f);
    engine_ = drone_ = 0;
    haveSandbox_ = false;
    if (!f.active) {
        haveMap_ = false;
        return;
    }
    footsteps(f.onFoot, f.grounded, f.feetX, f.feetY, f.feetZ, lastMap_.feetX, lastMap_.feetZ,
              haveMap_ && lastMap_.onFoot, f.verticalSpeed, lastMap_.grounded, f.surface, dt, out);
    lastMap_ = f;
    haveMap_ = true;
}

} // namespace ekron::audio
