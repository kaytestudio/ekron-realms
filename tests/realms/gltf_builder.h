// Builds tiny glTF 2.0 assets for the mesh map tests.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gltf_builder {

inline void put32(std::string &s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF);
}

// GLB container: header, JSON chunk (space padded), BIN chunk (zero padded).
inline std::string glb(std::string json, std::string bin) {
    while (json.size() % 4) json += ' ';
    while (bin.size() % 4) bin += '\0';
    std::string out;
    put32(out, 0x46546C67u);
    put32(out, 2);
    put32(out, uint32_t(12 + 8 + json.size() + (bin.empty() ? 0 : 8 + bin.size())));
    put32(out, uint32_t(json.size()));
    put32(out, 0x4E4F534Au);
    out += json;
    if (!bin.empty()) {
        put32(out, uint32_t(bin.size()));
        put32(out, 0x004E4942u);
        out += bin;
    }
    return out;
}

inline std::string base64(const std::string &in) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8) | uint8_t(in[i + 2]);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t v = uint32_t(uint8_t(in[i])) << 16;
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8);
        out += t[v >> 18];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// 8 x 8 m floor quad at y = 0 (CCW seen from +Y) followed by u16 indices:
// 48 bytes of positions + 12 bytes of indices.
inline std::string quadBin() {
    const float v[12] = {-4, 0, -4, -4, 0, 4, 4, 0, 4, 4, 0, -4};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    std::string bin(reinterpret_cast<const char *>(v), sizeof v);
    bin.append(reinterpret_cast<const char *>(idx), sizeof idx);
    return bin;
}

// Scene: "Floor" (collides), "Ceiling_nocollide" 2 m up (render only), a
// mirrored instance 4 m up (scale -1 on x), a water quad 1 m down and an
// info_player_start. `bufferUri` empty means the GLB BIN chunk.
inline std::string roomJson(const std::string &bufferUri = std::string()) {
    std::string buffer = "{\"byteLength\":60";
    if (!bufferUri.empty()) buffer += ",\"uri\":\"" + bufferUri + "\"";
    buffer += "}";
    return std::string("{\"asset\":{\"version\":\"2.0\",\"generator\":\"realms test\"},\"scene\":0,") +
           "\"scenes\":[{\"nodes\":[0,1,2,3,4]}]," +
           "\"nodes\":[{\"name\":\"Floor\",\"mesh\":0}," +
           "{\"name\":\"Ceiling_nocollide\",\"mesh\":0,\"translation\":[0,2,0]}," +
           "{\"name\":\"Mirror\",\"mesh\":0,\"matrix\":[-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,4,0,1]}," +
           "{\"name\":\"Pool\",\"mesh\":1,\"translation\":[0,-1,0]}," +
           "{\"name\":\"info_player_start.001\",\"translation\":[1,0.5,2],\"extras\":{\"realms_angle\":90}}]," +
           "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}," +
           "{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":1}]}]," +
           "\"materials\":[{\"name\":\"concrete\",\"extras\":{\"realms_value\":5}},{\"name\":\"Pool Water\"}]," +
           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]," +
           "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48}," +
           "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12}]," + "\"buffers\":[" + buffer + "]}";
}

inline std::string roomGlb() { return glb(roomJson(), quadBin()); }

// A game map: a 16 x 16 m floor with Quake 2 point entities named after
// their classnames, 0.75 m (24 units) up. The soldier faces map +Y (node
// rotation, 90 degrees about glTF +Y); the health has extras that become
// keys or are dropped; the explobox sits under a moved parent. func_door
// (a brush entity) and "monster_Bad Name" are not exported. Scene extras go
// to worldspawn.
inline std::string outpostJson() {
    return std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,") +
           "\"scenes\":[{\"nodes\":[0,1,2,3,4,5,6,7,8],"
           "\"extras\":{\"message\":\"Outpost\",\"sounds\":2,\"nextmap\":\"base1\"}}]," +
           "\"nodes\":[{\"name\":\"Floor\",\"mesh\":0,\"scale\":[2,1,2]}," +
           "{\"name\":\"info_player_start.001\",\"translation\":[-3,0.75,0]}," +
           "{\"name\":\"weapon_shotgun\",\"translation\":[-1,0.75,0]}," +
           "{\"name\":\"item_health.002\",\"translation\":[1,0.75,2],\"extras\":{\"realms_contents\":\"none\"," +
           "\"model\":\"*1\",\"bad key\":\"x\",\"_color\":[1,0.5,0],\"wait\":1.5,\"flag\":true,\"note\":\"}say \\\"hi\\\"{\"}}," +
           "{\"name\":\"monster_soldier\",\"translation\":[5,0.75,0],\"rotation\":[0,0.70710678,0,0.70710678]}," +
           "{\"name\":\"func_door\",\"translation\":[0,1,0]}," +
           "{\"name\":\"monster_Bad Name\",\"translation\":[0,1,0]}," +
           "{\"name\":\"Light\",\"translation\":[0,3,0],\"extras\":{\"light\":300}}," +
           "{\"name\":\"Props\",\"translation\":[2,0,-2],\"children\":[9]}," +
           "{\"name\":\"misc_explobox\",\"translation\":[0,0.75,0],\"extras\":{\"angle\":45}}]," +
           "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}]," +
           "\"materials\":[{\"name\":\"concrete\"}]," +
           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
           "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]," +
           "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48}," +
           "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12}],\"buffers\":[{\"byteLength\":60}]}";
}

inline std::string outpostGlb() { return glb(outpostJson(), quadBin()); }

// One node / one mesh / one material GLB from glTF-space (Y up, metres)
// positions and u16 triangle indices. No spawn entity.
inline std::string meshGlb(const std::vector<float> &positions, const std::vector<uint16_t> &indices,
                           const std::string &name = "Mesh") {
    std::string bin(reinterpret_cast<const char *>(positions.data()), positions.size() * sizeof(float));
    const size_t posBytes = bin.size();
    bin.append(reinterpret_cast<const char *>(indices.data()), indices.size() * sizeof(uint16_t));
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i + 2 < positions.size(); i += 3)
        for (size_t k = 0; k < 3; ++k) {
            if (positions[i + k] < mn[k]) mn[k] = positions[i + k];
            if (positions[i + k] > mx[k]) mx[k] = positions[i + k];
        }
    auto vec = [](const float *v) {
        return "[" + std::to_string(v[0]) + "," + std::to_string(v[1]) + "," + std::to_string(v[2]) + "]";
    };
    const std::string json =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],") +
        "\"nodes\":[{\"name\":\"" + name + "\",\"mesh\":0}]," +
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}]," +
        "\"materials\":[{\"name\":\"concrete\"}]," +
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(positions.size() / 3) +
        ",\"type\":\"VEC3\",\"min\":" + vec(mn) + ",\"max\":" + vec(mx) + "}," +
        "{\"bufferView\":1,\"componentType\":5123,\"count\":" + std::to_string(indices.size()) +
        ",\"type\":\"SCALAR\"}]," + "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" +
        std::to_string(posBytes) + "},{\"buffer\":0,\"byteOffset\":" + std::to_string(posBytes) +
        ",\"byteLength\":" + std::to_string(bin.size() - posBytes) + "}]," + "\"buffers\":[{\"byteLength\":" +
        std::to_string(bin.size()) + "}]}";
    return glb(json, bin);
}

// A yard: 32 x 32 m ground with a closed 4 x 4 x 3 m hut (four walls and a
// roof) in the middle, no spawn entity. The bounds' centre is inside the
// hut, so "drop from the centre" lands boxed in on the hut's floor.
inline std::string yardGlb() {
    std::vector<float> p = {-16, 0, -16, -16, 0, 16, 16, 0, 16, 16, 0, -16}; // ground 0..3
    const float h = 2.0f, top = 3.0f;
    const float corners[4][2] = {{-h, -h}, {-h, h}, {h, h}, {h, -h}}; // hut 4..7 bottom, 8..11 top
    for (float y : {0.0f, top})
        for (const auto &c : corners) {
            p.push_back(c[0]);
            p.push_back(y);
            p.push_back(c[1]);
        }
    std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};
    for (uint16_t i = 0; i < 4; ++i) { // walls
        const uint16_t a = uint16_t(4 + i), b = uint16_t(4 + (i + 1) % 4);
        const uint16_t c = uint16_t(b + 4), d = uint16_t(a + 4);
        idx.insert(idx.end(), {a, b, c, a, c, d});
    }
    idx.insert(idx.end(), {8, 9, 10, 8, 10, 11}); // roof
    return meshGlb(p, idx, "Yard");
}

// Two crossed walls and nothing to stand on.
inline std::string wallsGlb() {
    const std::vector<float> p = {-4, 0, 0, 4, 0, 0, 4, 3, 0, -4, 3, 0, 0, 0, -4, 0, 0, 4, 0, 3, 4, 0, 3, -4};
    return meshGlb(p, {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7}, "Walls");
}

} // namespace gltf_builder
