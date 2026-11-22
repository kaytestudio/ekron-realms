// Realms map mode for the desktop client (see DesktopMapMode.h).
#include "DesktopMapMode.h"

#include "realms/MapGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace ekron::desktop {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Map space (Quake 2: X fwd, Y left, Z up, units) -> render space (metres,
// Y up, -Z forward): r = C * m, C = (x, z, -y) / upm.
// A brush entity's pose is T(origin) * Rz(yaw) * Ry(-pitch) * Rx(-roll)
// (R_RotateForEntity), conjugated into render space: C * M * C^-1.
std::array<float, 16> entityTransform(const float origin[3], const float angles[3], float upm) {
    const float s = 1.0f / upm;
    const float y = angles[1] * kPi / 180.0f, p = -angles[0] * kPi / 180.0f, r = -angles[2] * kPi / 180.0f;
    const float cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p), cr = std::cos(r),
                sr = std::sin(r);
    // R = Rz(y) * Ry(p) * Rx(r), row-major map-space rotation
    float R[3][3] = {{cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr},
                     {sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr},
                     {-sp, cp * sr, cp * cr}};
    // C maps (x, y, z) -> (x, z, -y); in render space a = C R C^-1 with
    // C^-1 (X, Y, Z) = (X, -Z, Y). Render axis i takes map axis map[i] with sign sgn[i].
    const int map[3] = {0, 2, 1};
    const float sgn[3] = {1, 1, -1};
    float A[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) A[i][j] = sgn[i] * sgn[j] * R[map[i]][map[j]];
    const float t[3] = {origin[0] * s, origin[2] * s, -origin[1] * s};
    // column-major: m[col * 4 + row]
    return {A[0][0], A[1][0], A[2][0], 0, A[0][1], A[1][1], A[2][1], 0, A[0][2], A[1][2], A[2][2], 0,
            t[0], t[1], t[2], 1};
}

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
    for (const realms::GameEvent &ev : game_->takeEvents()) {
        if (ev.kind == realms::GameEvent::Kind::CenterPrint && !ev.text.empty()) {
            std::string t = ev.text;
            while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
            if (!t.empty()) messages_.push_back(t);
        }
    }
    if (messages_.size() > 16) messages_.erase(messages_.begin(), messages_.end() - 16);

    // Brush entity poses: each inline model drawn where its entity is, hidden
    // when no visible entity uses it (func_wall off, func_explosive gone...).
    int models = 0;
    for (int i = 1; i < g.numEdicts(); i++) {
        const q2edict_t *e = g.edict(i);
        if (e && e->inuse) models = std::max(models, g.inlineModel(e->s.modelindex));
    }
    poses_.assign(size_t(std::max(models, 0) + 1), DesktopRenderer::MapModelPose{});
    std::vector<bool> used(poses_.size(), false);
    for (int i = 1; i < g.numEdicts(); i++) {
        const q2edict_t *e = g.edict(i);
        if (!e || !e->inuse || (e->svflags & Q2_SVF_NOCLIENT)) continue;
        const int m = g.inlineModel(e->s.modelindex);
        if (m <= 0 || used[size_t(m)]) continue;
        used[size_t(m)] = true;
        poses_[size_t(m)].transform = entityTransform(e->s.origin, e->s.angles, session_.unitsPerMetre());
    }
    for (size_t m = 1; m < poses_.size(); m++) poses_[m].visible = used[m];
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
