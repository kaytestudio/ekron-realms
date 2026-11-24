// Quake 3 .md3 models (IDP3 version 15, as R_LoadMD3), the model format of
// map mode. All counts and offsets are checked before use.
//
// The Quake 2 game still names its models "models/.../tris.md2";
// md3PathFor turns that into the .md3 at the same path, which is the file
// GameModelCache loads. .md2 files themselves are not read.
//
// A model has frames (shared by all its surfaces), tags per frame, and
// surfaces, each with its own triangles, texture coordinates, vertices per
// frame and shaders. buildVertices blends two frames like R_DrawAliasModel
// (old * backlerp + new * (1 - backlerp)) in model space: units, x forward,
// y left, z up. Triangles come out counter-clockwise seen from outside,
// whatever winding the file uses (decided once, from the stored normals).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace realms {

struct ModelVertex {
    float x, y, z;    // model space, Quake units
    float nx, ny, nz; // unit normal
    float u, v;       // texture coordinates (v down, top row first); may repeat outside 0..1
};

class Md3Model {
public:
    static constexpr int kVersion = 15;
    static constexpr int kMaxFrames = 1024;    // MD3_MAX_FRAMES
    static constexpr int kMaxTags = 16;        // MD3_MAX_TAGS
    static constexpr int kMaxSurfaces = 32;    // MD3_MAX_SURFACES
    static constexpr int kMaxShaders = 256;    // MD3_MAX_SHADERS
    static constexpr int kMaxVerts = 4096;     // MD3_MAX_VERTS, per surface
    static constexpr int kMaxTriangles = 8192; // MD3_MAX_TRIANGLES, per surface
    static constexpr float kXyzScale = 1.0f / 64.0f; // MD3_XYZ_SCALE

    struct Frame {
        std::string name;
        std::array<float, 3> mins{}, maxs{}, localOrigin{};
        float radius = 0;
    };
    struct Tag {
        std::string name;
        std::array<float, 3> origin{};
        std::array<std::array<float, 3>, 3> axis{}; // forward, left, up
    };
    struct Surface {
        std::string name;
        std::vector<std::string> shaders;              // texture paths
        int numVerts = 0;
        std::vector<std::array<uint32_t, 3>> triangles; // vertex indices
        std::vector<std::array<float, 2>> st;           // per vertex
        std::vector<float> positions;                   // numFrames * numVerts * 3
        std::vector<float> normals;                     // numFrames * numVerts * 3, unit
    };

    bool parse(const unsigned char *data, size_t size, std::string &error);
    bool parse(const std::vector<unsigned char> &data, std::string &error) {
        return parse(data.data(), data.size(), error);
    }

    bool valid() const { return !frames_.empty() && !surfaces_.empty(); }
    const std::string &name() const { return name_; }
    int numFrames() const { return int(frames_.size()); }
    int numTags() const { return numTags_; }
    int numSurfaces() const { return int(surfaces_.size()); }
    int numTriangles() const; // all surfaces
    bool flippedWinding() const { return flipWinding_; }
    const std::vector<Frame> &frames() const { return frames_; }
    const std::vector<Surface> &surfaces() const { return surfaces_; }
    // Index of the frame called `name`, -1 if none.
    int findFrame(const std::string &name) const;
    // Tag `name` in `frame` (clamped like buildVertices), nullptr if none.
    const Tag *tag(int frame, const std::string &name) const;
    // The texture of `surface` for an entity's skinnum: its skinnum'th
    // shader, else its first; "" when the surface has none. This keeps Quake
    // 2's skin numbers working (pain skins, CTF colours).
    const std::string &skin(int surface, int skinnum) const;

    // Appends the triangles of `surface` (3 vertices each). Out-of-range
    // frames are clamped to 0 like the Quake 2 renderer does.
    void buildVertices(int surface, int frame, int oldframe, float backlerp, std::vector<ModelVertex> &out) const;
    // All surfaces, in file order.
    void buildVertices(int frame, int oldframe, float backlerp, std::vector<ModelVertex> &out) const;

private:
    std::string name_;
    int numTags_ = 0;
    bool flipWinding_ = false;
    std::vector<Frame> frames_;
    std::vector<Tag> tags_; // numFrames * numTags
    std::vector<Surface> surfaces_;
};

// The model file for a model name: "models/x/tris.md2" -> "models/x/tris.md3"
// (any case of ".md2"); other names are returned unchanged.
std::string md3PathFor(const std::string &modelName);
// True for names ending in .md2 or .md3: the alias models a game can name.
bool isAliasModelName(const std::string &name);

} // namespace realms
