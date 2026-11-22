// Realms Engine - the Quake 2 stack as one module (see Quake2Module.h).
#include "realms/Quake2Module.h"

#include "realms/q2game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace realms {
namespace {

bool startsWithWorldspawn(const Engine &engine) {
    const auto ents = Engine::parseEntities(engine.mapEntities());
    return !ents.empty() && Engine::entityValue(ents.front(), "classname") == "worldspawn";
}

struct CameraAxes {
    std::array<float, 3> forward, right, up;
};

// AngleVectors for (pitch, yaw, roll = 0).
CameraAxes cameraAxes(const Vec3 &angles) {
    const float d2r = 3.14159265f / 180.0f;
    const float sy = std::sin(angles[1] * d2r), cy = std::cos(angles[1] * d2r);
    const float sp = std::sin(angles[0] * d2r), cp = std::cos(angles[0] * d2r);
    return {{cp * cy, cp * sy, -sp}, {sy, -cy, 0.0f}, {sp * cy, sp * sy, cp}};
}

} // namespace

Quake2Module::Quake2Module() : lightStyles_(defaultLightStyles()) {}
Quake2Module::~Quake2Module() { close(); }

void Quake2Module::open(Engine &engine, const MapGeometry &geometry, const Quake2ModuleConfig &config,
                        std::string &why) {
    close();
    why.clear();
    executableDir_ = config.executableDir;
    if (config.lightmaps && geometry.source == MapGeometrySource::Faces) {
        std::string error;
        if (!lighting_.load(engine, error))
            engine.print("map: no lightmaps: " + error + "\n", REALMS_PRINT_DEVELOPER);
        if (!lighting_.empty()) lighting_.compose(lightStyles_, lightmapRgba_);
    }
    if (config.textures &&
        (geometry.source == MapGeometrySource::Faces || geometry.source == MapGeometrySource::Brushes)) {
        textures_.load(engine, geometry);
        if (textures_.missing())
            engine.print("map: " + std::to_string(textures_.missing()) + " textures not found\n", REALMS_PRINT_DEVELOPER);
    }
    if (config.sky && (geometry.source == MapGeometrySource::Faces || geometry.source == MapGeometrySource::Brushes)) {
        std::string error;
        if (!sky_.load(engine, skySettings(engine.mapEntities()), &error))
            engine.print("map: no sky: " + error + "\n", REALMS_PRINT_DEVELOPER);
    }
    if (config.startGame) startGame(engine, why);
}

void Quake2Module::close() {
    if (game_) {
        game_->stop();
        game_.reset();
    }
    failure_.clear();
    poses_.clear();
    messages_.clear();
    lastPickup_ = 0;
    lerp_.reset();
    models_.clear();
    scene_.clear();
    effects_.reset();
    sounds_.clear();
    lighting_.clear();
    textures_.clear();
    sky_.clear();
    lightStyles_ = defaultLightStyles();
    lightmapRgba_.clear();
}

bool Quake2Module::startGame(Engine &engine, std::string &why) {
    std::string where;
    if (!GameServer::moduleAvailable(executableDir_, &where, &why)) return false;
    if (!startsWithWorldspawn(engine)) {
        why = "the map has no worldspawn entity";
        return false;
    }
    if (!game_) game_ = std::make_unique<GameServer>();
    GameServerConfig config;
    config.executableDir = executableDir_;
    config.playerName = engine.cvarString("name", "Player");
    config.skill = engine.cvarInt("skill", 1);
    if (!game_->start(engine, config, why)) {
        game_.reset();
        return false;
    }
    lastPickup_ = 0;
    lerp_.reset();
    effects_.reset();
    sounds_.clear();
    std::string palette;
    if (!effects_.loadPalette(engine, &palette))
        engine.print("map: effects use the built-in palette (" + palette + ")\n", REALMS_PRINT_DEVELOPER);
    sync();
    return true;
}

bool Quake2Module::restartGame(Engine &engine, std::string &why) {
    poses_.clear();
    scene_.clear();
    return startGame(engine, why);
}

bool Quake2Module::gameRunning() const { return game_ && game_->running(); }
const GameServer *Quake2Module::game() const { return gameRunning() ? game_.get() : nullptr; }
GameServer *Quake2Module::game() { return gameRunning() ? game_.get() : nullptr; }

std::string Quake2Module::takeGameFailure() {
    if (game_ && !game_->running()) { // the game stopped (game error): back to walking
        failure_ = game_->lastError();
        if (failure_.empty()) failure_ = "the game stopped";
        game_.reset();
        poses_.clear();
        scene_.clear();
    }
    std::string out;
    out.swap(failure_);
    return out;
}

GameUserCmd Quake2Module::userCmd(const WalkInput &in, bool attack) {
    GameUserCmd cmd;
    cmd.forward = std::clamp(in.forward, -1.0f, 1.0f) * 400.0f;
    cmd.side = std::clamp(in.right, -1.0f, 1.0f) * 400.0f;
    cmd.up = in.jump ? 400.0f : in.crouch ? -400.0f : std::clamp(in.up, -1.0f, 1.0f) * 400.0f;
    cmd.yaw = in.yaw;
    cmd.pitch = in.pitch;
    cmd.attack = attack;
    return cmd;
}

void Quake2Module::frame(const GameUserCmd &cmd, double dt) {
    if (!gameRunning()) return;
    game_->frame(cmd, dt);
    sync();
}

void Quake2Module::sync() {
    if (!gameRunning()) return;
    const GameServer &g = *game_;
    if (const q2player_state_t *ps = g.playerState()) {
        const int pickup = ps->stats[Q2_STAT_PICKUP_STRING];
        if (pickup && pickup != lastPickup_) {
            const std::string &item = g.configString(pickup);
            if (!item.empty()) messages_.push_back(item);
        }
        lastPickup_ = pickup;
    }
    // Client time: the server frames plus the interpolation fraction, so
    // effects advance smoothly between game frames.
    const double now = clientTimeMs(g);
    for (const GameEvent &ev : game_->takeEvents()) {
        effects_.handleEvent(ev, now); // temp entities, muzzle flashes, entity events
        if (ev.kind == GameEvent::Kind::Sound) sounds_.push_back(ev);
        if (ev.kind == GameEvent::Kind::CenterPrint && !ev.text.empty()) {
            std::string t = ev.text;
            while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
            if (!t.empty()) messages_.push_back(t);
        }
    }
    if (messages_.size() > 16) messages_.erase(messages_.begin(), messages_.end() - 16);

    // Brush entity poses: each inline model where its entity is
    // (interpolated, so doors and lifts glide), hidden when no visible
    // entity uses it (func_wall off, func_explosive gone...).
    lerp_.update(g);
    const std::vector<GameLerpedEntity> lerped = lerp_.entities(g);
    int models = 0;
    for (const GameLerpedEntity &l : lerped)
        if (l.entity) models = std::max(models, l.entity->inlineModel);
    poses_.assign(size_t(std::max(models, 0) + 1), Quake2ModelPose{});
    std::vector<bool> used(poses_.size(), false);
    for (const GameLerpedEntity &l : lerped) {
        const int m = l.entity ? l.entity->inlineModel : 0;
        if (m <= 0 || used[size_t(m)]) continue;
        used[size_t(m)] = true;
        poses_[size_t(m)].origin = l.origin;
        poses_[size_t(m)].angles = l.angles;
    }
    for (size_t m = 1; m < poses_.size(); m++) poses_[m].visible = used[m];

    // Entity effects (trails, flies, lights), laser entities, then age everything.
    effects_.addEntityEffects(lerped, g.frameNumber(), now);
    effects_.setBeamEntities(g.beamEntities());
    effects_.update(now);
    for (GameEvent &ev : effects_.takeSounds()) sounds_.push_back(std::move(ev));
    if (sounds_.size() > 256) sounds_.erase(sounds_.begin(), sounds_.end() - 256);
}

Vec3 Quake2Module::viewOrigin() const { return gameRunning() ? game_->viewOrigin() : Vec3{0, 0, 0}; }
Vec3 Quake2Module::viewAngles() const { return gameRunning() ? game_->viewAngles() : Vec3{0, 0, 0}; }

bool Quake2Module::noclip() const {
    if (!gameRunning()) return false;
    const q2player_state_t *ps = game_->playerState();
    return ps && ps->pmove.pm_type == Q2_PM_SPECTATOR;
}

void Quake2Module::toggleNoclip() {
    if (gameRunning()) game_->clientCommand("noclip");
}

void Quake2Module::mirrorPlayer(MapWalker &w) const {
    if (!gameRunning()) return;
    w.origin = game_->playerOrigin();
    if (const q2player_state_t *ps = game_->playerState()) {
        for (int i = 0; i < 3; i++) w.velocity[size_t(i)] = ps->pmove.velocity[i] * 0.125f;
        w.onGround = (ps->pmove.pm_flags & Q2_PMF_ON_GROUND) != 0;
        w.ducked = (ps->pmove.pm_flags & Q2_PMF_DUCKED) != 0;
        w.noclip = ps->pmove.pm_type == Q2_PM_SPECTATOR;
        w.waterLevel = (ps->rdflags & 1 /* RDF_UNDERWATER */) ? 3 : 0;
    }
}

Quake2Module::PlayerStats Quake2Module::stats() const {
    PlayerStats s;
    if (!gameRunning()) return s;
    if (const q2player_state_t *ps = game_->playerState()) {
        s.health = ps->stats[Q2_STAT_HEALTH];
        s.armor = ps->stats[Q2_STAT_ARMOR];
        s.ammo = ps->stats[Q2_STAT_AMMO];
    }
    return s;
}

std::string Quake2Module::status() const {
    if (!gameRunning() || !game_->playerState()) return {};
    const PlayerStats s = stats();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "HP %d  Armor %d  Ammo %d", s.health, s.armor, s.ammo);
    return buf;
}

std::vector<std::string> Quake2Module::takeMessages() {
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}

const GameScene &Quake2Module::buildScene(Engine &engine, const Vec3 &cameraOrigin, const Vec3 &cameraAngles,
                                          bool viewWeapon) {
    scene_.clear();
    if (!gameRunning()) return scene_;
    GameSceneOptions opt;
    opt.viewWeapon = viewWeapon && !noclip();
    opt.hasCamera = true;
    opt.cameraOrigin = cameraOrigin;
    opt.cameraAngles = cameraAngles;
    buildGameScene(engine, *game_, lerp_, models_, opt, scene_);
    // explosions, smoke, flashes and cable segments (cl_tent.c)
    effects_.models(clientTimeMs(*game_), fxModels_);
    appendEffectModels(engine, models_, fxModels_, scene_);
    for (const std::string &e : models_.takeErrors()) engine.print("map: model " + e + "\n", REALMS_PRINT_DEVELOPER);
    effects_.lights(clientTimeMs(*game_), fxLights_);
    for (GameDrawBatch &b : scene_.batches)
        if (!b.fullbright) b.lit = lightAt(b.origin, fxLights_, b.light);
    return scene_;
}

void Quake2Module::buildEffects(const Vec3 &eye, const Vec3 &cameraAngles, size_t maxLights,
                                std::vector<Quake2EffectVertex> &quads, std::vector<GameDynamicLight> &lights) {
    quads.clear();
    lights.clear();
    if (!gameRunning()) return;
    const double now = clientTimeMs(*game_);
    const CameraAxes axes = cameraAxes(cameraAngles);
    const auto &forward = axes.forward, &right = axes.right, &up = axes.up;
    auto quad = [&](const std::array<std::array<float, 3>, 4> &c, const std::array<float, 4> &col, float shape) {
        static const float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            const auto &p = c[size_t(k)];
            quads.push_back({p[0], p[1], p[2], col[0], col[1], col[2], col[3], uv[k][0], uv[k][1], shape});
        }
    };

    effects_.particles(now, fxParticles_);
    quads.reserve(fxParticles_.size() * 6 + 64);
    for (const GameParticle &p : fxParticles_) {
        // gl_rpart.c: about one unit up close, growing with distance
        const float depth = (p.origin[0] - eye[0]) * forward[0] + (p.origin[1] - eye[1]) * forward[1] +
                            (p.origin[2] - eye[2]) * forward[2];
        if (depth < 1.0f) continue; // behind the camera
        const float scale = depth < 20.0f ? 1.0f : 1.0f + depth * 0.004f;
        const float h = p.size > 1.0f ? p.size : 0.8f * scale;
        std::array<std::array<float, 3>, 4> c;
        for (size_t i = 0; i < 3; i++) {
            c[0][i] = p.origin[i] - right[i] * h - up[i] * h;
            c[1][i] = p.origin[i] + right[i] * h - up[i] * h;
            c[2][i] = p.origin[i] + right[i] * h + up[i] * h;
            c[3][i] = p.origin[i] - right[i] * h + up[i] * h;
        }
        quad(c, p.color, 0.0f);
    }
    effects_.beams(now, fxBeams_);
    for (const GameBeam &b : fxBeams_) {
        const std::array<float, 3> dir{b.end[0] - b.start[0], b.end[1] - b.start[1], b.end[2] - b.start[2]};
        const std::array<float, 3> mid{(b.start[0] + b.end[0]) * 0.5f - eye[0], (b.start[1] + b.end[1]) * 0.5f - eye[1],
                                       (b.start[2] + b.end[2]) * 0.5f - eye[2]};
        std::array<float, 3> side{dir[1] * mid[2] - dir[2] * mid[1], dir[2] * mid[0] - dir[0] * mid[2],
                                  dir[0] * mid[1] - dir[1] * mid[0]};
        const float len = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (len < 1e-6f) continue;
        for (float &v : side) v *= 0.5f * b.width / len;
        std::array<std::array<float, 3>, 4> c;
        for (size_t i = 0; i < 3; i++) {
            c[0][i] = b.start[i] - side[i];
            c[1][i] = b.start[i] + side[i];
            c[2][i] = b.end[i] + side[i];
            c[3][i] = b.end[i] - side[i];
        }
        quad(c, b.color, 1.0f);
    }
    // lights: the closest to the camera first
    effects_.lights(now, lights);
    auto distance = [&](const GameDynamicLight &l) {
        const float x = l.origin[0] - eye[0], y = l.origin[1] - eye[1], z = l.origin[2] - eye[2];
        return x * x + y * y + z * z - l.radius * l.radius;
    };
    std::stable_sort(lights.begin(), lights.end(),
                     [&](const GameDynamicLight &a, const GameDynamicLight &b) { return distance(a) < distance(b); });
    if (lights.size() > maxLights) lights.resize(maxLights);
}

std::vector<GameEvent> Quake2Module::takeSounds() {
    std::vector<GameEvent> out;
    out.swap(sounds_);
    return out;
}

std::vector<GameLoopSound> Quake2Module::loopSounds() const {
    return gameRunning() ? game_->loopSounds() : std::vector<GameLoopSound>{};
}

GameListener Quake2Module::listener() const {
    GameListener l;
    if (!gameRunning()) return l;
    l.origin = game_->viewOrigin();
    l.right = gameRightVector(game_->viewAngles());
    l.entity = 1; // the local player's edict
    return l;
}

bool Quake2Module::lightmapUvs(const MapGeometry &geometry, std::vector<float> &uvs) const {
    uvs.clear();
    if (lighting_.empty()) return false;
    uvs.reserve(geometry.triangleCount() * 6);
    for (const auto &b : geometry.batches) {
        const size_t n = b.positions.size() / 3;
        for (size_t i = 0; i < n; ++i) {
            float uv[2] = {-1.0f, -1.0f};
            const size_t tri = i / 3;
            const int32_t face = tri < b.faces.size() ? b.faces[tri] : -1;
            if (face < 0 || !lighting_.faceUv(size_t(face), &b.positions[i * 3], uv)) uv[0] = uv[1] = -1.0f;
            uvs.push_back(uv[0]);
            uvs.push_back(uv[1]);
        }
    }
    return true;
}

void Quake2Module::disableLightmaps() {
    lighting_.clear();
    lightmapRgba_.clear();
}

MapLightStyles Quake2Module::currentLightStyles() const {
    MapLightStyles styles = defaultLightStyles();
    if (!gameRunning()) return styles;
    // CL_SetLightstyle / CL_RunLightStyles: the configstrings at 10 Hz
    const double now = clientTimeMs(*game_);
    for (int s : lighting_.styles()) {
        if (s >= Q2_MAX_LIGHTSTYLES) continue;
        styles[size_t(s)] = lightStyleValue(game_->configString(Q2_CS_LIGHTS + s), now);
    }
    return styles;
}

bool Quake2Module::updateLightStyles() {
    if (lighting_.empty()) return false;
    const MapLightStyles styles = currentLightStyles();
    if (!lighting_.stylesDiffer(styles, lightStyles_)) return false;
    lightStyles_ = styles;
    lighting_.compose(lightStyles_, lightmapRgba_);
    return true;
}

bool Quake2Module::lightAt(const Vec3 &p, std::array<float, 3> &rgb) const {
    std::vector<GameDynamicLight> dlights;
    if (gameRunning()) effects_.lights(clientTimeMs(*game_), dlights);
    return lightAt(p, dlights, rgb);
}

bool Quake2Module::lightAt(const Vec3 &p, const std::vector<GameDynamicLight> &dlights,
                           std::array<float, 3> &rgb) const {
    rgb = {1.0f, 1.0f, 1.0f};
    if (lighting_.empty()) return false;
    lighting_.lightPoint(p.data(), lightStyles_, rgb.data());
    addDynamicLights(p, dlights, rgb);
    if (std::max({rgb[0], rgb[1], rgb[2]}) < 0.1f) rgb = {0.1f, 0.1f, 0.1f};
    return true;
}

} // namespace realms
