// Quake 2 skybox (ref_gl R_SetSky / R_DrawSkyBox): worldspawn "sky"
// (default unit1_), "skyrotate" and "skyaxis"; the six faces from
// env/<sky><rt|bk|lf|ft|up|dn>.tga, or .pcx.
#pragma once

#include "realms/Md2Model.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace realms {

class Engine;

// Uncompressed or RLE true-colour TGA (types 2 and 10, 24 or 32 bit).
bool decodeTga(const unsigned char *data, size_t size, RgbaImage &out, std::string &error);

struct MapSkySettings {
    std::string name = "unit1_"; // SP_worldspawn's default
    float rotate = 0.0f;         // degrees per second
    std::array<float, 3> axis{0.0f, 0.0f, 0.0f};
};
// The sky keys of the worldspawn in an entity lump.
MapSkySettings skySettings(const std::string &entities);

class MapSky {
public:
    // Layer order: the box axes of gl_warp.c (st_to_vec), +X, -X, +Y, -Y,
    // +Z, -Z, which show rt, lf, bk, ft, up, dn.
    static constexpr int kFaces = 6;
    static const char *const kSuffixes[kFaces];

    // All six faces, resampled to one square size (the largest side rounded
    // up to a power of two, 16 to maxSize). False and empty when one is
    // missing or unreadable (`error` names it).
    bool load(const Engine &engine, const MapSkySettings &settings, std::string *error = nullptr,
              int maxSize = 512);
    void clear();

    bool empty() const { return pixels_.empty(); }
    int size() const { return size_; }
    const MapSkySettings &settings() const { return settings_; }
    // kFaces layers of size() x size() RGBA, top row first.
    const std::vector<uint8_t> &pixels() const { return pixels_; }

    // Column-major 3x3: a render-space direction (x, y up, z = -Quake y) to
    // the box's Quake axes, turned back by skyrotate * seconds about skyaxis.
    std::array<float, 9> renderToSky(double seconds) const;
    // The face and texture coordinates (0..1, v down) seen in direction d
    // (box axes): R_DrawSkyBox inverted. The map shaders do the same.
    static void faceCoords(const float d[3], int &layer, float &u, float &v);

private:
    MapSkySettings settings_;
    std::vector<uint8_t> pixels_;
    int size_ = 0;
};

} // namespace realms
