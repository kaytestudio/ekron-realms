// Realms map mode for the desktop client (see DesktopMapMode.h).
#include "DesktopMapMode.h"
#include "MapEntityTransform.h"

#include "realms/MapGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace ekron::desktop {
namespace {

bool startsWithWorldspawn(const realms::Engine &engine) {
    const auto ents = realms::Engine::parseEntities(engine.mapEntities());
    return !ents.empty() && realms::Engine::entityValue(ents.front(), "classname") == "worldspawn";
}

} // namespace

bool DesktopMapMode::open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error,
                          const std::string &executableDir) {
    close(gl, renderer);
    executableDir_ = executableDir;
    realms::MapGeometry geometry;
    if (!session_.open(engine, geometry, error)) return false;

    // glTF maps: draw the textured file itself, in its own space.
    if (session_.mesh()) {
        auto model = std::make_unique<DesktopGltfModel>();
        const realms::Engine &reader = engine;
        city::LoadOptions options;
        options.level = false;
        options.buildPhysics = false;
        options.keepSourceSpace = true;
        std::string log;
        if (model->loadWithReader(engine.mapPath(),
                                  [&reader](const std::string &p, std::vector<unsigned char> &out) {
                                      return reader.loadFile(p, out);
                                  },
                                  log, options)) {
            model->upload(gl);
            if (model->ready()) model_ = std::move(model);
        }
        if (!model_) engine.print("map: " + engine.mapPath() + ": no textured model, drawing collision triangles\n");
    }

    // Vertices grouped by BSP model (world first) so brush entities can move.
    std::vector<sandbox::MeshVertex> vertices;
    std::vector<DesktopRenderer::MapDrawRange> ranges;
    if (!model_) {
        std::map<int32_t, realms::MapGeometry> byModel;
        for (const auto &b : geometry.batches) byModel[std::max(b.model, 0)].batches.push_back(b);
        std::vector<sandbox::MeshVertex> part;
        for (auto &[modelIndex, g] : byModel) {
            mapview::toRenderVertices(g, session_.unitsPerMetre(), part);
            if (part.empty()) continue;
            ranges.push_back({modelIndex, GLint(vertices.size()), GLsizei(part.size())});
            vertices.insert(vertices.end(), part.begin(), part.end());
        }
    }
    renderer.setMapGeometry(gl, vertices, ranges.size() > 1 ? ranges : std::vector<DesktopRenderer::MapDrawRange>{});
    if (model_) session_.summary() += ", textured";

    engine.registerCvar("sv_game", "1", REALMS_CVAR_ARCHIVE);
    engine.registerCvar("cl_gun", "1", REALMS_CVAR_ARCHIVE);
    std::string why;
    if (engine.cvarBool("sv_game", true) && startGame(engine, why)) {
        session_.summary() += ", game";
        engine.print("map: Quake 2 game running (" + game_->moduleOrigin() + "); " +
                     std::to_string(game_->numEdicts()) + " edicts\n");
    } else if (!why.empty()) {
        engine.print("map: walking without the game module: " + why + "\n");
    }
    return true;
}

bool DesktopMapMode::startGame(realms::Engine &engine, std::string &why) {
    std::string where;
    if (!realms::GameServer::moduleAvailable(executableDir_, &where, &why)) return false;
    if (!startsWithWorldspawn(engine)) {
        why = "the map has no worldspawn entity";
        return false;
    }
    if (!game_) game_ = std::make_unique<realms::GameServer>();
    realms::GameServerConfig config;
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
    gameSounds_.clear();
    std::string palette;
    if (!effects_.loadPalette(engine, &palette))
        engine.print("map: effects use approximate colours (" + palette + ")\n", REALMS_PRINT_DEVELOPER);
    syncFromGame();
    return true;
}

void DesktopMapMode::close(ekron::gl::Api &gl, DesktopRenderer &renderer) {
    if (game_) {
        game_->stop();
        game_.reset();
    }
    poses_.clear();
    messages_.clear();
    lerp_.reset();
    effects_.reset();
    gameSounds_.clear();
    renderer.clearMapEffects();
    models_.clear();
    scene_.clear();
    releaseSkins(gl);
    renderer.clearMapEntities();
    if (model_) {
        model_->release(gl);
        model_.reset();
    }
    if (session_.active()) renderer.clearMapGeometry(gl);
    session_.close();
}

void DesktopMapMode::update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw,
                            float renderPitch, float dt, bool attack) {
    if (!gameRunning()) {
        if (game_) { // the game stopped (game error): back to walking
            messages_.push_back("Game stopped: " + game_->lastError());
            game_.reset();
            poses_.clear();
        }
        session_.update(engine, input, renderYaw, renderPitch, dt);
        return;
    }
    // Quake 2 client: always-run speeds (cl_forwardspeed / cl_sidespeed /
    // cl_upspeed 200, x2 run) and the camera's absolute angles.
    const realms::WalkInput in = mapview::walkInput(input, renderYaw, renderPitch);
    realms::GameUserCmd cmd;
    cmd.forward = std::clamp(in.forward, -1.0f, 1.0f) * 400.0f;
    cmd.side = std::clamp(in.right, -1.0f, 1.0f) * 400.0f;
    cmd.up = in.jump ? 400.0f : in.crouch ? -400.0f : std::clamp(in.up, -1.0f, 1.0f) * 400.0f;
    cmd.yaw = in.yaw;
    cmd.pitch = in.pitch;
    cmd.attack = attack;
    game_->frame(cmd, dt);
    syncFromGame();
}

void DesktopMapMode::syncFromGame() {
    if (!gameRunning()) return;
    const realms::GameServer &g = *game_;
    // The walker mirrors the game's player for the HUD and the sound director.
    realms::MapWalker &w = session_.walker();
    const realms::Vec3 o = g.playerOrigin();
    w.origin = o;
    if (const q2player_state_t *ps = g.playerState()) {
        for (int i = 0; i < 3; i++) w.velocity[size_t(i)] = ps->pmove.velocity[i] * 0.125f;
        w.onGround = (ps->pmove.pm_flags & Q2_PMF_ON_GROUND) != 0;
        w.ducked = (ps->pmove.pm_flags & Q2_PMF_DUCKED) != 0;
        w.noclip = ps->pmove.pm_type == Q2_PM_SPECTATOR;
        w.waterLevel = (ps->rdflags & 1 /* RDF_UNDERWATER */) ? 3 : 0;
        const int pickup = ps->stats[Q2_STAT_PICKUP_STRING];
        if (pickup && pickup != lastPickup_) {
            const std::string &item = g.configString(pickup);
            if (!item.empty()) messages_.push_back(item);
        }
        lastPickup_ = pickup;
    }
    // Client time: the server frames plus the interpolation fraction, so
    // effects advance smoothly between game frames.
    const double now = realms::clientTimeMs(g);
    for (const realms::GameEvent &ev : game_->takeEvents()) {
        effects_.handleEvent(ev, now); // temp entities, muzzle flashes, entity events
        if (ev.kind == realms::GameEvent::Kind::Sound) gameSounds_.push_back(ev);
        if (ev.kind == realms::GameEvent::Kind::CenterPrint && !ev.text.empty()) {
            std::string t = ev.text;
            while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
            if (!t.empty()) messages_.push_back(t);
        }
    }
    if (messages_.size() > 16) messages_.erase(messages_.begin(), messages_.end() - 16);

    // Brush entity poses: each inline model drawn where its entity is
    // (interpolated between game frames, so doors and lifts glide), hidden
    // when no visible entity uses it (func_wall off, func_explosive gone...).
    lerp_.update(g);
    const std::vector<realms::GameLerpedEntity> lerped = lerp_.entities(g);
    int models = 0;
    for (const realms::GameLerpedEntity &l : lerped)
        if (l.entity) models = std::max(models, l.entity->inlineModel);
    poses_.assign(size_t(std::max(models, 0) + 1), DesktopRenderer::MapModelPose{});
    std::vector<bool> used(poses_.size(), false);
    for (const realms::GameLerpedEntity &l : lerped) {
        const int m = l.entity ? l.entity->inlineModel : 0;
        if (m <= 0 || used[size_t(m)]) continue;
        used[size_t(m)] = true;
        poses_[size_t(m)].transform = entityTransform(l.origin.data(), l.angles.data(), session_.unitsPerMetre());
    }
    for (size_t m = 1; m < poses_.size(); m++) poses_[m].visible = used[m];

    // Entity effects (trails, flies, lights), laser entities, then age everything.
    effects_.addEntityEffects(lerped, g.frameNumber(), now);
    effects_.setBeamEntities(g.beamEntities());
    effects_.update(now);
    for (realms::GameEvent &ev : effects_.takeSounds()) gameSounds_.push_back(std::move(ev));
    if (gameSounds_.size() > 256) gameSounds_.erase(gameSounds_.begin(), gameSounds_.end() - 256);
}

std::vector<realms::GameEvent> DesktopMapMode::takeGameSounds() {
    std::vector<realms::GameEvent> out;
    out.swap(gameSounds_);
    return out;
}

std::vector<realms::GameLoopSound> DesktopMapMode::loopSounds() const {
    return gameRunning() ? game_->loopSounds() : std::vector<realms::GameLoopSound>{};
}

realms::GameListener DesktopMapMode::listener() const {
    realms::GameListener l;
    if (!gameRunning()) return l;
    l.origin = game_->viewOrigin();
    l.right = realms::gameRightVector(game_->viewAngles());
    l.entity = 1; // the local player's edict
    return l;
}

void DesktopMapMode::buildEffects(float renderYaw, float renderPitch,
                                  std::vector<DesktopRenderer::EffectVertex> &vertices,
                                  std::vector<DesktopRenderer::DynamicLight> &lights) {
    vertices.clear();
    lights.clear();
    if (!gameRunning()) return;
    const double now = realms::clientTimeMs(*game_);
    const float upm = session_.unitsPerMetre();
    // the camera in Quake 2 space
    const realms::WalkInput view = mapview::walkInput({}, renderYaw, renderPitch);
    const float d2r = 3.14159265f / 180.0f;
    const float sy = std::sin(view.yaw * d2r), cy = std::cos(view.yaw * d2r);
    const float sp = std::sin(view.pitch * d2r), cp = std::cos(view.pitch * d2r);
    const std::array<float, 3> forward{cp * cy, cp * sy, -sp}, right{sy, -cy, 0.0f}, up{sp * cy, sp * sy, cp};
    const realms::Vec3 eyeQ = game_->viewOrigin();
    auto toR = [&](const std::array<float, 3> &q) { return mapview::toRender(realms::Vec3{q[0], q[1], q[2]}, upm); };
    auto quad = [&](const std::array<std::array<float, 3>, 4> &c, const std::array<float, 4> &col, float shape) {
        static const float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            const physics::Vec3 r = toR(c[size_t(k)]);
            vertices.push_back({r.x, r.y, r.z, col[0], col[1], col[2], col[3], uv[k][0], uv[k][1], shape});
        }
    };

    effects_.particles(now, fxParticles_);
    vertices.reserve(fxParticles_.size() * 6 + 64);
    for (const realms::GameParticle &p : fxParticles_) {
        // gl_rpart.c: about one unit up close, growing with distance
        const float depth = (p.origin[0] - eyeQ[0]) * forward[0] + (p.origin[1] - eyeQ[1]) * forward[1] +
                            (p.origin[2] - eyeQ[2]) * forward[2];
        if (depth < 1.0f) continue; // behind the camera
        const float scale = depth < 20.0f ? 1.0f : 1.0f + depth * 0.004f;
        const float h = p.size > 1.0f ? p.size : 0.8f * scale;
        std::array<std::array<float, 3>, 4> c;
        for (int k = 0; k < 3; k++) {
            const size_t i = size_t(k);
            c[0][i] = p.origin[i] - right[i] * h - up[i] * h;
            c[1][i] = p.origin[i] + right[i] * h - up[i] * h;
            c[2][i] = p.origin[i] + right[i] * h + up[i] * h;
            c[3][i] = p.origin[i] - right[i] * h + up[i] * h;
        }
        quad(c, p.color, 0.0f);
    }
    effects_.beams(now, fxBeams_);
    for (const realms::GameBeam &b : fxBeams_) {
        std::array<float, 3> dir{b.end[0] - b.start[0], b.end[1] - b.start[1], b.end[2] - b.start[2]};
        const std::array<float, 3> mid{(b.start[0] + b.end[0]) * 0.5f - eyeQ[0], (b.start[1] + b.end[1]) * 0.5f - eyeQ[1],
                                       (b.start[2] + b.end[2]) * 0.5f - eyeQ[2]};
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
    effects_.lights(now, fxLights_);
    std::sort(fxLights_.begin(), fxLights_.end(), [&](const realms::GameDynamicLight &a, const realms::GameDynamicLight &b) {
        auto d = [&](const realms::GameDynamicLight &l) {
            const float x = l.origin[0] - eyeQ[0], y = l.origin[1] - eyeQ[1], z = l.origin[2] - eyeQ[2];
            return x * x + y * y + z * z - l.radius * l.radius;
        };
        return d(a) < d(b);
    });
    for (const realms::GameDynamicLight &l : fxLights_) {
        if (lights.size() >= DesktopRenderer::kMaxMapLights) break;
        const physics::Vec3 r = toR(l.origin);
        lights.push_back({r.x, r.y, r.z, l.radius / upm, l.color[0], l.color[1], l.color[2]});
    }
}

void DesktopMapMode::releaseSkins(ekron::gl::Api &gl) {
    for (auto &kv : skins_)
        if (kv.second) gl.DeleteTextures(1, &kv.second);
    skins_.clear();
}

GLuint DesktopMapMode::skinTexture(realms::Engine &engine, ekron::gl::Api &gl, const std::string &name) {
    if (name.empty()) return 0;
    const auto it = skins_.find(name);
    if (it != skins_.end()) return it->second;
    GLuint tex = 0;
    realms::RgbaImage img;
    std::string error;
    if (realms::GameModelCache::loadSkin(engine, name, img, error)) {
        gl.GenTextures(1, &tex);
        gl.BindTexture(GL_TEXTURE_2D, tex);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_SRGB8_ALPHA8), img.width, img.height, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, img.rgba.data());
        gl.GenerateMipmap(GL_TEXTURE_2D);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        gl.BindTexture(GL_TEXTURE_2D, 0);
    } else {
        engine.print("map: skin " + name + ": " + error + "\n", REALMS_PRINT_DEVELOPER);
    }
    skins_[name] = tex;
    return tex;
}

void DesktopMapMode::buildEntities(realms::Engine &engine, ekron::gl::Api &gl, float renderYaw, float renderPitch,
                                   std::vector<DesktopRenderer::EntityVertex> &vertices,
                                   std::vector<DesktopRenderer::EntityBatch> &batches) {
    vertices.clear();
    batches.clear();
    if (!gameRunning()) return;
    realms::GameSceneOptions opt;
    opt.viewWeapon = engine.cvarBool("cl_gun", true) && !noclip();
    opt.hasCamera = true;
    opt.cameraOrigin = game_->viewOrigin();
    const realms::WalkInput view = mapview::walkInput({}, renderYaw, renderPitch);
    opt.cameraAngles = {view.pitch, view.yaw, 0.0f};
    realms::buildGameScene(engine, *game_, lerp_, models_, opt, scene_);
    // explosions, smoke, flashes and cable segments (cl_tent.c)
    effects_.models(realms::clientTimeMs(*game_), fxModels_);
    realms::appendEffectModels(engine, models_, fxModels_, scene_);
    for (const std::string &e : models_.takeErrors()) engine.print("map: model " + e + "\n", REALMS_PRINT_DEVELOPER);

    // Model space (Quake 2 units, x forward, y left, z up) to the renderer's
    // axes and metres, like the map: (x, z, -y) / upm. entityTransform then
    // rotates and places them in render space (a proper rotation, so the
    // counter-clockwise winding is kept).
    const float s = 1.0f / session_.unitsPerMetre();
    vertices.reserve(scene_.vertices.size());
    for (const realms::Md2Vertex &v : scene_.vertices) {
        const auto r = toRenderModelVertex(v, s);
        vertices.push_back({r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]});
    }
    batches.reserve(scene_.batches.size());
    for (const realms::GameDrawBatch &b : scene_.batches) {
        DesktopRenderer::EntityBatch out;
        out.first = b.first;
        out.count = b.count;
        out.texture = skinTexture(engine, gl, b.skin);
        out.color = {b.color[0], b.color[1], b.color[2], b.color[3]};
        // untextured models (skin missing) get a neutral grey, boxes their colour
        if (!out.texture && !b.fallbackBox) out.color = {0.7f * b.color[0], 0.7f * b.color[1], 0.72f * b.color[2], b.color[3]};
        out.transform = entityTransform(b.origin.data(), b.angles.data(), session_.unitsPerMetre());
        out.viewWeapon = b.viewWeapon;
        out.fullbright = b.fullbright;
        batches.push_back(out);
    }
}

bool DesktopMapMode::respawn(realms::Engine &engine) {
    if (!session_.active()) return false;
    if (game_) {
        std::string why;
        if (startGame(engine, why)) return true;
        engine.print("map: game restart failed: " + why + "\n");
        return session_.respawn(engine);
    }
    return session_.respawn(engine);
}

void DesktopMapMode::toggleNoclip() {
    if (gameRunning()) game_->clientCommand("noclip");
    else session_.toggleNoclip();
}

bool DesktopMapMode::noclip() const {
    if (gameRunning()) {
        const q2player_state_t *ps = game_->playerState();
        return ps && ps->pmove.pm_type == Q2_PM_SPECTATOR;
    }
    return session_.walker().noclip;
}

physics::Vec3 DesktopMapMode::eye() const {
    if (gameRunning()) return mapview::toRender(game_->viewOrigin(), session_.unitsPerMetre());
    return session_.eye();
}

float DesktopMapMode::spawnRenderYaw() const {
    if (gameRunning()) return mapview::renderYawFromMap(game_->viewAngles()[1]);
    return session_.spawnRenderYaw();
}

std::string DesktopMapMode::gameStatus() const {
    if (!gameRunning()) return {};
    const q2player_state_t *ps = game_->playerState();
    if (!ps) return {};
    char buf[96];
    std::snprintf(buf, sizeof(buf), "HP %d  Armor %d  Ammo %d", int(ps->stats[Q2_STAT_HEALTH]),
                  int(ps->stats[Q2_STAT_ARMOR]), int(ps->stats[Q2_STAT_AMMO]));
    return buf;
}

std::vector<std::string> DesktopMapMode::takeGameMessages() {
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}

} // namespace ekron::desktop
