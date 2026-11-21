// glTF -> mesh map converter checks (realms/MeshMap.h). No core needed:
// included by realms_bridge_test.cpp after CHECK is defined.
#pragma once

#include "gltf_builder.h"
#include "realms/MeshMap.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace meshmap_suite {

inline bool convert(const std::string &file, realms::MeshMap &out, std::string &error,
                    const std::map<std::string, std::string> &files = {}, const std::string &path = "maps/room.glb") {
    const auto reader = [&files](const std::string &p, std::vector<unsigned char> &data) {
        const auto it = files.find(p);
        if (it == files.end()) return false;
        data.assign(it->second.begin(), it->second.end());
        return true;
    };
    return realms::gltfToMeshMap(reinterpret_cast<const unsigned char *>(file.data()), file.size(), path, reader,
                                 realms::GltfMapOptions{}, out, error);
}

inline void checkRoom(const realms::MeshMap &m) {
    CHECK(m.triangleCount() == 8 && m.vertices.size() == 16 * 3 && m.triangleSurfaces.size() == 8);
    CHECK(m.surfaces.size() == 3);
    if (m.surfaces.size() == 3) {
        CHECK(m.surfaces[0].name == "concrete" && m.surfaces[0].contents == REALMS_CONTENTS_SOLID &&
              m.surfaces[0].value == 5);
        CHECK(m.surfaces[1].name == "Pool Water" && m.surfaces[1].contents == REALMS_CONTENTS_WATER);
        CHECK(m.surfaces[2].name == "concrete" && m.surfaces[2].contents == 0); // Ceiling_nocollide variant
    }
    if (m.triangleSurfaces.size() == 8)
        CHECK(m.triangleSurfaces[0] == 0 && m.triangleSurfaces[2] == 2 && m.triangleSurfaces[4] == 0 &&
              m.triangleSurfaces[6] == 1);
    // Axes: glTF (x, y, z) metres -> map (x, -z, y) * 32. The first vertex is
    // (-4, 0, -4), the first ceiling vertex is 2 m up.
    if (m.vertices.size() >= 15) {
        CHECK(m.vertices[0] == -128.0f && m.vertices[1] == 128.0f && m.vertices[2] == 0.0f);
        CHECK(m.vertices[12 + 2] == 64.0f);
    }
    // Front faces stay counter-clockwise seen from +Z, also for the mirrored
    // instance (negative determinant).
    auto normalZ = [&](size_t t) {
        const float *a = &m.vertices[size_t(m.indices[t * 3]) * 3], *b = &m.vertices[size_t(m.indices[t * 3 + 1]) * 3],
                    *c = &m.vertices[size_t(m.indices[t * 3 + 2]) * 3];
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    };
    if (m.triangleCount() == 8)
        for (size_t t = 0; t < 8; ++t) CHECK(normalZ(t) > 0);
    CHECK(m.sourceMins[0] == -4.0f && m.sourceMaxs[0] == 4.0f && m.sourceMins[1] == -1.0f && m.sourceMaxs[1] == 4.0f);
    CHECK(m.entities.find("\"classname\" \"worldspawn\"") == 2);
    CHECK(m.entities.find("\"classname\" \"info_player_start\"\n\"origin\" \"32 -64 16\"\n\"angle\" \"90\"") !=
          std::string::npos);
}

inline void runMeshMapChecks() {
    using namespace gltf_builder;
    realms::MeshMap m;
    std::string error;

    CHECK(convert(roomGlb(), m, error));
    CHECK(error.empty());
    checkRoom(m);
    CHECK(m.entities.find("\"message\" \"room.glb\"") != std::string::npos);

    // JSON .gltf with an external buffer next to it (resolved through the
    // reader, relative to the asset) and with a base64 data URI.
    CHECK(convert(roomJson("room%20data.bin"), m, error, {{"maps/room data.bin", quadBin()}}, "maps/room.gltf"));
    checkRoom(m);
    CHECK(convert(roomJson("data:application/octet-stream;base64," + base64(quadBin())), m, error));
    checkRoom(m);
    CHECK(!convert(roomJson("missing.bin"), m, error) && error.find("couldn't load buffer") != std::string::npos);
    CHECK(!convert(roomJson("../secret.bin"), m, error) && error.find("unsafe") != std::string::npos);
    CHECK(m.indices.empty() && m.surfaces.empty()); // failures leave an empty map

    // Scale option and the core-facing description.
    {
        realms::GltfMapOptions o;
        o.scale = 64.0f;
        const std::string f = roomGlb();
        CHECK(realms::gltfToMeshMap(reinterpret_cast<const unsigned char *>(f.data()), f.size(), "a.glb", {}, o, m,
                                    error));
        CHECK(m.vertices[0] == -256.0f);
        std::vector<RealmsMeshSurface> storage;
        const RealmsMeshDesc d = realms::meshMapDesc(m, storage, "a.glb", 7);
        CHECK(d.struct_size == sizeof(RealmsMeshDesc) && d.num_triangles == 8 && d.num_vertices == 16);
        CHECK(d.num_surfaces == 3 && std::string(d.surfaces[1].name) == "Pool Water" && d.checksum == 7);
        CHECK(std::string(d.entities) == m.entities && std::string(d.name) == "a.glb");
        o.scale = 0.0f;
        CHECK(!realms::gltfToMeshMap(reinterpret_cast<const unsigned char *>(f.data()), f.size(), "a.glb", {}, o, m,
                                     error));
    }

    // Strips, fans and non-indexed primitives; points are ignored.
    {
        const std::string json =
            "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":["
            "{\"attributes\":{\"POSITION\":0},\"mode\":5},{\"attributes\":{\"POSITION\":0},\"mode\":6},"
            "{\"attributes\":{\"POSITION\":0}},{\"attributes\":{\"POSITION\":0},\"mode\":0}]}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteLength\":48}],\"buffers\":[{\"byteLength\":48}]}";
        CHECK(convert(glb(json, quadBin().substr(0, 48)), m, error));
        CHECK(m.triangleCount() == 2 + 2 + 1); // strip 2, fan 2, list 1
        CHECK(m.surfaces.size() == 1 && m.surfaces[0].name == "default");
        CHECK(m.entities == "{\n\"classname\" \"worldspawn\"\n\"message\" \"room.glb\"\n}\n");
    }

    // Quantized (normalized i16) positions with a byte stride.
    {
        std::string bin;
        const int16_t q[4][4] = {{-32767, 0, -32767, 0}, {-32767, 0, 32767, 0}, {32767, 0, 32767, 0}, {0, 0, 0, 0}};
        bin.append(reinterpret_cast<const char *>(q), sizeof q);
        const std::string json =
            "{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"KHR_mesh_quantization\"],"
            "\"nodes\":[{\"mesh\":0,\"scale\":[2,2,2]}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5122,\"normalized\":true,\"count\":3,\"type\":\"VEC3\"}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteLength\":32,\"byteStride\":8}],\"buffers\":[{\"byteLength\":32}]}";
        CHECK(convert(glb(json, bin), m, error));
        CHECK(m.triangleCount() == 1 && m.vertices.size() == 9 && m.vertices[0] == -64.0f && m.vertices[1] == 64.0f);
    }

    // Malformed input is rejected with a reason.
    const std::string good = roomGlb();
    CHECK(!convert("", m, error) && !error.empty());
    CHECK(!convert("{\"asset\":{\"version\":\"1.0\"}}", m, error) && error == "not a glTF 2.0 asset");
    CHECK(!convert("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]}", m,
                   error) &&
          error.find("KHR_draco_mesh_compression") != std::string::npos);
    CHECK(!convert(std::string(200, '['), m, error) && error.find("deep") != std::string::npos);
    CHECK(!convert(good.substr(0, good.size() - 8), m, error)); // truncated BIN chunk
    {
        std::string bad = roomJson();
        bad.replace(bad.find("\"count\":6"), 9, "\"count\":9");
        CHECK(!convert(glb(bad, quadBin()), m, error) && error == "accessor is out of range");
        bad = roomJson();
        bad.replace(bad.find("\"indices\":1"), 11, "\"indices\":7");
        CHECK(!convert(glb(bad, quadBin()), m, error) && error == "bad accessor index");
        std::string bin = quadBin();
        bin[48] = 9; // index 9 of 4 vertices
        CHECK(!convert(glb(roomJson(), bin), m, error) && error == "index is out of range");
        bad = roomJson();
        bad.replace(bad.find("\"nodes\":[0,1,2,3,4]"), 19, "\"nodes\":[0,1,2,3,9]");
        CHECK(!convert(glb(bad, quadBin()), m, error) && error == "bad scene");
        // Cycles in the node graph hit the depth limit instead of looping.
        bad = roomJson();
        bad.replace(bad.find("{\"name\":\"Floor\",\"mesh\":0}"), 25, "{\"name\":\"Floor\",\"children\":[0]}");
        CHECK(!convert(glb(bad, quadBin()), m, error) && error == "node hierarchy is too deep");
        // Out of map space.
        bad = roomJson();
        bad.replace(bad.find("\"translation\":[0,2,0]"), 21, "\"translation\":[0,90000,0]");
        CHECK(!convert(glb(bad, quadBin()), m, error) && error.find("map space") != std::string::npos);
    }

    // Mutation fuzz: random byte changes must never crash or hang.
    uint32_t seed = 0x9e3779b9u;
    auto rnd = [&seed]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        std::string f = good;
        const int edits = 1 + int(rnd() % 4);
        for (int k = 0; k < edits; ++k) f[rnd() % f.size()] = char(rnd() & 0xFF);
        if (rnd() % 8 == 0) f.resize(rnd() % f.size());
        if (convert(f, m, error)) {
            ++accepted;
            CHECK(m.indices.size() == m.triangleSurfaces.size() * 3);
        }
    }
    CHECK(accepted > 0);
}

} // namespace meshmap_suite
