// Quake 2 client effects for the sandbox (see GameEffects.h). Follows
// cl_fx.c / cl_tent.c / cl_ents.c of the GPL Quake 2 source; the tables
// (byte directions, monster muzzle flashes) are generated from the copies
// in this repository by tools/gen_game_effect_tables.py.
#include "realms/GameEffects.h"
#include "realms/MapTextures.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace realms {
namespace {

#include "GameEffectTables.inc"

using A3 = std::array<float, 3>;

constexpr float kPi = 3.14159265358979f;
constexpr float kGravity = 40.0f; // PARTICLE_GRAVITY
constexpr float kAttnNorm = 1.0f, kAttnStatic = 3.0f, kAttnNone = 0.0f;
constexpr int kChanAuto = 0, kChanWeapon = 1;

A3 add(const A3 &a, const A3 &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
A3 sub(const A3 &a, const A3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
A3 scale(const A3 &a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
A3 ma(const A3 &a, float s, const A3 &b) { return {a[0] + s * b[0], a[1] + s * b[1], a[2] + s * b[2]}; }
float dot(const A3 &a, const A3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float length(const A3 &a) { return std::sqrt(dot(a, a)); }
A3 cross(const A3 &a, const A3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float normalize(A3 &v) {
    const float l = length(v);
    if (l > 0.0f) v = scale(v, 1.0f / l);
    return l;
}
bool finite3(const A3 &v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// AngleVectors (q_shared.c)
void angleVectors(const A3 &angles, A3 *forward, A3 *right, A3 *up) {
    const float y = angles[1] * (kPi / 180), p = angles[0] * (kPi / 180), r = angles[2] * (kPi / 180);
    const float sy = std::sin(y), cy = std::cos(y), sp = std::sin(p), cp = std::cos(p), sr = std::sin(r),
                cr = std::cos(r);
    if (forward) *forward = {cp * cy, cp * sy, -sp};
    if (right) *right = {-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    if (up) *up = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
}

// MakeNormalVectors (cl_fx.c)
void makeNormalVectors(const A3 &forward, A3 &right, A3 &up) {
    right = {forward[2], -forward[0], forward[1]};
    const float d = dot(right, forward);
    right = ma(right, -d, forward);
    normalize(right);
    up = cross(right, forward);
}

// pitch / yaw of an impact direction, like CL_ParseTEnt's TE_BLASTER
A3 dirAngles(const A3 &dir) {
    A3 a{};
    a[0] = std::acos(std::clamp(dir[2], -1.0f, 1.0f)) / kPi * 180;
    if (dir[0] != 0.0f)
        a[1] = std::atan2(dir[1], dir[0]) / kPi * 180;
    else if (dir[1] > 0)
        a[1] = 90;
    else if (dir[1] < 0)
        a[1] = 270;
    return a;
}

std::array<std::array<float, 3>, 256> toFloat(const uint8_t *rgb768) {
    std::array<std::array<float, 3>, 256> p{};
    for (size_t i = 0; i < 256; i++) p[i] = {rgb768[i * 3] / 255.0f, rgb768[i * 3 + 1] / 255.0f, rgb768[i * 3 + 2] / 255.0f};
    return p;
}

constexpr int kBfgLightRamp[6] = {300, 400, 600, 300, 150, 75};
constexpr int kSplashColor[7] = {0x00, 0xe0, 0xb0, 0x50, 0xd0, 0xe0, 0xe8};

bool endsWith(const std::string &s, const char *ext) {
    const size_t n = std::char_traits<char>::length(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != ext[i]) return false;
    return true;
}

} // namespace

// ------------------------------------------------------------ helpers

Vec3 gameByteDir(int index) {
    if (index < 0 || index >= 162) return {0, 0, 0};
    return {kByteDirs[index][0], kByteDirs[index][1], kByteDirs[index][2]};
}

int gameDirToByte(const float dir[3]) {
    // MSG_WriteDir
    if (!dir) return 0;
    float best = 0.0f;
    int bestIndex = 0;
    for (int i = 0; i < 162; i++) {
        const float d = dir[0] * kByteDirs[i][0] + dir[1] * kByteDirs[i][1] + dir[2] * kByteDirs[i][2];
        if (d > best) {
            best = d;
            bestIndex = i;
        }
    }
    return bestIndex;
}

double clientTimeMs(const GameServer &server) {
    return (std::max(server.frameNumber() - 1, 0) + server.lerpFraction()) * 100.0;
}

GameEffects::GameEffects() {
    palette_ = toFloat(builtinQuake2Palette().data());
    reset();
}

void GameEffects::reset(uint32_t seed) {
    rng_ = seed ? seed : 0x2545f491u;
    particles_.clear();
    droppedParticles_ = 0;
    explosions_ = {};
    dlights_ = {};
    lasers_ = {};
    cables_ = {};
    frameLights_.clear();
    frameGlows_.clear();
    beamEntities_.clear();
    entities_.clear();
    avelocitiesSet_ = false;
    lastSnapshot_ = -1;
    sounds_.clear();
}

int GameEffects::rand15() {
    // xorshift32, 15 bits like the classic rand()
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return int((rng_ >> 9) & 0x7fff);
}

void GameEffects::setPalette(const uint8_t *rgb768) {
    palette_ = toFloat(rgb768 ? rgb768 : builtinQuake2Palette().data());
    paletteFromGame_ = rgb768 != nullptr;
}

bool GameEffects::loadPalette(const Engine &engine, std::string *error) {
    Quake2Palette palette;
    const bool fromGame = loadQuake2Palette(engine, palette, error);
    setPalette(fromGame ? palette.data() : nullptr);
    return fromGame;
}

std::array<float, 3> GameEffects::paletteColor(int index) const { return palette_[size_t(index & 255)]; }

GameEffects::Particle *GameEffects::alloc(double timeMs) {
    if (int(particles_.size()) >= kMaxParticles) {
        droppedParticles_++;
        return nullptr;
    }
    particles_.emplace_back();
    Particle *p = &particles_.back();
    p->time = timeMs;
    return p;
}

GameEffects::Explosion &GameEffects::allocExplosion() {
    for (Explosion &e : explosions_)
        if (e.type == ExType::Free) {
            e = Explosion{};
            return e;
        }
    // the oldest one
    size_t index = 0;
    for (size_t i = 1; i < explosions_.size(); i++)
        if (explosions_[i].start < explosions_[index].start) index = i;
    explosions_[index] = Explosion{};
    return explosions_[index];
}

GameEffects::Dlight &GameEffects::allocDlight(int key, double timeMs) {
    if (key != 0)
        for (Dlight &d : dlights_)
            if (d.key == key) {
                d = Dlight{};
                d.key = key;
                return d;
            }
    for (Dlight &d : dlights_)
        if (d.radius <= 0.0f || d.die < timeMs) {
            d = Dlight{};
            d.key = key;
            return d;
        }
    dlights_[0] = Dlight{};
    dlights_[0].key = key;
    return dlights_[0];
}

void GameEffects::sound(const std::string &name, int entity, const A3 &origin, float volume, float attenuation,
                        int channel, float delay) {
    GameEvent ev;
    ev.kind = GameEvent::Kind::Sound;
    ev.text = (name.compare(0, 8, "players/") == 0 ? "" : "sound/") + name;
    ev.entity = entity;
    ev.origin = origin;
    ev.volume = volume;
    ev.attenuation = attenuation;
    ev.level = channel;
    ev.delay = delay;
    sounds_.push_back(std::move(ev));
    if (sounds_.size() > 256) sounds_.erase(sounds_.begin());
}

std::vector<GameEvent> GameEffects::takeSounds() {
    std::vector<GameEvent> out;
    out.swap(sounds_);
    return out;
}

size_t GameEffects::explosionCount() const {
    size_t n = 0;
    for (const Explosion &e : explosions_) n += e.type != ExType::Free;
    return n;
}

size_t GameEffects::dlightCount(double timeMs) const {
    size_t n = 0;
    for (const Dlight &d : dlights_) n += d.radius > 0.0f && d.die >= timeMs;
    return n;
}

// ------------------------------------------------------------ events

void GameEffects::handleEvent(const GameEvent &ev, double t) {
    if (!finite3(ev.origin) || !std::isfinite(t)) return;
    switch (ev.kind) {
    case GameEvent::Kind::TempEntity: tempEntity(ev, t); break;
    case GameEvent::Kind::MuzzleFlash:
        if (ev.level >= 1000)
            monsterFlash(ev, t);
        else
            muzzleFlash(ev, t);
        break;
    case GameEvent::Kind::EntityEvent: entityEvent(ev, t); break;
    default: break;
    }
}

void GameEffects::smokeAndFlash(const A3 &org, double t) {
    Explosion &smoke = allocExplosion();
    smoke.ent.origin = org;
    smoke.type = ExType::Misc;
    smoke.frames = 4;
    smoke.translucent = true;
    smoke.start = t;
    smoke.ent.model = "models/objects/smoke/tris.md2";

    Explosion &flash = allocExplosion();
    flash.ent.origin = org;
    flash.type = ExType::Flash;
    flash.ent.fullbright = true;
    flash.frames = 2;
    flash.start = t;
    flash.ent.model = "models/objects/flash/tris.md2";
}

void GameEffects::polyExplosion(const A3 &org, const char *model, int frames, int baseframe, double t) {
    Explosion &ex = allocExplosion();
    ex.ent.origin = org;
    ex.type = ExType::Poly;
    ex.ent.fullbright = true;
    ex.start = t;
    ex.light = 350;
    ex.lightcolor = {1.0f, 0.5f, 0.5f};
    ex.ent.angles[1] = float(rand15() % 360);
    ex.ent.model = model;
    ex.frames = frames;
    ex.baseframe = baseframe;
}

void GameEffects::addLaser(const GameEvent &ev, int colors, double t) {
    for (Laser &l : lasers_) {
        if (l.endtime >= t) continue;
        l.beam.start = ev.origin;
        l.beam.end = ev.end;
        l.beam.width = 4; // frame 4
        l.colors = colors;
        l.endtime = t + 100;
        return;
    }
}

void GameEffects::addCable(const GameEvent &ev, const char *model, double t) {
    // CL_ParseBeam: one beam per entity, replaced by the newest
    Cable *slot = nullptr;
    for (Cable &c : cables_)
        if (c.endtime >= t && c.entity == ev.entity) slot = &c;
    if (!slot)
        for (Cable &c : cables_)
            if (c.endtime < t) {
                slot = &c;
                break;
            }
    if (!slot) return; // beam list overflow
    slot->entity = ev.entity;
    slot->model = model;
    slot->start = ev.origin;
    slot->end = ev.end;
    slot->offset = ev.offset;
    slot->endtime = t + 200;
}

void GameEffects::tempEntity(const GameEvent &ev, double t) {
    const A3 &pos = ev.origin;
    const A3 &dir = ev.dir;
    switch (ev.level) {
    case GTE_BLOOD: particleEffect(pos, dir, 0xe8, 60, t); break;
    case GTE_GUNSHOT:
    case GTE_SPARKS:
    case GTE_BULLET_SPARKS:
        if (ev.level == GTE_GUNSHOT)
            particleEffect(pos, dir, 0, 40, t);
        else
            particleEffect(pos, dir, 0xe0, 6, t);
        if (ev.level != GTE_SPARKS) {
            smokeAndFlash(pos, t);
            const int r = rand15() & 15;
            if (r >= 1 && r <= 3) sound("world/ric" + std::to_string(r) + ".wav", 0, pos, 1, kAttnNorm);
        }
        break;
    case GTE_SCREEN_SPARKS:
    case GTE_SHIELD_SPARKS:
        particleEffect(pos, dir, ev.level == GTE_SCREEN_SPARKS ? 0xd0 : 0xb0, 40, t);
        sound("weapons/lashit.wav", 0, pos, 1, kAttnNorm);
        break;
    case GTE_SHOTGUN:
        particleEffect(pos, dir, 0, 20, t);
        smokeAndFlash(pos, t);
        break;
    case GTE_SPLASH: {
        const int r = ev.color;
        particleEffect(pos, dir, r >= 0 && r <= 6 ? kSplashColor[r] : 0, ev.count, t);
        if (r == GSPLASH_SPARKS) {
            const int k = rand15() & 3;
            sound(k == 0 ? "world/spark5.wav" : k == 1 ? "world/spark6.wav" : "world/spark7.wav", 0, pos, 1,
                  kAttnStatic);
        }
        break;
    }
    case GTE_LASER_SPARKS: particleEffect(pos, dir, ev.color, ev.count, t, 2); break;
    case GTE_BLUEHYPERBLASTER: blasterParticles(pos, dir, 0xe0, t); break; // the "dir" is a position: as Q2
    case GTE_BLASTER:
    case GTE_BLASTER2:
    case GTE_FLECHETTE: {
        blasterParticles(pos, dir, ev.level == GTE_BLASTER ? 0xe0 : ev.level == GTE_BLASTER2 ? 0xd0 : 0x6f, t);
        Explosion &ex = allocExplosion();
        ex.ent.origin = pos;
        ex.ent.angles = dirAngles(dir);
        ex.type = ExType::Misc;
        ex.ent.fullbright = true;
        ex.translucent = true;
        ex.ent.skinnum = ev.level == GTE_BLASTER ? 0 : ev.level == GTE_BLASTER2 ? 1 : 2;
        ex.start = t;
        ex.light = 150;
        ex.lightcolor = ev.level == GTE_BLASTER ? A3{1, 1, 0} : ev.level == GTE_BLASTER2 ? A3{0, 1, 0}
                                                                                       : A3{0.19f, 0.41f, 0.75f};
        ex.ent.model = "models/objects/explode/tris.md2";
        ex.frames = 4;
        sound("weapons/lashit.wav", 0, pos, 1, kAttnNorm);
        break;
    }
    case GTE_RAILTRAIL:
        railTrail(pos, ev.end, t);
        sound("weapons/railgf1a.wav", 0, ev.end, 1, kAttnNorm);
        break;
    case GTE_EXPLOSION2:
    case GTE_GRENADE_EXPLOSION:
    case GTE_GRENADE_EXPLOSION_WATER:
        polyExplosion(pos, "models/objects/r_explode/tris.md2", 19, 30, t);
        explosionParticles(pos, 0xe0, t);
        sound(ev.level == GTE_GRENADE_EXPLOSION_WATER ? "weapons/xpld_wat.wav" : "weapons/grenlx1a.wav", 0, pos, 1,
              kAttnNorm);
        break;
    case GTE_PLASMA_EXPLOSION:
    case GTE_EXPLOSION1:
    case GTE_EXPLOSION1_BIG:
    case GTE_ROCKET_EXPLOSION:
    case GTE_ROCKET_EXPLOSION_WATER:
    case GTE_EXPLOSION1_NP:
    case GTE_PLAIN_EXPLOSION:
        polyExplosion(pos, ev.level == GTE_EXPLOSION1_BIG ? "models/objects/r_explode2/tris.md2"
                                                          : "models/objects/r_explode/tris.md2",
                      15, frand() < 0.5f ? 15 : 0, t);
        if (ev.level != GTE_EXPLOSION1_BIG && ev.level != GTE_EXPLOSION1_NP && ev.level != GTE_PLAIN_EXPLOSION)
            explosionParticles(pos, 0xe0, t);
        sound(ev.level == GTE_ROCKET_EXPLOSION_WATER ? "weapons/xpld_wat.wav" : "weapons/rocklx1a.wav", 0, pos, 1,
              kAttnNorm);
        break;
    case GTE_BFG_EXPLOSION: {
        Explosion &ex = allocExplosion();
        ex.ent.origin = pos;
        ex.type = ExType::Poly;
        ex.ent.fullbright = true;
        ex.translucent = true;
        ex.ent.alpha = 0.3f;
        ex.start = t;
        ex.light = 350;
        ex.lightcolor = {0, 1, 0};
        ex.ent.model = "sprites/s_bfg2.sp2"; // a sprite: drawn as a glow
        ex.frames = 4;
        break;
    }
    case GTE_BFG_BIGEXPLOSION: explosionParticles(pos, 0xd0, t); break;
    case GTE_BFG_LASER: addLaser(ev, int(0xd0d1d2d3u), t); break;
    case GTE_BUBBLETRAIL: bubbleTrail(pos, ev.end, t); break;
    case GTE_PARASITE_ATTACK:
    case GTE_MEDIC_CABLE_ATTACK: addCable(ev, "models/monsters/parasite/segment/tris.md2", t); break;
    case GTE_GRAPPLE_CABLE: addCable(ev, "models/ctf/segment/tris.md2", t); break;
    case GTE_BOSSTPORT:
        bigTeleportParticles(pos, t);
        sound("misc/bigtele.wav", 0, pos, 1, kAttnNone);
        break;
    case GTE_WELDING_SPARKS: {
        particleEffect(pos, dir, ev.color, ev.count, t, 2);
        Explosion &ex = allocExplosion(); // just the light
        ex.ent.origin = pos;
        ex.type = ExType::Flash;
        ex.start = t;
        ex.light = float(100 + rand15() % 75);
        ex.lightcolor = {1.0f, 1.0f, 0.3f};
        ex.frames = 2;
        break;
    }
    case GTE_GREENBLOOD: particleEffect(pos, dir, 0xdf, 30, t, 2); break;
    case GTE_TUNNEL_SPARKS: particleEffect(pos, dir, ev.color, ev.count, t, 3); break;
    case GTE_MOREBLOOD: particleEffect(pos, dir, 0xe8, 250, t); break;
    case GTE_ELECTRIC_SPARKS:
        particleEffect(pos, dir, 0x75, 40, t);
        sound("weapons/lashit.wav", 0, pos, 1, kAttnNorm);
        break;
    case GTE_TELEPORT_EFFECT:
    case GTE_DBALL_GOAL: teleportParticles(pos, t); break;
    default: break; // Rogue sustains, heat beams, tracker, force walls: not drawn
    }
}

void GameEffects::muzzleFlash(const GameEvent &ev, double t) {
    // CL_ParseMuzzleFlash
    const bool silenced = (ev.level & GMZ_SILENCED) != 0;
    const int weapon = ev.level & ~GMZ_SILENCED;
    const int ent = ev.entity;
    A3 fv, rv;
    angleVectors(ev.angles, &fv, &rv, nullptr);
    Dlight &dl = allocDlight(ent, t);
    dl.origin = ma(ma(ev.origin, 18, fv), 16, rv);
    dl.radius = float(silenced ? 100 + (rand15() & 31) : 200 + (rand15() & 31));
    dl.die = t + kMuzzleFlashMs;
    const float volume = silenced ? 0.2f : 1.0f;
    auto snd = [&](const std::string &name, float vol = -1.0f, int channel = kChanWeapon, float delay = 0.0f) {
        sound(name, ent, ev.origin, vol < 0 ? volume : vol, kAttnNorm, channel, delay);
    };
    auto machgun = [&]() { return "weapons/machgf" + std::to_string(rand15() % 5 + 1) + "b.wav"; };
    switch (weapon) {
    case GMZ_BLASTER: dl.color = {1, 1, 0}; snd("weapons/blastf1a.wav"); break;
    case GMZ_BLUEHYPERBLASTER: dl.color = {0, 0, 1}; snd("weapons/hyprbf1a.wav"); break;
    case GMZ_HYPERBLASTER: dl.color = {1, 1, 0}; snd("weapons/hyprbf1a.wav"); break;
    case GMZ_MACHINEGUN: dl.color = {1, 1, 0}; snd(machgun()); break;
    case GMZ_SHOTGUN:
        dl.color = {1, 1, 0};
        snd("weapons/shotgf1b.wav");
        snd("weapons/shotgr1b.wav", -1, kChanAuto, 0.1f);
        break;
    case GMZ_SSHOTGUN: dl.color = {1, 1, 0}; snd("weapons/sshotf1b.wav"); break;
    case GMZ_CHAINGUN1:
        dl.radius = float(200 + (rand15() & 31));
        dl.color = {1, 0.25f, 0};
        snd(machgun());
        break;
    case GMZ_CHAINGUN2:
        dl.radius = float(225 + (rand15() & 31));
        dl.color = {1, 0.5f, 0};
        snd(machgun());
        snd(machgun(), -1, kChanWeapon, 0.05f);
        break;
    case GMZ_CHAINGUN3:
        dl.radius = float(250 + (rand15() & 31));
        dl.color = {1, 1, 0};
        snd(machgun());
        snd(machgun(), -1, kChanWeapon, 0.033f);
        snd(machgun(), -1, kChanWeapon, 0.066f);
        break;
    case GMZ_RAILGUN: dl.color = {0.5f, 0.5f, 1.0f}; snd("weapons/railgf1a.wav"); break;
    case GMZ_ROCKET:
        dl.color = {1, 0.5f, 0.2f};
        snd("weapons/rocklf1a.wav");
        snd("weapons/rocklr1b.wav", -1, kChanAuto, 0.1f);
        break;
    case GMZ_GRENADE:
        dl.color = {1, 0.5f, 0};
        snd("weapons/grenlf1a.wav");
        snd("weapons/grenlr1b.wav", -1, kChanAuto, 0.1f);
        break;
    case GMZ_BFG: dl.color = {0, 1, 0}; snd("weapons/bfg__f1y.wav"); break;
    case GMZ_LOGIN:
    case GMZ_LOGOUT:
    case GMZ_RESPAWN:
        dl.color = weapon == GMZ_LOGIN ? A3{0, 1, 0} : weapon == GMZ_LOGOUT ? A3{1, 0, 0} : A3{1, 1, 0};
        dl.die = t + 1000;
        snd("weapons/grenlf1a.wav", 1.0f);
        logoutEffect(ev.origin, weapon, t);
        break;
    case GMZ_PHALANX: dl.color = {1, 0.5f, 0.5f}; snd("weapons/plasshot.wav"); break;
    case GMZ_IONRIPPER: dl.color = {1, 0.5f, 0.5f}; snd("weapons/rippfire.wav"); break;
    case GMZ_ETF_RIFLE: dl.color = {0.9f, 0.7f, 0}; snd("weapons/nail1.wav"); break;
    case GMZ_SHOTGUN2: dl.color = {1, 1, 0}; snd("weapons/shotg2.wav"); break;
    case GMZ_HEATBEAM: dl.color = {1, 1, 0}; dl.die = t + 100; break;
    case GMZ_BLASTER2: dl.color = {0, 1, 0}; snd("weapons/blastf1a.wav"); break;
    case GMZ_TRACKER: dl.radius = 0; snd("weapons/disint2.wav"); break; // a negative light: not drawn
    case GMZ_NUKE1: dl.color = {1, 0, 0}; dl.die = t + 100; break;
    case GMZ_NUKE2: dl.color = {1, 1, 0}; dl.die = t + 100; break;
    case GMZ_NUKE4: dl.color = {0, 0, 1}; dl.die = t + 100; break;
    case GMZ_NUKE8: dl.color = {0, 1, 1}; dl.die = t + 100; break;
    default: dl.radius = 0; break; // black: no light
    }
}

void GameEffects::monsterFlash(const GameEvent &ev, double t) {
    // CL_ParseMuzzleFlash2
    const int n = ev.level - 1000;
    if (n < 0 || n >= kMonsterFlashCount) return;
    const MonsterFlash &f = kMonsterFlashes[n];
    A3 forward, right;
    angleVectors(ev.angles, &forward, &right, nullptr);
    A3 origin;
    for (int k = 0; k < 3; k++)
        origin[size_t(k)] = ev.origin[size_t(k)] + forward[size_t(k)] * f.offset[0] + right[size_t(k)] * f.offset[1];
    origin[2] += f.offset[2];
    Dlight &dl = allocDlight(ev.entity, t);
    dl.origin = origin;
    dl.radius = float(f.radius + (rand15() & f.radiusRand)); // "rand() & 100" for the widow beams, as Q2
    dl.color = {f.color[0], f.color[1], f.color[2]};
    if (dl.color[0] < 0 || dl.color[1] < 0 || dl.color[2] < 0 || (dl.color[0] + dl.color[1] + dl.color[2]) <= 0)
        dl.radius = 0; // negative / black lights are not drawn
    dl.die = t + std::max(double(f.extraMs), kMuzzleFlashMs);
    if (f.puff) particleEffect(origin, {0, 0, 0}, 0, 40, t);
    if (f.smoke) smokeAndFlash(origin, t);
    if (f.sound[0]) {
        std::string name = f.sound;
        const size_t pct = name.find("%c");
        if (pct != std::string::npos)
            name.replace(pct, 2, 1, char('a' + rand15() % std::max(f.soundVariants, 1)));
        sound(name, ev.entity, ev.origin, 1, float(f.attenuation), kChanWeapon);
    }
}

void GameEffects::entityEvent(const GameEvent &ev, double t) {
    // CL_EntityEvent (the sounds come from the server's events)
    if (ev.level == GEV_ITEM_RESPAWN)
        itemRespawnParticles(ev.origin, t);
    else if (ev.level == GEV_PLAYER_TELEPORT || ev.level == GEV_OTHER_TELEPORT)
        teleportParticles(ev.origin, t);
}

// ------------------------------------------------------------ particles (cl_fx.c)

void GameEffects::particleEffect(const A3 &org, const A3 &dir, int color, int count, double t, int variant) {
    // CL_ParticleEffect (1), CL_ParticleEffect2 (2: one colour, closer), CL_ParticleEffect3 (3: rising)
    count = std::clamp(count, 0, kMaxParticles);
    for (int i = 0; i < count; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = variant == 1 ? color + (rand15() & 7) : color;
        const float d = float(rand15() & (variant == 1 ? 31 : 7));
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = org[size_t(j)] + float((rand15() & 7) - 4) + d * dir[size_t(j)];
            p->vel[size_t(j)] = crand() * 20;
        }
        p->accel = {0, 0, variant == 3 ? kGravity : -kGravity};
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

void GameEffects::logoutEffect(const A3 &org, int type, double t) {
    for (int i = 0; i < 500; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = (type == GMZ_LOGIN ? 0xd0 : type == GMZ_LOGOUT ? 0x40 : 0xe0) + (rand15() & 7);
        p->org = {org[0] - 16 + frand() * 32, org[1] - 16 + frand() * 32, org[2] - 24 + frand() * 56};
        for (float &v : p->vel) v = crand() * 20;
        p->accel = {0, 0, -kGravity};
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1.0f + frand() * 0.3f);
    }
}

void GameEffects::itemRespawnParticles(const A3 &org, double t) {
    for (int i = 0; i < 64; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = 0xd4 + (rand15() & 3);
        for (int j = 0; j < 3; j++) p->org[size_t(j)] = org[size_t(j)] + crand() * 8;
        for (float &v : p->vel) v = crand() * 8;
        p->accel = {0, 0, -kGravity * 0.2f};
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1.0f + frand() * 0.3f);
    }
}

void GameEffects::explosionParticles(const A3 &org, int color, double t) {
    // CL_ExplosionParticles (0xe0) / CL_BFGExplosionParticles (0xd0)
    for (int i = 0; i < 256; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = color + (rand15() & 7);
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = org[size_t(j)] + float(rand15() % 32 - 16);
            p->vel[size_t(j)] = float(rand15() % 384 - 192);
        }
        p->accel = {0, 0, -kGravity};
        p->alpha = 1.0f;
        p->alphavel = -0.8f / (0.5f + frand() * 0.3f);
    }
}

void GameEffects::bigTeleportParticles(const A3 &org, double t) {
    static const int colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    for (int i = 0; i < 4096; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = colortable[rand15() & 3];
        const float angle = kPi * 2 * float(rand15() & 1023) / 1023.0f;
        const float dist = float(rand15() & 31);
        p->org = {org[0] + std::cos(angle) * dist, org[1] + std::sin(angle) * dist, org[2] + 8 + float(rand15() % 90)};
        p->vel = {std::cos(angle) * float(70 + (rand15() & 63)), std::sin(angle) * float(70 + (rand15() & 63)),
                  float(-100 + (rand15() & 31))};
        p->accel = {-std::cos(angle) * 100, -std::sin(angle) * 100, kGravity * 4};
        p->alpha = 1.0f;
        p->alphavel = -0.3f / (0.5f + frand() * 0.3f);
    }
}

void GameEffects::blasterParticles(const A3 &org, const A3 &dir, int color, double t) {
    for (int i = 0; i < 40; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = color + (rand15() & 7);
        const float d = float(rand15() & 15);
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = org[size_t(j)] + float((rand15() & 7) - 4) + d * dir[size_t(j)];
            p->vel[size_t(j)] = dir[size_t(j)] * 30 + crand() * 40;
        }
        p->accel = {0, 0, -kGravity};
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

namespace {
// The trail loops step along start -> end; very long ones (a teleported
// projectile) are clamped so a bad origin can't flood the particle list.
constexpr float kMaxTrail = 2048.0f;
} // namespace

void GameEffects::blasterTrail(const A3 &start, const A3 &end, double t) {
    A3 move = start, vec = sub(end, start);
    float len = std::min(normalize(vec), kMaxTrail);
    vec = scale(vec, 5);
    while (len > 0) {
        len -= 5;
        Particle *p = alloc(t);
        if (!p) return;
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe0;
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = move[size_t(j)] + crand();
            p->vel[size_t(j)] = crand() * 5;
        }
        move = add(move, vec);
    }
}

void GameEffects::flagTrail(const A3 &start, const A3 &end, int color, double t) {
    A3 move = start, vec = sub(end, start);
    float len = std::min(normalize(vec), kMaxTrail);
    vec = scale(vec, 5);
    while (len > 0) {
        len -= 5;
        Particle *p = alloc(t);
        if (!p) return;
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.8f + frand() * 0.2f);
        p->color = color;
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = move[size_t(j)] + crand() * 16;
            p->vel[size_t(j)] = crand() * 5;
        }
        move = add(move, vec);
    }
}

void GameEffects::diminishingTrail(const A3 &start, const A3 &end, EntityState &old, uint32_t flags, double t) {
    A3 move = start, vec = sub(end, start);
    float len = std::min(normalize(vec), kMaxTrail);
    const float dec = 0.5f;
    vec = scale(vec, dec);
    float orgscale = 1, velscale = 5;
    if (old.trailcount > 900) {
        orgscale = 4;
        velscale = 15;
    } else if (old.trailcount > 800) {
        orgscale = 2;
        velscale = 10;
    }
    while (len > 0) {
        len -= dec;
        // drop less particles as it flies
        if ((rand15() & 1023) < old.trailcount) {
            Particle *p = alloc(t);
            if (!p) return;
            if (flags & (Q2_EF_GIB | Q2_EF_GREENGIB)) {
                p->alpha = 1.0f;
                p->alphavel = -1.0f / (1 + frand() * 0.4f);
                p->color = ((flags & Q2_EF_GIB) ? 0xe8 : 0xdb) + (rand15() & 7);
                for (int j = 0; j < 3; j++) {
                    p->org[size_t(j)] = move[size_t(j)] + crand() * orgscale;
                    p->vel[size_t(j)] = crand() * velscale;
                }
                p->vel[2] -= kGravity;
            } else { // smoke
                p->alpha = 1.0f;
                p->alphavel = -1.0f / (1 + frand() * 0.2f);
                p->color = 4 + (rand15() & 7);
                for (int j = 0; j < 3; j++) {
                    p->org[size_t(j)] = move[size_t(j)] + crand() * orgscale;
                    p->vel[size_t(j)] = crand() * velscale;
                }
                p->accel[2] = 20;
            }
        }
        old.trailcount = std::max(old.trailcount - 5, 100);
        move = add(move, vec);
    }
}

void GameEffects::rocketTrail(const A3 &start, const A3 &end, EntityState &old, double t) {
    diminishingTrail(start, end, old, Q2_EF_ROCKET, t); // smoke
    A3 move = start, vec = sub(end, start); // fire
    float len = std::min(normalize(vec), kMaxTrail);
    while (len > 0) {
        len -= 1;
        if ((rand15() & 7) == 0) {
            Particle *p = alloc(t);
            if (!p) return;
            p->alpha = 1.0f;
            p->alphavel = -1.0f / (1 + frand() * 0.2f);
            p->color = 0xdc + (rand15() & 3);
            for (int j = 0; j < 3; j++) {
                p->org[size_t(j)] = move[size_t(j)] + crand() * 5;
                p->vel[size_t(j)] = crand() * 20;
            }
            p->accel[2] = -kGravity;
        }
        move = add(move, vec);
    }
}

void GameEffects::railTrail(const A3 &start, const A3 &end, double t) {
    A3 vec = sub(end, start);
    const float len = std::min(normalize(vec), 8192.0f);
    A3 right, up;
    makeNormalVectors(vec, right, up);
    A3 move = start;
    for (int i = 0; i < int(len); i++) { // the spiral
        Particle *p = alloc(t);
        if (!p) return;
        const float d = float(i) * 0.1f;
        const A3 dir = ma(scale(right, std::cos(d)), std::sin(d), up);
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.2f);
        p->color = 0x74 + (rand15() & 7);
        p->org = ma(move, 3, dir);
        p->vel = scale(dir, 6);
        move = add(move, vec);
    }
    const float dec = 0.75f; // the core
    const A3 step = scale(vec, dec);
    move = start;
    float left = len;
    while (left > 0) {
        left -= dec;
        Particle *p = alloc(t);
        if (!p) return;
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.6f + frand() * 0.2f);
        p->color = rand15() & 15;
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = move[size_t(j)] + crand() * 3;
            p->vel[size_t(j)] = crand() * 3;
        }
        move = add(move, step);
    }
}

void GameEffects::bubbleTrail(const A3 &start, const A3 &end, double t) {
    A3 vec = sub(end, start);
    const float len = std::min(normalize(vec), 8192.0f);
    const A3 step = scale(vec, 32);
    A3 move = start;
    for (float i = 0; i < len; i += 32) {
        Particle *p = alloc(t);
        if (!p) return;
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.2f);
        p->color = 4 + (rand15() & 7);
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = move[size_t(j)] + crand() * 2;
            p->vel[size_t(j)] = crand() * 5;
        }
        p->vel[2] += 6;
        move = add(move, step);
    }
}

void GameEffects::ionripperTrail(const A3 &start, const A3 &end, double t) {
    A3 move = start, vec = sub(end, start);
    float len = std::min(normalize(vec), kMaxTrail);
    vec = scale(vec, 5);
    bool left = false;
    while (len > 0) {
        len -= 5;
        Particle *p = alloc(t);
        if (!p) return;
        p->alpha = 0.5f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe4 + (rand15() & 3);
        p->org = move;
        p->vel = {left ? 10.0f : -10.0f, 0, 0};
        left = !left;
        move = add(move, vec);
    }
}

void GameEffects::flyParticles(const A3 &origin, int count, double t) {
    constexpr float kBeamLength = 16;
    count = std::min(count, 162);
    if (!avelocitiesSet_) {
        for (auto &a : avelocities_)
            for (float &v : a) v = float(rand15() & 255) * 0.01f;
        avelocitiesSet_ = true;
    }
    const float ltime = float(t / 1000.0);
    for (int i = 0; i < count; i += 2) {
        float angle = ltime * avelocities_[size_t(i)][0];
        const float sy = std::sin(angle), cy = std::cos(angle);
        angle = ltime * avelocities_[size_t(i)][1];
        const float sp = std::sin(angle), cp = std::cos(angle);
        const A3 forward{cp * cy, cp * sy, -sp};
        Particle *p = alloc(t);
        if (!p) return;
        const float dist = std::sin(ltime + float(i)) * 64;
        for (int j = 0; j < 3; j++)
            p->org[size_t(j)] = origin[size_t(j)] + kByteDirs[i][j] * dist + forward[size_t(j)] * kBeamLength;
        p->color = 0;
        p->alpha = 1;
        p->alphavel = -100; // this frame only
    }
}

void GameEffects::flyEffect(EntityState &state, const A3 &origin, double t) {
    double starttime;
    if (state.flyStop < t) {
        starttime = t;
        state.flyStop = t + 60000;
    } else {
        starttime = state.flyStop - 60000;
    }
    double n = t - starttime;
    int count;
    if (n < 20000) {
        count = int(n * 162 / 20000.0);
    } else {
        n = state.flyStop - t;
        count = n < 20000 ? int(n * 162 / 20000.0) : 162;
    }
    flyParticles(origin, count, t);
}

void GameEffects::bfgParticles(const A3 &origin, double t) {
    constexpr float kBeamLength = 16;
    if (!avelocitiesSet_) {
        for (auto &a : avelocities_)
            for (float &v : a) v = float(rand15() & 255) * 0.01f;
        avelocitiesSet_ = true;
    }
    const float ltime = float(t / 1000.0);
    for (int i = 0; i < 162; i++) {
        float angle = ltime * avelocities_[size_t(i)][0];
        const float sy = std::sin(angle), cy = std::cos(angle);
        angle = ltime * avelocities_[size_t(i)][1];
        const float sp = std::sin(angle), cp = std::cos(angle);
        const A3 forward{cp * cy, cp * sy, -sp};
        Particle *p = alloc(t);
        if (!p) return;
        const float dist = std::sin(ltime + float(i)) * 64;
        for (int j = 0; j < 3; j++)
            p->org[size_t(j)] = origin[size_t(j)] + kByteDirs[i][j] * dist + forward[size_t(j)] * kBeamLength;
        const float d = length(sub(p->org, origin)) / 90.0f;
        p->color = int(std::floor(0xd0 + d * 7));
        p->alpha = 1.0f - d;
        p->alphavel = -100;
    }
}

void GameEffects::teleporterParticles(const A3 &origin, double t) {
    for (int i = 0; i < 8; i++) {
        Particle *p = alloc(t);
        if (!p) return;
        p->color = 0xdb;
        for (int j = 0; j < 2; j++) {
            p->org[size_t(j)] = origin[size_t(j)] - 16 + float(rand15() & 31);
            p->vel[size_t(j)] = crand() * 14;
        }
        p->org[2] = origin[2] - 8 + float(rand15() & 7);
        p->vel[2] = float(80 + (rand15() & 7));
        p->accel = {0, 0, -kGravity};
        p->alpha = 1.0f;
        p->alphavel = -0.5f;
    }
}

void GameEffects::teleportParticles(const A3 &org, double t) {
    for (int i = -16; i <= 16; i += 4)
        for (int j = -16; j <= 16; j += 4)
            for (int k = -16; k <= 32; k += 4) {
                Particle *p = alloc(t);
                if (!p) return;
                p->color = 7 + (rand15() & 7);
                p->alpha = 1.0f;
                p->alphavel = -1.0f / (0.3f + float(rand15() & 7) * 0.02f);
                p->org = {org[0] + float(i + (rand15() & 3)), org[1] + float(j + (rand15() & 3)),
                          org[2] + float(k + (rand15() & 3))};
                A3 dir{float(j * 8), float(i * 8), float(k * 8)};
                normalize(dir);
                p->vel = scale(dir, float(50 + (rand15() & 63)));
                p->accel = {0, 0, -kGravity};
            }
}

void GameEffects::trapParticles(A3 origin, double t) {
    origin[2] -= 14;
    A3 move = origin, vec{0, 0, 1};
    for (float len = 64; len > 0; len -= 5) {
        Particle *p = alloc(t);
        if (!p) return;
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe0;
        for (int j = 0; j < 3; j++) {
            p->org[size_t(j)] = move[size_t(j)] + crand();
            p->vel[size_t(j)] = crand() * 15;
        }
        p->accel[2] = kGravity;
        move = ma(move, 5, vec);
    }
    origin[2] += 14;
    for (int i = -2; i <= 2; i += 4)
        for (int j = -2; j <= 2; j += 4)
            for (int k = -2; k <= 4; k += 4) {
                Particle *p = alloc(t);
                if (!p) return;
                p->color = 0xe0 + (rand15() & 3);
                p->alpha = 1.0f;
                p->alphavel = -1.0f / (0.3f + float(rand15() & 7) * 0.02f);
                p->org = {origin[0] + float(i) + float(rand15() & 23) * crand(),
                          origin[1] + float(j) + float(rand15() & 23) * crand(),
                          origin[2] + float(k) + float(rand15() & 23) * crand()};
                A3 dir{float(j * 8), float(i * 8), float(k * 8)};
                normalize(dir);
                p->vel = scale(dir, float(50 + (rand15() & 63)));
                p->accel = {0, 0, -kGravity};
            }
}

// ------------------------------------------------------------ entities

void GameEffects::frameLight(const A3 &origin, float radius, float r, float g, float b) {
    if (radius <= 0.0f || frameLights_.size() >= size_t(kMaxDlights)) return;
    frameLights_.push_back({origin, radius, {r, g, b}});
}

void GameEffects::addEntityEffects(const std::vector<GameLerpedEntity> &entities, int snapshot, double t) {
    frameLights_.clear();
    frameGlows_.clear();
    const bool newFrame = snapshot != lastSnapshot_;
    lastSnapshot_ = snapshot;
    for (auto &kv : entities_) kv.second.seen = false;
    for (const GameLerpedEntity &l : entities) {
        const GameRenderEntity *e = l.entity;
        if (!e || !finite3(l.origin)) continue;
        auto it = entities_.find(e->number);
        if (it == entities_.end() || it->second.modelindex != e->modelindex) {
            EntityState fresh;
            fresh.modelindex = e->modelindex;
            // CL_DeltaEntity: a new entity's trail starts at its old_origin
            const A3 old = e->oldOrigin;
            const bool useOld = finite3(old) && length(sub(old, l.origin)) < 512; // not a teleport
            fresh.lerpOrigin = useOld ? old : l.origin;
            it = entities_.insert_or_assign(e->number, fresh).first;
        }
        EntityState &st = it->second;
        st.seen = true;
        const A3 &origin = l.origin;
        const uint32_t fx = e->effects;
        if (newFrame && (fx & Q2_EF_TELEPORTER)) teleporterParticles(origin, t);
        if (endsWith(e->model, ".sp2")) { // sprites (the BFG ball): a glow
            const A3 c = (fx & Q2_EF_BFG) ? A3{0.35f, 1.0f, 0.35f} : A3{1.0f, 0.85f, 0.5f};
            frameGlows_.push_back({origin, {c[0], c[1], c[2], (fx & Q2_EF_BFG) ? 0.6f : 0.8f}, 22.0f});
        }
        if (fx & ~uint32_t(Q2_EF_ROTATE)) {
            if (fx & Q2_EF_ROCKET) {
                rocketTrail(st.lerpOrigin, origin, st, t);
                frameLight(origin, 200, 1, 1, 0);
            } else if (fx & Q2_EF_BLASTER) {
                blasterTrail(st.lerpOrigin, origin, t);
                if (fx & Q2_EF_TRACKER)
                    frameLight(origin, 200, 0, 1, 0);
                else
                    frameLight(origin, 200, 1, 1, 0);
            } else if (fx & Q2_EF_HYPERBLASTER) {
                if (fx & Q2_EF_TRACKER)
                    frameLight(origin, 200, 0, 1, 0);
                else
                    frameLight(origin, 200, 1, 1, 0);
            } else if (fx & (Q2_EF_GIB | Q2_EF_GRENADE)) {
                diminishingTrail(st.lerpOrigin, origin, st, fx, t);
            } else if (fx & Q2_EF_FLIES) {
                flyEffect(st, origin, t);
            } else if (fx & Q2_EF_BFG) {
                int radius;
                if (fx & Q2_EF_ANIM_ALLFAST) {
                    bfgParticles(origin, t);
                    radius = 200;
                } else {
                    radius = kBfgLightRamp[std::clamp(e->frame, 0, 5)];
                }
                frameLight(origin, float(radius), 0, 1, 0);
            } else if (fx & Q2_EF_TRAP) {
                A3 o = origin;
                o[2] += 32;
                trapParticles(o, t);
                frameLight(o, float(rand15() % 100 + 100), 1, 0.8f, 0.1f);
            } else if (fx & Q2_EF_FLAG1) {
                flagTrail(st.lerpOrigin, origin, 242, t);
                frameLight(origin, 225, 1, 0.1f, 0.1f);
            } else if (fx & Q2_EF_FLAG2) {
                flagTrail(st.lerpOrigin, origin, 115, t);
                frameLight(origin, 225, 0.1f, 0.1f, 1);
            } else if (fx & Q2_EF_GREENGIB) {
                diminishingTrail(st.lerpOrigin, origin, st, fx, t);
            } else if (fx & Q2_EF_IONRIPPER) {
                ionripperTrail(st.lerpOrigin, origin, t);
                frameLight(origin, 100, 1, 0.5f, 0.5f);
            } else if (fx & Q2_EF_BLUEHYPERBLASTER) {
                frameLight(origin, 200, 0, 0, 1);
            }
        }
        st.lerpOrigin = origin;
    }
    for (auto it = entities_.begin(); it != entities_.end();)
        it = it->second.seen ? std::next(it) : entities_.erase(it);
}

// ------------------------------------------------------------ frame

void GameEffects::update(double t) {
    // CL_AddParticles drops what has faded; instant particles live one frame
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [&](const Particle &p) {
                                        const double age = (t - p.time) * 0.001;
                                        return p.alpha + float(age) * p.alphavel <= 0.0f;
                                    }),
                     particles_.end());
    for (Explosion &ex : explosions_) {
        if (ex.type == ExType::Free) continue;
        const int f = int(std::floor((t - ex.start) / 100.0));
        if ((ex.type == ExType::Flash && f >= 1) || (ex.type != ExType::Flash && f >= ex.frames - 1))
            ex.type = ExType::Free;
    }
    for (Dlight &d : dlights_)
        if (d.radius > 0.0f && d.die < t) d.radius = 0.0f;
}

void GameEffects::particles(double t, std::vector<GameParticle> &out) const {
    out.clear();
    out.reserve(particles_.size() + frameGlows_.size());
    for (const Particle &p : particles_) {
        const float time = float((t - p.time) * 0.001);
        float alpha = p.alpha + time * p.alphavel;
        if (alpha <= 0.0f) continue;
        alpha = std::min(alpha, 1.0f);
        const float time2 = time * time; // as Quake 2: accel * t^2, not t^2 / 2
        GameParticle g;
        for (int j = 0; j < 3; j++)
            g.origin[size_t(j)] = p.org[size_t(j)] + p.vel[size_t(j)] * time + p.accel[size_t(j)] * time2;
        const A3 &c = palette_[size_t(p.color & 255)];
        g.color = {c[0], c[1], c[2], alpha};
        out.push_back(g);
    }
    for (const GameParticle &g : frameGlows_) out.push_back(g);
    // explosions drawn with a sprite in Quake 2 (BFG): a glow
    for (const Explosion &ex : explosions_) {
        if (ex.type == ExType::Free || !endsWith(ex.ent.model, ".sp2")) continue;
        const float frac = float((t - ex.start) / 100.0);
        const float a = std::clamp(1.0f - frac / float(std::max(ex.frames, 1)), 0.0f, 1.0f);
        out.push_back({ex.ent.origin, {0.35f, 1.0f, 0.35f, 0.7f * a}, 40.0f + 30.0f * frac});
    }
}

void addDynamicLights(const std::array<float, 3> &point, const std::vector<GameDynamicLight> &lights,
                      std::array<float, 3> &rgb) {
    for (const GameDynamicLight &l : lights) {
        const float x = point[0] - l.origin[0], y = point[1] - l.origin[1], z = point[2] - l.origin[2];
        const float add = (l.radius - std::sqrt(x * x + y * y + z * z)) * (1.0f / 256.0f);
        if (!(add > 0.0f)) continue;
        for (int k = 0; k < 3; ++k) rgb[size_t(k)] += add * l.color[size_t(k)];
    }
}

void GameEffects::lights(double t, std::vector<GameDynamicLight> &out) const {
    out.clear();
    for (const Dlight &d : dlights_)
        if (d.radius > 0.0f && d.die >= t) out.push_back({d.origin, d.radius, d.color});
    for (const Explosion &ex : explosions_) {
        if (ex.type == ExType::Free || ex.light <= 0.0f) continue;
        const float frac = float((t - ex.start) / 100.0);
        const int f = int(std::floor(frac));
        float alpha = 1.0f;
        if (ex.type == ExType::Misc)
            alpha = 1.0f - frac / float(std::max(ex.frames - 1, 1));
        else if (ex.type == ExType::Poly)
            alpha = (16.0f - float(std::max(f, 0))) / 16.0f;
        else if (ex.type == ExType::Poly2)
            alpha = (5.0f - float(std::max(f, 0))) / 5.0f;
        if (ex.ent.model == "sprites/s_bfg2.sp2") alpha = 0.3f; // ent.alpha of the sprite
        const float radius = ex.light * alpha;
        if (radius > 0.0f) out.push_back({ex.ent.origin, radius, ex.lightcolor});
    }
    for (const GameDynamicLight &l : frameLights_) out.push_back(l);
    if (out.size() > size_t(kMaxDlights)) out.resize(size_t(kMaxDlights));
}

void GameEffects::models(double t, std::vector<GameEffectModel> &out) const {
    out.clear();
    // CL_AddExplosions
    for (const Explosion &ex : explosions_) {
        if (ex.type == ExType::Free || ex.ent.model.empty() || endsWith(ex.ent.model, ".sp2")) continue;
        const float frac = float((t - ex.start) / 100.0);
        int f = int(std::floor(frac));
        GameEffectModel m = ex.ent;
        bool translucent = ex.translucent;
        switch (ex.type) {
        case ExType::Misc:
            if (f >= ex.frames - 1) continue;
            m.alpha = 1.0f - frac / float(std::max(ex.frames - 1, 1));
            break;
        case ExType::Flash:
            if (f >= 1) continue;
            m.alpha = 1.0f;
            break;
        case ExType::Poly:
            if (f >= ex.frames - 1) continue;
            m.alpha = (16.0f - float(std::max(f, 0))) / 16.0f;
            if (f < 10) {
                m.skinnum = std::max(f >> 1, 0);
            } else {
                translucent = true;
                m.skinnum = f < 13 ? 5 : 6;
            }
            break;
        case ExType::Poly2:
            if (f >= ex.frames - 1) continue;
            m.alpha = (5.0f - float(std::max(f, 0))) / 5.0f;
            m.skinnum = 0;
            translucent = true;
            break;
        case ExType::Free: continue;
        }
        if (!translucent) m.alpha = 1.0f; // gl: alpha only counts with RF_TRANSLUCENT
        if (f < 0) f = 0;
        m.frame = ex.baseframe + f + 1;
        m.oldframe = ex.baseframe + f;
        m.backlerp = 1.0f - std::clamp(frac - float(f), 0.0f, 1.0f);
        out.push_back(std::move(m));
    }
    // CL_AddBeams: model segments from start to end
    for (const Cable &c : cables_) {
        if (c.endtime < t || c.model.empty()) continue;
        const A3 org = add(c.start, c.offset);
        A3 dist = sub(c.end, org);
        float pitch, yaw;
        if (dist[1] == 0 && dist[0] == 0) {
            yaw = 0;
            pitch = dist[2] > 0 ? 90.0f : 270.0f;
        } else {
            yaw = dist[0] ? std::atan2(dist[1], dist[0]) * 180 / kPi : (dist[1] > 0 ? 90.0f : 270.0f);
            if (yaw < 0) yaw += 360;
            const float forward = std::sqrt(dist[0] * dist[0] + dist[1] * dist[1]);
            pitch = std::atan2(dist[2], forward) * -180.0f / kPi;
            if (pitch < 0) pitch += 360.0f;
        }
        const float len = std::min(normalize(dist), 2048.0f);
        constexpr float kModelLength = 30.0f; // the segment models
        A3 cur = org;
        int segment = 0;
        for (float left = len; left > 0; left -= kModelLength, segment++) {
            GameEffectModel m;
            m.model = c.model;
            m.origin = cur;
            // Quake 2 rolls each segment at random every frame; a spin keeps
            // this const and frame-rate independent
            m.angles = {pitch, yaw, float(std::fmod(t * 0.36 + segment * 47.0, 360.0))};
            out.push_back(std::move(m));
            cur = ma(cur, kModelLength, dist);
        }
    }
}

void GameEffects::beams(double t, std::vector<GameBeam> &out) {
    out.clear();
    for (const Laser &l : lasers_) {
        if (l.endtime < t) continue;
        GameBeam b = l.beam;
        const int c = int((uint32_t(l.colors) >> ((rand15() % 4) * 8)) & 0xff);
        const A3 &rgb = palette_[size_t(c)];
        b.color = {rgb[0], rgb[1], rgb[2], 0.3f};
        out.push_back(b);
    }
    for (const GameBeamEntity &e : beamEntities_) {
        GameBeam b;
        b.start = e.origin;
        b.end = e.oldOrigin;
        if (!finite3(b.start) || !finite3(b.end)) continue;
        b.width = float(std::clamp(e.frame, 1, 64));
        const int c = int((uint32_t(e.skinnum) >> ((rand15() % 4) * 8)) & 0xff);
        const A3 &rgb = palette_[size_t(c)];
        b.color = {rgb[0], rgb[1], rgb[2], 0.3f};
        out.push_back(b);
    }
}

// ------------------------------------------------------------ scene

void appendEffectModels(const Engine &engine, GameModelCache &models, const std::vector<GameEffectModel> &effects,
                        GameScene &scene) {
    for (const GameEffectModel &m : effects) {
        if (!isAliasModelName(m.model)) continue;
        const Md3Model *md3 = models.model(engine, m.model);
        if (!md3) continue;
        GameDrawBatch b;
        b.model = m.model;
        b.origin = m.origin;
        b.angles = {-m.angles[0], m.angles[1], m.angles[2]}; // alias models: pitch negated
        b.color = {1, 1, 1, std::clamp(m.alpha, 0.0f, 1.0f)};
        b.fullbright = m.fullbright;
        b.effect = true;
        appendModelBatches(*md3, b, m.frame, m.oldframe, m.backlerp, m.skinnum, scene);
    }
}

} // namespace realms
