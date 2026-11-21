// Realms map mode, renderer independent (see MapSession.h).
#include "MapSession.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace mapview {

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

int materialLayer(const std::string &rawName, int32_t surfaceFlags, int32_t contents) {
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

void toRenderVertices(const realms::MapGeometry &geometry, float unitsPerMetre,
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

realms::WalkInput walkInput(const realms::WalkInput &input, float renderYaw, float renderPitch) {
    // Render forward is (-sin yaw, -cos yaw) in (x, z); map forward is
    // (cos a, sin a) in (x, y) with y = -z: a = yaw + 90 degrees. Quake 2
    // pitch is positive looking down.
    realms::WalkInput in = input;
    in.yaw = renderYaw * 180.0f / kPi + 90.0f;
    in.pitch = -renderPitch * 180.0f / kPi;
    return in;
}

physics::Vec3 toRender(const realms::Vec3 &p, float unitsPerMetre) {
    const float s = unitsPerMetre > 0.0f ? 1.0f / unitsPerMetre : 1.0f / kBspUnitsPerMetre;
    return {p[0] * s, p[2] * s, -p[1] * s};
}

float renderYawFromMap(float mapYawDegrees) { return (mapYawDegrees - 90.0f) * kPi / 180.0f; }

bool safeMapName(const std::string &name) {
    if (name.empty() || name.size() > 240 || name[0] == '/' || name.find("..") != std::string::npos) return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/' || c == ' '))
            return false;
    }
    return true;
}

physics::Vec3 toLevelScene(const physics::Vec3 &source, float sourceScale, const std::array<float, 3> &sourceOffset) {
    return {(source.x - sourceOffset[0]) * sourceScale, (source.y - sourceOffset[1]) * sourceScale,
            (source.z - sourceOffset[2]) * sourceScale};
}

bool MapSession::open(realms::Engine &engine, realms::MapGeometry &geometry, std::string &error) {
    close();
    error.clear();
    if (!engine.mapLoaded()) {
        error = "no map loaded";
        return false;
    }
    mesh_ = !engine.meshMap().indices.empty();
    unitsPerMetre_ = kBspUnitsPerMetre;
    if (mesh_) {
        const float scale = engine.cvarFloat("map_mesh_scale");
        if (std::isfinite(scale) && scale > 0.0f) unitsPerMetre_ = scale;
    }
    if (!realms::loadMapGeometry(engine, geometry, error)) return false;

    walker_ = realms::MapWalker{};
    walker_.spawn(engine);
    path_ = engine.mapPath();
    const char *source = geometry.source == realms::MapGeometrySource::Faces     ? "BSP faces"
                         : geometry.source == realms::MapGeometrySource::Brushes ? "BSP brushes"
                         : geometry.source == realms::MapGeometrySource::Mesh    ? "glTF mesh"
                                                                                 : "no geometry";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %zu triangles", source, geometry.triangleCount());
    summary_ = buf;
    active_ = true;
    return true;
}

void MapSession::close() {
    active_ = false;
    mesh_ = false;
    path_.clear();
    summary_.clear();
}

void MapSession::update(const realms::Engine &engine, const realms::WalkInput &input, float renderYaw,
                        float renderPitch, float dt) {
    if (active_) walker_.move(engine, walkInput(input, renderYaw, renderPitch), dt);
}

bool MapSession::respawn(const realms::Engine &engine) { return active_ && walker_.spawn(engine); }

void MapSession::toggleNoclip() {
    walker_.noclip = !walker_.noclip;
    walker_.velocity = {0.0f, 0.0f, 0.0f};
}

} // namespace mapview
