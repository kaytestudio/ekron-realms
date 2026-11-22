// Realms Engine - Quake 2 alias models (.md2) and PCX images.
//
// Md2Model::parse reads an IDP2 version 8 model (models/<name>/tris.md2 in
// a Quake 2 pak) the way the Quake 2 renderer does (Mod_LoadAliasModel):
// skins, texture coordinates, triangles and the compressed vertex frames.
// Every count, offset and index is checked first, so a corrupt file is
// rejected with an error instead of being read out of bounds.
//
// buildVertices produces a triangle list for one blend of two frames, as
// R_DrawAliasModel does: position = old * backlerp + new * (1 - backlerp),
// in model space (Quake 2 units, x forward, y left, z up). Normals are the
// area-weighted vertex normals of each frame, computed at load time (instead
// of the anorms table). Triangles come out counter-clockwise seen from
// outside, whichever winding the file uses (decided on frame 0).
//
// decodePcx decodes the 8-bit RLE PCX files Quake 2 uses for skins and
// pics (palette at the end of the file) to RGBA.
//
// No renderer types: the desktop client uploads the result to OpenGL, and
// Android / iOS can do the same with GLES / Metal.
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
