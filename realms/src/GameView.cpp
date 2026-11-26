// Realms Engine - client-side interpolation of game frames (see GameView.h).
#include "realms/GameView.h"
#include "realms/MapSky.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <tuple>
#include <utility>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace realms {

float GameEntityLerp::lerpAngle(float a2, float a1, float frac) {
    // q_shared.c LerpAngle: a2 is the old angle, a1 the new one
    if (a1 - a2 > 180) a1 -= 360;
    if (a1 - a2 < -180) a1 += 360;
    return a2 + frac * (a1 - a2);
}

void GameEntityLerp::reset() {
    serverFrame_ = -1;
    snapshots_ = 0;
    current_.clear();
    prev_.clear();
    cur_.clear();
    gunIndex_ = gunFrame_ = prevGunIndex_ = prevGunFrame_ = 0;
    viewPrev_ = viewCur_ = ViewSnap{};
}

GameEntityLerp::ViewSnap GameEntityLerp::viewSnap(const GameClient &client) {
    ViewSnap s;
    const q2player_state_t *ps = client.playerState();
    if (!client.running() || !ps) return s;
    s.valid = true;
    s.pmType = ps->pmove.pm_type;
    const Vec3 origin = client.playerOrigin();
    for (int k = 0; k < 3; k++) {
        s.origin[size_t(k)] = origin[size_t(k)];
        s.offset[size_t(k)] = ps->viewoffset[k];
        s.kick[size_t(k)] = ps->kick_angles[k];
        s.angles[size_t(k)] = ps->viewangles[k];
        s.gunOffset[size_t(k)] = ps->gunoffset[k];
        s.gunAngles[size_t(k)] = ps->gunangles[k];
    }
    return s;
}

void GameEntityLerp::update(const GameClient &client) {
    if (!client.running()) {
        reset();
        return;
    }
    if (client.frameNumber() == serverFrame_) return;
    // A restarted server counts frames from 0 again: drop the old history.
    if (client.frameNumber() < serverFrame_) reset();
    serverFrame_ = client.frameNumber();
    snapshots_++;
    current_ = client.renderEntities(true);
    prev_ = std::move(cur_);
    cur_.clear();
    for (const GameRenderEntity &e : current_) {
        Snap s;
        s.modelindex = e.modelindex;
        s.frame = e.frame;
        s.origin = e.origin;
        s.angles = e.angles;
        cur_[e.number] = s;
    }
    prevGunIndex_ = gunIndex_;
    prevGunFrame_ = gunFrame_;
    const q2player_state_t *ps = client.playerState();
    gunIndex_ = ps ? ps->gunindex : 0;
    gunFrame_ = ps ? ps->gunframe : 0;
    viewPrev_ = viewCur_;
    viewCur_ = viewSnap(client);
}

GameViewLerp GameEntityLerp::view(const GameClient &client) const {
    GameViewLerp v;
    // the snapshots of the last two frames, or the live state before update()
    const ViewSnap cur = viewCur_.valid ? viewCur_ : viewSnap(client);
    if (!client.running() || !cur.valid) return v;
    ViewSnap old = viewPrev_.valid ? viewPrev_ : cur;
    // CL_CalcViewValues: no interpolation across a teleport (256 units)
    for (int k = 0; k < 3; k++)
        if (std::fabs(cur.origin[size_t(k)] - old.origin[size_t(k)]) > 256.0f) old = cur;
    const float frac = float(client.lerpFraction());
    for (size_t k = 0; k < 3; k++) {
        v.offset[k] = old.offset[k] + frac * (cur.offset[k] - old.offset[k]);
        v.kick[k] = lerpAngle(old.kick[k], cur.kick[k], frac);
        v.angles[k] = lerpAngle(old.angles[k], cur.angles[k], frac);
    }
    v.locked = cur.pmType >= Q2_PM_DEAD;
    return v;
}

std::vector<GameLerpedEntity> GameEntityLerp::entities(const GameClient &client) const {
    std::vector<GameLerpedEntity> out;
    if (!client.running()) return out;
    const float frac = float(client.lerpFraction());
    // cl.time in ms at the drawn moment: the previous frame + frac
    const double clientTime = (double(serverFrame_) - 1.0 + double(frac)) * GameServer::kFrameSeconds;
    const float autorotate = float(std::fmod(clientTime * 100.0, 360.0));
    const int autoanim = int(clientTime * 2.0); // 2 Hz
    out.reserve(current_.size());
    for (const GameRenderEntity &e : current_) {
        GameLerpedEntity l;
        l.entity = &e;
        l.frame = e.frame;
        l.oldframe = e.frame;
        l.origin = e.origin;
        l.angles = e.angles;
        const auto it = prev_.find(e.number);
        bool lerp = it != prev_.end() && it->second.modelindex == e.modelindex;
        if (lerp) {
            const Snap &p = it->second;
            float d2 = 0;
            for (int k = 0; k < 3; k++) {
                const float d = e.origin[size_t(k)] - p.origin[size_t(k)];
                d2 += d * d;
            }
            if (d2 > 512.0f * 512.0f) lerp = false; // teleported
        }
        if (lerp) {
            const Snap &p = it->second;
            for (int k = 0; k < 3; k++) {
                l.origin[size_t(k)] = p.origin[size_t(k)] + frac * (e.origin[size_t(k)] - p.origin[size_t(k)]);
                l.angles[size_t(k)] = lerpAngle(p.angles[size_t(k)], e.angles[size_t(k)], frac);
            }
            l.oldframe = p.frame;
            l.backlerp = 1.0f - frac;
        }
        // cl_ents.c: client-side frame animation effects
        if (e.effects & Q2_EF_ANIM_ALL) {
            l.frame = l.oldframe = autoanim;
            l.backlerp = 0;
        } else if (e.effects & Q2_EF_ANIM_ALLFAST) {
            l.frame = l.oldframe = int(clientTime * 10.0);
            l.backlerp = 0;
        } else if (e.effects & Q2_EF_ANIM01) {
            l.frame = l.oldframe = e.frame + (autoanim & 1);
            l.backlerp = 0;
        } else if (e.effects & Q2_EF_ANIM23) {
            l.frame = l.oldframe = e.frame + 2 + (autoanim & 1);
            l.backlerp = 0;
        }
        if (e.effects & Q2_EF_ROTATE) l.angles = {0.0f, autorotate, 0.0f};
        out.push_back(l);
    }
    return out;
}

GameViewWeapon GameEntityLerp::viewWeapon(const GameClient &client) const {
    GameViewWeapon w;
    const q2player_state_t *ps = client.playerState();
    if (!client.running() || !ps || ps->gunindex <= 0) return w;
    w.model = client.modelName(ps->gunindex);
    if (w.model.empty() || w.model[0] == '*') return w;
    w.visible = true;
    const Vec3 eye = client.viewOrigin();
    const Vec3 view = client.viewAngles();
    // CL_AddViewWeapon: gunoffset / gunangles between the last two frames
    const ViewSnap cur = viewCur_.valid ? viewCur_ : viewSnap(client);
    const ViewSnap old = viewPrev_.valid ? viewPrev_ : cur;
    const float frac = float(client.lerpFraction());
    for (size_t k = 0; k < 3; k++) {
        w.gunOffset[k] = old.gunOffset[k] + frac * (cur.gunOffset[k] - old.gunOffset[k]);
        w.gunAngles[k] = lerpAngle(old.gunAngles[k], cur.gunAngles[k], frac);
        w.origin[k] = eye[k] + w.gunOffset[k];
        w.angles[k] = view[k] + w.gunAngles[k];
    }
    w.frame = ps->gunframe;
    w.oldframe = ps->gunframe;
    if (prevGunIndex_ == ps->gunindex && gunIndex_ == ps->gunindex) {
        w.oldframe = prevGunFrame_;
        w.backlerp = 1.0f - float(client.lerpFraction());
    }
    return w;
}

// ------------------------------------------------------------ models

const Md3Model *GameModelCache::model(const Engine &engine, const std::string &name) {
    auto it = models_.find(name);
    if (it != models_.end()) return it->second.model.get();
    Entry &entry = models_[name];
    const std::string path = md3PathFor(name);
    std::vector<unsigned char> bytes;
    if (!engine.loadFile(path, bytes)) {
        errors_.push_back(path + ": not found");
        return nullptr;
    }
    auto m = std::make_unique<Md3Model>();
    std::string error;
    if (!m->parse(bytes, error)) {
        errors_.push_back(path + ": " + error);
        return nullptr;
    }
    entry.model = std::move(m);
    return entry.model.get();
}

bool GameModelCache::loadSkin(const Engine &engine, const std::string &name, RgbaImage &out, std::string &error) {
    out = RgbaImage{};
    if (name.empty()) {
        error = "not found";
        return false;
    }
    std::string ext = name.size() >= 4 ? name.substr(name.size() - 4) : std::string();
    for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool known = ext == ".tga" || ext == ".pcx";
    const std::string base = known ? name.substr(0, name.size() - 4) : name;
    // the named file first, then the other format
    const std::string candidates[2] = {known ? name : name + ".tga",
                                       known ? base + (ext == ".tga" ? ".pcx" : ".tga") : name + ".pcx"};
    std::vector<unsigned char> bytes;
    for (const std::string &path : candidates) {
        if (!engine.loadFile(path, bytes)) continue;
        std::string lower = path.substr(path.size() - 4);
        for (char &c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lower == ".tga") return decodeTga(bytes.data(), bytes.size(), out, error);
        return decodePcx(bytes, out, error);
    }
    error = "not found";
    return false;
}

size_t GameModelCache::loaded() const {
    size_t n = 0;
    for (const auto &kv : models_) n += kv.second.model ? 1 : 0;
    return n;
}

size_t GameModelCache::missing() const { return models_.size() - loaded(); }

std::vector<std::string> GameModelCache::takeErrors() {
    std::vector<std::string> out;
    out.swap(errors_);
    return out;
}

// ------------------------------------------------------------ scene

namespace {

// 12 triangles, counter-clockwise seen from outside, flat normals.
void appendBox(const std::array<float, 3> &mn, const std::array<float, 3> &mx, std::vector<ModelVertex> &out) {
    static const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
    static const float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}};
    const float c[8][3] = {{mn[0], mn[1], mn[2]}, {mx[0], mn[1], mn[2]}, {mx[0], mx[1], mn[2]}, {mn[0], mx[1], mn[2]},
                           {mn[0], mn[1], mx[2]}, {mx[0], mn[1], mx[2]}, {mx[0], mx[1], mx[2]}, {mn[0], mx[1], mx[2]}};
    static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int f = 0; f < 6; f++) {
        for (int k : {0, 1, 2, 0, 2, 3}) {
            const float *p = c[faces[f][k]];
            out.push_back({p[0], p[1], p[2], normals[f][0], normals[f][1], normals[f][2], uv[k][0], uv[k][1]});
        }
    }
}

std::array<float, 4> fallbackColor(const GameRenderEntity &e, bool projectile) {
    if (e.svflags & Q2_SVF_DEADMONSTER) return {0.38f, 0.12f, 0.1f, 1.0f};
    if (e.svflags & Q2_SVF_MONSTER) return {0.78f, 0.22f, 0.16f, 1.0f};
    if (projectile) return {1.0f, 0.62f, 0.2f, 1.0f};
    if (e.solid == Q2_SOLID_TRIGGER) return {0.95f, 0.78f, 0.22f, 1.0f}; // items, pickups
    return {0.62f, 0.62f, 0.66f, 1.0f};
}

// RF_SHELL_* / RF_TRANSLUCENT, the way gl_mesh.c colours and fades models;
// cl_ents.c turns EF_QUAD / EF_PENT into shells and sets the alpha of
// EF_BFG / EF_PLASMA / EF_SPHERETRANS models.
std::array<float, 4> modelColor(int renderfx, uint32_t effects = 0) {
    if (effects & Q2_EF_QUAD) renderfx |= Q2_RF_SHELL_BLUE;
    if (effects & Q2_EF_PENT) renderfx |= Q2_RF_SHELL_RED;
    std::array<float, 4> c{1, 1, 1, 1};
    if (renderfx & (Q2_RF_SHELL_RED | Q2_RF_SHELL_GREEN | Q2_RF_SHELL_BLUE)) {
        c = {(renderfx & Q2_RF_SHELL_RED) ? 1.0f : 0.35f, (renderfx & Q2_RF_SHELL_GREEN) ? 1.0f : 0.35f,
             (renderfx & Q2_RF_SHELL_BLUE) ? 1.0f : 0.35f, 1.0f};
    }
    if (renderfx & Q2_RF_TRANSLUCENT) c[3] = 0.3f;
    if (effects & (Q2_EF_BFG | Q2_EF_SPHERETRANS)) c[3] = 0.3f;
    if (effects & Q2_EF_PLASMA) c[3] = 0.6f;
    return c;
}

bool endsWithSp2(const std::string &name) {
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".sp2";
}

} // namespace

void appendModelBatches(const Md3Model &model, const GameDrawBatch &base, int frame, int oldframe, float backlerp,
                        int skinnum, GameScene &scene) {
    for (int s = 0; s < model.numSurfaces(); s++) {
        GameDrawBatch b = base;
        b.first = int(scene.vertices.size());
        model.buildVertices(s, frame, oldframe, backlerp, scene.vertices);
        b.count = int(scene.vertices.size()) - b.first;
        b.skin = model.skin(s, skinnum);
        if (b.count > 0) scene.batches.push_back(std::move(b));
    }
}

void mirrorModelVertices(std::vector<ModelVertex> &vertices, size_t first) {
    // model +Y is the gun's left: y -> -y mirrors it to the other side, as
    // ref_gl's glScalef(-1, 1, 1) on the weapon's projection does on screen;
    // swapping two corners keeps the triangles' winding
    for (size_t i = first; i < vertices.size(); i++) {
        vertices[i].y = -vertices[i].y;
        vertices[i].ny = -vertices[i].ny;
    }
    for (size_t i = first; i + 2 < vertices.size(); i += 3) std::swap(vertices[i + 1], vertices[i + 2]);
}

std::vector<std::string> gameWeaponModels(const GameClient &client) {
    constexpr size_t kMaxClientWeaponModels = 20; // MAX_CLIENTWEAPONMODELS
    std::vector<std::string> out{"weapon.md2"};
    for (int i = 1; i < Q2_MAX_MODELS; i++) {
        const std::string name = client.modelName(i);
        if (name.empty()) break;
        if (name[0] == '#' && out.size() < kMaxClientWeaponModels) out.push_back(name.substr(1));
    }
    return out;
}

std::vector<std::string> gameWeaponModelFiles(const std::vector<std::string> &weaponModels, const std::string &model,
                                              int skinnum, bool vwep) {
    constexpr int kMaxClientWeaponModels = 20;
    int i = skinnum >> 8; // 0 is the default weapon model
    if (!vwep || i < 0 || i > kMaxClientWeaponModels - 1) i = 0;
    std::vector<std::string> files;
    auto add = [&files](const std::string &f) {
        if (std::find(files.begin(), files.end(), f) == files.end()) files.push_back(f);
    };
    const bool cyborg = model == "cyborg";
    for (int w : {i, 0}) {
        if (w >= int(weaponModels.size())) continue; // never loaded: no model
        add("players/" + model + "/" + weaponModels[size_t(w)]);
        if (cyborg) add("players/male/" + weaponModels[size_t(w)]);
    }
    add("players/male/weapon.md2"); // cl.baseclientinfo
    return files;
}

void buildGameScene(const Engine &engine, const GameClient &client, const GameEntityLerp &lerp,
                    GameModelCache &models, const GameSceneOptions &options, GameScene &out) {
    out.clear();
    if (!client.running()) return;
    std::vector<std::string> weaponModels; // filled on the first player weapon
    auto firstModel = [&](const std::vector<std::string> &files) -> std::pair<const Md3Model *, std::string> {
        for (const std::string &f : files)
            if (const Md3Model *m = models.model(engine, f)) return {m, f};
        return {nullptr, std::string()};
    };
    // cl_entities.c: "duplicate for linked models", in the entity's pose
    // with no skin, shell or translucency of its own
    auto addLinked = [&](const GameLerpedEntity &l) {
        const GameRenderEntity *e = l.entity;
        const int linked[3] = {e->modelindex2, e->modelindex3, e->modelindex4};
        for (int k = 0; k < 3; k++) {
            const int index = linked[k];
            if (index <= 0) continue;
            const Md3Model *md3 = nullptr;
            std::string name;
            if (k == 0 && index == 255) {
                // CL_LoadClientinfo: the body model, or male without one
                std::string model = GameServer::playerModelOf(client.configString(Q2_CS_PLAYERSKINS + (e->skinnum & 0xff)));
                if (model != "male" && !models.model(engine, "players/" + model + "/tris.md2")) model = "male";
                if (weaponModels.empty()) weaponModels = gameWeaponModels(client);
                std::tie(md3, name) = firstModel(gameWeaponModelFiles(weaponModels, model, e->skinnum, options.vwep));
            } else {
                name = client.modelName(index);
                if (!name.empty() && name[0] != '#' && isAliasModelName(name)) md3 = models.model(engine, name);
            }
            if (!md3) continue;
            GameDrawBatch b;
            b.entity = e->number;
            b.model = name;
            b.origin = l.origin;
            b.angles = {-l.angles[0], l.angles[1], l.angles[2]};
            b.color = modelColor(0);
            // the defender sphere's shell (Rogue)
            std::string lower = k == 0 ? client.modelName(index) : std::string();
            for (char &c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "models/items/shell/tris.md2") b.color[3] = 0.32f;
            appendModelBatches(*md3, b, l.frame, l.oldframe, l.backlerp, 0, out);
        }
    };
    for (const GameLerpedEntity &l : lerp.entities(client)) {
        const GameRenderEntity *e = l.entity;
        if (!e || e->inlineModel > 0 || endsWithSp2(e->model)) continue;
        const Md3Model *md3 = isAliasModelName(e->model) ? models.model(engine, e->model) : nullptr;
        // CL_LoadClientinfo: a player model that isn't there is drawn as male
        static const std::string kMale = "players/male/tris.md2";
        if (!md3 && e->model.rfind("players/", 0) == 0 && e->model != kMale) md3 = models.model(engine, kMale);
        GameDrawBatch b;
        b.entity = e->number;
        b.model = e->model;
        b.origin = l.origin;
        if (md3) {
            b.color = modelColor(e->renderfx, e->effects);
            b.fullbright = (e->renderfx & Q2_RF_FULLBRIGHT) != 0;
            b.angles = {-l.angles[0], l.angles[1], l.angles[2]}; // alias models: pitch negated
            appendModelBatches(*md3, b, l.frame, l.oldframe, l.backlerp, e->skinnum, out);
            addLinked(l);
            continue;
        }
        b.first = int(out.vertices.size());
        if (options.fallbackBoxes) {
            std::array<float, 3> mn = e->mins, mx = e->maxs;
            const bool degenerate = mx[0] - mn[0] < 1 || mx[1] - mn[1] < 1 || mx[2] - mn[2] < 1;
            if (degenerate) { // projectiles, gibs, effects: a small cube
                mn = {-4, -4, -4};
                mx = {4, 4, 4};
            }
            appendBox(mn, mx, out.vertices);
            b.color = fallbackColor(*e, degenerate);
            b.fallbackBox = true; // axis-aligned like the bounds: no rotation
        }
        b.count = int(out.vertices.size()) - b.first;
        if (b.count > 0) out.batches.push_back(std::move(b));
        addLinked(l);
    }
    if (options.viewWeapon && options.hand != 2) { // R_DrawAliasModel: no gun with r_lefthand 2
        const GameViewWeapon w = lerp.viewWeapon(client);
        const q2player_state_t *ps = client.playerState();
        const Md3Model *md3 = w.visible && isAliasModelName(w.model) ? models.model(engine, w.model) : nullptr;
        if (md3 && ps) {
            GameDrawBatch b;
            b.viewWeapon = true;
            b.model = w.model;
            b.origin = w.origin;
            b.angles = w.angles;
            if (options.hasCamera) {
                for (int k = 0; k < 3; k++) {
                    b.origin[size_t(k)] = options.cameraOrigin[size_t(k)] + w.gunOffset[size_t(k)];
                    b.angles[size_t(k)] = options.cameraAngles[size_t(k)] + w.gunAngles[size_t(k)];
                }
            }
            b.angles[0] = -b.angles[0];
            b.color = modelColor(0);
            const size_t first = out.vertices.size();
            appendModelBatches(*md3, b, w.frame, w.oldframe, w.backlerp, 0, out);
            if (options.hand == 1) mirrorModelVertices(out.vertices, first);
        }
    }
}

} // namespace realms
