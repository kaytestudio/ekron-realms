// Writes Quake 3 models (IDP3 version 15) and 8-bit RLE PCX images in
// memory, so the tests don't need game data (see bsp_builder.h for maps).
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace md3_builder {

inline void putInt(std::vector<unsigned char> &b, size_t at, int32_t v) {
    for (int i = 0; i < 4; i++) b[at + size_t(i)] = uint8_t(uint32_t(v) >> (8 * i));
}
inline void putShort(std::vector<unsigned char> &b, size_t at, int v) {
    b[at] = uint8_t(v & 0xff);
    b[at + 1] = uint8_t((v >> 8) & 0xff);
}
inline void putFloat(std::vector<unsigned char> &b, size_t at, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    putInt(b, at, int32_t(u));
}

// One surface of a model: triangles (vertex indices), per-vertex texture
// coordinates, and per frame, per vertex positions and normals.
struct SurfaceDef {
    std::string name = "body";
    std::vector<std::string> shaders = {"models/test/skin.pcx"};
    std::vector<std::array<int32_t, 3>> tris;
    std::vector<std::array<float, 2>> st;
    std::vector<std::vector<std::array<float, 3>>> positions; // [frame][vertex]
    std::vector<std::vector<std::array<float, 3>>> normals;   // [frame][vertex], unit
};

struct ModelDef {
    std::string name = "models/test/tris.md3";
    std::vector<std::string> frameNames;
    std::vector<SurfaceDef> surfaces;
    std::vector<std::string> tagNames;                    // tags per frame
    std::vector<std::vector<std::array<float, 3>>> tagOrigins; // [frame][tag]
};

// An axis-aligned box from `mn` to `mx` as one surface (24 vertices, flat
// normals, 12 triangles), one frame per entry of `boxes`. `clockwise`:
// Quake 3's own winding seen from outside.
inline SurfaceDef boxSurface(const std::vector<std::pair<std::array<float, 3>, std::array<float, 3>>> &boxes,
                             bool clockwise = true) {
    SurfaceDef s;
    // counter-clockwise from outside
    static const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
    static const float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}};
    static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int f = 0; f < 6; f++) {
        const int b = f * 4;
        std::array<int32_t, 3> t1{b, b + 1, b + 2}, t2{b, b + 2, b + 3};
        if (clockwise) {
            std::swap(t1[1], t1[2]);
            std::swap(t2[1], t2[2]);
        }
        s.tris.push_back(t1);
        s.tris.push_back(t2);
        for (int k = 0; k < 4; k++) s.st.push_back({uv[k][0], uv[k][1]});
    }
    for (const auto &bx : boxes) {
        const auto &mn = bx.first;
        const auto &mx = bx.second;
        const float c[8][3] = {{mn[0], mn[1], mn[2]}, {mx[0], mn[1], mn[2]}, {mx[0], mx[1], mn[2]}, {mn[0], mx[1], mn[2]},
                               {mn[0], mn[1], mx[2]}, {mx[0], mn[1], mx[2]}, {mx[0], mx[1], mx[2]}, {mn[0], mx[1], mx[2]}};
        std::vector<std::array<float, 3>> pos, nrm;
        for (int f = 0; f < 6; f++)
            for (int k = 0; k < 4; k++) {
                const float *p = c[faces[f][k]];
                pos.push_back({p[0], p[1], p[2]});
                nrm.push_back({normals[f][0], normals[f][1], normals[f][2]});
            }
        s.positions.push_back(pos);
        s.normals.push_back(nrm);
    }
    return s;
}

// A one-surface box model, one frame per box ("frame1", "frame2", ...).
inline ModelDef boxModel(const std::vector<std::pair<std::array<float, 3>, std::array<float, 3>>> &boxes,
                         bool clockwise = true) {
    ModelDef def;
    def.surfaces.push_back(boxSurface(boxes, clockwise));
    for (size_t i = 0; i < boxes.size(); i++) def.frameNames.push_back("frame" + std::to_string(i + 1));
    return def;
}

// The latitude / longitude bytes the model tools write.
inline int16_t encodeNormal(const std::array<float, 3> &n) {
    constexpr float kPi = 3.14159265358979323846f;
    if (n[0] == 0 && n[1] == 0) return int16_t(n[2] > 0 ? 0 : 128);
    int lng = int(std::lround(std::acos(n[2]) * 255.0f / (2 * kPi))) & 0xff;
    int lat = int(std::lround(std::atan2(n[1], n[0]) * 255.0f / (2 * kPi))) & 0xff;
    return int16_t(uint16_t(lat << 8 | lng));
}

// IDP3 version 15: header, frames, tags, then each surface (header,
// shaders, triangles, st, xyz/normals). Positions are stored in 1/64 units.
inline std::vector<unsigned char> writeMd3(const ModelDef &def) {
    const int numFrames = int(def.frameNames.size());
    const int numTags = int(def.tagNames.size());
    const int ofsFrames = 108;
    const int ofsTags = ofsFrames + numFrames * 56;
    const int ofsSurfaces = ofsTags + numFrames * numTags * 112;
    std::vector<unsigned char> b(size_t(ofsSurfaces), 0);
    std::memcpy(b.data(), "IDP3", 4);
    putInt(b, 4, 15);
    std::memcpy(&b[8], def.name.c_str(), std::min<size_t>(63, def.name.size()));
    putInt(b, 76, numFrames);
    putInt(b, 80, numTags);
    putInt(b, 84, int(def.surfaces.size()));
    putInt(b, 92, ofsFrames);
    putInt(b, 96, ofsTags);
    putInt(b, 100, ofsSurfaces);
    for (int f = 0; f < numFrames; f++) {
        const size_t at = size_t(ofsFrames) + size_t(f) * 56;
        float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
        for (const SurfaceDef &s : def.surfaces)
            for (const auto &v : s.positions[size_t(f)])
                for (int k = 0; k < 3; k++) {
                    mn[k] = std::min(mn[k], v[size_t(k)]);
                    mx[k] = std::max(mx[k], v[size_t(k)]);
                }
        for (int k = 0; k < 3; k++) {
            putFloat(b, at + size_t(k) * 4, mn[k]);
            putFloat(b, at + 12 + size_t(k) * 4, mx[k]);
        }
        putFloat(b, at + 36, 64.0f);
        std::memcpy(&b[at + 40], def.frameNames[size_t(f)].c_str(), std::min<size_t>(15, def.frameNames[size_t(f)].size()));
        for (int t = 0; t < numTags; t++) {
            const size_t ta = size_t(ofsTags) + (size_t(f) * size_t(numTags) + size_t(t)) * 112;
            std::memcpy(&b[ta], def.tagNames[size_t(t)].c_str(), std::min<size_t>(63, def.tagNames[size_t(t)].size()));
            for (int k = 0; k < 3; k++) putFloat(b, ta + 64 + size_t(k) * 4, def.tagOrigins[size_t(f)][size_t(t)][size_t(k)]);
            for (int a = 0; a < 3; a++) putFloat(b, ta + 76 + size_t(a * 3 + a) * 4, 1.0f); // identity axis
        }
    }
    for (const SurfaceDef &s : def.surfaces) {
        const size_t base = b.size();
        const int numVerts = s.positions.empty() ? 0 : int(s.positions[0].size());
        const int ofsShaders = 108;
        const int ofsTris = ofsShaders + int(s.shaders.size()) * 68;
        const int ofsSt = ofsTris + int(s.tris.size()) * 12;
        const int ofsXyz = ofsSt + numVerts * 8;
        const int ofsEnd = ofsXyz + numFrames * numVerts * 8;
        b.resize(base + size_t(ofsEnd), 0);
        std::memcpy(&b[base], "IDP3", 4);
        std::memcpy(&b[base + 4], s.name.c_str(), std::min<size_t>(63, s.name.size()));
        putInt(b, base + 72, numFrames);
        putInt(b, base + 76, int(s.shaders.size()));
        putInt(b, base + 80, numVerts);
        putInt(b, base + 84, int(s.tris.size()));
        putInt(b, base + 88, ofsTris);
        putInt(b, base + 92, ofsShaders);
        putInt(b, base + 96, ofsSt);
        putInt(b, base + 100, ofsXyz);
        putInt(b, base + 104, ofsEnd);
        for (size_t i = 0; i < s.shaders.size(); i++) {
            std::memcpy(&b[base + size_t(ofsShaders) + i * 68], s.shaders[i].c_str(), std::min<size_t>(63, s.shaders[i].size()));
            putInt(b, base + size_t(ofsShaders) + i * 68 + 64, int(i));
        }
        for (size_t i = 0; i < s.tris.size(); i++)
            for (int k = 0; k < 3; k++) putInt(b, base + size_t(ofsTris) + i * 12 + size_t(k) * 4, s.tris[i][size_t(k)]);
        for (int i = 0; i < numVerts; i++) {
            putFloat(b, base + size_t(ofsSt) + size_t(i) * 8, s.st[size_t(i)][0]);
            putFloat(b, base + size_t(ofsSt) + size_t(i) * 8 + 4, s.st[size_t(i)][1]);
        }
        for (int f = 0; f < numFrames; f++)
            for (int i = 0; i < numVerts; i++) {
                const size_t at = base + size_t(ofsXyz) + (size_t(f) * size_t(numVerts) + size_t(i)) * 8;
                for (int k = 0; k < 3; k++)
                    putShort(b, at + size_t(k) * 2, int(std::lround(s.positions[size_t(f)][size_t(i)][size_t(k)] * 64.0f)));
                putShort(b, at + 6, encodeNormal(s.normals[size_t(f)][size_t(i)]));
            }
    }
    putInt(b, 104, int(b.size()));
    return b;
}

// 8-bit RLE PCX with a grey-ramp palette (index i = (i, 255 - i, i / 2)).
inline std::vector<unsigned char> writePcx(int w, int h, const std::vector<uint8_t> &indices, bool padLine = false) {
    std::vector<unsigned char> b(128, 0);
    b[0] = 0x0a;
    b[1] = 5;
    b[2] = 1;
    b[3] = 8;
    putShort(b, 8, w - 1);
    putShort(b, 10, h - 1);
    putShort(b, 12, 72);
    putShort(b, 14, 72);
    b[65] = 1;
    const int bpl = w + (padLine ? 1 : 0);
    putShort(b, 66, bpl);
    putShort(b, 68, 1);
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < bpl) {
            const uint8_t v = x < w ? indices[size_t(y * w + x)] : 0;
            int run = 1;
            while (x + run < bpl && run < 63 && (x + run < w ? indices[size_t(y * w + x + run)] : 0) == v) run++;
            if (run > 1 || (v & 0xc0) == 0xc0) {
                b.push_back(uint8_t(0xc0 | run));
                b.push_back(v);
            } else {
                b.push_back(v);
            }
            x += run;
        }
    }
    b.push_back(0x0c);
    for (int i = 0; i < 256; i++) {
        b.push_back(uint8_t(i));
        b.push_back(uint8_t(255 - i));
        b.push_back(uint8_t(i / 2));
    }
    return b;
}

} // namespace md3_builder
