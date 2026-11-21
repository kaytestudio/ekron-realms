// Realms map mode for the desktop client (see DesktopMapMode.h).
#include "DesktopMapMode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ekron::desktop {

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool has(const std::string &haystack, const char *needle) { return haystack.find(needle) != std::string::npos; }

struct Tint {
    float r, g, b;
};

Tint tintFor(const std::string &name, int layer, int32_t contents) {
    if (contents & REALMS_CONTENTS_LAVA) return {1.0f, 0.45f, 0.15f};
    if (contents & REALMS_CONTENTS_SLIME) return {0.45f, 0.78f, 0.35f};
    if (layer == 4) return {0.42f, 0.60f, 0.82f}; // water / warp
    // Small per-texture variation so neighbouring surfaces stay readable.
    uint32_t h = 2166136261u;
    for (char c : name) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    const float v = 0.80f + float(h % 17u) / 100.0f;
    const float warm = float((h >> 8) % 7u) / 200.0f;
    return {v + warm, v, v - warm};
}

} // namespace

int DesktopMapMode::materialLayer(const std::string &rawName, int32_t surfaceFlags, int32_t contents) {
    std::string name = rawName;
    for (char &c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
    if ((contents & REALMS_MASK_WATER) || (surfaceFlags & REALMS_SURF_WARP) || has(name, "water") ||
        has(name, "lava") || has(name, "slime"))
        return 4;
    if ((surfaceFlags & (REALMS_SURF_TRANS33 | REALMS_SURF_TRANS66)) || has(name, "glass") || has(name, "window"))
        return 7;
    if (has(name, "brick")) return 2;
    if (has(name, "metal") || has(name, "mtl") || has(name, "rust") || has(name, "tech") || has(name, "grate") ||
        has(name, "door") || has(name, "pipe") || has(name, "steel"))
        return 3;
    if (has(name, "wood") || has(name, "crate") || has(name, "plank")) return 5;
    if (has(name, "carpet") || has(name, "fabric") || has(name, "cloth") || has(name, "rug")) return 6;
    if (has(name, "road") || has(name, "asphalt") || has(name, "street") || has(name, "tarmac")) return 0;
    return 1; // concrete
}

void DesktopMapMode::toRenderVertices(const realms::MapGeometry &geometry, float unitsPerMetre,
                                      std::vector<sandbox::MeshVertex> &out) {
    out.clear();
    out.reserve(geometry.triangleCount() * 3);
    const float s = unitsPerMetre > 0.0f ? 1.0f / unitsPerMetre : 1.0f / kBspUnitsPerMetre;
    // Texels (BSP texinfo) or map units: one texture repeat per 64.
    constexpr float kUvScale = 1.0f / 64.0f;
    for (const auto &b : geometry.batches) {
        const int layer = materialLayer(b.material, b.surfaceFlags, b.contents);
        const Tint tint = tintFor(b.material, layer, b.contents);
        const size_t n = b.positions.size() / 3;
        for (size_t i = 0; i < n; ++i) {
            const float *p = &b.positions[i * 3];
            const float *nrm = &b.normals[i * 3];
            const float *uv = &b.uvs[i * 2];
            sandbox::MeshVertex v{};
            v.x = p[0] * s;
            v.y = p[2] * s;
            v.z = -p[1] * s;
            v.r = tint.r;
            v.g = tint.g;
            v.b = tint.b;
            v.nx = nrm[0];
            v.ny = nrm[2];
            v.nz = -nrm[1];
            v.u = uv[0] * kUvScale;
            v.v = uv[1] * kUvScale;
            v.layer = float(layer);
            out.push_back(v);
        }
    }
}

bool DesktopMapMode::open(realms::Engine &engine, ekron::gl::Api &gl, DesktopRenderer &renderer, std::string &error) {
    close(gl, renderer);
    error.clear();
    if (!engine.mapLoaded()) {
        error = "no map loaded";
        return false;
    }
    const bool mesh = !engine.meshMap().indices.empty();
    unitsPerMetre_ = kBspUnitsPerMetre;
    if (mesh) {
        const float scale = engine.cvarFloat("map_mesh_scale");
        if (std::isfinite(scale) && scale > 0.0f) unitsPerMetre_ = scale;
    }

    realms::MapGeometry geometry;
    if (!realms::loadMapGeometry(engine, geometry, error)) return false;

    // glTF maps: draw the textured file itself, in its own space.
    if (mesh) {
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

    std::vector<sandbox::MeshVertex> vertices;
    if (!model_) toRenderVertices(geometry, unitsPerMetre_, vertices);
    renderer.setMapGeometry(gl, vertices);

    walker_ = realms::MapWalker{};
    walker_.spawn(engine);
    path_ = engine.mapPath();
    const char *source = geometry.source == realms::MapGeometrySource::Faces     ? "BSP faces"
                         : geometry.source == realms::MapGeometrySource::Brushes ? "BSP brushes"
                         : geometry.source == realms::MapGeometrySource::Mesh    ? "glTF mesh"
                                                                                 : "no geometry";
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s, %zu triangles%s", source, geometry.triangleCount(),
                  model_ ? ", textured" : "");
    summary_ = buf;
    active_ = true;
    return true;
}

void DesktopMapMode::close(ekron::gl::Api &gl, DesktopRenderer &renderer) {
    if (model_) {
        model_->release(gl);
        model_.reset();
    }
    if (active_) renderer.clearMapGeometry(gl);
    active_ = false;
    path_.clear();
    summary_.clear();
}

void DesktopMapMode::update(const realms::Engine &engine, const realms::WalkInput &input, float renderYaw,
                            float renderPitch, float dt) {
    if (!active_) return;
    realms::WalkInput in = input;
    in.yaw = renderYaw * 180.0f / kPi + 90.0f;
    in.pitch = -renderPitch * 180.0f / kPi;
    walker_.move(engine, in, dt);
}

bool DesktopMapMode::respawn(const realms::Engine &engine) { return active_ && walker_.spawn(engine); }

physics::Vec3 DesktopMapMode::eye() const {
    const realms::Vec3 e = walker_.eye();
    const float s = 1.0f / unitsPerMetre_;
    return {e[0] * s, e[2] * s, -e[1] * s};
}

float DesktopMapMode::spawnRenderYaw() const { return (walker_.spawnYaw - 90.0f) * kPi / 180.0f; }

} // namespace ekron::desktop
