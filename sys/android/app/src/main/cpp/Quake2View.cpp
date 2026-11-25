// Realms map mode with the Quake 2 stack (see Quake2View.h).
#include "Quake2View.h"

#include "MapEntityTransform.h"

#include "realms/realms_bridge.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mapview {

namespace {

// The map's textures (mapTextureLayer, white, coordinates over the texture
// size), then the effect bits and animation slots of every face.
void applySurfaces(const realms::MapGeometry &g, const realms::MapTextures &textures,
                   std::vector<sandbox::MeshVertex> &vertices, std::vector<std::vector<int>> &animations,
                   bool &translucent) {
    size_t at = 0;
    for (const auto &b : g.batches) {
        const size_t n = b.positions.size() / 3;
        if (at + n > vertices.size()) break;
        const int effects = mapEffects(b.surfaceFlags);
        if (effects & (kMapEffectTrans33 | kMapEffectTrans66)) translucent = true;
        const int index = textures.find(b.material);
        int slot = 0;
        if (index >= 0 && b.uvs.size() == n * 2) {
            const realms::MapTextures::Texture &t = textures.texture(size_t(index));
            // warp faces: texels / 64 like EmitWaterPolys (toRenderVertices' scale)
            const bool warp = (effects & kMapEffectWarp) != 0;
            const float w = warp ? 64.0f : float(t.width), h = warp ? 64.0f : float(t.height);
            const int materialClass = n ? int(vertices[at].layer + 0.5f) & 7 : 0;
            // animated: every frame needs its texture, else the face stays still
            std::vector<int> frames;
            for (const std::string &name : b.frames) {
                const int f = textures.find(name);
                if (f < 0) {
                    frames.clear();
                    break;
                }
                frames.push_back(mapTextureLayer(f, materialClass));
            }
            if (frames.size() > 1) {
                const auto found = std::find(animations.begin(), animations.end(), frames);
                if (found != animations.end()) {
                    slot = int(found - animations.begin()) + 1;
                } else if (animations.size() < kMaxMapAnimations) {
                    animations.push_back(frames);
                    slot = int(animations.size());
                }
            }
            for (size_t i = 0; i < n; i++) {
                sandbox::MeshVertex &v = vertices[at + i];
                v.u = b.uvs[i * 2] / w;
                v.v = b.uvs[i * 2 + 1] / h;
                v.r = v.g = v.b = 1.0f;
                v.layer = float(mapTextureLayer(index, int(v.layer + 0.5f)));
            }
        }
        for (size_t i = 0; i < n; i++) {
            sandbox::MeshVertex &v = vertices[at + i];
            v.layer = float((int(v.layer + 0.5f) & kMapLayerMask) | effects << kMapEffectShift |
                            slot << kMapAnimationShift);
        }
        at += n;
    }
}

} // namespace

int mapEffects(int32_t surfaceFlags) {
    int effects = 0;
    if (surfaceFlags & REALMS_SURF_WARP) effects |= kMapEffectWarp;
    if (surfaceFlags & REALMS_SURF_FLOWING) effects |= kMapEffectFlowing;
    // R_DrawAlphaSurfaces: TRANS33 wins
    if (surfaceFlags & REALMS_SURF_TRANS33) effects |= kMapEffectTrans33;
    else if (surfaceFlags & REALMS_SURF_TRANS66) effects |= kMapEffectTrans66;
    return effects;
}

MapLayerParts mapLayerParts(float layer) {
    const int packed = int(layer + 0.5f);
    MapLayerParts out;
    out.layer = packed & kMapLayerMask;
    out.effects = (packed >> kMapEffectShift) & 15;
    out.animation = packed >> kMapAnimationShift;
    return out;
}

std::array<float, 4> mapSurfaceMotion(double seconds) {
    const double t = std::isfinite(seconds) ? std::max(seconds, 0.0) : 0.0;
    auto wrap = [](double v) { return float(v - std::floor(v)); };
    const double flow = -64.0 * (t / 40.0 - std::floor(t / 40.0));
    const double warpFlow = -(t * 0.5 - std::floor(t * 0.5));
    const double twoPi = 6.283185307179586;
    return {float(std::fmod(t, twoPi)), wrap(flow), wrap(warpFlow), 0.0f};
}

realms::Vec3 Quake2View::cameraAngles(float renderYaw, float renderPitch) const {
    const realms::WalkInput view = walkInput({}, renderYaw, renderPitch);
    return q2_.cameraAngles({view.pitch, view.yaw, 0.0f});
}

Quake2View::DrawAngles Quake2View::drawAngles(float renderYaw, float renderPitch) const {
    if (!q2_.gameRunning()) return {renderYaw, renderPitch, 0.0f};
    // walkInput backwards. A positive Quake 2 roll leans the camera right
    // (AngleVectors' right vector points down), which turns the image
    // counter-clockwise like a positive render roll.
    const realms::Vec3 a = cameraAngles(renderYaw, renderPitch);
    constexpr float d2r = 3.14159265358979f / 180.0f;
    return {renderYawFromMap(a[1]), -a[0] * d2r, a[2] * d2r};
}

bool Quake2View::open(realms::Engine &engine, const Quake2ViewOptions &options, realms::MapGeometry &geometry,
                      std::string &error) {
    close();
    if (!session_.open(engine, geometry, error)) return false;
    modelOrigins_.assign(1, {0.0f, 0.0f, 0.0f});
    const float metresPerUnit = 1.0f / session_.unitsPerMetre();
    nodeModels_.clear();
    for (const realms::MeshMapModel &m : engine.meshMap().models) {
        modelOrigins_.push_back({m.origin[0] * metresPerUnit, m.origin[2] * metresPerUnit, -m.origin[1] * metresPerUnit});
        nodeModels_[m.node] = int(modelOrigins_.size()) - 1;
    }
    realms::Quake2ModuleConfig config;
    config.executableDir = options.executableDir;
    config.startGame = options.startGame;
    std::string why;
    q2_.open(engine, geometry, config, why);
    if (q2_.gameRunning()) {
        session_.summary() += ", game";
        engine.print("map: Quake 2 game running (" + q2_.game()->moduleOrigin() + "); " +
                     std::to_string(q2_.game()->numEdicts()) + " edicts\n");
        q2_.mirrorPlayer(session_.walker());
        syncPoses();
    } else if (!why.empty()) {
        engine.print("map: walking without the game module: " + why + "\n");
    }
    return true;
}

void Quake2View::close() {
    q2_.close();
    poses_.clear();
    modelOrigins_.clear();
    nodeModels_.clear();
    animations_.clear();
    translucent_ = false;
    messages_.clear();
    skyTime_ = 0.0;
    session_.close();
}

void Quake2View::triangles(const realms::MapGeometry &geometry, std::vector<sandbox::MeshVertex> &vertices,
                           std::vector<ModelRange> &ranges, std::vector<float> &lightUvs) {
    vertices.clear();
    ranges.clear();
    lightUvs.clear();
    animations_.clear();
    translucent_ = false;
    std::map<int32_t, realms::MapGeometry> byModel;
    for (const auto &b : geometry.batches) byModel[std::max(b.model, 0)].batches.push_back(b);
    std::vector<sandbox::MeshVertex> part;
    std::vector<float> uvs;
    bool lit = q2_.lightmapped();
    for (auto &[model, g] : byModel) {
        toRenderVertices(g, session_.unitsPerMetre(), part);
        if (part.empty()) continue;
        applySurfaces(g, q2_.textures(), part, animations_, translucent_);
        ranges.push_back({model, int(vertices.size()), int(part.size())});
        vertices.insert(vertices.end(), part.begin(), part.end());
        if (lit && q2_.lightmapUvs(g, uvs) && uvs.size() == part.size() * 2)
            lightUvs.insert(lightUvs.end(), uvs.begin(), uvs.end());
        else
            lit = false;
    }
    if (!lit) lightUvs.clear();
    if (ranges.size() <= 1) ranges.clear();
}

void Quake2View::update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                        float dt, bool attack) {
    skyTime_ += double(dt);
    const std::string failure = q2_.takeGameFailure(); // the game stopped (game error): back to walking
    if (!failure.empty()) {
        messages_.push_back("Game stopped: " + failure);
        poses_.clear();
    }
    if (!q2_.gameRunning()) {
        session_.update(engine, input, renderYaw, renderPitch, dt);
        return;
    }
    // Quake 2 client: always-run speeds and the camera's absolute angles.
    q2_.frame(realms::Quake2Module::userCmd(walkInput(input, renderYaw, renderPitch), attack), dt);
    q2_.mirrorPlayer(session_.walker()); // HUD, sound director, retained state
    syncPoses();
}

void Quake2View::syncPoses() {
    const std::vector<realms::Quake2ModelPose> &poses = q2_.modelPoses();
    poses_.assign(poses.size(), ModelPose{});
    for (size_t m = 1; m < poses.size(); m++) {
        poses_[m].visible = poses[m].visible;
        poses_[m].frame = poses[m].frame;
        if (!poses[m].visible) continue;
        poses_[m].transform = entityTransform(poses[m].origin.data(), poses[m].angles.data(), session_.unitsPerMetre());
        poses_[m].placed = m < modelOrigins_.size() ? mapview::placedTransform(poses_[m].transform, modelOrigins_[m].data())
                                                    : poses_[m].transform;
    }
}

void Quake2View::animationLayers(int model, std::vector<int> &layers) const {
    layers.resize(animations_.size());
    // r_worldentity.frame = time * 2; brush models: currententity->frame
    long long frame = 0;
    if (model <= 0) frame = static_cast<long long>(skyTime_ * 2.0);
    else if (size_t(model) < poses_.size()) frame = poses_[size_t(model)].frame;
    for (size_t s = 0; s < animations_.size(); s++) {
        const long long n = static_cast<long long>(animations_[s].size());
        layers[s] = animations_[s][size_t(((frame % n) + n) % n)];
    }
}

bool Quake2View::placedTransform(int model, std::array<float, 16> &out) const {
    out = ModelPose{}.placed;
    if (model <= 0 || size_t(model) >= poses_.size()) return true;
    if (!poses_[size_t(model)].visible) return false;
    out = poses_[size_t(model)].placed;
    return true;
}

bool Quake2View::respawn(realms::Engine &engine) {
    if (!session_.active()) return false;
    if (q2_.gameRunning()) {
        std::string why;
        if (q2_.restartGame(engine, why)) {
            q2_.mirrorPlayer(session_.walker());
            syncPoses();
            return true;
        }
        engine.print("map: game restart failed: " + why + "\n");
        poses_.clear();
    }
    return session_.respawn(engine);
}

void Quake2View::toggleNoclip() {
    if (q2_.gameRunning()) q2_.toggleNoclip();
    else session_.toggleNoclip();
}

bool Quake2View::noclip() const { return q2_.gameRunning() ? q2_.noclip() : session_.walker().noclip; }

physics::Vec3 Quake2View::eye() const {
    if (q2_.gameRunning()) return toRender(q2_.viewOrigin(), session_.unitsPerMetre());
    return session_.eye();
}

float Quake2View::spawnRenderYaw() const {
    if (q2_.gameRunning()) return renderYawFromMap(q2_.viewAngles()[1]);
    return session_.spawnRenderYaw();
}

std::vector<std::string> Quake2View::takeMessages() {
    std::vector<std::string> out;
    out.swap(messages_);
    for (std::string &m : q2_.takeMessages()) out.push_back(std::move(m));
    return out;
}

void Quake2View::entities(realms::Engine &engine, float renderYaw, float renderPitch, bool viewWeapon,
                          std::vector<EntityVertex> &vertices, std::vector<EntityBatch> &batches) {
    vertices.clear();
    batches.clear();
    if (!q2_.gameRunning()) return;
    const realms::GameScene &scene =
        q2_.buildScene(engine, q2_.viewOrigin(), cameraAngles(renderYaw, renderPitch), viewWeapon);
    // Model space (Quake 2 units, x forward, y left, z up) to the render
    // axes and metres, like the map: (x, z, -y) / upm. entityTransform then
    // rotates and places them (a proper rotation: the winding is kept).
    const float upm = session_.unitsPerMetre();
    vertices.reserve(scene.vertices.size());
    for (const realms::ModelVertex &v : scene.vertices) {
        const auto r = toRenderModelVertex(v, 1.0f / upm);
        vertices.push_back({r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]});
    }
    batches.reserve(scene.batches.size());
    for (const realms::GameDrawBatch &b : scene.batches) {
        EntityBatch out;
        out.first = b.first;
        out.count = b.count;
        out.skin = b.skin;
        out.color = b.color;
        out.transform = entityTransform(b.origin.data(), b.angles.data(), upm);
        out.viewWeapon = b.viewWeapon;
        out.fallbackBox = b.fallbackBox;
        out.fullbright = b.fullbright;
        out.lit = b.lit;
        out.light = b.light;
        batches.push_back(std::move(out));
    }
}

void Quake2View::effects(float renderYaw, float renderPitch, size_t maxLights, std::vector<EffectVertex> &vertices,
                         std::vector<DynamicLight> &lights) {
    vertices.clear();
    lights.clear();
    if (!q2_.gameRunning()) return;
    const float upm = session_.unitsPerMetre();
    q2_.buildEffects(q2_.viewOrigin(), cameraAngles(renderYaw, renderPitch), maxLights, fxQuads_, fxLights_);
    vertices.reserve(fxQuads_.size());
    for (const realms::Quake2EffectVertex &q : fxQuads_) {
        const physics::Vec3 r = toRender(realms::Vec3{q.x, q.y, q.z}, upm);
        vertices.push_back({r.x, r.y, r.z, q.r, q.g, q.b, q.a, q.u, q.v, q.shape});
    }
    lights.reserve(fxLights_.size());
    for (const realms::GameDynamicLight &l : fxLights_) {
        const physics::Vec3 r = toRender(realms::Vec3{l.origin[0], l.origin[1], l.origin[2]}, upm);
        lights.push_back({r.x, r.y, r.z, l.radius / upm, l.color[0], l.color[1], l.color[2]});
    }
}

} // namespace mapview
