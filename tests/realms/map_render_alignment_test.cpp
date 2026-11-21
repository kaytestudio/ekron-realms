// glTF maps are drawn and collided from two independent importers:
//   * realms::gltfToMeshMap (collision, handed to the Pascal / C++ core)
//   * city::Scene with keepSourceSpace (the textured model the desktop draws)
// The desktop maps core space to render space as render = (x, z, -y) / scale.
// This test proves both importers agree under that mapping - on the
// synthetic room and on every real .glb asset shipped with the game - so the
// player never walks on an invisible floor or through a visible wall.
// Android draws the sandbox's already loaded city (a "level" scene: rescaled
// and recentred) when the map is that file, through Scene::sourceScale /
// sourceOffset; that relation is checked on every asset too, along with
// mapview::MapSession's space and camera conversions.
//
//   realms_map_render_alignment_test [assets/models]
#include "GltfScene.h"
#include "MapSession.h"
#include "gltf_builder.h"

#include "realms/MeshMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        ++gChecks;                                                                 \
        if (!(cond)) {                                                             \
            ++gFailures;                                                           \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                          \
    } while (0)

constexpr float kScale = 32.0f;

bool loadScene(const std::string &path, const std::string &bytes, city::Scene &scene, std::string &log) {
    city::LoadOptions options;
    options.level = false;
    options.buildPhysics = false;
    options.keepSourceSpace = true;
    return scene.load(path,
                      [&](const std::string &p, std::vector<unsigned char> &out) {
                          if (p != path) return false;
                          out.assign(bytes.begin(), bytes.end());
                          return true;
                      },
                      log, options);
}

bool convert(const std::string &path, const std::string &bytes, realms::MeshMap &map, std::string &error) {
    realms::GltfMapOptions options;
    options.scale = kScale;
    return realms::gltfToMeshMap(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), path, {},
                                 options, map, error);
}

// Render-space bounds of the collision vertices.
void meshBounds(const realms::MeshMap &map, float lo[3], float hi[3]) {
    for (int k = 0; k < 3; ++k) {
        lo[k] = 1e30f;
        hi[k] = -1e30f;
    }
    for (size_t i = 0; i + 2 < map.vertices.size(); i += 3) {
        const float r[3] = {map.vertices[i] / kScale, map.vertices[i + 2] / kScale, -map.vertices[i + 1] / kScale};
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], r[k]);
            hi[k] = std::max(hi[k], r[k]);
        }
    }
}

void checkRoom() {
    const std::string room = gltf_builder::roomGlb();
    realms::MeshMap map;
    std::string error, log;
    city::Scene scene;
    CHECK(convert("room.glb", room, map, error));
    CHECK(loadScene("room.glb", room, scene, log));
    size_t sceneVertices = 0, matched = 0;
    for (const auto &batch : scene.batches) {
        for (const auto &v : batch.vertices) {
            ++sceneVertices;
            // Every drawn vertex is a collision vertex (exactly, in map units).
            const float m[3] = {v.x * kScale, -v.z * kScale, v.y * kScale};
            for (size_t i = 0; i + 2 < map.vertices.size(); i += 3) {
                if (map.vertices[i] == m[0] && map.vertices[i + 1] == m[1] && map.vertices[i + 2] == m[2]) {
                    ++matched;
                    break;
                }
            }
        }
    }
    CHECK(sceneVertices == 24 && matched == sceneVertices); // 8 triangles
    float lo[3], hi[3];
    meshBounds(map, lo, hi);
    for (int k = 0; k < 3; ++k) {
        CHECK(lo[k] == scene.minimum[size_t(k)] && hi[k] == scene.maximum[size_t(k)]);
        CHECK(map.sourceMins[size_t(k)] == scene.minimum[size_t(k)] && map.sourceMaxs[size_t(k)] == scene.maximum[size_t(k)]);
    }
    // The floor (glTF y = 0) is the collision floor (map z = 0).
    CHECK(scene.minimum[1] == -1.0f && map.vertices[2] == 0.0f);
}

void checkAsset(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty());
    const std::string name = file.filename().string();
    realms::MeshMap map;
    std::string error, log;
    city::Scene scene;
    const bool converted = convert(name, bytes, map, error);
    const bool loaded = loadScene(name, bytes, scene, log);
    CHECK(converted);
    CHECK(loaded);
    if (!converted || !loaded) {
        std::fprintf(stderr, "  %s: realms: %s / scene: %s\n", name.c_str(), error.c_str(), log.c_str());
        return;
    }
    float lo[3], hi[3];
    meshBounds(map, lo, hi);
    float extent = 0.0f;
    for (int k = 0; k < 3; ++k) extent = std::max(extent, scene.maximum[size_t(k)] - scene.minimum[size_t(k)]);
    const float tol = std::max(1e-4f, extent * 1e-5f);
    bool aligned = true;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(lo[k] - scene.minimum[size_t(k)]) > tol || std::fabs(hi[k] - scene.maximum[size_t(k)]) > tol)
            aligned = false;
    }
    CHECK(aligned);
    size_t drawn = 0;
    for (const auto &b : scene.batches) drawn += b.vertices.size() / 3;
    std::printf("  %s: %zu collision / %zu drawn triangles, bounds %.2f..%.2f x %.2f..%.2f x %.2f..%.2f m%s\n",
                name.c_str(), map.triangleCount(), drawn, scene.minimum[0], scene.maximum[0], scene.minimum[1],
                scene.maximum[1], scene.minimum[2], scene.maximum[2], aligned ? "" : "  MISALIGNED");
    if (!aligned)
        std::fprintf(stderr, "  collision bounds %.4f..%.4f x %.4f..%.4f x %.4f..%.4f\n", lo[0], hi[0], lo[1], hi[1],
                     lo[2], hi[2]);

    CHECK(scene.sourceMapped && scene.sourceScale == 1.0f);

    // The sandbox's level import is the source scene scaled and recentred:
    // level = (source - sourceOffset) * sourceScale, vertex for vertex.
    city::Scene level;
    city::LoadOptions levelOptions;
    levelOptions.buildPhysics = false;
    const bool levelLoaded = level.load(name,
                                        [&](const std::string &p, std::vector<unsigned char> &out) {
                                            if (p != name) return false;
                                            out.assign(bytes.begin(), bytes.end());
                                            return true;
                                        },
                                        log, levelOptions);
    CHECK(levelLoaded && level.sourceMapped && level.sourceScale > 0.0f);
    if (!levelLoaded) return;
    CHECK(level.batches.size() == scene.batches.size());
    const float k = level.sourceScale;
    const float levelTol = tol * k * 4.0f;
    size_t compared = 0, off = 0;
    for (size_t b = 0; b < std::min(level.batches.size(), scene.batches.size()); ++b) {
        const auto &a = scene.batches[b].vertices, &l = level.batches[b].vertices;
        if (a.size() != l.size()) {
            ++off;
            continue;
        }
        for (size_t i = 0; i < a.size(); i += 7) {
            ++compared;
            const float e[3] = {(a[i].x - level.sourceOffset[0]) * k, (a[i].y - level.sourceOffset[1]) * k,
                                (a[i].z - level.sourceOffset[2]) * k};
            if (std::fabs(e[0] - l[i].x) > levelTol || std::fabs(e[1] - l[i].y) > levelTol ||
                std::fabs(e[2] - l[i].z) > levelTol)
                ++off;
        }
    }
    CHECK(compared > 0 && off == 0);
    for (int i = 0; i < 3; ++i)
        CHECK(std::fabs((scene.minimum[size_t(i)] - level.sourceOffset[size_t(i)]) * k - level.minimum[size_t(i)]) <=
              levelTol);
    std::printf("    level import: scale %.4f, offset %.2f %.2f %.2f, %zu vertices compared%s\n", k,
                level.sourceOffset[0], level.sourceOffset[1], level.sourceOffset[2], compared, off ? "  MISMATCH" : "");
}

// mapview::MapSession conversions: map units <-> renderer metres / camera.
void checkMapSession() {
    const physics::Vec3 r = mapview::toRender({32.0f, 64.0f, 96.0f}, 32.0f);
    CHECK(r.x == 1.0f && r.y == 3.0f && r.z == -2.0f);
    for (float yaw : {0.0f, 0.5f, 1.5707964f, -2.0f, 3.0f}) {
        realms::WalkInput in;
        in.forward = 1.0f;
        const realms::WalkInput w = mapview::walkInput(in, yaw, 0.25f);
        CHECK(w.forward == 1.0f && std::fabs(w.pitch + 0.25f * 180.0f / 3.14159265f) < 1e-3f);
        // The walker's forward, taken to render space, is the camera's forward.
        const float a = w.yaw * 3.14159265f / 180.0f;
        const physics::Vec3 f = mapview::toRender({std::cos(a), std::sin(a), 0.0f}, 1.0f);
        CHECK(std::fabs(f.x + std::sin(yaw)) < 1e-4f && std::fabs(f.z + std::cos(yaw)) < 1e-4f && f.y == 0.0f);
        CHECK(std::fabs(mapview::renderYawFromMap(w.yaw) - yaw) < 1e-4f);
    }
    CHECK(mapview::materialLayer("e1u1/WATER1", 0, 0) == 4 && mapview::materialLayer("x", 0, REALMS_CONTENTS_LAVA) == 4);
    CHECK(mapview::materialLayer("city/Brick_wall", 0, 0) == 2 && mapview::materialLayer("glass", 0, 0) == 7);
    CHECK(mapview::materialLayer("concrete", 0, 0) == 1 && mapview::materialLayer("Road", 0, 0) == 0);

    // Room mesh geometry in render space equals the source-space scene.
    const std::string room = gltf_builder::roomGlb();
    realms::MeshMap map;
    std::string error, log;
    CHECK(convert("room.glb", room, map, error));
    realms::MapGeometry geometry;
    realms::meshMapGeometry(map, geometry);
    std::vector<sandbox::MeshVertex> vertices;
    mapview::toRenderVertices(geometry, kScale, vertices);
    CHECK(vertices.size() == 24);
    city::Scene scene;
    CHECK(loadScene("room.glb", room, scene, log));
    bool inside = !vertices.empty();
    for (const auto &v : vertices)
        for (int i = 0; i < 3; ++i) {
            const float c = i == 0 ? v.x : i == 1 ? v.y : v.z;
            if (c < scene.minimum[size_t(i)] || c > scene.maximum[size_t(i)]) inside = false;
        }
    CHECK(inside);
}

} // namespace

int main(int argc, char **argv) {
    checkRoom();
    checkMapSession();
    if (argc > 1) {
        std::vector<std::filesystem::path> files;
        for (const auto &e : std::filesystem::directory_iterator(argv[1]))
            if (e.path().extension() == ".glb") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        CHECK(!files.empty());
        for (const auto &f : files) checkAsset(f);
    }
    std::printf("[realms] map render alignment: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
