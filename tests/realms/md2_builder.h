// Writes Quake 2 alias models (IDP2 v8) and 8-bit RLE PCX images in memory,
// so the tests don't need id Software's data (see bsp_builder.h for maps).
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace md2_builder {

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

struct ModelDef {
    int skinWidth = 64, skinHeight = 32;
    std::vector<std::string> skins = {"models/test/skin.pcx"};
    std::vector<std::array<int16_t, 2>> st;               // texture coordinates (pixels)
    std::vector<std::array<uint16_t, 6>> tris;            // xyz[3], st[3]
    std::vector<std::vector<std::array<float, 3>>> frames; // per frame, per vertex positions
    std::vector<std::string> frameNames;
    bool clockwise = true; // Quake 2's own winding (seen from outside)
};

// An axis-aligned box from `mn` to `mx` (8 vertices, 12 triangles), with
// one frame per entry of `boxes`.
inline ModelDef boxModel(const std::vector<std::pair<std::array<float, 3>, std::array<float, 3>>> &boxes,
                         bool clockwise = true) {
    ModelDef def;
    def.clockwise = clockwise;
    def.st = {{0, 0}, {63, 0}, {63, 31}, {0, 31}};
    // counter-clockwise from outside
    static const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
    for (const auto &f : faces) {
        std::array<uint16_t, 6> a{uint16_t(f[0]), uint16_t(f[1]), uint16_t(f[2]), 0, 1, 2};
        std::array<uint16_t, 6> b{uint16_t(f[0]), uint16_t(f[2]), uint16_t(f[3]), 0, 2, 3};
        if (clockwise) {
            std::swap(a[1], a[2]);
            std::swap(a[4], a[5]);
            std::swap(b[1], b[2]);
            std::swap(b[4], b[5]);
        }
        def.tris.push_back(a);
        def.tris.push_back(b);
    }
    int n = 0;
    for (const auto &bx : boxes) {
        const auto &mn = bx.first;
        const auto &mx = bx.second;
        def.frames.push_back({{mn[0], mn[1], mn[2]}, {mx[0], mn[1], mn[2]}, {mx[0], mx[1], mn[2]}, {mn[0], mx[1], mn[2]},
                              {mn[0], mn[1], mx[2]}, {mx[0], mn[1], mx[2]}, {mx[0], mx[1], mx[2]}, {mn[0], mx[1], mx[2]}});
        def.frameNames.push_back("frame" + std::to_string(++n));
    }
    return def;
}

// Compresses each frame like the Quake 2 model tools: per-axis scale and
// translate, 8-bit coordinates (positions come back within scale / 2).
inline std::vector<unsigned char> writeMd2(const ModelDef &def) {
    const int numXyz = def.frames.empty() ? 0 : int(def.frames[0].size());
    const int frameSize = 40 + numXyz * 4;
    const int ofsSkins = 68;
    const int ofsSt = ofsSkins + int(def.skins.size()) * 64;
    const int ofsTris = ofsSt + int(def.st.size()) * 4;
    const int ofsFrames = ofsTris + int(def.tris.size()) * 12;
    const int ofsGl = ofsFrames + int(def.frames.size()) * frameSize;
    const int ofsEnd = ofsGl + 4; // one empty glcmd list terminator
    std::vector<unsigned char> b(size_t(ofsEnd), 0);
    std::memcpy(b.data(), "IDP2", 4);
    putInt(b, 4, 8);
    putInt(b, 8, def.skinWidth);
    putInt(b, 12, def.skinHeight);
    putInt(b, 16, frameSize);
    putInt(b, 20, int(def.skins.size()));
    putInt(b, 24, numXyz);
    putInt(b, 28, int(def.st.size()));
    putInt(b, 32, int(def.tris.size()));
    putInt(b, 36, 1);
    putInt(b, 40, int(def.frames.size()));
    putInt(b, 44, ofsSkins);
    putInt(b, 48, ofsSt);
    putInt(b, 52, ofsTris);
    putInt(b, 56, ofsFrames);
    putInt(b, 60, ofsGl);
    putInt(b, 64, ofsEnd);
    for (size_t i = 0; i < def.skins.size(); i++)
        std::memcpy(&b[size_t(ofsSkins) + i * 64], def.skins[i].c_str(), std::min<size_t>(63, def.skins[i].size()));
    for (size_t i = 0; i < def.st.size(); i++) {
        putShort(b, size_t(ofsSt) + i * 4, def.st[i][0]);
        putShort(b, size_t(ofsSt) + i * 4 + 2, def.st[i][1]);
    }
    for (size_t i = 0; i < def.tris.size(); i++)
        for (int k = 0; k < 6; k++) putShort(b, size_t(ofsTris) + i * 12 + size_t(k) * 2, def.tris[i][size_t(k)]);
    for (size_t f = 0; f < def.frames.size(); f++) {
        const size_t at = size_t(ofsFrames) + f * size_t(frameSize);
        float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
        for (const auto &v : def.frames[f])
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], v[size_t(k)]);
                mx[k] = std::max(mx[k], v[size_t(k)]);
            }
        float scale[3];
        for (int k = 0; k < 3; k++) {
            scale[k] = (mx[k] - mn[k]) / 255.0f;
            if (scale[k] <= 0) scale[k] = 1.0f / 255.0f;
            putFloat(b, at + size_t(k) * 4, scale[k]);
            putFloat(b, at + 12 + size_t(k) * 4, mn[k]);
        }
        const std::string name = f < def.frameNames.size() ? def.frameNames[f] : "frame";
        std::memcpy(&b[at + 24], name.c_str(), std::min<size_t>(15, name.size()));
        for (size_t i = 0; i < def.frames[f].size(); i++)
            for (int k = 0; k < 3; k++) {
                const float q = std::round((def.frames[f][i][size_t(k)] - mn[k]) / scale[k]);
                b[at + 40 + i * 4 + size_t(k)] = uint8_t(std::min(255.0f, std::max(0.0f, q)));
            }
    }
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

} // namespace md2_builder
