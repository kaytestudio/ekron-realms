// Quake 2 .md2 models (IDP2 v8, as Mod_LoadAliasModel) and 8-bit PCX
// images. All counts and offsets are checked before use.
//
// buildVertices blends two frames like R_DrawAliasModel
// (old * backlerp + new * (1 - backlerp)) in model space: units, x forward,
// y left, z up. Normals are computed per frame at load time. Triangles come
// out counter-clockwise whatever winding the file uses.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace realms {

struct Md2Vertex {
    float x, y, z;    // model space, Quake 2 units
    float nx, ny, nz; // unit normal
    float u, v;       // skin coordinates, 0..1 (v down, like the PCX rows)
};

class Md2Model {
public:
    static constexpr int kMaxSkins = 32;     // MAX_MD2SKINS
    static constexpr int kMaxVerts = 2048;   // MAX_VERTS
    static constexpr int kMaxTriangles = 4096;
    static constexpr int kMaxFrames = 512;

    struct Frame {
        std::string name;
        std::vector<float> positions; // numVerts * 3, decompressed
        std::vector<float> normals;   // numVerts * 3
        std::array<float, 3> mins{}, maxs{};
    };
    struct Triangle {
        std::array<uint16_t, 3> xyz{};
        std::array<uint16_t, 3> st{};
    };

    bool parse(const unsigned char *data, size_t size, std::string &error);
    bool parse(const std::vector<unsigned char> &data, std::string &error) {
        return parse(data.data(), data.size(), error);
    }

    bool valid() const { return !frames_.empty(); }
    int skinWidth() const { return skinWidth_; }
    int skinHeight() const { return skinHeight_; }
    int numVerts() const { return numVerts_; }
    int numFrames() const { return int(frames_.size()); }
    int numTriangles() const { return int(tris_.size()); }
    bool flippedWinding() const { return flipWinding_; }
    const std::vector<std::string> &skins() const { return skins_; }
    const std::vector<Frame> &frames() const { return frames_; }
    const std::vector<Triangle> &triangles() const { return tris_; }
    // Index of the frame called `name` ("stand01"), -1 if none.
    int findFrame(const std::string &name) const;

    // Appends numTriangles() * 3 vertices. Out-of-range frames are clamped
    // to 0 like the Quake 2 renderer does ("no such frame").
    void buildVertices(int frame, int oldframe, float backlerp, std::vector<Md2Vertex> &out) const;

private:
    int skinWidth_ = 0, skinHeight_ = 0, numVerts_ = 0;
    bool flipWinding_ = false;
    std::vector<std::string> skins_;
    std::vector<std::array<int16_t, 2>> st_;
    std::vector<Triangle> tris_;
    std::vector<Frame> frames_;
};

struct RgbaImage {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba; // width * height * 4, top row first
};

// 8-bit, 1-plane RLE PCX with a 256-colour palette (0x0C + 768 bytes at the
// end). `transparentIndex` >= 0 gets alpha 0 (255 for Quake 2 pics).
bool decodePcx(const unsigned char *data, size_t size, RgbaImage &out, std::string &error,
               int transparentIndex = -1);
inline bool decodePcx(const std::vector<unsigned char> &data, RgbaImage &out, std::string &error,
                      int transparentIndex = -1) {
    return decodePcx(data.data(), data.size(), out, error, transparentIndex);
}

} // namespace realms
