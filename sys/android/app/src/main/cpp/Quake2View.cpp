// Realms map mode with the Quake 2 stack (see Quake2View.h).
#include "Quake2View.h"

#include "MapEntityTransform.h"

#include <algorithm>
#include <map>

namespace mapview {

namespace {

// Faces whose material has a texture: white, coordinates over the texture's
// size (texinfo texels / width, height), mapTextureLayer.
void applyTextures(const realms::MapGeometry &g, const realms::MapTextures &textures,
                   std::vector<sandbox::MeshVertex> &vertices) {
    size_t at = 0;
    for (const auto &b : g.batches) {
        const size_t n = b.positions.size() / 3;
        const int index = textures.find(b.material);
        if (index >= 0 && b.uvs.size() == n * 2 && at + n <= vertices.size()) {
            const realms::MapTextures::Texture &t = textures.texture(size_t(index));
            for (size_t i = 0; i < n; i++) {
                sandbox::MeshVertex &v = vertices[at + i];
                v.u = b.uvs[i * 2] / float(t.width);
                v.v = b.uvs[i * 2 + 1] / float(t.height);
                v.r = v.g = v.b = 1.0f;
                v.layer = float(mapTextureLayer(index, int(v.layer + 0.5f)));
            }
        }
        at += n;
    }
}

} // namespace

realms::Vec3 Quake2View::cameraAngles(float renderYaw, float renderPitch) {
    const realms::WalkInput view = walkInput({}, renderYaw, renderPitch);
    return {view.pitch, view.yaw, 0.0f};
}

bool Quake2View::open(realms::Engine &engine, const Quake2ViewOptions &options, realms::MapGeometry &geometry,
                      std::string &error) {
    close();
    if (!session_.open(engine, geometry, error)) return false;
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
    messages_.clear();
    skyTime_ = 0.0;
    session_.close();
}

void Quake2View::triangles(const realms::MapGeometry &geometry, std::vector<sandbox::MeshVertex> &vertices,
                           std::vector<ModelRange> &ranges, std::vector<float> &lightUvs) const {
    vertices.clear();
    ranges.clear();
    lightUvs.clear();
    std::map<int32_t, realms::MapGeometry> byModel;
    for (const auto &b : geometry.batches) byModel[std::max(b.model, 0)].batches.push_back(b);
    std::vector<sandbox::MeshVertex> part;
    std::vector<float> uvs;
    bool lit = q2_.lightmapped();
    for (auto &[model, g] : byModel) {
        toRenderVertices(g, session_.unitsPerMetre(), part);
        if (part.empty()) continue;
        applyTextures(g, q2_.textures(), part);
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
        if (poses[m].visible)
            poses_[m].transform = entityTransform(poses[m].origin.data(), poses[m].angles.data(), session_.unitsPerMetre());
    }
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
    for (const realms::Md2Vertex &v : scene.vertices) {
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
